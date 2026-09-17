triSetLanguage "English"
triAssertEq [(triDisplay), 0]

triClick 119
triAssertEq [(triDisplay), 72]
triSimFrames 90

triAssert [(triGetControlVisible 2)]
triAssert [(triGetControlEnabled 2)]

triEndTest
