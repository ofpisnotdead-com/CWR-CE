triMpJoin 127.0.0.1 ${ports.game}
triAssertNgsClient 2
triMpPickSide WEST
triMpPickSlot 1
triMpWaitSlotTaken 2
triAssertNgsClient 14
triEndTest
