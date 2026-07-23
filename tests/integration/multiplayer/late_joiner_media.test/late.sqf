triAssertNgsClient 14
triAssertMissionPlayable
triScreenshot "late_joined"
triAssertNetworkAssetExistsForPlayerName ["playerFace", "early", "face.jpg"]
triAssertNetworkAssetExistsForPlayerName ["sound", "early", "tri_mp_ping.wav"]
triScreenshot "late_has_early_media"
lateDone = 1
publicVariable "lateDone"
triWait 2000
triEndTest
