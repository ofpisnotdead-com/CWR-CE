triSetLanguage "English"
triSimFrames 60

player switchCamera "EXTERNAL"
triSetDisplayStyle 0
triSetAspectPillarbox false
triSetPillarboxBarsEnabled true
triSimFrames 60

triScreenshot "00_world_full_width"
triAssertGt [(triGetPixelMaxChannel [0.02, 0.5]), 10]

cutObj ["tvset", "plain down", 100]
triSimFrames 60
triScreenshot "01_tvset_pillarbox"
triAssertNear [(triSamplePixel [0.02, 0.5]), "0,0,0", 16]
triAssertNear [(triSamplePixel [0.97, 0.5]), "0,0,0", 16]

triEndTest
