#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_keycode.h>
#include <Poseidon/Input/InputDeviceConstants.hpp>
#include <Poseidon/Input/InputCode.hpp>
#include <Poseidon/Input/KeyInput.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace Poseidon;
TEST_CASE("SDL scancodes have expected values", "[input]")
{
    // Verify key scancodes are in valid range
    REQUIRE(SDL_SCANCODE_ESCAPE > 0);
    REQUIRE(SDL_SCANCODE_ESCAPE < SDL_SCANCODE_COUNT);
    REQUIRE(SDL_SCANCODE_W < SDL_SCANCODE_COUNT);
    REQUIRE(SDL_SCANCODE_RETURN < SDL_SCANCODE_COUNT);
    REQUIRE(SDL_SCANCODE_F12 < SDL_SCANCODE_COUNT);
    REQUIRE(SDL_SCANCODE_KP_0 < SDL_SCANCODE_COUNT);

    // Verify distinct common keys don't collide
    REQUIRE(SDL_SCANCODE_W != SDL_SCANCODE_A);
    REQUIRE(SDL_SCANCODE_W != SDL_SCANCODE_S);
    REQUIRE(SDL_SCANCODE_W != SDL_SCANCODE_D);
    REQUIRE(SDL_SCANCODE_ESCAPE != SDL_SCANCODE_RETURN);
    REQUIRE(SDL_SCANCODE_SPACE != SDL_SCANCODE_TAB);
}

TEST_CASE("Key arrays use SDL_SCANCODE_COUNT", "[input]")
{
    Input input = {};
    // Verify arrays are large enough for any scancode
    REQUIRE(sizeof(input.keyboard.keys) / sizeof(input.keyboard.keys[0]) == SDL_SCANCODE_COUNT);
    REQUIRE(sizeof(input.keyboard.keysToDo) / sizeof(input.keyboard.keysToDo[0]) == SDL_SCANCODE_COUNT);
    REQUIRE(sizeof(input.keyboard.keyPressed) / sizeof(input.keyboard.keyPressed[0]) == SDL_SCANCODE_COUNT);

    // Verify common scancodes are valid indices
    input.keyboard.keys[SDL_SCANCODE_W] = 1.0f;
    input.keyboard.keys[SDL_SCANCODE_ESCAPE] = 1.0f;
    input.keyboard.keys[SDL_SCANCODE_F12] = 1.0f;
    REQUIRE(input.keyboard.keys[SDL_SCANCODE_W] == 1.0f);
    REQUIRE(input.keyboard.keys[SDL_SCANCODE_ESCAPE] == 1.0f);
    REQUIRE(input.keyboard.keys[SDL_SCANCODE_F12] == 1.0f);
}

TEST_CASE("SDL keycodes have expected values", "[input]")
{
    REQUIRE(SDLK_SPACE == 0x20);
    REQUIRE(SDLK_RETURN == 0x0Du);
    REQUIRE(SDLK_ESCAPE == 0x1Bu);
    REQUIRE(SDLK_TAB == 0x09u);
    // SDLK_LEFT etc. have the SDLK_SCANCODE_MASK bit set
    REQUIRE(SDLK_LEFT != 0);
    REQUIRE(SDLK_UP != 0);
    REQUIRE(SDLK_LEFT != SDLK_RIGHT);
    REQUIRE(SDLK_UP != SDLK_DOWN);
}

TEST_CASE("Input device masks are distinct", "[input]")
{
    REQUIRE(INPUT_DEVICE_KEYBOARD == 0x00000000);
    REQUIRE(INPUT_DEVICE_MOUSE == 0x00010000);
    REQUIRE(INPUT_DEVICE_STICK == 0x00020000);
    REQUIRE(INPUT_DEVICE_STICK_AXIS == 0x00030000);
    REQUIRE(INPUT_DEVICE_STICK_POV == 0x00040000);
    REQUIRE(INPUT_DEVICE_MOUSE_AXIS == 0x00100000);
    REQUIRE((INPUT_DEVICE_MOUSE & INPUT_DEVICE_MASK) == INPUT_DEVICE_MOUSE);
    REQUIRE((INPUT_DEVICE_STICK & INPUT_DEVICE_MASK) == INPUT_DEVICE_STICK);
    REQUIRE(InputCode::MouseWheelUp().toLegacy() == 0x00100004);
    REQUIRE(InputCode::MouseWheelDown().toLegacy() == 0x00100005);
}
