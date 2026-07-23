triAssertNgsClient 14
triAssertMissionPlayable
triAssertEq [(triNetworkAssetByteForPlayerName ["sound", "source", "same_size.wav", 29]), "64:65"]
triBarrier initial_media_observed
triBarrier replacement_uploaded
triAssertEq [(triNetworkAssetByteForPlayerName ["sound", "source", "same_size.wav", 29]), "64:66"]
triBarrier replacement_observed
triBarrier original_reuploaded
triAssertEq [(triNetworkAssetByteForPlayerName ["sound", "source", "same_size.wav", 29]), "64:65"]
triBarrier original_reobserved
triEndTest
