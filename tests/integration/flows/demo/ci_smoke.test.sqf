triSimFrames 5
triSetLanguage "English"

triAssertEq [(triDisplay), 0]
triAssertIncludes [(triVisibleTexts), "OPTIONS"]

triClickText "OPTIONS"
triAssertEq [(triDisplay), 9099]
triAssertIncludes [(triVisibleTexts), "Audio"]

triEndTest
