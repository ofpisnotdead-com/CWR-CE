#pragma once

#include <SDL3/SDL_events.h>

struct SDL_Window;

namespace Poseidon
{

class Engine;

enum class TouchButton
{
    Fire,
    Action,
    Reload,
    Optics,
    Equipment,
    PersonView,
    CycleWeapon,
    Pause,
    Count,
};

struct TouchInputDebugState
{
    bool enabled = false;
    bool moveActive = false;
    bool lookActive = false;
    bool aimFocusActive = false;
    bool mapPrimaryActive = false;
    bool mapGestureActive = false;
    bool actionScrollActive = false;
    float moveX = 0.0f;
    float moveY = 0.0f;
    float lookDx = 0.0f;
    float lookDy = 0.0f;
    float mapPanX = 0.0f;
    float mapPanY = 0.0f;
    float mapZoom = 0.0f;
    int actionScrollSteps = 0;
    bool buttons[(int)TouchButton::Count] = {};
};

void TouchInput_HandleFingerEvent(const SDL_TouchFingerEvent& event);
void TouchInput_UpdateSafeAreaFromWindow(SDL_Window* window);
void TouchInput_ProcessFrame(int viewportWidth, int viewportHeight);
void TouchInput_DrawOverlay(Engine* engine);
void TouchInput_SetEnabled(bool enabled);
bool TouchInput_IsEnabled();
bool TouchInput_IsAimFocusActive();
void TouchInput_SetAimSensitivity(float sensitivity);
float TouchInput_GetAimSensitivity();
void TouchInput_SetCursorSensitivity(float sensitivity);
float TouchInput_GetCursorSensitivity();
void TouchInput_Reset();
TouchInputDebugState TouchInput_GetDebugState();

void TouchInput_TestSetGameplaySceneOverride(bool enabled, bool isGameplayScene);
void TouchInput_TestSetMapSceneOverride(bool enabled, bool isMapScene);
void TouchInput_TestSetEditorMapSceneOverride(bool enabled, bool isEditorMapScene);
void TouchInput_TestSetDirectTouchSceneOverride(bool enabled, bool isDirectTouchScene);
void TouchInput_TestSetSafeArea(float left, float top, float right, float bottom);

} // namespace Poseidon
