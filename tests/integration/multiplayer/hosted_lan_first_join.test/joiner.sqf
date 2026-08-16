triAssertEq [(triDisplay), 0]
triClick 105
triAssertEq [(triDisplay), 8]
triWait 8000
triAssertEq [triSelectList [102, 0], true]
triInvokeButton 105
triAssertEq [(triDisplay), 70]
triAssertEq [triMpAssignSelfSlot "WEST:2", "OK"]
triScreenshot "joiner_assigned"
triBarrier slots_taken
triAssertNgsClient 12
triAssertNgsClient 14
triScreenshot "joiner_after_ngsplay"
triLogPlayState joiner_after_ngsplay
triInvokeButton 1
if (triDisplay == 204) then { triInvokeButton 1 }
triWait 5000
triLogPlayState joiner_before_playable_assert
triAssertMissionPlayable
triScreenshot "joiner_playable"
triBarrier playable
triEndTest
