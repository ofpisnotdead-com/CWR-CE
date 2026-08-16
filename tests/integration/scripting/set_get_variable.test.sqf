// test enableAi

triSimUntil { time >= 1 }

// todo: wish to have isNil command
_nil = "scalar bool array string 0xfcffffef";

// verify empty key
_state0 = player getVariable "key1";
_isNil0 = format ["%1", _state0] == _nil;
if (!_isNil0) exitWith { "FAIL: empty key of custom state 1 should return nothing" };

// set value1 to true
player setVariable ["key1", true];

// verify value1 is true
_state1 = player getVariable "key1";
if (format ["%1", _state1] != "true") exitWith { "FAIL: value1 should be true" };

// set value1 to 2
player setVariable ["key1", 2];

// verify value1 is 2
_state1 = player getVariable "key1";
if (format ["%1", _state1] != "2") exitWith { "FAIL: value1 should be 2" };


triEndTest
