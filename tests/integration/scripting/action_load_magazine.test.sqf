// test for "LOADMAGAZINE" action

triSimUntil { time >= 1 }

// move unit into tank. "LOADMAGAZINE" action requires a gunner to be inside the vehicle
s1 moveInGunner m1;
triSimFrames 40

// initlal "sabot" magazine verification
if ("heat120" != (m1 ammoArray "gun120") select 0) exitWith { "FAIL:abnormal initial magazine" };

s1 action ["LOADMAGAZINE", m1, objNull, 0, 0, "gun120", "shell120"]

// verify whether "heat" magazine is reloaded
triSimFrames 40
if ("shell120" != (m1 ammoArray "gun120") select 0) exitWith { "FAIL:'LOADMAGAZINE' action failed to work" };
triEndTest
