triAssertNgsClient 14
triAssertMissionPlayable
triBarrier initial_media_observed
triAssertEq [(triReplaceAndUploadCustomSound ["replacement.wav", "same_size.wav"]), "OK:64"]
triBarrier replacement_uploaded
triBarrier replacement_observed
triAssertEq [(triReplaceAndUploadCustomSound ["original.wav", "same_size.wav"]), "OK:64"]
triBarrier original_reuploaded
triBarrier original_reobserved
triEndTest
