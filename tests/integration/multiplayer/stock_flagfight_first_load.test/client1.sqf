triAssertEq [(triDisplay), 70]
triAssertEq [triMpAssignSelfSlot "WEST:1", "OK"]
triAssertNgsClient 13
triAssertEq [(triDisplay), 53]
triInvokeButton 1
triAssertNgsClient 14
triWait 3000
triAssertMissionPlayable
triAssertEq [triPlayerAuthority, "OK"]
triAssertGe [triPlayerCurrentMagazineAmmo, 0]
triAssertEq [triFirePlayerWeapon, "OK"]
triAssertEq [triDoDammageToPlayer 0.25, "OK"]
triAssertEq [triDamagePlayer 0.25, "OK"]
triAssertGe [triPlayerDammage, 0.01]
c1ready = 1
publicVariable "c1ready"
triScreenshot "client1_stock_flagfight_playable"
triAssertEq [format["%1", c2done], "1"]
triEndTest
