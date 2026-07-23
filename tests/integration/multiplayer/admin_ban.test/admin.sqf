triAssertNgsClient 14
triAssertMissionPlayable
triAssertEq [(triNetCommand "#login tri-admin"), "1"]
triAssertEq [format["%1", banneeReady], "1"]
triAssertEq [(triMpPlayerNames), "admin|bannee"]
triAssertEq [(triNetCommand "#ban bannee"), "1"]
triAssertEq [(triMpPlayerNames), "admin"]
triScreenshot "admin_after_ban"
triAssertEq [format["%1", unbanReady], "1"]
triAssertEq [(triNetCommand format["#unban %1", banId]), "1"]
triScreenshot "admin_after_unban"
triWait 3000
triEndTest
