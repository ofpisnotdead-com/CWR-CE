triAssertEq [(triDisplay), 0]
triWait 10000
triMpJoin 127.0.0.1 ${ports.game}
triMpAssignSelfSlot "WEST:2"
triAssertNgsClient 14
triInvokeButton 1
if (triDisplay == 204) then { triInvokeButton 1 }
triWait 3000
triAssertMissionPlayable
kickedready = 1
publicVariable "kickedready"
triAssertEq [(triDisplay), 0]
triScreenshot "kicked_back_at_menu"
triEndTest
