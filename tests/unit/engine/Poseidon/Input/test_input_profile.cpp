#include <Poseidon/Input/InputProfile.hpp>
#include <Poseidon/Input/InputCode.hpp>
#include <Poseidon/Input/InputDeviceConstants.hpp>
#include <Poseidon/Input/UserAction.hpp>
#include <SDL3/SDL_scancode.h>
#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace Poseidon;
TEST_CASE("InputProfile default construction has empty bindings", "[input][InputProfile]")
{
    InputProfile profile;
    for (int i = 0; i < UAN; i++)
    {
        REQUIRE(profile.GetBindings(static_cast<UserAction>(i)).empty());
        REQUIRE(profile.BindingCount(static_cast<UserAction>(i)) == 0);
    }
}

TEST_CASE("InputProfile Bind adds an input code", "[input][InputProfile]")
{
    InputProfile profile;
    auto key = InputCode::Key(SDL_SCANCODE_W);
    profile.Bind(UAMoveForward, key);

    REQUIRE(profile.BindingCount(UAMoveForward) == 1);
    REQUIRE(profile.HasBinding(UAMoveForward, key));
    REQUIRE(profile.GetBindings(UAMoveForward)[0] == key);
}

TEST_CASE("InputProfile Bind same code twice doesn't duplicate", "[input][InputProfile]")
{
    InputProfile profile;
    auto key = InputCode::Key(SDL_SCANCODE_W);
    profile.Bind(UAMoveForward, key);
    profile.Bind(UAMoveForward, key);

    REQUIRE(profile.BindingCount(UAMoveForward) == 1);
}

TEST_CASE("InputProfile detailed bindings preserve modifier and analog scale", "[input][InputProfile]")
{
    InputProfile profile;
    InputBinding bare(InputCode::GamepadAx(0), {}, ActivationMode::OnHold, 1.0f);
    InputBinding modified(InputCode::GamepadAx(0), InputCode::GamepadBtn(6), ActivationMode::OnHold, -1.0f);

    profile.Bind(UAAxisTurn, bare);
    profile.Bind(UAAxisTurn, modified);

    REQUIRE(profile.BindingCount(UAAxisTurn) == 2);
    REQUIRE(profile.HasBinding(UAAxisTurn, bare));
    REQUIRE(profile.HasBinding(UAAxisTurn, modified));
    REQUIRE(profile.GetBindings(UAAxisTurn).size() == 2);
    REQUIRE(profile.GetBindingEntries(UAAxisTurn)[1].modifier == InputCode::GamepadBtn(6));
    REQUIRE(profile.GetBindingEntries(UAAxisTurn)[1].scale == -1.0f);
}

TEST_CASE("InputProfile detailed binding duplicate check includes modifier and scale", "[input][InputProfile]")
{
    InputProfile profile;
    InputBinding modified(InputCode::Key(SDL_SCANCODE_W), InputCode::Key(SDL_SCANCODE_LCTRL));

    profile.Bind(UAMoveForward, modified);
    profile.Bind(UAMoveForward, modified);
    profile.Bind(UAMoveForward, InputBinding(InputCode::Key(SDL_SCANCODE_W), InputCode::Key(SDL_SCANCODE_LSHIFT)));

    REQUIRE(profile.BindingCount(UAMoveForward) == 2);
}

TEST_CASE("InputProfile multiple bindings per action", "[input][InputProfile]")
{
    InputProfile profile;
    auto w = InputCode::Key(SDL_SCANCODE_W);
    auto up = InputCode::Key(SDL_SCANCODE_UP);
    profile.Bind(UAMoveForward, w);
    profile.Bind(UAMoveForward, up);

    REQUIRE(profile.BindingCount(UAMoveForward) == 2);
    REQUIRE(profile.HasBinding(UAMoveForward, w));
    REQUIRE(profile.HasBinding(UAMoveForward, up));
}

TEST_CASE("InputProfile Unbind removes a specific code", "[input][InputProfile]")
{
    InputProfile profile;
    auto w = InputCode::Key(SDL_SCANCODE_W);
    auto up = InputCode::Key(SDL_SCANCODE_UP);
    profile.Bind(UAMoveForward, w);
    profile.Bind(UAMoveForward, up);
    profile.Unbind(UAMoveForward, w);

    REQUIRE(profile.BindingCount(UAMoveForward) == 1);
    REQUIRE(!profile.HasBinding(UAMoveForward, w));
    REQUIRE(profile.HasBinding(UAMoveForward, up));
}

TEST_CASE("InputProfile Unbind non-existent code is safe", "[input][InputProfile]")
{
    InputProfile profile;
    auto w = InputCode::Key(SDL_SCANCODE_W);
    auto s = InputCode::Key(SDL_SCANCODE_S);
    profile.Bind(UAMoveForward, w);
    profile.Unbind(UAMoveForward, s);

    REQUIRE(profile.BindingCount(UAMoveForward) == 1);
    REQUIRE(profile.HasBinding(UAMoveForward, w));
}

TEST_CASE("InputProfile ClearBindings empties one action", "[input][InputProfile]")
{
    InputProfile profile;
    profile.Bind(UAMoveForward, InputCode::Key(SDL_SCANCODE_W));
    profile.Bind(UAMoveBack, InputCode::Key(SDL_SCANCODE_S));
    profile.ClearBindings(UAMoveForward);

    REQUIRE(profile.BindingCount(UAMoveForward) == 0);
    REQUIRE(profile.BindingCount(UAMoveBack) == 1);
}

TEST_CASE("InputProfile ClearAll empties everything", "[input][InputProfile]")
{
    InputProfile profile;
    profile.Bind(UAMoveForward, InputCode::Key(SDL_SCANCODE_W));
    profile.Bind(UAMoveBack, InputCode::Key(SDL_SCANCODE_S));
    profile.Bind(UAFire, InputCode::MouseButton(0));
    profile.ClearAll();

    for (int i = 0; i < UAN; i++)
        REQUIRE(profile.BindingCount(static_cast<UserAction>(i)) == 0);
}

TEST_CASE("InputProfile HasBinding returns correct values", "[input][InputProfile]")
{
    InputProfile profile;
    auto w = InputCode::Key(SDL_SCANCODE_W);
    auto s = InputCode::Key(SDL_SCANCODE_S);

    REQUIRE(!profile.HasBinding(UAMoveForward, w));
    profile.Bind(UAMoveForward, w);
    REQUIRE(profile.HasBinding(UAMoveForward, w));
    REQUIRE(!profile.HasBinding(UAMoveForward, s));
    REQUIRE(!profile.HasBinding(UAMoveBack, w));
}

TEST_CASE("InputProfile GetBindings returns empty for out-of-range", "[input][InputProfile]")
{
    InputProfile profile;
    auto outOfRange = static_cast<UserAction>(UAN + 10);
    const auto& bindings = profile.GetBindings(outOfRange);
    REQUIRE(bindings.empty());
    REQUIRE(profile.BindingCount(outOfRange) == 0);
    REQUIRE(!profile.HasBinding(outOfRange, InputCode::Key(SDL_SCANCODE_W)));
}

TEST_CASE("InputProfile SetBindingsFromLegacy converts packed ints", "[input][InputProfile]")
{
    InputProfile profile;
    int legacyKeys[] = {
        static_cast<int>(SDL_SCANCODE_W),
        0x00010001, // INPUT_DEVICE_MOUSE | 1
        0x00020003, // INPUT_DEVICE_STICK | 3
    };
    profile.SetBindingsFromLegacy(UAMoveForward, legacyKeys, 3);

    REQUIRE(profile.BindingCount(UAMoveForward) == 3);
    REQUIRE(profile.HasBinding(UAMoveForward, InputCode::Key(SDL_SCANCODE_W)));
    REQUIRE(profile.HasBinding(UAMoveForward, InputCode::MouseButton(1)));
    REQUIRE(profile.HasBinding(UAMoveForward, InputCode::GamepadBtn(3)));
}

TEST_CASE("InputProfile SetBindingsFromLegacy skips zero codes", "[input][InputProfile]")
{
    InputProfile profile;
    int legacyKeys[] = {static_cast<int>(SDL_SCANCODE_W), 0, 0x00010001};
    profile.SetBindingsFromLegacy(UAMoveForward, legacyKeys, 3);

    REQUIRE(profile.BindingCount(UAMoveForward) == 2);
}

TEST_CASE("InputProfile different actions have independent bindings", "[input][InputProfile]")
{
    InputProfile profile;
    auto w = InputCode::Key(SDL_SCANCODE_W);
    auto s = InputCode::Key(SDL_SCANCODE_S);
    profile.Bind(UAMoveForward, w);
    profile.Bind(UAMoveBack, s);

    REQUIRE(profile.HasBinding(UAMoveForward, w));
    REQUIRE(!profile.HasBinding(UAMoveForward, s));
    REQUIRE(!profile.HasBinding(UAMoveBack, w));
    REQUIRE(profile.HasBinding(UAMoveBack, s));
}

TEST_CASE("InputProfile mixed device bindings on same action", "[input][InputProfile]")
{
    InputProfile profile;
    auto key = InputCode::Key(SDL_SCANCODE_SPACE);
    auto mouse = InputCode::MouseButton(0);
    auto pad = InputCode::GamepadBtn(0);
    auto axis = InputCode::GamepadAx(1);
    auto pov = InputCode::GamepadPov(2);

    profile.Bind(UAFire, key);
    profile.Bind(UAFire, mouse);
    profile.Bind(UAFire, pad);
    profile.Bind(UAFire, axis);
    profile.Bind(UAFire, pov);

    REQUIRE(profile.BindingCount(UAFire) == 5);
    REQUIRE(profile.HasBinding(UAFire, key));
    REQUIRE(profile.HasBinding(UAFire, mouse));
    REQUIRE(profile.HasBinding(UAFire, pad));
    REQUIRE(profile.HasBinding(UAFire, axis));
    REQUIRE(profile.HasBinding(UAFire, pov));
}

TEST_CASE("InputProfile LoadDefaults skips legacy gamepad bindings", "[input][InputProfile]")
{
    InputProfile profile;
    profile.LoadDefaults();

    REQUIRE_FALSE(profile.HasBinding(UAFire, InputCode::GamepadBtn(7)));
    REQUIRE_FALSE(profile.HasBinding(UAAxisTurn, InputCode::GamepadAx(0)));
    REQUIRE_FALSE(profile.HasBinding(UAAxisDive, InputCode::GamepadAx(1)));
}

TEST_CASE("InputProfile defaults VoN CapsLock to push-to-talk only", "[input][InputProfile]")
{
    InputProfile profile;
    profile.LoadDefaults();

    REQUIRE(profile.HasBinding(UAVoiceOverNetPushToTalk, InputCode::Key(SDL_SCANCODE_CAPSLOCK)));
    REQUIRE_FALSE(profile.HasBinding(UAVoiceOverNet, InputCode::Key(SDL_SCANCODE_CAPSLOCK)));
}
