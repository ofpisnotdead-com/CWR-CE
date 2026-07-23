triAssertNgsClient 14
triAssertMissionPlayable
triAssertEq [format["%1", c2in], "1"]
triAssertEq [(triMpPlayerNames), "client1|client2"]
triScreenshot "client1_both_in"
triEndTest
