triMpJoin 127.0.0.1 ${ports.game}
triAssertNgsClient 2
triMpWaitServerPlayers 127.0.0.1 ${ports.game} 2
triWait 10000
triMpPickSlot WEST:1
triMpWaitSlotTaken WEST:2
triAssertNgsClient 13
triInvokeButton 1
triAssertNgsClient 14
triAssertMissionPlayable
triAssertEq [triPlayerAuthority, "OK"]
triAssertEq [format["%1", gameStarted], "true"]
triAssertEq [format["%1", serverStarted], "true"]
triAssertEq [format["%1", gamePropertiesStarted], "true"]
triAssertEq [format["%1", mapCenterComplete], "true"]
triAssertEq [format["%1", MFCTI116Extensions], "true"]
triAssertEq [format["%1", westResources], "9000"]
triAssertEq [format["%1", eastResources], "9000"]
triAssertEq [isNull westStartingLocation, false]
triAssertEq [isNull eastStartingLocation, false]
triAssertGe [(getPos westStartingLocation) select 0, 2000]
triAssertGe [(getPos eastStartingLocation) select 0, 2000]
triAssertEq [primaryWeapon player, "M16"]
triAssertPlayerMoves 26 2500 0.25
triAssertEq [triFirePlayerWeapon, "OK"]
triMfctiClient1Unit = player
publicVariable "triMfctiClient1Unit"
triMfctiClient1Ready = 1
publicVariable "triMfctiClient1Ready"
triAssertEq [format["%1", triMfctiClient2Ready], "1"]
triAssertEq [triDamagePlayer 0.25, "OK"]
triAssertGe [triPlayerDammage, 0.20]
triMfctiClient1Damaged = 1
publicVariable "triMfctiClient1Damaged"
triAssertEq [format["%1", triMfctiClient2ObservedDamage], "1"]
triMfctiClient1Done = 1
publicVariable "triMfctiClient1Done"
triAssertEq [format["%1", triMfctiClient2Done], "1"]
triAssertEq [format["%1", triMfctiServerRelease], "1"]
triScreenshot "mfcti_client1_initialized"
triMfctiClient1Ack = 1
publicVariable "triMfctiClient1Ack"
triWait 500
triEndTest
