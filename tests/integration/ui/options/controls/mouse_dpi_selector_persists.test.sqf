// Mouse DPI selector: the new preset stepper must appear on the Mouse page,
// cycle through presets, and keep its value across a Back / reopen.
//
// Companion to mouse_sensitivity_back_persists — same Back-persistence shape,
// but for the Mouse DPI stepper added for HiDPI mice.  The row label has a
// source-side English fallback ("Mouse DPI"), so this passes against
// unmodified game data even before the stringtable ships the localized key.

#include "../../../helpers/options_preamble.sqf"
#include "../../../helpers/controls_preamble.sqf"

triClickText "Mouse"
triAssertEq [(triDisplay), 9099]
triAssertIncludes [(triVisibleTexts), "Mouse DPI"]

// Mouse DPI is row 4 / slot 4: label 540, stepper value 541, Next arrow 549.
if ((triControlText 540) != "Mouse DPI") exitWith {
    format ["FAIL:dpi_label actual='%1'", triControlText 540]
};

// Default is "Off" (normalization disabled = classic, backwards compatible).
if ((triControlText 541) != "Off") exitWith {
    format ["FAIL:dpi_default_not_off actual='%1'", triControlText 541]
};

// Cycle Next (Off -> 400 -> 800 -> 1200 -> 1600) until the value reads 1600.
for "_i" from 0 to 9 do { if ((triControlText 541) == "1600") exitWith {}; triClick 549; triSimFrames 2 };
private _chosen = triControlText 541;
if (_chosen != "1600") exitWith {
    format ["FAIL:could_not_select_1600 actual='%1'", _chosen]
};

// Leave through the pinned Back/Close footer (slot 8 hover IDC 583), using the
// real pointer path, then confirm we're back on the Controls page.
triCursorMoveControl 583
triSimFrames 6
if ((triAssert [(triGetControlFocused 583)]) != "OK") exitWith { "FAIL:back_footer_not_focused_before_click" };
triMouseLeft 1
triSimFrames 6
triMouseLeft 0
triAssertIncludes [(triVisibleTexts), "Keyboard & Mouse"]

// Reopen Mouse — the DPI selection must have survived.
triClickText "Mouse"
triAssertIncludes [(triVisibleTexts), "Mouse DPI"]

private _afterBack = triControlText 541;
if (_afterBack != _chosen) exitWith {
    format ["FAIL:dpi_changed_after_back before=%1 after=%2", _chosen, _afterBack]
};

triEndTest
