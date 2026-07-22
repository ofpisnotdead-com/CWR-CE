triAssertNgsClient 14
triAssertMissionPlayable
triScreenshot "early_playing"
triAssertNetworkAssetExistsForPlayerName ["playerFace", "late", "face.jpg"]
triAssertNetworkAssetExistsForPlayerName ["sound", "late", "tri_mp_ping.wav"]
triScreenshot "early_has_late_media"
triAssertEq [format["%1", lateDone], "1"]
triEndTest
