#include <catch2/catch_test_macros.hpp>

#include <climits>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Network/WireBounds.hpp>

using namespace Poseidon;

// Decode-check primitives for the table-driven dispatcher (RC2/RC3 closure).
// These are the seam migrated message handlers route wire-read counts/lengths/
// indices through, replacing the per-handler "trust the wire then Resize/memcpy/
// arr[i]" pattern. Each predicate is tested at its boundary because that is
// exactly where the attacker lives.

TEST_CASE("WireBounds::ValidCount caps and rejects negatives", "[network][dispatch][wirebounds]")
{
    REQUIRE(WireBounds::ValidCount(0, 100));
    REQUIRE(WireBounds::ValidCount(100, 100));
    REQUIRE_FALSE(WireBounds::ValidCount(101, 100));
    REQUIRE_FALSE(WireBounds::ValidCount(-1, 100));
    REQUIRE_FALSE(WireBounds::ValidCount(INT_MIN, 100));
}

TEST_CASE("WireBounds::MulFitsInt rejects overflowing count*size", "[network][dispatch][wirebounds]")
{
    REQUIRE(WireBounds::MulFitsInt(1000, 16));
    REQUIRE(WireBounds::MulFitsInt(0, 16));
    // Broken-state delta: a naive `count*size` here wraps to a small/negative int
    // and a downstream Resize under-allocates, then the loop writes past it.
    REQUIRE_FALSE(WireBounds::MulFitsInt(INT_MAX, 16));
    REQUIRE_FALSE(WireBounds::MulFitsInt(INT_MAX / 4 + 1, 4));
    REQUIRE_FALSE(WireBounds::MulFitsInt(-1, 16));
    REQUIRE_FALSE(WireBounds::MulFitsInt(10, 0));
    // exactly at the boundary fits.
    REQUIRE(WireBounds::MulFitsInt(INT_MAX / 4, 4));
}

TEST_CASE("WireBounds::RangeInBounds guards offset+span vs size", "[network][dispatch][wirebounds]")
{
    REQUIRE(WireBounds::RangeInBounds(0, 10, 10));
    REQUIRE(WireBounds::RangeInBounds(5, 5, 10));
    REQUIRE_FALSE(WireBounds::RangeInBounds(6, 5, 10));
    REQUIRE_FALSE(WireBounds::RangeInBounds(-1, 5, 10));
    REQUIRE_FALSE(WireBounds::RangeInBounds(0, -1, 10));
    // offset+span must not overflow into a false pass.
    REQUIRE_FALSE(WireBounds::RangeInBounds(INT_MAX, INT_MAX, 10));
}

TEST_CASE("WireBounds::ValidIdentifier accepts well-formed names, rejects junk", "[network][dispatch][wirebounds]")
{
    // publicVariable name guard (N-SEC-15): a wire name must be a bounded identifier
    // before it reaches the script var table.
    REQUIRE(WireBounds::ValidIdentifier("myVar", 256));
    REQUIRE(WireBounds::ValidIdentifier("_x", 256));
    REQUIRE(WireBounds::ValidIdentifier("a1_b2", 256));
    REQUIRE(WireBounds::ValidIdentifier("A", 256));

    REQUIRE_FALSE(WireBounds::ValidIdentifier(nullptr, 256));  // null
    REQUIRE_FALSE(WireBounds::ValidIdentifier("", 256));       // empty
    REQUIRE_FALSE(WireBounds::ValidIdentifier("1abc", 256));   // leading digit
    REQUIRE_FALSE(WireBounds::ValidIdentifier("a b", 256));    // space
    REQUIRE_FALSE(WireBounds::ValidIdentifier("a;drop", 256)); // punctuation/injection
    REQUIRE_FALSE(WireBounds::ValidIdentifier("a.b", 256));    // dot

    // Length bound: a 10-char name with maxLen 8 is rejected; within bound passes.
    REQUIRE_FALSE(WireBounds::ValidIdentifier("abcdefghij", 8));
    REQUIRE(WireBounds::ValidIdentifier("abcdefgh", 8));
}

TEST_CASE("AutoArray::AtOrNull bounds-checks in release builds", "[foundation][containers][wirebounds]")
{
    AutoArray<int> a;
    a.Add(10);
    a.Add(20);
    a.Add(30);

    REQUIRE(a.AtOrNull(0) != nullptr);
    REQUIRE(*a.AtOrNull(0) == 10);
    REQUIRE(*a.AtOrNull(2) == 30);

    // Broken-state delta: operator[] (AssertDebug) returns _data[i] with no check
    // under NDEBUG; AtOrNull returns nullptr for the same out-of-range index.
    REQUIRE(a.AtOrNull(3) == nullptr);
    REQUIRE(a.AtOrNull(-1) == nullptr);
    REQUIRE(a.AtOrNull(INT_MAX) == nullptr);

    *a.AtOrNull(1) = 99;
    REQUIRE(a[1] == 99);

    const AutoArray<int>& ca = a;
    REQUIRE(ca.AtOrNull(1) != nullptr);
    REQUIRE(*ca.AtOrNull(1) == 99);
    REQUIRE(ca.AtOrNull(5) == nullptr);
}
