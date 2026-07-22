// MP server mission rows must render underscores, not clip them below the 3D list row.

triSetLanguage "English"
triAssertEq [(triDisplay), 0]
triAssertIncludes [(triActiveMods), "@mpmissionbrowser"]

triClick 105
triAssertEq [(triDisplay), 8]
triClick 104
triAssertEq [(triDisplay), 17]

triSelectList [101, 0]
triAssertEq [(triAssertListText [102, "tri_under_score"]), "OK"]
triAssertEq [(triSelectListByData [102, "tri_under_score"]), true]
triWaitFrames 90
triScreenshot "mp_mission_underscores"

triAssertEq [(triAssertRegionLit [0.577, 0.512, 0.592, 0.522, 180, 20]), "OK"]
triAssertEq [(triAssertRegionLit [0.648, 0.512, 0.664, 0.522, 180, 20]), "OK"]

triClick 2
triClick 2
triAssertEq [(triDisplay), 0]
triEndTest
