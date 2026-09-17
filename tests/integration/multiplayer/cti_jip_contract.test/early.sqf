triAssertNgsClient 14
triAssertMissionPlayable
triAssertEq [triClearRemoteExecLog, "OK"]
triAssertEq [format["%1", ctiMissionInitRan], "true"]
ctiEarlyReady = 1
publicVariable "ctiEarlyReady"

triAssertEq [format["%1", ctiSnapshot select 0], "1"]
_obsolete = ((ctiSnapshot select 2) select 2) select 1
triAssertEq [isNull _obsolete, false]
ctiEarlyRevisionOne = 1
publicVariable "ctiEarlyRevisionOne"

triAssertEq [format["%1", ctiSnapshot select 0], "2"]
triAssertEq [format["%1", count (ctiSnapshot select 2)], "2"]
triAssertEq [isNull _obsolete, true]
_factory = ((ctiSnapshot select 2) select 0) select 1
_patrol = (((ctiSnapshot select 2) select 1) select 1) select 0
triAssertEq [isNull _factory, false]
triAssertEq [isNull _patrol, false]
triAssertNear [getDammage _factory, 0.35, 0.05]
triAssertNear [(getPos _patrol) select 0, 6560, 1]
triAssertNear [(getPos _patrol) select 1, 6490, 1]
triAssertEq [triRemoteExecLog, "before-late"]

triAssertEq [format["%1", ctiSnapshot select 0], "3"]
triAssertEq [format["%1", (ctiSnapshot select 1) select 0], "1400"]
triAssertEq [triRemoteExecLog, "before-late|after-late"]
triAssertEq [format["%1", ctiServerFinal], "1"]
ctiEarlyDone = 1
publicVariable "ctiEarlyDone"
triWait 500
triEndTest
