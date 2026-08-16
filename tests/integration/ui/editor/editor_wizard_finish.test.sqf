// Regression: the single-player editor wizard must be able to finish a
// template-created mission. In Czech the final wizard-map action is "Konec";
// when broken, clicking it leaves the user stuck on DisplayWizardMap (IDD 68).

triSetLanguage "Czech"
triAssertEq [(triDisplay), 0]

// Main menu -> Mission Editor -> island selection.
triClick 115
triAssertEq [(triDisplay), 51]
triAssert [(triGetControlVisible 103)]
triAssertEq [triSelectListByData [101, "Demo"], true]

// Open the wizard from the island-selection notebook.
triClick 103
triAssertEq [(triDisplay), 67]
triAssert [(triGetControlVisible 101)]  // template list
triAssert [(triGetControlVisible 103)]  // mission name edit

// Accept the default template, but provide a mission name first.
triSendText [103, "TriWizardFinish"]
triScreenshot "00_wizard_template_named"
triClick 1

// Finish the wizard map. This is the Czech "Konec" button.
triAssertEq [(triDisplay), 68]
triScreenshot "01_wizard_map_before_finish"
triClick 1

// Successful finish closes the wizard chain and starts the generated mission.
triAssertNe [(triDisplay), 68]
triAssertEq [(triDisplay), 37]
triScreenshot "02_get_ready"
triClick 1

// The load-failed message box also leaves the wizard and shows terrain behind it;
// require the generated mission to be actually playable.
triAssertEq [(triInGameplay), "1"]
triScreenshot "03_generated_mission_live"

triEndTest
