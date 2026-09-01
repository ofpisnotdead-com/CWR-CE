#pragma once

#include <SDL3/SDL_scancode.h>
#include <cstdint>

namespace Poseidon
{

enum class InputDevice : uint32_t
{
    Keyboard = 0x00000000,
    Mouse = 0x00010000,
    GamepadButton = 0x00020000,
    GamepadAxis = 0x00030000,
    GamepadPOV = 0x00040000,
    JoystickButton = 0x00050000,
    JoystickAxis = 0x00060000,
    JoystickPOV = 0x00070000,
    MouseAxis = 0x00100000,
};

constexpr uint32_t INPUT_CODE_DEVICE_MASK = 0x00ff0000;
constexpr uint32_t INPUT_CODE_VALUE_MASK = 0x0000ffff;

// Typed input code — packs device type + code index into a single value.
// Compatible with existing INPUT_DEVICE_* | code integers used in KeyList.
struct InputCode
{
    uint32_t raw = 0;

    constexpr InputCode() = default;
    constexpr explicit InputCode(uint32_t packed) : raw(packed) {}

    static constexpr InputCode Key(SDL_Scancode sc)
    {
        return InputCode(static_cast<uint32_t>(InputDevice::Keyboard) | static_cast<uint32_t>(sc));
    }
    static constexpr InputCode MouseButton(int btn)
    {
        return InputCode(static_cast<uint32_t>(InputDevice::Mouse) | static_cast<uint32_t>(btn));
    }
    static constexpr InputCode MouseWheelUp() { return InputCode(static_cast<uint32_t>(InputDevice::MouseAxis) | 4); }
    static constexpr InputCode MouseWheelDown() { return InputCode(static_cast<uint32_t>(InputDevice::MouseAxis) | 5); }
    static constexpr InputCode GamepadBtn(int btn)
    {
        return InputCode(static_cast<uint32_t>(InputDevice::GamepadButton) | static_cast<uint32_t>(btn));
    }
    static constexpr InputCode GamepadAx(int axis)
    {
        return InputCode(static_cast<uint32_t>(InputDevice::GamepadAxis) | static_cast<uint32_t>(axis));
    }
    static constexpr InputCode GamepadPov(int pov)
    {
        return InputCode(static_cast<uint32_t>(InputDevice::GamepadPOV) | static_cast<uint32_t>(pov));
    }
    static constexpr InputCode JoystickBtn(int btn)
    {
        return InputCode(static_cast<uint32_t>(InputDevice::JoystickButton) | static_cast<uint32_t>(btn));
    }
    static constexpr InputCode JoystickAx(int axis)
    {
        return InputCode(static_cast<uint32_t>(InputDevice::JoystickAxis) | static_cast<uint32_t>(axis));
    }
    static constexpr InputCode JoystickPov(int hat, int dir)
    {
        return InputCode(static_cast<uint32_t>(InputDevice::JoystickPOV) | static_cast<uint32_t>(hat * 8 + dir));
    }

    static constexpr InputCode FromLegacy(int packed) { return InputCode(static_cast<uint32_t>(packed)); }

    constexpr InputDevice device() const { return static_cast<InputDevice>(raw & INPUT_CODE_DEVICE_MASK); }
    constexpr uint32_t code() const { return raw & INPUT_CODE_VALUE_MASK; }
    constexpr int toLegacy() const { return static_cast<int>(raw); }
    constexpr bool valid() const { return raw != 0; }

    constexpr bool operator==(InputCode other) const { return raw == other.raw; }
    constexpr bool operator!=(InputCode other) const { return raw != other.raw; }
    constexpr bool operator<(InputCode other) const { return raw < other.raw; }
};
} // namespace Poseidon
