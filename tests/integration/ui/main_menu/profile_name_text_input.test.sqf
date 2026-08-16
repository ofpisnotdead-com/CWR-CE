triSetLanguage "English"
triAssertEq [(triDisplay), 0]

triClick 109
triAssertEq [(triDisplay), 31]
triClick 102
triAssertEq [(triDisplay), 42]

triCursorMoveControl 101
triMouseLeft 1
triSimFrames 2
triMouseLeft 0
triSimFrames 2
triAssertEq [(triTextInputActive), 1]

triAssertEq [triRemoveNestedControl 101, true]
triAssertEq [(triTextInputActive), 0]
triEndTest
