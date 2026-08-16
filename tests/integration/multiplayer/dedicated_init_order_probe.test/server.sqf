triAssertNgs 14
triAssertNgsClient 14
triAssertEq [format["%1", probeRootInitSeen], "1"]
triAssertEq [format["%1", probeObjectInitSeen], "1"]
triAssertEq [format["%1", probeServerInitSeen], "1"]
triAssertEq [probeServerInitDuplicate, false]
triAssertEq [format["%1", probeServerToken], "7303"]
triAssertEq [local probeServerLogic, true]
triAssertEq [format["%1", probeSimulationSeen], "1"]
triAssertEq [format["%1", probeClientDone], "1"]
probeServerDone = 1
publicVariable "probeServerDone"
triWait 500
triEndTest
