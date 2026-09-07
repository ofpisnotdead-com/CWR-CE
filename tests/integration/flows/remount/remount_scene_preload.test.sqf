// The Scene owns the CfgScenePreload shapes - bullet-impact craters, blood,
// cloudlets, footsteps, sky clouds, light halos - and an in-process re-mount
// replaces the Scene. Empty slots on the new one draw nothing and raise no error.

triSetLanguage "English"
triSimUntil { triGameMode == 2 }
triSimUntil { triScenePreloadCount > 0 }
_before = triScenePreloadCount

_r = triRemount
triAssertEq [_r, "OK"]
triSimFrames 60
triSimUntil { triGameMode == 2 && triLoadedShapeCount > 0 }

triAssertEq [triScenePreloadCount, _before]
triEndTest
