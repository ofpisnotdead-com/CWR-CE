#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Input/KeyInput.hpp>
#include <Poseidon/Input/TouchInput.hpp>

using namespace Poseidon;

namespace Poseidon
{
extern Input GInput;
}

namespace
{
constexpr float kFireButtonX = 0.042f;
constexpr float kFireButtonY = 0.907f;
constexpr float kActionButtonX = 0.042f;
constexpr float kActionButtonY = 0.531f;
constexpr float kEquipmentButtonX = 0.958f;
constexpr float kEquipmentButtonY = 0.173f;

SDL_TouchFingerEvent Finger(SDL_EventType type, SDL_FingerID id, float x, float y)
{
    SDL_TouchFingerEvent event = {};
    event.type = type;
    event.fingerID = id;
    event.x = x;
    event.y = y;
    return event;
}

void ClearBufferedInput()
{
    GInput.mouse.DiscardBuffered();
    GInput.mouse.FlushAndReset();
    for (int i = 0; i < N_MOUSE_BUTTONS; ++i)
    {
        GInput.mouse.buttons[i] = 0.0f;
        GInput.mouse.buttonsToDo[i] = false;
        GInput.mouse.buttonsDoubleToDo[i] = false;
        GInput.mouse.buttonsDoubleActive[i] = false;
    }
    GInput.mouse.left = false;
    GInput.mouse.right = false;
    GInput.mouse.middle = false;
    GInput.mouse.leftToDo = false;
    GInput.mouse.rightToDo = false;
    GInput.mouse.middleToDo = false;
    GInput.mouse.doubleClickToDo = false;
    GInput.keyboard.ForgetKeys();
}

struct TouchFixture
{
    TouchFixture()
    {
        ClearBufferedInput();
        TouchInput_SetEnabled(true);
        TouchInput_SetAimSensitivity(1.0f);
        TouchInput_SetCursorSensitivity(1.0f);
        TouchInput_TestSetGameplaySceneOverride(false, false);
        TouchInput_TestSetMapSceneOverride(false, false);
        TouchInput_TestSetEditorMapSceneOverride(false, false);
        TouchInput_TestSetDirectTouchSceneOverride(false, false);
        TouchInput_TestSetSafeArea(0.0f, 0.0f, 1.0f, 1.0f);
        TouchInput_Reset();
    }
    ~TouchFixture()
    {
        TouchInput_Reset();
        ClearBufferedInput();
        TouchInput_TestSetGameplaySceneOverride(false, false);
        TouchInput_TestSetMapSceneOverride(false, false);
        TouchInput_TestSetEditorMapSceneOverride(false, false);
        TouchInput_TestSetDirectTouchSceneOverride(false, false);
        TouchInput_TestSetSafeArea(0.0f, 0.0f, 1.0f, 1.0f);
        TouchInput_SetAimSensitivity(1.0f);
        TouchInput_SetCursorSensitivity(1.0f);
        Glob.uiTime = Poseidon::Foundation::UITime(0);
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

TEST_CASE("TouchInput: mostly horizontal gameplay move snaps to pure strafe", "[input][touch]")
{
    TouchFixture fixture;
    TouchInput_TestSetGameplaySceneOverride(true, true);

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 1, 0.20f, 0.80f));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_MOTION, 1, 0.29f, 0.805f));
    TouchInput_ProcessFrame(1920, 1080);

    float syntheticX = 0.0f;
    float syntheticY = 0.0f;
    REQUIRE(InputSubsystem::Instance().GetSyntheticLeftStick(syntheticX, syntheticY));
    CHECK(syntheticX > 0.7f);
    CHECK(syntheticY == Catch::Approx(0.0f));
}

TEST_CASE("TouchInput: right-side drag owns cursor/look and does not steal button touches", "[input][touch]")
{
    TouchFixture fixture;
    TouchInput_TestSetGameplaySceneOverride(true, true);

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 1, 0.70f, 0.50f));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 2, kFireButtonX, kFireButtonY));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_MOTION, 1, 0.73f, 0.47f));
    TouchInput_ProcessFrame(1920, 1080);

    TouchInputDebugState state = TouchInput_GetDebugState();
    REQUIRE(state.lookActive);
    CHECK_FALSE(state.moveActive);
    CHECK(state.buttons[(int)TouchButton::Fire]);
    CHECK(state.lookDx == Catch::Approx(21.6f).margin(0.1f));
    CHECK(state.lookDy == Catch::Approx(-15.6f).margin(0.1f));
}

TEST_CASE("TouchInput: stationary held look finger does not repeat movement", "[input][touch]")
{
    TouchFixture fixture;

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 1, 0.70f, 0.50f));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_MOTION, 1, 0.73f, 0.50f));
    TouchInput_ProcessFrame(1920, 1080);

    TouchInputDebugState state = TouchInput_GetDebugState();
    REQUIRE(state.lookActive);
    CHECK(state.lookDx == Catch::Approx(57.6f).margin(0.1f));

    TouchInput_ProcessFrame(1920, 1080);
    state = TouchInput_GetDebugState();
    CHECK(state.lookActive);
    CHECK(state.lookDx == Catch::Approx(0.0f));
    CHECK(state.lookDy == Catch::Approx(0.0f));
}

// GroupBarUnitsAtTouch (the commanding group-bar hit test) requires a live GWorld/AIGroup,
// which this Input-layer unit test binary never sets up (GWorld stays null). A tap over
// where the group bar would be must therefore keep falling back to the pre-existing
// Look-region quick-tap behavior instead of silently doing nothing or crashing.
TEST_CASE("TouchInput: group-bar tap without a live world falls back to primary click", "[input][touch]")
{
    TouchFixture fixture;
    TouchInput_TestSetGameplaySceneOverride(true, true);
    GInput.keyboard.ForgetKeys();
    GInput.mouse.FlushAndReset();

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 1, 0.50f, 0.95f));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_UP, 1, 0.50f, 0.95f));

    GInput.keyboard.Update(Poseidon::Foundation::GlobalTickCount(), 16, true);
    GInput.mouse.Update(GInput.cursor, 0, false,
                        Poseidon::Foundation::UITime((int)Poseidon::Foundation::GlobalTickCount()), nullptr);

    CHECK_FALSE(GInput.keyboard.keysToDo[SDL_SCANCODE_F1]);
    CHECK(GInput.mouse.buttonsToDo[0]);
}

// Same reasoning as the group-bar test above: CommandMenuKeyAtTouch requires
// a live GWorld/InGameUI to have anything recorded in _commandMenuTapZones,
// which this Input-layer unit test binary never sets up. A tap over where
// the commanding menu would be (upper-right, away from the group bar) must
// keep falling back to the pre-existing Look-region quick-tap behavior.
TEST_CASE("TouchInput: command-menu tap without a live world falls back to primary click", "[input][touch]")
{
    TouchFixture fixture;
    TouchInput_TestSetGameplaySceneOverride(true, true);
    GInput.keyboard.ForgetKeys();
    GInput.mouse.FlushAndReset();

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 1, 0.72f, 0.18f));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_UP, 1, 0.72f, 0.18f));

    GInput.keyboard.Update(Poseidon::Foundation::GlobalTickCount(), 16, true);
    GInput.mouse.Update(GInput.cursor, 0, false,
                        Poseidon::Foundation::UITime((int)Poseidon::Foundation::GlobalTickCount()), nullptr);

    CHECK_FALSE(GInput.keyboard.keysToDo[SDL_SCANCODE_1]);
    CHECK(GInput.mouse.buttonsToDo[0]);
}

TEST_CASE("TouchInput: long gameplay look hold does not fire on release", "[input][touch]")
{
    TouchFixture fixture;
    TouchInput_TestSetGameplaySceneOverride(true, true);
    GInput.mouse.FlushAndReset();

    Glob.uiTime = Poseidon::Foundation::UITime(1000);
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 1, 0.70f, 0.50f));
    TouchInput_ProcessFrame(1920, 1080);

    Glob.uiTime = Poseidon::Foundation::UITime(1600);
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_UP, 1, 0.70f, 0.50f));
    GInput.mouse.Update(GInput.cursor, 0, false, Glob.uiTime, nullptr);

    CHECK_FALSE(GInput.mouse.buttonsToDo[0]);
    CHECK_FALSE(GInput.mouse.buttons[0] > 0.0f);
}

TEST_CASE("TouchInput: simultaneous move look and button state coexist", "[input][touch]")
{
    TouchFixture fixture;
    TouchInput_TestSetGameplaySceneOverride(true, true);

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 1, 0.18f, 0.78f));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_MOTION, 1, 0.18f, 0.66f));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 2, 0.72f, 0.50f));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_MOTION, 2, 0.75f, 0.50f));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 3, kActionButtonX, kActionButtonY));
    TouchInput_ProcessFrame(1920, 1080);

    TouchInputDebugState state = TouchInput_GetDebugState();
    CHECK(state.moveActive);
    CHECK(state.lookActive);
    CHECK(state.moveY < -0.9f);
    CHECK(state.lookDx > 8.0f);
    CHECK(state.buttons[(int)TouchButton::Action]);
}

TEST_CASE("TouchInput: calm gameplay look activates aim focus after dwell", "[input][touch]")
{
    TouchFixture fixture;
    TouchInput_TestSetGameplaySceneOverride(true, true);

    Glob.uiTime = Poseidon::Foundation::UITime(1000);
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 1, 0.70f, 0.50f));
    TouchInput_ProcessFrame(1920, 1080);
    CHECK_FALSE(TouchInput_GetDebugState().aimFocusActive);

    Glob.uiTime = Poseidon::Foundation::UITime(1250);
    TouchInput_ProcessFrame(1920, 1080);
    CHECK(TouchInput_GetDebugState().aimFocusActive);
}

TEST_CASE("TouchInput: fast gameplay look releases aim focus", "[input][touch]")
{
    TouchFixture fixture;
    TouchInput_TestSetGameplaySceneOverride(true, true);

    Glob.uiTime = Poseidon::Foundation::UITime(1000);
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 1, 0.70f, 0.50f));
    TouchInput_ProcessFrame(1920, 1080);

    Glob.uiTime = Poseidon::Foundation::UITime(1250);
    TouchInput_ProcessFrame(1920, 1080);
    REQUIRE(TouchInput_GetDebugState().aimFocusActive);

    Glob.uiTime = Poseidon::Foundation::UITime(1260);
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_MOTION, 1, 0.74f, 0.50f));
    TouchInput_ProcessFrame(1920, 1080);
    CHECK_FALSE(TouchInput_GetDebugState().aimFocusActive);
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

TEST_CASE("TouchInput: safe area clamps edge buttons out of unsafe strips", "[input][touch]")
{
    TouchFixture fixture;
    TouchInput_TestSetGameplaySceneOverride(true, true);
    TouchInput_TestSetSafeArea(0.12f, 0.0f, 1.0f, 1.0f);

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 1, kFireButtonX, kFireButtonY));
    TouchInput_ProcessFrame(1920, 1080);
    CHECK_FALSE(TouchInput_GetDebugState().buttons[(int)TouchButton::Fire]);

    TouchInput_Reset();
    TouchInput_TestSetGameplaySceneOverride(true, true);
    TouchInput_TestSetSafeArea(0.12f, 0.0f, 1.0f, 1.0f);

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 2, 0.15f, kFireButtonY));
    TouchInput_ProcessFrame(1920, 1080);
    CHECK(TouchInput_GetDebugState().buttons[(int)TouchButton::Fire]);
}

TEST_CASE("TouchInput: safe area preserves staggered action column", "[input][touch]")
{
    TouchFixture fixture;
    TouchInput_TestSetGameplaySceneOverride(true, true);
    TouchInput_TestSetSafeArea(0.12f, 0.0f, 1.0f, 1.0f);

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 1, 0.12f, kActionButtonY));
    TouchInput_ProcessFrame(1920, 1080);
    CHECK_FALSE(TouchInput_GetDebugState().buttons[(int)TouchButton::Action]);

    TouchInput_Reset();
    TouchInput_TestSetGameplaySceneOverride(true, true);
    TouchInput_TestSetSafeArea(0.12f, 0.0f, 1.0f, 1.0f);

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 2, 0.16f, kActionButtonY));
    TouchInput_ProcessFrame(1920, 1080);
    CHECK(TouchInput_GetDebugState().buttons[(int)TouchButton::Action]);
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

TEST_CASE("TouchInput: action button is hidden outside gameplay", "[input][touch]")
{
    TouchFixture fixture;
    TouchInput_TestSetDirectTouchSceneOverride(true, true);

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 1, kActionButtonX, kActionButtonY));
    TouchInput_ProcessFrame(1920, 1080);

    TouchInputDebugState state = TouchInput_GetDebugState();
    CHECK_FALSE(state.buttons[(int)TouchButton::Action]);
}

TEST_CASE("TouchInput: map scene keeps equipment button for map toggle", "[input][touch]")
{
    TouchFixture fixture;
    TouchInput_TestSetMapSceneOverride(true, true);

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 1, kActionButtonX, kActionButtonY));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 2, kEquipmentButtonX, kEquipmentButtonY));
    TouchInput_ProcessFrame(1920, 1080);

    TouchInputDebugState state = TouchInput_GetDebugState();
    CHECK_FALSE(state.buttons[(int)TouchButton::Action]);
    CHECK(state.buttons[(int)TouchButton::Equipment]);
}

TEST_CASE("TouchInput: editor map hides equipment button", "[input][touch]")
{
    TouchFixture fixture;
    TouchInput_TestSetEditorMapSceneOverride(true, true);

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 1, kEquipmentButtonX, kEquipmentButtonY));
    TouchInput_ProcessFrame(1920, 1080);

    TouchInputDebugState state = TouchInput_GetDebugState();
    CHECK_FALSE(state.buttons[(int)TouchButton::Equipment]);
    CHECK(state.mapPrimaryActive);
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

TEST_CASE("TouchInput: direct touch scene one finger drives primary touch", "[input][touch]")
{
    TouchFixture fixture;
    TouchInput_TestSetDirectTouchSceneOverride(true, true);

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 1, 0.50f, 0.50f));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_MOTION, 1, 0.52f, 0.52f));
    TouchInput_ProcessFrame(1920, 1080);

    TouchInputDebugState state = TouchInput_GetDebugState();
    CHECK(state.mapPrimaryActive);
    CHECK_FALSE(state.mapGestureActive);
    CHECK_FALSE(state.moveActive);
    CHECK_FALSE(state.lookActive);
}

TEST_CASE("TouchInput: direct touch scene quick tap emits primary click on release", "[input][touch]")
{
    TouchFixture fixture;
    TouchInput_TestSetDirectTouchSceneOverride(true, true);
    GInput.mouse.FlushAndReset();

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 1, 0.50f, 0.50f));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_UP, 1, 0.50f, 0.50f));
    GInput.mouse.Update(GInput.cursor, 0, false,
                        Poseidon::Foundation::UITime((int)Poseidon::Foundation::GlobalTickCount()), nullptr);

    CHECK(GInput.mouse.buttonsToDo[0]);
    CHECK_FALSE(GInput.mouse.buttons[0] > 0.0f);
}

TEST_CASE("TouchInput: direct touch scene does not treat two fingers as map gesture", "[input][touch]")
{
    TouchFixture fixture;
    TouchInput_TestSetDirectTouchSceneOverride(true, true);

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 1, 0.30f, 0.40f));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 2, 0.50f, 0.40f));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_MOTION, 1, 0.32f, 0.42f));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_MOTION, 2, 0.56f, 0.42f));
    TouchInput_ProcessFrame(1920, 1080);

    TouchInputDebugState state = TouchInput_GetDebugState();
    CHECK_FALSE(state.mapPrimaryActive);
    CHECK_FALSE(state.mapGestureActive);
    CHECK_FALSE(state.moveActive);
    CHECK_FALSE(state.lookActive);
}

TEST_CASE("TouchInput: action button hold-drag emits action menu scroll steps", "[input][touch]")
{
    TouchFixture fixture;
    TouchInput_TestSetGameplaySceneOverride(true, true);
    GInput.keyboard.ForgetKeys();

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 1, kActionButtonX, kActionButtonY));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_MOTION, 1, kActionButtonX, kActionButtonY - 0.06f));
    TouchInput_ProcessFrame(1920, 1080);

    TouchInputDebugState state = TouchInput_GetDebugState();
    CHECK(state.buttons[(int)TouchButton::Action]);
    CHECK(state.actionScrollActive);
    CHECK(state.actionScrollSteps == 1);

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_MOTION, 1, kActionButtonX, kActionButtonY + 0.07f));
    TouchInput_ProcessFrame(1920, 1080);
    state = TouchInput_GetDebugState();
    CHECK(state.actionScrollSteps == -1);

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_UP, 1, kActionButtonX, kActionButtonY + 0.07f));
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

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 1, kActionButtonX, kActionButtonY));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_UP, 1, kActionButtonX, kActionButtonY));

    GInput.keyboard.Update(Poseidon::Foundation::GlobalTickCount(), 16, true);
    CHECK(GInput.keyboard.keysToDo[SDL_SCANCODE_RETURN]);
}

TEST_CASE("TouchInput: action hold with drag does not trigger action on release", "[input][touch]")
{
    TouchFixture fixture;
    TouchInput_TestSetGameplaySceneOverride(true, true);
    GInput.keyboard.ForgetKeys();

    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_DOWN, 1, kActionButtonX, kActionButtonY));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_MOTION, 1, kActionButtonX, kActionButtonY + 0.03f));
    TouchInput_HandleFingerEvent(Finger(SDL_EVENT_FINGER_UP, 1, kActionButtonX, kActionButtonY + 0.03f));

    GInput.keyboard.Update(Poseidon::Foundation::GlobalTickCount(), 16, true);
    CHECK_FALSE(GInput.keyboard.keysToDo[SDL_SCANCODE_RETURN]);
}
