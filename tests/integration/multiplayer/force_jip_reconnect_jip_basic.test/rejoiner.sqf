triWait 35000
triMpJoin 127.0.0.1 ${ports.game}
triAssertNgsClient 2
triMpAssignSelfSlot "WEST:1"
triAssertNgsClient 14
if (triDisplay == 204) then { triInvokeButton 1 }
triWait 3000
triAssertMissionPlayable
triScreenshot "rejoiner_reused_slot_playable"
triEndTest
