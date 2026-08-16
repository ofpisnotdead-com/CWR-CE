triAssertNgsClient 14
triAssertMissionPlayable
triAssertEq [missionName, "rot_one"]
triScreenshot "client1_rot_one"
triAssertEq [(triDisplay), 50]
triScreenshot "client1_debriefing"
triInvokeButton 104
closeDialog 0
if (triDisplay == 70) then { triMpAssignSelfSlot "WEST:1" }
if (triDisplay == 70) then { triMpClientReady 14 }
triAssertNgsClient 14
if (triDisplay == 204) then { triInvokeButton 1 }
triWait 2000
triAssertMissionPlayable
triAssertEq [missionName, "rot_two"]
triScreenshot "client1_rotated_to_two"
triEndTest
