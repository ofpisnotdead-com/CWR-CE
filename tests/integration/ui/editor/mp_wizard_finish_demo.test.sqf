// Regression: the multiplayer editor wizard must be able to finish a template
// mission from Demo data and launch it as a hosted multiplayer game.

triSetLanguage "Czech"
triAssertEq [(triDisplay), 0]

// Main menu -> Multiplayer -> New server.
triClick 105
triAssertEq [(triDisplay), 8]
triClick 104
triAssertEq [(triDisplay), 17]

// Pick the Demo island and the "new wizard" mission row.
triAssertEq [triSelectListByData [101, "Demo"], true]
triAssertEq [(triAssertListText [102, "<< Nová - Průvodce >>"]), "OK"]
triAssertEq [triSelectList [102, 1], true]
triScreenshot "00_server_new_wizard"
triClick 1

// Accept the default MP template and mission name.
triAssertEq [(triDisplay), 67]
triAssert [(triGetControlVisible 101)]
triAssert [(triGetControlVisible 103)]
triSendText [103, "TriMPWizardFinish"]
triScreenshot "01_wizard_template_named"
triClick 1

// Finish the wizard map. This is the Czech "Konec" button.
triAssertEq [(triDisplay), 68]
triScreenshot "02_wizard_map_before_finish"
triClick 1

// The generated mission enters MP setup, then server get-ready, then gameplay.
triAssertEq [(triDisplay), 70]
triScreenshot "03_mp_setup"
triAssertEq [triMpAssignSelfSlot "WEST:1", "OK"]
triScreenshot "04_mp_setup_role_assigned"
triClick 1

triAssertEq [(triDisplay), 52]
triScreenshot "05_server_get_ready"
triClick 1

triAssertEq [(triInGameplay), "1"]
triScreenshot "06_generated_mp_mission_live"

triEndTest
