#pragma once

namespace Poseidon
{

#define N_MOUSE_BUTTONS 8
#define N_JOYSTICK_BUTTONS 12 // [0-3]=A/B/X/Y [4-5]=LB/RB [6-7]=LT/RT thresh [8-9]=Back/Start [10-11]=LThumb/RThumb
#define N_JOYSTICK_AXES 8     // 3 normal, 3 rotate, 2 sliders
#define N_JOYSTICK_POV 8      // pov - 8 state

// Capacities for a device read through the raw joystick API.
#define N_RAW_JOYSTICK_BUTTONS 32
#define N_RAW_JOYSTICK_AXES 8
#define N_RAW_JOYSTICK_HATS 4
#define N_RAW_JOYSTICK_POV (N_RAW_JOYSTICK_HATS * 8) // hat*8 + direction

#define INPUT_DEVICE_MASK 0x00ff0000
#define INPUT_DEVICE_KEYBOARD 0x00000000
#define INPUT_DEVICE_MOUSE 0x00010000
#define INPUT_DEVICE_STICK 0x00020000
#define INPUT_DEVICE_STICK_AXIS 0x00030000
#define INPUT_DEVICE_STICK_POV 0x00040000
#define INPUT_DEVICE_JOYSTICK 0x00050000
#define INPUT_DEVICE_JOYSTICK_AXIS 0x00060000
#define INPUT_DEVICE_JOYSTICK_POV 0x00070000
#define INPUT_DEVICE_MOUSE_AXIS 0x00100000

#define INPUT_MOUSE_WHEEL_UP 4
#define INPUT_MOUSE_WHEEL_DOWN 5

// Binding mode bits live below INPUT_DEVICE_MASK so the existing packed
// device class stays stable on disk and in conflict checks.
#define INPUT_BINDING_DOUBLE_TAP 0x00008000
#define INPUT_BINDING_VALUE_MASK 0x00007fff

inline bool InputBindingIsDoubleTap(int packedCode)
{
    return (packedCode & INPUT_BINDING_DOUBLE_TAP) != 0;
}

inline int InputBindingDevice(int packedCode)
{
    return packedCode & INPUT_DEVICE_MASK;
}

inline int InputBindingValue(int packedCode)
{
    return packedCode & INPUT_BINDING_VALUE_MASK;
}

inline int InputBindingBaseCode(int packedCode)
{
    return InputBindingDevice(packedCode) | InputBindingValue(packedCode);
}

inline bool InputBindingIsRawJoystick(int packedCode)
{
    const int dev = InputBindingDevice(packedCode);
    return dev == INPUT_DEVICE_JOYSTICK || dev == INPUT_DEVICE_JOYSTICK_AXIS || dev == INPUT_DEVICE_JOYSTICK_POV;
}

inline int InputBindingDoubleTapCode(int packedCode)
{
    return InputBindingBaseCode(packedCode) | INPUT_BINDING_DOUBLE_TAP;
}
} // namespace Poseidon
