#include <Poseidon/UI/Options/PressButtonPage.hpp>

#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Input/InputDeviceConstants.hpp>
#include <Poseidon/Input/KeyInput.hpp>

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>

namespace Poseidon
{

namespace
{
// Returns the packed gamepad input currently pressed, or -1 if none.
// Polls InputSubsystem rather than the SDL event so the harness's
// triGpadButton / triGpadPov verbs (which write synthetic state directly)
// are also observable.
int CapturedGamepadInput()
{
    auto& sub = InputSubsystem::Instance();
    // Synthetic first — prevents phantom HID devices from overriding intentional triGpadButton/triGpadPov injection.
    for (int i = 0; i < N_JOYSTICK_BUTTONS; i++)
        if (sub.ConsumeSyntheticStickButton(i))
            return INPUT_DEVICE_STICK + i;
    for (int i = 0; i < N_JOYSTICK_POV; i++)
        if (sub.ConsumeSyntheticStickPov(i))
            return INPUT_DEVICE_STICK_POV + i;
    for (int i = 0; i < N_JOYSTICK_AXES; i++)
        if (sub.ConsumeAxisBigActive(i))
            return INPUT_DEVICE_STICK_AXIS + i;
    // Physical fallback.
    for (int i = 0; i < N_JOYSTICK_BUTTONS; i++)
        if (sub.GetStickButtonToDo(i))
            return INPUT_DEVICE_STICK + i;
    for (int i = 0; i < N_JOYSTICK_POV; i++)
        if (sub.GetStickPovToDo(i))
            return INPUT_DEVICE_STICK_POV + i;
    return -1;
}

int CapturedGamepadModifier(int mainCode)
{
    if (InputBindingDevice(mainCode) != INPUT_DEVICE_STICK_AXIS)
        return -1;

    auto& sub = InputSubsystem::Instance();
    for (int i = 0; i < N_JOYSTICK_BUTTONS; i++)
    {
        int packed = INPUT_DEVICE_STICK + i;
        if (packed != mainCode && sub.IsGamepadButtonDown(i))
            return packed;
    }
    return -1;
}
} // namespace

CapturePage::Result PressButtonPage::InterpretKey(unsigned nChar, int& outPackedCode, int& outModifier) const
{
    // Prefer a concurrent gamepad-button press if one is available —
    // that's the canonical capture path.
    int gpad = CapturedGamepadInput();
    if (gpad >= 0)
    {
        outPackedCode = gpad;
        outModifier = CapturedGamepadModifier(gpad);
        return Result::Main;
    }

    // Fallback: accept a keyboard scancode so dev / harness keyboard
    // injection still works while real gamepad capture lands.  Refuses
    // standalone modifier keys (Ctrl/Shift/Alt).
    switch (nChar)
    {
        case SDLK_LCTRL:
        case SDLK_RCTRL:
        case SDLK_LSHIFT:
        case SDLK_RSHIFT:
        case SDLK_LALT:
        case SDLK_RALT:
            return Result::Refused;
        default:
            break;
    }

    // SDL3 maps keycode 0 (SDLK_UNKNOWN) to a non-UNKNOWN scancode on some
    // platforms, so guard it explicitly before calling into SDL.
    if (nChar == 0)
        return Result::Refused;

    SDL_Scancode sc = SDL_GetScancodeFromKey((SDL_Keycode)nChar, nullptr);
    if (sc == SDL_SCANCODE_UNKNOWN)
        return Result::Refused;
    outPackedCode = (int)sc;
    outModifier = -1;
    return Result::Main;
}

bool PressButtonPage::OnKeyDown(OptionsShell& shell, unsigned nChar)
{
    if (IsListening())
    {
        int gpad = CapturedGamepadInput();
        if (gpad >= 0)
            return TryCapture(shell, gpad, CapturedGamepadModifier(gpad));
    }
    return CapturePage::OnKeyDown(shell, nChar);
}

void PressButtonPage::OnSimulate(OptionsShell& shell)
{
    CapturePage::OnSimulate(shell);
    if (!IsListening())
        return;

    // OnSimulate polls for gamepad input that arrived since the last frame.
    // Do NOT route through InterpretKey(0) — keycode 0 is not a real key and
    // SDL3 may map it to a non-UNKNOWN scancode, capturing garbage.
    int gpad = CapturedGamepadInput();
    if (gpad >= 0)
        TryCapture(shell, gpad, CapturedGamepadModifier(gpad));
}

} // namespace Poseidon
