triAssertNgsClient 14
triAssertMissionPlayable
triAssertNetworkAssetExistsForRole ["playerFace", 1, "face.jpg"]
triAssertEq [(triSetView [6719.005, 83.012, 5421.118, 0, 0, -1]), "OK"]
triSimFrames 10
triScreenshot "client1_custom_face"
triAssertLt [(triGetPixelMaxChannel [0.43, 0.20]), 230]
triAssertLt [(triGetPixelMaxChannel [0.45, 0.22]), 160]
triClearView
