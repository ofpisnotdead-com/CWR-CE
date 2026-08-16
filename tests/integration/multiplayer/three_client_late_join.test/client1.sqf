triAssertNgsClient 14
triAssertMissionPlayable
triAssertEq [triPlayerAuthority, "OK"]
triAssertEq [(triMpPlayerNames), "client1|client2|client3"]
triScreenshot "client1_three_players"
triWait 8000
triEndTest
