#pragma once

#include <Poseidon/Input/InputDeviceConstants.hpp>
#include <Poseidon/Core/Global.hpp>

namespace Poseidon
{

struct GamepadState
{
    // Per-index state arrays (legacy engine format, indexed by UserAction offsets)
    bool stickPov[N_JOYSTICK_POV] = {};
    bool stickPovOld[N_JOYSTICK_POV] = {};
    bool stickPovToDo[N_JOYSTICK_POV] = {};

    float stickButtons[N_JOYSTICK_BUTTONS] = {};
    bool stickButtonsToDo[N_JOYSTICK_BUTTONS] = {};

    float stickAxis[N_JOYSTICK_AXES] = {};

    // Configuration (persisted to UserInfo.cfg)
    bool enabled = true;
    bool reverseYStick = false;
    float deadzoneStick = 0.21f;
    float deadzoneTrigger = 0.10f;
    float lookSensitivity = 1.0f;

    // Activity timestamps
    Foundation::UITime moveLastActive = UITIME_MIN;
    Foundation::UITime thrustLastActive = UITIME_MIN;

    // Axis hysteresis state
    int jAxisLast[N_JOYSTICK_AXES] = {};
    Foundation::UITime jAxisLastActive[N_JOYSTICK_AXES] = {UITIME_MIN, UITIME_MIN, UITIME_MIN, UITIME_MIN,
                                                UITIME_MIN, UITIME_MIN, UITIME_MIN, UITIME_MIN};
    int jAxisBigLast[N_JOYSTICK_AXES] = {};
    Foundation::UITime jAxisBigLastActive[N_JOYSTICK_AXES] = {UITIME_MIN, UITIME_MIN, UITIME_MIN, UITIME_MIN,
                                                   UITIME_MIN, UITIME_MIN, UITIME_MIN, UITIME_MIN};
};
} // namespace Poseidon

