triSetLanguage "English"
triAssertEq [(triGetDifficultyEnabled 11), 1]
triAssertEq [(triGetDifficultyEnabled 2), 1]

triClickText "OPTIONS"
triClickText "Difficulty"
triAssertEq [(triDisplay), 9099]
if ((triControlText 531) != "Enabled") then { format ["FAIL: expected Enemy Tag enabled after reload, got '%1'", triControlText 531] } else { "OK" }

triEndTest
