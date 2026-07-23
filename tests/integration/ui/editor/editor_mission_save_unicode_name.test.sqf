// Editor save regression: a Czech mission name with spaces and a dash must not
// show a failure dialog after Save.

private _missionName = "Všechny vehikly - test";

triSetLanguage "English"
triWaitFrames 5
triAssertEq [(triDisplay), 0]
triClick 115
triAssertEq [(triDisplay), 51]
triClick 1
triAssertEq [(triDisplay), 26]
triAssert [(triGetControlVisible 51)]

triDblClick 51
triAssertEq [(triDisplay), 27]
triClick 1
triAssertEq [(triDisplay), 26]

triClick 102
triAssertEq [(triDisplay), 29]
triAssert [(triGetControlFocused 101)]
triTypeText _missionName
triAssertEq [(triControlText 101), _missionName]
triScreenshot "00_unicode_save_name_entered"
triClick 1
triWaitFrames 30
triScreenshot "01_after_unicode_save_click"
triAssertEq [(triAssertExcludes [(triVisibleTexts), "Saving of mission failed!"]), "OK"]
triAssertEq [(triDisplay), 26]
triClick 2
triAssertEq [(triDisplay), 203]
triClick 1
triAssertEq [(triDisplay), 0]
triClick 106
