triAssertNgsClient 14
triAssertMissionPlayable
triAssertIncludes [(triPlayerVehicle), "@"]
triAssertEq [triPlayerAuthority, "OK"]
triBarrier vehicle_jip_active
triAssertEq [(triMpPlayerNames), "anchor|driver"]
triScreenshot "late_driver_owns_person_and_jeep"
triEndTest
