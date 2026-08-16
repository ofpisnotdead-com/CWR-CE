// A dedicated server has no in-game UI. Updating an objective must still
// persist its state without trying to display the cadet-mode hint.
"trident" objStatus "DONE"
triAssertEq [OBJ_trident, 1]
triEndTest
