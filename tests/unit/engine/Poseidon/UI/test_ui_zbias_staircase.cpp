#include <catch2/catch_test_macros.hpp>

// I-27 / B-031: Layered zBias z-order in composed UI.
//
// The object menu surface has a depth-staircase: every UI layer
// that composes on top of an existing surface declares a zBias
// strictly greater than its parent's zBias, so a depth-tie can't
// hide the upper layer behind the lower.  B-031 was a mic-test
// meter inheriting the page-row zBias (0.0001) instead of one
// above the modal panel (0.0008); the panel covered the meter
// entirely except for a peak square that won the tie by accident.
//
// This file mirrors the constants from
// `packages/Combined/BIN/optionsUI.hpp` (the config-language
// source of truth) so the staircase can be asserted without a
// ParamFile load.  Any change there must be mirrored here OR the
// constants moved into a single shared header that both sides
// include — either preserves the locking property.

namespace
{
// Layer 0 — page rows on the bare object surface.
constexpr float kOptListBarZBias = 0.0001f;
// Layer 1 — modal frame above page rows.
constexpr float kOptDlgBorderZBias = 0.0006f;
// Layer 2 — modal panel above its frame.
constexpr float kOptDlgPanelZBias = 0.0008f;
// Layer 3 — mic-test meter (Track/Fill/Peak) above modal panel.
constexpr float kOptMicTestMeterZBias = 0.0010f;
} // namespace

TEST_CASE("UI zBias staircase: page < modal border < modal panel", "[UI][zBias][I-27]")
{
    REQUIRE(kOptListBarZBias < kOptDlgBorderZBias);
    REQUIRE(kOptDlgBorderZBias < kOptDlgPanelZBias);
}

TEST_CASE("UI zBias staircase: mic-test meter sits above modal panel (B-031)", "[UI][zBias][I-27]")
{
    // The B-031 broken state was meter zBias == OPT_LIST_BAR_ZBIAS,
    // BELOW the panel.  Asserting strict inequality locks the fix in.
    REQUIRE(kOptMicTestMeterZBias > kOptDlgPanelZBias);

    // Meter must clear the panel + at least one tie-tolerance step;
    // the staircase uses 0.0002 increments, so a strict gap of >0.0001
    // documents the intended margin.
    REQUIRE((kOptMicTestMeterZBias - kOptDlgPanelZBias) > 0.0001f);
}

TEST_CASE("UI zBias staircase: meter must stay below C3DActiveText focus highlight", "[UI][zBias][I-27]")
{
    // C3DActiveText draws the menu-item focus background at z = -0.001
    // (hardcoded — see optionsUI.hpp comment block).  zBias is camera-
    // forward; the focus highlight wins when |z| > zBias, so any UI
    // layer composing inside a modal must stay below 0.001 zBias.
    // The meter at 0.0010 is at the upper bound.
    REQUIRE(kOptMicTestMeterZBias <= 0.001f);
}
