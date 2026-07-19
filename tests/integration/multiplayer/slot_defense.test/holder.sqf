triAssertNgsClient 14
triAssertMissionPlayable
holderReady = 1
publicVariable "holderReady"
triScreenshot "holder_playing"
triAssertEq [format["%1", thiefTried], "1"]
triWait 3000
triAssertMissionPlayable
triAssertEq [(triMpPlayerNames), "holder|thief"]
triScreenshot "holder_kept_slot"
triEndTest
