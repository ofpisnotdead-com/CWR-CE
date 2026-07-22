triAssertNgsClient 14
triAssertMissionPlayable
triAssertEq [(triNetCommand "#login tri-admin"), "1"]
triAssertEq [(triMpPlayerNames), "client1|kicked"]
triAssertEq [format["%1", kickedready], "1"]
triWait 1000
triAssertEq [(triNetCommand "#kick kicked"), "1"]
triAssertEq [(triMpPlayerNames), "client1"]
triScreenshot "client1_after_kick"
triEndTest
