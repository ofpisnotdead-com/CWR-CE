triMpJoin 127.0.0.1 ${ports.game}
triAssertNgsClient 2
triMpWaitServerPlayers 127.0.0.1 ${ports.game} 2
triWait 10000
triMpPickSlot WEST:1
triMpWaitSlotTaken WEST:2
triAssertNgsClient 13
triInvokeButton 1
triAssertNgsClient 14
if (triDisplay == 204) then { triInvokeButton 1 }
triWait 3000
triAssertMissionPlayable
triScreenshot "leaver_playable_before_exit"
triEndTest
