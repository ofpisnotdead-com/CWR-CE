// Workshop catalog rows merge with the local scan. Missing rows remain unmounted
// until Load Mods downloads them.

triSetLanguage "English"
triSimUntil { triGameMode == 2 }
triAssertEq [(triDisplay), 0]

triClick 119                 // IDC_MAIN_MODS
triAssertEq [(triDisplay), 72]          // IDD_MODS

triWaitFrames 10
_local = triModsVisibleCount
if (_local != 1) exitWith { format ["FAIL:local=%1 (want 1 fixture mod, workshop fetch off in autotest)", _local] }

// Merge 3 remote catalog entries. Missing workshop packages stay out of Installed.
triSeedWorkshopMods 3
triWaitFrames 5
_all = triModsVisibleCount
if (_all != 4) exitWith { format ["FAIL:all=%1 (want 1 local + 3 workshop)", _all] }
triScreenshot "01_installed"

// Tabs prove the per-row source: 3 Workshop, 1 Local.
triClick 7003                // Workshop
triAssertIncludes [(triVisibleTexts), "Workshop (3)"]
_ws = triModsVisibleCount
if (_ws != 3) exitWith { format ["FAIL:workshop=%1 (want 3)", _ws] }
triScreenshot "02_workshop_only"

triClick 7004                // Local
triAssertIncludes [(triVisibleTexts), "Local (1)"]
_lo = triModsVisibleCount
if (_lo != 1) exitWith { format ["FAIL:local-filter=%1 (want 1)", _lo] }

// Not-downloaded: ticking a Workshop mod selects it but it is NOT in the mount set
// (it is still Missing on disk); only ticked mods that are downloaded mount.
triClick 7003
triModsRowClick [0, 0.03]     // tick Workshop Mod 1
triAssertEq [(triGetModsActiveSet), "wsmod1"]   // it IS selected (ticked)
triAssertEq [(triGetModsMountSet), ""]           // Load Mods would mount nothing yet
triClick 7004
triModsRowClick [0, 0.03]     // also tick the local Fixture Mod
triAssertEq [(triGetModsMountSet), "fixturemod"]   // only the local mod mounts
triEndTest
