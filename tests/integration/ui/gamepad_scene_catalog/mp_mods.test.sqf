// Temporary controller scene catalog captures for multiplayer, mods, and
// modded-join modal surfaces.

triSetLanguage "English"
triAssertEq [(triDisplay), 0]

triClick 105
triAssertEq [(triDisplay), 8]
triAssertEq [(triGetControllerScene), "Multiplayer"]
triAssertEq [(triGetControllerSection), "Pager"]
triAssertIncludes [(triGetControllerPrompts), "LT/RT Page"]
triAssertIncludes [(triGetControllerPrompts), "X Refresh"]
triAssertIncludes [(triGetControllerPrompts), "Y Filter"]
triAssertIncludes [(triGetControllerPrompts), "LB/RB Source"]
triSeedSessions 8
triWaitFrames 30
triAssertIncludes [(triVisibleTexts), "Server"]
triAssertIncludes [(triVisibleTexts), "Join"]
triScreenshot "10_mp_browser"

triGpadButton 3
triAssertEq [(triDisplay), 71]
triClick 2
triAssertEq [(triDisplay), 8]

triOpenJoinRequirements 0
triAssertEq [(triDisplay), 75]
triAssertEq [(triGetControllerScene), "Multiplayer"]
triAssertIncludes [(triGetControllerPrompts), "LT/RT Page"]
triAssertIncludes [(triVisibleTexts), "Join"]
triScreenshot "11_join_requirements"

triClick 2
triAssertEq [(triDisplay), 8]
triClick 2
triAssertEq [(triDisplay), 0]

triClick 119
triAssertEq [(triDisplay), 72]
triAssertEq [(triGetControllerScene), "ModsCatalog"]
triAssertEq [(triGetControllerSection), "Pager"]
triAssertIncludes [(triGetControllerPrompts), "LT/RT Page"]
triAssertIncludes [(triGetControllerPrompts), "X Apply"]
triAssertIncludes [(triGetControllerPrompts), "Y Filter"]
triAssertIncludes [(triGetControllerPrompts), "LB/RB Source"]
triSeedMods 6
triWaitFrames 20
triAssertIncludes [(triVisibleTexts), "Name"]
triAssertIncludes [(triVisibleTexts), "State"]
triScreenshot "12_mods_catalog"

triGpadButton 3
triAssertEq [(triDisplay), 72]

triClick 2
triAssertEq [(triDisplay), 0]

triEndTest
