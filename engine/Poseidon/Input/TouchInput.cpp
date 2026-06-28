#include <Poseidon/Input/TouchInput.hpp>

#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Graphics/Textures/TextureBank.hpp>
#include <Poseidon/Input/ControllerUiInput.hpp>
#include <Poseidon/Input/ControllerUiScene.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Input/KeyInput.hpp>
#include <Poseidon/World/World.hpp>
#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <array>
#include <cmath>

extern void SDLInput_BufferControllerUiAction(Poseidon::ControllerUiAction action, bool menuFallback);
extern void SDLInput_BufferKeyEvent(SDL_Scancode sc, bool down, DWORD timestamp);
extern void SDLInput_BufferMouseButton(int btn, bool down);
extern void SDLInput_BufferMouseMotion(float dx, float dy);

namespace Poseidon
{

extern World* GWorld;
extern Input GInput;

namespace
{
constexpr int kMaxFingers = 10;
constexpr float kMoveRegionX = 0.45f;
constexpr float kLowerRegionY = 0.30f;
constexpr float kStickRadius = 0.115f;
constexpr float kBaseLookScaleX = 720.0f;
constexpr float kBaseLookScaleY = 520.0f;
constexpr float kCursorStickScaleX = 22.0f;
constexpr float kCursorStickScaleY = 22.0f;
constexpr float kMinSensitivity = 0.25f;
constexpr float kMaxSensitivity = 3.0f;
constexpr float kTapMaxTravel = 0.018f;

#ifdef POSEIDON_TARGET_IOS
constexpr bool kDefaultEnabled = true;
#else
constexpr bool kDefaultEnabled = false;
#endif

enum class FingerRole
{
    None,
    Move,
    Look,
    Button,
};

struct Finger
{
    bool active = false;
    SDL_FingerID id = 0;
    FingerRole role = FingerRole::None;
    TouchButton button = TouchButton::Count;
    float startX = 0.0f;
    float startY = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
    float lastX = 0.0f;
    float lastY = 0.0f;
    float maxTravel = 0.0f;
};

struct ButtonZone
{
    TouchButton button;
    float x;
    float y;
    float r;
};

struct PixelLayout
{
    float edgeMargin;
    float topMargin;
    float buttonRadius;
    float buttonGap;
};

std::array<Finger, kMaxFingers> sFingers;
bool sEnabled = kDefaultEnabled;
bool sButtonDown[(int)TouchButton::Count] = {};
bool sPrevButtonDown[(int)TouchButton::Count] = {};
float sMoveX = 0.0f;
float sMoveY = 0.0f;
float sLookDx = 0.0f;
float sLookDy = 0.0f;
float sMoveAnchorX = 0.18f;
float sMoveAnchorY = 0.74f;
float sMoveThumbX = 0.18f;
float sMoveThumbY = 0.74f;
float sLookX = 0.0f;
float sLookY = 0.0f;
int sViewportW = 1920;
int sViewportH = 1080;
float sAimSensitivity = 1.0f;
float sCursorSensitivity = 1.0f;

float Clamp01(float v)
{
    return std::clamp(v, 0.0f, 1.0f);
}

float ClampUnit(float v)
{
    return std::clamp(v, -1.0f, 1.0f);
}

float Length(float x, float y)
{
    return std::sqrt(x * x + y * y);
}

PixelLayout BuildPixelLayout(int width, int height)
{
    if (width <= 0)
        width = 1920;
    if (height <= 0)
        height = 1080;

    const float minDim = (float)std::min(width, height);
    PixelLayout layout;
    layout.edgeMargin = std::max(70.0f, minDim * 0.075f);
    layout.topMargin = std::max(54.0f, minDim * 0.055f);
    layout.buttonRadius = std::max(52.0f, minDim * 0.052f);
    layout.buttonGap = std::max(layout.buttonRadius * 1.95f, minDim * 0.105f);
    return layout;
}

float NormX(float pixelX, int width)
{
    return Clamp01(pixelX / (float)std::max(1, width));
}

float NormY(float pixelY, int height)
{
    return Clamp01(pixelY / (float)std::max(1, height));
}

bool IsGameplayScene()
{
    return GWorld && GWorld->GetControllerUiScene().kind == ControllerSceneKind::Gameplay;
}

float ClampSensitivity(float v)
{
    return std::clamp(v, kMinSensitivity, kMaxSensitivity);
}

void EmitPrimaryClick()
{
    SDLInput_BufferMouseButton(0, true);
    SDLInput_BufferMouseButton(0, false);
}

std::array<ButtonZone, (int)TouchButton::Count> BuildButtonZones(int width, int height)
{
    if (width <= 0)
        width = 1920;
    if (height <= 0)
        height = 1080;
    const PixelLayout layout = BuildPixelLayout(width, height);
    const float columnX = (float)width - layout.edgeMargin;
    const float innerX = columnX - layout.buttonGap * 0.95f;
    const float fireY = (float)height - layout.edgeMargin - layout.buttonRadius * 0.35f;
    const float actionY = fireY - layout.buttonGap * 0.88f;
    const float reloadY = fireY - layout.buttonGap * 1.78f;
    const float opticsY = fireY - layout.buttonGap * 2.68f;
    const float mapY = fireY - layout.buttonGap * 3.58f;
    const float radius = layout.buttonRadius / (float)height;
    return {{{TouchButton::Fire, NormX(columnX, width), NormY(fireY, height), radius * 1.18f},
             {TouchButton::Action, NormX(innerX, width), NormY(actionY, height), radius},
             {TouchButton::Reload, NormX(columnX, width), NormY(reloadY, height), radius},
             {TouchButton::Optics, NormX(innerX, width), NormY(opticsY, height), radius},
             {TouchButton::Map, NormX(columnX, width), NormY(mapY, height), radius},
             {TouchButton::Pause, NormX((float)width - layout.edgeMargin, width), NormY(layout.topMargin, height),
              radius * 0.72f}}};
}

TouchButton HitButton(float x, float y)
{
    for (const ButtonZone& zone : BuildButtonZones(sViewportW, sViewportH))
    {
        const float dx = (x - zone.x) * (float)std::max(1, sViewportW);
        const float dy = (y - zone.y) * (float)std::max(1, sViewportH);
        const float r = zone.r * (float)std::max(1, sViewportH);
        if (Length(dx, dy) <= r)
            return zone.button;
    }
    return TouchButton::Count;
}

Finger* FindFinger(SDL_FingerID id)
{
    for (Finger& finger : sFingers)
    {
        if (finger.active && finger.id == id)
            return &finger;
    }
    return nullptr;
}

Finger* AllocateFinger(SDL_FingerID id)
{
    for (Finger& finger : sFingers)
    {
        if (!finger.active)
        {
            finger = {};
            finger.active = true;
            finger.id = id;
            return &finger;
        }
    }
    return nullptr;
}

bool RoleActive(FingerRole role)
{
    for (const Finger& finger : sFingers)
    {
        if (finger.active && finger.role == role)
            return true;
    }
    return false;
}

void ClassifyFinger(Finger& finger)
{
    finger.button = HitButton(finger.x, finger.y);
    if (finger.button != TouchButton::Count)
    {
        finger.role = FingerRole::Button;
        return;
    }
    if (!RoleActive(FingerRole::Move) && finger.x <= kMoveRegionX && finger.y >= kLowerRegionY)
    {
        finger.role = FingerRole::Move;
        finger.startX = finger.x;
        finger.startY = finger.y;
        return;
    }
    if (!RoleActive(FingerRole::Look) && finger.x > kMoveRegionX)
    {
        finger.role = FingerRole::Look;
        return;
    }
    finger.role = FingerRole::None;
}

void EmitButtonEdge(TouchButton button, bool down)
{
    if (button == TouchButton::Count)
        return;

    switch (button)
    {
        case TouchButton::Fire:
            SDLInput_BufferMouseButton(0, down);
            break;
        case TouchButton::Action:
            InputSubsystem::Instance().SetSyntheticStickButton(0, down);
            if (down)
                SDLInput_BufferControllerUiAction(ControllerUiAction::Confirm, true);
            break;
        case TouchButton::Reload:
            SDLInput_BufferKeyEvent(SDL_SCANCODE_R, down, Foundation::GlobalTickCount());
            break;
        case TouchButton::Optics:
            SDLInput_BufferKeyEvent(SDL_SCANCODE_V, down, Foundation::GlobalTickCount());
            break;
        case TouchButton::Map:
            SDLInput_BufferKeyEvent(SDL_SCANCODE_M, down, Foundation::GlobalTickCount());
            break;
        case TouchButton::Pause:
            if (down)
                SDLInput_BufferControllerUiAction(IsGameplayScene() ? ControllerUiAction::Pause : ControllerUiAction::Cancel,
                                                  true);
            if (!IsGameplayScene())
                SDLInput_BufferKeyEvent(SDL_SCANCODE_ESCAPE, down, Foundation::GlobalTickCount());
            break;
        default:
            break;
    }
}

void EmitButtonHeld(TouchButton button)
{
    switch (button)
    {
        case TouchButton::Fire:
            break;
        case TouchButton::Action:
            InputSubsystem::Instance().SetSyntheticStickButton(0, true);
            break;
        default:
            break;
    }
}

void DrawRingApprox(Engine* engine, float cx, float cy, float rY, PackedColor color)
{
    const int w = engine->Width();
    const int h = engine->Height();
    const float rx = rY * (float)h / (float)std::max(1, w);
    constexpr int kSegments = 24;
    float lastX = cx + rx;
    float lastY = cy;
    for (int i = 1; i <= kSegments; i++)
    {
        const float a = (float)i * (6.28318530718f / (float)kSegments);
        const float x = cx + std::cos(a) * rx;
        const float y = cy + std::sin(a) * rY;
        engine->DrawLine(Line2DAbs(lastX * w, lastY * h, x * w, y * h), color, color);
        lastX = x;
        lastY = y;
    }
}

void DrawCircleApprox(Engine* engine, const MipInfo& white, float cx, float cy, float rY, PackedColor color)
{
    DrawRingApprox(engine, cx, cy, rY, color);
}

void DrawRectNorm(Engine* engine, const MipInfo& white, float cx, float cy, float w, float h, PackedColor color)
{
    const int sw = engine->Width();
    const int sh = engine->Height();
    engine->Draw2D(white, color,
                   Rect2DAbs((cx - w * 0.5f) * sw, (cy - h * 0.5f) * sh, std::max(1.0f, w * sw),
                             std::max(1.0f, h * sh)));
}

void DrawLineNorm(Engine* engine, float x0, float y0, float x1, float y1, PackedColor color)
{
    const int sw = engine->Width();
    const int sh = engine->Height();
    engine->DrawLine(Line2DAbs(x0 * sw, y0 * sh, x1 * sw, y1 * sh), color, color);
}

void DrawTouchIcon(Engine* engine, const MipInfo& white, TouchButton button, float cx, float cy, float r,
                   PackedColor color)
{
    const float aspect = (float)engine->Height() / (float)std::max(1, engine->Width());
    const float sx = r * aspect;
    const float sy = r;

    switch (button)
    {
        case TouchButton::Fire:
            DrawCircleApprox(engine, white, cx, cy, r * 0.34f, color);
            DrawLineNorm(engine, cx - sx * 0.52f, cy, cx - sx * 0.18f, cy, color);
            DrawLineNorm(engine, cx + sx * 0.18f, cy, cx + sx * 0.52f, cy, color);
            DrawLineNorm(engine, cx, cy - sy * 0.52f, cx, cy - sy * 0.18f, color);
            DrawLineNorm(engine, cx, cy + sy * 0.18f, cx, cy + sy * 0.52f, color);
            break;
        case TouchButton::Action:
            DrawLineNorm(engine, cx, cy - sy * 0.46f, cx + sx * 0.46f, cy, color);
            DrawLineNorm(engine, cx + sx * 0.46f, cy, cx, cy + sy * 0.46f, color);
            DrawLineNorm(engine, cx, cy + sy * 0.46f, cx - sx * 0.46f, cy, color);
            DrawLineNorm(engine, cx - sx * 0.46f, cy, cx, cy - sy * 0.46f, color);
            break;
        case TouchButton::Reload:
            DrawLineNorm(engine, cx - sx * 0.42f, cy - sy * 0.10f, cx + sx * 0.16f, cy - sy * 0.10f, color);
            DrawLineNorm(engine, cx + sx * 0.16f, cy - sy * 0.10f, cx + sx * 0.05f, cy - sy * 0.30f, color);
            DrawLineNorm(engine, cx + sx * 0.16f, cy - sy * 0.10f, cx + sx * 0.05f, cy + sy * 0.10f, color);
            DrawRectNorm(engine, white, cx - sx * 0.05f, cy + sy * 0.26f, sx * 0.50f, sy * 0.20f, color);
            break;
        case TouchButton::Optics:
            DrawCircleApprox(engine, white, cx, cy, r * 0.25f, color);
            DrawLineNorm(engine, cx - sx * 0.55f, cy, cx - sx * 0.25f, cy, color);
            DrawLineNorm(engine, cx + sx * 0.25f, cy, cx + sx * 0.55f, cy, color);
            break;
        case TouchButton::Map:
            DrawLineNorm(engine, cx - sx * 0.48f, cy - sy * 0.34f, cx + sx * 0.48f, cy - sy * 0.26f, color);
            DrawLineNorm(engine, cx + sx * 0.48f, cy - sy * 0.26f, cx + sx * 0.48f, cy + sy * 0.34f, color);
            DrawLineNorm(engine, cx + sx * 0.48f, cy + sy * 0.34f, cx - sx * 0.48f, cy + sy * 0.26f, color);
            DrawLineNorm(engine, cx - sx * 0.48f, cy + sy * 0.26f, cx - sx * 0.48f, cy - sy * 0.34f, color);
            DrawLineNorm(engine, cx - sx * 0.16f, cy - sy * 0.31f, cx - sx * 0.16f, cy + sy * 0.29f, color);
            DrawLineNorm(engine, cx + sx * 0.16f, cy - sy * 0.29f, cx + sx * 0.16f, cy + sy * 0.31f, color);
            break;
        case TouchButton::Pause:
            DrawRectNorm(engine, white, cx - sx * 0.16f, cy, sx * 0.12f, sy * 0.62f, color);
            DrawRectNorm(engine, white, cx + sx * 0.16f, cy, sx * 0.12f, sy * 0.62f, color);
            break;
        default:
            break;
    }
}

} // namespace

void TouchInput_HandleFingerEvent(const SDL_TouchFingerEvent& event)
{
    if (!sEnabled)
        return;

    const float x = Clamp01(event.x);
    const float y = Clamp01(event.y);
    const bool release = event.type == SDL_EVENT_FINGER_UP || event.type == SDL_EVENT_FINGER_CANCELED;

    Finger* finger = FindFinger(event.fingerID);
    if (!finger && !release)
        finger = AllocateFinger(event.fingerID);
    if (!finger)
        return;

    if (release)
    {
        if (finger->role == FingerRole::Look && finger->maxTravel <= kTapMaxTravel && IsGameplayScene())
            EmitPrimaryClick();
        *finger = {};
        return;
    }

    finger->lastX = finger->x;
    finger->lastY = finger->y;
    finger->x = x;
    finger->y = y;
    finger->maxTravel = std::max(finger->maxTravel, Length(finger->x - finger->startX, finger->y - finger->startY));
    if (event.type == SDL_EVENT_FINGER_DOWN)
    {
        finger->startX = x;
        finger->startY = y;
        finger->lastX = x;
        finger->lastY = y;
        finger->maxTravel = 0.0f;
        ClassifyFinger(*finger);
    }
}

void TouchInput_ProcessFrame(int viewportWidth, int viewportHeight)
{
    sViewportW = viewportWidth;
    sViewportH = viewportHeight;
    sMoveX = 0.0f;
    sMoveY = 0.0f;
    sLookDx = 0.0f;
    sLookDy = 0.0f;
    std::fill(std::begin(sButtonDown), std::end(sButtonDown), false);

    if (!sEnabled)
        return;

    bool moveActive = false;
    bool lookActive = false;
    for (const Finger& finger : sFingers)
    {
        if (!finger.active)
            continue;
        if (finger.role == FingerRole::Move)
        {
            const float dx = finger.x - finger.startX;
            const float dy = finger.y - finger.startY;
            const float len = Length(dx, dy);
            const float scale = len > kStickRadius ? kStickRadius / len : 1.0f;
            sMoveAnchorX = finger.startX;
            sMoveAnchorY = finger.startY;
            sMoveThumbX = finger.startX + dx * scale;
            sMoveThumbY = finger.startY + dy * scale;
            sMoveX = ClampUnit(dx / kStickRadius);
            sMoveY = ClampUnit(dy / kStickRadius);
            moveActive = true;
        }
        else if (finger.role == FingerRole::Look)
        {
            const bool gameplay = IsGameplayScene();
            const float sensitivity = gameplay ? sAimSensitivity : sCursorSensitivity;
            const float scaleX = gameplay ? kBaseLookScaleX : (float)std::max(1, sViewportW);
            const float scaleY = gameplay ? kBaseLookScaleY : (float)std::max(1, sViewportH);
            sLookDx += (finger.x - finger.lastX) * scaleX * sensitivity;
            sLookDy += (finger.y - finger.lastY) * scaleY * sensitivity;
            sLookX = finger.x;
            sLookY = finger.y;
            lookActive = true;
        }
        else if (finger.role == FingerRole::Button && finger.button != TouchButton::Count)
        {
            sButtonDown[(int)finger.button] = true;
        }
    }

    if (IsGameplayScene())
    {
        InputSubsystem::Instance().SetSyntheticLeftStick(sMoveX, sMoveY);
        if (moveActive)
            GInput.gamepad.moveLastActive = Glob.uiTime;
    }
    else
    {
        InputSubsystem::Instance().SetSyntheticLeftStick(0.0f, 0.0f);
        if (moveActive)
        {
            SDLInput_BufferMouseMotion(sMoveX * kCursorStickScaleX * sCursorSensitivity,
                                       sMoveY * kCursorStickScaleY * sCursorSensitivity);
            GInput.mouse.cursorLastActive = Glob.uiTime;
        }
    }

    if (lookActive)
    {
        SDLInput_BufferMouseMotion(sLookDx, sLookDy);
        GInput.mouse.cursorLastActive = Glob.uiTime;
        GInput.mouse.turnLastActive = Glob.uiTime;
    }

    for (int i = 0; i < (int)TouchButton::Count; i++)
    {
        if (sButtonDown[i] != sPrevButtonDown[i])
        {
            EmitButtonEdge((TouchButton)i, sButtonDown[i]);
            sPrevButtonDown[i] = sButtonDown[i];
        }
        if (sButtonDown[i])
            EmitButtonHeld((TouchButton)i);
    }
}

void TouchInput_DrawOverlay(Engine* engine)
{
    if (!sEnabled || !engine || !engine->TextBank())
        return;

    const MipInfo white = engine->TextBank()->UseMipmap(nullptr, 0, 0);
    const PackedColor base(Color(0.02f, 0.03f, 0.04f, 0.24f));
    const PackedColor active(Color(0.95f, 0.98f, 1.0f, 0.42f));
    const PackedColor pressed(Color(0.80f, 0.93f, 1.0f, 0.58f));

    if (std::fabs(sMoveX) > 0.001f || std::fabs(sMoveY) > 0.001f)
    {
        DrawCircleApprox(engine, white, sMoveAnchorX, sMoveAnchorY, kStickRadius, base);
        DrawCircleApprox(engine, white, sMoveThumbX, sMoveThumbY, kStickRadius * 0.42f, active);
    }
    if (std::fabs(sLookDx) > 0.001f || std::fabs(sLookDy) > 0.001f)
        DrawCircleApprox(engine, white, sLookX, sLookY, 0.035f, active);

    const int w = sViewportW > 0 ? sViewportW : engine->Width();
    const int h = sViewportH > 0 ? sViewportH : engine->Height();
    for (const ButtonZone& zone : BuildButtonZones(w, h))
    {
        DrawCircleApprox(engine, white, zone.x, zone.y, zone.r,
                         sButtonDown[(int)zone.button] ? pressed : base);
        DrawTouchIcon(engine, white, zone.button, zone.x, zone.y, zone.r * 0.82f,
                      sButtonDown[(int)zone.button] ? PackedColor(Color(0.05f, 0.08f, 0.10f, 0.78f)) : active);
    }
}

void TouchInput_SetEnabled(bool enabled)
{
    if (sEnabled == enabled)
        return;
    sEnabled = enabled;
    TouchInput_Reset();
}

bool TouchInput_IsEnabled()
{
    return sEnabled;
}

void TouchInput_SetAimSensitivity(float sensitivity)
{
    sAimSensitivity = ClampSensitivity(sensitivity);
}

float TouchInput_GetAimSensitivity()
{
    return sAimSensitivity;
}

void TouchInput_SetCursorSensitivity(float sensitivity)
{
    sCursorSensitivity = ClampSensitivity(sensitivity);
}

float TouchInput_GetCursorSensitivity()
{
    return sCursorSensitivity;
}

void TouchInput_Reset()
{
    sFingers = {};
    std::fill(std::begin(sButtonDown), std::end(sButtonDown), false);
    std::fill(std::begin(sPrevButtonDown), std::end(sPrevButtonDown), false);
    sMoveX = sMoveY = sLookDx = sLookDy = 0.0f;
    InputSubsystem::Instance().SetSyntheticLeftStick(0.0f, 0.0f);
}

TouchInputDebugState TouchInput_GetDebugState()
{
    TouchInputDebugState state;
    state.enabled = sEnabled;
    state.moveActive = RoleActive(FingerRole::Move);
    state.lookActive = RoleActive(FingerRole::Look);
    state.moveX = sMoveX;
    state.moveY = sMoveY;
    state.lookDx = sLookDx;
    state.lookDy = sLookDy;
    for (int i = 0; i < (int)TouchButton::Count; i++)
        state.buttons[i] = sButtonDown[i];
    return state;
}

} // namespace Poseidon
