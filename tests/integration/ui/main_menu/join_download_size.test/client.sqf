triSetLanguage "English"
triAssertEq [(triDisplay), 0]
triClick 105
triAssertEq [(triDisplay), 8]
triInvokeButton 103
triAssertEq [(triDisplay), 19]
triAssertEq [triControlText 101, "127.0.0.1"]
triInvokeButton 1
triAssertEq [(triDisplay), 8]
triAssertEq [triSelectListByData [102, "127.0.0.1"], true]
triInvokeButton 105
triAssertEq [(triDisplay), 75]
triAssertIncludes [(triControlText 136), "[dl] Fixture Mod   download "]
triBarrier requirements_open
triScreenshot "join_download_size"
triAssertIncludes [(triControlText 136), " B"]
triInvokeButton 2
triAssertEq [(triDisplay), 8]
triEndTest
