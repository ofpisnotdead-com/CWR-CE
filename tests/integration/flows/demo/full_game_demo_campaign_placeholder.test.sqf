// Full PoseidonGame can use RemasterDemo as its data directory. The full game
// registers the campaign module, so opening Campaign Game must find a valid
// placeholder campaign instead of crashing on an absent campaign tree.

triAssertEq [(triDisplay), 0]
triAssert [(triGetControlEnabled 101)]
triClick 101
triAssertEq [(triDisplay), 43]
triAssertIncludes [(triVisibleTexts), "Demo Campaign"]
triSendKey 41
triSimUntil { triDisplay == 0 }
triAssertEq [(triDisplay), 0]
triEndTest
