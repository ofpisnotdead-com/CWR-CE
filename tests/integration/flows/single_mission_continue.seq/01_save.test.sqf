triSetLanguage "English"
triAssertEq [(triDisplay), 0]

triClickText "SINGLE MISSION"
triAssertEq [(triDisplay), 2]
triClickText "Play"
triSendKey 41
triClick 1
triAssertMissionPlayable

player setPos [6710, 5408, 0]
triSendKey 41
triAssertEq [(triDisplay), 49]
triAssert [(triGetControlVisible 103)]
triClick 103
triAssertMissionPlayable

triEndTest
