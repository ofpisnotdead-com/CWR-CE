// Auto-healing separates the wounded unit into a temporary subgroup whose
// command rejoins the main subgroup as soon as healing completes.
wounded = (units group player) select 1
_position = getPos wounded
ambulance = "SoldierWMedic" createVehicle _position
wounded setDammage 0.5

triAssertEq [triIssueAutoHeal [wounded, ambulance], "OK"]
triSimFrames 120
wounded setDammage 0
triSimFrames 60
triAssertLt [getDammage wounded, 0.01]
triEndTest
