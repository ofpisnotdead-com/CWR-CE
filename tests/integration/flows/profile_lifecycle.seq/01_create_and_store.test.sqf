triSetLanguage "English"

private _initial = triPlayerName;
private _ascii = "Alpha 123-Z";
private _unicode = "Český_玩家_Игрок";
triAssert [_initial]

triClick 109
triAssertEq [(triDisplay), 31]
triAssertEq [(triListSize 101), 1]
triAssertEq [(triListText [101, 0]), _initial]

triClick 104
triAssertEq [(triDisplay), 42]
triCursorMoveControl 101
triMouseLeft 1
triSimFrames 2
triMouseLeft 0
triSimFrames 2
for "_i" from 0 to 23 do { triSendKey 42; triSimFrames 1 }
triSendText [101, _ascii]
triAssertEq [(triControlText 101), _ascii]
triClick 1
triSimFrames 30
triAssertEq [triPlayerName, _ascii]
triAssertEq [(triAssertProfileMissing _initial), "OK"]

triClick 109
triAssertEq [(triDisplay), 31]
triClick 102
triAssertEq [(triDisplay), 42]
triCursorMoveControl 101
triMouseLeft 1
triSimFrames 2
triMouseLeft 0
triSimFrames 2
for "_i" from 0 to 23 do { triSendKey 42; triSimFrames 1 }
triSendText [101, _unicode]
triAssertEq [(triControlText 101), _unicode]
triClick 1
triSimFrames 30
triAssertEq [triPlayerName, _unicode]

triClick 109
triAssertEq [(triDisplay), 31]
triAssertEq [(triListSize 101), 2]
triAssertEq [(triListText [101, 0]), _ascii]
triAssertEq [(triListText [101, 1]), _unicode]
triEndTest
