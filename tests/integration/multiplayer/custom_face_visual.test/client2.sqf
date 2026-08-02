triAssertNgsClient 14
triAssertMissionPlayable
triAssertNetworkAssetExistsForRole ["playerFace", 0, "face.jpg"]
triAssertEq [(triSetView [6710.528, 83.608, 5410.092, 0, 0, -1]), "OK"]
triSimFrames 10
triScreenshot "client2_custom_face"
triAssertLt [(triGetPixelMaxChannel [0.28, 0.20]), 220]
triAssertLt [(triGetPixelMaxChannel [0.33, 0.19]), 180]
triClearView
