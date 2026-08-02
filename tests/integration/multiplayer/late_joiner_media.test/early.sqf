triAssertNgsClient 14
triAssertMissionPlayable
triScreenshot "early_playing"
triAssertNetworkAssetExistsForRole ["sound", 1, "tri_mp_ping.wav"]
triScreenshot "early_has_late_sound"
triAssertEq [format["%1", lateDone], "1"]
triEndTest
