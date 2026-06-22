// GamepadPage / GamepadTuningPage Provider tests — exercises the
// device filter (joystick-only, mirror of KbmPage's keyboard-only),
// row layout, and the toggle / slider scalar mapping for the tuning
// page.  Snapshots GInput state so parallel ctest runs stay clean.

#include <Poseidon/Input/InputDeviceConstants.hpp>
#include <Poseidon/Input/InputBinding.hpp>
#include <Poseidon/Input/InputCode.hpp>
#include <Poseidon/Input/InputProfile.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Input/KeyInput.hpp>
#include <Poseidon/UI/Options/GamepadPage.hpp>
#include <Poseidon/UI/Options/GamepadTuningPage.hpp>
#include <Poseidon/UI/Options/OptionsScrollList.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <SDL3/SDL_scancode.h>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

using namespace Poseidon;
namespace Poseidon
{
extern Input GInput;
}

namespace
{
struct GamepadStateSnapshot
{
    bool enabled;
    float deadzoneStick;
    float deadzoneTrigger;
    float lookSensitivity;

    GamepadStateSnapshot()
    {
        enabled = GInput.gamepad.enabled;
        deadzoneStick = GInput.gamepad.deadzoneStick;
        deadzoneTrigger = GInput.gamepad.deadzoneTrigger;
        lookSensitivity = GInput.gamepad.lookSensitivity;
    }
    ~GamepadStateSnapshot()
    {
        GInput.gamepad.enabled = enabled;
        GInput.gamepad.deadzoneStick = deadzoneStick;
        GInput.gamepad.deadzoneTrigger = deadzoneTrigger;
        GInput.gamepad.lookSensitivity = lookSensitivity;
    }
};

class TestableGamepadPage : public GamepadPage
{
  public:
    bool Filter(int code) const { return DeviceFilter(code); }
    OptionsScrollList::Provider& Provider() { return ProviderRef(); }
    bool Capture(ControlsCategory category, UserAction action, int packedCode, int modifier = -1)
    {
        return ApplyCaptureOverride(category, action, 0, packedCode, modifier);
    }
};

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

class TestableGamepadTuningPage : public GamepadTuningPage
{
  public:
    OptionsScrollList::Provider& Provider() { return ProviderRef(); }
};

void LoadMainMenuStringtable()
{
    static bool loaded = false;
    if (loaded)
    {
        SetLanguage("English");
        return;
    }

    const std::filesystem::path csv =
        std::filesystem::path(TESTS_ROOT_DIR) / "fixtures" / "stringtable" / "STRINGTABLE_MAINMENU.utf8.csv";
    LoadStringtable("global", csv.string().c_str(), 0.0f, true);
    SetLanguage("English");
    loaded = true;
}

const InputBinding* FindGamepadBinding(const InputProfile& profile, UserAction action, int axis)
{
    const int packed = INPUT_DEVICE_STICK_AXIS + axis;
    for (const InputBinding& binding : profile.GetBindingEntries(action))
        if (binding.code.toLegacy() == packed)
            return &binding;
    return nullptr;
}

void CheckAxisBinding(const InputProfile& profile, UserAction action, int axis, float scale, int modifier = -1)
{
    const InputBinding* binding = FindGamepadBinding(profile, action, axis);
    REQUIRE(binding != nullptr);
    CHECK(binding->scale == scale);
    const int actualModifier = binding->modifier.valid() ? binding->modifier.toLegacy() : -1;
    CHECK(actualModifier == modifier);
}
} // namespace

// ---------------------------------------------------------------------------
// GamepadPage: device filter — accepts joystick entries, rejects everything else.

TEST_CASE("GamepadPage: device filter accepts stick / pov / axis bits", "[UI][GamepadPage]")
{
    TestableGamepadPage page;
    CHECK(page.Filter(INPUT_DEVICE_STICK + 0));
    CHECK(page.Filter(INPUT_DEVICE_STICK + 7));
    CHECK(page.Filter(INPUT_DEVICE_STICK_POV + 0));
    CHECK(page.Filter(INPUT_DEVICE_STICK_AXIS + 0));
}

TEST_CASE("GamepadPage: device filter rejects keyboard scancodes and mouse buttons", "[UI][GamepadPage]")
{
    TestableGamepadPage page;
    CHECK_FALSE(page.Filter((int)SDL_SCANCODE_W));
    CHECK_FALSE(page.Filter((int)SDL_SCANCODE_LCTRL));
    CHECK_FALSE(page.Filter(INPUT_DEVICE_MOUSE + 1));
    CHECK_FALSE(page.Filter(INPUT_DEVICE_MOUSE + 2));
    CHECK_FALSE(page.Filter(-1));
    CHECK_FALSE(page.Filter(0));
}

TEST_CASE("GamepadPage: on-foot rows expose grouped stick bindings", "[UI][GamepadPage]")
{
    LoadMainMenuStringtable();

    TestableGamepadPage page;
    auto& p = page.Provider();

    bool movement = false;
    bool aim = false;
    bool freelook = false;
    for (int row = 1; row < p.RowCount(); ++row)
    {
        std::string label = p.RowLabel(row);
        if (label == "Movement")
        {
            movement = true;
            CHECK(std::string(p.BindingPrimary(row)) == "LS");
        }
        if (label == "Aim")
        {
            aim = true;
            CHECK(std::string(p.BindingPrimary(row)) == "RS");
        }
        if (label == "Freelook")
        {
            freelook = true;
            CHECK(std::string(p.BindingPrimary(row)) == "LB+RS");
        }

        CHECK(label != "Move back");
        CHECK(label != "Aim up");
        CHECK(label != "Look left");
    }

    CHECK(movement);
    CHECK(aim);
    CHECK(freelook);
}

TEST_CASE("GamepadPage: grouped stick labels use the selected category profile", "[UI][GamepadPage]")
{
    LoadMainMenuStringtable();

    auto& sub = InputSubsystem::Instance();
    ProfileSnapshot tankGunnerSnap(sub, InputContext::TankGunner);
    ProfileSnapshot infantrySnap(sub, InputContext::Infantry);

    sub.GetProfile(InputContext::TankGunner).ClearBindings(UAAimRight);
    sub.GetProfile(InputContext::TankGunner)
        .Bind(UAAimRight, InputBinding(InputCode::GamepadAx(0), InputCode{}, ActivationMode::OnHold, 1.0f));
    sub.GetProfile(InputContext::Infantry).ClearBindings(UAAimRight);
    sub.GetProfile(InputContext::Infantry)
        .Bind(UAAimRight, InputBinding(InputCode::GamepadAx(3), InputCode{}, ActivationMode::OnHold, 1.0f));

    TestableGamepadPage page;
    auto& p = page.Provider();
    p.SetRowValue(0, ControlsCategoryGunner);

    bool aim = false;
    for (int row = 1; row < p.RowCount(); ++row)
    {
        if (std::string(p.RowLabel(row)) != "Aim")
            continue;
        aim = true;
        CHECK(std::string(p.BindingPrimary(row)) == "LS");
    }
    CHECK(aim);

    p.SetRowValue(0, ControlsCategoryVehicles);
    CHECK(std::string(p.RowLabel(1)) != "Movement");
    CHECK(std::string(p.BindingPrimary(1)) != "LS");
}

TEST_CASE("GamepadPage: grouped movement capture writes whole axis pair to infantry profile", "[UI][GamepadPage]")
{
    auto& sub = InputSubsystem::Instance();
    ProfileSnapshot infantrySnap(sub, InputContext::Infantry);
    InputProfile& infantry = sub.GetProfile(InputContext::Infantry);
    infantry.ClearBindings(UAMoveForward);
    infantry.ClearBindings(UAMoveBack);
    infantry.ClearBindings(UAMoveLeft);
    infantry.ClearBindings(UAMoveRight);

    TestableGamepadPage page;
    REQUIRE(page.Capture(ControlsCategoryOnFoot, UAMoveForward, INPUT_DEVICE_STICK_AXIS + 3));

    CheckAxisBinding(infantry, UAMoveForward, 4, -1.0f);
    CheckAxisBinding(infantry, UAMoveBack, 4, 1.0f);
    CheckAxisBinding(infantry, UAMoveLeft, 3, -1.0f);
    CheckAxisBinding(infantry, UAMoveRight, 3, 1.0f);
}

TEST_CASE("GamepadPage: grouped aim capture writes whole axis pair to selected role profiles", "[UI][GamepadPage]")
{
    auto& sub = InputSubsystem::Instance();
    ProfileSnapshot tankGunnerSnap(sub, InputContext::TankGunner);
    ProfileSnapshot gunnerSnap(sub, InputContext::Gunner);
    InputProfile& tankGunner = sub.GetProfile(InputContext::TankGunner);
    InputProfile& gunner = sub.GetProfile(InputContext::Gunner);
    for (UserAction action : {UAAimUp, UAAimDown, UAAimLeft, UAAimRight})
    {
        tankGunner.ClearBindings(action);
        gunner.ClearBindings(action);
    }

    TestableGamepadPage page;
    REQUIRE(page.Capture(ControlsCategoryGunner, UAAimRight, INPUT_DEVICE_STICK_AXIS + 0));

    for (const InputProfile* profile : {&tankGunner, &gunner})
    {
        CheckAxisBinding(*profile, UAAimUp, 1, -1.0f);
        CheckAxisBinding(*profile, UAAimDown, 1, 1.0f);
        CheckAxisBinding(*profile, UAAimLeft, 0, -1.0f);
        CheckAxisBinding(*profile, UAAimRight, 0, 1.0f);
    }
}

TEST_CASE("GamepadPage: grouped freelook capture writes LB-modified right-stick pair", "[UI][GamepadPage]")
{
    auto& sub = InputSubsystem::Instance();
    ProfileSnapshot infantrySnap(sub, InputContext::Infantry);
    InputProfile& infantry = sub.GetProfile(InputContext::Infantry);
    for (UserAction action : {UALookAround, UALookUp, UALookDown, UALookLeft, UALookRight})
        infantry.ClearBindings(action);

    TestableGamepadPage page;
    REQUIRE(page.Capture(ControlsCategoryOnFoot, UALookAround, INPUT_DEVICE_STICK_AXIS + 3, INPUT_DEVICE_STICK + 4));

    REQUIRE(infantry.BindingCount(UALookAround) == 1);
    CHECK(infantry.GetBindingEntries(UALookAround)[0].code.toLegacy() == INPUT_DEVICE_STICK + 4);
    CheckAxisBinding(infantry, UALookUp, 4, -1.0f, INPUT_DEVICE_STICK + 4);
    CheckAxisBinding(infantry, UALookDown, 4, 1.0f, INPUT_DEVICE_STICK + 4);
    CheckAxisBinding(infantry, UALookLeft, 3, -1.0f, INPUT_DEVICE_STICK + 4);
    CheckAxisBinding(infantry, UALookRight, 3, 1.0f, INPUT_DEVICE_STICK + 4);
}

// ---------------------------------------------------------------------------
// GamepadTuningPage: 4 scalar rows + auto Close.

TEST_CASE("GamepadTuningPage: provider exposes 4 scalar rows + close", "[UI][GamepadTuningPage]")
{
    TestableGamepadTuningPage page;
    auto& p = page.Provider();
    CHECK(p.RowCount() == 5);
}

TEST_CASE("GamepadTuningPage: row labels", "[UI][GamepadTuningPage]")
{
    LoadMainMenuStringtable();

    TestableGamepadPage bindingsPage;
    TestableGamepadTuningPage page;
    auto& p = page.Provider();
    CHECK(std::string(bindingsPage.TitleText()) == "Gamepad");
    CHECK(std::string(page.TitleText()) == "Gamepad Tuning");
    CHECK(std::string(p.RowLabel(0)) == "Gamepad enabled");
    CHECK(std::string(p.RowLabel(1)) == "Stick deadzone");
    CHECK(std::string(p.RowLabel(2)) == "Trigger deadzone");
    CHECK(std::string(p.RowLabel(3)) == "Look sensitivity");
}

TEST_CASE("GamepadTuningPage: row descriptions localize from the main menu shard", "[UI][GamepadTuningPage]")
{
    LoadMainMenuStringtable();

    TestableGamepadTuningPage page;
    auto& p = page.Provider();
    CHECK(std::string(p.RowDescription(0)) ==
          "Master switch for gamepad input. Disable to ignore the controller entirely.");
    CHECK(std::string(p.RowDescription(1)) ==
          "Center deadzone for the analog sticks. Range 0% to 50% of full deflection.");
    CHECK(std::string(p.RowDescription(2)) == "Activation threshold for the analog triggers. Range 0% to 50%.");
    CHECK(std::string(p.RowDescription(3)) == "Right-stick look-aim sensitivity. Range 0.1x to 5.0x.");
}

TEST_CASE("GamepadTuningPage: row kinds - toggle stepper + 3 sliders + close action", "[UI][GamepadTuningPage]")
{
    TestableGamepadTuningPage page;
    auto& p = page.Provider();
    CHECK(p.RowKind(0) == OptionsScrollList::KindStepper); // enabled toggle
    CHECK(p.RowKind(1) == OptionsScrollList::KindSlider);  // stick deadzone
    CHECK(p.RowKind(2) == OptionsScrollList::KindSlider);  // trigger deadzone
    CHECK(p.RowKind(3) == OptionsScrollList::KindSlider);  // look sensitivity
    CHECK(p.RowKind(4) == OptionsScrollList::KindAction);  // Close
}

TEST_CASE("GamepadTuningPage: enabled toggle round-trips through GInput", "[UI][GamepadTuningPage]")
{
    GamepadStateSnapshot snap;
    TestableGamepadTuningPage page;
    auto& p = page.Provider();

    GInput.gamepad.enabled = true;
    CHECK(p.RowValue(0) == 1);
    p.SetRowValue(0, 0);
    CHECK(GInput.gamepad.enabled == false);
    p.SetRowValue(0, 1);
    CHECK(GInput.gamepad.enabled == true);

    // Out-of-range rejected.
    p.SetRowValue(0, 99);
    CHECK(GInput.gamepad.enabled == true);
}

TEST_CASE("GamepadTuningPage: deadzone slider maps 0..100 to 0.0..0.5", "[UI][GamepadTuningPage]")
{
    GamepadStateSnapshot snap;
    TestableGamepadTuningPage page;
    auto& p = page.Provider();

    p.SetRowValue(1, 0);
    CHECK(GInput.gamepad.deadzoneStick == 0.0f);
    p.SetRowValue(1, 100);
    CHECK(GInput.gamepad.deadzoneStick == 0.5f);
    p.SetRowValue(1, 50);
    CHECK(GInput.gamepad.deadzoneStick == 0.25f);

    p.SetRowValue(2, 20);
    CHECK(GInput.gamepad.deadzoneTrigger == 0.10f);

    // Negative / >100 clamp.
    p.SetRowValue(1, -5);
    CHECK(GInput.gamepad.deadzoneStick == 0.0f);
    p.SetRowValue(1, 200);
    CHECK(GInput.gamepad.deadzoneStick == 0.5f);
}

TEST_CASE("GamepadTuningPage: look sensitivity slider maps 0..100 to 0.1..5.0", "[UI][GamepadTuningPage]")
{
    GamepadStateSnapshot snap;
    TestableGamepadTuningPage page;
    auto& p = page.Provider();

    p.SetRowValue(3, 0);
    CHECK(GInput.gamepad.lookSensitivity == 0.1f);
    p.SetRowValue(3, 100);
    CHECK(GInput.gamepad.lookSensitivity == 5.0f);

    // Engine default 1.0 reads back at ~18% on the linear slider —
    // documents the (0.1..5.0) chosen range; default isn't at 50%
    // because the range is biased toward higher sensitivities.
    GInput.gamepad.lookSensitivity = 1.0f;
    int pct = p.RowValue(3);
    CHECK(pct >= 17);
    CHECK(pct <= 19);
}
