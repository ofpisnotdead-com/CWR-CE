triAssertNgsClient 14
triAssertMissionPlayable
triAssertNetworkAssetExistsForRole ["playerFace", 1, "face.jpg"]
triAssertEq [(triSetRoleFaceView [1, 0.6]), "OK"]
triSimFrames 2
triAssertIncludes [(triRoleFaceTexture 1), "tmp/players/"]
triAssertIncludes [(triRoleFaceTexture 1), "face.jpg"]
triAssertEq [triPauseGame, "OK"]
triAssertEq [(triSetRoleFaceView [1, 0.6]), "OK"]
triWaitFrames 2
triAssertEq [(triAssertRegionHasColor [0.42, 0.42, 0.58, 0.65, 205, 85, 85, 60]), "OK"]
triClearView
triAssertEq [triUnpauseGame, "OK"]
