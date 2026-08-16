triAssertEq [(triServerBanCount), 0]
triAssertEq [(triServerBanCount), 1]
unbanCommand = format["#unban %1", (triServerBanFirstId)]
publicVariable "unbanCommand"
triAssertEq [(triServerBanCount), 0]
unbanDone = 1
publicVariable "unbanDone"
triAssertEq [format["%1", unbanAck], "1"]
triEndTest
