#include <Poseidon/Foundation/Strings/LocaleCollate.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace Poseidon::Foundation;

// UTF-8 byte strings for the accented names under test. Kept as explicit escapes so
// the test is independent of source-file encoding.
namespace
{
const char* kCsla = "\xC4\x8C"
                    "SLA"; // "ČSLA"  (Č = U+010C)
const char* kSkoda = "\xC5\xA0"
                     "koda";             // "Škoda" (Š = U+0160)
const char* kZan = "\xC5\xBD\xC3\xA1n";  // "Žán"   (Ž = U+017D, á = U+00E1)
const char* kCafeAccent = "caf\xC3\xA9"; // "café" (é = U+00E9)
} // namespace

// The fold fallback is deterministic and locale-free, so it can be pinned exactly.
TEST_CASE("FoldCompareUtf8 groups accented letters with their base letter", "[locale][collate]")
{
    // Č folds to C: sorts after B, before D, and — the actual bug — before Z, not last.
    CHECK(FoldCompareUtf8(kCsla, "Bravo") > 0);
    CHECK(FoldCompareUtf8(kCsla, "Delta") < 0);
    CHECK(FoldCompareUtf8(kCsla, "Zulu") < 0);
    CHECK(FoldCompareUtf8(kCsla, "Alpha") > 0);

    // Within the C group, the second letter decides.
    CHECK(FoldCompareUtf8(kCsla, "CWA") < 0); // Č==C, then S < W

    // Š/Ž fold to S/Z, so they land in those groups rather than after everything.
    CHECK(FoldCompareUtf8(kSkoda, "Sedan") > 0); // Š==S, then k > e
    CHECK(FoldCompareUtf8(kZan, "Zebra") < 0);   // Ž==Z, then á(a) < e
}

TEST_CASE("FoldCompareUtf8 is case-insensitive with a stable accent tie-break", "[locale][collate]")
{
    CHECK(FoldCompareUtf8("abc", "ABC") == 0);
    CHECK(FoldCompareUtf8("MoD", "mod") == 0);

    // Base and accented fold equal, so the raw code point breaks the tie: plain
    // 'e' (0x65) sorts before 'é' (0xE9), and ASCII 'S' before 'Š'.
    CHECK(FoldCompareUtf8("cafe", kCafeAccent) < 0);
    CHECK(FoldCompareUtf8("Skoda", kSkoda) < 0);
    CHECK(FoldCompareUtf8("hostile", "hostile") == 0);
}

// CollateUtf8 uses OS collation when available and the fold otherwise; the "ČSLA is
// not last" invariant must hold either way, so it is safe to assert on any platform.
TEST_CASE("CollateUtf8 keeps CSLA in the C group on any platform", "[locale][collate]")
{
    CHECK(CollateUtf8(kCsla, "Bravo") > 0);
    CHECK(CollateUtf8(kCsla, "Delta") < 0);
    CHECK(CollateUtf8(kCsla, "Zulu") < 0);

    // Plain ASCII ordering is stable regardless of locale.
    CHECK(CollateUtf8("apple", "banana") < 0);
    CHECK(CollateUtf8("banana", "apple") > 0);
    CHECK(CollateUtf8("mod", "mod") == 0);
}
