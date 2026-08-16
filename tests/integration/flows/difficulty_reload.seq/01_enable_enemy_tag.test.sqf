triSetLanguage "English"
triClickText "OPTIONS"
triClickText "Difficulty"
triAssertEq [(triDisplay), 9099]

if ((triControlText 501) != "Cadet") then { format ["FAIL: expected active mode Cadet, got '%1'", triControlText 501] } else { "OK" }
if ((triControlText 531) != "Disabled") then { format ["FAIL: expected Enemy Tag disabled before toggle, got '%1'", triControlText 531] } else { "OK" }

triSendKey 81
triSimFrames 2
triSendKey 81
triSimFrames 2
triSendKey 81
triSimFrames 2
triSendKey 40
triSimFrames 2

if ((triControlText 531) != "Enabled") then { format ["FAIL: expected Enemy Tag enabled after toggle, got '%1'", triControlText 531] } else { "OK" }
triAssertEq [(triGetDifficultyEnabled 2), 1]

triSendKey 81
triSimFrames 2
triSendKey 81
triSimFrames 2
triSendKey 81
triSimFrames 2
triSendKey 81
triSimFrames 2
triSendKey 81
triSimFrames 2
triSendKey 81
triSimFrames 2
triSendKey 81
triSimFrames 2
triSendKey 81
triSimFrames 2
triSendKey 81
triSimFrames 2
triSendKey 40
triSimFrames 2
triAssertEq [(triGetDifficultyEnabled 11), 1]

triEndTest
