triAssertNgsClient 14
triAssertMissionPlayable
triAssertEq [format["%1", ctiMissionInitRan], "true"]
triAssertEq [format["%1", ctiJipHookRan], "true"]
triAssertEq [format["%1", ctiJipRevisionObserved], "2"]
triAssertEq [format["%1", ctiSnapshot select 0], "2"]
triAssertEq [format["%1", (ctiSnapshot select 1) select 0], "1250"]
triAssertEq [format["%1", count (ctiSnapshot select 2)], "2"]
triAssertEq [format["%1", count ((ctiSnapshot select 3) select 0)], "1"]
_factory = ((ctiSnapshot select 2) select 0) select 1
_patrol = (((ctiSnapshot select 2) select 1) select 1) select 0
triAssertEq [isNull _factory, false]
triAssertEq [isNull _patrol, false]
triAssertNear [getDammage _factory, 0.35, 0.05]
triAssertNear [(getPos _patrol) select 0, 6560, 1]
triAssertNear [(getPos _patrol) select 1, 6490, 1]
triAssertEq [triRemoteExecLog, ""]
ctiLateReady = 1
publicVariable "ctiLateReady"

triAssertEq [format["%1", ctiSnapshot select 0], "3"]
triAssertEq [format["%1", (ctiSnapshot select 1) select 0], "1400"]
triAssertEq [triRemoteExecLog, "after-late"]
triAssertEq [format["%1", ctiServerFinal], "1"]
ctiLateDone = 1
publicVariable "ctiLateDone"
triWait 500
triEndTest
