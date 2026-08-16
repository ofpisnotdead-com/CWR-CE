triAssertNgsClient 14
triAssertGe [triMpTransferOverlayShows, 1]
triAssertLe [triMpTransferOverlayShows, 1]
triWait 1000
triScreenshot "client1_round_start"
triAssertMissionPlayable
triAssertEq [player == U1, true]
triAssertEq [format["%1", param1], "1800"]
triAssertEq [format["%1", A0], "1"]
triAssertEq [triGetCameraEffectActive, 1]
triAssertEq [format["%1", A2], "1"]
triAssertEq [triGetCameraEffectActive, 0]
triAssertEq [primaryWeapon player, "M4"]
triAssertPlayerMoves 26 2500 0.25
triAssertEq [triFirePlayerWeapon, "OK"]
triAssertEq [triDamagePlayer 0.25, "OK"]
triAssertGe [triPlayerDammage, 0.20]
paintballClient1Done = 1
publicVariable "paintballClient1Done"
triAssertEq [format["%1", paintballClient2Done], "1"]
triAssertEq [format["%1", paintballServerDone], "1"]
triScreenshot "client1_initialized_and_replicating"
triEndTest
