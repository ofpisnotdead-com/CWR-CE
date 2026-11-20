triSimUntil { triReturnMissionTicks > 0 }
triAssertEq [(typeOf player), "SoldierWB"]
triAssertEq [(getWorld), "demo"]
triAssertEq [(triStartRandomCutscene), "OK"]
triAssertEq [(triGameMode), 2]
triAssertEq [(getWorld), "demo"]
triAssertEq [(isNull player), true]
triReturnMissionTicks = 0
triSimFrames 60
triAssertEq [(triGameMode), 2]
triAssertEq [(isNull player), true]
triAssertEq [(triReturnMissionTicks), 0]
triEndTest
