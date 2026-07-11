// TouchPage provider tests — labels, slider shape, and live touch state.

#include <Poseidon/Input/TouchInput.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <Poseidon/UI/Options/OptionsScrollList.hpp>
#include <Poseidon/UI/Options/TouchPage.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

using namespace Poseidon;

namespace
{
class TouchStateSnapshot
{
  public:
    TouchStateSnapshot()
    {
        aim = TouchInput_GetAimSensitivity();
        cursor = TouchInput_GetCursorSensitivity();
        mode = TouchInput_GetDisplayMode();
    }
    ~TouchStateSnapshot()
    {
        TouchInput_SetAimSensitivity(aim);
        TouchInput_SetCursorSensitivity(cursor);
        TouchInput_SetDisplayMode(mode);
    }

  private:
    float aim = 1.0f;
    float cursor = 1.0f;
    TouchDisplayMode mode = TouchDisplayMode::Auto;
};

class TestableTouchPage : public TouchPage
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

TEST_CASE("TouchPage: provider exposes 3 rows + close", "[UI][TouchPage]")
{
    TestableTouchPage page;
    CHECK(page.Provider().RowCount() == 4);
}

TEST_CASE("TouchPage: row labels and descriptions match stringtable", "[UI][TouchPage]")
{
    LoadMainMenuStringtable();

    TestableTouchPage page;
    auto& p = page.Provider();
    CHECK(std::string(page.TitleText()) == "Touch Controls");
    CHECK(std::string(p.RowLabel(0)) == "Aim sensitivity");
    CHECK(std::string(p.RowDescription(0)) == "Right-side touch look and aim sensitivity. Range 0.25x to 3.0x.");
    CHECK(std::string(p.RowLabel(1)) == "Cursor movement sensitivity");
    CHECK(std::string(p.RowDescription(1)) == "Touch cursor movement sensitivity outside gameplay. Range 0.25x to 3.0x.");
    CHECK(std::string(p.RowLabel(2)) == "Show touch controls");
    CHECK(std::string(p.RowDescription(2)) ==
          "Auto shows touch controls when you're using touch and hides them when you switch to keyboard or a gamepad.");
}

TEST_CASE("TouchPage: display mode row exposes 3 stepper options and round-trips", "[UI][TouchPage]")
{
    LoadMainMenuStringtable();
    TouchStateSnapshot snap;
    TestableTouchPage page;
    auto& p = page.Provider();

    const OptionsScrollList::RowDef def = p.RowFor(2);
    REQUIRE(def.count == 3);
    REQUIRE(def.options != nullptr);
    CHECK(std::string(def.options[0]) == "Auto");
    CHECK(std::string(def.options[1]) == "Always on");
    CHECK(std::string(def.options[2]) == "Always off");

    p.SetRowValue(2, static_cast<int>(TouchDisplayMode::AlwaysOn));
    CHECK(p.RowValue(2) == static_cast<int>(TouchDisplayMode::AlwaysOn));
    CHECK(TouchInput_GetDisplayMode() == TouchDisplayMode::AlwaysOn);

    p.SetRowValue(2, static_cast<int>(TouchDisplayMode::AlwaysOff));
    CHECK(TouchInput_GetDisplayMode() == TouchDisplayMode::AlwaysOff);

    p.SetRowValue(2, static_cast<int>(TouchDisplayMode::Auto));
    CHECK(TouchInput_GetDisplayMode() == TouchDisplayMode::Auto);
}

TEST_CASE("TouchPage: aim sensitivity slider maps 0..100 to 0.25..3.0 linearly", "[UI][TouchPage]")
{
    TouchStateSnapshot snap;
    TestableTouchPage page;
    auto& p = page.Provider();

    p.SetRowValue(0, 0);
    CHECK(TouchInput_GetAimSensitivity() == 0.25f);
    p.SetRowValue(0, 100);
    CHECK(TouchInput_GetAimSensitivity() == 3.0f);
    p.SetRowValue(0, 50);
    CHECK(TouchInput_GetAimSensitivity() == 1.625f);
}

TEST_CASE("TouchPage: cursor sensitivity slider gives more travel to low speeds", "[UI][TouchPage]")
{
    TouchStateSnapshot snap;
    TestableTouchPage page;
    auto& p = page.Provider();

    p.SetRowValue(1, -10);
    CHECK(TouchInput_GetCursorSensitivity() == 0.25f);
    p.SetRowValue(1, 200);
    CHECK(TouchInput_GetCursorSensitivity() == 3.0f);
    p.SetRowValue(1, 60);
    CHECK(TouchInput_GetCursorSensitivity() == Catch::Approx(0.525f).margin(0.001f));
}

TEST_CASE("TouchPage: sensitivity readback inverts cleanly", "[UI][TouchPage]")
{
    TouchStateSnapshot snap;
    TestableTouchPage page;
    auto& p = page.Provider();

    TouchInput_SetAimSensitivity(0.25f);
    CHECK(p.RowValue(0) == 0);
    TouchInput_SetAimSensitivity(3.0f);
    CHECK(p.RowValue(0) == 100);
    TouchInput_SetCursorSensitivity(0.525f);
    CHECK(p.RowValue(1) == 60);
}

TEST_CASE("TouchPage: sensitivity helpers clamp the supported range", "[UI][TouchPage]")
{
    CHECK(TouchPage::SensitivityToPercent(0.1f) == 0);
    CHECK(TouchPage::SensitivityToPercent(0.25f) == 0);
    CHECK(TouchPage::SensitivityToPercent(1.625f) == 50);
    CHECK(TouchPage::SensitivityToPercent(3.0f) == 100);
    CHECK(TouchPage::SensitivityToPercent(4.0f) == 100);

    CHECK(TouchPage::PercentToSensitivity(-10) == 0.25f);
    CHECK(TouchPage::PercentToSensitivity(0) == 0.25f);
    CHECK(TouchPage::PercentToSensitivity(50) == 1.625f);
    CHECK(TouchPage::PercentToSensitivity(100) == 3.0f);
    CHECK(TouchPage::PercentToSensitivity(120) == 3.0f);

    CHECK(TouchPage::CursorSensitivityToPercent(0.1f) == 0);
    CHECK(TouchPage::CursorSensitivityToPercent(0.25f) == 0);
    CHECK(TouchPage::CursorSensitivityToPercent(0.525f) == 60);
    CHECK(TouchPage::CursorSensitivityToPercent(3.0f) == 100);
    CHECK(TouchPage::CursorSensitivityToPercent(4.0f) == 100);

    CHECK(TouchPage::PercentToCursorSensitivity(-10) == 0.25f);
    CHECK(TouchPage::PercentToCursorSensitivity(0) == 0.25f);
    CHECK(TouchPage::PercentToCursorSensitivity(60) == Catch::Approx(0.525f).margin(0.001f));
    CHECK(TouchPage::PercentToCursorSensitivity(100) == 3.0f);
    CHECK(TouchPage::PercentToCursorSensitivity(120) == 3.0f);
}
