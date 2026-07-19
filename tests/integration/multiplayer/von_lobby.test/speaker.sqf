triAssertNgsClient 6
triWait 5000
triAssertEq [(triSendVonTestTone [30, 12000]), "OK:30"]
triWait 500
triAssertEq [(triSendVonTestTone [30, 12000]), "OK:30"]
triScreenshot "speaker_lobby_sent_tone"
triWait 2000
triEndTest
