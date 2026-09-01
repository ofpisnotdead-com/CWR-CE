#include <Poseidon/Input/InputCode.hpp>
#include <Poseidon/Input/InputDeviceConstants.hpp>
#include <Poseidon/Input/JoystickState.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace Poseidon;

TEST_CASE("JoystickState: default construction zeroes everything", "[input][joystick]")
{
    JoystickState js;

    REQUIRE_FALSE(js.connected);
    REQUIRE(js.axisCount == 0);
    REQUIRE(js.buttonCount == 0);
    REQUIRE(js.hatCount == 0);

    for (int i = 0; i < N_RAW_JOYSTICK_AXES; i++)
        REQUIRE(js.axis[i] == 0.0f);
    for (int i = 0; i < N_RAW_JOYSTICK_BUTTONS; i++)
    {
        REQUIRE(js.buttons[i] == 0.0f);
        REQUIRE_FALSE(js.buttonsToDo[i]);
    }
    for (int i = 0; i < N_RAW_JOYSTICK_POV; i++)
    {
        REQUIRE_FALSE(js.pov[i]);
        REQUIRE_FALSE(js.povOld[i]);
        REQUIRE_FALSE(js.povToDo[i]);
    }

    REQUIRE(js.enabled);
    REQUIRE(js.deadzone == Catch::Approx(0.07f));
}

TEST_CASE("JoystickState: Clear resets live state but keeps edge history", "[input][joystick]")
{
    JoystickState js;
    js.axis[3] = 0.5f;
    js.buttons[2] = 1.0f;
    js.buttonsToDo[2] = true;
    js.pov[9] = true;
    js.povToDo[9] = true;
    js.povOld[9] = true;

    js.Clear();

    REQUIRE(js.axis[3] == 0.0f);
    REQUIRE(js.buttons[2] == 0.0f);
    REQUIRE_FALSE(js.buttonsToDo[2]);
    REQUIRE_FALSE(js.pov[9]);
    REQUIRE_FALSE(js.povToDo[9]);
    // povOld drives edge detection across frames and must survive the per-frame clear.
    REQUIRE(js.povOld[9]);
}

TEST_CASE("JoystickAxisSlot: four-axis stick lands on the original DirectInput slots", "[input][joystick]")
{
    // A four-axis stick reports X, Y, twist and throttle as SDL axes 0..3, and the
    // stock bindings address them as slots 0, 1, 5 and 6.
    REQUIRE(JoystickAxisSlot(4, 0) == 0); // X    -> AxisTurn
    REQUIRE(JoystickAxisSlot(4, 1) == 1); // Y    -> AxisDive
    REQUIRE(JoystickAxisSlot(4, 2) == 5); // Rz   -> AxisRudder
    REQUIRE(JoystickAxisSlot(4, 3) == 6); // slid -> AxisThrust
}

TEST_CASE("JoystickAxisSlot: six- and eight-axis devices keep DirectInput order", "[input][joystick]")
{
    for (int i = 0; i < 6; i++)
        REQUIRE(JoystickAxisSlot(6, i) == i);
    for (int i = 0; i < 8; i++)
        REQUIRE(JoystickAxisSlot(8, i) == i);

    // Three axes is X, Y and twist.
    REQUIRE(JoystickAxisSlot(3, 2) == 5);
}

TEST_CASE("JoystickAxisSlot: rejects out-of-range indices", "[input][joystick]")
{
    REQUIRE(JoystickAxisSlot(4, -1) == -1);
    REQUIRE(JoystickAxisSlot(4, 4) == -1);
    REQUIRE(JoystickAxisSlot(0, 0) == -1);
    // More axes than the engine stores still resolves the ones it can.
    REQUIRE(JoystickAxisSlot(12, 0) == 0);
    REQUIRE(JoystickAxisSlot(12, 7) == 7);
}

TEST_CASE("Joystick input codes occupy their own device space", "[input][joystick][InputCode]")
{
    auto btn = InputCode::JoystickBtn(6);
    REQUIRE(btn.device() == InputDevice::JoystickButton);
    REQUIRE(btn.code() == 6);

    auto axis = InputCode::JoystickAx(5);
    REQUIRE(axis.device() == InputDevice::JoystickAxis);
    REQUIRE(axis.code() == 5);

    auto pov = InputCode::JoystickPov(0, 4);
    REQUIRE(pov.device() == InputDevice::JoystickPOV);
    REQUIRE(pov.code() == 4);

    // A second hat continues past the first hat's eight directions.
    REQUIRE(InputCode::JoystickPov(1, 0).code() == 8);

    // Raw joystick codes must not collide with the gamepad ones, or a stick would
    // drive bindings meant for a pad.
    REQUIRE(btn.raw != InputCode::GamepadBtn(6).raw);
    REQUIRE(axis.raw != InputCode::GamepadAx(5).raw);
    REQUIRE(pov.raw != InputCode::GamepadPov(4).raw);

    REQUIRE(InputBindingIsRawJoystick(btn.toLegacy()));
    REQUIRE(InputBindingIsRawJoystick(axis.toLegacy()));
    REQUIRE(InputBindingIsRawJoystick(pov.toLegacy()));
    REQUIRE_FALSE(InputBindingIsRawJoystick(InputCode::GamepadBtn(6).toLegacy()));
}
