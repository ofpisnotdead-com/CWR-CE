// Mouse sensitivity page: Back must not perturb a just-set slider value.
//
// Regression for tester report in Fizzy #265:
//   Change mouse sensitivity, click Back, reopen Mouse.  The slider must
//   still show the chosen value.  Esc already behaved correctly; this locks
//   the mouse-click Back path.

#include "../../../helpers/options_preamble.sqf"
#include "../../../helpers/controls_preamble.sqf"

triClickText "Mouse"
triAssertEq [(triDisplay), 9099]
triAssertIncludes [(triVisibleTexts), "Mouse sensitivity X"]

// Sensitivity X is row 3 / slot 3 after the Mouse section header. Its slider click overlay is 537 and
// visible text is 532.  Use the real pointer path, not triClick, so hover,
// down/up, and Back-click routing match user input.
triCursorMoveControl 537
triSimFrames 6
triMouseLeft 1
triSimFrames 6
triMouseLeft 0
triSimFrames 3

private _chosen = triControlText 532;
if (_chosen != "1.25x") exitWith {
    format ["FAIL:setup_mouse_sensitivity_not_50 got='%1'", _chosen]
};

// Leave through the pinned Back/Close row (slot 8 hover IDC 583), not Esc,
// again using real pointer motion/click.
triCursorMoveControl 583
triSimFrames 6
if ((triAssert [(triGetControlFocused 583)]) != "OK") exitWith { "FAIL:back_footer_not_focused_before_click" };
triMouseLeft 1
triSimFrames 6
triMouseLeft 0
triAssertIncludes [(triVisibleTexts), "Keyboard & Mouse"]

triClickText "Mouse"
triAssertIncludes [(triVisibleTexts), "Mouse sensitivity X"]

private _afterBack = triControlText 532;
if (_afterBack != _chosen) exitWith {
    format ["FAIL:mouse_sensitivity_changed_after_back before=%1 after=%2", _chosen, _afterBack]
};

triEndTest
