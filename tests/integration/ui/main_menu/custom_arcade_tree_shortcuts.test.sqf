triSetLanguage "English"
triAssertEq [(triDisplay), 0]

triClick 104
triAssertEq [(triDisplay), 25]
triAssertEq [(triTextInputActive), 0]

triSendKey 87
triSendKey 81
triSimFrames 2
triAssert [(triGetControlVisible 103)]

triSendKey 82
triSendKey 86
triSendKey 81
triSimFrames 2
triRefute [(triGetControlVisible 103)]
triEndTest
