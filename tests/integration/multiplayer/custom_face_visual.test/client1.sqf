triAssertNgsClient 14
triAssertMissionPlayable
triAssertNetworkAssetExistsForRole ["playerFace", 1, "face.paa"]
triWait 5000
triScreenshot "client1_custom_face"
triAssertLt [(triGetPixelMaxChannel [0.525, 0.86]), 220]
triAssertLt [(triGetPixelMaxChannel [0.5375, 0.883]), 220]
