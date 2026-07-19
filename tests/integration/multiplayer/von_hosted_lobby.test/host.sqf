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
triAssertVonReceived 1
triWait 250
triAssertVonReceived 2
triWait 250
triAssertVonReceived 3
triEndTest
