triAssertNgsClient 14
triAssertMissionPlayable
triBarrier anchor_playing
triWait 10000
triAssertEq [(triMpPlayerNames), "anchor"]
triAssertMissionPlayable
triScreenshot "anchor_still_playing_after_rejected_jip"
triEndTest
