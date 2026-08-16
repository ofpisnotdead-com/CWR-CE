triAssertEq [(triDisplay), 0]
triClick 105
triAssertEq [(triDisplay), 8]
triInvokeButton 104
triAssertEq [(triDisplay), 17]
triAssertEq [(triAssertListText [102, "hosted-von"]), "OK"]
triAssertEq [triSelectListByData [102, "hosted-von"], true]
triInvokeButton 1
triAssertEq [(triDisplay), 70]
triAssertEq [triMpAssignSelfSlot "WEST:1", "OK"]
triBarrier hosted_lobby_ready
triMpWaitSlotTaken WEST:2
triResetVonReceived
triBarrier burst1_start
triAssertVonSpeaking 1
triAssertVonReceived 20
triBarrier burst1_end
triResetVonReceived
triBarrier burst2_start
triAssertVonReceived 30
triAssertVonSpeaking 1
triBarrier burst2_end
triResetVonReceived
triBarrier burst3_start
triAssertVonReceived 10
triAssertVonSpeaking 1
triBarrier burst3_end
triEndTest
