triSetLanguage "English"
triSimFrames 60

triSetDisplayStyle 0
triSetPillarboxBarsEnabled true
triAssertEq [(triAspectGameplayActive), 1]

player switchCamera "GUNNER"
player addWeapon "Binocular"
player selectWeapon "Binocular"
triSimFrames 90

triAssertEq [triSaveGame "aspect_gameplay_active", "OK"]

triSetAspectGameplayActive false
triAssertEq [(triAspectGameplayActive), 0]
triAssertEq [triLoadGame "aspect_gameplay_active", "OK"]
triSimFrames 90

triAssertEq [(triAspectGameplayActive), 1]
triScreenshot "00_loaded_binocular"
triAssertNear [(triSamplePixel [0.02, 0.5]), "0,0,0", 16]
triAssertNear [(triSamplePixel [0.97, 0.5]), "0,0,0", 16]
triAssertGt [(triGetPixelMaxChannel [0.35, 0.55]), 10]

triEndTest
