// Regression: å/ä/ö (legacy CP1252 bytes in a CfgWorlds `description`) don't show
// in the editor's "Select Island" world list. DisplayUI.cpp's IDC_SELECT_ISLAND
// handler passes the description straight to C3DListBox::AddString, which stores
// it raw; CStatic::SetText decodes through Poseidon::DecodeLegacyTextToRString
// first (UIControls.cpp's DecodeControlText), but AddString never goes through
// that path, so the raw CP1252 byte is invalid UTF-8 and the glyph drops.
//
// The @nordicnames fixture mod adds a world whose description is a real on-disk
// CP1252 byte for "Malmö" (0xF6 for ö).
//
// Teeth: without the fix, the invalid-UTF-8 byte reaching the row text breaks
// the tri harness's own wire protocol when it reports the mismatch back -- the
// connection drops with "broken pipe" instead of a clean assertion failure, but
// it's still a deterministic failure on the current broken state.

triSetLanguage "English"
triAssertEq [(triDisplay), 0]
triClick 115
triAssertEq [(triDisplay), 51]

// IDC_SELECT_ISLAND = 101 (the world listbox on the "Select Island" dialog).
triAssertListText [101, "Malmö"]

triClick 2
triAssertEq [(triDisplay), 0]
triEndTest
