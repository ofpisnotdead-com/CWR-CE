// Campaign list must show a mission title whose token lives only in the campaign stringtable.
triSetLanguage "English"
triWaitFrames 10
triAssertEq [(triCheatUnlockCampaign), "OK"]
triWaitFrames 10
triClickText "CAMPAIGN GAME"
triAssertEq [(triDisplay), 43]
triWaitFrames 30
for "_i" from 1 to 4 do { if ((triControlText 105) != "Trident Campaign Table") then { triInvokeButton 107; triWaitFrames 10 } }
triAssertEq [(triControlText 105), "Trident Campaign Table"]
triAssertEq [(triAssertListText [101, "Campaign Table Title"]), "OK"]
triEndTest
