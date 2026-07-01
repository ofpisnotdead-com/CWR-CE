#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Input/KeyInput.hpp>
#include <Poseidon/Input/TouchInput.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <Poseidon/Core/resincl.hpp>
#include <Poseidon/Foundation/Platform/AppConfig.hpp>
#include <Poseidon/UI/Settings/ContextControlsConfig.hpp>
#include <Poseidon/UI/Settings/GamepadConfig.hpp>
#include <Poseidon/UI/Settings/MouseConfig.hpp>
#include <Poseidon/UI/Settings/TouchConfig.hpp>
#include <SDL3/SDL_scancode.h>
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <math.h>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>
#include <vector>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>

namespace Poseidon
{

Input GInput;

namespace
{
std::string MouseCfgPath()
{
    return GamePaths::Instance().UserDir() + "mouse.cfg";
}
std::string GamepadCfgPath()
{
    return GamePaths::Instance().UserDir() + "gamepad.cfg";
}
std::string TouchCfgPath()
{
    return GamePaths::Instance().UserDir() + "touch.cfg";
}
std::string ContextControlsCfgPath()
{
    return GamePaths::Instance().UserDir() + "contextControls.cfg";
}

struct ContextList
{
    const InputContext* data = nullptr;
    int count = 0;
};

ContextList ContextsForCategory(ControlsCategory cat)
{
    static constexpr InputContext onFoot[] = {InputContext::Infantry};
    static constexpr InputContext vehicles[] = {InputContext::CarDriver, InputContext::TankDriver,
                                                InputContext::ShipDriver};
    static constexpr InputContext pilot[] = {InputContext::HeliPilot, InputContext::PlanePilot};
    static constexpr InputContext gunner[] = {InputContext::TankGunner, InputContext::Gunner};
    static constexpr InputContext common[] = {
        InputContext::Menu,       InputContext::Infantry,  InputContext::CarDriver, InputContext::TankDriver,
        InputContext::TankGunner, InputContext::HeliPilot, InputContext::PlanePilot, InputContext::ShipDriver,
        InputContext::Gunner,     InputContext::Spectator, InputContext::Map,       InputContext::Chat,
        InputContext::Editor,
    };

    switch (cat)
    {
        case ControlsCategoryOnFoot:
            return {onFoot, static_cast<int>(std::size(onFoot))};
        case ControlsCategoryVehicles:
            return {vehicles, static_cast<int>(std::size(vehicles))};
        case ControlsCategoryPilot:
            return {pilot, static_cast<int>(std::size(pilot))};
        case ControlsCategoryGunner:
            return {gunner, static_cast<int>(std::size(gunner))};
        case ControlsCategoryCommon:
            return {common, static_cast<int>(std::size(common))};
        default:
            return {onFoot, static_cast<int>(std::size(onFoot))};
    }
}
} // namespace

static float QueryKey(const Input& in, int dik, bool checkFocus)
{
    if (checkFocus && in.gameFocusLost > 0)
        return 0;
#if _ENABLE_CHEATS
    if (in.keyboard.cheat1 || in.keyboard.cheat2)
        return 0;
#endif
    if (in.keyboard.awaitCheat)
        return 0;
    if (dik < 0 || dik >= SDL_SCANCODE_COUNT)
        return 0;
    return in.keyboard.keys[dik];
}

static float QueryDoubleTapKey(const Input& in, int dik, bool checkFocus)
{
    if (checkFocus && in.gameFocusLost > 0)
        return 0;
#if _ENABLE_CHEATS
    if (in.keyboard.cheat1 || in.keyboard.cheat2)
        return 0;
#endif
    if (in.keyboard.awaitCheat)
        return 0;
    if (dik < 0 || dik >= SDL_SCANCODE_COUNT)
        return 0;
    return in.keyboard.keysDoubleTapActive[dik] ? in.keyboard.keys[dik] : 0.0f;
}

static bool QueryKeyToDo(Input& in, int dik, bool reset, bool checkFocus)
{
    if (checkFocus && in.gameFocusLost > 0)
        return false;
#if _ENABLE_CHEATS
    if (in.keyboard.cheat1 || in.keyboard.cheat2)
        return false;
#endif
    if (in.keyboard.awaitCheat)
        return false;
    if (dik < 0 || dik >= SDL_SCANCODE_COUNT)
        return false;
    bool ret = in.keyboard.keysToDo[dik];
    if (reset)
        in.keyboard.keysToDo[dik] = false;
    return ret;
}

static bool QueryDoubleTapKeyToDo(Input& in, int dik, bool reset, bool checkFocus)
{
    if (checkFocus && in.gameFocusLost > 0)
        return false;
#if _ENABLE_CHEATS
    if (in.keyboard.cheat1 || in.keyboard.cheat2)
        return false;
#endif
    if (in.keyboard.awaitCheat)
        return false;
    if (dik < 0 || dik >= SDL_SCANCODE_COUNT)
        return false;
    bool ret = in.keyboard.keysDoubleTapToDo[dik];
    if (reset)
        in.keyboard.keysDoubleTapToDo[dik] = false;
    return ret;
}

static float QueryMouseButton(const Input& in, int index, bool checkFocus)
{
    if (checkFocus && in.gameFocusLost > 0)
        return 0;
    if (index < 0 || index >= N_MOUSE_BUTTONS)
        return 0;
    return in.mouse.buttons[index];
}

static float QueryDoubleTapMouseButton(const Input& in, int index, bool checkFocus)
{
    if (checkFocus && in.gameFocusLost > 0)
        return 0;
    if (index < 0 || index >= N_MOUSE_BUTTONS)
        return 0;
    return in.mouse.buttonsDoubleActive[index] ? in.mouse.buttons[index] : 0.0f;
}

static bool QueryMouseButtonToDo(Input& in, int index, bool reset, bool checkFocus)
{
    if (checkFocus && in.gameFocusLost > 0)
        return false;
    if (index < 0 || index >= N_MOUSE_BUTTONS)
        return false;
    bool ret = in.mouse.buttonsToDo[index];
    if (reset)
        in.mouse.buttonsToDo[index] = false;
    return ret;
}

static bool QueryDoubleTapMouseButtonToDo(Input& in, int index, bool reset, bool checkFocus)
{
    if (checkFocus && in.gameFocusLost > 0)
        return false;
    if (index < 0 || index >= N_MOUSE_BUTTONS)
        return false;
    bool ret = in.mouse.buttonsDoubleToDo[index];
    if (reset)
        in.mouse.buttonsDoubleToDo[index] = false;
    return ret;
}

static float QueryJoystickButton(const Input& in, int index, bool checkFocus)
{
    if (checkFocus && in.gameFocusLost > 0)
        return 0;
    if (index < 0)
        return 0;
    return in.gamepad.stickButtons[index];
}

static bool QueryJoystickButtonToDo(Input& in, int index, bool reset, bool checkFocus)
{
    if (checkFocus && in.gameFocusLost > 0)
        return false;
    if (index < 0)
        return false;
    bool ret = in.gamepad.stickButtonsToDo[index];
    if (reset)
        in.gamepad.stickButtonsToDo[index] = false;
    return ret;
}

// Modifier check: a v1 binding can carry an optional held input in the
// parallel userKeysModifiers[] array.  If set, the modifier must be
// currently held for the binding to fire.  Returns true if no modifier
// is required OR the modifier is held.
static bool ModifierHeld(const Input& in, UserAction action, int slot)
{
    if (slot >= in.userKeysModifiers[action].Size())
        return true;
    int mod = in.userKeysModifiers[action][slot];
    if (mod < 0)
        return true;

    const int value = InputBindingValue(mod);
    switch (InputBindingDevice(mod))
    {
        case INPUT_DEVICE_KEYBOARD:
            return value >= 0 && value < SDL_SCANCODE_COUNT && in.keyboard.keys[value] > 0.0f;
        case INPUT_DEVICE_MOUSE:
            return value >= 0 && value < N_MOUSE_BUTTONS && in.mouse.buttons[value] > 0.0f;
        case INPUT_DEVICE_STICK:
            return value >= 0 && value < N_JOYSTICK_BUTTONS && in.gamepad.stickButtons[value] > 0.0f;
        case INPUT_DEVICE_STICK_AXIS:
            return value >= 0 && value < N_JOYSTICK_AXES && std::fabs(in.gamepad.stickAxis[value]) > 0.8f;
        case INPUT_DEVICE_STICK_POV:
            return value >= 0 && value < N_JOYSTICK_POV && in.gamepad.stickPov[value];
        default:
            return false;
    }
}

static float QueryAction(const Input& in, UserAction action, bool checkFocus)
{
    float sum = 0;
    for (int i = 0; i < in.userKeys[action].Size(); i++)
    {
        if (!ModifierHeld(in, action, i))
            continue;
        int key = in.userKeys[action][i];
        const int value = InputBindingValue(key);
        switch (InputBindingDevice(key))
        {
            case INPUT_DEVICE_KEYBOARD:
                sum += InputBindingIsDoubleTap(key) ? QueryDoubleTapKey(in, value, checkFocus)
                                                    : QueryKey(in, value, checkFocus);
                break;
            case INPUT_DEVICE_MOUSE:
                sum += InputBindingIsDoubleTap(key) ? QueryDoubleTapMouseButton(in, value, checkFocus)
                                                    : QueryMouseButton(in, value, checkFocus);
                break;
            case INPUT_DEVICE_STICK:
                sum += QueryJoystickButton(in, value, checkFocus);
                break;
            case INPUT_DEVICE_STICK_AXIS:
            {
                int offset = value;
                PoseidonAssert(offset >= 0);
                PoseidonAssert(offset < N_JOYSTICK_AXES);
                sum += in.gamepad.stickAxis[offset];
            }
            break;
            case INPUT_DEVICE_STICK_POV:
            {
                int offset = value;
                PoseidonAssert(offset >= 0);
                PoseidonAssert(offset < N_JOYSTICK_POV);
                sum += in.gamepad.stickPov[offset];
            }
            break;
        }
    }
    return sum;
}

static bool QueryActionToDo(Input& in, UserAction action, bool reset, bool checkFocus)
{
    if (in.actionDone[action])
        return false;
    if (reset)
        in.actionDone[action] = true;

    bool found = false;
    for (int i = 0; i < in.userKeys[action].Size(); i++)
    {
        if (!ModifierHeld(in, action, i))
            continue;
        int key = in.userKeys[action][i];
        const int value = InputBindingValue(key);
        switch (InputBindingDevice(key))
        {
            case INPUT_DEVICE_KEYBOARD:
                if (InputBindingIsDoubleTap(key) ? QueryDoubleTapKeyToDo(in, value, false, checkFocus)
                                                 : QueryKeyToDo(in, value, false, checkFocus))
                    found = true;
                break;
            case INPUT_DEVICE_MOUSE:
                if (InputBindingIsDoubleTap(key) ? QueryDoubleTapMouseButtonToDo(in, value, false, checkFocus)
                                                 : QueryMouseButtonToDo(in, value, false, checkFocus))
                    found = true;
                break;
            case INPUT_DEVICE_STICK:
                if (QueryJoystickButtonToDo(in, value, false, checkFocus))
                    found = true;
                break;
            case INPUT_DEVICE_STICK_POV:
                if (!checkFocus || in.gameFocusLost <= 0)
                {
                    int offset = value;
                    PoseidonAssert(offset >= 0);
                    PoseidonAssert(offset < N_JOYSTICK_POV);
                    if (in.gamepad.stickPovToDo[offset])
                        found = true;
                }
                break;
        }
    }
    return found;
}

static bool ProfileModifierHeld(const Input& in, InputCode modifier, bool checkFocus)
{
    if (!modifier.valid())
        return true;
    const int packed = modifier.toLegacy();
    const int value = InputBindingValue(packed);
    switch (InputBindingDevice(packed))
    {
        case INPUT_DEVICE_KEYBOARD:
            return QueryKey(in, value, checkFocus) > 0.0f;
        case INPUT_DEVICE_MOUSE:
            return QueryMouseButton(in, value, checkFocus) > 0.0f;
        case INPUT_DEVICE_STICK:
            return QueryJoystickButton(in, value, checkFocus) > 0.0f;
        case INPUT_DEVICE_STICK_AXIS:
            return value >= 0 && value < N_JOYSTICK_AXES && std::fabs(in.gamepad.stickAxis[value]) > 0.8f;
        case INPUT_DEVICE_STICK_POV:
            return value >= 0 && value < N_JOYSTICK_POV && in.gamepad.stickPov[value];
        default:
            return false;
    }
}

static float QueryProfileCode(const Input& in, InputCode code, bool checkFocus)
{
    if (!code.valid())
        return 0.0f;
    const int packed = code.toLegacy();
    const int value = InputBindingValue(packed);
    switch (InputBindingDevice(packed))
    {
        case INPUT_DEVICE_KEYBOARD:
            return InputBindingIsDoubleTap(packed) ? QueryDoubleTapKey(in, value, checkFocus)
                                                   : QueryKey(in, value, checkFocus);
        case INPUT_DEVICE_MOUSE:
            return InputBindingIsDoubleTap(packed) ? QueryDoubleTapMouseButton(in, value, checkFocus)
                                                   : QueryMouseButton(in, value, checkFocus);
        case INPUT_DEVICE_STICK:
            return QueryJoystickButton(in, value, checkFocus);
        case INPUT_DEVICE_STICK_AXIS:
            return value >= 0 && value < N_JOYSTICK_AXES ? in.gamepad.stickAxis[value] : 0.0f;
        case INPUT_DEVICE_STICK_POV:
            return value >= 0 && value < N_JOYSTICK_POV && in.gamepad.stickPov[value] ? 1.0f : 0.0f;
        default:
            return 0.0f;
    }
}

static bool QueryProfileCodeToDo(Input& in, InputCode code, bool reset, bool checkFocus)
{
    if (!code.valid())
        return false;
    const int packed = code.toLegacy();
    const int value = InputBindingValue(packed);
    switch (InputBindingDevice(packed))
    {
        case INPUT_DEVICE_KEYBOARD:
            return InputBindingIsDoubleTap(packed) ? QueryDoubleTapKeyToDo(in, value, reset, checkFocus)
                                                   : QueryKeyToDo(in, value, reset, checkFocus);
        case INPUT_DEVICE_MOUSE:
            return InputBindingIsDoubleTap(packed) ? QueryDoubleTapMouseButtonToDo(in, value, reset, checkFocus)
                                                   : QueryMouseButtonToDo(in, value, reset, checkFocus);
        case INPUT_DEVICE_STICK:
            return QueryJoystickButtonToDo(in, value, reset, checkFocus);
        case INPUT_DEVICE_STICK_POV:
            if (checkFocus && in.gameFocusLost > 0)
                return false;
            if (value < 0 || value >= N_JOYSTICK_POV)
                return false;
            {
                bool ret = in.gamepad.stickPovToDo[value];
                if (reset)
                    in.gamepad.stickPovToDo[value] = false;
                return ret;
            }
        default:
            return false;
    }
}

static float QueryProfileAction(const Input& in, const InputProfile& profile, UserAction action, bool checkFocus)
{
    float sum = 0.0f;
    for (const InputBinding& binding : profile.GetBindingEntries(action))
    {
        if (!ProfileModifierHeld(in, binding.modifier, checkFocus))
            continue;
        const float value = QueryProfileCode(in, binding.code, checkFocus) * binding.scale;
        switch (action)
        {
            case UAMoveForward:
            case UAMoveBack:
            case UATurnLeft:
            case UATurnRight:
            case UAMoveUp:
            case UAMoveDown:
            case UAMoveFastForward:
            case UAMoveSlowForward:
            case UAMoveLeft:
            case UAMoveRight:
            case UALookLeftDown:
            case UALookDown:
            case UALookRightDown:
            case UALookLeft:
            case UALookRight:
            case UALookLeftUp:
            case UALookUp:
            case UALookRightUp:
            case UAAimUp:
            case UAAimDown:
            case UAAimLeft:
            case UAAimRight:
                sum += std::max(0.0f, value);
                break;
            default:
                sum += value;
                break;
        }
    }
    return sum;
}

static bool QueryProfileActionToDo(Input& in,
                                   const InputProfile& profile,
                                   UserAction action,
                                   bool& actionDone,
                                   bool reset,
                                   bool checkFocus)
{
    if (actionDone && in.actionDone[action])
        return false;
    if (reset)
    {
        actionDone = true;
        in.actionDone[action] = true;
    }

    bool found = false;
    for (const InputBinding& binding : profile.GetBindingEntries(action))
    {
        if (!ProfileModifierHeld(in, binding.modifier, checkFocus))
            continue;
        if (QueryProfileCodeToDo(in, binding.code, false, checkFocus))
            found = true;
    }
    return found;
}

static bool ActionHasGamepadAxisBinding(const InputProfile& profile, UserAction action, int axis)
{
    for (const InputBinding& binding : profile.GetBindingEntries(action))
    {
        if (binding.code.device() == InputDevice::GamepadAxis && binding.code.code() == axis)
            return true;
    }
    return false;
}

InputSubsystem& InputSubsystem::Instance()
{
    static InputSubsystem instance;
    return instance;
}

InputSubsystem::InputSubsystem() = default;

void InputSubsystem::Update()
{
    ComputeMovementState();
    SyncToGInput();
}

void InputSubsystem::ComputeMovementState()
{
#if _ENABLE_CHEATS
    GInput.keyboard.cheat1 = false;
    GInput.keyboard.cheat2 = false;
    bool cheat1 = GetAction(UACheat1, false) > 0;
    bool cheat2 = GetAction(UACheat2, false) > 0;
    GInput.keyboard.cheat1 = cheat1;
    GInput.keyboard.cheat2 = cheat2;
#endif

    moveLeft_ = 0;
    moveRight_ = 0;
    moveUp_ = 0;
    moveDown_ = 0;
    moveForward_ = 0;
    moveFastForward_ = 0;
    moveSlowForward_ = 0;
    moveBack_ = 0;
    turnLeft_ = 0;
    turnRight_ = 0;
    fire_ = false;
    fireToDo_ = false;
    freelookChanged_ = false;

    for (int i = 0; i < UAN; i++)
    {
        GInput.actionDone[i] = false;
        for (auto& contextDone : actionDoneByContext_)
            contextDone[i] = false;
    }

    bool turbo = false;
#if _ENABLE_CHEATS
    if (!GInput.keyboard.cheat1 && !GInput.keyboard.cheat2)
#endif
    {
        if (GetAction(UATurbo, true))
            turbo = true;

        if (GetAction(UAFire, true) > 0)
            fire_ = true;

        if (GetActionToDo(UAFire, false, true))
            fireToDo_ = true;

        if (GetActionToDo(UALookAroundToggle, true, true))
            lookAroundToggled_ = !lookAroundToggled_;

        moveSlowForward_ = GetAction(UAMoveSlowForward, true);
        moveFastForward_ = GetAction(UAMoveFastForward, true);
        moveBack_ += GetAction(UAMoveBack, true);
        if (turbo)
            moveFastForward_ += GetAction(UAMoveForward, true);
        else
            moveForward_ += GetAction(UAMoveForward, true);

        turnLeft_ += GetAction(UATurnLeft, true);
        turnRight_ += GetAction(UATurnRight, true);

        bool oldLookAround = lookAroundEnabled_;
        if (GetAction(UALookAround, true))
            lookAroundEnabled_ = true;
        else
            lookAroundEnabled_ = lookAroundToggled_;

        if (oldLookAround != lookAroundEnabled_)
            freelookChanged_ = true;

        moveUp_ += GetAction(UAMoveUp, true);
        moveDown_ += GetAction(UAMoveDown, true);
        moveLeft_ += GetAction(UAMoveLeft, true);
        moveRight_ += GetAction(UAMoveRight, true);
    }

}

void InputSubsystem::SyncToGInput()
{
    GInput.keyMoveLeft = moveLeft_;
    GInput.keyMoveRight = moveRight_;
    GInput.keyMoveUp = moveUp_;
    GInput.keyMoveDown = moveDown_;
    GInput.keyMoveForward = moveForward_;
    GInput.keyMoveFastForward = moveFastForward_;
    GInput.keyMoveSlowForward = moveSlowForward_;
    GInput.keyMoveBack = moveBack_;
    GInput.keyTurnLeft = turnLeft_;
    GInput.keyTurnRight = turnRight_;
    GInput.fire = fire_;
    GInput.fireToDo = fireToDo_;
    GInput.lookAroundEnabled = lookAroundEnabled_;
    GInput.lookAroundToggleEnabled = lookAroundToggled_;
}

void InputSubsystem::LoadDefaultProfiles()
{
    ContextControlsConfig contextControls;
    contextControls.LoadDefaults();
    profiles_ = contextControls.profiles;
}

void InputSubsystem::SetContext(InputContext ctx)
{
    context_ = ctx;
}

float InputSubsystem::GetAction(UserAction action, bool checkFocus) const
{
    return GetAction(context_, action, checkFocus);
}

float InputSubsystem::GetAction(InputContext ctx, UserAction action, bool checkFocus) const
{
    const int idx = static_cast<int>(ctx);
    if (idx < 0 || idx >= kNumContexts)
        return 0.0f;

    const InputProfile& profile = profiles_[idx];
    float value = QueryProfileAction(GInput, profile, action, checkFocus);

    // Touch's virtual left stick should behave like keyboard forward/back
    // even in vehicle contexts whose physical gamepad defaults use triggers.
    // If the active profile already binds left-stick Y for this action
    // (infantry, custom bindings), QueryProfileAction has already included it.
    if (checkFocus && GInput.gameFocusLost > 0)
        return value;
    if ((action == UAMoveForward || action == UAMoveBack) && syntheticLeftStickY_ != 0.0f &&
        !ActionHasGamepadAxisBinding(profile, action, 1))
    {
        if (action == UAMoveForward)
            value += std::max(0.0f, -syntheticLeftStickY_);
        else
            value += std::max(0.0f, syntheticLeftStickY_);
    }
    if ((action == UATurnLeft || action == UATurnRight) && syntheticLeftStickX_ != 0.0f &&
        !ActionHasGamepadAxisBinding(profile, action, 0))
    {
        if (action == UATurnLeft)
            value += std::max(0.0f, -syntheticLeftStickX_);
        else
            value += std::max(0.0f, syntheticLeftStickX_);
    }

    return value;
}

bool InputSubsystem::GetActionToDo(UserAction action, bool reset, bool checkFocus)
{
    const int idx = static_cast<int>(context_);
    if (idx < 0 || idx >= kNumContexts)
        return false;
    return QueryProfileActionToDo(GInput, profiles_[idx], action, actionDoneByContext_[idx][action], reset, checkFocus);
}

bool InputSubsystem::IsKeyDown(SDL_Scancode sc) const
{
    int idx = static_cast<int>(sc);
    if (idx < 0 || idx >= SDL_SCANCODE_COUNT)
        return false;
    return GInput.keyboard.keys[idx] > 0.0f;
}

bool InputSubsystem::IsKeyPressed(SDL_Scancode sc) const
{
    int idx = static_cast<int>(sc);
    if (idx < 0 || idx >= SDL_SCANCODE_COUNT)
        return false;
    return GInput.keyboard.keysToDo[idx];
}

float InputSubsystem::GetKeyValue(SDL_Scancode sc) const
{
    int idx = static_cast<int>(sc);
    if (idx < 0 || idx >= SDL_SCANCODE_COUNT)
        return 0.0f;
    return GInput.keyboard.keys[idx];
}

bool InputSubsystem::ConsumeKeyPress(SDL_Scancode sc)
{
    int idx = static_cast<int>(sc);
    if (idx < 0 || idx >= SDL_SCANCODE_COUNT)
        return false;
    bool pressed = GInput.keyboard.keysToDo[idx];
    GInput.keyboard.keysToDo[idx] = false;
    return pressed;
}

bool InputSubsystem::ConsumeDevKeyPress(SDL_Scancode sc)
{
    // Always consume the edge — even when dev mode is off — so the press
    // doesn't linger and confuse a later frame.  But only report it when
    // --dev is set.
    int idx = static_cast<int>(sc);
    if (idx < 0 || idx >= SDL_SCANCODE_COUNT)
        return false;
    bool pressed = GInput.keyboard.keysToDo[idx];
    GInput.keyboard.keysToDo[idx] = false;
    return pressed && AppConfig::Instance().DevMode();
}

#if _ENABLE_CHEATS
bool InputSubsystem::GetCheat1ToDo(SDL_Scancode sc, bool reset)
{
    int dik = static_cast<int>(sc);
    if (!GInput.keyboard.cheat1 || GInput.keyboard.cheat2 || dik < 0)
        return false;
    bool ret = GInput.keyboard.keysToDo[dik];
    if (reset)
        GInput.keyboard.keysToDo[dik] = false;
    return ret;
}

bool InputSubsystem::GetCheat2ToDo(SDL_Scancode sc, bool reset)
{
    int dik = static_cast<int>(sc);
    if (GInput.keyboard.cheat1 || !GInput.keyboard.cheat2 || dik < 0)
        return false;
    bool ret = GInput.keyboard.keysToDo[dik];
    if (reset)
        GInput.keyboard.keysToDo[dik] = false;
    return ret;
}
#endif

bool InputSubsystem::IsMouseButtonDown(int button) const
{
    if (button < 0 || button >= N_MOUSE_BUTTONS)
        return false;
    return GInput.mouse.buttons[button] > 0.0f;
}

bool InputSubsystem::IsMouseLeftDown() const
{
    return GInput.mouse.left;
}
bool InputSubsystem::IsMouseRightDown() const
{
    return GInput.mouse.right;
}
bool InputSubsystem::IsMouseMiddleDown() const
{
    return GInput.mouse.middle;
}

float InputSubsystem::GetMouseDeltaX() const
{
    return GInput.mouse.deltaX;
}
float InputSubsystem::GetMouseDeltaY() const
{
    return GInput.mouse.deltaY;
}
float InputSubsystem::GetMouseWheel() const
{
    return GInput.mouse.deltaZ;
}

float InputSubsystem::GetMouseRawDeltaX() const
{
    return GInput.mouse.rawDeltaX;
}
float InputSubsystem::GetMouseRawDeltaY() const
{
    return GInput.mouse.rawDeltaY;
}

float InputSubsystem::GetCursorX() const
{
    return GInput.cursor.cursorX;
}
float InputSubsystem::GetCursorY() const
{
    return GInput.cursor.cursorY;
}

float InputSubsystem::GetGamepadAxis(int axis) const
{
    if (axis < 0 || axis >= N_JOYSTICK_AXES)
        return 0.0f;
    return GInput.gamepad.stickAxis[axis];
}

bool InputSubsystem::IsGamepadButtonDown(int button) const
{
    if (button < 0 || button >= N_JOYSTICK_BUTTONS)
        return false;
    return GInput.gamepad.stickButtons[button] > 0.0f;
}

bool InputSubsystem::IsGamepadButtonPressed(int button) const
{
    if (button < 0 || button >= N_JOYSTICK_BUTTONS)
        return false;
    return GInput.gamepad.stickButtonsToDo[button];
}

InputProfile& InputSubsystem::GetProfile(InputContext ctx)
{
    return profiles_[static_cast<int>(ctx)];
}

const InputProfile& InputSubsystem::GetProfile(InputContext ctx) const
{
    return profiles_[static_cast<int>(ctx)];
}

void InputSubsystem::ResetLookAroundToggle()
{
    lookAroundToggled_ = false;
    GInput.lookAroundToggleEnabled = false;
}

bool InputSubsystem::IsMouseTurnActive() const
{
    if (GInput.lookAroundEnabled)
        return false;
    if (GInput.keyboard.cursorLastActive > GInput.mouse.cursorLastActive)
        return false;
    if (GInput.keyboard.turnLastActive > GInput.mouse.turnLastActive)
        return false;
    if (GInput.gamepad.moveLastActive > GInput.mouse.turnLastActive)
        return false;
    return true;
}
bool InputSubsystem::IsMouseCursorActive() const
{
    return GInput.keyboard.cursorLastActive <= GInput.mouse.cursorLastActive;
}
bool InputSubsystem::IsJoystickActive() const
{
    if (!GInput.gamepad.enabled)
        return false;
    if (GInput.keyboard.moveLastActive >= GInput.gamepad.moveLastActive)
        return false;
    if (!GInput.lookAroundEnabled && GInput.mouse.cursorLastActive > GInput.gamepad.moveLastActive)
        return false;
    return true;
}
bool InputSubsystem::IsJoystickEnabled() const
{
    return GInput.gamepad.enabled;
}
bool InputSubsystem::IsJoystickThrustActive() const
{
    if (!GInput.gamepad.enabled)
        return false;
    return GInput.gamepad.thrustLastActive > GInput.keyboard.thrustLastActive;
}
bool InputSubsystem::IsKeyboardCursorMoreRecent() const
{
    return GInput.keyboard.cursorLastActive > GInput.mouse.cursorLastActive;
}
bool InputSubsystem::IsMouseCursorRecentlyActive() const
{
    return GInput.mouse.cursorLastActive >= Glob.uiTime - 1;
}

void InputSubsystem::MarkKeyboardMoveActive()
{
    GInput.keyboard.moveLastActive = Glob.uiTime;
}
void InputSubsystem::MarkKeyboardTurnActive()
{
    GInput.keyboard.turnLastActive = Glob.uiTime;
}
void InputSubsystem::MarkKeyboardCursorActive()
{
    GInput.keyboard.cursorLastActive = Glob.uiTime;
}
void InputSubsystem::MarkKeyboardThrustActive()
{
    GInput.keyboard.thrustLastActive = Glob.uiTime;
}
void InputSubsystem::MarkJoystickMoveActive()
{
    GInput.gamepad.moveLastActive = Glob.uiTime;
}
void InputSubsystem::MarkJoystickThrustActive()
{
    GInput.gamepad.thrustLastActive = Glob.uiTime;
}

bool InputSubsystem::IsActionBoundToRecentAxis(UserAction action) const
{
    const int idx = static_cast<int>(context_);
    if (idx < 0 || idx >= kNumContexts)
        return false;

    const auto& bindings = profiles_[idx].GetBindingEntries(action);
    for (const InputBinding& binding : bindings)
    {
        int key = binding.code.toLegacy();
        if ((key & INPUT_DEVICE_MASK) == INPUT_DEVICE_STICK_AXIS)
        {
            int axis = key - INPUT_DEVICE_STICK_AXIS;
            if (axis >= 0 && axis < N_JOYSTICK_AXES)
            {
                if (GInput.gamepad.jAxisLastActive[axis] >= Glob.uiTime - 0.1)
                    return true;
            }
        }
    }
    return false;
}

float InputSubsystem::GetStickForward() const
{
    float legacyAxis = -GetAction(UAAxisDive, true);
    if (std::fabs(legacyAxis) > 0.001f)
        return legacyAxis;
    return GetAction(UAMoveForward, true) - GetAction(UAMoveBack, true);
}
float InputSubsystem::GetStickLeft() const
{
    float legacyAxis = -GetAction(UAAxisTurn, true);
    if (std::fabs(legacyAxis) > 0.001f)
        return legacyAxis;
    return GetAction(UATurnLeft, true) - GetAction(UATurnRight, true);
}
float InputSubsystem::GetStickThrust() const
{
    return GetAction(UAAxisThrust, true);
}
float InputSubsystem::GetStickRudder() const
{
    return GetAction(UAAxisRudder, true);
}

void InputSubsystem::ChangeGameFocus(int add)
{
    GInput.gameFocusLost += add;
}

bool InputSubsystem::GetFire(bool checkFocus) const
{
    if (checkFocus && GInput.gameFocusLost > 0)
        return false;
    return GInput.fire;
}
bool InputSubsystem::GetFireToDo(bool reset, bool checkFocus)
{
    if (checkFocus && GInput.gameFocusLost > 0)
        return false;
    bool ret = GInput.fireToDo;
    if (reset)
        GInput.fireToDo = false;
    return ret;
}
bool InputSubsystem::GetMouseL(bool checkFocus)
{
    if (checkFocus && GInput.gameFocusLost > 0)
        return false;
    return GInput.mouse.left;
}
bool InputSubsystem::GetMouseR(bool checkFocus)
{
    if (checkFocus && GInput.gameFocusLost > 0)
        return false;
    return GInput.mouse.right;
}
bool InputSubsystem::GetMouseLToDo(bool reset, bool checkFocus)
{
    if (checkFocus && GInput.gameFocusLost > 0)
        return false;
    bool ret = GInput.mouse.leftToDo;
    if (reset)
        GInput.mouse.leftToDo = false;
    return ret;
}
bool InputSubsystem::GetMouseRToDo(bool reset, bool checkFocus)
{
    if (checkFocus && GInput.gameFocusLost > 0)
        return false;
    bool ret = GInput.mouse.rightToDo;
    if (reset)
        GInput.mouse.rightToDo = false;
    return ret;
}

bool InputSubsystem::IsMouseLeftPressed() const
{
    return GInput.mouse.leftToDo;
}
bool InputSubsystem::IsMouseRightPressed() const
{
    return GInput.mouse.rightToDo;
}

float InputSubsystem::ConsumeCursorDeltaX()
{
    float val = GInput.cursor.aimDeltaX;
    GInput.cursor.aimDeltaX = 0;
    return val;
}

float InputSubsystem::ConsumeCursorDeltaY()
{
    float val = GInput.cursor.aimDeltaY;
    GInput.cursor.aimDeltaY = 0;
    return val;
}

float InputSubsystem::ConsumeCursorScroll()
{
    float val = GInput.cursor.aimDeltaZ;
    GInput.cursor.aimDeltaZ = 0;
    return val;
}

void InputSubsystem::ResetCursorPosition()
{
    GInput.cursor.cursorX = 0;
    GInput.cursor.cursorY = 0;
}

void InputSubsystem::FlushAndResetMouse()
{
    GInput.mouse.FlushAndReset();
    GInput.cursor = {};
}

float InputSubsystem::GetKey(int packedKey, bool checkFocus) const
{
    const int value = InputBindingValue(packedKey);
    return InputBindingIsDoubleTap(packedKey) ? QueryDoubleTapKey(GInput, value, checkFocus)
                                             : QueryKey(GInput, value, checkFocus);
}

bool InputSubsystem::GetKeyToDo(int packedKey, bool reset, bool checkFocus)
{
    const int value = InputBindingValue(packedKey);
    return InputBindingIsDoubleTap(packedKey) ? QueryDoubleTapKeyToDo(GInput, value, reset, checkFocus)
                                             : QueryKeyToDo(GInput, value, reset, checkFocus);
}

int InputSubsystem::CheatActivated() const
{
    return GInput.keyboard.CheatActivated();
}
void InputSubsystem::CheatServed()
{
    GInput.keyboard.CheatServed();
}
void InputSubsystem::ForgetKeys()
{
    GInput.keyboard.ForgetKeys();

    for (int i = 0; i < N_JOYSTICK_AXES; i++)
    {
        GInput.gamepad.jAxisLastActive[i] = Glob.uiTime - 120;
        GInput.gamepad.jAxisBigLastActive[i] = Glob.uiTime - 120;
    }

    GInput.gamepad.moveLastActive = Glob.uiTime - 1;
    GInput.keyboard.moveLastActive = Glob.uiTime + 5;
    GInput.mouse.cursorLastActive = Glob.uiTime - 1;
    GInput.mouse.turnLastActive = Glob.uiTime - 1;
    GInput.keyboard.cursorLastActive = Glob.uiTime;
    GInput.keyboard.turnLastActive = Glob.uiTime;
    GInput.cursor.aimDeltaX = GInput.cursor.aimDeltaY = GInput.cursor.aimDeltaZ = 0;
}
void InputSubsystem::LoadKeys()
{
    // contextControls.cfg owns all action bindings.
    ContextControlsConfig contextControls;
    const std::string contextControlsPath = ContextControlsCfgPath();
    if (!contextControls.Load(contextControlsPath))
    {
        contextControls.LoadDefaults();
        contextControls.Save(contextControlsPath);
    }
    profiles_ = contextControls.profiles;

    // GInput.userKeys is no longer a persistence source. Keep it empty so
    // leftover controls.cfg/gamepad.cfg files cannot leak bindings into the
    // active context profile path.
    for (int i = 0; i < UAN; i++)
    {
        GInput.userKeys[i].Resize(0);
        GInput.userKeysModifiers[i].Resize(0);
    }

    // gamepad.cfg owns the four tuning scalars only.
    GamepadConfig gamepad;
    const std::string gamepadPath = GamepadCfgPath();
    if (!gamepad.Load(gamepadPath))
    {
        gamepad.LoadDefaults();
        gamepad.Save(gamepadPath);
    }
    gamepad.Normalize();

    // Mouse scalars — mouse.cfg.  Same eager-write-on-miss posture.
    MouseConfig mouse;
    const std::string mousePath = MouseCfgPath();
    if (!mouse.Load(mousePath))
    {
        mouse.LoadDefaults();
        mouse.Save(mousePath);
    }
    mouse.Normalize();
    GInput.mouse.reverseY = mouse.reverseY;
    GInput.mouse.buttonsReversed = mouse.buttonsReversed;
    GInput.mouse.sensitivityX = mouse.sensitivityX;
    GInput.mouse.sensitivityY = mouse.sensitivityY;

    GInput.gamepad.enabled = gamepad.enabled;
    GInput.gamepad.deadzoneStick = gamepad.deadzoneStick;
    GInput.gamepad.deadzoneTrigger = gamepad.deadzoneTrigger;
    GInput.gamepad.lookSensitivity = gamepad.lookSensitivity;

    // Touch scalars — touch.cfg. Runtime state lives in TouchInput.
    TouchConfig touch;
    const std::string touchPath = TouchCfgPath();
    if (!touch.Load(touchPath))
    {
        touch.LoadDefaults();
        touch.Save(touchPath);
    }
    touch.Normalize();
    TouchInput_SetAimSensitivity(touch.aimSensitivity);
    TouchInput_SetCursorSensitivity(touch.cursorSensitivity);
    TouchInput_SetDisplayMode(static_cast<TouchDisplayMode>(touch.displayMode));
}
void InputSubsystem::SaveKeys()
{
    ContextControlsConfig contextControls;
    contextControls.profiles = profiles_;
    contextControls.Save(ContextControlsCfgPath());

    GamepadConfig gamepad;
    gamepad.enabled = GInput.gamepad.enabled;
    gamepad.deadzoneStick = GInput.gamepad.deadzoneStick;
    gamepad.deadzoneTrigger = GInput.gamepad.deadzoneTrigger;
    gamepad.lookSensitivity = GInput.gamepad.lookSensitivity;
    gamepad.Save(GamepadCfgPath());

    MouseConfig mouse;
    mouse.reverseY = GInput.mouse.reverseY;
    mouse.buttonsReversed = GInput.mouse.buttonsReversed;
    mouse.sensitivityX = GInput.mouse.sensitivityX;
    mouse.sensitivityY = GInput.mouse.sensitivityY;
    mouse.Save(MouseCfgPath());

    TouchConfig touch;
    touch.aimSensitivity = TouchInput_GetAimSensitivity();
    touch.cursorSensitivity = TouchInput_GetCursorSensitivity();
    touch.displayMode = static_cast<int>(TouchInput_GetDisplayMode());
    touch.Save(TouchCfgPath());
}

UserAction InputSubsystem::FindBindingConflict(int packedCode, UserAction excludeAction, int excludeSlot,
                                               int modifier) const
{
    if (packedCode < 0)
        return UAN;
    const int contextIdx = static_cast<int>(context_);
    if (contextIdx < 0 || contextIdx >= kNumContexts)
        return UAN;

    const InputProfile& profile = profiles_[contextIdx];
    for (int i = 0; i < UAN; i++)
    {
        const auto& bindings = profile.GetBindingEntries(static_cast<UserAction>(i));
        for (int j = 0; j < static_cast<int>(bindings.size()); j++)
        {
            const InputBinding& binding = bindings[j];
            if (binding.code.toLegacy() != packedCode)
                continue;
            int slotMod = binding.modifier.valid() ? binding.modifier.toLegacy() : -1;
            // Conflict only when both code AND modifier match — so
            // "Ctrl+W" and bare "W" coexist as distinct bindings.
            if (slotMod != modifier)
                continue;
            if ((UserAction)i == excludeAction && (excludeSlot < 0 || j == excludeSlot))
                continue;
            return (UserAction)i;
        }
    }
    return UAN;
}

void InputSubsystem::ResetCategoryDefaults(ControlsCategory cat)
{
    UserActionDesc* descs = GetUserActionDesc();
    const UserAction* list = GetControlsCategoryActions(cat);
    ContextList contexts = ContextsForCategory(cat);
    for (int i = 0; list[i] != UAN; i++)
    {
        int idx = (int)list[i];
        if (idx < 0 || idx >= UAN)
            continue;

        std::vector<InputBinding> defaultBindings;
        const KeyList& defaultKeys = descs[idx].keys;
        for (int j = 0; j < defaultKeys.Size(); j++)
        {
            if (!GamepadConfig::IsGamepadCode(defaultKeys[j]))
                defaultBindings.push_back(InputBinding(InputCode::FromLegacy(defaultKeys[j])));
        }

        for (int c = 0; c < contexts.count; ++c)
        {
            InputProfile& profile = GetProfile(contexts.data[c]);
            profile.ClearBindings(static_cast<UserAction>(idx));
            for (const InputBinding& binding : defaultBindings)
                profile.Bind(static_cast<UserAction>(idx), binding);
        }
    }
}

UserActionDesc* InputSubsystem::GetUserActionDesc()
{
    static UserActionDesc userActionDesc[] = {
        UserActionDesc("MoveForward", IDS_USRACT_MOVE_FORWARD, SDL_SCANCODE_W, SDL_SCANCODE_UP, -1),
        UserActionDesc("MoveBack", IDS_USRACT_MOVE_BACK, SDL_SCANCODE_S, SDL_SCANCODE_DOWN, -1),
        UserActionDesc("TurnLeft", IDS_USRACT_TURN_LEFT, SDL_SCANCODE_A, SDL_SCANCODE_LEFT, -1),
        UserActionDesc("TurnRight", IDS_USRACT_TURN_RIGHT, SDL_SCANCODE_D, SDL_SCANCODE_RIGHT, -1),
        UserActionDesc("MoveUp", IDS_USRACT_MOVE_UP, SDL_SCANCODE_Q, SDL_SCANCODE_PAGEUP, -1),
        UserActionDesc("MoveDown", IDS_USRACT_MOVE_DOWN, SDL_SCANCODE_Z, SDL_SCANCODE_PAGEDOWN, -1),
        UserActionDesc("MoveFastForward", IDS_USRACT_FAST_FORWARD, SDL_SCANCODE_E, -1),
        UserActionDesc("MoveSlowForward", IDS_USRACT_SLOW_FORWARD, SDL_SCANCODE_Q, -1),
        UserActionDesc("MoveLeft", IDS_USRACT_MOVE_LEFT, SDL_SCANCODE_X, SDL_SCANCODE_DELETE, -1),
        UserActionDesc("MoveRight", IDS_USRACT_MOVE_RIGHT, SDL_SCANCODE_C, SDL_SCANCODE_END, -1),
        UserActionDesc("ToggleWeapons", IDS_USRACT_TOGGLE_WEAPONS, SDL_SCANCODE_SPACE, SDL_SCANCODE_RCTRL,
                       INPUT_DEVICE_STICK + 3, -1),
        UserActionDesc("Fire", IDS_USRACT_FIRE, SDL_SCANCODE_LCTRL, INPUT_DEVICE_STICK + 7, -1),
        UserActionDesc("ReloadMagazine", IDS_USRACT_RELOAD_MAGAZINE, SDL_SCANCODE_R, SDL_SCANCODE_HOME,
                       INPUT_DEVICE_STICK + 2, -1),
        UserActionDesc("LockTargets", IDS_USRACT_LOCK_TARGETS, SDL_SCANCODE_TAB, INPUT_DEVICE_STICK + 1, -1),
        UserActionDesc("LockTarget", IDS_USRACT_LOCK_OR_ZOOM, INPUT_DEVICE_MOUSE + 1, -1),
        UserActionDesc("RevealTarget", IDS_USRACT_REVEAL_TARGET, INPUT_DEVICE_MOUSE + 1, -1),
        UserActionDesc("PrevAction", IDS_USRACT_PREV_ACTION, SDL_SCANCODE_LEFTBRACKET, INPUT_DEVICE_STICK + 4,
                       INPUT_DEVICE_STICK_POV + 0, -1),
        UserActionDesc("NextAction", IDS_USRACT_NEXT_ACTION, SDL_SCANCODE_RIGHTBRACKET, INPUT_DEVICE_STICK + 5,
                       INPUT_DEVICE_STICK_POV + 4, -1),
        UserActionDesc("Action", IDS_USRACT_ACTION, SDL_SCANCODE_RETURN, INPUT_DEVICE_MOUSE + 2, INPUT_DEVICE_STICK + 0,
                       -1),
        UserActionDesc("Headlights", IDS_USRACT_HEADLIGHTS, SDL_SCANCODE_L, -1),
        UserActionDesc("NightVision", IDS_USRACT_NIGHT_VISION, SDL_SCANCODE_N, -1),
        UserActionDesc("Binocular", IDS_USRACT_BINOCULAR, SDL_SCANCODE_B, -1),
        UserActionDesc("Handgun", IDS_USRACT_HANDGUN, SDL_SCANCODE_Y, -1),
        UserActionDesc("Compass", IDS_USRACT_COMPASS, SDL_SCANCODE_G, INPUT_DEVICE_STICK_POV + 6, -1),
        UserActionDesc("Watch", IDS_USRACT_WATCH, SDL_SCANCODE_T, INPUT_DEVICE_STICK_POV + 2, -1),
        UserActionDesc("Map", IDS_USRACT_MAP, SDL_SCANCODE_M, INPUT_DEVICE_STICK + 8, -1),
        UserActionDesc("Help", IDS_USRACT_HELP, SDL_SCANCODE_H, INPUT_DEVICE_STICK + 9, -1),
        UserActionDesc("TimeInc", IDS_USRACT_TIME_INC, SDL_SCANCODE_EQUALS, -1),
        UserActionDesc("TimeDec", IDS_USRACT_TIME_DEC, SDL_SCANCODE_MINUS, -1),
        UserActionDesc("Optics", IDS_USRACT_OPTICS, SDL_SCANCODE_V, SDL_SCANCODE_KP_0, -1),
        UserActionDesc("PersonView", IDS_USRACT_PERSON_VIEW, SDL_SCANCODE_KP_ENTER, -1),
        UserActionDesc("TacticalView", IDS_USRACT_TACTICAL_VIEW, SDL_SCANCODE_KP_PERIOD, -1),
        UserActionDesc("ZoomIn", IDS_USRACT_ZOOM_IN, SDL_SCANCODE_KP_PLUS, -1),
        UserActionDesc("ZoomOut", IDS_USRACT_ZOOM_OUT, SDL_SCANCODE_KP_MINUS, -1),
        UserActionDesc("LookAround", IDS_USRACT_LOOK_ARROUND, SDL_SCANCODE_LALT, INPUT_DEVICE_STICK + 6, -1),
        UserActionDesc("LookAroundToggle", IDS_USRACT_LOOK_ARROUND_TOGGLE, SDL_SCANCODE_KP_MULTIPLY, -1),
        UserActionDesc("LookLeftDown", IDS_USRACT_LOOK_LEFT_DOWN, SDL_SCANCODE_KP_1, -1),
        UserActionDesc("LookDown", IDS_USRACT_LOOK_DOWN, SDL_SCANCODE_KP_2, -1),
        UserActionDesc("LookRightDown", IDS_USRACT_LOOK_RIGHT_DOWN, SDL_SCANCODE_KP_3, -1),
        UserActionDesc("LookLeft", IDS_USRACT_LOOK_LEFT, SDL_SCANCODE_KP_4, -1),
        UserActionDesc("LookCenter", IDS_USRACT_LOOK_CENTER, SDL_SCANCODE_KP_5, INPUT_DEVICE_STICK + 11, -1),
        UserActionDesc("LookRight", IDS_USRACT_LOOK_RIGHT, SDL_SCANCODE_KP_6, -1),
        UserActionDesc("LookLeftUp", IDS_USRACT_LOOK_LEFT_UP, SDL_SCANCODE_KP_7, -1),
        UserActionDesc("LookUp", IDS_USRACT_LOOK_UP, SDL_SCANCODE_KP_8, -1),
        UserActionDesc("LookRightUp", IDS_USRACT_LOOK_RIGHT_UP, SDL_SCANCODE_KP_9, -1),
        UserActionDesc("PrevChannel", IDS_USRACT_PREV_CHANNEL, SDL_SCANCODE_COMMA, -1),
        UserActionDesc("NextChannel", IDS_USRACT_NEXT_CHANNEL, SDL_SCANCODE_PERIOD, -1),
        UserActionDesc("Chat", IDS_USRACT_CHAT, SDL_SCANCODE_SLASH, -1),
        UserActionDesc("VoiceOverNet", IDS_USRACT_VOICE_OVER_NET, SDL_SCANCODE_CAPSLOCK, -1),
        UserActionDesc("NetworkStats", IDS_USRACT_NETWORK_STATS, SDL_SCANCODE_I, -1),
        UserActionDesc("NetworkPlayers", IDS_USRACT_NETWORK_PLAYERS, SDL_SCANCODE_P, -1),
        UserActionDesc("SelectAll", IDS_USRACT_SELECT_ALL, SDL_SCANCODE_GRAVE, -1),
        UserActionDesc("Turbo", IDS_USRACT_TURBO, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT, -1),
        UserActionDesc("Walk", IDS_USRACT_WALK, SDL_SCANCODE_F, -1),
        UserActionDesc(true, "AxisTurn", IDS_USRACT_TURN, INPUT_DEVICE_STICK_AXIS + 0, -1),
        UserActionDesc(true, "AxisDive", IDS_USRACT_ACCELERATE, INPUT_DEVICE_STICK_AXIS + 1, -1),
        UserActionDesc(true, "AxisRudder", IDS_USRACT_RUDDER, INPUT_DEVICE_STICK_AXIS + 5, -1),
        UserActionDesc(true, "AxisThrust", IDS_USRACT_THRUST, INPUT_DEVICE_STICK_AXIS + 2, INPUT_DEVICE_STICK_AXIS + 6,
                       -1),
        UserActionDesc("AimUp", IDS_USRACT_AIM_UP, -1),
        UserActionDesc("AimDown", IDS_USRACT_AIM_DOWN, -1),
        UserActionDesc("AimLeft", IDS_USRACT_AIM_LEFT, -1),
        UserActionDesc("AimRight", IDS_USRACT_AIM_RIGHT, -1),
#if _ENABLE_CHEATS
        UserActionDesc("Cheat1", IDS_USRACT_CHEAT_1, SDL_SCANCODE_RGUI, -1),
        UserActionDesc("Cheat2", IDS_USRACT_CHEAT_2, SDL_SCANCODE_RALT, -1),
#endif
    };
    return userActionDesc;
}

bool InputSubsystem::IsReverseMouse() const
{
    return GInput.mouse.reverseY;
}
void InputSubsystem::SetReverseMouse(bool v)
{
    GInput.mouse.reverseY = v;
}
void InputSubsystem::ToggleReverseMouse()
{
    GInput.mouse.reverseY = !GInput.mouse.reverseY;
}
void InputSubsystem::SetJoystickEnabled(bool v)
{
    GInput.gamepad.enabled = v;
}
void InputSubsystem::ToggleJoystickEnabled()
{
    GInput.gamepad.enabled = !GInput.gamepad.enabled;
}
bool InputSubsystem::IsMouseButtonsReversed() const
{
    return GInput.mouse.buttonsReversed;
}
void InputSubsystem::SetMouseButtonsReversed(bool v)
{
    GInput.mouse.buttonsReversed = v;
}
void InputSubsystem::ToggleMouseButtonsReversed()
{
    GInput.mouse.buttonsReversed = !GInput.mouse.buttonsReversed;
}
float InputSubsystem::GetMouseSensitivityX() const
{
    return GInput.mouse.sensitivityX;
}
float InputSubsystem::GetMouseSensitivityY() const
{
    return GInput.mouse.sensitivityY;
}
void InputSubsystem::SetMouseSensitivityX(float v)
{
    GInput.mouse.sensitivityX = v;
}
void InputSubsystem::SetMouseSensitivityY(float v)
{
    GInput.mouse.sensitivityY = v;
}

const AutoArray<int>& InputSubsystem::GetUserKeys(UserAction action) const
{
    return GInput.userKeys[action];
}
void InputSubsystem::SetUserKeys(UserAction action, const AutoArray<int>& keys)
{
    GInput.userKeys[action] = keys;
}
void InputSubsystem::CompactUserKeys(UserAction action)
{
    GInput.userKeys[action].Compact();
}
const AutoArray<int>& InputSubsystem::GetUserKeyModifiers(UserAction action) const
{
    return GInput.userKeysModifiers[action];
}
void InputSubsystem::SetUserKeyModifiers(UserAction action, const AutoArray<int>& modifiers)
{
    GInput.userKeysModifiers[action] = modifiers;
}

bool InputSubsystem::GetMouseButtonToDo(int i) const
{
    return GInput.mouse.buttonsToDo[i];
}
bool InputSubsystem::GetStickButtonToDo(int i) const
{
    return GInput.gamepad.stickButtonsToDo[i];
}
bool InputSubsystem::GetStickPovToDo(int i) const
{
    return GInput.gamepad.stickPovToDo[i];
}
void InputSubsystem::SetSyntheticStickButton(int i, bool value)
{
    if (i < 0 || i >= int(std::size(syntheticStickButtons_)))
        return;
    syntheticStickButtons_[i] = value;
}
void InputSubsystem::SetSyntheticStickPov(int i, bool value)
{
    if (i < 0 || i >= int(std::size(syntheticStickPov_)))
        return;
    syntheticStickPov_[i] = value;
}
void InputSubsystem::SetSyntheticLeftStick(float x, float y)
{
    syntheticLeftStickX_ = std::clamp(x, -1.0f, 1.0f);
    syntheticLeftStickY_ = std::clamp(y, -1.0f, 1.0f);

    const float values[2] = {syntheticLeftStickX_, syntheticLeftStickY_};
    for (int i = 0; i < 2; i++)
    {
        GInput.gamepad.stickAxis[i] = values[i];
        const int intAxis = static_cast<int>(values[i] * 100.0f);
        if (std::abs(intAxis - GInput.gamepad.jAxisBigLast[i]) > 80)
        {
            GInput.gamepad.jAxisBigLast[i] = intAxis;
            GInput.gamepad.jAxisBigLastActive[i] = Glob.uiTime;
        }
    }
}
bool InputSubsystem::HasSyntheticStickInput() const
{
    if (syntheticLeftStickX_ != 0.0f || syntheticLeftStickY_ != 0.0f)
        return true;
    for (bool value : syntheticStickButtons_)
    {
        if (value)
            return true;
    }
    for (bool value : syntheticStickPov_)
    {
        if (value)
            return true;
    }
    return false;
}
bool InputSubsystem::ConsumeSyntheticStickButton(int i)
{
    if (i < 0 || i >= int(std::size(syntheticStickButtons_)))
        return false;
    bool ret = syntheticStickButtons_[i];
    syntheticStickButtons_[i] = false;
    return ret;
}
bool InputSubsystem::ConsumeSyntheticStickPov(int i)
{
    if (i < 0 || i >= int(std::size(syntheticStickPov_)))
        return false;
    bool ret = syntheticStickPov_[i];
    syntheticStickPov_[i] = false;
    return ret;
}
bool InputSubsystem::GetSyntheticLeftStick(float& x, float& y) const
{
    x = syntheticLeftStickX_;
    y = syntheticLeftStickY_;
    return x != 0.0f || y != 0.0f;
}
bool InputSubsystem::ConsumeAxisBigActive(int i)
{
    if (GInput.gamepad.jAxisBigLastActive[i] > Glob.uiTime - 0.2)
    {
        GInput.gamepad.jAxisBigLastActive[i] = UITIME_MIN;
        return true;
    }
    return false;
}

#if _ENABLE_CHEATS
float InputSubsystem::GetCheat1(SDL_Scancode sc) const
{
    int dik = static_cast<int>(sc);
    if (!GInput.keyboard.cheat1 || GInput.keyboard.cheat2 || dik < 0)
        return 0;
    return GInput.keyboard.keys[dik];
}
float InputSubsystem::GetCheat2(SDL_Scancode sc) const
{
    int dik = static_cast<int>(sc);
    if (GInput.keyboard.cheat1 || !GInput.keyboard.cheat2 || dik < 0)
        return 0;
    return GInput.keyboard.keys[dik];
}
#endif

} // namespace Poseidon
