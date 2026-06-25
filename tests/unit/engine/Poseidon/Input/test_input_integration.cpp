#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Input/InputBinding.hpp>
#include <Poseidon/Input/InputContext.hpp>
#include <Poseidon/Input/InputProfile.hpp>
#include <Poseidon/Input/InputCode.hpp>
#include <Poseidon/Input/KeyInput.hpp>
#include <Poseidon/Input/UserAction.hpp>
#include <SDL3/SDL_scancode.h>
#include <catch2/catch_test_macros.hpp>

using namespace Poseidon;
namespace Poseidon
{
extern Input GInput;
}

namespace
{
class ProfileSnapshot
{
  public:
    ProfileSnapshot(InputSubsystem& sub, InputContext ctx) : sub_(sub), ctx_(ctx), saved_(sub.GetProfile(ctx)) {}
    ~ProfileSnapshot() { sub_.GetProfile(ctx_) = saved_; }

  private:
    InputSubsystem& sub_;
    InputContext ctx_;
    InputProfile saved_;
};

class GamepadSnapshot
{
  public:
    GamepadSnapshot() : saved_(GInput.gamepad) {}
    ~GamepadSnapshot() { GInput.gamepad = saved_; }

  private:
    GamepadState saved_;
};
} // namespace

TEST_CASE("InputSubsystem singleton returns same instance", "[input][integration]")
{
    auto& a = InputSubsystem::Instance();
    auto& b = InputSubsystem::Instance();
    REQUIRE(&a == &b);
}

TEST_CASE("InputSubsystem default context is Menu", "[input][integration]")
{
    auto& sub = InputSubsystem::Instance();
    REQUIRE(sub.GetContext() == InputContext::Menu);
}

TEST_CASE("InputSubsystem SetContext changes context", "[input][integration]")
{
    auto& sub = InputSubsystem::Instance();
    sub.SetContext(InputContext::Infantry);
    REQUIRE(sub.GetContext() == InputContext::Infantry);

    sub.SetContext(InputContext::HeliPilot);
    REQUIRE(sub.GetContext() == InputContext::HeliPilot);

    // Restore default
    sub.SetContext(InputContext::Menu);
}

TEST_CASE("InputSubsystem GetProfile returns valid profile for each context", "[input][integration]")
{
    auto& sub = InputSubsystem::Instance();

    InputContext contexts[] = {
        InputContext::Menu,       InputContext::Infantry,  InputContext::CarDriver,  InputContext::TankDriver,
        InputContext::TankGunner, InputContext::HeliPilot, InputContext::PlanePilot, InputContext::ShipDriver,
        InputContext::Gunner,     InputContext::Spectator, InputContext::Map,        InputContext::Chat,
        InputContext::Editor,
    };

    for (auto ctx : contexts)
    {
        auto& profile = sub.GetProfile(ctx);
        // Profile should be accessible without crash — binding count for any action >= 0
        REQUIRE(profile.BindingCount(UAMoveForward) >= 0);
    }
}

TEST_CASE("InputSubsystem profiles are independent", "[input][integration]")
{
    auto& sub = InputSubsystem::Instance();

    auto& menuProfile = sub.GetProfile(InputContext::Menu);
    auto& infantryProfile = sub.GetProfile(InputContext::Infantry);

    // Clear menu profile bindings for MoveForward
    menuProfile.ClearBindings(UAMoveForward);
    // Add a unique binding to menu
    menuProfile.Bind(UAMoveForward, InputCode::Key(SDL_SCANCODE_F1));

    // Infantry profile should not have been affected
    REQUIRE(!infantryProfile.HasBinding(UAMoveForward, InputCode::Key(SDL_SCANCODE_F1)));

    // Verify menu has it
    REQUIRE(menuProfile.HasBinding(UAMoveForward, InputCode::Key(SDL_SCANCODE_F1)));

    // Clean up
    menuProfile.ClearBindings(UAMoveForward);
}

TEST_CASE("InputSubsystem context round-trip preserves profile state", "[input][integration]")
{
    auto& sub = InputSubsystem::Instance();

    // Set up infantry profile with a specific binding
    sub.SetContext(InputContext::Infantry);
    auto& infantryProfile = sub.GetProfile(InputContext::Infantry);
    infantryProfile.ClearBindings(UAFire);
    infantryProfile.Bind(UAFire, InputCode::Key(SDL_SCANCODE_SPACE));

    // Switch away and back
    sub.SetContext(InputContext::Menu);
    sub.SetContext(InputContext::Infantry);

    // Infantry profile state should be preserved
    auto& profileAfter = sub.GetProfile(InputContext::Infantry);
    REQUIRE(profileAfter.HasBinding(UAFire, InputCode::Key(SDL_SCANCODE_SPACE)));

    // Clean up
    profileAfter.ClearBindings(UAFire);
    sub.SetContext(InputContext::Menu);
}

TEST_CASE("InputSubsystem context profile action queries preserve analog values", "[input][integration]")
{
    auto& sub = InputSubsystem::Instance();
    ProfileSnapshot infantrySnap(sub, InputContext::Infantry);
    ProfileSnapshot carSnap(sub, InputContext::CarDriver);
    GamepadSnapshot gamepadSnap;

    auto& infantry = sub.GetProfile(InputContext::Infantry);
    auto& car = sub.GetProfile(InputContext::CarDriver);
    infantry.ClearBindings(UAAxisTurn);
    car.ClearBindings(UAAxisTurn);
    infantry.Bind(UAAxisTurn, InputBinding(InputCode::GamepadAx(0), {}, ActivationMode::OnHold, 1.0f));
    car.Bind(UAAxisTurn, InputBinding(InputCode::GamepadAx(0), {}, ActivationMode::OnHold, -1.0f));

    GInput.gamepad.stickAxis[0] = 0.625f;

    REQUIRE(sub.GetAction(InputContext::Infantry, UAAxisTurn, false) == 0.625f);
    REQUIRE(sub.GetAction(InputContext::CarDriver, UAAxisTurn, false) == -0.625f);
}

TEST_CASE("InputSubsystem context profile action queries honor gamepad modifiers", "[input][integration]")
{
    auto& sub = InputSubsystem::Instance();
    ProfileSnapshot snap(sub, InputContext::Infantry);
    GamepadSnapshot gamepadSnap;

    auto& infantry = sub.GetProfile(InputContext::Infantry);
    infantry.ClearBindings(UAAimRight);
    infantry.Bind(UAAimRight, InputBinding(InputCode::GamepadAx(3), InputCode::GamepadBtn(6)));

    GInput.gamepad.stickAxis[3] = 0.75f;
    GInput.gamepad.stickButtons[6] = 0.0f;
    REQUIRE(sub.GetAction(InputContext::Infantry, UAAimRight, false) == 0.0f);

    GInput.gamepad.stickButtons[6] = 1.0f;
    REQUIRE(sub.GetAction(InputContext::Infantry, UAAimRight, false) == 0.75f);
}

TEST_CASE("InputSubsystem default profiles expose infantry sticks as context bindings", "[input][integration]")
{
    auto& sub = InputSubsystem::Instance();
    ProfileSnapshot infantrySnap(sub, InputContext::Infantry);
    ProfileSnapshot carSnap(sub, InputContext::CarDriver);
    GamepadSnapshot gamepadSnap;
    InputContext savedContext = sub.GetContext();

    sub.LoadDefaultProfiles();
    sub.SetContext(InputContext::Infantry);

    GInput.gamepad.stickAxis[0] = -0.7f;
    GInput.gamepad.stickAxis[1] = -0.5f;
    sub.Update();

    CHECK(sub.GetMoveLeft() == 0.7f);
    CHECK(sub.GetMoveRight() == 0.0f);
    CHECK(sub.GetMoveForward() == 0.5f);
    CHECK(sub.GetMoveBack() == 0.0f);

    GInput.gamepad.stickAxis[0] = 0.8f;
    GInput.gamepad.stickAxis[1] = 0.6f;
    sub.Update();

    CHECK(sub.GetMoveLeft() == 0.0f);
    CHECK(sub.GetMoveRight() == 0.8f);
    CHECK(sub.GetMoveForward() == 0.0f);
    CHECK(sub.GetMoveBack() == 0.6f);

    sub.SetContext(InputContext::CarDriver);
    GInput.gamepad.stickAxis[0] = 0.625f;
    CHECK(sub.GetAction(UATurnLeft, false) == 0.0f);
    CHECK(sub.GetAction(UATurnRight, false) == 0.625f);
    CHECK(sub.GetStickLeft() == -0.625f);

    GInput.gamepad.stickAxis[0] = 0.0f;
    GInput.gamepad.stickAxis[2] = 0.5f;
    CHECK(sub.GetAction(UAMoveForward, false) == 0.5f);
    CHECK(sub.GetStickForward() == 0.5f);

    GInput.gamepad.stickAxis[2] = 0.0f;
    GInput.gamepad.stickAxis[5] = 0.75f;
    CHECK(sub.GetAction(UAMoveBack, false) == 0.75f);
    CHECK(sub.GetStickForward() == -0.75f);

    sub.SetContext(savedContext);
}

TEST_CASE("InputSubsystem default profiles expose LB plus right stick freelook", "[input][integration]")
{
    auto& sub = InputSubsystem::Instance();
    ProfileSnapshot infantrySnap(sub, InputContext::Infantry);
    GamepadSnapshot gamepadSnap;

    sub.LoadDefaultProfiles();

    GInput.gamepad.stickAxis[3] = 0.75f;
    GInput.gamepad.stickButtons[4] = 0.0f;
    CHECK(sub.GetAction(InputContext::Infantry, UALookRight, false) == 0.0f);

    GInput.gamepad.stickButtons[4] = 1.0f;
    CHECK(sub.GetAction(InputContext::Infantry, UALookRight, false) == 0.75f);
    CHECK(sub.GetAction(InputContext::Infantry, UALookAround, false) == 1.0f);
}

TEST_CASE("InputSubsystem context profile action edges are context-local", "[input][integration]")
{
    auto& sub = InputSubsystem::Instance();
    ProfileSnapshot menuSnap(sub, InputContext::Menu);
    ProfileSnapshot infantrySnap(sub, InputContext::Infantry);
    InputContext savedContext = sub.GetContext();
    bool savedActionDone = GInput.actionDone[UAFire];
    float savedC = GInput.keyboard.keys[SDL_SCANCODE_C];
    float savedV = GInput.keyboard.keys[SDL_SCANCODE_V];
    bool savedCToDo = GInput.keyboard.keysToDo[SDL_SCANCODE_C];
    bool savedVToDo = GInput.keyboard.keysToDo[SDL_SCANCODE_V];

    auto& menu = sub.GetProfile(InputContext::Menu);
    auto& infantry = sub.GetProfile(InputContext::Infantry);
    menu.ClearBindings(UAFire);
    infantry.ClearBindings(UAFire);
    menu.Bind(UAFire, InputCode::Key(SDL_SCANCODE_C));
    infantry.Bind(UAFire, InputCode::Key(SDL_SCANCODE_V));

    GInput.actionDone[UAFire] = false;
    GInput.keyboard.keys[SDL_SCANCODE_C] = 1.0f;
    GInput.keyboard.keys[SDL_SCANCODE_V] = 1.0f;
    GInput.keyboard.keysToDo[SDL_SCANCODE_C] = true;
    GInput.keyboard.keysToDo[SDL_SCANCODE_V] = true;

    sub.SetContext(InputContext::Menu);
    CHECK(sub.GetActionToDo(UAFire, true, false));
    CHECK_FALSE(sub.GetActionToDo(UAFire, true, false));

    sub.SetContext(InputContext::Infantry);
    CHECK(sub.GetActionToDo(UAFire, true, false));
    CHECK_FALSE(sub.GetActionToDo(UAFire, true, false));

    sub.SetContext(savedContext);
    GInput.actionDone[UAFire] = savedActionDone;
    GInput.keyboard.keys[SDL_SCANCODE_C] = savedC;
    GInput.keyboard.keys[SDL_SCANCODE_V] = savedV;
    GInput.keyboard.keysToDo[SDL_SCANCODE_C] = savedCToDo;
    GInput.keyboard.keysToDo[SDL_SCANCODE_V] = savedVToDo;
}

TEST_CASE("InputSubsystem Update computes movement from active context profile", "[input][integration]")
{
    auto& sub = InputSubsystem::Instance();
    ProfileSnapshot infantrySnap(sub, InputContext::Infantry);
    InputContext savedContext = sub.GetContext();
    float savedW = GInput.keyboard.keys[SDL_SCANCODE_W];
    AutoArray<int> savedKeys = GInput.userKeys[UAMoveForward];

    auto& infantry = sub.GetProfile(InputContext::Infantry);
    infantry.ClearBindings(UAMoveForward);
    infantry.Bind(UAMoveForward, InputCode::Key(SDL_SCANCODE_W));
    GInput.userKeys[UAMoveForward].Resize(0);

    sub.SetContext(InputContext::Infantry);
    GInput.keyboard.keys[SDL_SCANCODE_W] = 1.0f;
    sub.Update();

    CHECK(sub.GetMoveForward() == 1.0f);

    GInput.keyboard.keys[SDL_SCANCODE_W] = savedW;
    GInput.userKeys[UAMoveForward] = savedKeys;
    sub.SetContext(savedContext);
}

TEST_CASE("InputSubsystem Update isolates vehicle movement from infantry bindings", "[input][integration]")
{
    auto& sub = InputSubsystem::Instance();
    ProfileSnapshot infantrySnap(sub, InputContext::Infantry);
    ProfileSnapshot carSnap(sub, InputContext::CarDriver);
    InputContext savedContext = sub.GetContext();
    float savedW = GInput.keyboard.keys[SDL_SCANCODE_W];
    float savedH = GInput.keyboard.keys[SDL_SCANCODE_H];
    bool savedHDoubleTapActive = GInput.keyboard.keysDoubleTapActive[SDL_SCANCODE_H];
    AutoArray<int> savedKeys = GInput.userKeys[UAMoveForward];

    auto& infantry = sub.GetProfile(InputContext::Infantry);
    auto& car = sub.GetProfile(InputContext::CarDriver);
    infantry.ClearBindings(UAMoveForward);
    car.ClearBindings(UAMoveForward);
    infantry.Bind(UAMoveForward, InputCode::Key(SDL_SCANCODE_W));
    car.Bind(UAMoveForward, InputCode::FromLegacy(InputBindingDoubleTapCode((int)SDL_SCANCODE_H)));
    GInput.userKeys[UAMoveForward].Resize(0);

    sub.SetContext(InputContext::CarDriver);
    GInput.keyboard.keys[SDL_SCANCODE_W] = 1.0f;
    GInput.keyboard.keys[SDL_SCANCODE_H] = 0.0f;
    GInput.keyboard.keysDoubleTapActive[SDL_SCANCODE_H] = false;
    sub.Update();
    CHECK(sub.GetMoveForward() == 0.0f);

    GInput.keyboard.keys[SDL_SCANCODE_W] = 0.0f;
    GInput.keyboard.keys[SDL_SCANCODE_H] = 1.0f;
    GInput.keyboard.keysDoubleTapActive[SDL_SCANCODE_H] = true;
    sub.Update();
    CHECK(sub.GetMoveForward() == 1.0f);

    GInput.keyboard.keys[SDL_SCANCODE_W] = savedW;
    GInput.keyboard.keys[SDL_SCANCODE_H] = savedH;
    GInput.keyboard.keysDoubleTapActive[SDL_SCANCODE_H] = savedHDoubleTapActive;
    GInput.userKeys[UAMoveForward] = savedKeys;
    sub.SetContext(savedContext);
}

TEST_CASE("InputSubsystem key queries on zeroed GInput", "[input][integration]")
{
    auto& sub = InputSubsystem::Instance();

    // GInput is zero-initialized in test exe, so all keys should be "up"
    REQUIRE_FALSE(sub.IsKeyDown(SDL_SCANCODE_A));
    REQUIRE_FALSE(sub.IsKeyPressed(SDL_SCANCODE_A));
    REQUIRE(sub.GetKeyValue(SDL_SCANCODE_A) == 0.0f);
    REQUIRE_FALSE(sub.ConsumeKeyPress(SDL_SCANCODE_A));

    // Out-of-range scancodes should be safe
    REQUIRE_FALSE(sub.IsKeyDown(static_cast<SDL_Scancode>(-1)));
    REQUIRE(sub.GetKeyValue(static_cast<SDL_Scancode>(9999)) == 0.0f);
}

TEST_CASE("InputSubsystem mouse queries on zeroed GInput", "[input][integration]")
{
    auto& sub = InputSubsystem::Instance();

    REQUIRE_FALSE(sub.IsMouseLeftDown());
    REQUIRE_FALSE(sub.IsMouseRightDown());
    REQUIRE_FALSE(sub.IsMouseMiddleDown());
    REQUIRE_FALSE(sub.IsMouseButtonDown(0));
    REQUIRE(sub.GetMouseDeltaX() == 0.0f);
    REQUIRE(sub.GetMouseDeltaY() == 0.0f);
    REQUIRE(sub.GetMouseWheel() == 0.0f);
    REQUIRE(sub.GetCursorX() == 0.0f);
    REQUIRE(sub.GetCursorY() == 0.0f);
}

TEST_CASE("InputSubsystem gamepad queries on zeroed GInput", "[input][integration]")
{
    auto& sub = InputSubsystem::Instance();

    REQUIRE(sub.GetGamepadAxis(0) == 0.0f);
    REQUIRE_FALSE(sub.IsGamepadButtonDown(0));
    REQUIRE_FALSE(sub.IsGamepadButtonPressed(0));

    // Out-of-range
    REQUIRE(sub.GetGamepadAxis(-1) == 0.0f);
    REQUIRE(sub.GetGamepadAxis(99) == 0.0f);
    REQUIRE_FALSE(sub.IsGamepadButtonDown(-1));
}

// --- Computed movement state tests ---

TEST_CASE("InputSubsystem Update zeroes movement state on zeroed GInput", "[input][integration]")
{
    auto& sub = InputSubsystem::Instance();
    sub.Update();

    REQUIRE(sub.GetMoveForward() == 0.0f);
    REQUIRE(sub.GetMoveFastForward() == 0.0f);
    REQUIRE(sub.GetMoveSlowForward() == 0.0f);
    REQUIRE(sub.GetMoveBack() == 0.0f);
    REQUIRE(sub.GetMoveLeft() == 0.0f);
    REQUIRE(sub.GetMoveRight() == 0.0f);
    REQUIRE(sub.GetMoveUp() == 0.0f);
    REQUIRE(sub.GetMoveDown() == 0.0f);
    REQUIRE(sub.GetTurnLeft() == 0.0f);
    REQUIRE(sub.GetTurnRight() == 0.0f);
    REQUIRE_FALSE(sub.IsFiring());
    REQUIRE_FALSE(sub.IsFirePressed());
}

TEST_CASE("InputSubsystem Update resets freelookChanged each frame", "[input][integration]")
{
    auto& sub = InputSubsystem::Instance();

    // First update with zeroed input — no freelook change
    sub.Update();
    REQUIRE_FALSE(sub.FreelookChanged());

    // Subsequent update — still no change
    sub.Update();
    REQUIRE_FALSE(sub.FreelookChanged());
}

TEST_CASE("InputSubsystem lookAround defaults to off", "[input][integration]")
{
    auto& sub = InputSubsystem::Instance();
    sub.Update();

    REQUIRE_FALSE(sub.IsLookAroundEnabled());
    REQUIRE_FALSE(sub.IsLookAroundToggled());
}

// --- Joystick/activity/focus/fire/cursor tests (Phase 2H API) ---

TEST_CASE("InputSubsystem joystick axes return zero on zeroed GInput", "[input][integration]")
{
    auto& sub = InputSubsystem::Instance();

    REQUIRE(sub.GetStickForward() == 0.0f);
    REQUIRE(sub.GetStickLeft() == 0.0f);
    REQUIRE(sub.GetStickThrust() == 0.0f);
    REQUIRE(sub.GetStickRudder() == 0.0f);
}

TEST_CASE("InputSubsystem joystick activity queries on zeroed GInput", "[input][integration]")
{
    auto& sub = InputSubsystem::Instance();

    REQUIRE_FALSE(sub.IsJoystickActive());
    REQUIRE_FALSE(sub.IsJoystickThrustActive());
}

TEST_CASE("InputSubsystem fire/mouse focus queries on zeroed GInput", "[input][integration]")
{
    auto& sub = InputSubsystem::Instance();

    REQUIRE_FALSE(sub.GetFire());
    REQUIRE_FALSE(sub.GetFireToDo());
    REQUIRE_FALSE(sub.GetMouseL());
    REQUIRE_FALSE(sub.GetMouseR());
    REQUIRE_FALSE(sub.GetMouseLToDo());
    REQUIRE_FALSE(sub.GetMouseRToDo());
    REQUIRE_FALSE(sub.IsMouseLeftPressed());
    REQUIRE_FALSE(sub.IsMouseRightPressed());
}

TEST_CASE("InputSubsystem cursor consume returns zero and clears", "[input][integration]")
{
    auto& sub = InputSubsystem::Instance();

    REQUIRE(sub.ConsumeCursorDeltaX() == 0.0f);
    REQUIRE(sub.ConsumeCursorDeltaY() == 0.0f);
    REQUIRE(sub.ConsumeCursorScroll() == 0.0f);
}

TEST_CASE("InputSubsystem config settings round-trip", "[input][integration]")
{
    auto& sub = InputSubsystem::Instance();

    bool origRev = sub.IsReverseMouse();
    sub.SetReverseMouse(!origRev);
    REQUIRE(sub.IsReverseMouse() == !origRev);
    sub.ToggleReverseMouse();
    REQUIRE(sub.IsReverseMouse() == origRev);

    bool origJoy = sub.IsJoystickEnabled();
    sub.SetJoystickEnabled(!origJoy);
    REQUIRE(sub.IsJoystickEnabled() == !origJoy);
    sub.ToggleJoystickEnabled();
    REQUIRE(sub.IsJoystickEnabled() == origJoy);

    bool origRevJoy = sub.IsReverseJoystick();
    sub.SetReverseJoystick(!origRevJoy);
    REQUIRE(sub.IsReverseJoystick() == !origRevJoy);
    sub.ToggleReverseJoystick();
    REQUIRE(sub.IsReverseJoystick() == origRevJoy);

    bool origButtons = sub.IsMouseButtonsReversed();
    sub.SetMouseButtonsReversed(!origButtons);
    REQUIRE(sub.IsMouseButtonsReversed() == !origButtons);
    sub.ToggleMouseButtonsReversed();
    REQUIRE(sub.IsMouseButtonsReversed() == origButtons);

    float origSensX = sub.GetMouseSensitivityX();
    sub.SetMouseSensitivityX(1.5f);
    REQUIRE(sub.GetMouseSensitivityX() == 1.5f);
    sub.SetMouseSensitivityX(origSensX);

    float origSensY = sub.GetMouseSensitivityY();
    sub.SetMouseSensitivityY(0.75f);
    REQUIRE(sub.GetMouseSensitivityY() == 0.75f);
    sub.SetMouseSensitivityY(origSensY);
}

TEST_CASE("InputSubsystem binding detection defaults inactive", "[input][integration]")
{
    auto& sub = InputSubsystem::Instance();

    REQUIRE_FALSE(sub.GetMouseButtonToDo(1));
    REQUIRE_FALSE(sub.GetStickButtonToDo(0));
    REQUIRE_FALSE(sub.GetStickPovToDo(0));
    REQUIRE_FALSE(sub.ConsumeAxisBigActive(0));
}

TEST_CASE("InputSubsystem synthetic gamepad edges consume once", "[input][integration]")
{
    auto& sub = InputSubsystem::Instance();

    REQUIRE_FALSE(sub.ConsumeSyntheticStickButton(0));
    REQUIRE_FALSE(sub.ConsumeSyntheticStickPov(4));

    sub.SetSyntheticStickButton(0, true);
    sub.SetSyntheticStickPov(4, true);

    REQUIRE(sub.ConsumeSyntheticStickButton(0));
    REQUIRE(sub.ConsumeSyntheticStickPov(4));
    REQUIRE_FALSE(sub.ConsumeSyntheticStickButton(0));
    REQUIRE_FALSE(sub.ConsumeSyntheticStickPov(4));
}

TEST_CASE("InputSubsystem SaveKeys does not crash", "[input][integration]")
{
    auto& sub = InputSubsystem::Instance();
    sub.SaveKeys(); // just verify no crash
}

TEST_CASE("InputSubsystem packed key queries on zeroed GInput", "[input][integration]")
{
    auto& sub = InputSubsystem::Instance();

    REQUIRE(sub.GetKey(0) == 0.0f);
    REQUIRE_FALSE(sub.GetKeyToDo(0));
}

TEST_CASE("InputSubsystem cheat system on zeroed GInput", "[input][integration]")
{
    auto& sub = InputSubsystem::Instance();

    REQUIRE(sub.CheatActivated() == 0);
    sub.CheatServed(); // should not crash
    sub.ForgetKeys();  // should not crash
}

TEST_CASE("InputSubsystem activity timing on zeroed GInput", "[input][integration]")
{
    auto& sub = InputSubsystem::Instance();
    sub.ForgetKeys();

    // After Init(), keyboard cursor is at uiTime, mouse cursor is at uiTime-1
    // So keyboard IS more recent than mouse by default
    REQUIRE(sub.IsKeyboardCursorMoreRecent());
    // Mouse cursor was set to exactly uiTime-1, which is at the boundary (>= check)
    REQUIRE(sub.IsMouseCursorRecentlyActive());
}

TEST_CASE("InputSubsystem activity timestamp marking", "[input][integration]")
{
    auto& sub = InputSubsystem::Instance();

    // Mark various activity timestamps — should not crash
    sub.MarkKeyboardMoveActive();
    sub.MarkKeyboardTurnActive();
    sub.MarkKeyboardCursorActive();
    sub.MarkKeyboardThrustActive();
    sub.MarkJoystickMoveActive();
    sub.MarkJoystickThrustActive();

    // After marking keyboard cursor, it should be more recent than mouse
    REQUIRE(sub.IsKeyboardCursorMoreRecent());
}

TEST_CASE("InputSubsystem joystick enabled and axis activity", "[input][integration]")
{
    auto& sub = InputSubsystem::Instance();

    // Joystick enabled state delegates to GInput
    // Default depends on Init() — just check it doesn't crash
    (void)sub.IsJoystickEnabled();

    // IsActionBoundToRecentAxis with no bindings should return false
    REQUIRE_FALSE(sub.IsActionBoundToRecentAxis(UAAxisTurn));
    REQUIRE_FALSE(sub.IsActionBoundToRecentAxis(UAAxisThrust));
}
