triAssertEq [(triDisplay), 0]
triBarrier anchor_playing
triMpJoin 127.0.0.1 ${ports.game}
triWait 6000
triAssertEq [(triDisplay), 0]
triAssertLt [triGetNgsClientState, 13]
triScreenshot "late_rejected_to_menu"
triEndTest
