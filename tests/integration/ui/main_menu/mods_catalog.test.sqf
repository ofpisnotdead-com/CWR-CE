// MODS catalog table: open/close, seeded rows, column headers, sorting.

triSetLanguage "English"
triAssertEq [(triDisplay), 0]
triAssertIncludes [(triVisibleTexts), "MODS"]

// Open MODS via triClick (proves the button is a real active-text, not a CStatic).
triClick 119
triAssertEq [(triDisplay), 72]
triAssertEq [(triControlText 101), "MODS"]
triAssertIncludes [(triVisibleTexts), "Operated by master.example"]
triScreenshot "00_mods_screen"

// Reset sort to Name before seeding: sort column persists across test runs in
// the user profile, so click Name column header (111) here and again after re-seed.
triClick 111
triWaitFrames 10

// Seed 6 rows; assert row count, column headers, and default Name sort.
triSeedMods 6
_seeded = triModsVisibleCount
if (_seeded != 6) exitWith { format ["FAIL:seeded=%1 (want 6)", _seeded] }
triAssertIncludes [(triVisibleTexts), "Name"]
triAssertIncludes [(triVisibleTexts), "Source"]
triAssertIncludes [(triVisibleTexts), "State"]
triAssertEq [(triGetModsSortColumn), 0]
triScreenshot "01_mods_seeded_with_headers"

// Sort by State via column header click.
triClick 114
triScreenshot "02_sorted_by_state_header"

// Sort by Source via column header click.
triClick 120
triAssertEq [(triGetModsSortColumn), 4]
triScreenshot "03_sorted_by_source_header"

// Re-seed and reset to Name sort via header click, then sort by State via triSortMods.
// triSeedMods resets row data but not the sort column; click Name header (111) resets it.
triSeedMods 6
triWaitFrames 10
triClick 111
triAssertEq [(triGetModsSortColumn), 0]
triScreenshot "04_reseeded_order"

triSortMods 3
triScreenshot "05_sorted_by_state_direct"

// Cancel closes the MODS notebook and returns to the main menu.
triClick 2
triAssertEq [(triDisplay), 0]
triScreenshot "06_back_at_menu"

triEndTest
