triAssertEq [triPlayerFace, "custom"]
triAssertEq [triCustomRadio, "tri_profile_ping.wav"]

triAssertEq [(triSetPlayerFaceView [1.2, 1.55]), "OK"]
triSimFrames 10
triScreenshot "local_custom_skin"
triAssertEq [(triAssertPixelNotWhite [0.5, 0.5, 245]), "OK"]
triClearView

triAssertEq [(triPlayCustomRadio 0), "OK"]
triAssertGt [(triRadioWaveOffset "tri_profile_ping.wav"), -1]
triEndTest
