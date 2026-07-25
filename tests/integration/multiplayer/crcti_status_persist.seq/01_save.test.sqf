// Phase 1 of a two-boot sequence sharing one user dir. In a non-campaign
// mission (CurrentCampaign is empty, exactly as MP sets it) TZK and crCTI persist
// loadouts by camCreating a throwaway SoldierWB, encoding data as magazines, and
// calling saveStatus. Here the unit carries two M16 magazines saved under a named
// entry; the next boot must read them back from a fresh game.

triSetLanguage "English"

tzkUnit = "SoldierWB" camCreate [6710.527832, 82.907585, 5408.891602]
removeAllWeapons tzkUnit
tzkUnit addWeapon "M16"
tzkUnit addMagazine "M16"
tzkUnit addMagazine "M16"
triAssertEq [(count magazines tzkUnit), 2]

tzkSaved = tzkUnit saveStatus "crcti_loadout_regress"
triAssertEq [(if (tzkSaved) then {1} else {0}), 1]

deleteVehicle tzkUnit
triEndTest
