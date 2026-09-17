// Single-player mission list must include missions shipped inside an active mod's Missions/ folder.
triSetLanguage "English"
triAssertEq [(triDisplay), 0]
triClickText "SINGLE MISSION"
triAssertEq [(triDisplay), 2]
triWaitFrames 30
triAssertEq [(triAssertListText [101, "Tri SP Gap1"]), "OK"]
triAssertEq [(triAssertListText [101, "Tri SP Loose"]), "OK"]
// A category folder named in UTF-8 survives the decode untouched. A folder named in a legacy
// codepage cannot be a fixture here: such a path is not valid UTF-8 and macOS refuses to check
// it out, so the byte-level decode is pinned by the codepage unit test instead.
triAssertEq [(triAssertListText [101, "Čsla..."]), "OK"]
// Mission files and folders named outside the ASCII range still open: the title comes from
// mission.sqm rather than falling back to the raw name. These shapes ship in real mods - a
// Czech category folder in CSLA, Finnish mission files in FDF, Cyrillic ones in Liberation.
triAssertEq [(triAssertListText [101, "Tri SP Finnish Name"]), "OK"]
triAssertEq [(triAssertListText [101, "Tri SP Cyrillic Name"]), "OK"]
triAssertEq [(triAssertListText [101, "Tri SP Odd Name"]), "OK"]

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
// A mission whose addons are not installed refuses the same way, instead of loading and
// dropping back to the main menu.
triAssertEq [(triAssertListText [101, "Tri SP No Addon"]), "OK"]
triAssertEq [(triSelectListByData [101, "tri_sp_noaddon.Demo"]), true]
triClick 1
triAssertEq [(triDisplay), -1]
triClick 1
triSimFrames 20
triAssertEq [(triDisplay), 2]
triAssertEq [(triSelectListByData [101, "TriLoose.Demo"]), true]
triClickText "Play"
triAssertNear [(triGetViewDistance), 777, 1]
triEndTest
