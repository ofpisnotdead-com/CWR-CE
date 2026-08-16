triSetLanguage "English"
triAssertEq [(triDisplay), 0]

triClick 115
triAssertEq [(triDisplay), 51]
triClick 1
triAssertEq [(triDisplay), 26]

triDblClick 51
triAssertEq [(triDisplay), 27]
triClick 1
triAssertEq [(triDisplay), 26]

triClick 107
triAssertEq [(triDisplay), 46]

private _savedX = (getPos player) select 0

triSendKey 41
triAssertEq [(triDisplay), 49]
triAssert [(triGetControlVisible 103)]
triClick 103
triAssertEq [(triDisplay), 46]

player setPos [(_savedX + 100), ((getPos player) select 1), 0]
triAssertGe [abs (((getPos player) select 0) - _savedX), 90]

triSendKey 41
triAssertEq [(triDisplay), 49]
triAssert [(triGetControlVisible 102)]
triClick 102
triAssertEq [(triDisplay), 46]
triAssertNear [((getPos player) select 0), _savedX, 1]

triEndTest
