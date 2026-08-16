triAssertEq [(triDisplay), 0]
triWait 8000
triClick 105
triAssertEq [(triDisplay), 8]
triAssertEq [triSelectList [102, 0], true]
triInvokeButton 105
triAssertEq [(triDisplay), 70]
triAssertEq [triMpAssignSelfSlot "WEST:3", "OK"]
triScreenshot "joiner2_assigned"
triBarrier slots_taken
triAssertNgsClient 12
triAssertNgsClient 14
triScreenshot "joiner2_after_ngsplay"
triInvokeButton 1
if (triDisplay == 204) then { triInvokeButton 1 }
triWait 3000
triLogPlayState joiner2_before_playable_assert
triAssertMissionPlayable
triScreenshot "joiner2_playable"
triBarrier playable
triEndTest
