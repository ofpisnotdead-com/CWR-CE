// MP server creation should list mission PBOs shipped inside an active mod.

triSetLanguage "English"
triAssertEq [(triDisplay), 0]
triAssertIncludes [(triActiveMods), "@mpmissionbrowser"]

triClick 105
triAssertEq [(triDisplay), 8]
triClick 104
triAssertEq [(triDisplay), 17]

triSelectList [101, 0]
triAssertEq [(triAssertListText [102, "addon mission from mod"]), "OK"]
// One row per mod mission, whatever case the mod spells MPMissions in.
_n = 0; _i = 0
while { _i < (triListSize 102) } do { if ((triListText [102, _i]) == "addon mission from mod") then { _n = _n + 1 }; _i = _i + 1 }
triAssertEq [_n, 1]
triScreenshot "mp_mod_missions"

triClick 2
triClick 2
triAssertEq [(triDisplay), 0]
triEndTest
