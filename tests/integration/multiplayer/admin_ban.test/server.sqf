triAssertEq [(triServerBanCount), 0]
triAssertEq [(triServerBanCount), 1]
banId = (triServerBanFirstId)
unbanReady = 1
publicVariable "banId"
publicVariable "unbanReady"
triAssertEq [(triServerBanCount), 0]
triEndTest
