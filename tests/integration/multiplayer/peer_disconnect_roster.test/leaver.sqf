triAssertNgsClient 14
triAssertMissionPlayable
triAssertEq [(triMpPlayerNames), "stayer|leaver"]
leaverReady = 1
publicVariable "leaverReady"
triWait 3000
triEndTest
