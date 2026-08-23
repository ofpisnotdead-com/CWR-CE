#include <Poseidon/Core/resincl.hpp>
#include <Poseidon/Input/InputDeviceConstants.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/UI/Controls/UIControls.hpp>
#include <Poseidon/UI/Map/UIMap.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/UI/DisplayUI.hpp>
#include <Poseidon/Foundation/Strings/Mbcs.hpp>
#include <Poseidon/UI/DisplayUICommon.hpp>
#include <catch2/catch_test_macros.hpp>
#include <SDL3/SDL_scancode.h>
#include <cstring>
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

TEST_CASE("displayUI names mouse wheel bindings", "[UI][displayUI]")
{
    CHECK(std::string((const char*)GetKeyName(INPUT_DEVICE_MOUSE_AXIS + INPUT_MOUSE_WHEEL_UP)) == "Mouse wheel up");
    CHECK(std::string((const char*)GetKeyName(INPUT_DEVICE_MOUSE_AXIS + INPUT_MOUSE_WHEEL_DOWN)) == "Mouse wheel down");
}

TEST_CASE("stalled client abort uses a disconnect-only debriefing", "[UI][multiplayer][disconnect]")
{
    CHECK(ResolveClientDebriefingMode(true, true, false) == ClientDebriefingMode::DisconnectOnly);
    CHECK(ResolveClientDebriefingMode(true, false, false) == ClientDebriefingMode::MissionResult);
    CHECK(ResolveClientDebriefingMode(false, true, false) == ClientDebriefingMode::MissionResult);
    CHECK(ResolveClientDebriefingMode(false, false, false) == ClientDebriefingMode::MissionResult);
    CHECK(ResolveClientDebriefingMode(false, false, true) == ClientDebriefingMode::DisconnectOnly);
}

TEST_CASE("multiplayer session row keeps its two text lines separate", "[UI][multiplayer][layout]")
{
    constexpr MultiplayerSessionRowLayout layout = GetMultiplayerSessionRowLayout();

    CHECK(layout.primarySize == 0.4f);
    CHECK(layout.secondarySize == 0.4f);
    CHECK(layout.primaryTop + layout.primarySize <= layout.secondaryTop);
    CHECK(layout.secondaryTop + layout.secondarySize <= 1.0f);
}

TEST_CASE("mod rows describe the operation applied to the loaded set", "[UI][mods]")
{
    ModRow row;
    CHECK(GetModRowAction(row) == ModRowAction::None);

    row.checked = true;
    CHECK(GetModRowAction(row) == ModRowAction::DownloadAndLoad);
    CHECK_FALSE(IsModRowActive(row));

    row.state = ModRowState::Downloaded;
    CHECK(GetModRowAction(row) == ModRowAction::Load);

    row.freshness = ModRowFreshness::UpdateAvailable;
    CHECK(GetModRowAction(row) == ModRowAction::UpdateAndLoad);

    row.state = ModRowState::Active;
    row.freshness = ModRowFreshness::Current;
    CHECK(GetModRowAction(row) == ModRowAction::None);
    CHECK(IsModRowActive(row));

    row.checked = false;
    CHECK(GetModRowAction(row) == ModRowAction::Unload);
    CHECK(IsModRowActive(row));
}

TEST_CASE("mod search ignores case and Czech diacritics", "[UI][mods][search]")
{
    const char* csla = "\xC4\x8C"
                       "SLA";
    CHECK(CModsList::ContainsNoCase(csla, "CSLA"));
    CHECK(CModsList::ContainsNoCase(csla, "csla"));
}

TEST_CASE("mod table columns follow their content", "[UI][mods][layout]")
{
    const auto widths = CModsList::AllocateColumnWidths({0.1f, 0.2f, 0.1f, 0.1f, 0.1f, 0.1f}, MTCName, 1.0f);

    CHECK(widths[MTCActive] == 0.1f);
    CHECK(widths[MTCAction] == 0.1f);
    CHECK(widths[MTCName] == 0.5f);
}

TEST_CASE("mod guidance marquees only when its rendered text overflows", "[UI][mods][marquee]")
{
    const RString text = "Enter stáhne vybrané mody";
    CHECK(strcmp(FormatModsGuidance(text, 1.0f, 2000), text) == 0);

    const RString marquee = FormatModsGuidance(text, 2.0f, 1400);
    CHECK(strcmp(marquee, text) != 0);
    CHECK(Foundation::CountUtf8Codepoints(marquee) == Foundation::CountUtf8Codepoints(text) / 2);
}
