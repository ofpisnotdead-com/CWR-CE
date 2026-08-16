// Which item the hand proxies draw: the Binocular wins over another class
// sharing the binocular slot in either pickup order, whether that class comes
// from the base config (NVGoggles) or from a mod (@handitem), and a lone slot
// item resolves on its own.

triSimFrames 60

removeAllWeapons player
player addWeapon "Binocular"
triSimFrames 10
triAssertEq [triGetHandItemClass, "Binocular"]

removeAllWeapons player
player addWeapon "NVGoggles"
player addWeapon "Binocular"
triSimFrames 10
triAssertEq [triGetHandItemClass, "Binocular"]

removeAllWeapons player
player addWeapon "Binocular"
player addWeapon "NVGoggles"
triSimFrames 10
triAssertEq [triGetHandItemClass, "Binocular"]

removeAllWeapons player
player addWeapon "NVGoggles"
triSimFrames 10
triAssertEq [triGetHandItemClass, "NVGoggles"]

removeAllWeapons player
player addWeapon "SyntheticHandItem"
triSimFrames 10
triAssertEq [triGetHandItemClass, "SyntheticHandItem"]

removeAllWeapons player
player addWeapon "SyntheticHandItem"
player addWeapon "Binocular"
triSimFrames 10
triAssertEq [triGetHandItemClass, "Binocular"]

removeAllWeapons player
triSimFrames 10
triAssertEq [triGetHandItemClass, ""]

triEndTest
