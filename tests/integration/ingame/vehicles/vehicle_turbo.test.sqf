// The Turbo modifier (Shift + forward) must speed a vehicle up: Turbo promotes
// MoveForward to fast-forward, read per input context by the pilots. Two fresh
// Jeeps cover the same ground so Shift + W can be compared with W alone.
// Both runs accelerate for the same simulated time and stop short of the Jeep's
// top speed, where the two runs converge and terrain decides the reading.
// Scancodes: forward W 26, Left Shift 225.

triSetLanguage "English"
triSimFrames 15
if ((driver turboCar) != player) then { "FAIL:fixture player is not the driver" } else { "OK" }
turboCar engineOn true
triSimFrames 10

_startPos = getPos turboCar
_startDir = getDir turboCar

_startTime = time
triKeyDown 26
while {(time - _startTime) < 4} do { triSimFrames 3 }
_sW = speed turboCar
triKeyUp 26
triAssertGt [_sW, 1]

player action ["EJECT", turboCar]
triSimFrames 5
_turboCar = (typeOf turboCar) createVehicle _startPos
_turboCar setDir _startDir
player moveInDriver _turboCar
_turboCar engineOn true
deleteVehicle turboCar
triSimFrames 15
if ((driver _turboCar) != player) then { "FAIL:player did not enter the fresh fixture vehicle" } else { "OK" }

_turboTime = time
triKeyDown 26
triKeyDown 225
while {(time - _turboTime) < 4} do { triSimFrames 3 }
_sT = speed _turboCar
triKeyUp 225
triKeyUp 26
triAssertGt [_sT, (_sW + 1)]

triEndTest
