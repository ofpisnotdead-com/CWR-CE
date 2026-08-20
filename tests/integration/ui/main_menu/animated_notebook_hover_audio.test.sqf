triSetLanguage "English"
triAssertEq [(triDisplay), 0]

triCursorMoveControl 102
triSimFrames 90
_baseline = triAudioActive2D

triClick 102
triAssertEq [(triDisplay), 9099]

_clickFinished = false
_frames = 0
while {!_clickFinished && _frames < 30} do {
    triSimFrames 1;
    _clickFinished = triAudioActive2D == _baseline;
    _frames = _frames + 1;
}
triAssertEq [_clickFinished, true]

_peak = _baseline
for "_i" from 0 to 49 do {
    triSimFrames 1;
    if (triAudioActive2D > _peak) then {_peak = triAudioActive2D};
}
triAssertEq [_peak, _baseline]

triEndTest
