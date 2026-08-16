triAssertNgsClient 14
triAssertMissionPlayable
triScreenshot "late_joined"
triAssertNetworkAssetExistsForRole ["sound", 0, "tri_mp_ping.wav"]
triScreenshot "late_has_early_sound"
lateDone = 1
publicVariable "lateDone"
triWait 2000
triEndTest
