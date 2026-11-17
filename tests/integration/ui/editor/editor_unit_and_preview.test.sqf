// Merged from place_unit, preview_mission, and mission_options_index — all
// share the same editor boot and unit-placement setup, so one run covers all
// three original scenarios:
//   - Draw2D draw-item accumulation regression (place_unit)
//   - Preview-dialog play flow (preview_mission)
//   - Mission-options index from the preview pause menu (mission_options_index)

triSetLanguage "English"
triAssertEq [(triDisplay), 0]
triClick 115
triAssertEq [(triDisplay), 51]
triClick 1
triAssertEq [(triDisplay), 26]
triAssert [(triGetControlVisible 51)]

// Open unit placement dialog and exercise island-type selection.  Without
// _drawItems.clear() in InitDraw the draw-items vector grows unboundedly;
// a few lbSetCurSel triggers enough redraws to expose the leak.
triDblClick 51
triAssertEq [(triDisplay), 27]
triWait 500
lbSetCurSel [102, 1]
triWait 500
lbSetCurSel [102, 2]
triWait 500
triAssertLt [(triGetDrawItemCount), 5000]

// Accept default unit placement and return to editor.
triClick 1
triAssertEq [(triDisplay), 26]
triAssert [(triGetControlVisible 107)]

// Open the preview dialog (display 46), then Esc into the preview pause menu
// (display 49) where both the options and the play button live.
triClick 107
triAssertEq [(triDisplay), 46]
triSendKey 41
triAssertEq [(triDisplay), 49]

// Mission-options index check (display 9099) from the preview pause menu.
triClick 101
triAssertEq [(triDisplay), 9099]
triAssertEq [(triControlText 1105), "Back"]
triRefute [(triGetControlVisible 1106)]
triScreenshot "00_mission_options_index"
triSetLanguage "Czech"
triAssertEq [(triControlText 1105), "Zpět"]
triRefute [(triGetControlVisible 1106)]
triClick 1101
triAssertEq [(triDisplay), 9099]
triSendKey 41
triAssertEq [(triDisplay), 9099]
triAssertEq [(triControlText 1105), "Zpět"]
triRefute [(triGetControlVisible 1106)]
triScreenshot "01_mission_options_index_return"
triClick 1105
triAssertEq [(triDisplay), 49]
triSetLanguage "English"
triWaitFrames 5

// Activate the pause-menu Abort action with gamepad navigation + A,
// then cancel back to the editor. The buttons stand in one row with the focus on
// Continue at its right end, so Abort is one step left.
triGpadPov 6
triWaitFrames 5
triAssert [(triGetControlFocused 104)]
triGpadButton 0
triWaitFrames 5
triAssertEq [(triDisplay), 50]
triClick 2
triAssertEq [(triDisplay), 26]

// Exit editor cleanly.
triClick 2
triAssertEq [(triDisplay), 203]
triClick 1
triAssertEq [(triDisplay), 0]
triEndTest
