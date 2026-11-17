triSetLanguage "English"
triAssertEq [(triDisplay), 0]
triClick 105
triAssertEq [(triDisplay), 8]
triInvokeButton 104
triAssertEq [(triDisplay), 17]
triAssertEq [triSelectListByData [101, "Demo"], true]
triAssertEq [triSelectListByData [102, "missing_addons"], true]
triInvokeButton 1
triAssertEq [(triDisplay), -1]
triAssertEq [triControlText 1, "OK"]
triScreenshot "missing_addons"
triInvokeButton 1
triAssertEq [(triDisplay), 17]
triAssertEq [triSelectListByData [102, "missing_addons"], true]
triInvokeButton 1
triAssertEq [(triDisplay), -1]
triAssertEq [triControlText 1, "OK"]
triInvokeButton 1
triAssertEq [(triDisplay), 17]
triAssertEq [triSelectListByData [102, "available"], true]
triInvokeButton 1
triAssertEq [(triDisplay), 70]
triEndTest
