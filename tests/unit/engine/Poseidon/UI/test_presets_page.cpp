// PresetsPage Provider tests

#include <Poseidon/UI/Options/PresetsPage.hpp>
#include <Poseidon/UI/Options/OptionsScrollList.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

using namespace Poseidon;

namespace
{
class TestablePresetsPage : public PresetsPage
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
} // namespace

// ---------------------------------------------------------------------------
// TestablePresetsPage: stepper row + action rows + auto Close.

TEST_CASE("PresetsPage: provider exposes stepper row + action row + close", "[UI][PresetsPage]")
{
    TestablePresetsPage page;
    auto& p = page.Provider();
    CHECK(p.RowCount() == 3);
}

TEST_CASE("PresetsPage: row labels", "[UI][PresetsPage]")
{
    LoadMainMenuStringtable();

    TestablePresetsPage page;
    auto& p = page.Provider();
    CHECK(std::string(page.TitleText()) == "Reset All");
    CHECK(std::string(p.RowLabel(0)) == "Preset");
    CHECK(std::string(p.RowLabel(1)) == "Reset all to preset");
}

TEST_CASE("PresetsPage: row descriptions localize from the main menu shard", "[UI][PresetsPage]")
{
    LoadMainMenuStringtable();

    TestablePresetsPage page;
    auto& p = page.Provider();
    CHECK(std::string(p.RowDescription(0)) == "Choose which preset to reset controls to.");
    CHECK(std::string(p.RowDescription(1)) == "Apply the selected preset to all controls.");
}

TEST_CASE("PresetsPage: row kinds - presets stepper + apply action + close action", "[UI][PresetsPage]")
{
    TestablePresetsPage page;
    auto& p = page.Provider();
    CHECK(p.RowKind(0) == OptionsScrollList::KindStepper); // presets stepper
    CHECK(p.RowKind(1) == OptionsScrollList::KindAction);  // apply preset
    CHECK(p.RowKind(2) == OptionsScrollList::KindAction);  // Close
}

TEST_CASE("PresetsPage: selected preset cycles correctly", "[UI][GamepadTuningPage]")
{
    TestablePresetsPage page;
    auto& p = page.Provider();

    CHECK(p.RowValue(0) == 0);
    p.SetRowValue(0, 1);
    CHECK(p.RowValue(0) == 1);

    // Out-of-range rejected.
    p.SetRowValue(0, 99);
    CHECK(p.RowValue(0) == 1);
}
