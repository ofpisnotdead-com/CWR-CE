triAssertNgsClient 14
triAssertMissionPlayable
triBarrier play_ready
triAssertVonReceived 1
triAssertVonSpeaking 1
triBarrier alice_done
triAssertEq [(triSendVonTestTone [120, 12000]), "OK:120"]
triWait 2000
triScreenshot "bob_heard_alice"
triEndTest
