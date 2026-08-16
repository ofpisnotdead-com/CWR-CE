// The Turbo modifier (Shift + forward) must speed a vehicle up: Turbo promotes
// MoveForward to fast-forward, read per input context by the pilots. The player
// drives a Jeep; after building speed on W alone, holding Left Shift must raise it.
// Scancodes: forward W 26, Left Shift 225.

triSetLanguage "English"
triSimFrames 15
if ((driver turboCar) != player) then { "FAIL:fixture player is not the driver" } else { "OK" }
turboCar engineOn true
triSimFrames 10

triKeyDown 26
triSimFrames 90
_sW = speed turboCar
triAssertGt [_sW, 1]

triKeyDown 225
triSimFrames 90
_sT = speed turboCar
triKeyUp 225
triKeyUp 26
triAssertGt [_sT, (_sW + 1)]

triEndTest
