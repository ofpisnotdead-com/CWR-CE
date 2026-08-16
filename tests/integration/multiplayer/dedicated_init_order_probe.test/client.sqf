triAssertEq [(triDisplay), 70]
triAssertEq [triMpAssignSelfSlot "WEST:1", "OK"]
triAssertNgsClient 13
triAssertEq [(triDisplay), 53]

triAssertEq [probeRootInitSeen, 1]
triAssertEq [probeRootInitDuplicate, false]
triAssertEq [probeObjectInitAtScan, true]
triAssertEq [probeRoleLeaderAtScan, true]
triAssertEq [probePlayerIsRoleAtScan, true]
triAssertEq [probePlayerLocalAtScan, true]
triAssertEq [probePlayerGroupAtScan, true]
triAssertEq [probeClientScanSeen, 1]
triAssertEq [probeClientScanDuplicate, false]
triAssertEq [probePlayerInitSeen, 1]
triAssertEq [probePlayerInitDuplicate, false]
triAssertEq [probeServerTokenSeen, 7303]
triAssertEq [probePlayerInitComplete, 1]
triAssertEq [player == probeWest, true]
triAssertEq [group player == probeGroup, true]
triAssertEq [local player, true]
triAssertEq [local probeServerLogic, false]
triAssertEq [triPlayerAuthority, "OK"]
triAssertNear [(triPlayerPosX), 6600, 0.5]
triAssertNear [(triPlayerPosZ), 6400, 0.5]

triInvokeButton 1
triAssertNgsClient 14
triAssertMissionPlayable
triAssertEq [triPlayerAuthority, "OK"]
triAssertEq [format["%1", probeSimulationSeen], "1"]
triAssertIncludes [(triActionMenuText), "MP initialized"]
triAssertPlayerMoves 26 2500 0.25
triAssertEq [triFirePlayerWeapon, "OK"]
triAssertEq [triDamagePlayer 0.25, "OK"]
triAssertGe [triPlayerDammage, 0.20]
probeClientDone = 1
publicVariable "probeClientDone"
triAssertEq [format["%1", probeServerDone], "1"]
triEndTest
