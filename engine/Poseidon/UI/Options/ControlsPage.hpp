#pragma once

// Controls section index — landing page for input settings.
//
// Rows: Keyboard & Mouse (binding rebinder), Mouse (sensitivity /
// Y-invert / button swap), Gamepad (binding rebinder), Gamepad Tuning
// (deadzones / sensitivity), Reset all to presets, Close.  Mirrors
// IndexPage's FlatMenuPage shape — see optionsShell.hpp's
// "RscOptionsPageControls" for the resource bundle.

#include <Poseidon/UI/Options/FlatMenuPage.hpp>

namespace Poseidon
{
class ControlsPage : public FlatMenuPage
{
  public:
    ControlsPage() : FlatMenuPage(kCycle, kCycleSize) {}

    const char* TitleText() const override;               // STR_DISP_OPT_CONTROLS
    int DefaultFocusIdc() const override { return 1401; } // Keyboard & Mouse — top row
    const char* ResourceClassName() const override { return "RscOptionsPageControls"; }

  protected:
    bool OnNav(OptionsShell& shell, int idc) override;

  private:
    // Cycle order matches the visible row layout in RscOptionsPageControls.
    // 1401 KB&M · 1402 Mouse · 1405 Gamepad · 1406 Gamepad Tuning ·
    // 1403 Reset all to presets · 1404 Close.  Stay in sync with the
    // resource bundle when adding rows.
    static constexpr int kCycle[] = {1401, 1402, 1405, 1406, 1403, 1404};
    static constexpr int kCycleSize = sizeof(kCycle) / sizeof(kCycle[0]);
};

} // namespace Poseidon
