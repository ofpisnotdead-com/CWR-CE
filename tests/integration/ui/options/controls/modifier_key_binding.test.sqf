// Modifier keys must be bindable from the Keyboard & Mouse controls page.
// InterpretKey used to refuse every modifier-only press, so Left Ctrl / Shift /
// Alt could never be captured.
//
// Part 1: a lone modifier captures and lands in the binding cell.
// Part 2: a real key pressed while a modifier is held binds as "Mod+Key", not
// clobbering the modifier. keys[LCTRL] reads as held only on the second keyboard
// Update after key-down, so sim several frames before the second key
// (triSimFrames is deterministic; a wall-clock triWait starves under load).
//
// Scancodes: Down 81, Return 40, Left Ctrl 224, W 26.
// Capture modal IDCs: Save 9301, Cancel 9302 (default focus), Title 9380.
// KB&M list: row 1 (Move forward) -> label 510, primary cell 511.

#include "../../../helpers/options_preamble.sqf"
#include "../../../helpers/controls_preamble.sqf"

triClickText "Keyboard & Mouse"
triAssertEq [(triDisplay), 9099]
triSimFrames 2

triAssertEq [(triControlText 510), "Move forward"]
triAssertEq [(triControlText 511), "W"]

// --- Part 1: a lone modifier (Left Ctrl) must capture and assign ---

triSendKey 81
triSimFrames 2
triSendKey 40
triSimFrames 3
triAssert [(triGetControlFocused 9302)]
triAssertEq [(triControlText 9380), "Press a key"]

triSendKey 224
triSimFrames 3
triAssert [(triGetControlFocused 9301)]
triAssertEq [(triControlText 9380), "Captured: Left Ctrl"]

triClick 9301
triSimFrames 3
triAssertIncludes [(triVisibleTexts), "Keyboard & Mouse"]
triAssertEq [(triControlText 511), "Left Ctrl"]

// --- Part 2: modifier + key must still form a combo (guards TryUpgradeToCombo) ---

triSendKey 40
triSimFrames 3
triAssert [(triGetControlFocused 9302)]

triKeyDown 224
triSimFrames 6
triKeyDown 26
triSimFrames 4
triKeyUp 26
triKeyUp 224
triSimFrames 2
triAssert [(triGetControlFocused 9301)]
triAssertEq [(triControlText 9380), "Captured: Left Ctrl+W"]

triClick 9301
triSimFrames 3
triAssertIncludes [(triVisibleTexts), "Keyboard & Mouse"]
triAssertEq [(triControlText 511), "Left Ctrl+W"]

triEndTest
