triMpJoin 127.0.0.1 ${ports.game}
triAssertNgsClient 2
triMpPickSide WEST
triMpPickSlot 2
triMpWaitSlotTaken 1
triMpReady 14
triAssertNgsClient 14
triAssertMissionPlayable
triResetVonReceived
triAssertVonReceived 5
triAssertVonSpeaking 1
triScreenshot "listener_von_received"
triEndTest
