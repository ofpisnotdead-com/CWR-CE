triAssertNgsClient 6
triWait 5000
triAssertEq [(triGetVoiceChannel), -1]
triKeyDown 57
triWait 100
triAssertEq [(triGetVonPushToTalkAction), 1]
triAssertEq [(triGetVoiceChatActive), 1]
triAssertEq [(triGetVoiceChannel), 0]
triAssertEq [(triSendVonTestTone [12, 12000]), "OK:12"]
triKeyUp 57
triWait 50
triAssertEq [(triGetVoiceChannel), -1]
triAssertEq [(triGetVoiceChatActive), 0]
triKeyDown 57
triWait 100
triAssertEq [(triGetVonPushToTalkAction), 1]
triAssertEq [(triGetVoiceChatActive), 1]
triAssertEq [(triGetVoiceChannel), 0]
triAssertEq [(triSendVonTestTone [12, 12000]), "OK:12"]
triKeyUp 57
triWait 50
triAssertEq [(triGetVoiceChannel), -1]
triAssertEq [(triGetVoiceChatActive), 0]
triKeyDown 57
triWait 100
triAssertEq [(triGetVonPushToTalkAction), 1]
triAssertEq [(triGetVoiceChatActive), 1]
triAssertEq [(triGetVoiceChannel), 0]
triAssertEq [(triSendVonTestTone [12, 12000]), "OK:12"]
triKeyUp 57
triWait 50
triAssertEq [(triGetVoiceChannel), -1]
triAssertEq [(triGetVoiceChatActive), 0]
triKeyDown 57
triWait 100
triAssertEq [(triGetVonPushToTalkAction), 1]
triAssertEq [(triGetVoiceChatActive), 1]
triAssertEq [(triGetVoiceChannel), 0]
triAssertEq [(triSendVonTestTone [12, 12000]), "OK:12"]
triKeyUp 57
triWait 50
triAssertEq [(triGetVoiceChannel), -1]
triAssertEq [(triGetVoiceChatActive), 0]
triKeyDown 57
triWait 100
triAssertEq [(triGetVonPushToTalkAction), 1]
triAssertEq [(triGetVoiceChatActive), 1]
triAssertEq [(triGetVoiceChannel), 0]
triAssertEq [(triSendVonTestTone [12, 12000]), "OK:12"]
triKeyUp 57
triWait 250
triAssertEq [(triGetVoiceChannel), -1]
triAssertEq [(triGetVoiceChatActive), 0]
triEndTest
