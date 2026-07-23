triAssertEq [(triDisplay), 0]
triClick 105
triAssertEq [(triDisplay), 8]
triInvokeButton 104
triAssertEq [(triDisplay), 17]
triAssertEq [(triAssertListText [102, "hosted-first-join"]), "OK"]
triAssertEq [triSelectListByData [102, "hosted-first-join"], true]
triScreenshot "host_server_mission"
triInvokeButton 1
triAssertEq [(triDisplay), 70]
triScreenshot "host_setup"
triAssertEq [triMpAssignSelfSlot "WEST:1", "OK"]
triScreenshot "host_assigned"
triMpWaitSlotTaken WEST:2
triMpWaitSlotTaken WEST:3
triBarrier slots_taken
triInvokeButton 1
triAssertNgsClient 12
triAssertNgsClient 13
triAssertEq [triMpClientReady 14, "OK"]
triAssertNgsClient 14
triAssertEq [(triDisplay), 52]
triInvokeButton 1
if (triDisplay == 204) then { triInvokeButton 1 }
triWait 3000
triLogPlayState host_before_playable_assert
triAssertMissionPlayable
triScreenshot "host_playable"
triBarrier playable
triEndTest
