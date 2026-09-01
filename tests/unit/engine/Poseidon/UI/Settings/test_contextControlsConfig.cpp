#include <Poseidon/UI/Settings/ContextControlsConfig.hpp>

#include <Poseidon/Input/InputBinding.hpp>
#include <Poseidon/Input/InputCode.hpp>
#include <Poseidon/Input/UserAction.hpp>
#include <SDL3/SDL_scancode.h>
#include <catch2/catch_test_macros.hpp>

#include "test_fixtures.hpp"

#include <array>
#include <filesystem>
#include <random>
#include <string>

using namespace Poseidon;

namespace
{
std::string TmpPath(const char* leaf)
{
    static std::random_device rd;
    static std::mt19937 rng(rd());
    std::uniform_int_distribution<unsigned> dist;
    auto root = std::filesystem::temp_directory_path() / ("context_controls_test_" + std::to_string(dist(rng)));
    std::filesystem::create_directories(root);
    return (root / leaf).string();
}
} // namespace

TEST_CASE("ContextControlsConfig: missing file returns false", "[Settings][ContextControlsConfig]")
{
    ContextControlsConfig cfg;
    CHECK_FALSE(cfg.Load(TmpPath("missing.cfg")));
}

TEST_CASE("ContextControlsConfig: Save then Load round-trips separate context profiles",
          "[Settings][ContextControlsConfig]")
{
    const std::string path = TmpPath("context_controls.cfg");
    std::filesystem::remove(path);

    ContextControlsConfig src;
    src.profiles[(int)InputContext::Infantry].Bind(
        UAMoveForward, InputBinding(InputCode::GamepadAx(1), {}, ActivationMode::OnHold, -1.0f));
    src.profiles[(int)InputContext::CarDriver].Bind(
        UAMoveForward, InputBinding(InputCode::GamepadAx(2), InputCode::GamepadBtn(6), ActivationMode::OnHold, 0.5f));
    src.profiles[(int)InputContext::Infantry].Bind(UAFire, InputCode::Key(SDL_SCANCODE_SPACE));

    REQUIRE(src.Save(path));

    ContextControlsConfig dst;
    REQUIRE(dst.Load(path));

    const auto& infantryMove = dst.profiles[(int)InputContext::Infantry].GetBindingEntries(UAMoveForward);
    REQUIRE(infantryMove.size() == 1);
    CHECK(infantryMove[0].code == InputCode::GamepadAx(1));
    CHECK_FALSE(infantryMove[0].modifier.valid());
    CHECK(infantryMove[0].scale == -1.0f);

    const auto& carMove = dst.profiles[(int)InputContext::CarDriver].GetBindingEntries(UAMoveForward);
    REQUIRE(carMove.size() == 1);
    CHECK(carMove[0].code == InputCode::GamepadAx(2));
    CHECK(carMove[0].modifier == InputCode::GamepadBtn(6));
    CHECK(carMove[0].scale == 0.5f);

    CHECK(dst.profiles[(int)InputContext::Infantry].HasBinding(UAFire, InputCode::Key(SDL_SCANCODE_SPACE)));
    CHECK_FALSE(dst.profiles[(int)InputContext::CarDriver].HasBinding(UAFire, InputCode::Key(SDL_SCANCODE_SPACE)));

    std::filesystem::remove(path);
}

TEST_CASE("ContextControlsConfig: Save then Load preserves an empty positional slot",
          "[Settings][ContextControlsConfig]")
{
    const std::string path = TmpPath("context_controls_empty_slot.cfg");
    std::filesystem::remove(path);

    // A cleared primary that keeps its alt: empty slot 0, a real binding in slot 1.
    // The empty placeholder must survive the round-trip so the alt does not shift
    // up into the primary on reload.
    ContextControlsConfig src;
    src.profiles[(int)InputContext::Infantry].Bind(UAMoveForward, InputBinding{});
    src.profiles[(int)InputContext::Infantry].Bind(UAMoveForward, InputCode::Key(SDL_SCANCODE_UP));

    REQUIRE(src.Save(path));

    ContextControlsConfig dst;
    REQUIRE(dst.Load(path));

    const auto& move = dst.profiles[(int)InputContext::Infantry].GetBindingEntries(UAMoveForward);
    REQUIRE(move.size() == 2);
    CHECK_FALSE(move[0].code.valid());                      // empty primary slot kept
    CHECK(move[1].code == InputCode::Key(SDL_SCANCODE_UP)); // alt still in slot 1

    std::filesystem::remove(path);
}

TEST_CASE("ContextControlsConfig: version 3 adds map wheel bindings to available keyboard or mouse slots",
          "[Settings][ContextControlsConfig]")
{
    REQUIRE_FIXTURE("cfg/contextControls_v3_map_wheel.cfg");
    ContextControlsConfig migrated;
    REQUIRE(migrated.Load(GET_FIXTURE("cfg/contextControls_v3_map_wheel.cfg")));
    CHECK(migrated.migratedOnLoad);

    const auto& infantryZoomIn = migrated.profiles[(int)InputContext::Infantry].GetBindingEntries(UAMapZoomIn);
    REQUIRE(infantryZoomIn.size() == 2);
    CHECK(infantryZoomIn[0].code == InputCode::Key(SDL_SCANCODE_KP_PLUS));
    CHECK(infantryZoomIn[1].code == InputCode::MouseWheelDown());

    const auto& infantryZoomOut = migrated.profiles[(int)InputContext::Infantry].GetBindingEntries(UAMapZoomOut);
    REQUIRE(infantryZoomOut.size() == 2);
    CHECK(infantryZoomOut[0].code == InputCode::Key(SDL_SCANCODE_KP_MINUS));
    CHECK(infantryZoomOut[1].code == InputCode::MouseWheelUp());

    const auto& menuZoomIn = migrated.profiles[(int)InputContext::Menu].GetBindingEntries(UAMapZoomIn);
    REQUIRE(menuZoomIn.size() == 2);
    CHECK(menuZoomIn[0].code == InputCode::Key(SDL_SCANCODE_KP_PLUS));
    CHECK(menuZoomIn[1].code == InputCode::Key(SDL_SCANCODE_A));

    const auto& editorZoomIn = migrated.profiles[(int)InputContext::Editor].GetBindingEntries(UAMapZoomIn);
    REQUIRE(editorZoomIn.size() == 2);
    CHECK(editorZoomIn[0].code == InputCode::MouseWheelDown());
    CHECK(editorZoomIn[1].code == InputCode::Key(SDL_SCANCODE_A));
    CHECK(migrated.profiles[(int)InputContext::Editor].BindingCount(UAMapZoomOut) == 0);

    const std::string path = TmpPath("context_controls_v3_map_wheel_migrated.cfg");
    REQUIRE(migrated.Save(path));

    ContextControlsConfig reloaded;
    REQUIRE(reloaded.Load(path));
    CHECK_FALSE(reloaded.migratedOnLoad);
    CHECK(reloaded.profiles[(int)InputContext::Infantry].BindingCount(UAMapZoomIn) == 2);
    CHECK(reloaded.profiles[(int)InputContext::Infantry].BindingCount(UAMapZoomOut) == 2);

    std::filesystem::remove(path);
}

// A real, full contextControls.cfg captured from a version-2 user profile, from
// before several actions existed. Loading and copying the profiles is the path
// InputSubsystem::LoadKeys runs: listed bindings are preserved, and actions the
// file lacks are seeded with their defaults.
TEST_CASE("ContextControlsConfig: an older config keeps its bindings and defaults new actions",
          "[Settings][ContextControlsConfig]")
{
    REQUIRE_FIXTURE("cfg/contextControls_prior.cfg");

    ContextControlsConfig cfg;
    REQUIRE(cfg.Load(GET_FIXTURE("cfg/contextControls_prior.cfg")));
    CHECK(cfg.migratedOnLoad);

    // The array copy that used to fault when object files disagreed on UAN.
    std::array<InputProfile, ContextControlsConfig::ContextCount> copy = cfg.profiles;

    // Single keyboard binding.
    const auto& von = copy[(int)InputContext::Infantry].GetBindingEntries(UAVoiceOverNet);
    REQUIRE(von.size() == 1);
    CHECK(von[0].code.toLegacy() == 57);

    // Multi-binding action (keyboard + gamepad) round-trips both codes in order,
    // then migration appends the joystick trigger the file predates.
    const auto& fire = copy[(int)InputContext::Infantry].GetBindingEntries(UAFire);
    REQUIRE(fire.size() == 3);
    CHECK(fire[0].code.toLegacy() == 224);
    CHECK(fire[1].code.toLegacy() == 131079);
    CHECK(fire[2].code == InputCode::JoystickBtn(0));

    // A binding in a different context, to prove per-context separation held.
    const auto& chat = copy[(int)InputContext::Chat].GetBindingEntries(UAChat);
    REQUIRE(chat.size() == 1);
    CHECK(chat[0].code.toLegacy() == 56);

    // Push-to-talk was absent from the file; migration seeds its CapsLock default.
    CHECK(
        copy[(int)InputContext::Infantry].HasBinding(UAVoiceOverNetPushToTalk, InputCode::Key(SDL_SCANCODE_CAPSLOCK)));

    // A file listing every action it knew still comes up with joystick bindings.
    const auto& infantry = copy[(int)InputContext::Infantry];
    CHECK(infantry.HasBinding(UAAxisTurn, InputCode::JoystickAx(0)));
    CHECK(infantry.HasBinding(UAAxisDive, InputCode::JoystickAx(1)));
    CHECK(infantry.HasBinding(UAAxisRudder, InputCode::JoystickAx(5)));
    CHECK(infantry.HasBinding(UAAxisThrust, InputCode::JoystickAx(6)));
    CHECK(infantry.HasBinding(UALookUp, InputCode::JoystickPov(0, 0)));
    CHECK(infantry.HasBinding(UALookLeft, InputCode::JoystickPov(0, 6)));
}

// A complete version-2 config, as the current engine writes it minus the actions
// added since (map zoom, cheat entry, chat navigation). It parses across every context; the actions
// it lists are kept, the ones it predates are seeded to their defaults, and a save
// then reload comes up current with the fill persisted.
TEST_CASE("ContextControlsConfig: a full version-2 config parses and migrates to 6",
          "[Settings][ContextControlsConfig]")
{
    REQUIRE_FIXTURE("cfg/contextControls_v2_full.cfg");
    const int inf = (int)InputContext::Infantry;

    ContextControlsConfig migrated;
    REQUIRE(migrated.Load(GET_FIXTURE("cfg/contextControls_v2_full.cfg")));
    CHECK(migrated.migratedOnLoad);

    // Listed v2 actions parse and keep their values.
    CHECK(migrated.profiles[inf].BindingCount(UAFire) > 0);
    CHECK(migrated.profiles[inf].HasBinding(UAVoiceOverNetPushToTalk, InputCode::Key(SDL_SCANCODE_CAPSLOCK)));
    // The Map-context optics ZoomIn (ctxMapZoomIn) is name-adjacent to the new
    // MapZoomIn action but stays its own binding.
    CHECK(migrated.profiles[(int)InputContext::Map].BindingCount(UAZoomIn) > 0);

    // Actions the file predates are seeded to their defaults, including the cheat
    // trigger's Shift + Numpad-Minus combo.
    CHECK(migrated.profiles[inf].HasBinding(UAMapZoomIn, InputCode::Key(SDL_SCANCODE_KP_PLUS)));
    CHECK(migrated.profiles[inf].HasBinding(UAMapZoomOut, InputCode::Key(SDL_SCANCODE_KP_MINUS)));
    const auto& cheat = migrated.profiles[inf].GetBindingEntries(UACheatEntry);
    REQUIRE(cheat.size() == 1);
    CHECK(cheat[0].code == InputCode::Key(SDL_SCANCODE_KP_MINUS));
    CHECK(cheat[0].modifier == InputCode::Key(SDL_SCANCODE_LSHIFT));

    // Save the modernized config and reload it: now current, no second migration,
    // seeded defaults still present.
    const std::string path = TmpPath("context_controls_v2_full_migrated.cfg");
    std::filesystem::remove(path);
    REQUIRE(migrated.Save(path));

    ContextControlsConfig reloaded;
    REQUIRE(reloaded.Load(path));
    CHECK_FALSE(reloaded.migratedOnLoad);
    CHECK(reloaded.profiles[inf].HasBinding(UAMapZoomIn, InputCode::Key(SDL_SCANCODE_KP_PLUS)));
    CHECK(reloaded.profiles[inf].HasBinding(UAVoiceOverNetPushToTalk, InputCode::Key(SDL_SCANCODE_CAPSLOCK)));

    std::filesystem::remove(path);
}

// A version-1 config, older still: it predates push-to-talk as well as the newest
// actions. Loading migrates it across the two-version gap, keeping its bindings and
// seeding every action it lacks - push-to-talk, map zoom, and cheat entry.
TEST_CASE("ContextControlsConfig: a full version-1 config parses and migrates", "[Settings][ContextControlsConfig]")
{
    REQUIRE_FIXTURE("cfg/contextControls_v1_full.cfg");
    const int inf = (int)InputContext::Infantry;

    ContextControlsConfig migrated;
    REQUIRE(migrated.Load(GET_FIXTURE("cfg/contextControls_v1_full.cfg")));
    CHECK(migrated.migratedOnLoad);

    // A listed binding is kept.
    CHECK(migrated.profiles[inf].BindingCount(UAFire) > 0);

    // Every action the file predates is seeded to its default.
    CHECK(migrated.profiles[inf].HasBinding(UAVoiceOverNetPushToTalk, InputCode::Key(SDL_SCANCODE_CAPSLOCK)));
    CHECK(migrated.profiles[inf].HasBinding(UAMapZoomIn, InputCode::Key(SDL_SCANCODE_KP_PLUS)));
    CHECK(migrated.profiles[inf].HasBinding(UAMapZoomOut, InputCode::Key(SDL_SCANCODE_KP_MINUS)));
    CHECK(migrated.profiles[inf].BindingCount(UACheatEntry) == 1);

    // Save and reload comes up current with the fill persisted.
    const std::string path = TmpPath("context_controls_v1_full_migrated.cfg");
    std::filesystem::remove(path);
    REQUIRE(migrated.Save(path));

    ContextControlsConfig reloaded;
    REQUIRE(reloaded.Load(path));
    CHECK_FALSE(reloaded.migratedOnLoad);
    CHECK(reloaded.profiles[inf].HasBinding(UAVoiceOverNetPushToTalk, InputCode::Key(SDL_SCANCODE_CAPSLOCK)));
    CHECK(reloaded.profiles[inf].HasBinding(UAMapZoomIn, InputCode::Key(SDL_SCANCODE_KP_PLUS)));

    std::filesystem::remove(path);
}

TEST_CASE("ContextControlsConfig: version 4 gains chat navigation bindings", "[Settings][ContextControlsConfig]")
{
    REQUIRE_FIXTURE("cfg/contextControls_v4_chat.cfg");

    ContextControlsConfig migrated;
    REQUIRE(migrated.Load(GET_FIXTURE("cfg/contextControls_v4_chat.cfg")));
    CHECK(migrated.migratedOnLoad);

    const auto& chat = migrated.profiles[(int)InputContext::Chat];
    CHECK(chat.HasBinding(UAPrevChannel, InputCode::Key(SDL_SCANCODE_A)));
    CHECK(chat.HasBinding(UAChatPrevChannel, InputCode::Key(SDL_SCANCODE_DOWN)));
    CHECK(chat.HasBinding(UAChatNextChannel, InputCode::Key(SDL_SCANCODE_UP)));
    CHECK(chat.HasBinding(UAChatHistoryUp, InputCode::Key(SDL_SCANCODE_PAGEUP)));
    CHECK(chat.HasBinding(UAChatHistoryDown, InputCode::Key(SDL_SCANCODE_F2)));
    CHECK_FALSE(chat.HasBinding(UAChatHistoryDown, InputCode::Key(SDL_SCANCODE_PAGEDOWN)));
    CHECK(chat.HasBinding(UAMapZoomIn, InputCode::MouseWheelDown()));
    CHECK(chat.HasBinding(UAMapZoomOut, InputCode::MouseWheelUp()));

    const std::string path = TmpPath("context_controls_v4_chat_migrated.cfg");
    std::filesystem::remove(path);
    REQUIRE(migrated.Save(path));

    ContextControlsConfig reloaded;
    REQUIRE(reloaded.Load(path));
    CHECK_FALSE(reloaded.migratedOnLoad);
    CHECK(reloaded.profiles[(int)InputContext::Chat].HasBinding(UAChatHistoryDown, InputCode::Key(SDL_SCANCODE_F2)));

    std::filesystem::remove(path);
}
