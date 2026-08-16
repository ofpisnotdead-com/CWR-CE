triAssertNgsClient 14
triAssertMissionPlayable
triAssertNetworkAssetExistsForRole ["playerFace", 0, "face.jpg"]
triAssertNetworkAssetExistsForRole ["sound", 0, "tri_mp_ping.wav"]
triAssertEq [triCustomRadio, "tri_mp_ping.wav"]
customSoundReceiverReady = 1
publicVariable "customSoundReceiverReady"
triAssertGt [(triRadioWaveOffset "tri_mp_ping.wav"), -1]
triScreenshot "client2_custom_media"
