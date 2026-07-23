triAssertEq [(triDisplay), 0]

triClick 119
triAssertEq [(triDisplay), 72]
triAssertEq [(triModsVisibleCount), 1]

triModsRowClick [0, 0.03]
triAssertEq [(triGetModsActiveSet), "fixturemod"]
triAssertEq [(triGetModsMountSet), ""]

triClick 115
triAssertEq [(triDisplay), 74]
triAssertIncludes [(triVisibleTexts), "Download"]
triClick 125
triAssertIncludes [(triVisibleTexts), "Complete"]
triAssertIncludes [(triVisibleTexts), "Continue"]
triClick 125

triAssertConfigClass "CfgPatches/fixture_core"
triAssertEq [(triDisplay), 0]
triEndTest
