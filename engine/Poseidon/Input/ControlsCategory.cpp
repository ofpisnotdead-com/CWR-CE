#include <Poseidon/Input/ControlsCategory.hpp>

namespace Poseidon
{

namespace
{
// Per-category UserAction lists.  Terminated by UAN.  Order within each
// list is the order rows appear on the screen (so it's also the
// control-table order — keep them in sync).

const UserAction kOnFoot[] = {
    // Movement
    UAMoveForward, UAMoveBack, UATurnLeft, UATurnRight, UAMoveLeft, UAMoveRight, UAMoveUp, UAMoveDown,
    UAMoveFastForward, UAMoveSlowForward, UATurbo,
    UASlow, // "Walk" — the slow-movement toggle (label/default key live in UserActionDesc)
    // Weapons & combat
    UAFire, UAReloadMagazine, UAToggleWeapons, UAHandgun, UALockTarget, UALockTargets, UARevealTarget, UAOptics,
    UAZoomIn, UAZoomOut,
    // Actions
    UAAction,
    // Gamepad aim grouping; hidden on KB&M on-foot rows.
    UAAimUp, UAAimDown, UAAimLeft, UAAimRight,
    // View / freelook
    UALookAround, UALookAroundToggle, UALookCenter, UALookLeft, UALookRight, UALookUp, UALookDown, UALookLeftUp,
    UALookRightUp, UALookLeftDown, UALookRightDown,
    // HUD
    UABinocular, UANightVision, UAN};

const UserAction kVehicles[] = {UAMoveForward,  UAMoveBack,      UATurnLeft,         UATurnRight,   UATurbo,
                                UAFire,         UALockTarget,    UALockTargets,      UAAction,      UAZoomIn,
                                UAZoomOut,      UALookAround,    UALookAroundToggle, UALookCenter,  UALookLeft,
                                UALookRight,    UALookUp,        UALookDown,         UALookLeftUp,  UALookRightUp,
                                UALookLeftDown, UALookRightDown, UAHeadlights,       UANightVision, UAN};

const UserAction kPilot[] = {
    UAMoveForward,      UAMoveBack,     UATurnLeft,      UATurnRight,   UAMoveLeft, UAMoveRight, UAMoveUp,
    UAMoveDown,         UAFire,         UALockTarget,    UALockTargets, UAZoomIn,   UAZoomOut,   UALookAround,
    UALookAroundToggle, UALookCenter,   UALookLeft,      UALookRight,   UALookUp,   UALookDown,  UALookLeftUp,
    UALookRightUp,      UALookLeftDown, UALookRightDown, UAHeadlights,  UAN};

const UserAction kGunner[] = {
    UAFire,       UAReloadMagazine,   UAToggleWeapons, UALockTarget,   UALockTargets,   UAOptics,
    UAZoomIn,     UAZoomOut,          UAAimUp,         UAAimDown,      UAAimLeft,       UAAimRight,
    UALookAround, UALookAroundToggle, UALookCenter,    UALookLeft,     UALookRight,     UALookUp,
    UALookDown,   UALookLeftUp,       UALookRightUp,   UALookLeftDown, UALookRightDown, UAN};

const UserAction kCommon[] = {
    UAPrevAction,  UANextAction,  UAPersonView,   UATacticalView,   UAMap,  UACompass,      UAWatch,
    UAHelp,        UATimeInc,     UATimeDec,      UASelectAll,      UAChat, UAVoiceOverNet, UAVoiceOverNetPushToTalk,
    UAPrevChannel, UANextChannel, UANetworkStats, UANetworkPlayers, UAN};

const UserAction* const kCategoryTable[ControlsCategoryCount] = {
    kOnFoot, kVehicles, kPilot, kGunner, kCommon,
};

int Count(const UserAction* list)
{
    int n = 0;
    while (list[n] != UAN)
        ++n;
    return n;
}
} // namespace

const UserAction* GetControlsCategoryActions(ControlsCategory cat)
{
    if (cat < 0 || cat >= ControlsCategoryCount)
        return kOnFoot;
    return kCategoryTable[cat];
}

int GetControlsCategoryActionCount(ControlsCategory cat)
{
    return Count(GetControlsCategoryActions(cat));
}

bool IsActionInControlsCategory(UserAction action, ControlsCategory cat)
{
    const UserAction* list = GetControlsCategoryActions(cat);
    for (int i = 0; list[i] != UAN; ++i)
        if (list[i] == action)
            return true;
    return false;
}

const char* GetControlsCategoryName(ControlsCategory cat)
{
    switch (cat)
    {
        case ControlsCategoryOnFoot:
            return "On foot";
        case ControlsCategoryVehicles:
            return "Vehicles";
        case ControlsCategoryPilot:
            return "Pilot";
        case ControlsCategoryGunner:
            return "Gunner";
        case ControlsCategoryCommon:
            return "Common";
        default:
            return "";
    }
}
} // namespace Poseidon
