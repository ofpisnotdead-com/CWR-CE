triAssertNgsClient 14
triAssertMissionPlayable
triAssertEq [(triMpPlayerNames), "stayer|leaver"]
triAssertEq [format["%1", leaverReady], "1"]
triScreenshot "stayer_both_present"
triAssertEq [(triMpPlayerNames), "stayer"]
triAssertMissionPlayable
triScreenshot "stayer_after_peer_left"
triEndTest
