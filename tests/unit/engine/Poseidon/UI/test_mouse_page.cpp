// MousePage::MouseProvider — pure-data test of the row provider that
// backs the Mouse settings page. Exercises labels/descriptions, the
// float-percent codec, row state, and get/set round-trips via the live
// InputSubsystem (snapshotted + restored to keep parallel ctest runs clean).

#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/UI/Options/MousePage.hpp>
#include <Poseidon/UI/Options/OptionsScrollList.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

using namespace Poseidon;
namespace
{
class MouseStateSnapshot
{
  public:
    MouseStateSnapshot()
    {
        auto& sub = InputSubsystem::Instance();
        revY = sub.IsReverseMouse();
        btn = sub.IsMouseButtonsReversed();
        sx = sub.GetMouseSensitivityX();
        sy = sub.GetMouseSensitivityY();
        tuning = sub.GetMouseTuning();
    }
    ~MouseStateSnapshot()
    {
        auto& sub = InputSubsystem::Instance();
        sub.SetReverseMouse(revY);
        sub.SetMouseButtonsReversed(btn);
        sub.SetMouseSensitivityX(sx);
        sub.SetMouseSensitivityY(sy);
        sub.GetMouseTuning() = tuning;
    }

  private:
    bool revY = false;
    bool btn = false;
    float sx = 1.0f;
    float sy = 1.0f;
    MouseTuning tuning;
};

class TestableMousePage : public MousePage
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

TEST_CASE("MousePage: provider exposes sectioned mouse rows + close", "[UI][MousePage]")
{
    TestableMousePage page;
    auto& p = page.Provider();
    CHECK(p.RowCount() == 15); // 14 mouse rows + auto-appended Close
}

TEST_CASE("MousePage: row labels match the resource bundle expectations", "[UI][MousePage]")
{
    LoadMainMenuStringtable();

    TestableMousePage page;
    auto& p = page.Provider();
    CHECK(std::string(page.TitleText()) == "Mouse");
    CHECK(std::string(p.RowLabel(0)) == "Mouse");
    CHECK(std::string(p.RowLabel(1)) == "Y-axis inversion");
    CHECK(std::string(p.RowLabel(2)) == "Mouse buttons swap");
    CHECK(std::string(p.RowLabel(3)) == "Mouse sensitivity X");
    CHECK(std::string(p.RowLabel(4)) == "Mouse sensitivity Y");
    CHECK(std::string(p.RowLabel(5)) == "Mouse DPI");
    CHECK(std::string(p.RowLabel(6)) == "Advanced Mouse");
    CHECK(std::string(p.RowLabel(7)) == "Input dead zone");
    CHECK(std::string(p.RowLabel(8)) == "Mouse smoothing");
    CHECK(std::string(p.RowLabel(9)) == "Mouse acceleration");
    CHECK(std::string(p.RowLabel(10)) == "Aim mode");
    CHECK(std::string(p.RowLabel(11)) == "Free-aim zone X");
    CHECK(std::string(p.RowLabel(12)) == "Free-aim zone Y");
    CHECK(std::string(p.RowLabel(13)) == "Menu cursor speed");
}

TEST_CASE("MousePage: row descriptions explain every settable row", "[UI][MousePage]")
{
    LoadMainMenuStringtable();

    TestableMousePage page;
    auto& p = page.Provider();
    CHECK(std::string(p.RowDescription(1)) == "Invert vertical mouse motion - pushing the mouse forward looks down.");
    CHECK(std::string(p.RowDescription(2)) == "Swap left and right mouse buttons.");
    CHECK(std::string(p.RowDescription(3)) == "Horizontal mouse sensitivity. Range 0.5x to 2.0x.");
    CHECK(std::string(p.RowDescription(4)) == "Vertical mouse sensitivity. Range 0.5x to 2.0x.");
    CHECK(std::string(p.RowDescription(7)) ==
          "Ignore very small mouse movement before it affects aiming. Range 0.00 to 2.00 normalized counts.");
    CHECK(std::string(p.RowDescription(8)) ==
          "Smooth mouse movement over time. Higher values feel steadier but add delay.");
    CHECK(std::string(p.RowDescription(9)) ==
          "Increase turn speed for faster mouse movement. 0% disables acceleration.");
    CHECK(std::string(p.RowDescription(10)) == "Choose how much free-aim movement is allowed before the view turns. "
                                               "Custom unlocks the free-aim zone sliders.");
    CHECK(std::string(p.RowDescription(11)) ==
          "Horizontal free-aim zone in normalized cursor units. Range 0.00 to 0.80.");
    CHECK(std::string(p.RowDescription(12)) ==
          "Vertical free-aim zone in normalized cursor units. Range 0.00 to 0.50.");
    CHECK(std::string(p.RowDescription(13)) == "Menu cursor speed separate from aiming. Range 0.1x to 4.0x.");
}

TEST_CASE("MousePage: row kinds match the sectioned menu contract", "[UI][MousePage]")
{
    TestableMousePage page;
    auto& p = page.Provider();
    // Default Provider::RowKind infers from RowFor().count: >0 = Stepper, -1 = Slider.
    // Headers and the Close footer override that default kind.
    CHECK(p.RowKind(0) == OptionsScrollList::KindHeader);   // Mouse section header
    CHECK(p.RowKind(1) == OptionsScrollList::KindStepper);  // Y-invert toggle
    CHECK(p.RowKind(2) == OptionsScrollList::KindStepper);  // Buttons swap toggle
    CHECK(p.RowKind(3) == OptionsScrollList::KindSlider);   // Sensitivity X
    CHECK(p.RowKind(4) == OptionsScrollList::KindSlider);   // Sensitivity Y
    CHECK(p.RowKind(5) == OptionsScrollList::KindStepper);  // Mouse DPI (preset select)
    CHECK(p.RowKind(6) == OptionsScrollList::KindHeader);   // Advanced Mouse section header
    CHECK(p.RowKind(7) == OptionsScrollList::KindSlider);   // Input dead zone
    CHECK(p.RowKind(8) == OptionsScrollList::KindSlider);   // Mouse smoothing
    CHECK(p.RowKind(9) == OptionsScrollList::KindSlider);   // Mouse acceleration amount
    CHECK(p.RowKind(10) == OptionsScrollList::KindStepper); // Aim mode preset
    CHECK(p.RowKind(11) == OptionsScrollList::KindSlider);  // Free-aim zone X
    CHECK(p.RowKind(12) == OptionsScrollList::KindSlider);  // Free-aim zone Y
    CHECK(p.RowKind(13) == OptionsScrollList::KindSlider);  // Menu cursor speed
    CHECK(p.RowKind(14) == OptionsScrollList::KindAction);  // Close
}

TEST_CASE("MousePage: toggle rows round-trip through the InputSubsystem", "[UI][MousePage]")
{
    MouseStateSnapshot snap;
    TestableMousePage page;
    auto& p = page.Provider();
    auto& sub = InputSubsystem::Instance();

    sub.SetReverseMouse(false);
    sub.SetMouseButtonsReversed(false);

    CHECK(p.RowValue(1) == 0);
    CHECK(p.RowValue(2) == 0);

    p.SetRowValue(1, 1);
    p.SetRowValue(2, 1);
    CHECK(sub.IsReverseMouse() == true);
    CHECK(sub.IsMouseButtonsReversed() == true);
    CHECK(p.RowValue(1) == 1);
    CHECK(p.RowValue(2) == 1);

    p.SetRowValue(1, 99);
    CHECK(sub.IsReverseMouse() == true);
    p.SetRowValue(1, -1);
    CHECK(sub.IsReverseMouse() == true);
}

TEST_CASE("MousePage: sensitivity slider maps 0..100 to 0.5..2.0 linearly", "[UI][MousePage]")
{
    MouseStateSnapshot snap;
    TestableMousePage page;
    auto& p = page.Provider();
    auto& sub = InputSubsystem::Instance();

    sub.GetMouseTuning().extendedRange = true;
    p.SetRowValue(3, 0);
    CHECK(sub.GetMouseSensitivityX() == 0.5f);
    p.SetRowValue(3, 100);
    CHECK(sub.GetMouseSensitivityX() == 2.0f);
    p.SetRowValue(3, 50);
    CHECK(sub.GetMouseSensitivityX() == 1.25f);

    p.SetRowValue(4, -10);
    CHECK(sub.GetMouseSensitivityY() == 0.5f);
    p.SetRowValue(4, 200);
    CHECK(sub.GetMouseSensitivityY() == 2.0f);
}

TEST_CASE("MousePage: sensitivity readback inverts cleanly", "[UI][MousePage]")
{
    MouseStateSnapshot snap;
    TestableMousePage page;
    auto& p = page.Provider();
    auto& sub = InputSubsystem::Instance();

    sub.SetMouseSensitivityX(0.5f);
    CHECK(p.RowValue(3) == 0);
    sub.SetMouseSensitivityX(2.0f);
    CHECK(p.RowValue(3) == 100);
    sub.SetMouseSensitivityX(1.25f);
    CHECK(p.RowValue(3) == 50);
    sub.SetMouseSensitivityY(1.0f);
    CHECK(p.RowValue(4) == 33);
}

TEST_CASE("MousePage: Mouse DPI helpers map index <-> preset", "[UI][MousePage]")
{
    CHECK(MousePage::DpiToIndex(false, 1600) == 0);
    CHECK(MousePage::DpiToIndex(false, 400) == 0);
    CHECK(MousePage::DpiToIndex(true, 400) == 1);
    CHECK(MousePage::DpiToIndex(true, 1600) == 4);
    CHECK(MousePage::DpiToIndex(true, 16000) == 9);
    CHECK(MousePage::DpiToIndex(true, 5000) == 7);

    CHECK(MousePage::IndexToDpi(0) == 0);
    CHECK(MousePage::IndexToDpi(1) == 400);
    CHECK(MousePage::IndexToDpi(4) == 1600);
    CHECK(MousePage::IndexToDpi(9) == 16000);
    CHECK(MousePage::IndexToDpi(99) == 0);
}

TEST_CASE("MousePage: Mouse DPI row round-trips through the tuning", "[UI][MousePage]")
{
    MouseStateSnapshot snap;
    TestableMousePage page;
    auto& p = page.Provider();
    auto& sub = InputSubsystem::Instance();

    p.SetRowValue(5, 0);
    CHECK(sub.GetMouseTuning().dpiNormalize == false);
    CHECK(p.RowValue(5) == 0);

    p.SetRowValue(5, 4);
    CHECK(sub.GetMouseTuning().dpiNormalize == true);
    CHECK(sub.GetMouseTuning().mouseDpi == 1600);
    CHECK(p.RowValue(5) == 4);

    p.SetRowValue(5, 9);
    CHECK(sub.GetMouseTuning().mouseDpi == 16000);
    CHECK(p.RowValue(5) == 9);
}

TEST_CASE("MousePage: advanced tuning sliders round-trip through MouseTuning", "[UI][MousePage]")
{
    MouseStateSnapshot snap;
    TestableMousePage page;
    auto& p = page.Provider();
    auto& sub = InputSubsystem::Instance();

    p.SetRowValue(7, 0);
    CHECK(sub.GetMouseTuning().inputDeadZone == Catch::Approx(0.0f));
    p.SetRowValue(7, 50);
    CHECK(sub.GetMouseTuning().inputDeadZone == Catch::Approx(1.0f));
    CHECK(p.RowValue(7) == 50);
    p.SetRowValue(7, 100);
    CHECK(sub.GetMouseTuning().inputDeadZone == Catch::Approx(2.0f));

    p.SetRowValue(8, 0);
    CHECK(sub.GetMouseTuning().smoothing == Catch::Approx(0.0f));
    p.SetRowValue(8, 50);
    CHECK(sub.GetMouseTuning().smoothing == Catch::Approx(0.475f));
    CHECK(p.RowValue(8) == 50);
    p.SetRowValue(8, 100);
    CHECK(sub.GetMouseTuning().smoothing == Catch::Approx(0.95f));

    p.SetRowValue(9, 0);
    CHECK(sub.GetMouseTuning().acceleration == false);
    CHECK(sub.GetMouseTuning().accelExponent == Catch::Approx(1.0f));
    p.SetRowValue(9, 50);
    CHECK(sub.GetMouseTuning().acceleration == true);
    CHECK(sub.GetMouseTuning().accelExponent == Catch::Approx(1.5f));
    CHECK(p.RowValue(9) == 50);
    p.SetRowValue(9, 100);
    CHECK(sub.GetMouseTuning().accelExponent == Catch::Approx(2.0f));

    p.SetRowValue(13, 0);
    CHECK(sub.GetMouseTuning().menuCursorScale == Catch::Approx(0.1f));
    p.SetRowValue(13, 50);
    CHECK(sub.GetMouseTuning().menuCursorScale == Catch::Approx(2.05f));
    CHECK(p.RowValue(13) == 50);
    p.SetRowValue(13, 100);
    CHECK(sub.GetMouseTuning().menuCursorScale == Catch::Approx(4.0f));
}

TEST_CASE("MousePage: aim mode presets set free-aim zones and custom unlocks sliders", "[UI][MousePage]")
{
    MouseStateSnapshot snap;
    TestableMousePage page;
    auto& p = page.Provider();
    auto& t = InputSubsystem::Instance().GetMouseTuning();

    p.SetRowValue(10, static_cast<int>(MouseAimMode::Classic));
    CHECK(t.aimMode == MouseAimMode::Classic);
    CHECK(t.freeAimZoneX == Catch::Approx(0.8f));
    CHECK(t.freeAimZoneY == Catch::Approx(0.5f));
    CHECK(p.IsDisabled(11));
    CHECK(p.IsDisabled(12));

    p.SetRowValue(10, static_cast<int>(MouseAimMode::Reduced));
    CHECK(t.freeAimZoneX == Catch::Approx(0.3f));
    CHECK(t.freeAimZoneY == Catch::Approx(0.2f));

    p.SetRowValue(10, static_cast<int>(MouseAimMode::Direct));
    CHECK(t.freeAimZoneX == Catch::Approx(0.0f));
    CHECK(t.freeAimZoneY == Catch::Approx(0.0f));

    p.SetRowValue(11, 100);
    CHECK(t.freeAimZoneX == Catch::Approx(0.0f));

    p.SetRowValue(10, static_cast<int>(MouseAimMode::Custom));
    CHECK_FALSE(p.IsDisabled(11));
    CHECK_FALSE(p.IsDisabled(12));
    p.SetRowValue(11, 50);
    CHECK(t.freeAimZoneX == Catch::Approx(0.4f));
    p.SetRowValue(12, 50);
    CHECK(t.freeAimZoneY == Catch::Approx(0.25f));
}

TEST_CASE("MousePage: slider value text remains numeric", "[UI][MousePage]")
{
    MouseStateSnapshot snap;
    TestableMousePage page;
    auto& p = page.Provider();
    auto& sub = InputSubsystem::Instance();

    sub.SetMouseSensitivityX(1.25f);
    sub.GetMouseTuning().inputDeadZone = 1.0f;
    sub.GetMouseTuning().smoothing = 0.475f;
    sub.GetMouseTuning().acceleration = true;
    sub.GetMouseTuning().accelExponent = 1.5f;
    sub.GetMouseTuning().aimMode = MouseAimMode::Custom;
    sub.GetMouseTuning().freeAimZoneX = 0.4f;
    sub.GetMouseTuning().freeAimZoneY = 0.25f;
    sub.GetMouseTuning().menuCursorScale = 2.05f;

    CHECK(std::string(p.SliderValueText(3)) == "1.25x");
    CHECK(std::string(p.SliderValueText(7)) == "1.00");
    CHECK(std::string(p.SliderValueText(8)) == "50%");
    CHECK(std::string(p.SliderValueText(9)) == "50%");
    CHECK(std::string(p.SliderValueText(11)) == "0.40");
    CHECK(std::string(p.SliderValueText(12)) == "0.25");
    CHECK(std::string(p.SliderValueText(13)) == "2.05x");
}

TEST_CASE("MousePage: a hand-edited non-preset DPI is preserved on read", "[UI][MousePage]")
{
    MouseStateSnapshot snap;
    TestableMousePage page;
    auto& p = page.Provider();
    auto& sub = InputSubsystem::Instance();

    sub.GetMouseTuning().dpiNormalize = true;
    sub.GetMouseTuning().mouseDpi = 5000;

    CHECK(p.RowValue(5) == 7);
    CHECK(sub.GetMouseTuning().mouseDpi == 5000);
    CHECK(sub.GetMouseTuning().dpiNormalize == true);
}

TEST_CASE("MousePage: sensitivity helpers clamp the supported 0.5x..2.0x range", "[UI][MousePage]")
{
    CHECK(MousePage::SensitivityToPercent(0.1f) == 0);
    CHECK(MousePage::SensitivityToPercent(0.5f) == 0);
    CHECK(MousePage::SensitivityToPercent(1.25f) == 50);
    CHECK(MousePage::SensitivityToPercent(2.0f) == 100);
    CHECK(MousePage::SensitivityToPercent(3.0f) == 100);

    CHECK(MousePage::PercentToSensitivity(-10) == 0.5f);
    CHECK(MousePage::PercentToSensitivity(0) == 0.5f);
    CHECK(MousePage::PercentToSensitivity(50) == 1.25f);
    CHECK(MousePage::PercentToSensitivity(100) == 2.0f);
    CHECK(MousePage::PercentToSensitivity(120) == 2.0f);
}
