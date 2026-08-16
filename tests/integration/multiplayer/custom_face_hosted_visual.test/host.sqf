triAssertEq [(triDisplay), 0]
triClick 105
triAssertEq [(triDisplay), 8]
triInvokeButton 104
triAssertEq [(triDisplay), 17]
triAssertEq [(triAssertListText [102, "custom-face-hosted"]), "OK"]
triAssertEq [triSelectListByData [102, "custom-face-hosted"], true]
triInvokeButton 1
triAssertEq [(triDisplay), 70]
triAssertEq [triMpAssignSelfSlot "WEST:1", "OK"]
triMpWaitSlotTaken WEST:2
triBarrier slots_taken
triInvokeButton 1
triAssertNgsClient 12
triAssertNgsClient 13
triAssertEq [triMpClientReady 14, "OK"]
triAssertNgsClient 14
triAssertEq [(triDisplay), 52]
triInvokeButton 1
if (triDisplay == 204) then { triInvokeButton 1 }
triAssertMissionPlayable
triAssertEq [triPlayerFace, "custom"]
triAssertIncludes [(triRoleFaceTexture 0), "face.jpg"]
triAssertNetworkAssetExistsForRole ["playerFace", 1, "face.jpg"]
triAssertIncludes [(triRoleFaceTexture 1), "tmp/players/"]
triAssertIncludes [(triRoleFaceTexture 1), "face.jpg"]
triAssertEq [(triSetRoleFaceView [1, 1.2]), "OK"]
triSimFrames 10
triScreenshot "host_joiner_custom_face"
triClearView
triBarrier host_face_checked
triBarrier playable
triEndTest
