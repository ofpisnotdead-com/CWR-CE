triAssertEq [(triDisplay), 0]

triClick 105
triAssertEq [(triDisplay), 8]
triInvokeButton 122
triAssertEq [(triControlText 122), "Address: Internet"]
triInvokeButton 123
triAssertEq [triSelectList [102, 0], true]

triInvokeButton 105
triAssertEq [(triDisplay), 75]
triAssertIncludes [(triVisibleTexts), "Fixture Mod"]
triAssertIncludes [(triVisibleTexts), "download"]
triSendText [137, "tri-secret"]
triScreenshot "client1_join_requirements"

triClick 1
triAssertEq [(triDisplay), 74]
triClick 125
triAssertIncludes [(triVisibleTexts), "Complete"]
triAssertIncludes [(triVisibleTexts), "Continue"]
triScreenshot "client1_download_complete"

triClick 125
triAssertNgsClient 14
triAssertMissionPlayable
triAssertIncludes [(triActiveMods), "fixturemod"]
triAssertNetworkAssetExistsForRole ["playerFace", 0, "face.jpg"]
triAssertNetworkAssetExistsForRole ["sound", 0, "tri_mp_ping.wav"]
triScreenshot "client1_full_stack_playable"
triEndTest
