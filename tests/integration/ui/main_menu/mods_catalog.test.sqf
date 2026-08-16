// MODS catalog table: tabs, scrolling, row actions, sorting and keyboard focus.

triSetLanguage "English"
triAssertEq [(triDisplay), 0]
triAssertIncludes [(triVisibleTexts), "MODS"]

// Open MODS via triClick (proves the button is a real active-text, not a CStatic).
triClick 119
triAssertEq [(triDisplay), 72]
triAssertEq [(triControlText 101), "MODS"]
triAssertIncludes [(triVisibleTexts), "Operated by master.example"]
triAssertIncludes [(triVisibleTexts), "All (0)"]
triAssertIncludes [(triVisibleTexts), "Active (0)"]
triAssertIncludes [(triVisibleTexts), "Installed (0)"]
triAssertIncludes [(triVisibleTexts), "Workshop (0)"]
triAssertIncludes [(triVisibleTexts), "Local (0)"]
triAssertIncludes [(triVisibleTexts), "Search:"]
triAssertEq [(triControlText 7010), ""]
triAssertIncludes [(triVisibleTexts), "Enter downloads selected mods when needed and loads them."]
triAssertEq [(triControlText 115), "Load Mods"]

// Reset sort to Name before seeding: sort column persists across test runs in
// the user profile, so click Name column header (111) here and again after re-seed.
triClick 111
triWaitFrames 10

// More rows than the table can show keeps the production scrollbar exercised.
triSeedMods 16
_seeded = triModsVisibleCount
if (_seeded != 16) exitWith { format ["FAIL:all=%1 (want 16)", _seeded] }
triAssertIncludes [(triVisibleTexts), "All (16)"]
triAssertIncludes [(triVisibleTexts), "Active (5)"]
triAssertIncludes [(triVisibleTexts), "Installed (10)"]
triAssertIncludes [(triVisibleTexts), "Workshop (8)"]
triAssertIncludes [(triVisibleTexts), "Local (8)"]
triAssertIncludes [(triVisibleTexts), "Changes ready to load"]
triAssertIncludes [(triVisibleTexts), "Active"]
triAssertIncludes [(triVisibleTexts), "Name"]
triAssertIncludes [(triVisibleTexts), "Source"]
triAssertIncludes [(triVisibleTexts), "State"]
triAssertIncludes [(triVisibleTexts), "Action"]
triAssertEq [(triGetModsSortColumn), 0]

// The focused row can be toggled by Space and double-click.
triSelectList [110, 0]
_before = triGetModsActiveSet
triSendKey 44
if ((triGetModsActiveSet) == _before) exitWith { "FAIL:space did not toggle selected mod" }
triDblClick 110
triAssertEq [(triGetModsActiveSet), _before]

// Sort by State via column header click.
triClick 114
triAssert [(triGetControlFocused 110)]

// Sort by Source via column header click.
triClick 120
triAssertEq [(triGetModsSortColumn), 4]
triAssert [(triGetControlFocused 110)]

// Sort by the operation that Load Mods will perform.
triClick 7013
triAssertEq [(triGetModsSortColumn), 6]
triAssert [(triGetControlFocused 110)]

// Re-seed and reset to Name sort via header click, then sort by State via triSortMods.
// triSeedMods resets row data but not the sort column; click Name header (111) resets it.
triSeedMods 16
triWaitFrames 10
triClick 111
triAssertEq [(triGetModsSortColumn), 0]

triSortMods 3
triScreenshot "mods_table_wired"

triSetLanguage "Czech"
triWaitFrames 2
triAssertIncludes [(triVisibleTexts), "Enter stáhne (pokud nejsou stažené) a načte vybrané mody."]

// Cancel closes the MODS notebook and returns to the main menu.
triClick 2
triAssertEq [(triDisplay), 0]

triEndTest
