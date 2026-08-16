// Load Mods action: re-mount the game in place with exactly the
// ticked mods. Two things under test:
//   1. The selection -> mod set. triSeedMods ticks every even row (i%2==0), so
//      the checked set is testmod1/3/5; triAssertModsActiveSet pins the exact
//      @modId list (selection order, comma-separated) that Load Mods hands to
//      RequestRemountWithMods (as a ';'-joined path).
//   2. The Load Mods -> re-mount wiring. Clicking IDC_MODS_APPLY routes through
//      DisplayMods::OnButtonClicked -> RequestRemountWithMods (deferred to AppIdle),
//      which tears down the MODS notebook and rebuilds a fresh main menu. We pump
//      frames and assert we land back on the menu scene (the seeded mod folders
//      don't exist on disk, so the bank loader skips them -> effectively a base-game
//      re-mount; the point is the button drives the proven re-mount path).
//
// The full re-mount round-trip itself is covered deterministically by the CLI
// --remount-selftest and tests/integration/flows/remount/remount_clean.test.sqf;
// here we only prove Load Mods reaches it with the right set.

triSetLanguage "English"
triSimUntil { triGameMode == 2 }     // main-menu intro scene resident
triAssertEq [(triDisplay), 0]

triClick 119                         // IDC_MAIN_MODS
triAssertEq [(triDisplay), 72]                  // IDD_MODS

triSeedMods 6
triAssertEq [(triGetModsActiveSet), "@testmod1,@testmod3,@testmod5"]
triAssertIncludes [(triVisibleTexts), "Load Mods"]            // the action rendered as a real control
triAssertIncludes [(triVisibleTexts), "Cancel"]               // paired with Cancel (centre-bottom)
triScreenshot "01_seeded"
triWaitFrames 30                     // let the async screenshot flush on the stable MODS screen

triAssert [(triGetControlFocused 110)]
triSendKey 40                        // Enter uses the same remount path as the footer
triSimFrames 60
triSimUntil { triGameMode == 2 && triLoadedShapeCount > 0 }   // re-mount done, menu rebuilt
triAssertEq [(triDisplay), 0]                   // notebook gone -> back on the main menu
triScreenshot "02_after_apply"

triClick 119
triAssertEq [(triDisplay), 72]
triSeedMods 6
triAssert [(triGetControlFocused 110)]
triSendKey 40                        // Enter still applies after a completed re-mount
triSimFrames 60
triAssertEq [(triDisplay), 0]

triClick 119
triAssertEq [(triDisplay), 72]
triSeedMods 6
triClick 115                         // footer action still applies after a completed re-mount
triSimFrames 60
triAssertEq [(triDisplay), 0]
triEndTest
