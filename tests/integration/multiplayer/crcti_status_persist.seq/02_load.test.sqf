// Phase 2 boots against the same user dir phase 1 left behind (the .seq runner
// shares POSEIDON_USER_DIR across phases, each a fresh game launch). A throwaway
// SoldierWB with its weapons stripped must recover the two M16 magazines phase 1
// saved, proving object statuses persist across separate game sessions.

triSetLanguage "English"

tzkUnit = "SoldierWB" camCreate [6710.527832, 82.907585, 5408.891602]
removeAllWeapons tzkUnit

tzkLoaded = tzkUnit loadStatus "crcti_loadout_regress"
triAssertEq [(if (tzkLoaded) then {1} else {0}), 1]
triAssertEq [(count magazines tzkUnit), 2]

deleteVehicle tzkUnit
triEndTest
