triAssertNgsClient 14
triAssertMissionPlayable
triAssertEq [triPlayerAuthority, "OK"]
triBarrier vehicle_jip_active
triAssertEq [(triMpPlayerNames), "anchor|driver"]
triEndTest
