// Map zoom must follow rebinds: the map polls the bound actions, not hardcoded numpad
// keycodes. Rebind Map Zoom In to a new key; assert that key zooms and the old numpad
// key no longer does. (The mouse-wheel path in OnMouseZChanged is unchanged.)
// Scancodes: K 14, Numpad+ 87.
triSetLanguage "English"
triSimFrames 15

triOpenMap
triShowMap 1
triSimFrames 5
_s0 = triMapGetScale
triAssertGt [_s0, 0]

triBindAction ["MapZoomIn", 14]
triSimFrames 2

// The new key zooms the map in (scale decreases).
triKeyDown 14
triSimFrames 20
triKeyUp 14
_s1 = triMapGetScale
triAssertLt [_s1, _s0]

// The old numpad+ is no longer bound to Map Zoom In, so from the same baseline it
// does not zoom the map (broken state: it would zoom in and the scale would drop).
triMapSetScale _s0
triSimFrames 2
triKeyDown 87
triSimFrames 20
triKeyUp 87
_s2 = triMapGetScale
triAssertEq [_s2, _s0]

triEndTest
