triAssertEq [(triDisplay), 0]
triClick 105
triAssertEq [(triDisplay), 8]
triWait 8000
triAssertEq [triSelectList [102, 0], true]
triInvokeButton 105
triAssertEq [(triDisplay), 70]
triAssertEq [triMpAssignSelfSlot "WEST:2", "OK"]
triBarrier slots_taken
triAssertNgsClient 12
triAssertNgsClient 14
triInvokeButton 1
if (triDisplay == 204) then { triInvokeButton 1 }
triAssertMissionPlayable
triAssertEq [triPlayerFace, "custom"]
triAssertIncludes [(triRoleFaceTexture 1), "face.jpg"]
triAssertNetworkAssetExistsForRole ["playerFace", 0, "face.jpg"]
triBarrier host_face_checked
triAssertIncludes [(triRoleFaceTexture 0), "tmp/players/"]
triAssertIncludes [(triRoleFaceTexture 0), "face.jpg"]
triAssertEq [(triSetRoleFaceView [0, 1.2]), "OK"]
triSimFrames 10
triScreenshot "joiner_host_custom_face"
triClearView
triBarrier playable
triEndTest
