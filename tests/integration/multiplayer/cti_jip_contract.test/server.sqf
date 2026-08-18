triAssertNgs 14
triAssertNgsClient 14
triAssertEq [format["%1", ctiEarlyReady], "1"]

ctiFactory = "Jeep" createVehicle [6520, 6480, 0]
ctiPatrol = "Jeep" createVehicle [6530, 6480, 0]
ctiObsolete = "Jeep" createVehicle [6540, 6480, 0]
ctiSnapshot = [1, [1000, 800], [["factory", ctiFactory], ["patrol", [ctiPatrol, 3]], ["obsolete", ctiObsolete]], [0, "running"]]
publicVariable "ctiSnapshot"

triAssertEq [format["%1", ctiEarlyRevisionOne], "1"]
deleteVehicle ctiObsolete
ctiFactory setDammage 0.35
ctiPatrol setPos [6560, 6490, 0]
ctiSnapshot = [2, [1250, 900], [["factory", ctiFactory], ["patrol", [ctiPatrol, 4]]], [["obsolete"], "running"]]
publicVariable "ctiSnapshot"
["before-late"] remoteExec ["triRecordRemoteExec", -2]

triAssertEq [format["%1", ctiLateReady], "1"]
ctiSnapshot = [3, [1400, 950], [["factory", ctiFactory], ["patrol", [ctiPatrol, 5]]], [["obsolete"], "running"]]
publicVariable "ctiSnapshot"
["after-late"] remoteExec ["triRecordRemoteExec", -2]
ctiServerFinal = 1
publicVariable "ctiServerFinal"

triAssertEq [format["%1", ctiEarlyDone], "1"]
triAssertEq [format["%1", ctiLateDone], "1"]
triWait 500
triEndTest
