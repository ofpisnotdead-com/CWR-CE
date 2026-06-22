#include <Poseidon/Input/KeyInput.hpp>
#include <Poseidon/Input/ControllerUiLayout.hpp>
#include <Poseidon/Input/ControllerUiScene.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_keycode.h>
#include <Poseidon/World/World.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>

#include <SDL3/SDL.h>

#include <Poseidon/UI/UITestEngine.hpp>
#include <Poseidon/Foundation/Strings/Mbcs.hpp>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/Memtype.h>

namespace Poseidon
{
extern Input GInput;
} // namespace Poseidon

using namespace Poseidon;

extern World* ::Poseidon::GWorld;
extern Engine* ::Poseidon::GEngine;

// UI key event dispatch — routes SDL key events to World::DoKeyDown/DoKeyUp

struct UIKeyEvent
{
    SDL_Keycode key;
    bool down;
};
static UIKeyEvent sUIKeyBuffer[64];
static int sUIKeyCount = 0;

struct UICharEvent
{
    unsigned ch;
};
static UICharEvent sUICharBuffer[64];
static int sUICharCount = 0;

struct UIControllerIntentEvent
{
    ControllerUiAction action;
    bool menuFallback;
};

static UIControllerIntentEvent sUIIntentBuffer[64];
static int sUIIntentCount = 0;
static bool sControllerPrimaryReleasePending = false;

void SDLInput_BufferMouseButton(int btn, bool down);

void SDLInput_BufferUIKeyEvent(SDL_Keycode key, bool down)
{
    if (key == SDLK_UNKNOWN)
        return;
    if (sUIKeyCount >= 64)
        return;
    sUIKeyBuffer[sUIKeyCount++] = {key, down};
    if (GUIRecorder)
        GUIRecorder->RecordKey((int)key, down);
}

void SDLInput_BufferControllerUiAction(ControllerUiAction action, bool menuFallback)
{
    if (sUIIntentCount >= 64)
        return;
    sUIIntentBuffer[sUIIntentCount++] = {action, menuFallback};
}

static void DispatchControllerUiDispatch(const ControllerUiDispatch& dispatch)
{
    switch (dispatch.kind)
    {
        case ControllerUiDispatchKind::KeyTap:
            if (sUIKeyCount + 2 <= 64)
            {
                sUIKeyBuffer[sUIKeyCount++] = {dispatch.key, true};
                sUIKeyBuffer[sUIKeyCount++] = {dispatch.key, false};
            }
            break;
        case ControllerUiDispatchKind::PointerPrimaryDown:
            SDLInput_BufferMouseButton(0, true);
            sControllerPrimaryReleasePending = false;
            break;
        case ControllerUiDispatchKind::PointerPrimaryUp:
            SDLInput_BufferMouseButton(0, false);
            sControllerPrimaryReleasePending = false;
            break;
        case ControllerUiDispatchKind::PointerPrimaryClick:
            SDLInput_BufferMouseButton(0, true);
            sControllerPrimaryReleasePending = true;
            break;
        default:
            break;
    }
}

static bool IsControllerPointerAction(ControllerUiAction action)
{
    return action == ControllerUiAction::PrimaryDown || action == ControllerUiAction::PrimaryUp ||
           action == ControllerUiAction::PrimaryClick;
}

void SDLInput_BufferUICharEvent(const char* text)
{
    for (const char* p = text; p && *p && sUICharCount < 64;)
    {
        unsigned codepoint = 0xFFFD;
        int consumed = DecodeUtf8Codepoint(p, &codepoint);
        if (consumed <= 0)
            break;
        sUICharBuffer[sUICharCount++] = {codepoint};
        p += consumed;
    }
}

void SDLInput_DispatchUIKeys()
{
    if (sControllerPrimaryReleasePending)
    {
        SDLInput_BufferMouseButton(0, false);
        sControllerPrimaryReleasePending = false;
    }

    if (!GWorld)
    {
        sUIIntentCount = 0;
        sUIKeyCount = 0;
        sUICharCount = 0;
        return;
    }

    ControllerMenuUiLayout menuLayout;
    ControllerEditorUiLayout editorLayout;
    for (int i = 0; i < sUIIntentCount; i++)
    {
        const UIControllerIntentEvent& event = sUIIntentBuffer[i];
        const ControllerUiScene scene = GWorld->GetControllerUiScene();
        if (!ControllerUiSceneAccepts(scene, event.action))
            continue;
        if (IsControllerPointerAction(event.action))
        {
            DispatchControllerUiDispatch(editorLayout.Map(event.action));
            continue;
        }
        if (GWorld->DoControllerUiAction(event.action) || !event.menuFallback)
            continue;

        if (scene.menuFallbackAllowed)
            DispatchControllerUiDispatch(menuLayout.Map(event.action));
    }
    sUIIntentCount = 0;

    for (int i = 0; i < sUIKeyCount; i++)
    {
        if (sUIKeyBuffer[i].down)
        {
            GWorld->DoKeyDown(sUIKeyBuffer[i].key, 1, 0);
        }
        else
        {
            GWorld->DoKeyUp(sUIKeyBuffer[i].key, 1, 0);
        }
    }
    sUIKeyCount = 0;

    for (int i = 0; i < sUICharCount; i++)
    {
        GWorld->DoChar(sUICharBuffer[i].ch, 1, 0);
    }
    sUICharCount = 0;
}

void SDLInput_BufferKeyEvent(SDL_Scancode sc, bool down, DWORD timestamp)
{
    GInput.keyboard.BufferKeyEvent((int)sc, down, timestamp);
}

void ProcessKeyboard_SDL(DWORD /*sysTime*/, DWORD timeDelta)
{
    // Use GlobalTickCount() so all timestamps share the same epoch.
    // (Event timestamps are in GlobalTickCount() space, not ::GetTickCount())
    DWORD sysTime = GlobalTickCount();
    GInput.keyboard.Update(sysTime, timeDelta, GWorld->IsUserInputEnabled());
}

void SDLInput_BufferMouseButton(int btn, bool down)
{
    GInput.mouse.BufferButton(btn, down);
    if (GUIRecorder)
        GUIRecorder->RecordMouseButton(btn, down, GInput.cursor.cursorX, GInput.cursor.cursorY);
}

void SDLInput_BufferMouseMotion(float dx, float dy)
{
    GInput.mouse.BufferMotion(dx, dy);
}

void SDLInput_BufferMouseWheel(float dy)
{
    GInput.mouse.BufferWheel(dy);
}

void ProcessMouse_SDL(DWORD timeDelta)
{
    if (!GWorld->IsUserInputEnabled())
    {
        GInput.mouse.DiscardBuffered();
        return;
    }

    // Compute cursor clamp from aspect settings
    MouseState::CursorClamp clamp;
    const MouseState::CursorClamp* clampPtr = nullptr;
    if (::Poseidon::GEngine)
    {
        AspectSettings as;
        ::Poseidon::GEngine->GetAspectSettings(as);
        float screenTopX = as.uiTopLeftX / (as.uiTopLeftX - as.uiBottomRightX);
        float screenTopY = as.uiTopLeftY / (as.uiTopLeftY - as.uiBottomRightY);
        float screenBotX = (1 - as.uiTopLeftX) / (as.uiBottomRightX - as.uiTopLeftX);
        float screenBotY = (1 - as.uiTopLeftY) / (as.uiBottomRightY - as.uiTopLeftY);
        clamp.minX = screenTopX * 2 - 1;
        clamp.maxX = screenBotX * 2 - 1;
        clamp.minY = screenTopY * 2 - 1;
        clamp.maxY = screenBotY * 2 - 1;
        clampPtr = &clamp;
    }

    bool markKeyboardTurn =
        GInput.mouse.Update(GInput.cursor, GInput.gameFocusLost, GInput.lookAroundEnabled, Glob.uiTime, clampPtr);

    if (markKeyboardTurn)
        GInput.keyboard.turnLastActive = Glob.uiTime;
}

static SDL_Gamepad* sGamepad = nullptr;

void SDLInput_GamepadAdded(SDL_JoystickID which)
{
    if (!sGamepad)
        sGamepad = SDL_OpenGamepad(which);
}

void SDLInput_GamepadRemoved(SDL_JoystickID which)
{
    if (!sGamepad || SDL_GetGamepadID(sGamepad) != which)
        return;
    SDL_CloseGamepad(sGamepad);
    sGamepad = nullptr;
    for (int i = 0; i < N_JOYSTICK_BUTTONS; i++)
    {
        GInput.gamepad.stickButtons[i] = 0;
        GInput.gamepad.stickButtonsToDo[i] = false;
    }
    for (int i = 0; i < N_JOYSTICK_AXES; i++)
        GInput.gamepad.stickAxis[i] = 0;
    for (int i = 0; i < N_JOYSTICK_POV; i++)
    {
        GInput.gamepad.stickPov[i] = false;
        GInput.gamepad.stickPovToDo[i] = false;
        GInput.gamepad.stickPovOld[i] = false;
    }
}

// SDLRumble — 3-source model (engine continuous + weapon ramp)

struct SDLRumble
{
    float engineMag = 0;
    float weaponBegMag = 0, weaponEndMag = 0;
    uint32_t weaponStart = 0, weaponEnd = 0;

    void SetEngine(float mag) { engineMag = fabsf(mag); }

    void PlayRamp(float beg, float end, float duration)
    {
        weaponBegMag = fabsf(beg);
        weaponEndMag = fabsf(end);
        weaponStart = SDL_GetTicks();
        weaponEnd = weaponStart + (uint32_t)(duration * 1000.f);
    }

    void Update(SDL_Gamepad* g)
    {
        if (!g)
            return;
        float weaponMag = 0;
        uint32_t now = SDL_GetTicks();
        if (now < weaponEnd)
        {
            float t = (float)(now - weaponStart) / (float)(weaponEnd - weaponStart);
            weaponMag = weaponBegMag + (weaponEndMag - weaponBegMag) * t;
        }
        // Left motor = low freq (engine feel), right = high freq (weapon snap)
        float left = fmaxf(engineMag * 0.7f, weaponMag * 0.5f);
        float right = fmaxf(engineMag * 0.3f, weaponMag * 1.0f);
        // Min thresholds from Xbox version: ignore < 10%/5%, clamp up to 20%/10%
        if (left < 0.10f)
            left = 0;
        else if (left < 0.20f)
            left = 0.20f;
        if (right < 0.05f)
            right = 0;
        else if (right < 0.10f)
            right = 0.10f;
        left = fminf(left, 1.0f);
        right = fminf(right, 1.0f);
        SDL_RumbleGamepad(g, (uint16_t)(left * 65535.f), (uint16_t)(right * 65535.f), 50);
    }
};
static SDLRumble sRumble;

void SDLGamepad_SetEngine(float mag)
{
    sRumble.SetEngine(mag);
}
void SDLGamepad_PlayRamp(float beg, float end, float dur)
{
    sRumble.PlayRamp(beg, end, dur);
}

static float ApplyDeadzone(float val, float dz)
{
    if (fabsf(val) < dz)
        return 0.0f;
    return (val - (val > 0 ? dz : -dz)) / (1.0f - dz);
}

// Expands circular stick range to full square (prevents diagonal under-reach)
static void CircleToSquare(float& x, float& y)
{
    float x2 = x * x, y2 = y * y;
    float d = 1.0f;
    if (x2 < y2 && y2 > 0.0f)
        d = sqrtf(1.0f + x2 / y2);
    else if (x2 > 0.0f)
        d = sqrtf(1.0f + y2 / x2);
    x = fmaxf(-1.0f, fminf(1.0f, x * d));
    y = fmaxf(-1.0f, fminf(1.0f, y * d));
}

static inline float saturatef(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static ControllerEditorUiLayout sControllerEditorUiLayout;

static void BufferControllerEditorDirection(bool up, bool down, bool left, bool right)
{
    const int count = (up ? 1 : 0) + (down ? 1 : 0) + (left ? 1 : 0) + (right ? 1 : 0);
    if (count != 1)
    {
        sControllerEditorUiLayout.ResetDirections();
        return;
    }

    const Foundation::UITime now = Glob.uiTime;
    struct Direction
    {
        bool pressed;
        ControllerUiAction action;
    };
    const Direction directions[] = {{up, ControllerUiAction::NavigateUp},
                                    {down, ControllerUiAction::NavigateDown},
                                    {left, ControllerUiAction::NavigateLeft},
                                    {right, ControllerUiAction::NavigateRight}};

    for (const Direction& direction : directions)
    {
        if (sControllerEditorUiLayout.ShouldEmitDirection(direction.action, direction.pressed, now))
            SDLInput_BufferControllerUiAction(direction.action, false);
    }
}

static void BufferControllerFaceAndShoulderIntents(bool suppressConfirm)
{
    if (!suppressConfirm && GInput.gamepad.stickButtonsToDo[0])
        SDLInput_BufferControllerUiAction(ControllerUiAction::Confirm, true);
    if (GInput.gamepad.stickButtonsToDo[1])
        SDLInput_BufferControllerUiAction(ControllerUiAction::Cancel, true);
    if (GInput.gamepad.stickButtonsToDo[2])
        SDLInput_BufferControllerUiAction(ControllerUiAction::Preview, false);
    if (GInput.gamepad.stickButtonsToDo[3])
        SDLInput_BufferControllerUiAction(ControllerUiAction::Delete, false);
    if (GInput.gamepad.stickButtonsToDo[4])
        SDLInput_BufferControllerUiAction(ControllerUiAction::PreviousTab, false);
    if (GInput.gamepad.stickButtonsToDo[5])
        SDLInput_BufferControllerUiAction(ControllerUiAction::NextTab, false);
    if (GInput.gamepad.stickButtonsToDo[6])
        SDLInput_BufferControllerUiAction(ControllerUiAction::PagePrevious, true);
    if (GInput.gamepad.stickButtonsToDo[7])
        SDLInput_BufferControllerUiAction(ControllerUiAction::PageNext, true);
    if (GInput.gamepad.stickButtonsToDo[9])
        SDLInput_BufferControllerUiAction(ControllerUiAction::Pause, false);
}

// ProcessJoystick — called from GameLoop after ProcessKeyboard

void ProcessJoystick_SDL()
{
    for (int i = 0; i < N_JOYSTICK_BUTTONS; i++)
    {
        GInput.gamepad.stickButtons[i] = 0;
        GInput.gamepad.stickButtonsToDo[i] = false;
    }
    for (int i = 0; i < N_JOYSTICK_POV; i++)
    {
        GInput.gamepad.stickPov[i] = false;
        GInput.gamepad.stickPovToDo[i] = false;
    }
    for (int i = 0; i < N_JOYSTICK_AXES; i++)
        GInput.gamepad.stickAxis[i] = 0;

    if (!sGamepad)
    {
        // Graphics backends call SDL_Init(SDL_INIT_VIDEO) only, so the
        // gamepad subsystem isn't running by default — without this
        // SDL_HasGamepad always returns false and the pad is invisible
        // even when physically connected.  Init once, lazily.
        if (!SDL_WasInit(SDL_INIT_GAMEPAD))
            SDL_InitSubSystem(SDL_INIT_GAMEPAD);
        // Auto-open first available gamepad if not yet opened
        if (SDL_HasGamepad())
        {
            int count = 0;
            SDL_JoystickID* ids = SDL_GetGamepads(&count);
            if (ids && count > 0)
                sGamepad = SDL_OpenGamepad(ids[0]);
            SDL_free(ids);
        }
    }

    float syntheticLx = 0.0f;
    float syntheticLy = 0.0f;
    const bool hasSyntheticLeftStick = InputSubsystem::Instance().GetSyntheticLeftStick(syntheticLx, syntheticLy);

    if ((!sGamepad && !hasSyntheticLeftStick) || !GWorld->IsUserInputEnabled())
    {
        sControllerEditorUiLayout.ResetDirections();
        sRumble.Update(sGamepad);
        return;
    }

    const float dzStick = GInput.gamepad.deadzoneStick;
    const float dzTrigger = GInput.gamepad.deadzoneTrigger;

    // ---- Left stick → vehicle analog axes + infantry WASD injection ----
    float lx =
        sGamepad ? ApplyDeadzone(SDL_GetGamepadAxis(sGamepad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f, dzStick) : 0.0f;
    float ly =
        sGamepad ? ApplyDeadzone(SDL_GetGamepadAxis(sGamepad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f, dzStick) : 0.0f;
    if (hasSyntheticLeftStick)
    {
        lx = syntheticLx;
        ly = syntheticLy;
    }
    CircleToSquare(lx, ly);

    GInput.gamepad.stickAxis[0] = lx; // UAAxisTurn (vehicle yaw)
    GInput.gamepad.stickAxis[1] = ly; // UAAxisDive (vehicle pitch)

    if (fabsf(lx) > 0.1f || fabsf(ly) > 0.1f)
        GInput.gamepad.moveLastActive = Glob.uiTime;

    // ---- Right stick → camera look (direct cursorMoved injection) ----
    float rx =
        sGamepad ? ApplyDeadzone(SDL_GetGamepadAxis(sGamepad, SDL_GAMEPAD_AXIS_RIGHTX) / 32767.0f, dzStick) : 0.0f;
    float ry =
        sGamepad ? ApplyDeadzone(SDL_GetGamepadAxis(sGamepad, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.0f, dzStick) : 0.0f;
    CircleToSquare(rx, ry);

    GInput.gamepad.stickAxis[3] = rx;
    GInput.gamepad.stickAxis[4] = ry;

    const bool freelookModifier = sGamepad && SDL_GetGamepadButton(sGamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) != 0;
    if (!freelookModifier && (fabsf(rx) > 0.01f || fabsf(ry) > 0.01f))
    {
        const float lookScale = 0.2f * GInput.gamepad.lookSensitivity;
        float moveX = saturatef(rx * lookScale, -0.6f, 0.6f);
        float moveY = saturatef(ry * lookScale, -0.4f, 0.4f);
        if (GInput.gameFocusLost <= 0)
        {
            GInput.cursor.aimDeltaX += moveX;
            GInput.cursor.aimDeltaY += GInput.mouse.reverseY ? -moveY : moveY;
        }
        GInput.cursor.cursorX += moveX;
        GInput.cursor.cursorY += moveY;
        GInput.mouse.cursorLastActive = Glob.uiTime;
        GInput.mouse.turnLastActive = Glob.uiTime;
    }

    // ---- Triggers → stickAxis (analog, 0..+1 unipolar) ----
    float rt = sGamepad ? SDL_GetGamepadAxis(sGamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) / 32767.0f : 0.0f;
    float lt = sGamepad ? SDL_GetGamepadAxis(sGamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) / 32767.0f : 0.0f;
    GInput.gamepad.stickAxis[2] = rt; // UAAxisThrust
    GInput.gamepad.stickAxis[5] = lt; // UAAxisRudder

    // ---- Buttons [0..11] with edge detection ----
    // [0]=A  [1]=B  [2]=X  [3]=Y  [4]=LB  [5]=RB
    // [6]=LT threshold  [7]=RT threshold
    // [8]=Back  [9]=Start  [10]=LThumb  [11]=RThumb
    static bool sPrev[N_JOYSTICK_BUTTONS] = {};
    const SDL_GamepadButton kButtons[12] = {
        SDL_GAMEPAD_BUTTON_SOUTH,          // [0] A
        SDL_GAMEPAD_BUTTON_EAST,           // [1] B
        SDL_GAMEPAD_BUTTON_WEST,           // [2] X
        SDL_GAMEPAD_BUTTON_NORTH,          // [3] Y
        SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,  // [4] LB
        SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, // [5] RB
        SDL_GAMEPAD_BUTTON_INVALID,        // [6] LT — threshold-based, see below
        SDL_GAMEPAD_BUTTON_INVALID,        // [7] RT — threshold-based
        SDL_GAMEPAD_BUTTON_BACK,           // [8] Back
        SDL_GAMEPAD_BUTTON_START,          // [9] Start
        SDL_GAMEPAD_BUTTON_LEFT_STICK,     // [10] LThumb
        SDL_GAMEPAD_BUTTON_RIGHT_STICK,    // [11] RThumb
    };
    for (int i = 0; i < N_JOYSTICK_BUTTONS; i++)
    {
        bool pressed;
        if (i == 6)
            pressed = lt > dzTrigger;
        else if (i == 7)
            pressed = rt > dzTrigger;
        else if (sGamepad)
            pressed = SDL_GetGamepadButton(sGamepad, kButtons[i]) != 0;
        else
            pressed = false;

        GInput.gamepad.stickButtons[i] = pressed ? 1.0f : 0.0f;
        GInput.gamepad.stickButtonsToDo[i] = pressed && !sPrev[i];
        sPrev[i] = pressed;
    }

    // ---- D-pad → 8-way stickPov ----
    // 0=N 1=NE 2=E 3=SE 4=S 5=SW 6=W 7=NW
    bool dpUp = sGamepad && SDL_GetGamepadButton(sGamepad, SDL_GAMEPAD_BUTTON_DPAD_UP) != 0;
    bool dpDown = sGamepad && SDL_GetGamepadButton(sGamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN) != 0;
    bool dpLeft = sGamepad && SDL_GetGamepadButton(sGamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT) != 0;
    bool dpRight = sGamepad && SDL_GetGamepadButton(sGamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) != 0;
    int pov8 = -1;
    if (dpUp && dpRight)
        pov8 = 1;
    else if (dpRight && dpDown)
        pov8 = 3;
    else if (dpDown && dpLeft)
        pov8 = 5;
    else if (dpLeft && dpUp)
        pov8 = 7;
    else if (dpUp)
        pov8 = 0;
    else if (dpRight)
        pov8 = 2;
    else if (dpDown)
        pov8 = 4;
    else if (dpLeft)
        pov8 = 6;

    for (int i = 0; i < N_JOYSTICK_POV; i++)
    {
        GInput.gamepad.stickPov[i] = (pov8 == i);
        GInput.gamepad.stickPovToDo[i] = GInput.gamepad.stickPov[i] && !GInput.gamepad.stickPovOld[i];
        GInput.gamepad.stickPovOld[i] = GInput.gamepad.stickPov[i];
    }

    // ---- jAxisLast hysteresis (same thresholds as DI version: 20/80 in -100..100) ----
    const int jTHold = 20;
    const int jBigTHold = 80;
    for (int i = 0; i < N_JOYSTICK_AXES; i++)
    {
        int intAxis = (int)(GInput.gamepad.stickAxis[i] * 100.f);
        if (abs(intAxis - GInput.gamepad.jAxisLast[i]) > jTHold)
        {
            GInput.gamepad.jAxisLast[i] = intAxis;
            GInput.gamepad.jAxisLastActive[i] = Glob.uiTime;
        }
        if (abs(intAxis - GInput.gamepad.jAxisBigLast[i]) > jBigTHold)
        {
            GInput.gamepad.jAxisBigLast[i] = intAxis;
            GInput.gamepad.jAxisBigLastActive[i] = Glob.uiTime;
        }
    }

    if (GInput.gamepad.stickPovToDo[0])
        SDLInput_BufferControllerUiAction(ControllerUiAction::NavigateUp, true);
    if (GInput.gamepad.stickPovToDo[4])
        SDLInput_BufferControllerUiAction(ControllerUiAction::NavigateDown, true);
    if (GInput.gamepad.stickPovToDo[6])
        SDLInput_BufferControllerUiAction(ControllerUiAction::NavigateLeft, true);
    if (GInput.gamepad.stickPovToDo[2])
        SDLInput_BufferControllerUiAction(ControllerUiAction::NavigateRight, true);

    // Left stick is editor-only UI navigation. Main menus keep their old D-pad/button path.
    constexpr float kLeftStickEditorThreshold = 0.50f;
    const bool stickUp = ly < -kLeftStickEditorThreshold;
    const bool stickDown = ly > kLeftStickEditorThreshold;
    const bool stickLeft = lx < -kLeftStickEditorThreshold;
    const bool stickRight = lx > kLeftStickEditorThreshold;
    const bool editorUiActive = GWorld->IsEditorControllerUiActive();
    static bool sEditorControllerMouseLeft = false;
    const bool controllerMouseLeft = GInput.gamepad.stickButtons[0] > 0.0f;
    if (editorUiActive && controllerMouseLeft != sEditorControllerMouseLeft)
    {
        sEditorControllerMouseLeft = controllerMouseLeft;
        SDLInput_BufferControllerUiAction(
            sEditorControllerMouseLeft ? ControllerUiAction::PrimaryDown : ControllerUiAction::PrimaryUp, false);
    }
    else if (!editorUiActive && sEditorControllerMouseLeft)
    {
        sEditorControllerMouseLeft = false;
        SDLInput_BufferControllerUiAction(ControllerUiAction::PrimaryUp, false);
    }

    if (editorUiActive)
        BufferControllerEditorDirection(stickUp, stickDown, stickLeft, stickRight);
    else
        sControllerEditorUiLayout.ResetDirections();
    BufferControllerFaceAndShoulderIntents(editorUiActive);

    sRumble.Update(sGamepad);
}

static bool SkipKeys = false;

void SetSkipKeys_SDL(bool skip)
{
    SkipKeys = skip;
}
