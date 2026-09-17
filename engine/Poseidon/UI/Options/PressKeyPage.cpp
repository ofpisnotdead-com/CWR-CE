#include <Poseidon/UI/Options/PressKeyPage.hpp>

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
bool IsModifierOnly(unsigned key)
{
    switch (key)
    {
        case SDLK_LCTRL:
        case SDLK_RCTRL:
        case SDLK_LSHIFT:
        case SDLK_RSHIFT:
        case SDLK_LALT:
        case SDLK_RALT:
        case SDLK_LGUI:
        case SDLK_RGUI:
            return true;
        default:
            return false;
    }
}

bool IsModifierScancode(int sc)
{
    switch (sc)
    {
        case SDL_SCANCODE_LCTRL:
        case SDL_SCANCODE_RCTRL:
        case SDL_SCANCODE_LSHIFT:
        case SDL_SCANCODE_RSHIFT:
        case SDL_SCANCODE_LALT:
        case SDL_SCANCODE_RALT:
        case SDL_SCANCODE_LGUI:
        case SDL_SCANCODE_RGUI:
            return true;
        default:
            return false;
    }
}

// SDL keycode → SDL scancode.  v1 storage is the raw SDL scancode in
// the low bits — same encoding the table uses.
SDL_Scancode KeyToScancode(unsigned key)
{
    return SDL_GetScancodeFromKey((SDL_Keycode)key, nullptr);
}

// Snapshot the currently-held modifier scancode.  Returns the canonical
// left variant (LCtrl / LShift / LAlt) regardless of which side was
// pressed — the gameplay resolver checks the exact scancode, so storing
// LCtrl means "Ctrl on the left specifically", which is fine for v1
// (matches the modifier the user pressed at capture time).
int CapturedModifierScancode()
{
    auto& sub = InputSubsystem::Instance();
    if (sub.IsKeyDown(SDL_SCANCODE_LCTRL))
        return (int)SDL_SCANCODE_LCTRL;
    if (sub.IsKeyDown(SDL_SCANCODE_RCTRL))
        return (int)SDL_SCANCODE_RCTRL;
    if (sub.IsKeyDown(SDL_SCANCODE_LSHIFT))
        return (int)SDL_SCANCODE_LSHIFT;
    if (sub.IsKeyDown(SDL_SCANCODE_RSHIFT))
        return (int)SDL_SCANCODE_RSHIFT;
    if (sub.IsKeyDown(SDL_SCANCODE_LALT))
        return (int)SDL_SCANCODE_LALT;
    if (sub.IsKeyDown(SDL_SCANCODE_RALT))
        return (int)SDL_SCANCODE_RALT;
    return -1;
}

int CapturedMouseButtonInput()
{
    auto& sub = InputSubsystem::Instance();
    for (int i = 0; i < N_MOUSE_BUTTONS; ++i)
    {
        if (sub.GetMouseButtonToDo(i))
            return INPUT_DEVICE_MOUSE + i;
    }
    return -1;
}
} // namespace

bool PressKeyPage::IsBareModifierCode(int packedCode) const
{
    return IsModifierScancode(packedCode);
}

CapturePage::Result PressKeyPage::InterpretKey(unsigned nChar, int& outPackedCode, int& outModifier) const
{
    SDL_Scancode sc = KeyToScancode(nChar);
    if (sc == SDL_SCANCODE_UNKNOWN)
        return Result::Refused;

    outPackedCode = (int)sc;
    // A lone modifier captures standalone; a real key pressed while it is held
    // upgrades to a "Mod+Key" combo. Other keys pick up the modifier held now.
    outModifier = IsModifierOnly(nChar) ? -1 : CapturedModifierScancode();
    return Result::Main;
}

bool PressKeyPage::OnKeyDown(OptionsShell& shell, unsigned nChar)
{
    int packed = -1;
    int modifier = -1;
    if (InterpretKey(nChar, packed, modifier) == Result::Main &&
        (TryUpgradeToCombo(shell, packed, modifier) || TryUpgradeToDoubleTap(shell, packed, modifier)))
        return true;
    return CapturePage::OnKeyDown(shell, nChar);
}

void PressKeyPage::OnSimulate(OptionsShell& shell)
{
    CapturePage::OnSimulate(shell);
    auto& input = InputSubsystem::Instance();
    const float wheel = input.GetMouseWheel();
    if (m_allowMouseWheel && IsListening() && wheel != 0.0f)
    {
        input.ConsumeCursorScroll();
        TryCapture(shell,
                   wheel > 0.0f ? INPUT_DEVICE_MOUSE_AXIS + INPUT_MOUSE_WHEEL_UP
                                : INPUT_DEVICE_MOUSE_AXIS + INPUT_MOUSE_WHEEL_DOWN,
                   -1);
        return;
    }

    const int mouse = CapturedMouseButtonInput();
    if (mouse < 0)
        return;

    if (IsListening())
        TryCapture(shell, mouse, -1);
    else
        TryUpgradeToDoubleTap(shell, mouse, -1);
}

} // namespace Poseidon
