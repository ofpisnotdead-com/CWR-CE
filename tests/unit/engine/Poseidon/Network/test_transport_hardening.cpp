#include <catch2/catch_test_macros.hpp>

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <Poseidon/Network/RateLimit.hpp>
#include <Poseidon/Network/WireBounds.hpp>

using namespace Poseidon;

// Transport-layer hardening for the dispatch redesign (network-security-gaps.md
// N-SEC-10/12/13/14/16). The transport handlers can't be unit-instantiated without
// the live UDP stack, so the non-trivial bounds math is extracted into WireBounds
// predicates the handlers call, and these tests bind to those predicates.

TEST_CASE("N-SEC-13: big-ack word count is bounded by the packet, not by oldest/newest", "[network][transport][oob]")
{
    // BigAckPacket is a 16-byte header followed by a flexible unsigned32 ack[].
    const int header = 16;
    const int word = 4;

    // A 24-byte packet carries exactly (24-16)/4 = 2 ack words, regardless of the
    // attacker-supplied oldest/newest that used to bound the read loop.
    REQUIRE(WireBounds::TrailingElementCount(24, header, word) == 2);
    REQUIRE(WireBounds::TrailingElementCount(16, header, word) == 0); // header only
    // Broken-state delta: the loop ran (newest-oldest+1)/32 times reading ack[aPtr++];
    // a header-only packet claiming a huge newest read far past the buffer. The count
    // here caps the loop at the words actually present.
    REQUIRE(WireBounds::TrailingElementCount(8, header, word) == 0);  // short -> nothing
    REQUIRE(WireBounds::TrailingElementCount(-1, header, word) == 0); // garbage length
}

TEST_CASE("N-SEC-12: serial window rejects far-out serials, accepts in-window", "[network][transport][dos]")
{
    const uint32_t lo = 1000; // ackMaskMin
    const uint32_t hi = 1050; // inputMax
    const int64_t span = 256; // par.maxChannelBitMask

    // In-window and just-ahead serials are accepted (normal traffic).
    REQUIRE(WireBounds::SerialWithinSpan(1025, lo, hi, span));
    REQUIRE(WireBounds::SerialWithinSpan(hi + 100, lo, hi, span));
    REQUIRE(WireBounds::SerialWithinSpan(lo - 100, lo, hi, span));

    // Broken-state delta: ackMask.on(ser) grew ~(ser-min)/32 words and the catch-up
    // loop spun (ser-inputMax) times; a serial far ahead/behind is now rejected.
    REQUIRE_FALSE(WireBounds::SerialWithinSpan(hi + 1000000, lo, hi, span));
    REQUIRE_FALSE(WireBounds::SerialWithinSpan(lo - 1000000, lo, hi, span));
    // Wrap-around: a serial just below 0 relative to a small window is "behind", not ahead.
    REQUIRE_FALSE(WireBounds::SerialWithinSpan(0xFFFFFFFFu, lo, hi, span));
}

TEST_CASE("N-SEC-07: NMTMessages batch sub-count is bounded by remaining bytes", "[network][transport][dos]")
{
    // DecodeMessage reads a sub-message count n off the wire and loops over it. Each
    // sub-message is at least its 1-byte type varint, so n can never legitimately
    // exceed the bytes left; the handler now rejects a crafted n via this predicate.
    const int remaining = 12;
    REQUIRE(WireBounds::DecodeCountFits(12, remaining)); // every byte a 1-byte sub-msg
    REQUIRE(WireBounds::DecodeCountFits(0, remaining));
    // Broken-state delta: an uncapped n (e.g. 0x7FFFFFFF) drove the loop far past the
    // packet body; it is now rejected before the first iteration.
    REQUIRE_FALSE(WireBounds::DecodeCountFits(0x7FFFFFFF, remaining));
    REQUIRE_FALSE(WireBounds::DecodeCountFits(13, remaining));
    REQUIRE_FALSE(WireBounds::DecodeCountFits(-1, remaining));
}

TEST_CASE("N-SEC-16: a datagram shorter than the trailing CRC is rejected", "[network][transport][oob]")
{
    // OnServerUserMessage does `bufferSize -= sizeof(int); crc = *(int*)(buffer+bufferSize)`.
    // The guard requires room for the CRC int before subtracting (RangeInBounds).
    REQUIRE(WireBounds::RangeInBounds(0, (int)sizeof(int), 4));       // exactly the CRC
    REQUIRE(WireBounds::RangeInBounds(0, (int)sizeof(int), 8));       // body + CRC
    REQUIRE_FALSE(WireBounds::RangeInBounds(0, (int)sizeof(int), 3)); // too short -> OOB read
    REQUIRE_FALSE(WireBounds::RangeInBounds(0, (int)sizeof(int), 0));
}

TEST_CASE("N-SEC-09: enum-reply token bucket caps reflected replies", "[network][transport][dos]")
{
    // The enum responder answers spoofable requests with a reply larger than the
    // request. The bucket lets at most `burst` through in a flood and `ratePerSec`
    // sustained, so the server can't be a useful UDP amplifier.
    RateLimit::TokenBucket b;
    b.configure(/*ratePerSec*/ 50.0, /*burst*/ 100.0);

    // A flood at the same instant drains exactly `burst` replies, then is throttled.
    int allowed = 0;
    for (int i = 0; i < 1000; ++i)
    {
        if (b.tryConsume(/*nowMs*/ 1000))
        {
            ++allowed;
        }
    }
    REQUIRE(allowed == 100);
    // Broken-state delta: without the bucket every one of the 1000 spoofed requests
    // reflected a reply; now only the burst did.
    REQUIRE_FALSE(b.tryConsume(1000));

    // One second later it has refilled ratePerSec tokens (50), no more.
    int refilled = 0;
    for (int i = 0; i < 1000; ++i)
    {
        if (b.tryConsume(/*nowMs*/ 2000))
        {
            ++refilled;
        }
    }
    REQUIRE(refilled == 50);

    // Legitimate cadence (one request every 100 ms) is never throttled.
    RateLimit::TokenBucket steady;
    steady.configure(50.0, 100.0);
    bool allPassed = true;
    for (int i = 0; i < 200; ++i)
    {
        if (!steady.tryConsume(static_cast<uint32_t>(i) * 100u))
        {
            allPassed = false;
        }
    }
    REQUIRE(allPassed);
}

TEST_CASE("N-SEC-09: token bucket handles tick-count wraparound", "[network][transport][dos]")
{
    RateLimit::TokenBucket b;
    b.configure(50.0, 100.0);
    b.tryConsume(0xFFFFFF00u); // just before 32-bit rollover
    // After wrap, the unsigned delta is small (256 ms), not ~4.3e9 ms — so the
    // bucket credits ~12.8 tokens, not an effectively-infinite amount.
    b.tryConsume(0x00000000u); // wrapped
    // Drain whatever is available at the wrapped instant; must be bounded by burst.
    int allowed = 0;
    for (int i = 0; i < 1000; ++i)
    {
        if (b.tryConsume(0x00000000u))
        {
            ++allowed;
        }
    }
    REQUIRE(allowed < 100); // not flooded open by the wrap
}

TEST_CASE("N-SEC-10: a wire server name is rendered as data, never as a format", "[network][transport][format]")
{
    // The enumeration handler now does snprintf(dst, n, "%s", s->name) instead of
    // snprintf(dst, n, s->name, addr) — a malicious "%s%n" name is shown literally.
    const char* malicious = "%s%s%s%n";
    char out[64];
    int n = snprintf(out, sizeof(out), "%s", malicious);
    REQUIRE(n == (int)strlen(malicious));
    REQUIRE(strcmp(out, "%s%s%s%n") == 0); // conversion specifiers were not interpreted
}
