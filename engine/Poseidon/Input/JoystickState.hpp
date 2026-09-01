#pragma once

#include <Poseidon/Input/InputDeviceConstants.hpp>

namespace Poseidon
{

// Bindings address axes as fixed slots (0=X 1=Y 2=Z 3=Rx 4=Ry 5=Rz 6=Slider0
// 7=Slider1), but SDL reports only the axes a device has, densely in hardware order.
// Resolve a dense index to its slot from the axis count, so a four-axis stick lands
// on 0, 1, 5 and 6 (X, Y, twist, throttle) instead of 0 through 3.
// Returns -1 when the index has no slot.
inline int JoystickAxisSlot(int axisCount, int index)
{
    static const int k1[] = {0};
    static const int k2[] = {0, 1};
    static const int k3[] = {0, 1, 5};
    static const int k4[] = {0, 1, 5, 6};
    static const int k5[] = {0, 1, 5, 6, 7};
    static const int k6[] = {0, 1, 2, 3, 4, 5};
    static const int k7[] = {0, 1, 2, 3, 4, 5, 6};
    static const int k8[] = {0, 1, 2, 3, 4, 5, 6, 7};
    static const int* const kMaps[] = {nullptr, k1, k2, k3, k4, k5, k6, k7, k8};

    if (axisCount < 1 || index < 0)
        return -1;
    if (axisCount > N_RAW_JOYSTICK_AXES)
        axisCount = N_RAW_JOYSTICK_AXES;
    if (index >= axisCount)
        return -1;
    return kMaps[axisCount][index];
}

// Raw state of a joystick opened without a gamepad mapping. Axes are stored by slot,
// not by the index SDL reported them under.
struct JoystickState
{
    bool connected = false;
    int axisCount = 0;
    int buttonCount = 0;
    int hatCount = 0;

    float axis[N_RAW_JOYSTICK_AXES] = {};

    float buttons[N_RAW_JOYSTICK_BUTTONS] = {};
    bool buttonsToDo[N_RAW_JOYSTICK_BUTTONS] = {};

    bool pov[N_RAW_JOYSTICK_POV] = {};
    bool povOld[N_RAW_JOYSTICK_POV] = {};
    bool povToDo[N_RAW_JOYSTICK_POV] = {};

    bool enabled = true;
    // Applies to every axis: a slider is indistinguishable from a centring one here.
    float deadzone = 0.07f;

    void Clear()
    {
        for (int i = 0; i < N_RAW_JOYSTICK_AXES; i++)
            axis[i] = 0.0f;
        for (int i = 0; i < N_RAW_JOYSTICK_BUTTONS; i++)
        {
            buttons[i] = 0.0f;
            buttonsToDo[i] = false;
        }
        for (int i = 0; i < N_RAW_JOYSTICK_POV; i++)
        {
            pov[i] = false;
            povToDo[i] = false;
        }
    }
};
} // namespace Poseidon
