triAssertEq [(triDisplay), 0]
triClick 105
triAssertEq [(triDisplay), 8]
triWait 8000
triAssertEq [triSelectList [102, 0], true]
triInvokeButton 105
triAssertEq [(triDisplay), 70]
triAssertEq [triMpAssignSelfSlot "EAST:1", "OK"]
triScreenshot "joiner_assigned_east1"
triAssertNgsClient 12
triAssertNgsClient 13
triAssertEq [triMpClientReady 14, "OK"]
triScreenshot "joiner_after_ready"
triLogPlayState joiner_after_ready
triAssertNgsClient 14
triLogPlayState joiner_before_playable_assert
triWait 3000
triAssertMissionPlayable
triScreenshot "joiner_playable"
triEndTest
