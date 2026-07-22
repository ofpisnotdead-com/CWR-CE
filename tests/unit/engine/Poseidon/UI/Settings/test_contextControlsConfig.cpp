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

// Regression: a real, full contextControls.cfg captured from a user profile that
// predates UAVoiceOverNetPushToTalk. Loading it and copying the profiles is the
// exact path InputSubsystem::LoadKeys runs (`profiles_ = contextControls.profiles`).
// This guards that path stays crash-free and parses a complete config correctly,
// including a bound action carrying two codes and the new action being absent.
TEST_CASE("ContextControlsConfig: loads a real prior config and copies profiles cleanly",
          "[Settings][ContextControlsConfig]")
{
    REQUIRE_FIXTURE("cfg/contextControls_prior.cfg");

    ContextControlsConfig cfg;
    REQUIRE(cfg.Load(GET_FIXTURE("cfg/contextControls_prior.cfg")));

    // The array copy that used to fault when object files disagreed on UAN.
    std::array<InputProfile, ContextControlsConfig::ContextCount> copy = cfg.profiles;

    // Single keyboard binding.
    const auto& von = copy[(int)InputContext::Infantry].GetBindingEntries(UAVoiceOverNet);
    REQUIRE(von.size() == 1);
    CHECK(von[0].code.toLegacy() == 57);

    // Multi-binding action (keyboard + gamepad) round-trips both codes in order.
    const auto& fire = copy[(int)InputContext::Infantry].GetBindingEntries(UAFire);
    REQUIRE(fire.size() == 2);
    CHECK(fire[0].code.toLegacy() == 224);
    CHECK(fire[1].code.toLegacy() == 131079);

    // A binding in a different context, to prove per-context separation held.
    const auto& chat = copy[(int)InputContext::Chat].GetBindingEntries(UAChat);
    REQUIRE(chat.size() == 1);
    CHECK(chat[0].code.toLegacy() == 56);

    // The config predates push-to-talk: the new action loads as simply unbound.
    CHECK(copy[(int)InputContext::Infantry].BindingCount(UAVoiceOverNetPushToTalk) == 0);
}
