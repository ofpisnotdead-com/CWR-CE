triSetLanguage "English"
triSimFrames 30

private _pos = getPos player
gunVeh = "M113" createVehicle [(_pos select 0) + 10, (_pos select 1) + 10, 0]
triSimFrames 10
player moveInGunner gunVeh
player setBehaviour "COMBAT"
triSimFrames 60
triAssertEq [(if ((vehicle player) == gunVeh) then {1} else {0}), 1]

gunVeh switchCamera "GUNNER"
triSetDisplayStyle 0
triSetPillarboxBarsEnabled true
triSimFrames 60
triScreenshot "00_gunner"
triAssertGt [(triGetPixelMaxChannel [0.02, 0.5]), 10]
triAssertGt [(triGetPixelMaxChannel [0.97, 0.5]), 10]

cutCam = "camera" camCreate [(_pos select 0) - 20, (_pos select 1) - 20, 6]
cutCam cameraEffect ["internal", "back"]
cutCam camSetTarget gunVeh
cutCam camCommit 0
showCinemaBorder true
triSimFrames 60
triScreenshot "01_cutscene_cinema_border"
triAssertNear [(triSamplePixel [0.02, 0.5]), "0,0,0", 16]
triAssertNear [(triSamplePixel [0.97, 0.5]), "0,0,0", 16]
triAssertGt [(triGetPixelMaxChannel [0.4, 0.7]), 10]

cutRsc ["binocular", "plain", 0]
triSimFrames 60
triScreenshot "02_cutscene_rsc_binocular"
triAssertNear [(triSamplePixel [0.02, 0.5]), "0,0,0", 16]
triAssertNear [(triSamplePixel [0.97, 0.5]), "0,0,0", 16]

cutRsc ["default", "plain", 0]
cutCam cameraEffect ["terminate", "back"]
camDestroy cutCam
gunVeh switchCamera "GUNNER"
triSimFrames 60
triScreenshot "03_gunner_after_cutscene"
triAssertGt [(triGetPixelMaxChannel [0.02, 0.5]), 10]
triAssertGt [(triGetPixelMaxChannel [0.97, 0.5]), 10]

triEndTest
