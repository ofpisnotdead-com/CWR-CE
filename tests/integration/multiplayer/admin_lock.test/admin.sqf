triAssertNgsClient 14
triAssertMissionPlayable
triAssertEq [(triNetCommand "#login tri-admin"), "1"]
triWait 1000
triAssertEq [(triNetCommand "#lock"), "1"]
triScreenshot "admin_after_lock"
triWait 3000
triAssertEq [(triNetCommand "#unlock"), "1"]
triScreenshot "admin_after_unlock"
triWait 2000
triEndTest
