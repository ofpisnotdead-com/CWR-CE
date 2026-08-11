triSetLanguage "English"

private _ascii = "Alpha 123-Z";
private _unicode = "Český_玩家_Игрок";
triAssertEq [triPlayerName, _unicode]

triClick 109
triAssertEq [(triDisplay), 31]
triAssertEq [(triListSize 101), 2]
triAssertEq [(triListText [101, 0]), _ascii]
triAssertEq [(triListText [101, 1]), _unicode]
triAssertEq [triSelectList [101, 0], true]
triClick 1
triSimFrames 30
triAssertEq [triPlayerName, _ascii]
triEndTest
