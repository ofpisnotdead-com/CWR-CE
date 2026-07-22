#pragma once

#include <Poseidon/Foundation/Types/EnumDecl.hpp>

namespace Poseidon
{

DECL_ENUM(UserAction)

DEFINE_ENUM_BEG(UserAction)
// user defined keys
UAMoveForward, UAMoveBack, UATurnLeft, UATurnRight, UAMoveUp, UAMoveDown, UAMoveFastForward, UAMoveSlowForward,
    UAMoveLeft, UAMoveRight, UAToggleWeapons, UAFire, UAReloadMagazine,
    UALockTargets, // next / prev target - SDL_SCANCODE_TAB
    UALockTarget,  // lock target - RIGHT MOUSE BUTTON
    UARevealTarget, UAPrevAction, UANextAction, UAAction, UAHeadlights, UANightVision, UABinocular, UAHandgun,
    UACompass, UAWatch, UAMap, UAHelp, UATimeInc, UATimeDec, UAOptics, UAPersonView, UATacticalView, UAZoomIn,
    UAZoomOut, UALookAround, UALookAroundToggle, UALookLeftDown, UALookDown, UALookRightDown, UALookLeft, UALookCenter,
    UALookRight, UALookLeftUp, UALookUp, UALookRightUp, UAPrevChannel, UANextChannel, UAChat, UAVoiceOverNet,
    UAVoiceOverNetPushToTalk, UANetworkStats, UANetworkPlayers, UASelectAll, UATurbo, UASlow,

    UAAxisTurn, UAAxisDive, UAAxisRudder, UAAxisThrust,
    UAAimUp, UAAimDown, UAAimLeft, UAAimRight,
#if _ENABLE_CHEATS
    UACheat1, UACheat2,
#endif
    UAN // terminator
        DEFINE_ENUM_END(UserAction)
} // namespace Poseidon

