#include "../../../helpers/options_preamble.sqf"
#include "../../../helpers/controls_preamble.sqf"

triClickText "Keyboard & Mouse"
triAssertEq [(triDisplay), 9099]
triSimFrames 2

triSendKey 81
triSimFrames 1
triSendKey 81
triSimFrames 2
triSendKey 40
triSimFrames 3
triSendKey 26
triSimFrames 3
triAssertEq [(triControlText 9381), "Already used for ""Move forward""."]
triClick 9301
triSimFrames 3

triSendKey 81
triSimFrames 2
triSendKey 40
triSimFrames 3
triSendKey 26
triSimFrames 3
triClick 9301
triSimFrames 3

triSendKey 81
triSimFrames 2
triSendKey 40
triSimFrames 3
triSendKey 26
triSimFrames 3
triClick 9301
triSimFrames 3

triSendKey 81
triSimFrames 2
triSendKey 40
triSimFrames 3
triSendKey 26
triSimFrames 3

_initialConflictText = triControlText 9381
triAssertIncludes [_initialConflictText, "Already assigned to multiple actions:"]
triAssert [(triGetControlFocused 9301)]
triScreenshot "multiple_binding_conflicts"
triWaitFrames 90
triAssertNe [(triControlText 9381), _initialConflictText]

triEndTest
