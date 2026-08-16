triAssertNgsClient 14
triAssertMissionPlayable
triAssertNetworkAssetExistsForRole ["playerFace", 1, "face.jpg"]
triAssertNetworkAssetExistsForRole ["sound", 1, "tri_mp_ping.wav"]
triAssertEq [triCustomRadio, "tri_mp_ping.wav"]
triAssertEq [format["%1", customSoundReceiverReady], "1"]
triAssertEq [(triPlayCustomRadio 0), "OK"]
triAssertGt [(triRadioWaveOffset "tri_mp_ping.wav"), -1]
triScreenshot "client1_custom_media"
