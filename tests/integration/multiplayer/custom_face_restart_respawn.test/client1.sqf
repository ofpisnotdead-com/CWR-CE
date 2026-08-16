triAssertNgsClient 14
triAssertMissionPlayable
triAssertEq [triPlayerFace, "custom"]
triAssertEq [(triSetPlayerFaceView [1.2, 1.55]), "OK"]
triSimFrames 10
triScreenshot "client1_before_restart"
triAssertEq [(triAssertPixelNotWhite [0.5, 0.5, 245]), "OK"]
triClearView

triAssertEq [(triNetCommand "#login tri-admin"), "1"]
triWait 1000
triAssertEq [(triNetCommand "#restart"), "1"]
triAssertEq [(triDisplay), 50]
triInvokeButton 2
triMpAssignSelfSlot "WEST:1"
triAssertNgsClient 13
triAssertEq [(triMpClientReady 14), "OK"]
triAssertNgsClient 14
if (triDisplay == 204) then { triInvokeButton 1 }
triAssertMissionPlayable
triAssertEq [triPlayerFace, "custom"]
triAssertEq [(triSetPlayerFaceView [1.2, 1.55]), "OK"]
triSimFrames 10
triScreenshot "client1_after_restart_before_respawn"
triAssertEq [(triAssertPixelNotWhite [0.5, 0.5, 245]), "OK"]
triClearView

triAssertEq [(triDamagePlayer 1), "OK"]
triWait 8000
triAssertNgsClient 14
triAssertMissionPlayable
triAssertEq [triPlayerFace, "custom"]
triAssertEq [(triSetPlayerFaceView [1.2, 1.55]), "OK"]
triSimFrames 10
triScreenshot "client1_after_respawn"
triAssertEq [(triAssertPixelNotWhite [0.5, 0.5, 245]), "OK"]
triClearView
triEndTest
