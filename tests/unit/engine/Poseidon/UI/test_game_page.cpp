// GamePage Provider tests — exercise the Game options row layout, row
// kinds, and value round-trip for the View Distance slider + Respect
// Mission Visibility toggle.  Regression: removing kRowViewDistance from the enum trips
// the RowCount and RowKind / SliderValueText assertions below.
//
// Snapshots ENGINE_CONFIG / USER_CONFIG / chat / view-distance state so
// parallel ctest runs stay clean.

#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/UI/Settings/GameSettingsConfig.hpp>
#include <Poseidon/UI/Options/GamePage.hpp>
#include <Poseidon/UI/Options/OptionsScrollList.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <string>

using namespace Poseidon;
namespace
{
class TestableGamePage : public GamePage
{
  public:
    OptionsScrollList::Provider& Provider() { return ProviderRef(); }
};

struct GamePageStateSnapshot
{
    bool blood;
    bool subtitles;
    bool radioSubtitles;
    float preferredViewDistance;
    bool respectMissionViewDistance;

    GamePageStateSnapshot()
    {
        blood = ENGINE_CONFIG.blood;
        subtitles = USER_CONFIG.showTitles;
        radioSubtitles = GChatList.Enabled();
        preferredViewDistance = GetSelectedPreferredViewDistance();
        respectMissionViewDistance = GetRespectMissionViewDistance();
    }

    ~GamePageStateSnapshot()
    {
        ENGINE_CONFIG.blood = blood;
        USER_CONFIG.showTitles = subtitles;
        GChatList.Enable(radioSubtitles);
        SetSelectedPreferredViewDistance(preferredViewDistance);
        SetRespectMissionViewDistance(respectMissionViewDistance);
    }
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
} // namespace

TEST_CASE("GamePage: provider exposes 7 rows + close", "[UI][GamePage]")
{
    TestableGamePage page;
    auto& p = page.Provider();
    CHECK(p.RowCount() == 8);
}

TEST_CASE("GamePage: row labels include view-distance controls", "[UI][GamePage]")
{
    LoadMainMenuStringtable();

    TestableGamePage page;
    auto& p = page.Provider();
    CHECK(std::string(page.TitleText()) == "Game");
    CHECK(std::string(p.RowLabel(2)) == "Visibility distance");
    CHECK(std::string(p.RowLabel(3)) == "Respect mission visibility");
}

TEST_CASE("GamePage: view-distance row accepts exact numeric entry", "[UI][GamePage]")
{
    GamePageStateSnapshot snap;
    LoadMainMenuStringtable();

    SetSelectedPreferredViewDistance(900.0f);

    TestableGamePage page;
    auto& p = page.Provider();
    CHECK(p.RowKind(2) == OptionsScrollList::KindNumericSlider);
    CHECK(p.RowValue(2) == 16);
    CHECK(std::string(p.SliderValueText(2)) == "900 m");

    p.SetRowValue(2, 0);
    CHECK(GetSelectedPreferredViewDistance() == GameSettingsConfig::kMinViewDistance);
    p.SetRowValue(2, 100);
    CHECK(GetSelectedPreferredViewDistance() == GameSettingsConfig::kMaxViewDistance);
}

TEST_CASE("GamePage: respect-mission visibility row toggles runtime setting", "[UI][GamePage]")
{
    GamePageStateSnapshot snap;

    SetRespectMissionViewDistance(true);

    TestableGamePage page;
    auto& p = page.Provider();
    CHECK(p.RowKind(3) == OptionsScrollList::KindBoolean);
    CHECK(p.RowValue(3) == 1);

    p.SetRowValue(3, 0);
    CHECK(GetRespectMissionViewDistance() == false);
    p.SetRowValue(3, 1);
    CHECK(GetRespectMissionViewDistance() == true);
}

TEST_CASE("GamePage: view-distance helpers clamp and round to the supported range", "[UI][GamePage]")
{
    CHECK(GamePage::ViewDistanceToSlider(GameSettingsConfig::kMinViewDistance - 500.0f) == 0);
    CHECK(GamePage::ViewDistanceToSlider(GameSettingsConfig::kMinViewDistance) == 0);
    CHECK(GamePage::ViewDistanceToSlider(GameSettingsConfig::kMaxViewDistance) == 100);
    CHECK(GamePage::ViewDistanceToSlider(GameSettingsConfig::kMaxViewDistance + 500.0f) == 100);

    CHECK(GamePage::SliderToViewDistance(-10) == GameSettingsConfig::kMinViewDistance);
    CHECK(GamePage::SliderToViewDistance(0) == GameSettingsConfig::kMinViewDistance);
    CHECK(GamePage::SliderToViewDistance(100) == GameSettingsConfig::kMaxViewDistance);
    CHECK(GamePage::SliderToViewDistance(120) == GameSettingsConfig::kMaxViewDistance);
    CHECK(std::fmod(GamePage::SliderToViewDistance(37), 100.0f) == Catch::Approx(0.0f));
}
