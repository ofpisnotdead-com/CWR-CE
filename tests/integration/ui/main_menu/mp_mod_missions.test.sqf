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
triScreenshot "mp_mod_missions"

triClick 2
triClick 2
triAssertEq [(triDisplay), 0]
triEndTest
