triAssertNgs 14
triAssertNgsClient 14
triAssertEq [format["%1", param1], "1800"]
triAssertEq [format["%1", A2], "1"]
triAssertEq [format["%1", paintballClient1Done], "1"]
triAssertEq [format["%1", paintballClient2Done], "1"]
paintballServerDone = 1
publicVariable "paintballServerDone"
triWait 1000
triEndTest
