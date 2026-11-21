// The menu under a script dialog must not take the dialog's clicks or keys.
// Scancode: Enter 40. IDCs: 115 Editor, 117 Single mission.
triSetLanguage "English"
triAssertEq [(triDisplay), 0]

createDialog "RscDisplayMainRemaster"
triAssertEq [dialog, true]
triCursorMoveControl 115
triMouseLeft 1
triSimFrames 2
triAssertEq [(triGetControlFocused 115), "1"]
triMouseLeft 0
triSimFrames 2
closeDialog 0
triSimFrames 2
triAssertEq [dialog, false]
triAssertEq [(triDisplay), 0]
triAssertEq [(triGetControlFocused 117), "1"]

createDialog "RscDisplayChat"
triAssertEq [dialog, true]
triKeyDown 40
triWaitFrames 3
triKeyUp 40
triWaitFrames 3
closeDialog 0
triSimFrames 2
triAssertEq [dialog, false]
triAssertEq [(triDisplay), 0]
triEndTest
