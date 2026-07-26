triAssertEq [triDisplay, 0]
triWaitFrames 10
triAssertIncludes [triGetAspectSettings, "1.5000,0.8000,"]
triEndTest
