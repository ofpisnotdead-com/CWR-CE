triSetLanguage "English"
triClickText "CAMPAIGN GAME"
triAssertEq [(triDisplay), 43]
triWaitFrames 30

triInvokeButton 107
triWaitFrames 5
triAssertEq [(triControlText 105), "Trident Debrief Campaign"]
triAssertEq [(triAssertListText [101, "Start"]), "OK"]
triInvokeButton 1
triWaitFrames 60

triAssertEq [(triDisplay), 37]
triInvokeButton 1
triWaitFrames 30
triAssertEq [(triEndMission "end1"), "OK"]
triAssertEq [(triDisplay), 50]
triWaitFrames 30

triAssertIncludes [(triControlText 102), "Mission complete"]
triAssertIncludes [(triControlText 102), "The synthetic campaign result is visible."]
triAssertExcludes [(triControlText 102), "Hidden alternate result"]
triEndTest
