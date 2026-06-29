#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Input/KeyInput.hpp>
#include <Poseidon/Input/TouchInput.hpp>

using namespace Poseidon;

namespace Poseidon
{
extern Input GInput;
}

namespace
{
SDL_TouchFingerEvent Finger(SDL_EventType type, SDL_FingerID id, float x, float y)
{
    SDL_TouchFingerEvent event = {};
    event.type = type;
    event.fingerID = id;
    event.x = x;
    event.y = y;
    return event;
}

struct TouchFixture
{
    TouchFixture()
    {
        TouchInput_SetEnabled(true);
        TouchInput_SetAimSensitivity(1.0f);
        TouchInput_SetCursorSensitivity(1.0f);
        TouchInput_TestSetGameplaySceneOverride(false, false);
        TouchInput_TestSetMapSceneOverride(false, false);
        TouchInput_Reset();
    }
    ~TouchFixture()
    {
        TouchInput_Reset();
        TouchInput_TestSetGameplaySceneOverride(false, false);
        TouchInput_TestSetMapSceneOverride(false, false);
        TouchInput_SetAimSensitivity(1.0f);
        TouchInput_SetCursorSensitivity(1.0f);
        TouchInput_SetEnabled(false);
    }
};
} // namespace

TEST_CASE("TouchInput: left lower finger owns movement stick", "[input][touch]")
{
    TouchFixture fixture;

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 1, 0.20f, 0.80f));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_MOTION, 1, 0.26f, 0.74f));
    TouchInput_ProcessFrame(1920, 1080);

    TouchInputDebugState state = TouchInput_GetDebugState();
    REQUIRE(state.moveActive);
    REQUIRE_FALSE(state.lookActive);
    CHECK(state.moveX == Catch::Approx(0.52f).margin(0.04f));
    CHECK(state.moveY == Catch::Approx(-0.52f).margin(0.04f));

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_UP, 1, 0.26f, 0.74f));
    TouchInput_ProcessFrame(1920, 1080);
    state = TouchInput_GetDebugState();
    CHECK_FALSE(state.moveActive);
    CHECK(state.moveX == Catch::Approx(0.0f));
    CHECK(state.moveY == Catch::Approx(0.0f));
}

TEST_CASE("TouchInput: right-side drag owns cursor/look and does not steal button touches", "[input][touch]")
{
    TouchFixture fixture;

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 1, 0.70f, 0.50f));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 2, 0.96f, 0.91f));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_MOTION, 1, 0.73f, 0.47f));
    TouchInput_ProcessFrame(1920, 1080);

    TouchInputDebugState state = TouchInput_GetDebugState();
    REQUIRE(state.lookActive);
    CHECK_FALSE(state.moveActive);
    CHECK(state.buttons[(int)TouchButton::Fire]);
    CHECK(state.lookDx == Catch::Approx(57.6f).margin(0.1f));
    CHECK(state.lookDy == Catch::Approx(-32.4f).margin(0.1f));
}

TEST_CASE("TouchInput: simultaneous move look and button state coexist", "[input][touch]")
{
    TouchFixture fixture;

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 1, 0.18f, 0.78f));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_MOTION, 1, 0.18f, 0.66f));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 2, 0.72f, 0.50f));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_MOTION, 2, 0.75f, 0.50f));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 3, 0.90f, 0.81f));
    TouchInput_ProcessFrame(1920, 1080);

    TouchInputDebugState state = TouchInput_GetDebugState();
    CHECK(state.moveActive);
    CHECK(state.lookActive);
    CHECK(state.moveY < -0.9f);
    CHECK(state.lookDx > 8.0f);
    CHECK(state.buttons[(int)TouchButton::Action]);
}

TEST_CASE("TouchInput: finger coordinates are clamped before classification", "[input][touch]")
{
    TouchFixture fixture;

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 1, -0.50f, 1.50f));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_MOTION, 1, 0.50f, -0.50f));
    TouchInput_ProcessFrame(1920, 1080);

    TouchInputDebugState state = TouchInput_GetDebugState();
    REQUIRE(state.moveActive);
    CHECK(state.moveX == Catch::Approx(1.0f));
    CHECK(state.moveY == Catch::Approx(-1.0f));
}

TEST_CASE("TouchInput: map scene one finger drives primary map interaction", "[input][touch]")
{
    TouchFixture fixture;
    TouchInput_TestSetMapSceneOverride(true, true);

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 1, 0.25f, 0.55f));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_MOTION, 1, 0.35f, 0.65f));
    TouchInput_ProcessFrame(1920, 1080);

    TouchInputDebugState state = TouchInput_GetDebugState();
    CHECK_FALSE(state.moveActive);
    CHECK_FALSE(state.lookActive);
    CHECK(state.mapPrimaryActive);
    CHECK_FALSE(state.mapGestureActive);
    CHECK(state.mapPanX == Catch::Approx(0.0f));
    CHECK(state.mapZoom == Catch::Approx(0.0f));
}

TEST_CASE("TouchInput: map scene two-finger gesture pans and pinches", "[input][touch]")
{
    TouchFixture fixture;
    TouchInput_TestSetMapSceneOverride(true, true);

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 1, 0.30f, 0.40f));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 2, 0.50f, 0.40f));
    TouchInput_ProcessFrame(1920, 1080);

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_MOTION, 1, 0.32f, 0.42f));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_MOTION, 2, 0.56f, 0.42f));
    TouchInput_ProcessFrame(1920, 1080);

    TouchInputDebugState state = TouchInput_GetDebugState();
    REQUIRE(state.mapGestureActive);
    CHECK_FALSE(state.mapPrimaryActive);
    CHECK_FALSE(state.moveActive);
    CHECK_FALSE(state.lookActive);
    CHECK(state.mapPanX == Catch::Approx(0.08f).margin(0.001f));
    CHECK(state.mapPanY == Catch::Approx(0.04f).margin(0.001f));
    CHECK(state.mapZoom < -0.8f);

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_UP, 2, 0.56f, 0.42f));
    TouchInput_ProcessFrame(1920, 1080);
    state = TouchInput_GetDebugState();
    CHECK_FALSE(state.mapGestureActive);
}

TEST_CASE("TouchInput: action button hold-drag emits action menu scroll steps", "[input][touch]")
{
    TouchFixture fixture;
    TouchInput_TestSetGameplaySceneOverride(true, true);
    GInput.keyboard.ForgetKeys();

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 1, 0.90f, 0.81f));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_MOTION, 1, 0.90f, 0.75f));
    TouchInput_ProcessFrame(1920, 1080);

    TouchInputDebugState state = TouchInput_GetDebugState();
    CHECK(state.buttons[(int)TouchButton::Action]);
    CHECK(state.actionScrollActive);
    CHECK(state.actionScrollSteps == 1);

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_MOTION, 1, 0.90f, 0.88f));
    TouchInput_ProcessFrame(1920, 1080);
    state = TouchInput_GetDebugState();
    CHECK(state.actionScrollSteps == -1);

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_UP, 1, 0.90f, 0.88f));
    state = TouchInput_GetDebugState();
    CHECK_FALSE(state.actionScrollActive);

    GInput.keyboard.Update(Poseidon::Foundation::GlobalTickCount(), 16, true);
    CHECK_FALSE(GInput.keyboard.keysToDo[SDL_SCANCODE_RETURN]);
}

TEST_CASE("TouchInput: quick action tap emits action key on release", "[input][touch]")
{
    TouchFixture fixture;
    TouchInput_TestSetGameplaySceneOverride(true, true);
    GInput.keyboard.ForgetKeys();

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 1, 0.90f, 0.81f));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_UP, 1, 0.90f, 0.81f));

    GInput.keyboard.Update(Poseidon::Foundation::GlobalTickCount(), 16, true);
    CHECK(GInput.keyboard.keysToDo[SDL_SCANCODE_RETURN]);
}

TEST_CASE("TouchInput: action hold with drag does not trigger action on release", "[input][touch]")
{
    TouchFixture fixture;
    TouchInput_TestSetGameplaySceneOverride(true, true);
    GInput.keyboard.ForgetKeys();

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 1, 0.90f, 0.81f));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_MOTION, 1, 0.90f, 0.84f));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_UP, 1, 0.90f, 0.84f));

    GInput.keyboard.Update(Poseidon::Foundation::GlobalTickCount(), 16, true);
    CHECK_FALSE(GInput.keyboard.keysToDo[SDL_SCANCODE_RETURN]);
}
