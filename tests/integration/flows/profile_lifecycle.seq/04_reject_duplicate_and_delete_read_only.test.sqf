triSetLanguage "English"

private _renamed = "Málaga_日本_Ω";
triAssertEq [triPlayerName, _renamed]

triClick 109
triAssertEq [(triDisplay), 31]
triAssertEq [(triListSize 101), 1]
triClick 102
triAssertEq [(triDisplay), 42]
triCursorMoveControl 101
triMouseLeft 1
triSimFrames 2
triMouseLeft 0
triSimFrames 2
for "_i" from 0 to 23 do { triSendKey 42; triSimFrames 1 }
triSimFrames 30
triTypeText _renamed
triClick 1
triSimFrames 5
triClick 1
triAssertEq [(triDisplay), 42]
triClick 2
triSimFrames 30

triAssertEq [(triDisplay), 0]
triClick 109
triAssertEq [(triDisplay), 31]
triAssertEq [(triListSize 101), 1]
triAssertEq [(triListText [101, 0]), _renamed]
triAssertEq [(triMakeProfileReadOnly _renamed), "OK"]
triClick 103
triClick 1
triAssertEq [(triAssertProfileMissing _renamed), "OK"]
triAssertEq [triPlayerName, ""]
triAssertEq [(triListSize 101), 0]

triClick 2
triAssertEq [(triDisplay), 0]
triClick 109
triAssertEq [(triDisplay), 31]
triAssertEq [(triListSize 101), 0]
triEndTest
