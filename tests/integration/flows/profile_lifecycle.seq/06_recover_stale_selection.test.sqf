triSetLanguage "English"

private _profile = triPlayerName;
triAssert [_profile]
triAssertEq [(triAssertProfileMissing "GhostProfile"), "OK"]

triClick 109
triAssertEq [(triDisplay), 31]
triAssertEq [(triListSize 101), 1]
triAssertEq [(triListText [101, 0]), _profile]
triEndTest
