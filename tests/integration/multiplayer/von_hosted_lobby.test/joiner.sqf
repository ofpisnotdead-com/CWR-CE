triAssertEq [(triDisplay), 0]
triBarrier hosted_lobby_ready
triClick 105
triAssertEq [(triDisplay), 8]
triAssertEq [triSelectListByData [102, ":${ports.game}"], true]
triInvokeButton 105
triAssertEq [(triDisplay), 70]
triAssertEq [triMpAssignSelfSlot "WEST:2", "OK"]
triKeyDown 57
triWait 100
triAssertEq [(triGetVonPushToTalkAction), 1]
triAssertEq [(triGetVoiceChatActive), 1]
triAssertEq [(triGetVoiceChannel), 0]
triAssertEq [(triSendVonTestTone [12, 12000]), "OK:12"]
triKeyUp 57
triWait 50
triKeyDown 57
triWait 100
triAssertEq [(triGetVoiceChannel), 0]
triAssertEq [(triSendVonTestTone [12, 12000]), "OK:12"]
triKeyUp 57
triWait 50
triKeyDown 57
triWait 100
triAssertEq [(triGetVoiceChannel), 0]
triAssertEq [(triSendVonTestTone [12, 12000]), "OK:12"]
triKeyUp 57
triWait 250
triEndTest
