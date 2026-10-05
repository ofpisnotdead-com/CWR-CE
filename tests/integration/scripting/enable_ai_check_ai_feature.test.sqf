// test enableAi and checkAIFeature

triSimUntil { time >= 1 }

// check illegal feature will always return false
if (s1 checkAIFeature "ILLEGAL") exitWith { "FAIL:'checkAIFeature ILLEGAL' failed to return false for illegal feature" };


// check default "move" feature is enabled
if (not (s1 checkAIFeature "MOVE")) exitWith { "FAIL:'checkAIFeature MOVE' failed to return true for default enabled feature" };

// disableAI "MOVE"
s1 disableAI "MOVE";
if (s1 checkAIFeature "MOVE") exitWith { "FAIL:'checkAIFeature MOVE' failed to return false after having been disabled" };

// enableAI "MOVE"
s1 enableAI "MOVE";
if (not (s1 checkAIFeature "MOVE")) exitWith { "FAIL:'checkAIFeature MOVE' failed to return true after having been enabled" };

triEndTest
