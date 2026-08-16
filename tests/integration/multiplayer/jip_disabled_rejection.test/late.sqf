triAssertEq [(triDisplay), 0]
triBarrier anchor_playing
triMpJoin 127.0.0.1 ${ports.game}
triWait 5000
triAssertEq [(triDisplay), 22]
triBarrier late_joined
triBarrier anchor_checked
triEndTest
