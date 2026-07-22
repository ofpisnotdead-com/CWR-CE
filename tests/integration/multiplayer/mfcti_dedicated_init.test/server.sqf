triAssertNgs 14
triAssertNgsClient 14
triAssertEq [format["%1", gameStarted], "true"]
triAssertEq [format["%1", serverStarted], "true"]
triAssertEq [format["%1", gamePropertiesStarted], "true"]
triAssertEq [format["%1", MFCTI116Extensions], "true"]
triAssertEq [format["%1", westResources], "9000"]
triAssertEq [format["%1", eastResources], "9000"]
triAssertGe [count startingLocations, 48]
triAssertEq [isNull westStartingLocation, false]
triAssertEq [isNull eastStartingLocation, false]
triAssertGe [(getPos westStartingLocation) select 0, 2000]
triAssertGe [(getPos eastStartingLocation) select 0, 2000]
triAssertEq [format["%1", triMfctiClient1Done], "1"]
triAssertEq [format["%1", triMfctiClient2Done], "1"]
triAssertEq [format["%1", triMfctiClient2ObservedDamage], "1"]
triMfctiServerRelease = 1
publicVariable "triMfctiServerRelease"
triAssertEq [format["%1", triMfctiClient1Ack], "1"]
triAssertEq [format["%1", triMfctiClient2Ack], "1"]
triWait 1000
triEndTest
