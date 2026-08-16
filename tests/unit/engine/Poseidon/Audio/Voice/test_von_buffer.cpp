#include <catch2/catch_test_macros.hpp>
#include <Poseidon/Audio/Voice/VonBuffer.hpp>
#include <vector>
#include <cstring>
#include <numeric>
#include <stdint.h>

using namespace Poseidon;
static constexpr int FRAME = 320;

static void fillFrame(int16_t* buf, int16_t value)
{
    for (int i = 0; i < FRAME; ++i)
        buf[i] = value;
}

TEST_CASE("VoNJitterBuffer empty pull returns 0", "[VoN][buffer]")
{
    VoNJitterBuffer jb(4, FRAME);
    REQUIRE(jb.empty());
    int16_t out[FRAME];
    REQUIRE(jb.pull(out, FRAME) == 0); // not started
}

TEST_CASE("VoNJitterBuffer in-order push/pull", "[VoN][buffer]")
{
    VoNJitterBuffer jb(4, FRAME);

    int16_t f0[FRAME], f1[FRAME], f2[FRAME];
    fillFrame(f0, 100);
    fillFrame(f1, 200);
    fillFrame(f2, 300);

    jb.push(0, f0, FRAME);
    jb.push(FRAME, f1, FRAME);
    jb.push(FRAME * 2, f2, FRAME);
    REQUIRE(jb.buffered() == 3);

    int16_t out[FRAME];
    REQUIRE(jb.pull(out, FRAME) == FRAME);
    REQUIRE(out[0] == 100);

    REQUIRE(jb.pull(out, FRAME) == FRAME);
    REQUIRE(out[0] == 200);

    REQUIRE(jb.pull(out, FRAME) == FRAME);
    REQUIRE(out[0] == 300);
}

TEST_CASE("VoNJitterBuffer out-of-order reordering", "[VoN][buffer]")
{
    VoNJitterBuffer jb(4, FRAME);

    int16_t f0[FRAME], f1[FRAME], f2[FRAME];
    fillFrame(f0, 10);
    fillFrame(f1, 20);
    fillFrame(f2, 30);

    // Push out of order: frame2 first, then frame0, then frame1
    jb.push(FRAME * 2, f2, FRAME);
    jb.push(0, f0, FRAME);
    jb.push(FRAME, f1, FRAME);

    int16_t out[FRAME];
    REQUIRE(jb.pull(out, FRAME) == FRAME);
    REQUIRE(out[0] == 10); // frame 0

    REQUIRE(jb.pull(out, FRAME) == FRAME);
    REQUIRE(out[0] == 20); // frame 1

    REQUIRE(jb.pull(out, FRAME) == FRAME);
    REQUIRE(out[0] == 30); // frame 2
}

TEST_CASE("VoNJitterBuffer duplicate rejected", "[VoN][buffer]")
{
    VoNJitterBuffer jb(4, FRAME);

    int16_t f0[FRAME];
    fillFrame(f0, 42);

    jb.push(0, f0, FRAME);
    jb.push(0, f0, FRAME); // duplicate
    REQUIRE(jb.buffered() == 1);

    int16_t out[FRAME];
    REQUIRE(jb.pull(out, FRAME) == FRAME);
    REQUIRE(out[0] == 42);
}

TEST_CASE("VoNJitterBuffer gap inserts silence", "[VoN][buffer]")
{
    VoNJitterBuffer jb(8, FRAME);

    int16_t f0[FRAME], f2[FRAME];
    fillFrame(f0, 100);
    fillFrame(f2, 300);

    // Push frame 0 and frame 2 (skip frame 1)
    jb.push(0, f0, FRAME);
    jb.push(FRAME * 2, f2, FRAME);

    int16_t out[FRAME];
    REQUIRE(jb.pull(out, FRAME) == FRAME);
    REQUIRE(out[0] == 100); // frame 0

    REQUIRE(jb.pull(out, FRAME) == FRAME);
    REQUIRE(out[0] == 0); // frame 1 missing → silence

    REQUIRE(jb.pull(out, FRAME) == FRAME);
    REQUIRE(out[0] == 300); // frame 2
}

TEST_CASE("VoNJitterBuffer too-old packet discarded", "[VoN][buffer]")
{
    VoNJitterBuffer jb(4, FRAME);

    int16_t f[FRAME];
    fillFrame(f, 10);

    // Push and pull 5 frames to advance _nextOrigin well past capacity
    for (int i = 0; i < 5; ++i)
    {
        jb.push(FRAME * i, f, FRAME);
        int16_t out[FRAME];
        jb.pull(out, FRAME);
    }
    // _nextOrigin is now FRAME*5

    // Push origin=0, which is 5 frames behind (>= capacity of 4)
    jb.push(0, f, FRAME);
    REQUIRE(jb.buffered() == 0); // rejected as too old
}

TEST_CASE("VoNJitterBuffer resyncs to a far-ahead origin", "[VoN][buffer]")
{
    VoNJitterBuffer jb(4, FRAME);

    int16_t f0[FRAME], f10[FRAME];
    fillFrame(f0, 1);
    fillFrame(f10, 77);

    jb.push(0, f0, FRAME); // sets _nextOrigin = 0
    // A frame beyond the window (slot 10, capacity 4) means the stream
    // skipped ahead; buffered frames behind it can no longer form a
    // contiguous stream — the buffer must jump to the new position.
    jb.push(FRAME * 10, f10, FRAME);
    REQUIRE(jb.buffered() == 1);

    int16_t out[FRAME];
    REQUIRE(jb.pull(out, FRAME) == FRAME);
    REQUIRE(out[0] == 77); // the far frame plays; frame 0 was dropped
}

// Regression: a push-to-talk burst that follows a large capture backlog
// flood used to wedge the buffer permanently — the flood advanced the
// sender origin far past the play cursor, every later frame was "too far
// ahead", and the sender stayed mute for the rest of the session.
TEST_CASE("VoNJitterBuffer keeps accepting live audio after a flood beyond capacity", "[VoN][buffer]")
{
    VoNJitterBuffer jb(8, FRAME);

    int16_t f[FRAME];
    int16_t out[FRAME];

    // Burst 1: three frames sent and played in lock-step.
    for (int i = 0; i < 3; ++i)
    {
        fillFrame(f, static_cast<int16_t>(100 + i));
        jb.push(static_cast<uint64_t>(FRAME) * i, f, FRAME);
        REQUIRE(jb.pull(out, FRAME) == FRAME);
    }

    // Burst 2 arrives as a 50-frame backlog flood in one network pump,
    // origins continuing monotonically from burst 1.
    for (int i = 3; i < 53; ++i)
    {
        fillFrame(f, static_cast<int16_t>(1000 + i));
        jb.push(static_cast<uint64_t>(FRAME) * i, f, FRAME);
    }

    // Live speech continues after the flood.
    fillFrame(f, 9999);
    jb.push(static_cast<uint64_t>(FRAME) * 53, f, FRAME);

    // Draining the buffer must eventually play the live frame.
    bool sawLive = false;
    for (int i = 0; i < 16 && jb.pull(out, FRAME) == FRAME; ++i)
        sawLive = sawLive || out[0] == 9999;
    REQUIRE(sawLive);
}

// audio-invariants A-31 — every gap-fill emitted by pull() increments
// underrunGapFrames so callers / load regressions can observe network-
// loss events without screen-scraping logs.  reset() rewinds the
// counter.
TEST_CASE("VoNJitterBuffer underrun counter tracks gap-fill events", "[VoN][buffer][A-31]")
{
    VoNJitterBuffer jb(8, FRAME);
    REQUIRE(jb.underrunGapFrames() == 0);

    int16_t f0[FRAME];
    int16_t f3[FRAME];
    fillFrame(f0, 11);
    fillFrame(f3, 33);
    // Push frame 0 and frame 3 (skip 1 + 2).
    jb.push(0, f0, FRAME);
    jb.push(FRAME * 3, f3, FRAME);

    int16_t out[FRAME];
    // Frame 0 — real data, no underrun.
    REQUIRE(jb.pull(out, FRAME) == FRAME);
    CHECK(out[0] == 11);
    CHECK(jb.underrunGapFrames() == 0);

    // Frame 1 — gap, counter ticks.
    REQUIRE(jb.pull(out, FRAME) == FRAME);
    CHECK(out[0] == 0);
    CHECK(jb.underrunGapFrames() == 1);

    // Frame 2 — another gap.
    REQUIRE(jb.pull(out, FRAME) == FRAME);
    CHECK(out[0] == 0);
    CHECK(jb.underrunGapFrames() == 2);

    // Frame 3 — real data, counter does NOT increment.
    REQUIRE(jb.pull(out, FRAME) == FRAME);
    CHECK(out[0] == 33);
    CHECK(jb.underrunGapFrames() == 2);

    // reset() rewinds the counter (it's a session-cumulative probe
    // tied to the buffer's stream lifetime).
    jb.reset();
    CHECK(jb.underrunGapFrames() == 0);
}

TEST_CASE("VoNJitterBuffer reset", "[VoN][buffer]")
{
    VoNJitterBuffer jb(4, FRAME);

    int16_t f[FRAME];
    fillFrame(f, 99);
    jb.push(0, f, FRAME);
    REQUIRE(jb.buffered() == 1);

    jb.reset();
    REQUIRE(jb.empty());

    int16_t out[FRAME];
    REQUIRE(jb.pull(out, FRAME) == 0); // not started after reset
}
