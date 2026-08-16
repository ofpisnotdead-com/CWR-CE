triBarrier lobby_ready
triClick 105
triAssertEq [(triDisplay), 8]
triAssertEq [triSelectListByData [102, ":${ports.game}"], true]
triInvokeButton 105
triAssertEq [(triDisplay), 70]
triAssertEq [triMpAssignSelfSlot "WEST:2", "OK"]
triAssertEq [(triGetVonTransmitHealth), 0]
triHoldKey 57
triWait 100
triAssertEq [(triGetVonPushToTalkAction), 1]
triAssertEq [(triGetVoiceChatActive), 1]
triBarrier ptt_held
triAssertEq [(triGetVonTransmitHealth), 4]
triBarrier health_checked
triBarrier done
triReleaseKey 57
triEndTest
