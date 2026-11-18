triSetLanguage "English"
triSimFrames 60

player switchCamera "EXTERNAL"
triSetDisplayStyle 0
triSetPillarboxBarsEnabled true
triSimFrames 30
triScreenshot "00_on_foot"
triAssertGt [(triGetPixelMaxChannel [0.02, 0.5]), 10]

private _pos = getPos player
cutCam = "camera" camCreate [(_pos select 0) - 20, (_pos select 1) - 20, 6]
cutCam cameraEffect ["internal", "back"]
cutCam camSetTarget player
cutCam camCommit 0
showCinemaBorder true
triSimFrames 60
triScreenshot "01_cinema_border"
triAssertNear [(triSamplePixel [0.02, 0.5]), "0,0,0", 16]
triAssertNear [(triSamplePixel [0.97, 0.5]), "0,0,0", 16]
triAssertGt [(triGetPixelMaxChannel [0.4, 0.7]), 10]

cutCam cameraEffect ["terminate", "back"]
camDestroy cutCam
triSimFrames 30
triScreenshot "02_after_cutscene"
triAssertGt [(triGetPixelMaxChannel [0.02, 0.5]), 10]

triEndTest
