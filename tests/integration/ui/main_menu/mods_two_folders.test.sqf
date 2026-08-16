// Two managed mod folders: local mods (--mods-dir / <UserContent>/Mods) and
// downloaded mods (--workshop-dir / <UserContent>/Workshop). A mod's source is
// classified by WHICH folder it sits in, so a downloaded mod stays "Workshop"
// across launches instead of looking identical to a hand-dropped local mod.
//
// Both fixtures are real on-disk mods (no catalog merge — the live fetch is off
// under autotest); the Source filter proves each was classified by location.

triSetLanguage "English"
triSimUntil { triGameMode == 2 }
triAssertEq [(triDisplay), 0]

triClick 119                       // IDC_MAIN_MODS
triAssertEq [(triDisplay), 72]                // IDD_MODS
triWaitFrames 10

// 1 local (Mods/) + 1 workshop (Workshop/), both scanned from disk.
_all = triModsVisibleCount
if (_all != 2) exitWith { format ["FAIL:rows=%1 (want 1 local + 1 workshop)", _all] }
triScreenshot "01_two_folders"

// Tabs prove classification by location (no catalog involved).
triClick 7003                      // Workshop
triAssertIncludes [(triVisibleTexts), "Workshop (1)"]
_ws = triModsVisibleCount
if (_ws != 1) exitWith { format ["FAIL:workshop=%1 (want 1)", _ws] }

triClick 7004                      // Local
triAssertIncludes [(triVisibleTexts), "Local (1)"]
_lo = triModsVisibleCount
if (_lo != 1) exitWith { format ["FAIL:local=%1 (want 1)", _lo] }

triClick 7002                      // back to Installed
triAssertIncludes [(triVisibleTexts), "Installed (2)"]

// Both are downloaded-on-disk, so ticking either mounts it (each from its own
// root). Name-sorted rows: 0=Fixture Mod, 1=Workshop Fixture.
triModsRowClick [0, 0.03]          // tick the local fixture
triModsRowClick [1, 0.03]          // tick the workshop fixture
triAssertEq [(triGetModsMountSet), "fixturemod,wsfixture"]
triEndTest
