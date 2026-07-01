#include <Poseidon/Input/TouchInput.hpp>

#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Core/Profile/DifficultyTypes.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Graphics/Textures/TextureBank.hpp>
#include <Poseidon/Input/ControllerUiInput.hpp>
#include <Poseidon/Input/ControllerUiScene.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Input/KeyInput.hpp>
#include <Poseidon/AI/AIUnit.hpp>
#include <Poseidon/AI/EntityAI.hpp>
#include <Poseidon/World/Entities/Infantry/Person.hpp>
#include <Poseidon/World/Entities/Weapons/Weapons.hpp>
#include <Poseidon/World/World.hpp>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_video.h>

#include <algorithm>
#include <array>
#include <cmath>

extern void SDLInput_BufferControllerUiAction(Poseidon::ControllerUiAction action, bool menuFallback);
extern void SDLInput_BufferKeyEvent(SDL_Scancode sc, bool down, DWORD timestamp);
extern void SDLInput_BufferMouseButton(int btn, bool down);
extern void SDLInput_BufferMouseMotion(float dx, float dy);
extern void SDLInput_BufferMouseWheel(float dy);

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
constexpr float kAimFocusDwellSeconds = 0.22f;
constexpr float kAimFocusCalmPixels = 4.0f;
constexpr float kAimFocusReleasePixels = 10.0f;
constexpr float kTouchStrafeSnapRatio = 0.27f;
constexpr float kMinSensitivity = 0.25f;
constexpr float kMaxSensitivity = 3.0f;
constexpr float kTapMaxTravel = 0.018f;
constexpr float kTapMaxSeconds = 0.30f;
constexpr float kMapPanMouseScaleX = 200.0f / 1.5f;
constexpr float kMapPanMouseScaleY = 150.0f / 1.5f;
constexpr float kMapPinchZoomScale = 5.0f;
constexpr float kActionScrollStepY = 0.035f;
constexpr float kActionScrollWheelTicks = 1.0f;
constexpr float kEquipmentRadialCenterDeg = 320.0f;
constexpr float kEquipmentRadialMinSpanDeg = 58.0f;
constexpr float kEquipmentRadialSpanStepDeg = 23.0f;
constexpr float kEquipmentRadialMinDistance = 1.65f;
constexpr float kEquipmentRadialDistanceStep = 0.36f;

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

enum class EquipmentItem
{
    None,
    Map,
    Compass,
    Watch,
    Binocular,
    NightVision,
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
    Foundation::UITime startTime;
};

struct ButtonZone
{
    TouchButton button;
    float x;
    float y;
    float r;
};

struct EquipmentZone
{
    EquipmentItem item = EquipmentItem::None;
    float x = 0.0f;
    float y = 0.0f;
    float r = 0.0f;
};

struct PixelLayout
{
    float edgeMargin;
    float topMargin;
    float buttonRadius;
    float buttonGap;
};

struct SafeArea
{
    float left = 0.0f;
    float top = 0.0f;
    float right = 1.0f;
    float bottom = 1.0f;
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
bool sAimFocusActive = false;
bool sAimFocusCalm = false;
Foundation::UITime sAimFocusCalmSince;
bool sMapGestureActive = false;
bool sMapPrimaryActive = false;
SDL_FingerID sMapPrimaryFingerId = 0;
float sMapPanX = 0.0f;
float sMapPanY = 0.0f;
float sMapZoom = 0.0f;
bool sActionScrollActive = false;
SDL_FingerID sActionScrollFingerId = 0;
float sActionScrollAccumY = 0.0f;
int sActionScrollSteps = 0;
bool sActionTapPending = false;
SDL_FingerID sActionTapFingerId = 0;
bool sEquipmentRadialActive = false;
SDL_FingerID sEquipmentFingerId = 0;
EquipmentItem sEquipmentHover = EquipmentItem::None;
EquipmentItem sLatchedEquipment = EquipmentItem::None;
int sViewportW = 1920;
int sViewportH = 1080;
float sAimSensitivity = 1.0f;
float sCursorSensitivity = 1.0f;

bool sMapSceneOverrideEnabled = false;
bool sMapSceneOverride = false;
bool sEditorMapSceneOverrideEnabled = false;
bool sEditorMapSceneOverride = false;
bool sDirectTouchSceneOverrideEnabled = false;
bool sDirectTouchSceneOverride = false;
SafeArea sSafeArea;
bool sGameplaySceneOverrideEnabled = false;
bool sGameplaySceneOverride = false;

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

float TouchToUiX(float x)
{
    if (!GEngine)
        return x;
    AspectSettings as;
    GEngine->GetAspectSettings(as);
    const float width = as.uiBottomRightX - as.uiTopLeftX;
    if (width <= 0.0001f)
        return x;
    return Clamp01((x - as.uiTopLeftX) / width);
}

float TouchToUiY(float y)
{
    if (!GEngine)
        return y;
    AspectSettings as;
    GEngine->GetAspectSettings(as);
    const float height = as.uiBottomRightY - as.uiTopLeftY;
    if (height <= 0.0001f)
        return y;
    return Clamp01((y - as.uiTopLeftY) / height);
}

float TouchToCursorUiX(float x)
{
    if (!GEngine)
        return x;
    AspectSettings as;
    GEngine->GetAspectSettings(as);
    const float width = as.uiBottomRightX - as.uiTopLeftX;
    if (width <= 0.0001f)
        return x;
    return (x - as.uiTopLeftX) / width;
}

float TouchToCursorUiY(float y)
{
    if (!GEngine)
        return y;
    AspectSettings as;
    GEngine->GetAspectSettings(as);
    const float height = as.uiBottomRightY - as.uiTopLeftY;
    if (height <= 0.0001f)
        return y;
    return (y - as.uiTopLeftY) / height;
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

float ButtonXRadius(const ButtonZone& zone, int width, int height)
{
    return zone.r * (float)std::max(1, height) / (float)std::max(1, width);
}

void ShiftButtonGroup(std::array<ButtonZone, (int)TouchButton::Count>& zones, const int* ids, int count, float dx,
                      float dy)
{
    for (int i = 0; i < count; i++)
    {
        ButtonZone& zone = zones[ids[i]];
        zone.x = Clamp01(zone.x + dx);
        zone.y = Clamp01(zone.y + dy);
    }
}

void ClampButtonGroupToSafeArea(std::array<ButtonZone, (int)TouchButton::Count>& zones, const int* ids, int count,
                                int width, int height, bool clampX, bool clampY)
{
    if (count <= 0)
        return;

    float minX = 1.0f;
    float maxX = 0.0f;
    float minY = 1.0f;
    float maxY = 0.0f;
    for (int i = 0; i < count; i++)
    {
        const ButtonZone& zone = zones[ids[i]];
        const float xRadius = ButtonXRadius(zone, width, height);
        const float yRadius = zone.r;
        minX = std::min(minX, zone.x - xRadius);
        maxX = std::max(maxX, zone.x + xRadius);
        minY = std::min(minY, zone.y - yRadius);
        maxY = std::max(maxY, zone.y + yRadius);
    }

    float dx = 0.0f;
    float dy = 0.0f;
    if (clampX)
    {
        if (minX < sSafeArea.left)
            dx = sSafeArea.left - minX;
        if (maxX + dx > sSafeArea.right)
            dx = sSafeArea.right - maxX;
    }
    if (clampY)
    {
        if (minY < sSafeArea.top)
            dy = sSafeArea.top - minY;
        if (maxY + dy > sSafeArea.bottom)
            dy = sSafeArea.bottom - maxY;
    }
    ShiftButtonGroup(zones, ids, count, dx, dy);
}

bool IsGameplayScene()
{
    if (sGameplaySceneOverrideEnabled)
        return sGameplaySceneOverride;
    return GWorld && GWorld->GetControllerUiScene().kind == ControllerSceneKind::Gameplay;
}

bool IsPersonViewButtonAvailable()
{
    return IsGameplayScene() && USER_CONFIG.IsEnabled(DT3rdPersonView);
}

bool IsMapScene()
{
    if (sEditorMapSceneOverrideEnabled && sEditorMapSceneOverride)
        return true;
    if (sMapSceneOverrideEnabled)
        return sMapSceneOverride;
    if (!GWorld)
        return false;
    const ControllerSceneKind kind = GWorld->GetControllerUiScene().kind;
    return kind == ControllerSceneKind::Map || kind == ControllerSceneKind::EditorMap;
}

bool IsRegularMapScene()
{
    if (sEditorMapSceneOverrideEnabled && sEditorMapSceneOverride)
        return false;
    if (sMapSceneOverrideEnabled)
        return sMapSceneOverride;
    if (!GWorld)
        return false;
    return GWorld->GetControllerUiScene().kind == ControllerSceneKind::Map;
}

bool IsTouchButtonAvailable(TouchButton button)
{
    switch (button)
    {
        case TouchButton::Fire:
        case TouchButton::Action:
        case TouchButton::Reload:
        case TouchButton::Optics:
            return IsGameplayScene();
        case TouchButton::Equipment:
            return IsGameplayScene() || IsRegularMapScene();
        case TouchButton::PersonView:
            return IsPersonViewButtonAvailable();
        case TouchButton::Pause:
            return true;
        default:
            return false;
    }
}

bool IsEditorMapScene()
{
    if (sEditorMapSceneOverrideEnabled)
        return sEditorMapSceneOverride;
    if (!GWorld)
        return false;
    return GWorld->GetControllerUiScene().kind == ControllerSceneKind::EditorMap;
}

bool IsDirectTouchScene()
{
    if (sDirectTouchSceneOverrideEnabled)
        return sDirectTouchSceneOverride;
    if (IsMapScene())
        return true;
    if (!GWorld)
        return false;
    const ControllerSceneKind kind = GWorld->GetControllerUiScene().kind;
    switch (kind)
    {
        case ControllerSceneKind::Menu:
        case ControllerSceneKind::Modal:
        case ControllerSceneKind::PauseMenu:
        case ControllerSceneKind::Notebook:
        case ControllerSceneKind::EditorDialog:
        case ControllerSceneKind::Multiplayer:
        case ControllerSceneKind::ModsCatalog:
        case ControllerSceneKind::DownloadModal:
            return true;
        default:
            return false;
    }
}

float DirectTouchCursorX(float x)
{
    return IsEditorMapScene() ? TouchToUiX(x) : TouchToCursorUiX(x);
}

float DirectTouchCursorY(float y)
{
    return IsEditorMapScene() ? TouchToUiY(y) : TouchToCursorUiY(y);
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
    const float columnX = layout.edgeMargin;
    const float innerX = columnX + layout.buttonGap * 0.95f;
    const float fireY = (float)height - layout.edgeMargin - layout.buttonRadius * 0.35f;
    const float actionY = fireY - layout.buttonGap * 0.88f;
    const float reloadY = fireY - layout.buttonGap * 1.78f;
    const float opticsY = fireY - layout.buttonGap * 2.68f;
    const float mapY = fireY - layout.buttonGap * 3.58f;
    const float radius = layout.buttonRadius / (float)height;
    const float oppositeX = (float)width - layout.edgeMargin;
    std::array<ButtonZone, (int)TouchButton::Count> zones = {
        {{TouchButton::Fire, NormX(columnX, width), NormY(fireY, height), radius * 1.18f},
         {TouchButton::Action, NormX(innerX, width), NormY(actionY, height), radius},
         {TouchButton::Reload, NormX(columnX, width), NormY(reloadY, height), radius},
         {TouchButton::Optics, NormX(innerX, width), NormY(opticsY, height), radius},
         {TouchButton::Equipment, NormX(columnX, width), NormY(mapY, height), radius},
         {TouchButton::PersonView, NormX(oppositeX, width), NormY(layout.topMargin, height), radius * 0.72f},
         {TouchButton::Pause, NormX(layout.edgeMargin, width), NormY(layout.topMargin, height), radius * 0.72f}}};
    const int leftCluster[] = {(int)TouchButton::Fire,   (int)TouchButton::Action,    (int)TouchButton::Reload,
                               (int)TouchButton::Optics, (int)TouchButton::Equipment, (int)TouchButton::Pause};
    const int rightCluster[] = {(int)TouchButton::PersonView};
    const int lowerCluster[] = {(int)TouchButton::Fire,   (int)TouchButton::Action, (int)TouchButton::Reload,
                                (int)TouchButton::Optics, (int)TouchButton::Equipment};
    const int upperCluster[] = {(int)TouchButton::Pause, (int)TouchButton::PersonView};
    ClampButtonGroupToSafeArea(zones, leftCluster, 6, width, height, true, false);
    ClampButtonGroupToSafeArea(zones, rightCluster, 1, width, height, true, false);
    ClampButtonGroupToSafeArea(zones, lowerCluster, 5, width, height, false, true);
    ClampButtonGroupToSafeArea(zones, upperCluster, 2, width, height, false, true);
    return zones;
}

ButtonZone GetEquipmentAnchor(int width, int height)
{
    return BuildButtonZones(width, height)[(int)TouchButton::Equipment];
}

bool PlayerHasBinocular()
{
    if (!GWorld || !GWorld->GetRealPlayer())
        return false;

    const Person* person = GWorld->GetRealPlayer();
    for (int i = 0; i < person->NMagazineSlots(); i++)
    {
        const MagazineSlot& slot = person->GetMagazineSlot(i);
        if (slot._weapon && (slot._weapon->_weaponType & MaskSlotBinocular))
            return true;
    }
    return false;
}

int BuildAvailableEquipment(EquipmentItem* items, int maxItems)
{
    int count = 0;
    auto add = [&](EquipmentItem item) {
        if (count < maxItems)
            items[count++] = item;
    };

    if (GWorld)
    {
        if (GWorld->CanShowMap())
            add(EquipmentItem::Map);
        if (GWorld->CanShowCompass())
            add(EquipmentItem::Compass);
        if (GWorld->CanShowWatch())
            add(EquipmentItem::Watch);
        if (PlayerHasBinocular())
            add(EquipmentItem::Binocular);
        Person* player = GWorld->GetRealPlayer();
        if (player && player->IsNVEnabled())
            add(EquipmentItem::NightVision);
    }
    return count;
}

int BuildEquipmentZones(EquipmentZone* zones, int maxZones, int width, int height)
{
    EquipmentItem items[5] = {};
    const int count = BuildAvailableEquipment(items, 5);
    if (count <= 0 || maxZones <= 0)
        return 0;

    const ButtonZone anchor = GetEquipmentAnchor(width, height);
    const float span = count > 1 ? kEquipmentRadialMinSpanDeg + kEquipmentRadialSpanStepDeg * (float)(count - 2) : 0.0f;
    const float start = kEquipmentRadialCenterDeg - span * 0.5f;
    const float step = count > 1 ? span / (float)(count - 1) : 0.0f;
    const float distance = anchor.r * (kEquipmentRadialMinDistance + kEquipmentRadialDistanceStep * (float)(count - 1));
    const int outCount = std::min(count, maxZones);
    for (int i = 0; i < outCount; i++)
    {
        const float deg = count > 1 ? start + step * (float)i : kEquipmentRadialCenterDeg;
        const float rad = deg * (3.14159265359f / 180.0f);
        zones[i].item = items[i];
        zones[i].x = Clamp01(anchor.x + std::cos(rad) * distance * (float)height / (float)std::max(1, width));
        zones[i].y = Clamp01(anchor.y + std::sin(rad) * distance);
        zones[i].r = anchor.r * 0.82f;
    }
    return outCount;
}

EquipmentItem HitEquipmentItem(float x, float y)
{
    EquipmentZone zones[5];
    const int count = BuildEquipmentZones(zones, 5, sViewportW, sViewportH);
    for (int i = 0; i < count; i++)
    {
        const float dx = (x - zones[i].x) * (float)std::max(1, sViewportW);
        const float dy = (y - zones[i].y) * (float)std::max(1, sViewportH);
        const float r = zones[i].r * (float)std::max(1, sViewportH);
        if (Length(dx, dy) <= r)
            return zones[i].item;
    }
    return EquipmentItem::None;
}

TouchButton HitButton(float x, float y)
{
    for (const ButtonZone& zone : BuildButtonZones(sViewportW, sViewportH))
    {
        if (!IsTouchButtonAvailable(zone.button))
            continue;
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

bool IsTouchButtonFinger(const Finger& finger)
{
    return finger.role == FingerRole::Button || finger.button != TouchButton::Count;
}

int CollectMapGestureFingers(const Finger** first, const Finger** second)
{
    int count = 0;
    for (const Finger& finger : sFingers)
    {
        if (!finger.active || IsTouchButtonFinger(finger))
            continue;
        if (count == 0 && first)
            *first = &finger;
        else if (count == 1 && second)
            *second = &finger;
        ++count;
    }
    return count;
}

void BufferCursorToTouch(float x, float y)
{
    const float targetX = DirectTouchCursorX(x) * 2.0f - 1.0f;
    const float targetY = DirectTouchCursorY(y) * 2.0f - 1.0f;
    const float cursorDx = targetX - GInput.cursor.cursorX;
    const float cursorDy = targetY - GInput.cursor.cursorY;
    if (std::fabs(cursorDx) > 0.0001f || std::fabs(cursorDy) > 0.0001f)
        SDLInput_BufferMouseMotion(cursorDx * kMapPanMouseScaleX, cursorDy * kMapPanMouseScaleY);
}

void ClassifyFinger(Finger& finger)
{
    finger.button = HitButton(finger.x, finger.y);
    if (finger.button != TouchButton::Count)
    {
        finger.role = FingerRole::Button;
        return;
    }
    if (IsDirectTouchScene())
    {
        finger.role = FingerRole::None;
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

void EndMapGesture()
{
    if (sMapGestureActive)
        SDLInput_BufferMouseButton(1, false);
    sMapGestureActive = false;
    sMapPanX = 0.0f;
    sMapPanY = 0.0f;
    sMapZoom = 0.0f;
}

void EndAimFocus()
{
    sAimFocusActive = false;
    sAimFocusCalm = false;
    sAimFocusCalmSince = Glob.uiTime;
}

void EndMapPrimary()
{
    if (sMapPrimaryActive)
        SDLInput_BufferMouseButton(0, false);
    sMapPrimaryActive = false;
    sMapPrimaryFingerId = 0;
}

void ReleaseDirectTouchFinger(const Finger& finger)
{
    if (finger.role != FingerRole::None || !IsDirectTouchScene())
        return;

    BufferCursorToTouch(finger.x, finger.y);
    if (sMapPrimaryActive && sMapPrimaryFingerId == finger.id)
    {
        EndMapPrimary();
    }
    else if (finger.maxTravel <= kTapMaxTravel)
    {
        EmitPrimaryClick();
    }
    GInput.mouse.cursorLastActive = Glob.uiTime;
}

void ProcessMapPrimary(const Finger& finger)
{
    if (sMapGestureActive)
        EndMapGesture();

    BufferCursorToTouch(finger.x, finger.y);
    if (!sMapPrimaryActive || sMapPrimaryFingerId != finger.id)
    {
        if (sMapPrimaryActive)
            SDLInput_BufferMouseButton(0, false);
        sMapPrimaryFingerId = finger.id;
        sMapPrimaryActive = true;
        SDLInput_BufferMouseButton(0, true);
    }
    GInput.mouse.cursorLastActive = Glob.uiTime;
}

void ProcessMapGesture()
{
    const Finger* a = nullptr;
    const Finger* b = nullptr;
    const bool mapScene = IsMapScene();
    const int count = IsDirectTouchScene() ? CollectMapGestureFingers(&a, &b) : 0;
    if (count < 2 || !a || !b)
    {
        EndMapGesture();
        if (count == 1 && a)
            ProcessMapPrimary(*a);
        else
            EndMapPrimary();
        return;
    }

    EndMapPrimary();
    if (!mapScene)
    {
        EndMapGesture();
        return;
    }

    const float ax = TouchToUiX(a->x);
    const float ay = TouchToUiY(a->y);
    const float bx = TouchToUiX(b->x);
    const float by = TouchToUiY(b->y);
    const float lastAx = TouchToUiX(a->lastX);
    const float lastAy = TouchToUiY(a->lastY);
    const float lastBx = TouchToUiX(b->lastX);
    const float lastBy = TouchToUiY(b->lastY);
    const float cx = (ax + bx) * 0.5f;
    const float cy = (ay + by) * 0.5f;
    const float lastCx = (lastAx + lastBx) * 0.5f;
    const float lastCy = (lastAy + lastBy) * 0.5f;
    const float dist = std::max(Length(ax - bx, ay - by), 0.0001f);
    const float lastDist = std::max(Length(lastAx - lastBx, lastAy - lastBy), 0.0001f);

    if (!sMapGestureActive)
        SDLInput_BufferMouseButton(1, true);
    sMapGestureActive = true;

    sMapPanX = (cx - lastCx) * 2.0f;
    sMapPanY = (cy - lastCy) * 2.0f;
    sMapZoom = -std::log(dist / lastDist) * kMapPinchZoomScale;

    if (std::fabs(sMapPanX) > 0.0001f || std::fabs(sMapPanY) > 0.0001f)
    {
        SDLInput_BufferMouseMotion(sMapPanX * kMapPanMouseScaleX, sMapPanY * kMapPanMouseScaleY);
        GInput.mouse.cursorLastActive = Glob.uiTime;
    }
    if (std::fabs(sMapZoom) > 0.001f)
    {
        SDLInput_BufferMouseWheel(sMapZoom);
        GInput.mouse.cursorLastActive = Glob.uiTime;
    }
}

void ResetActionScroll()
{
    sActionScrollActive = false;
    sActionScrollFingerId = 0;
    sActionScrollAccumY = 0.0f;
    sActionScrollSteps = 0;
}

void ResetActionTap()
{
    sActionTapPending = false;
    sActionTapFingerId = 0;
}

void ResetEquipmentRadial()
{
    sEquipmentRadialActive = false;
    sEquipmentFingerId = 0;
    sEquipmentHover = EquipmentItem::None;
}

void EmitKeyTap(SDL_Scancode sc)
{
    SDLInput_BufferKeyEvent(sc, true, Foundation::GlobalTickCount());
    SDLInput_BufferKeyEvent(sc, false, Foundation::GlobalTickCount());
}

SDL_Scancode EquipmentHoldScancode(EquipmentItem item)
{
    switch (item)
    {
        case EquipmentItem::Compass:
            return SDL_SCANCODE_G;
        case EquipmentItem::Watch:
            return SDL_SCANCODE_T;
        default:
            return SDL_SCANCODE_UNKNOWN;
    }
}

void SetLatchedEquipment(EquipmentItem item)
{
    const SDL_Scancode oldScancode = EquipmentHoldScancode(sLatchedEquipment);
    if (oldScancode != SDL_SCANCODE_UNKNOWN)
        SDLInput_BufferKeyEvent(oldScancode, false, Foundation::GlobalTickCount());

    if (sLatchedEquipment == item)
    {
        sLatchedEquipment = EquipmentItem::None;
        return;
    }

    sLatchedEquipment = item;
    const SDL_Scancode newScancode = EquipmentHoldScancode(item);
    if (newScancode != SDL_SCANCODE_UNKNOWN)
        SDLInput_BufferKeyEvent(newScancode, true, Foundation::GlobalTickCount());
}

void EmitEquipmentItem(EquipmentItem item)
{
    switch (item)
    {
        case EquipmentItem::Map:
            SetLatchedEquipment(EquipmentItem::None);
            EmitKeyTap(SDL_SCANCODE_M);
            break;
        case EquipmentItem::Compass:
            SetLatchedEquipment(EquipmentItem::Compass);
            break;
        case EquipmentItem::Watch:
            SetLatchedEquipment(EquipmentItem::Watch);
            break;
        case EquipmentItem::Binocular:
            SetLatchedEquipment(EquipmentItem::None);
            EmitKeyTap(SDL_SCANCODE_B);
            break;
        case EquipmentItem::NightVision:
            SetLatchedEquipment(EquipmentItem::None);
            EmitKeyTap(SDL_SCANCODE_N);
            break;
        default:
            break;
    }
}

void EmitActionTap()
{
    InputSubsystem::Instance().SetSyntheticStickButton(0, true);
    SDLInput_BufferKeyEvent(SDL_SCANCODE_RETURN, true, Foundation::GlobalTickCount());
    SDLInput_BufferKeyEvent(SDL_SCANCODE_RETURN, false, Foundation::GlobalTickCount());
    SDLInput_BufferControllerUiAction(ControllerUiAction::Confirm, true);
}

void ProcessActionButtonDrag(const Finger& finger, float dy)
{
    if (!IsGameplayScene())
        return;
    if (finger.role != FingerRole::Button || finger.button != TouchButton::Action)
        return;

    if (!sActionScrollActive || sActionScrollFingerId != finger.id)
    {
        sActionScrollActive = true;
        sActionScrollFingerId = finger.id;
        sActionScrollAccumY = 0.0f;
    }

    sActionScrollAccumY += dy;
    while (std::fabs(sActionScrollAccumY) >= kActionScrollStepY)
    {
        const float direction = sActionScrollAccumY < 0.0f ? 1.0f : -1.0f;
        SDLInput_BufferMouseWheel(direction * kActionScrollWheelTicks);
        sActionScrollSteps += direction > 0.0f ? 1 : -1;
        ResetActionTap();
        sActionScrollAccumY -= direction < 0.0f ? kActionScrollStepY : -kActionScrollStepY;
        GInput.mouse.cursorLastActive = Glob.uiTime;
    }
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
            break;
        case TouchButton::Reload:
            SDLInput_BufferKeyEvent(SDL_SCANCODE_R, down, Foundation::GlobalTickCount());
            break;
        case TouchButton::Optics:
            SDLInput_BufferKeyEvent(SDL_SCANCODE_V, down, Foundation::GlobalTickCount());
            break;
        case TouchButton::Equipment:
            break;
        case TouchButton::PersonView:
            if (down)
                SDLInput_BufferKeyEvent(SDL_SCANCODE_KP_ENTER, true, Foundation::GlobalTickCount());
            else
                SDLInput_BufferKeyEvent(SDL_SCANCODE_KP_ENTER, false, Foundation::GlobalTickCount());
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

void EmitFingerButtonEdge(const Finger& finger, bool down)
{
    if (finger.role != FingerRole::Button || finger.button == TouchButton::Count)
        return;
    if (finger.button == TouchButton::Equipment)
    {
        if (down)
        {
            sEquipmentRadialActive = true;
            sEquipmentFingerId = finger.id;
            sEquipmentHover = EquipmentItem::None;
        }
        else
        {
            if (sEquipmentRadialActive && sEquipmentFingerId == finger.id && sEquipmentHover != EquipmentItem::None)
                EmitEquipmentItem(sEquipmentHover);
            ResetEquipmentRadial();
        }
        sPrevButtonDown[(int)finger.button] = down;
        return;
    }
    if (finger.button == TouchButton::Action)
    {
        if (down)
        {
            sActionTapPending = true;
            sActionTapFingerId = finger.id;
        }
        else
        {
            if (sActionTapPending && sActionTapFingerId == finger.id && sActionScrollSteps == 0 &&
                finger.maxTravel <= kTapMaxTravel)
                EmitActionTap();
            ResetActionTap();
        }
        sPrevButtonDown[(int)finger.button] = down;
        return;
    }
    EmitButtonEdge(finger.button, down);
    sPrevButtonDown[(int)finger.button] = down;
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
        case TouchButton::Equipment:
            DrawLineNorm(engine, cx - sx * 0.48f, cy - sy * 0.34f, cx + sx * 0.48f, cy - sy * 0.26f, color);
            DrawLineNorm(engine, cx + sx * 0.48f, cy - sy * 0.26f, cx + sx * 0.48f, cy + sy * 0.34f, color);
            DrawLineNorm(engine, cx + sx * 0.48f, cy + sy * 0.34f, cx - sx * 0.48f, cy + sy * 0.26f, color);
            DrawLineNorm(engine, cx - sx * 0.48f, cy + sy * 0.26f, cx - sx * 0.48f, cy - sy * 0.34f, color);
            DrawLineNorm(engine, cx - sx * 0.16f, cy - sy * 0.31f, cx - sx * 0.16f, cy + sy * 0.29f, color);
            DrawLineNorm(engine, cx + sx * 0.16f, cy - sy * 0.29f, cx + sx * 0.16f, cy + sy * 0.31f, color);
            DrawCircleApprox(engine, white, cx, cy, r * 0.16f, color);
            break;
        case TouchButton::PersonView:
            DrawCircleApprox(engine, white, cx, cy, r * 0.36f, color);
            DrawLineNorm(engine, cx - sx * 0.46f, cy + sy * 0.10f, cx, cy - sy * 0.38f, color);
            DrawLineNorm(engine, cx, cy - sy * 0.38f, cx + sx * 0.46f, cy + sy * 0.10f, color);
            DrawLineNorm(engine, cx - sx * 0.46f, cy + sy * 0.10f, cx - sx * 0.26f, cy + sy * 0.36f, color);
            DrawLineNorm(engine, cx + sx * 0.46f, cy + sy * 0.10f, cx + sx * 0.26f, cy + sy * 0.36f, color);
            break;
        case TouchButton::Pause:
            DrawRectNorm(engine, white, cx - sx * 0.16f, cy, sx * 0.12f, sy * 0.62f, color);
            DrawRectNorm(engine, white, cx + sx * 0.16f, cy, sx * 0.12f, sy * 0.62f, color);
            break;
        default:
            break;
    }
}

void DrawTouchIconContrast(Engine* engine, const MipInfo& white, TouchButton button, float cx, float cy, float r,
                           bool pressed)
{
    const float dx = 1.2f / (float)std::max(1, engine->Width());
    const float dy = 1.2f / (float)std::max(1, engine->Height());
    const PackedColor halo(Color(0.0f, 0.0f, 0.0f, pressed ? 0.52f : 0.72f));
    const PackedColor icon(pressed ? Color(0.02f, 0.04f, 0.05f, 0.92f) : Color(0.98f, 1.0f, 1.0f, 0.92f));

    DrawTouchIcon(engine, white, button, cx - dx, cy, r, halo);
    DrawTouchIcon(engine, white, button, cx + dx, cy, r, halo);
    DrawTouchIcon(engine, white, button, cx, cy - dy, r, halo);
    DrawTouchIcon(engine, white, button, cx, cy + dy, r, halo);
    DrawTouchIcon(engine, white, button, cx, cy, r, icon);
}

void DrawEquipmentItemIcon(Engine* engine, const MipInfo& white, EquipmentItem item, float cx, float cy, float r,
                           PackedColor color)
{
    const float aspect = (float)engine->Height() / (float)std::max(1, engine->Width());
    const float sx = r * aspect;
    const float sy = r;

    switch (item)
    {
        case EquipmentItem::Map:
            DrawLineNorm(engine, cx - sx * 0.48f, cy - sy * 0.34f, cx + sx * 0.48f, cy - sy * 0.26f, color);
            DrawLineNorm(engine, cx + sx * 0.48f, cy - sy * 0.26f, cx + sx * 0.48f, cy + sy * 0.34f, color);
            DrawLineNorm(engine, cx + sx * 0.48f, cy + sy * 0.34f, cx - sx * 0.48f, cy + sy * 0.26f, color);
            DrawLineNorm(engine, cx - sx * 0.48f, cy + sy * 0.26f, cx - sx * 0.48f, cy - sy * 0.34f, color);
            DrawLineNorm(engine, cx - sx * 0.16f, cy - sy * 0.31f, cx - sx * 0.16f, cy + sy * 0.29f, color);
            DrawLineNorm(engine, cx + sx * 0.16f, cy - sy * 0.29f, cx + sx * 0.16f, cy + sy * 0.31f, color);
            break;
        case EquipmentItem::Compass:
            DrawCircleApprox(engine, white, cx, cy, r * 0.42f, color);
            DrawLineNorm(engine, cx, cy - sy * 0.48f, cx + sx * 0.10f, cy - sy * 0.20f, color);
            DrawLineNorm(engine, cx, cy - sy * 0.48f, cx - sx * 0.10f, cy - sy * 0.20f, color);
            DrawLineNorm(engine, cx, cy - sy * 0.30f, cx, cy + sy * 0.34f, color);
            break;
        case EquipmentItem::Watch:
            DrawCircleApprox(engine, white, cx, cy, r * 0.42f, color);
            DrawLineNorm(engine, cx, cy, cx, cy - sy * 0.28f, color);
            DrawLineNorm(engine, cx, cy, cx + sx * 0.24f, cy + sy * 0.10f, color);
            DrawLineNorm(engine, cx - sx * 0.20f, cy - sy * 0.52f, cx + sx * 0.20f, cy - sy * 0.52f, color);
            DrawLineNorm(engine, cx - sx * 0.20f, cy + sy * 0.52f, cx + sx * 0.20f, cy + sy * 0.52f, color);
            break;
        case EquipmentItem::Binocular:
            DrawCircleApprox(engine, white, cx - sx * 0.22f, cy, r * 0.22f, color);
            DrawCircleApprox(engine, white, cx + sx * 0.22f, cy, r * 0.22f, color);
            DrawLineNorm(engine, cx - sx * 0.04f, cy, cx + sx * 0.04f, cy, color);
            DrawLineNorm(engine, cx - sx * 0.44f, cy - sy * 0.34f, cx - sx * 0.18f, cy - sy * 0.18f, color);
            DrawLineNorm(engine, cx + sx * 0.44f, cy - sy * 0.34f, cx + sx * 0.18f, cy - sy * 0.18f, color);
            break;
        case EquipmentItem::NightVision:
            DrawCircleApprox(engine, white, cx - sx * 0.20f, cy, r * 0.20f, color);
            DrawCircleApprox(engine, white, cx + sx * 0.20f, cy, r * 0.20f, color);
            DrawLineNorm(engine, cx - sx * 0.38f, cy - sy * 0.30f, cx + sx * 0.38f, cy - sy * 0.30f, color);
            DrawLineNorm(engine, cx, cy - sy * 0.30f, cx, cy - sy * 0.52f, color);
            DrawLineNorm(engine, cx - sx * 0.18f, cy + sy * 0.22f, cx - sx * 0.32f, cy + sy * 0.44f, color);
            DrawLineNorm(engine, cx + sx * 0.18f, cy + sy * 0.22f, cx + sx * 0.32f, cy + sy * 0.44f, color);
            break;
        default:
            break;
    }
}

void DrawEquipmentItemIconContrast(Engine* engine, const MipInfo& white, EquipmentItem item, float cx, float cy,
                                   float r, bool hover)
{
    const float dx = 1.2f / (float)std::max(1, engine->Width());
    const float dy = 1.2f / (float)std::max(1, engine->Height());
    const PackedColor halo(Color(0.0f, 0.0f, 0.0f, hover ? 0.52f : 0.72f));
    const PackedColor icon(hover ? Color(0.02f, 0.04f, 0.05f, 0.92f) : Color(0.98f, 1.0f, 1.0f, 0.92f));

    DrawEquipmentItemIcon(engine, white, item, cx - dx, cy, r, halo);
    DrawEquipmentItemIcon(engine, white, item, cx + dx, cy, r, halo);
    DrawEquipmentItemIcon(engine, white, item, cx, cy - dy, r, halo);
    DrawEquipmentItemIcon(engine, white, item, cx, cy + dy, r, halo);
    DrawEquipmentItemIcon(engine, white, item, cx, cy, r, icon);
}

void DrawTouchButtonFrame(Engine* engine, const MipInfo& white, float cx, float cy, float r, bool pressed)
{
    const PackedColor outer(Color(0.0f, 0.0f, 0.0f, pressed ? 0.78f : 0.66f));
    const PackedColor inner(pressed ? Color(0.86f, 0.95f, 1.0f, 0.82f) : Color(0.94f, 0.98f, 1.0f, 0.62f));
    const PackedColor glint(Color(1.0f, 1.0f, 1.0f, pressed ? 0.52f : 0.34f));

    DrawCircleApprox(engine, white, cx, cy, r * 1.07f, outer);
    DrawCircleApprox(engine, white, cx, cy, r, inner);
    DrawCircleApprox(engine, white, cx, cy, r * 0.78f, glint);
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
        if (finger->role == FingerRole::Button && finger->button == TouchButton::Action &&
            sActionScrollFingerId == finger->id)
            ResetActionScroll();
        if (finger->role == FingerRole::Button && finger->button == TouchButton::Equipment &&
            sEquipmentRadialActive && sEquipmentFingerId == finger->id)
            sEquipmentHover = HitEquipmentItem(finger->x, finger->y);
        EmitFingerButtonEdge(*finger, false);
        ReleaseDirectTouchFinger(*finger);
        const bool quickTap = Glob.uiTime - finger->startTime <= kTapMaxSeconds;
        if (finger->role == FingerRole::Look && finger->maxTravel <= kTapMaxTravel && quickTap && IsGameplayScene())
            EmitPrimaryClick();
        *finger = {};
        return;
    }

    finger->lastX = finger->x;
    finger->lastY = finger->y;
    finger->x = x;
    finger->y = y;
    finger->maxTravel = std::max(finger->maxTravel, Length(finger->x - finger->startX, finger->y - finger->startY));
    if (event.type == SDL_EVENT_FINGER_MOTION)
    {
        ProcessActionButtonDrag(*finger, finger->y - finger->lastY);
        if (finger->role == FingerRole::Button && finger->button == TouchButton::Equipment &&
            sEquipmentRadialActive && sEquipmentFingerId == finger->id)
            sEquipmentHover = HitEquipmentItem(finger->x, finger->y);
    }
    if (event.type == SDL_EVENT_FINGER_DOWN)
    {
        finger->startX = x;
        finger->startY = y;
        finger->lastX = x;
        finger->lastY = y;
        finger->startTime = Glob.uiTime;
        finger->maxTravel = 0.0f;
        ClassifyFinger(*finger);
        EmitFingerButtonEdge(*finger, true);
    }
}

void TouchInput_UpdateSafeAreaFromWindow(SDL_Window* window)
{
    SafeArea safe;
    if (window)
    {
        int windowW = 0;
        int windowH = 0;
        SDL_Rect rect{};
        if (SDL_GetWindowSize(window, &windowW, &windowH) && SDL_GetWindowSafeArea(window, &rect) && windowW > 0 &&
            windowH > 0 && rect.w > 0 && rect.h > 0)
        {
            safe.left = Clamp01((float)rect.x / (float)windowW);
            safe.top = Clamp01((float)rect.y / (float)windowH);
            safe.right = Clamp01((float)(rect.x + rect.w) / (float)windowW);
            safe.bottom = Clamp01((float)(rect.y + rect.h) / (float)windowH);
            if (safe.right <= safe.left || safe.bottom <= safe.top)
                safe = SafeArea();
        }
    }
    sSafeArea = safe;
}

void TouchInput_ProcessFrame(int viewportWidth, int viewportHeight)
{
    sViewportW = viewportWidth;
    sViewportH = viewportHeight;
    sMoveX = 0.0f;
    sMoveY = 0.0f;
    sLookDx = 0.0f;
    sLookDy = 0.0f;
    sMapPanX = 0.0f;
    sMapPanY = 0.0f;
    sMapZoom = 0.0f;
    std::fill(std::begin(sButtonDown), std::end(sButtonDown), false);

    if (!sEnabled)
    {
        EndAimFocus();
        ResetActionScroll();
        EndMapPrimary();
        EndMapGesture();
        return;
    }

    ProcessMapGesture();

    bool moveActive = false;
    bool lookActive = false;
    for (Finger& finger : sFingers)
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
            const float dx = (finger.x - finger.lastX) * scaleX * sensitivity;
            const float dy = (finger.y - finger.lastY) * scaleY * sensitivity;
            sLookDx += dx;
            sLookDy += dy;
            if (gameplay)
            {
                const float lookPixels = Length(dx, dy);
                if (lookPixels <= kAimFocusCalmPixels)
                {
                    if (!sAimFocusCalm)
                    {
                        sAimFocusCalm = true;
                        sAimFocusCalmSince = Glob.uiTime;
                    }
                }
                else
                {
                    sAimFocusCalm = false;
                    sAimFocusCalmSince = Glob.uiTime;
                }
                if (sAimFocusActive && lookPixels >= kAimFocusReleasePixels)
                    EndAimFocus();
            }
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
        float syntheticMoveY = sMoveY;
        if (std::fabs(sMoveX) > 0.35f && std::fabs(sMoveY) < std::fabs(sMoveX) * kTouchStrafeSnapRatio)
            syntheticMoveY = 0.0f;
        InputSubsystem::Instance().SetSyntheticLeftStick(sMoveX, syntheticMoveY);
        if (moveActive)
        {
            GInput.gamepad.moveLastActive = Glob.uiTime;
            GInput.mouse.turnLastActive = Glob.uiTime;
        }
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
        if (!IsGameplayScene())
            EndAimFocus();
    }
    else
    {
        EndAimFocus();
    }

    if (lookActive && IsGameplayScene() && sAimFocusCalm && !sAimFocusActive &&
        Glob.uiTime - sAimFocusCalmSince >= kAimFocusDwellSeconds)
        sAimFocusActive = true;

    for (int i = 0; i < (int)TouchButton::Count; i++)
    {
        if (sButtonDown[i])
            EmitButtonHeld((TouchButton)i);
    }

    for (Finger& finger : sFingers)
    {
        if (!finger.active)
            continue;
        finger.lastX = finger.x;
        finger.lastY = finger.y;
    }
}

void TouchInput_DrawOverlay(Engine* engine)
{
    if (!sEnabled || !engine || !engine->TextBank())
        return;

    const MipInfo white = engine->TextBank()->UseMipmap(nullptr, 0, 0);
    const PackedColor base(Color(0.02f, 0.03f, 0.04f, 0.44f));
    const PackedColor active(Color(0.98f, 1.0f, 1.0f, 0.72f));

    if (std::fabs(sMoveX) > 0.001f || std::fabs(sMoveY) > 0.001f)
    {
        DrawCircleApprox(engine, white, sMoveAnchorX, sMoveAnchorY, kStickRadius, base);
        DrawCircleApprox(engine, white, sMoveThumbX, sMoveThumbY, kStickRadius * 0.42f, active);
    }
    if (std::fabs(sLookDx) > 0.001f || std::fabs(sLookDy) > 0.001f)
        DrawCircleApprox(engine, white, sLookX, sLookY, 0.035f, active);

    const int w = sViewportW > 0 ? sViewportW : engine->Width();
    const int h = sViewportH > 0 ? sViewportH : engine->Height();
    if (sEquipmentRadialActive)
    {
        EquipmentZone zones[5];
        const int count = BuildEquipmentZones(zones, 5, w, h);
        const ButtonZone anchor = GetEquipmentAnchor(w, h);
        for (int i = 0; i < count; i++)
        {
            DrawLineNorm(engine, anchor.x, anchor.y, zones[i].x, zones[i].y,
                         PackedColor(Color(0.0f, 0.0f, 0.0f, 0.54f)));
            DrawLineNorm(engine, anchor.x, anchor.y, zones[i].x, zones[i].y,
                         PackedColor(Color(0.95f, 0.98f, 1.0f, 0.44f)));
            const bool hover = zones[i].item == sEquipmentHover;
            DrawTouchButtonFrame(engine, white, zones[i].x, zones[i].y, zones[i].r, hover);
            DrawEquipmentItemIconContrast(engine, white, zones[i].item, zones[i].x, zones[i].y, zones[i].r * 0.82f,
                                          hover);
        }
        return;
    }
    for (const ButtonZone& zone : BuildButtonZones(w, h))
    {
        if (!IsTouchButtonAvailable(zone.button))
            continue;
        const bool down = sButtonDown[(int)zone.button];
        DrawTouchButtonFrame(engine, white, zone.x, zone.y, zone.r, down);
        DrawTouchIconContrast(engine, white, zone.button, zone.x, zone.y, zone.r * 0.82f, down);
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

bool TouchInput_IsAimFocusActive()
{
    return sAimFocusActive;
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
    for (const Finger& finger : sFingers)
        EmitFingerButtonEdge(finger, false);
    ResetActionTap();
    ResetActionScroll();
    EndAimFocus();
    SetLatchedEquipment(EquipmentItem::None);
    ResetEquipmentRadial();
    EndMapPrimary();
    EndMapGesture();
    sFingers = {};
    std::fill(std::begin(sButtonDown), std::end(sButtonDown), false);
    std::fill(std::begin(sPrevButtonDown), std::end(sPrevButtonDown), false);
    sMoveX = sMoveY = sLookDx = sLookDy = sMapPanX = sMapPanY = sMapZoom = 0.0f;
    InputSubsystem::Instance().SetSyntheticLeftStick(0.0f, 0.0f);
}

TouchInputDebugState TouchInput_GetDebugState()
{
    TouchInputDebugState state;
    state.enabled = sEnabled;
    state.moveActive = RoleActive(FingerRole::Move);
    state.lookActive = RoleActive(FingerRole::Look);
    state.aimFocusActive = sAimFocusActive;
    state.mapPrimaryActive = sMapPrimaryActive;
    state.mapGestureActive = sMapGestureActive;
    state.actionScrollActive = sActionScrollActive;
    state.moveX = sMoveX;
    state.moveY = sMoveY;
    state.lookDx = sLookDx;
    state.lookDy = sLookDy;
    state.mapPanX = sMapPanX;
    state.mapPanY = sMapPanY;
    state.mapZoom = sMapZoom;
    state.actionScrollSteps = sActionScrollSteps;
    for (int i = 0; i < (int)TouchButton::Count; i++)
        state.buttons[i] = sButtonDown[i];
    return state;
}

void TouchInput_TestSetMapSceneOverride(bool enabled, bool isMapScene)
{
    sMapSceneOverrideEnabled = enabled;
    sMapSceneOverride = isMapScene;
}

void TouchInput_TestSetEditorMapSceneOverride(bool enabled, bool isEditorMapScene)
{
    sEditorMapSceneOverrideEnabled = enabled;
    sEditorMapSceneOverride = isEditorMapScene;
}

void TouchInput_TestSetDirectTouchSceneOverride(bool enabled, bool isDirectTouchScene)
{
    sDirectTouchSceneOverrideEnabled = enabled;
    sDirectTouchSceneOverride = isDirectTouchScene;
}

void TouchInput_TestSetGameplaySceneOverride(bool enabled, bool isGameplayScene)
{
    sGameplaySceneOverrideEnabled = enabled;
    sGameplaySceneOverride = isGameplayScene;
}

void TouchInput_TestSetSafeArea(float left, float top, float right, float bottom)
{
    SafeArea safe;
    safe.left = Clamp01(left);
    safe.top = Clamp01(top);
    safe.right = Clamp01(right);
    safe.bottom = Clamp01(bottom);
    if (safe.right <= safe.left || safe.bottom <= safe.top)
        safe = SafeArea();
    sSafeArea = safe;
}

} // namespace Poseidon
