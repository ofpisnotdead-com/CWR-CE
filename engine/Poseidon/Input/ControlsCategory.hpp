#pragma once

// User-facing categorization of UserAction values for the Controls UI.
//
// 5 categories are shown to the user under Controls > Keyboard & Mouse:
//   On foot · Vehicles · Pilot · Gunner · Common
//
// Each UserAction can appear in multiple categories — the table is a
// display projection over the flat v1 binding storage, not a per-context
// split.
// "Action inventory".
//
// Hidden in v1: AxisTurn / AxisDive / AxisRudder / AxisThrust (Gamepad
// page disabled), Cheat1 / Cheat2 (always hidden in this UI regardless
// of build).  Their entries simply don't appear in any category list.

#include <Poseidon/Input/UserAction.hpp>

namespace Poseidon
{

enum ControlsCategory : int
{
    ControlsCategoryOnFoot   = 0,
    ControlsCategoryVehicles = 1,
    ControlsCategoryPilot    = 2,
    ControlsCategoryGunner   = 3,
    ControlsCategoryCommon   = 4,
    ControlsCategoryCount    = 5,
};

// Returns a pointer to a static array of UserAction values for the
// given category, terminated by UAN.  Callers iterate until they hit
// UAN.  Same UserAction may appear in multiple categories.
const UserAction* GetControlsCategoryActions(ControlsCategory cat);

// Number of actions in a category (excluding the UAN terminator).
int GetControlsCategoryActionCount(ControlsCategory cat);

// True if the action appears in the category's list.
bool IsActionInControlsCategory(UserAction action, ControlsCategory cat);

// Human-readable label, e.g. "On foot" / "Vehicles" / ...  English
// fallback; the UI layer is responsible for going through LocalizeString
// when a stringtable entry exists.
const char* GetControlsCategoryName(ControlsCategory cat);

// Whether an action's row is shown on the Keyboard & Mouse / Gamepad controls page.
// Both pages share the category lists; these encode the intentional per-device
// differences (analog-stick group folding on gamepad, on-foot aim = mouse on KB&M).
// The KbmPage / GamepadPage IsActionVisible overrides delegate here.
bool IsActionVisibleOnKeyboard(UserAction action, ControlsCategory cat);
bool IsActionVisibleOnGamepad(UserAction action, ControlsCategory cat);
} // namespace Poseidon

