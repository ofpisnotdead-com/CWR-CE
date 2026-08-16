triAssertEq [(triDisplay), 0]
triClick 105
triAssertEq [(triDisplay), 8]
triInvokeButton 104
triAssertEq [(triDisplay), 17]
triAssertEq [triSelectListByData [101, "demo"], true]
triWait 1000
triAssertEq [triSelectList [102, 2], true]
triAssertEq [(triListSel 102), 2]
triScreenshot "host_selected_kingcity"
triInvokeButton 1
triAssertEq [(triDisplay), 70]
triScreenshot "host_setup"
triAssertEq [triMpAssignSelfSlot "WEST:1", "OK"]
triScreenshot "host_assigned_west1"
triWait 20000
triInvokeButton 1
triAssertNgsClient 12
triAssertNgsClient 13
triAssertEq [triMpClientReady 14, "OK"]
triAssertNgsClient 14
triAssertEq [(triDisplay), 52]
triInvokeButton 1
if (triDisplay == 204) then { triInvokeButton 1 }
triWait 3000
triAssertMissionPlayable
triScreenshot "host_playable"
triWait 10000
triEndTest
