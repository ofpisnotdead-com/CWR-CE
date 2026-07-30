triAssertNgsClient 14
triAssertMissionPlayable
triAssertEq [(triNetCommand "#login tri-admin"), "1"]
triAssertEq [(triGetAdminLoggedIn), "1"]
triAssertEq [format["%1", banneeReady], "1"]
triAssertEq [(triMpPlayerNames), "admin|bannee"]
triAssertEq [(triNetCommand "#ban bannee"), "1"]
triAssertEq [(triMpPlayerNames), "admin"]
triScreenshot "admin_after_ban"
triAssertIncludes [format["%1", unbanCommand], "#unban "]
triAssertEq [(triNetCommand unbanCommand), "1"]
triScreenshot "admin_after_unban"
triAssertEq [format["%1", unbanDone], "1"]
unbanAck = 1
publicVariable "unbanAck"
triEndTest
