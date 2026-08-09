// Checkbox click target on the MODS catalog.
//
// Bug 1 (column): a click ANYWHERE in the row toggled the tick, so selecting a
// checked row by clicking its name silently un-ticked it. Fix: only toggle when
// the click's horizontal fraction lands in the checkbox column (u < width).
//
// Bug 2 (cross-row leak): OnLButtonDown toggled GetCurSel()'s checkbox using the
// new click's u — but at button-down the selection still holds the PREVIOUSLY
// selected row, so a checkbox click between/over a different row un-ticked that
// OTHER row. Fix: HandleRowClick derives the row from the click's vertical
// fraction v (the row under the cursor), never from GetCurSel().
//
// triModsRowClick [row, u] drives the real order (toggle the cursor row, then
// select it) without pre-selecting `row`, so both bugs are reproducible.
//
// Seeded rows: testmod1..6, even-indexed (1/3/5) ticked, name-sorted so display
// row 0 = testmod1, row 2 = testmod3.

triSetLanguage "English"
triSimUntil { triGameMode == 2 }
triAssertEq [(triDisplay), 0]

triClick 119                 // IDC_MAIN_MODS
triAssertEq [(triDisplay), 72]          // IDD_MODS

triSeedMods 6
triAssertEq [(triGetModsActiveSet), "@testmod1,@testmod3,@testmod5"]
triClick 7003                // Workshop contains testmod1, testmod3, testmod5

// Click the row body (name column, u=0.5) — must NOT change the tick.
triModsRowClick [0, 0.5]
triAssertEq [(triGetModsActiveSet), "@testmod1,@testmod3,@testmod5"]
triScreenshot "01_body_click_no_toggle"

// Click the checkbox column (u=0.03) — toggles row 0 (testmod1) off.
triModsRowClick [0, 0.03]
triAssertEq [(triGetModsActiveSet), "@testmod3,@testmod5"]
triScreenshot "02_checkbox_off"

// Click the checkbox again — toggles it back on.
triModsRowClick [0, 0.03]
triAssertEq [(triGetModsActiveSet), "@testmod1,@testmod3,@testmod5"]

// Cross-row leak: a checkbox click must toggle the row UNDER THE CURSOR, not the
// previously-selected row. Select testmod3 (row 2) via a body click (no toggle),
// then click testmod1's (row 0) checkbox. With the leak bug the toggle followed
// the selection (row 2) -> "@testmod1,@testmod5"; the fix toggles row 0.
triModsRowClick [2, 0.5]     // select row 2 (testmod3); body click, no toggle
triAssertEq [(triGetModsActiveSet), "@testmod1,@testmod3,@testmod5"]
triModsRowClick [0, 0.03]    // checkbox of row 0 while row 2 is selected
triAssertEq [(triGetModsActiveSet), "@testmod3,@testmod5"]   // testmod1 off; testmod3 untouched
triScreenshot "03_cross_row_target"
triEndTest
