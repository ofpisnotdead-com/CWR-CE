#pragma once

// Gamepad scalar state persisted to <user-dir>/gamepad.cfg.
// Per-action bindings live in contextControls.cfg.
//
// File shape:
//   enabled=1;
//   reverseYStick=0;
//   deadzoneStick=0.21;
//   deadzoneTrigger=0.10;
//   lookSensitivity=1.0;
// Lifecycle mirrors AudioConfig / MouseConfig:
//   1. Boot: try Load(path).  Missing/corrupt → LoadDefaults() + Save.
//   2. Boot: Normalize() clamps deadzones to [0.0, 0.5] and look
//            sensitivity to [0.1, 5.0].
//   3. UI:   GamepadTuningPage applies live and Save on Unmount via
//            InputSubsystem::SaveKeys.

#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Input/UserAction.hpp>

#include <string>


namespace Poseidon
{
class GamepadConfig
{
public:
    // Tuning scalars (live values mirrored to GInput.gamepad.*).
    bool  enabled         = true;
    bool  reverseYStick   = false;
    float deadzoneStick   = 0.21f;
    float deadzoneTrigger = 0.10f;
    float lookSensitivity = 1.0f;

    // Deprecated binding fields kept only for callers/tests that still
    // construct GamepadConfig directly. LoadDefaults/Load/Save do not
    // seed or persist these; contextControls.cfg owns action bindings.
    AutoArray<int> bindings[UAN];
    AutoArray<int> modifiers[UAN];

    // True if a packed binding code belongs to the gamepad device
    // class (STICK / STICK_AXIS / STICK_POV).  Used by the InputSubsystem
    // split logic and by Save to filter out non-gamepad entries.
    static bool IsGamepadCode(int packedCode);

    // Reset every field to factory defaults for scalar tuning.
    void LoadDefaults();

    // Clamp deadzones to [0.0, 0.5] and lookSensitivity to [0.1, 5.0].
    // Returns true if any field was changed.  No Environment — gamepad
    // tuning is purely numeric, no live device list to validate against.
    bool Normalize();

    // True on successful parse, false if the file doesn't exist or is
    // unparseable.  Same chain-friendly contract as the other configs.
    bool Load(const std::string& path);

    // Writes via ParamFile.  Returns false on I/O error.
    bool Save(const std::string& path) const;
};

} // namespace Poseidon
