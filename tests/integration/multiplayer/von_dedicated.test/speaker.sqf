triMpJoin 127.0.0.1 ${ports.game}
triAssertNgsClient 2
triMpPickSide WEST
triMpPickSlot 1
triMpWaitSlotTaken 2
triMpReady 14
triAssertNgsClient 14
triAssertMissionPlayable
triWait 2000
triAssertEq [(triSendVonTestTone [30, 12000]), "OK:30"]
triWait 500
triAssertEq [(triSendVonTestTone [30, 12000]), "OK:30"]
triScreenshot "speaker_sent_tone"
triWait 3000
triEndTest
