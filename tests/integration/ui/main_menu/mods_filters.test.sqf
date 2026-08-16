// MODS catalog tabs and inline name search.

triSetLanguage "English"
triSimUntil { triGameMode == 2 }
triAssertEq [(triDisplay), 0]

triClick 119
triAssertEq [(triDisplay), 72]

triSeedMods 6

// Seed state cycles Missing/Downloaded/Active: all six are shown initially and
// four are installed.
_all = triModsVisibleCount
if (_all != 6) exitWith { format ["FAIL:all count=%1 (want 6)", _all] }
triAssertIncludes [(triVisibleTexts), "All (6)"]
triAssertIncludes [(triVisibleTexts), "Active (2)"]
triAssertIncludes [(triVisibleTexts), "Installed (4)"]

// Tab advances through the source tabs while keeping table focus.
triSendKey 43
_active = triModsVisibleCount
if (_active != 2) exitWith { format ["FAIL:active count=%1 (want 2)", _active] }

triSendKey 43
_installed = triModsVisibleCount
if (_installed != 4) exitWith { format ["FAIL:installed count=%1 (want 4)", _installed] }

triClick 7003
_ws = triModsVisibleCount
if (_ws != 3) exitWith { format ["FAIL:workshop count=%1 (want 3)", _ws] }

triClick 7004
_lo = triModsVisibleCount
if (_lo != 3) exitWith { format ["FAIL:local count=%1 (want 3)", _lo] }

triClick 7001
_all2 = triModsVisibleCount
if (_all2 != 6) exitWith { format ["FAIL:all-again count=%1 (want 6)", _all2] }

// Typing through the focused inline search updates the list on the next frame.
triClick 123
triTypeText "Test Mod 1"
triSimFrames 2
triAssertEq [(triControlText 123), "Test Mod 1"]
_one = triModsVisibleCount
if (_one != 1) exitWith { format ["FAIL:filtered count=%1 (want 1)", _one] }

// The harness clear also proves the display refreshes when the query is removed.
triModsSetFilter ""
_all3 = triModsVisibleCount
if (_all3 != 6) exitWith { format ["FAIL:cleared count=%1 (want 6)", _all3] }

triEndTest
