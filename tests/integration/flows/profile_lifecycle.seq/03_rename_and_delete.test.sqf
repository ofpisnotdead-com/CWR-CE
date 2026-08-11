triSetLanguage "English"

private _ascii = "Alpha 123-Z";
private _unicode = "Český_玩家_Игрок";
private _renamed = "Málaga_日本_Ω";
triAssertEq [triPlayerName, _ascii]

triClick 109
triAssertEq [(triDisplay), 31]
triClick 104
triAssertEq [(triDisplay), 42]
triCursorMoveControl 101
triMouseLeft 1
triSimFrames 2
triMouseLeft 0
triSimFrames 2
for "_i" from 0 to 23 do { triSendKey 42; triSimFrames 1 }
triSendText [101, _renamed]
triAssertEq [(triControlText 101), _renamed]
triClick 1
triSimFrames 30
triAssertEq [triPlayerName, _renamed]
triAssertEq [(triAssertProfileMissing _ascii), "OK"]

triClick 109
triAssertEq [(triDisplay), 31]
triAssertEq [(triListSize 101), 2]
triAssertEq [(triListText [101, 0]), _renamed]
triAssertEq [(triListText [101, 1]), _unicode]
triAssertEq [triSelectList [101, 1], true]
triClick 103
triClick 1
triAssertEq [(triAssertProfileMissing _unicode), "OK"]
triAssertEq [triPlayerName, _renamed]
triAssertEq [(triListSize 101), 1]
triAssertEq [(triListText [101, 0]), _renamed]

triClick 2
triAssertEq [(triDisplay), 0]
triClick 109
triAssertEq [(triDisplay), 31]
triAssertEq [(triListSize 101), 1]
triAssertEq [(triListText [101, 0]), _renamed]
triEndTest
