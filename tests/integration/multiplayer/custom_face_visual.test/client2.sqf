triAssertNgsClient 14
triAssertMissionPlayable
triAssertNetworkAssetExistsForRole ["playerFace", 0, "face.jpg"]
triAssertEq [(triSetRoleFaceView [0, 0.6]), "OK"]
triSimFrames 2
triAssertIncludes [(triRoleFaceTexture 0), "tmp/players/"]
triAssertIncludes [(triRoleFaceTexture 0), "face.jpg"]
triAssertEq [triPauseGame, "OK"]
triAssertEq [(triSetRoleFaceView [0, 0.6]), "OK"]
triWaitFrames 2
triAssertEq [(triAssertRegionHasColor [0.42, 0.42, 0.58, 0.65, 55, 70, 205, 60]), "OK"]
triClearView
triAssertEq [triUnpauseGame, "OK"]
