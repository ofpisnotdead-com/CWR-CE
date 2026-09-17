// Map zoom follows keyboard and mouse-wheel bindings while preserving its defaults.
_keyK = 14
_keyNumpadPlus = 87
_wheelUp = 1048580
_wheelDown = 1048581
triSetLanguage "English"
triSimFrames 15

triOpenMap
triShowMap 1
triSimFrames 5
_s0 = triMapGetScale
triAssertGt [_s0, 0]

triMapWheel 1
_s3 = triMapGetScale
triAssertGt [_s3, _s0]

triMapSetScale _s0
triMapWheel -1
_s4 = triMapGetScale
triAssertLt [_s4, _s0]

triMapSetScale _s0

triBindAction ["MapZoomIn", _keyK]
triSimFrames 2

triKeyDown _keyK
triSimFrames 20
triKeyUp _keyK
_s1 = triMapGetScale
triAssertLt [_s1, _s0]

triMapSetScale _s0
triSimFrames 2
triKeyDown _keyNumpadPlus
triSimFrames 20
triKeyUp _keyNumpadPlus
_s2 = triMapGetScale
triAssertEq [_s2, _s0]

triBindAction ["MapZoomIn", _wheelUp]
triBindAction ["MapZoomOut", _wheelDown]
triMapSetScale _s0
triMapWheel 1
_s5 = triMapGetScale
triAssertLt [_s5, _s0]

triBindAction ["MapZoomIn", 0]
triMapSetScale _s0
triMapWheel 1
_s6 = triMapGetScale
triAssertEq [_s6, _s0]

triEndTest
