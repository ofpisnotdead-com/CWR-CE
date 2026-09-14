// Street lamps created by script used to crash: the lamp constructor read the
// "light" memory point from the shape passed in, which createVehicle leaves
// empty (the entity base falls back to the type's shape), and the abstract
// StreetLamp class has no model at all, so its hit-point setup dereferenced a
// null shape. The concrete classes must come back as real objects; the abstract
// one is refused and returns objNull.
triSimUntil { time >= 2 }
_p = getpos player
cwrLampWood = "StreetLampWood" createVehicle [(_p select 0) + 5, (_p select 1) + 5, 0]
cwrLampMetal = "StreetLampMetal" createVehicle [(_p select 0) - 5, (_p select 1) + 5, 0]
cwrLampAbstract = "StreetLamp" createVehicle [(_p select 0), (_p select 1) + 8, 0]
triWaitFrames 8
if (isNull cwrLampWood) exitWith { "FAIL:StreetLampWood createVehicle returned objNull" }
if (isNull cwrLampMetal) exitWith { "FAIL:StreetLampMetal createVehicle returned objNull" }
if (!(isNull cwrLampAbstract)) exitWith { "FAIL:abstract StreetLamp should be refused" }
triEndTest
