// Long key names in the narrow Keyboard & Mouse binding cells scroll on the
// focused row and clip otherwise, reusing the row-label / stepper marquee.
// triControlText reports the full binding; triControlDisplayText reports the
// clipped on-screen text.
//
// Bind Left Shift into Move forward's alt cell, move focus away so the row is
// idle: the alt text clips to "Left S" while the semantic value stays "Left Shift".
//
// Scancodes: Down 81, Right 79, Enter 40, Left Shift 225. Cells: primary 511, alt 512.

#include "../../../helpers/options_preamble.sqf"
#include "../../../helpers/controls_preamble.sqf"

triClickText "Keyboard & Mouse"
triAssertEq [(triDisplay), 9099]
triSimFrames 2

triAssertEq [(triControlText 511), "W"]
triAssertEq [(triControlText 512), "UP"]

// Select the alt cell of Move forward and bind Left Shift into it.
triSendKey 81
triSimFrames 2
triSendKey 79
triSimFrames 2
triSendKey 40
triSimFrames 3
triSendKey 225
triSimFrames 3
triClick 9301
triSimFrames 3
triAssertEq [(triControlText 512), "Left Shift"]

// Move focus off the row so the alt cell renders idle: clipped on screen,
// full via the semantic value.
triSendKey 81
triSimFrames 3
triAssertEq [(triControlText 512), "Left Shift"]
triAssertEq [(triControlDisplayText 512), "Left S"]
// The short primary is untouched and never clips.
triAssertEq [(triControlDisplayText 511), "W"]

triEndTest
