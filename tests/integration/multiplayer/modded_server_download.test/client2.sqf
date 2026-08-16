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

triClick 1
triAssertEq [(triDisplay), 74]
triClick 125
triAssertIncludes [(triVisibleTexts), "Complete"]
triAssertIncludes [(triVisibleTexts), "Continue"]
triEndTest
