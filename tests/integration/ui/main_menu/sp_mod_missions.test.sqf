// Single-player mission list must include missions shipped inside an active mod's Missions/ folder.
triSetLanguage "English"
triAssertEq [(triDisplay), 0]
triClickText "SINGLE MISSION"
triAssertEq [(triDisplay), 2]
triWaitFrames 30
triAssertEq [(triAssertListText [101, "Tri SP Gap1"]), "OK"]
triAssertEq [(triAssertListText [101, "Tri SP Loose"]), "OK"]
// The title comes from the mission's own stringtable.csv.
triAssertEq [(triAssertListText [101, "Tri SP Local Title"]), "OK"]
// A packed mission with no briefingName is listed under its own name.
triAssertEq [(triAssertListText [101, "tri_sp_noname.Demo"]), "OK"]
// A mission whose island is not installed stays listed but refuses to start.
triAssertEq [(triAssertListText [101, "Tri SP No World"]), "OK"]
triAssertEq [(triSelectListByData [101, "tri_sp_noworld"]), true]
triClick 1
// Play raises a message box naming the missing world instead of loading anything.
triAssertEq [(triDisplay), -1]
triAssertIncludes [(triVisibleTexts), "OK"]
triClick 1
triSimFrames 20
triAssertEq [(triDisplay), 2]
triAssertEq [(triSelectListByData [101, "TriLoose.Demo"]), true]
triClickText "Play"
triAssertNear [(triGetViewDistance), 777, 1]
triEndTest
