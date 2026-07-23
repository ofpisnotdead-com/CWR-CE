triAssertNgsClient 2
triTransferUiAssigned = 0
triAssertEq [if (triTransferUiAssigned == 0) then { triTransferUiAssigned = 1; triMpAssignSelfSlot "WEST:1"; "FAIL:assigned slot, waiting for transfer UI" } else { triTransferUiTexts = triMpSetupMessage; triTransferUiStats = triMpTransferStats; triTransferUiIncludes = triAssertIncludes [triTransferUiTexts, "Receiving mission file"]; if (triTransferUiIncludes != "OK") then { triTransferUiIncludes } else { if ((triTransferUiStats select 1) <= 0) then { format["FAIL:transfer total is zero stats=%1 text=%2", triTransferUiStats, triTransferUiTexts] } else { triAssertExcludes [triTransferUiTexts, "0 KB / 0 KB"] } } }, "OK"]
triAssertNgsClient 13
triAssertEq [(triMpClientReady 14), "OK"]
triAssertNgsClient 14
triAssertMissionPlayable
clientDone = 1
publicVariable "clientDone"
triEndTest
