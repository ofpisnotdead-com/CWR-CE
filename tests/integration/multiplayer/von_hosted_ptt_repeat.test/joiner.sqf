triBarrier hosted_lobby_ready
triClick 105
triAssertEq [(triDisplay), 8]
triAssertEq [triSelectListByData [102, ":${ports.game}"], true]
triInvokeButton 105
triAssertEq [(triDisplay), 70]
triAssertEq [triMpAssignSelfSlot "WEST:2", "OK"]
triHoldKey 57
triWait 100
triAssertEq [(triGetVonPushToTalkAction), 1]
triAssertEq [(triGetVoiceChatActive), 1]
triAssertEq [(triGetVoiceChannel), 0]
triAssertEq [(triGetVonTransmitHealth), 1]
triBarrier burst1_start
triWait 1500
triBarrier burst1_end
triReleaseKey 57
triWait 1500
triHoldKey 57
triWait 100
triAssertEq [(triGetVoiceChannel), 0]
triBarrier burst2_start
triWait 1500
triBarrier burst2_end
triReleaseKey 57
triWait 1500
triHoldKey 57
triWait 100
triBarrier burst3_start
triWait 1500
triBarrier burst3_end
triReleaseKey 57
triEndTest
