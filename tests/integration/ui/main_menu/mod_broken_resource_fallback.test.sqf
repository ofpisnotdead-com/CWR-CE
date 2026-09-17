triSetLanguage "English"
triSimUntil { triGameMode == 2 }
triAssertEq [(triDisplay), 0]

triClick 119
triAssertEq [(triDisplay), 72]
triWaitFrames 10
triAssertEq [(triModsVisibleCount), 1]
triModsRowClick [0, 0.03]
triAssertEq [(triGetModsActiveSet), "brokenresource"]

triClick 115
triSimFrames 60
triSimUntil { triGameMode == 2 && triLoadedShapeCount > 0 }
triAssertEq [(triDisplay), 0]
triAssertIncludes [(triGetActiveMods), "brokenresource"]
triEndTest
