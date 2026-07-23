#include <Poseidon/Input/GamepadState.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace Poseidon;
TEST_CASE("GamepadState: default construction zeroes everything", "[input][gamepad]")
{
    GamepadState gs;

    for (int i = 0; i < N_JOYSTICK_POV; i++)
    {
        REQUIRE(gs.stickPov[i] == false);
        REQUIRE(gs.stickPovOld[i] == false);
        REQUIRE(gs.stickPovToDo[i] == false);
    }
    for (int i = 0; i < N_JOYSTICK_BUTTONS; i++)
    {
        REQUIRE(gs.stickButtons[i] == 0.0f);
        REQUIRE(gs.stickButtonsToDo[i] == false);
    }
    for (int i = 0; i < N_JOYSTICK_AXES; i++)
    {
        REQUIRE(gs.stickAxis[i] == 0.0f);
        REQUIRE(gs.jAxisLast[i] == 0);
        REQUIRE(gs.jAxisBigLast[i] == 0);
    }

    REQUIRE(gs.enabled == true);
    REQUIRE_FALSE(gs.reverseYStick);
    REQUIRE(gs.deadzoneStick == Catch::Approx(0.21f));
    REQUIRE(gs.deadzoneTrigger == Catch::Approx(0.10f));
    REQUIRE(gs.lookSensitivity == Catch::Approx(1.0f));
}
