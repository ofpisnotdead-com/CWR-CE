// Assignment respects the selected slot. The two binding cells are independent
// positional slots (primary, alt): capturing into the alt while the primary is
// empty must land in the alt slot, not fall into the first free slot; and later
// filling the primary must not disturb the alt. Together with the positional
// clear in binding_clear, this locks in the two independent slots.
//
// Uses Move forward (row 1). Scancodes: Down 81, Right 79, Left 80, Delete 76,
// Backspace 42, Enter 40, Up-arrow 82, W 26. Empty cell renders as "—".
// Cells: primary 511, alt 512. The selected-cell highlight is a display-only
// colour tint (the cell text is unchanged), so triControlText reads the value.

#include "../../../helpers/options_preamble.sqf"
#include "../../../helpers/controls_preamble.sqf"

triClickText "Keyboard & Mouse"
triAssertEq [(triDisplay), 9099]
triSimFrames 2

// Focus Move forward and clear both cells so it starts empty.
triSendKey 81
triSimFrames 2
triSendKey 76
triSimFrames 2
triSendKey 79
triSimFrames 2
triSendKey 42
triSimFrames 3
triAssertEq [(triControlText 511), "—"]
triAssertEq [(triControlText 512), "—"]

// Assign W to the ALT cell while the primary is empty: it must land in the alt
// slot (not the first free slot), leaving the primary empty.
triSendKey 79
triSimFrames 2
triSendKey 40
triSimFrames 3
triSendKey 26
triSimFrames 3
triClick 9301
triSimFrames 3
triAssertEq [(triControlText 511), "—"]
triAssertEq [(triControlText 512), "W"]

// Fill the PRIMARY with the Up arrow: it lands in the primary and leaves the alt.
triSendKey 80
triSimFrames 2
triSendKey 40
triSimFrames 3
triSendKey 82
triSimFrames 3
triClick 9301
triSimFrames 3
triAssertEq [(triControlText 511), "UP"]
triAssertEq [(triControlText 512), "W"]

triEndTest
