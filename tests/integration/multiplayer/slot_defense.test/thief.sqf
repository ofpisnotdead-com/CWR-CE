triAssertEq [(triDisplay), 0]
triWait 2000
triMpJoin 127.0.0.1 ${ports.game}
triAssertEq [(triMpAssignSelfSlot "WEST:1"), "FAIL:occupied"]
triAssertEq [(triMpAssignSelfSlot "WEST:2"), "OK"]
triAssertNgsClient 14
triInvokeButton 1
if (triDisplay == 204) then { triInvokeButton 1 }
triWait 3000
triAssertMissionPlayable
thiefTried = 1
publicVariable "thiefTried"
triScreenshot "thief_on_west2"
triWait 6000
triEndTest
