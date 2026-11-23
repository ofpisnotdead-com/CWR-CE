// test enableAi and checkAIFeature

triSimUntil { time >= 1 }

// create exist marker will fail
_existedMarker = "conflictName";
createMarker [_existedMarker, [0, 0, 0]]; // checked in the end

// verify first marker
TriAssertEq [getMarkerColor _existedMarker, "ColorRed"]

// verify second marker
deleteMarker _existedMarker;
TriAssertEq [getMarkerColor _existedMarker, "ColorBlue"]

// verify third marker
deleteMarker _existedMarker;
TriAssertEq [getMarkerColor _existedMarker, "ColorGreen"]

// check no marker left
deleteMarker _existedMarker;
TriAssertEq [getMarkerColor _existedMarker, ""]


// check delete dubpicate markers
// existing markers defined by mission.sqm: a, b, a, c, a, d

// verify b/c/d
TriAssertEq [(getMarkerPos "b") select 0, 1];
TriAssertEq [(getMarkerPos "c") select 0, 2];
TriAssertEq [(getMarkerPos "d") select 0, 3];

// remove first "a" marker
deleteMarker "a";
// markers now are b, a, c, a, d. Index of a is now 1 and 3. Index of b is 0, c is 2, d is 4

// verify second "a"
TriAssertEq [(getMarkerPos "a") select 0, 100];

// verify b/c/d
TriAssertEq [(getMarkerPos "b") select 0, 1];
TriAssertEq [(getMarkerPos "c") select 0, 2];
TriAssertEq [(getMarkerPos "d") select 0, 3];

// remove second "a" marker
deleteMarker "a";
// markers now are b, c, a, d. Index of a is now 2. Index of b is 0, c is 1, d is 3

// verify third "a"
TriAssertEq [(getMarkerPos "a") select 0, 200];

// verify b/c/d
TriAssertEq [(getMarkerPos "b") select 0, 1];
TriAssertEq [(getMarkerPos "c") select 0, 2];
TriAssertEq [(getMarkerPos "d") select 0, 3];

// remove third "a" marker
deleteMarker "a";
// markers now are b, c, d. Index of b is 0, c is 1, d is 2

// verify b/c/d
TriAssertEq [(getMarkerPos "b") select 0, 1];
TriAssertEq [(getMarkerPos "c") select 0, 2];
TriAssertEq [(getMarkerPos "d") select 0, 3];

triEndTest
