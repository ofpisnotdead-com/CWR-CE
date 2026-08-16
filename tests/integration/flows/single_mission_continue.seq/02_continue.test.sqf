triSetLanguage "English"
triAssertEq [(triDisplay), 0]

triClickText "SINGLE MISSION"
triAssertEq [(triDisplay), 2]
triAssert [(triGetControlVisible 105)]
triClick 105

triAssertMissionPlayable
triAssertNear [((getPos player) select 0), 6710, 1]
triEndTest
