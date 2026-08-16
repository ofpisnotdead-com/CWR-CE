triMpJoin 127.0.0.1 ${ports.game}
triAssertNgsClient 2
triMpPickSide WEST
triMpPickSlot 2
triMpWaitSlotTaken 1
triAssertNgsClient 14
triEndTest
