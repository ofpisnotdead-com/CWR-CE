triAssertNgsClient 14
triAssertMissionPlayable
c2in = 1
publicVariable "c2in"
triAssertEq [(triMpPlayerNames), "client1|client2"]
triScreenshot "client2_late_join_playable"
triEndTest
