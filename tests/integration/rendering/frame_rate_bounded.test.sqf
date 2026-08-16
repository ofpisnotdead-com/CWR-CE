// A default install carries an FPS cap, and with vsync off that cap is the only
// thing pacing the loop. Asserted against the cap this machine stamped, so the
// test holds on any display.

triResetGLErrorBaseline

private _cap = triGetFpsCap;
triAssertGt [_cap, 0]

triSetVsync 0

// triWaitFrames is a render-only pump that never reaches the cap in RenderFrame.
triSimFrames 200

private _peak = 0;
for "_i" from 0 to 4 do {
    triSimFrames 40;
    private _sample = triFps;
    if (_sample > _peak) then { _peak = _sample };
};

// Whole-millisecond sleeps let a cap of N reach 1000/floor(1000/N), ~16% at 144.
triAssertLt [_peak, _cap * 1.5]

triAssertGt [(triGetBackBufferNonBlackCount), 0]
triAssertEq [(triGetGLErrorCount), 0]

triEndTest
