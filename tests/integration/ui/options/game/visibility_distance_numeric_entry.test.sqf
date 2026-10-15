triSetLanguage "English"
#include "../../../helpers/options_preamble.sqf"

triClickText "Game"
triAssertEq [(triDisplay), 9099]
triSimFrames 2
triAssertEq [(triControlText 520), "Visibility distance"]

triSendKey 81
triSimFrames 2
triSendKey 81
triSimFrames 2
triAssert [(triGetControlFocused 523)]
triSendKey 40
triSimFrames 3

triAssertEq [(triControlText 9480), "Visibility distance (100 - 5000 m)"]
private _rangePromptColor = triGetControlTextColor 9480
triAssert [(triGetControlFocused 9401)]
triAssertEq [(triGetControlFocused 9481), "0"]
triAssertEq [(triTextInputActive), 1]
triSendKey 81
triSimFrames 2
triAssert [(triGetControlFocused 9402)]
triAssertEq [(triGetControlFocused 9481), "0"]
triAssertEq [(triTextInputActive), 1]
triSendKey 82
triSimFrames 2
triAssert [(triGetControlFocused 9401)]
triAssertEq [(triTextInputActive), 1]
triCursorMoveControl 9402
triSimFrames 2
triAssert [(triGetControlFocused 9402)]
triAssertEq [(triTextInputActive), 1]
triTypeText "5001"
triAssertEq [(triControlText 9481), "5001"]
triAssert [(triGetControlFocused 9402)]
triAssertEq [(triGetControlFocused 9481), "0"]
triAssertEq [(triTextInputActive), 1]
triSendKey 42
triSimFrames 2
triAssertEq [(triControlText 9481), "500"]
triAssert [(triGetControlFocused 9402)]
triAssertEq [(triTextInputActive), 1]
triTypeText "1"
triAssertEq [(triControlText 9481), "5001"]
triAssert [(triGetControlFocused 9402)]
triCursorMoveControl 9401
triSimFrames 2
triAssert [(triGetControlFocused 9401)]
triAssertEq [(triTextInputActive), 1]
triSendKey 40
triSimFrames 2
triAssert [(triGetControlFocused 9401)]
triAssertEq [(triTextInputActive), 1]
triAssertEq [(triControlText 9480), "Invalid value"]
triAssertEq [(triGetControlTextColor 9480), "242,77,77,255"]
triWait 3200
triSimFrames 2
triAssertEq [(triControlText 9480), "Visibility distance (100 - 5000 m)"]
triAssertEq [(triGetControlTextColor 9480), _rangePromptColor]
triAssert [(triGetControlFocused 9401)]
triAssertEq [(triTextInputActive), 1]
triTypeText "900"
triAssertEq [(triControlText 9481), "900"]
triSendKey 40
triSimFrames 3

triAssertEq [(triControlText 522), "900 m"]

triSendKey 40
triSimFrames 2
triTypeText "1100"
triSendKey 81
triSendKey 40
triSimFrames 2
triAssertEq [(triControlText 522), "900 m"]
triEndTest
