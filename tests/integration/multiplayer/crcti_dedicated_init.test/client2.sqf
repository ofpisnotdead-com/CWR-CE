triMpJoin 127.0.0.1 ${ports.game}
triAssertNgsClient 2
triMpWaitServerPlayers 127.0.0.1 ${ports.game} 2
triWait 10000
triMpPickSlot WEST:2
triMpWaitSlotTaken WEST:1
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
triAssertEq [format["%1", giPlayer], "1"]
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
triCrctiClient2Unit = player
publicVariable "triCrctiClient2Unit"
triCrctiClient2Ready = 1
publicVariable "triCrctiClient2Ready"
triAssertEq [format["%1", triCrctiClient1Ready], "1"]
triAssertEq [isNull triCrctiClient1Unit, false]
triAssertEq [format["%1", triCrctiClient1Damaged], "1"]
triAssertGe [getDammage triCrctiClient1Unit, 0.10]
triCrctiClient2ObservedDamage = 1
publicVariable "triCrctiClient2ObservedDamage"
triCrctiClient2Done = 1
publicVariable "triCrctiClient2Done"
triAssertEq [format["%1", triCrctiClient1Done], "1"]
triAssertEq [format["%1", triCrctiServerRelease], "1"]
triScreenshot "crcti_client2_initialized"
triCrctiClient2Ack = 1
publicVariable "triCrctiClient2Ack"
triWait 500
triEndTest
