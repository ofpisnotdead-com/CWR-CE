triAssertNgs 14
triAssertNgsClient 14
triAssertEq [format["%1", param1], "9"]
triAssertEq [format["%1", param2], "0"]
triAssertEq [isNull (mhq select 0), false]
triAssertEq [isNull (mhq select 1), false]
triAssertEq [alive (mhq select 0), true]
triAssertEq [alive (mhq select 1), true]
triAssertEq [format["%1", triCrctiClient1Done], "1"]
triAssertEq [format["%1", triCrctiClient2Done], "1"]
triAssertEq [format["%1", triCrctiClient2ObservedDamage], "1"]
triCrctiServerRelease = 1
publicVariable "triCrctiServerRelease"
triAssertEq [format["%1", triCrctiClient1Ack], "1"]
triAssertEq [format["%1", triCrctiClient2Ack], "1"]
triWait 1000
triEndTest
