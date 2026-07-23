triAssertEq [(triDisplay), 0]
triClick 105
triAssertEq [(triDisplay), 8]
triInvokeButton 104
triAssertEq [(triDisplay), 17]
triAssertEq [triSelectListByData [102, "lifecycle-back-navigation"], true]
triInvokeButton 1
triAssertEq [(triDisplay), 70]
triAssertEq [triMpAssignSelfSlot "WEST:1", "OK"]

triAssertEq [(triInvokeButton 2), true]
triAssertEq [(triDisplay), 17]

triAssertEq [triSelectListByData [102, "lifecycle-back-navigation"], true]
triInvokeButton 1
triAssertEq [(triDisplay), 70]
triAssertEq [triMpAssignSelfSlot "WEST:1", "OK"]
triInvokeButton 1
triAssertNgsClient 12
triAssertNgsClient 13
triAssertEq [(triDisplay), 52]

triAssertEq [(triInvokeButton 2), true]
triAssertEq [(triDisplay), 70]
triAssertEq [triMpSlotTaken "WEST:1", "OK"]

triInvokeButton 1
triAssertNgsClient 12
triAssertNgsClient 13
triAssertEq [(triDisplay), 52]
triAssertEq [triMpClientReady 14, "OK"]
triAssertNgsClient 14
triAssertEq [(triDisplay), 52]
triInvokeButton 1
triAssertEq [(triDisplay), 46]
triAssertEq [triInGameplay, "1"]
triAssertEq [triPlayerAuthority, "OK"]
triAssertMissionPlayable
triEndTest
