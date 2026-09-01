// The Controls page offers a Joystick row that opens a bindings page carrying the
// analog actions, which the Keyboard and Gamepad pages hide. The row itself comes
// from the resource bundle in packages/, so the engine alone cannot pin it.

#include "../../../helpers/options_preamble.sqf"
#include "../../../helpers/controls_preamble.sqf"

triAssertIncludes [(triVisibleTexts), "Joystick"]

triClickText "Joystick"
triSimFrames 2

// The analog rows head the vehicle categories, not On foot.
triClickText ">"
triSimFrames 2
triAssertIncludes [(triVisibleTexts), "Vehicles"]

triAssertIncludes [(triVisibleTexts), "Stick X-Axis"]
triAssertIncludes [(triVisibleTexts), "Stick Y-Axis"]
triAssertIncludes [(triVisibleTexts), "Stick Z-Rotate"]
triAssertIncludes [(triVisibleTexts), "Stick Z-Axis"]
triAssertIncludes [(triVisibleTexts), "Stick Slider 1"]

triEndTest
