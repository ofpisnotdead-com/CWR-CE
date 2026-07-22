triAssertEq [(triDisplay), 0]
triClick 105
triAssertEq [(triDisplay), 8]
triInvokeButton 122
triAssertEq [(triControlText 122), "Address: Internet"]
triInvokeButton 123
triAssertEq [triSelectList [102, 0], true]
triWait 2000
if (triSessionPing 0 <= 0) then {
    triInvokeButton 123
    triSelectList [102, 0]
}
triWait 2000
triAssertEq [triSelectList [102, 0], true]
triAssertGt [(triSessionPing 0), 0]
triInvokeButton 105
triAssertNgsClient 14
