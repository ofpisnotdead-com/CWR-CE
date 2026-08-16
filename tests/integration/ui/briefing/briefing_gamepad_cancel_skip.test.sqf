triSetLanguage "English"

triAssertEq [(triDisplay), 0]
triClickText "SINGLE MISSION"
triAssertEq [(triDisplay), 2]
triClickText "Play"
triAssertEq [(triGetCameraEffectActive), 1]
triGpadButton 1
triSimFrames 30
triAssertEq [(triGetCameraEffectActive), 0]

triEndTest
