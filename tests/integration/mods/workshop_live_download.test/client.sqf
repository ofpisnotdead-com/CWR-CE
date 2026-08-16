triAssertEq [(triDisplay), 0]

triClick 119
triAssertEq [(triDisplay), 72]
triAssertEq [(triModsVisibleCount), 1]
triAssertEq [(triFetchWorkshopMods), true]
triAssertEq [(triModsFreshness 0), "update"]

triModsRowClick [0, 0.03]
triAssertEq [(triGetModsActiveSet), "fixturemod"]
triAssertEq [(triGetModsMountSet), "fixturemod"]

triClick 115
triAssertEq [(triDisplay), 74]
triAssertIncludes [(triVisibleTexts), "Update 1 addon"]
triAssertIncludes [(triVisibleTexts), "Download"]
triClick 125
triAssertIncludes [(triVisibleTexts), "Complete"]
triAssertIncludes [(triVisibleTexts), "Continue"]
triAssertIncludes [(triVisibleTexts), "Back"]
triClick 2

triAssertEq [(triDisplay), 72]
triAssertIncludes [(triReadWorkshopFile ["fixturemod", "revision.txt"]), "revision-one"]
triModsRowClick [0, 0.03]
triClick 115
triAssertEq [(triDisplay), 0]

triAssertIncludes [(triReadWorkshopFile ["fixturemod", "revision.txt"]), "revision-two"]
triAssertEq [(triReadWorkshopFile ["fixturemod", "stale.txt"]), ""]
triAssertIncludes [(triReadWorkshopFile ["fixturemod", "mod.json"]), "packageRevision"]
triAssertEq [(triDisplay), 0]
triEndTest
