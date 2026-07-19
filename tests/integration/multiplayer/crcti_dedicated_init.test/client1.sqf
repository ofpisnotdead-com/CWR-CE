triMpJoin 127.0.0.1 ${ports.game}
triAssertNgsClient 2
triMpWaitServerPlayers 127.0.0.1 ${ports.game} 2
triWait 10000
triMpPickSlot WEST:1
triMpWaitSlotTaken WEST:2
triAssertNgsClient 13
triInvokeButton 1
triAssertNgsClient 14
triAssertEq [format["%1", param1], "9"]
triAssertEq [format["%1", param2], "0"]
triAssertEq [primaryWeapon player, "DVD_G36a"]
triAssertEq [dialog, true]
triAssertMissionPlayable
triAssertEq [triPlayerAuthority, "OK"]
triAssertEq [format["%1", siPlayer], "0"]
triAssertEq [format["%1", giPlayer], "0"]
triAssertEq [groupPlayer == group player, true]
triAssertEq [groupPlayer == ((groupMatrix select siPlayer) select giPlayer), true]
triAssertEq [format["%1", count aiOrders], "15"]
triAssertEq [format["%1", (aiOrders select 14) select 0], "Disband"]
triAssertEq [bBlockedByClient, false]
triAssertEq [isNull (mhq select 0), false]
triAssertEq [isNull (mhq select 1), false]
triAssertEq [alive (mhq select 0), true]
triAssertEq [alive (mhq select 1), true]
triAssertEq [triFirePlayerWeapon, "OK"]
triCrctiClient1Unit = player
publicVariable "triCrctiClient1Unit"
triCrctiClient1Ready = 1
publicVariable "triCrctiClient1Ready"
triAssertEq [format["%1", triCrctiClient2Ready], "1"]
triAssertEq [triDamagePlayer 0.25, "OK"]
triAssertGe [triPlayerDammage, 0.20]
triCrctiClient1Damaged = 1
publicVariable "triCrctiClient1Damaged"
triAssertEq [format["%1", triCrctiClient2ObservedDamage], "1"]
triCrctiClient1Done = 1
publicVariable "triCrctiClient1Done"
triAssertEq [format["%1", triCrctiClient2Done], "1"]
triAssertEq [format["%1", triCrctiServerRelease], "1"]
triScreenshot "crcti_client1_initialized"
triCrctiClient1Ack = 1
publicVariable "triCrctiClient1Ack"
triWait 500
triEndTest
