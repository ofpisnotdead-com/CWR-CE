// The texture bank derives its detail, specular, grass and water-bump set from
// `CfgDetailTextures`, and an in-process re-mount swaps that config under it. A set
// carried over from the previous load points at the previous config's file names.

triSetLanguage "English"
triSimUntil { triGameMode == 2 }
triSimUntil { triDetailTextureLoads > 0 }
_before = triDetailTextureLoads

_r = triRemount
triAssertEq [_r, "OK"]
triSimFrames 60
triSimUntil { triGameMode == 2 && triLoadedShapeCount > 0 }

triAssertEq [triDetailTextureLoads, _before + 1]
triEndTest
