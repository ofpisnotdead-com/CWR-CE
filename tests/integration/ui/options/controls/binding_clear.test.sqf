// A Keyboard & Mouse binding must be clearable, respecting the selected slot:
// clearing the primary leaves the alt in place, it does not shift the alt up.
// Backspace/Delete on a focused row, the controller Delete (Y), and right-click
// all clear via OnBindingCleared.
//
// Default On-foot bindings, no capture step needed:
//   row 1 Move forward   W  (511) / UP   (512)
//   row 2 Move back      S  (521) / DOWN (522)
//   row 3 Strafe left    A  (531) / LEFT (532)
// Scancodes: Down 81, Right 79, Left 80, Delete 76, Backspace 42. Gamepad Y = 3.
// Empty cell renders as "—".

#include "../../../helpers/options_preamble.sqf"
#include "../../../helpers/controls_preamble.sqf"

triClickText "Keyboard & Mouse"
triAssertEq [(triDisplay), 9099]
triSimFrames 2

triAssertEq [(triControlText 511), "W"]
triAssertEq [(triControlText 512), "UP"]
triAssertEq [(triControlText 521), "S"]

// --- Part 1: keyboard, clearing the primary must leave the alt in place ---

triSendKey 81
triSimFrames 2
triSendKey 76
triSimFrames 3
triAssertEq [(triControlText 511), "—"]
triAssertEq [(triControlText 512), "UP"]

triSendKey 79
triSimFrames 2
triSendKey 42
triSimFrames 3
triAssertEq [(triControlText 512), "—"]
triAssertEq [(triControlText 511), "—"]

// --- Part 2: controller, Y (Delete) clears the selected alt cell (row 3) ---

triSendKey 81
triSimFrames 1
triSendKey 81
triSimFrames 2
triSendKey 79
triSimFrames 2
triGpadButton 3
triSimFrames 3
triAssertEq [(triControlText 532), "—"]
triAssertEq [(triControlText 531), "A"]

// --- Part 3: mouse, a right-click on the primary clears it, the alt stays ---
// Aim at the row label: it sits in the primary zone (the alt overlay only covers
// the right of the row), so the click lands on the primary regardless of the
// notebook projection jitter. Click a few times to absorb an occasional miss
// into the gap. Clearing the primary must leave the alt (positional).

triCursorMoveControl 520
triSimFrames 2
triMouseRight 1
triSimFrames 2
triMouseRight 0
triSimFrames 2
triCursorMoveControl 520
triSimFrames 2
triMouseRight 1
triSimFrames 2
triMouseRight 0
triSimFrames 2
triCursorMoveControl 520
triSimFrames 2
triMouseRight 1
triSimFrames 2
triMouseRight 0
triSimFrames 3
triAssertEq [(triControlText 521), "—"]
triAssertEq [(triControlText 522), "DOWN"]

triEndTest
