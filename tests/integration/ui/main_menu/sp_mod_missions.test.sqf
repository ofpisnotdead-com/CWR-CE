// Single-player mission list must include missions shipped inside an active mod's Missions/ folder.
triSetLanguage "English"
triAssertEq [(triDisplay), 0]
triClickText "SINGLE MISSION"
triAssertEq [(triDisplay), 2]
triWaitFrames 30
triAssertEq [(triAssertListText [101, "Tri SP Gap1"]), "OK"]
triAssertEq [(triAssertListText [101, "Tri SP Loose"]), "OK"]
// A packed mission with no briefingName is listed under its own name.
triAssertEq [(triAssertListText [101, "tri_sp_noname.Demo"]), "OK"]
triAssertEq [(triSelectListByData [101, "TriLoose.Demo"]), true]
triClickText "Play"
triAssertNear [(triGetViewDistance), 777, 1]
triEndTest
