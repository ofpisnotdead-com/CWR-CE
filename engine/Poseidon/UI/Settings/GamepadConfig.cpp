#include <Poseidon/UI/Settings/GamepadConfig.hpp>

#include <Poseidon/Input/InputDeviceConstants.hpp>

#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/UI/Settings/SettingsFile.hpp>

#include <algorithm>
#include <Poseidon/Foundation/Strings/RString.hpp>

namespace Poseidon
{

namespace
{
constexpr float kMinDeadzone = 0.0f;
constexpr float kMaxDeadzone = 0.5f;
constexpr float kMinLookSens = 0.1f;
constexpr float kMaxLookSens = 5.0f;
} // namespace

bool GamepadConfig::IsGamepadCode(int packedCode)
{
    if (packedCode < 0)
        return false;
    int dev = packedCode & INPUT_DEVICE_MASK;
    return dev == INPUT_DEVICE_STICK || dev == INPUT_DEVICE_STICK_AXIS || dev == INPUT_DEVICE_STICK_POV;
}

void GamepadConfig::LoadDefaults()
{
    enabled = true;
    reverseYStick = false;
    deadzoneStick = 0.21f;
    deadzoneTrigger = 0.10f;
    lookSensitivity = 1.0f;
    for (int i = 0; i < UAN; i++)
    {
        bindings[i].Resize(0);
        modifiers[i].Resize(0);
    }
}

bool GamepadConfig::Normalize()
{
    bool changed = false;
    auto clampF = [&](float& v, float lo, float hi)
    {
        float c = std::clamp(v, lo, hi);
        if (c != v)
        {
            v = c;
            changed = true;
        }
    };
    clampF(deadzoneStick, kMinDeadzone, kMaxDeadzone);
    clampF(deadzoneTrigger, kMinDeadzone, kMaxDeadzone);
    clampF(lookSensitivity, kMinLookSens, kMaxLookSens);
    return changed;
}

bool GamepadConfig::Load(const std::string& path)
{
    ParamFile cfg;
    if (!ReadSettingsFile(path, cfg))
        return false;

    if (auto* e = cfg.FindEntry("enabled"))
        enabled = (bool)*e;
    if (auto* e = cfg.FindEntry("reverseYStick"))
        reverseYStick = (bool)*e;
    if (auto* e = cfg.FindEntry("deadzoneStick"))
        deadzoneStick = (float)*e;
    if (auto* e = cfg.FindEntry("deadzoneTrigger"))
        deadzoneTrigger = (float)*e;
    if (auto* e = cfg.FindEntry("lookSensitivity"))
        lookSensitivity = (float)*e;

    for (int i = 0; i < UAN; i++)
    {
        bindings[i].Resize(0);
        modifiers[i].Resize(0);
    }

    return true;
}

bool GamepadConfig::Save(const std::string& path) const
{
    ParamFile cfg;
    cfg.Add("enabled", enabled);
    cfg.Add("reverseYStick", reverseYStick);
    cfg.Add("deadzoneStick", deadzoneStick);
    cfg.Add("deadzoneTrigger", deadzoneTrigger);
    cfg.Add("lookSensitivity", lookSensitivity);

    return WriteSettingsFile(path, cfg);
}

} // namespace Poseidon
