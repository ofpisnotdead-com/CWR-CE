#include <Poseidon/Core/resincl.hpp>
#include <Poseidon/Input/InputDeviceConstants.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/UI/Controls/UIControls.hpp>
#include <Poseidon/UI/Map/UIMap.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/UI/DisplayUI.hpp>
#include <Poseidon/UI/DisplayUICommon.hpp>
#include <catch2/catch_test_macros.hpp>
#include <SDL3/SDL_scancode.h>
#include <string>
#include <Poseidon/Foundation/Strings/RString.hpp>

using namespace Poseidon;

TEST_CASE("displayUI compiles", "[UI][compile]")
{
    SUCCEED();
}

TEST_CASE("displayUI names higher stick buttons", "[UI][displayUI]")
{
    CHECK(std::string((const char*)GetKeyName(INPUT_DEVICE_STICK + 8)) == "Stick Btn. #9");
    CHECK(std::string((const char*)GetKeyName(INPUT_DEVICE_STICK + 9)) == "Stick Btn. #10");
    CHECK(std::string((const char*)GetKeyName(INPUT_DEVICE_STICK + 10)) == "LS");
    CHECK(std::string((const char*)GetKeyName(INPUT_DEVICE_STICK + 11)) == "RS");
}

TEST_CASE("displayUI names double-tap keyboard and mouse bindings", "[UI][displayUI]")
{
    CHECK(std::string((const char*)GetKeyName(InputBindingDoubleTapCode((int)SDL_SCANCODE_G))).find("2x ") == 0);
    CHECK(std::string((const char*)GetKeyName(InputBindingDoubleTapCode(INPUT_DEVICE_MOUSE + 1))).find("2x ") == 0);
}

TEST_CASE("stalled client abort uses a disconnect-only debriefing", "[UI][multiplayer][disconnect]")
{
    CHECK(ResolveClientDebriefingMode(true, true, false) == ClientDebriefingMode::DisconnectOnly);
    CHECK(ResolveClientDebriefingMode(true, false, false) == ClientDebriefingMode::MissionResult);
    CHECK(ResolveClientDebriefingMode(false, true, false) == ClientDebriefingMode::MissionResult);
    CHECK(ResolveClientDebriefingMode(false, false, false) == ClientDebriefingMode::MissionResult);
    CHECK(ResolveClientDebriefingMode(false, false, true) == ClientDebriefingMode::DisconnectOnly);
}
