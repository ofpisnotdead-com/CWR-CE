// A "fired" handler can find the round it was told about: nearestObject with the
// handler's ammo type resolves the shell at the muzzle while the handler runs.

triSimFrames 30

player removeAllEventHandlers "fired"

firedAmmo = ""
firedType = ""
firedNull = ""
firedDist = 1e10

player addEventHandler ["fired", {firedAmmo = _this select 4; _shot = nearestObject [_this select 0, _this select 4]; firedNull = format["%1", isNull _shot]; firedType = format["%1", typeOf _shot]; firedDist = (_this select 0) distance _shot}]

triAssertEq [triFirePlayerWeapon, "OK"]
triSimFrames 2

triAssertNe [firedAmmo, ""]
triAssertEq [firedNull, "false"]
triAssertEq [firedType, firedAmmo]
triAssertLt [firedDist, 10]

triEndTest
