#pragma once

#include <Poseidon/Core/Global.hpp>

#include <Poseidon/Input/UserAction.hpp>
#include <Poseidon/Input/UserActionDesc.hpp>
#include <Poseidon/Input/GamepadState.hpp>
#include <Poseidon/Input/JoystickState.hpp>
#include <Poseidon/Input/MouseState.hpp>
#include <Poseidon/Input/KeyboardState.hpp>

namespace Poseidon
{

struct Input
{
    KeyList userKeys[UAN];
    // Deprecated flat binding storage kept for legacy helper paths and tests.
    // Active action bindings live in InputSubsystem context profiles.
    KeyList userKeysModifiers[UAN];
    bool actionDone[UAN] = {};

    KeyboardState keyboard;
    MouseState mouse;
    MouseState::CursorAccum cursor;
    GamepadState gamepad;
    JoystickState joystick;

    bool lookAroundToggleEnabled = false;
    bool lookAroundEnabled = false;

    bool fire = false, fireToDo = false;

    float keyTurnLeft = 0, keyTurnRight = 0;

    float keyMoveFastForward = 0, keyMoveSlowForward = 0;
    float keyMoveForward = 0, keyMoveBack = 0;
    float keyMoveUp = 0, keyMoveDown = 0;
    float keyMoveLeft = 0, keyMoveRight = 0;

    int gameFocusLost = 0;
};
} // namespace Poseidon

