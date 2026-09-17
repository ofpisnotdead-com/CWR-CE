// MP mission list shows category folders as rows that open, with ".." back to the root.
triSetLanguage "English"
triAssertEq [(triDisplay), 0]
triAssertIncludes [(triActiveMods), "@mpfolders"]

triClick 105
triAssertEq [(triDisplay), 8]
triClick 104
triAssertEq [(triDisplay), 17]
triSelectList [101, 0]

// At the root the folder is a row and the mission inside it is not listed.
triAssertEq [(triAssertListText [102, "Coop..."]), "OK"]
triAssertEq [(triAssertListText [102, "tri_folder_root"]), "OK"]
_n = 0; _i = 0
while { _i < (triListSize 102) } do { if ((triListText [102, _i]) == "tri_in_folder") then { _n = _n + 1 }; _i = _i + 1 }
triAssertEq [_n, 0]

// Opening the folder lists what is inside it, and only that.
triAssertEq [(triSelectListByData [102, "Coop"]), true]
triClick 1
triSimFrames 20
triAssertEq [(triAssertListText [102, "tri_in_folder"]), "OK"]
_n = 0; _i = 0
while { _i < (triListSize 102) } do { if ((triListText [102, _i]) == "tri_folder_root") then { _n = _n + 1 }; _i = _i + 1 }
triAssertEq [_n, 0]

// Folders nest: Coop holds Small, which holds its own mission.
triAssertEq [(triAssertListText [102, "Small..."]), "OK"]
triAssertEq [(triSelectListByData [102, "Small"]), true]
triClick 1
triSimFrames 20
triAssertEq [(triAssertListText [102, "tri_nested"]), "OK"]
_n = 0; _i = 0
while { _i < (triListSize 102) } do { if ((triListText [102, _i]) == "tri_in_folder") then { _n = _n + 1 }; _i = _i + 1 }
triAssertEq [_n, 0]

// ".." climbs one level at a time, back to Coop first, with the folder just left selected.
triAssertEq [(triListText [102, 0]), ".."]
triSelectList [102, 0]
triClick 1
triSimFrames 20
triAssertEq [(triAssertListText [102, "tri_in_folder"]), "OK"]
triAssertEq [(triAssertListText [102, "Small..."]), "OK"]
triAssertEq [(triListText [102, (triListSel 102)]), "Small..."]

// The first row is "..", which returns to the root.
triAssertEq [(triListText [102, 0]), ".."]
triSelectList [102, 0]
triClick 1
triSimFrames 20
triAssertEq [(triAssertListText [102, "tri_folder_root"]), "OK"]
triAssertEq [(triAssertListText [102, "Coop..."]), "OK"]
triAssertEq [(triListText [102, (triListSel 102)]), "Coop..."]

// A mission inside a folder starts, at any depth: the load path resolves it by name.
triAssertEq [(triSelectListByData [102, "Coop"]), true]
triClick 1
triSimFrames 20
triAssertEq [(triSelectListByData [102, "Small"]), true]
triClick 1
triSimFrames 20
triAssertEq [(triSelectListByData [102, "tri_nested"]), true]
triClick 1
triSimFrames 60
triAssertNe [(triDisplay), 17]
triEndTest
