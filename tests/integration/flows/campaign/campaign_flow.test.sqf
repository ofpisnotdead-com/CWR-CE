// Guard: main menu has all primary nav buttons enabled, and the campaign
// screen opens and returns cleanly.
//
// Folded from all_buttons_enabled: asserts enabled states for all four main
// nav buttons (117=SingleMission, 101=Campaign, 105=MultiPlayer, 115=Editor).
// campaign_flow: verifies campaign button (101) opens display 43 and back
// returns to the main menu.
triAssertEq [(triDisplay), 0]
triAssert [(triGetControlEnabled 117)]
triAssert [(triGetControlEnabled 101)]
triAssert [(triGetControlEnabled 105)]
triAssert [(triGetControlEnabled 115)]
triClick 101
triAssertEq [(triDisplay), 43]
triWaitFrames 5
triSendKey 41
triSimUntil { triDisplay == 0 }
triAssertEq [(triDisplay), 0]
triEndTest
