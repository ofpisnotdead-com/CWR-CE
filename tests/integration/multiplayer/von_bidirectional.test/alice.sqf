triAssertNgsClient 14
triAssertMissionPlayable
triBarrier play_ready
triAssertEq [(triSendVonTestTone [120, 12000]), "OK:120"]
triWait 2000
triBarrier alice_done
triAssertVonReceived 1
triAssertVonSpeaking 1
triScreenshot "alice_heard_bob"
triEndTest
