triSetLanguage "English"
triAssertEq [(triDisplay), 0]

triCursorMoveControl 102
triSimFrames 30

// The notebook animates open under a cursor that never moves, so no row may
// take hover and play its enter sound. triInvokeButton opens the shell without
// a click of its own, leaving the whole window free of control sounds.
triLogMark
triInvokeButton 102
triAssertEq [(triDisplay), 9099]
triSimFrames 50
triAssertLogAbsent "control sound"

triEndTest
