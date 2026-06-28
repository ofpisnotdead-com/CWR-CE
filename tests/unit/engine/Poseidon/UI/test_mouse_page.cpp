// MousePage::MouseProvider — pure-data test of the row provider that
// backs the new Mouse settings page.  Exercises label/description
// strings, the float-percent codec, and the get/set round-trip via
// the live InputSubsystem (snapshotted + restored to keep parallel
// ctest runs clean).

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
        norm = sub.GetMouseTuning().dpiNormalize;
        dpi = sub.GetMouseTuning().mouseDpi;
        smoothing = sub.GetMouseTuning().smoothing;
        acceleration = sub.GetMouseTuning().acceleration;
        accelExponent = sub.GetMouseTuning().accelExponent;
        menuCursorScale = sub.GetMouseTuning().menuCursorScale;
        extendedRange = sub.GetMouseTuning().extendedRange;
    }
    ~MouseStateSnapshot()
    {
        auto& sub = InputSubsystem::Instance();
        sub.SetReverseMouse(revY);
        sub.SetMouseButtonsReversed(btn);
        sub.SetMouseSensitivityX(sx);
        sub.SetMouseSensitivityY(sy);
        sub.GetMouseTuning().dpiNormalize = norm;
        sub.GetMouseTuning().mouseDpi = dpi;
        sub.GetMouseTuning().smoothing = smoothing;
        sub.GetMouseTuning().acceleration = acceleration;
        sub.GetMouseTuning().accelExponent = accelExponent;
        sub.GetMouseTuning().menuCursorScale = menuCursorScale;
        sub.GetMouseTuning().extendedRange = extendedRange;
    }

  private:
    bool revY = false;
    bool btn = false;
    float sx = 1.0f;
    float sy = 1.0f;
    bool norm = false;
    int dpi = 1600;
    float smoothing = 0.0f;
    bool acceleration = false;
    float accelExponent = 1.0f;
    float menuCursorScale = 1.0f;
    bool extendedRange = false;
};

// MousePage's provider is a private member type — re-create the same
// shape inline for testing the row contract.  This keeps the public
// MousePage interface clean (no test-only friend or accessor) and
// ensures the tests catch a mismatch between the docs / inline expected
// row IDs and the actual implementation by exercising the page through
// its public RowCount / RowLabel / RowValue / SetRowValue contract.
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

TEST_CASE("MousePage: provider exposes mouse rows + close", "[UI][MousePage]")
{
    TestableMousePage page;
    auto& p = page.Provider();
    // 10 mouse rows (basic + advanced) + the auto-appended Close row.
    CHECK(p.RowCount() == 11);
}

TEST_CASE("MousePage: row labels match the resource bundle expectations", "[UI][MousePage]")
{
    LoadMainMenuStringtable();

    TestableMousePage page;
    auto& p = page.Provider();
    CHECK(std::string(page.TitleText()) == "Mouse");
    CHECK(std::string(p.RowLabel(0)) == "Y-axis inversion");
    CHECK(std::string(p.RowLabel(1)) == "Mouse buttons swap");
    CHECK(std::string(p.RowLabel(2)) == "Mouse sensitivity X");
    CHECK(std::string(p.RowLabel(3)) == "Mouse sensitivity Y");
    CHECK(std::string(p.RowLabel(4)) == "Mouse DPI");
    CHECK(std::string(p.RowLabel(5)) == "Advanced sensitivity range");
    CHECK(std::string(p.RowLabel(6)) == "Mouse smoothing");
    CHECK(std::string(p.RowLabel(7)) == "Mouse acceleration");
    CHECK(std::string(p.RowLabel(8)) == "Acceleration strength");
    CHECK(std::string(p.RowLabel(9)) == "Menu cursor speed");
    // Row 10 is the Close action — label comes from the localized
    // STR_DISP_CLOSE which the unit-test environment doesn't populate;
    // structural check (it exists as an action row) covered separately.
}

TEST_CASE("MousePage: row descriptions are non-empty for every settable row", "[UI][MousePage]")
{
    LoadMainMenuStringtable();

    TestableMousePage page;
    auto& p = page.Provider();
    CHECK(std::string(p.RowDescription(0)) == "Invert vertical mouse motion - pushing the mouse forward looks down.");
    CHECK(std::string(p.RowDescription(1)) == "Swap left and right mouse buttons.");
    CHECK(std::string(p.RowDescription(2)) == "Horizontal mouse sensitivity. Range 0.5x to 2.0x.");
    CHECK(std::string(p.RowDescription(3)) == "Vertical mouse sensitivity. Range 0.5x to 2.0x.");
    CHECK(std::string(p.RowDescription(5)) == "Allow sensitivity sliders to use 0.05x to 3.0x.");
    CHECK(std::string(p.RowDescription(6)) == "Smooth uneven mouse input. Higher values add latency.");
    CHECK(std::string(p.RowDescription(7)) == "Increase large mouse movements while keeping fine aim slower.");
    CHECK(std::string(p.RowDescription(8)) == "Acceleration curve strength. Range 1.0x to 2.0x.");
    CHECK(std::string(p.RowDescription(9)) == "Menu cursor speed separate from aiming. Range 0.1x to 4.0x.");
}

TEST_CASE("MousePage: kind is Stepper for toggles, Slider for sensitivities, Action for close", "[UI][MousePage]")
{
    TestableMousePage page;
    auto& p = page.Provider();
    // Default Provider::RowKind infers from RowFor().count: >0 = Stepper, -1 = Slider.
    CHECK(p.RowKind(0) == OptionsScrollList::KindStepper); // Y-invert toggle
    CHECK(p.RowKind(1) == OptionsScrollList::KindStepper); // Buttons swap toggle
    CHECK(p.RowKind(2) == OptionsScrollList::KindSlider);  // Sensitivity X
    CHECK(p.RowKind(3) == OptionsScrollList::KindSlider);  // Sensitivity Y
    CHECK(p.RowKind(4) == OptionsScrollList::KindStepper); // Mouse DPI (preset select)
    CHECK(p.RowKind(5) == OptionsScrollList::KindStepper); // Advanced sensitivity range
    CHECK(p.RowKind(6) == OptionsScrollList::KindSlider);  // Mouse smoothing
    CHECK(p.RowKind(7) == OptionsScrollList::KindStepper); // Mouse acceleration
    CHECK(p.RowKind(8) == OptionsScrollList::KindSlider);  // Acceleration strength
    CHECK(p.RowKind(9) == OptionsScrollList::KindSlider);  // Menu cursor speed
    CHECK(p.RowKind(10) == OptionsScrollList::KindAction); // Close
}

TEST_CASE("MousePage: toggle rows round-trip through the InputSubsystem", "[UI][MousePage]")
{
    MouseStateSnapshot snap;
    TestableMousePage page;
    auto& p = page.Provider();
    auto& sub = InputSubsystem::Instance();

    sub.SetReverseMouse(false);
    sub.SetMouseButtonsReversed(false);

    CHECK(p.RowValue(0) == 0);
    CHECK(p.RowValue(1) == 0);

    p.SetRowValue(0, 1);
    p.SetRowValue(1, 1);
    CHECK(sub.IsReverseMouse() == true);
    CHECK(sub.IsMouseButtonsReversed() == true);
    CHECK(p.RowValue(0) == 1);
    CHECK(p.RowValue(1) == 1);

    // Out-of-range value is rejected.
    p.SetRowValue(0, 99);
    CHECK(sub.IsReverseMouse() == true);
    p.SetRowValue(0, -1);
    CHECK(sub.IsReverseMouse() == true);
}

TEST_CASE("MousePage: sensitivity slider maps 0..100 to 0.5..2.0 linearly", "[UI][MousePage]")
{
    MouseStateSnapshot snap;
    TestableMousePage page;
    auto& p = page.Provider();
    auto& sub = InputSubsystem::Instance();

    p.SetRowValue(2, 0);
    CHECK(sub.GetMouseSensitivityX() == 0.5f);
    p.SetRowValue(2, 100);
    CHECK(sub.GetMouseSensitivityX() == 2.0f);
    p.SetRowValue(2, 50);
    CHECK(sub.GetMouseSensitivityX() == 1.25f); // 0.5 + 0.5 * 1.5

    // Negative / >100 clamp.
    p.SetRowValue(3, -10);
    CHECK(sub.GetMouseSensitivityY() == 0.5f);
    p.SetRowValue(3, 200);
    CHECK(sub.GetMouseSensitivityY() == 2.0f);
}

TEST_CASE("MousePage: advanced sensitivity range widens the sensitivity slider", "[UI][MousePage]")
{
    MouseStateSnapshot snap;
    TestableMousePage page;
    auto& p = page.Provider();
    auto& sub = InputSubsystem::Instance();

    sub.GetMouseTuning().extendedRange = false;
    CHECK(p.RowValue(5) == 0);

    p.SetRowValue(5, 1);
    CHECK(sub.GetMouseTuning().extendedRange == true);
    CHECK(p.RowValue(5) == 1);

    p.SetRowValue(2, 0);
    CHECK(sub.GetMouseSensitivityX() == Catch::Approx(0.05f));
    p.SetRowValue(2, 100);
    CHECK(sub.GetMouseSensitivityX() == Catch::Approx(3.0f));
    p.SetRowValue(2, 50);
    CHECK(sub.GetMouseSensitivityX() == Catch::Approx(1.525f));

    sub.SetMouseSensitivityY(0.05f);
    CHECK(p.RowValue(3) == 0);
    sub.SetMouseSensitivityY(3.0f);
    CHECK(p.RowValue(3) == 100);

    sub.SetMouseSensitivityX(3.0f);
    p.SetRowValue(5, 0);
    CHECK(sub.GetMouseTuning().extendedRange == false);
    CHECK(sub.GetMouseSensitivityX() == 2.0f);
    CHECK(sub.GetMouseSensitivityY() == 2.0f);
}

TEST_CASE("MousePage: sensitivity readback inverts cleanly", "[UI][MousePage]")
{
    MouseStateSnapshot snap;
    TestableMousePage page;
    auto& p = page.Provider();
    auto& sub = InputSubsystem::Instance();

    sub.SetMouseSensitivityX(0.5f);
    CHECK(p.RowValue(2) == 0);
    sub.SetMouseSensitivityX(2.0f);
    CHECK(p.RowValue(2) == 100);
    sub.SetMouseSensitivityX(1.25f);
    CHECK(p.RowValue(2) == 50);
    // Engine default 1.0 maps to 33% on a linear slider — a UX choice
    // (matches the legacy slider math) we want this test to lock in.
    sub.SetMouseSensitivityY(1.0f);
    CHECK(p.RowValue(3) == 33);
}

TEST_CASE("MousePage: Mouse DPI helpers map index <-> preset", "[UI][MousePage]")
{
    // Off when normalization is disabled, regardless of stored DPI.
    CHECK(MousePage::DpiToIndex(false, 1600) == 0);
    CHECK(MousePage::DpiToIndex(false, 400) == 0);
    // Exact presets map to their index (kMouseDpiPresets: 0,400,800,1200,1600,...).
    CHECK(MousePage::DpiToIndex(true, 400) == 1);
    CHECK(MousePage::DpiToIndex(true, 1600) == 4);
    CHECK(MousePage::DpiToIndex(true, 16000) == 9);
    // A non-preset value snaps to the NEAREST preset for display (5000 → 6400 = idx 7).
    CHECK(MousePage::DpiToIndex(true, 5000) == 7);

    CHECK(MousePage::IndexToDpi(0) == 0); // Off sentinel
    CHECK(MousePage::IndexToDpi(1) == 400);
    CHECK(MousePage::IndexToDpi(4) == 1600);
    CHECK(MousePage::IndexToDpi(9) == 16000);
    CHECK(MousePage::IndexToDpi(99) == 0); // out of range → Off
}

TEST_CASE("MousePage: Mouse DPI row round-trips through the tuning", "[UI][MousePage]")
{
    MouseStateSnapshot snap;
    TestableMousePage page;
    auto& p = page.Provider();
    auto& sub = InputSubsystem::Instance();

    // Row 4 is Mouse DPI.  Index 0 = Off → normalization disabled.
    p.SetRowValue(4, 0);
    CHECK(sub.GetMouseTuning().dpiNormalize == false);
    CHECK(p.RowValue(4) == 0);

    // Index 4 = 1600 → normalization on, mouseDpi 1600.
    p.SetRowValue(4, 4);
    CHECK(sub.GetMouseTuning().dpiNormalize == true);
    CHECK(sub.GetMouseTuning().mouseDpi == 1600);
    CHECK(p.RowValue(4) == 4);

    // Index 9 = 16000.
    p.SetRowValue(4, 9);
    CHECK(sub.GetMouseTuning().mouseDpi == 16000);
    CHECK(p.RowValue(4) == 9);
}

TEST_CASE("MousePage: advanced tuning rows round-trip through MouseTuning", "[UI][MousePage]")
{
    MouseStateSnapshot snap;
    TestableMousePage page;
    auto& p = page.Provider();
    auto& sub = InputSubsystem::Instance();

    p.SetRowValue(6, 0);
    CHECK(sub.GetMouseTuning().smoothing == Catch::Approx(0.0f));
    CHECK(p.RowValue(6) == 0);
    p.SetRowValue(6, 50);
    CHECK(sub.GetMouseTuning().smoothing == Catch::Approx(0.475f));
    CHECK(p.RowValue(6) == 50);
    p.SetRowValue(6, 100);
    CHECK(sub.GetMouseTuning().smoothing == Catch::Approx(0.95f));
    CHECK(p.RowValue(6) == 100);

    p.SetRowValue(7, 1);
    CHECK(sub.GetMouseTuning().acceleration == true);
    CHECK(p.RowValue(7) == 1);
    p.SetRowValue(7, 0);
    CHECK(sub.GetMouseTuning().acceleration == false);
    CHECK(p.RowValue(7) == 0);

    p.SetRowValue(8, 0);
    CHECK(sub.GetMouseTuning().accelExponent == Catch::Approx(1.0f));
    p.SetRowValue(8, 50);
    CHECK(sub.GetMouseTuning().accelExponent == Catch::Approx(1.5f));
    CHECK(p.RowValue(8) == 50);
    p.SetRowValue(8, 100);
    CHECK(sub.GetMouseTuning().accelExponent == Catch::Approx(2.0f));

    p.SetRowValue(9, 0);
    CHECK(sub.GetMouseTuning().menuCursorScale == Catch::Approx(0.1f));
    p.SetRowValue(9, 50);
    CHECK(sub.GetMouseTuning().menuCursorScale == Catch::Approx(2.05f));
    CHECK(p.RowValue(9) == 50);
    p.SetRowValue(9, 100);
    CHECK(sub.GetMouseTuning().menuCursorScale == Catch::Approx(4.0f));
}

TEST_CASE("MousePage: a hand-edited non-preset DPI is preserved on read", "[UI][MousePage]")
{
    MouseStateSnapshot snap;
    TestableMousePage page;
    auto& p = page.Provider();
    auto& sub = InputSubsystem::Instance();

    // Simulate a value hand-edited into mouse.cfg that isn't a menu preset.
    sub.GetMouseTuning().dpiNormalize = true;
    sub.GetMouseTuning().mouseDpi = 5000;

    // Reading the row for display returns the nearest preset index...
    CHECK(p.RowValue(4) == 7); // nearest to 5000 is 6400
    // ...but must NOT mutate the stored custom value.
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
