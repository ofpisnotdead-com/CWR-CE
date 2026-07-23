triAssertEq [(triDisplay), 0]
triWait 20000
triMpJoin 127.0.0.1 ${ports.game}
triMpAssignSelfSlot "WEST:1"
triAssertNgsClient 14
triInvokeButton 1
if (triDisplay == 204) then { triInvokeButton 1 }
triWait 3000
triAssertMissionPlayable
triAssertEq [(triMpPlayerNames), "rejoiner"]
triScreenshot "rejoiner_reused_slot_playable"
triEndTest
