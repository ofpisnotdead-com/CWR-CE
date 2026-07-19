triMpJoin 127.0.0.1 ${ports.game}
triAssertNgsClient 2
triMpWaitServerPlayers 127.0.0.1 ${ports.game} 2
triWait 10000
triMpPickSlot WEST:2
triMpWaitSlotTaken WEST:1
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
triMfctiClient2Unit = player
publicVariable "triMfctiClient2Unit"
triMfctiClient2Ready = 1
publicVariable "triMfctiClient2Ready"
triAssertEq [format["%1", triMfctiClient1Ready], "1"]
triAssertEq [isNull triMfctiClient1Unit, false]
triAssertEq [format["%1", triMfctiClient1Damaged], "1"]
triAssertGe [getDammage triMfctiClient1Unit, 0.10]
triMfctiClient2ObservedDamage = 1
publicVariable "triMfctiClient2ObservedDamage"
triMfctiClient2Done = 1
publicVariable "triMfctiClient2Done"
triAssertEq [format["%1", triMfctiClient1Done], "1"]
triAssertEq [format["%1", triMfctiServerRelease], "1"]
triScreenshot "mfcti_client2_initialized"
triMfctiClient2Ack = 1
publicVariable "triMfctiClient2Ack"
triWait 500
triEndTest
