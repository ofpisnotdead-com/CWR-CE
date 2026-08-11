triSetLanguage "English"

private _renamed = "Málaga_日本_Ω";
private _profile = triPlayerName;
triAssert [_profile]
triAssertEq [(triAssertProfileMissing _renamed), "OK"]

triClick 109
triAssertEq [(triDisplay), 31]
triAssertEq [(triListSize 101), 1]
triAssertEq [(triListText [101, 0]), _profile]
triSetActiveProfile "GhostProfile"
triEndTest
