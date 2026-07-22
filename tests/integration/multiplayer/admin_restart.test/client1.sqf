triAssertNgsClient 14
triAssertMissionPlayable
triAssertEq [(triNetCommand "#login tri-admin"), "1"]
triWait 3000
triAssertEq [(triNetCommand "#restart"), "1"]
triAssertEq [(triDisplay), 50]
triScreenshot "client1_debriefing"
triInvokeButton 2
triMpAssignSelfSlot "WEST:1"
triAssertNgsClient 13
triAssertEq [(triMpClientReady 14), "OK"]
triAssertNgsClient 14
if (triDisplay == 204) then { triInvokeButton 1 }
triWait 2000
triAssertMissionPlayable
triScreenshot "client1_second_cycle_playable"

triAssertEq [(triNetCommand "#restart"), "1"]
triAssertEq [(triDisplay), 50]
triInvokeButton 2
triMpAssignSelfSlot "WEST:1"
triAssertNgsClient 13
triAssertEq [(triMpClientReady 14), "OK"]
triAssertNgsClient 14
if (triDisplay == 204) then { triInvokeButton 1 }
triWait 2000
triAssertMissionPlayable
triScreenshot "client1_third_cycle_playable"
triEndTest
