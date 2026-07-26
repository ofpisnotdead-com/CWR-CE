#include <Poseidon/UI/Options/OptionsScrollList.hpp>
#include <Poseidon/UI/Options/OptionsShell.hpp>
#include <Poseidon/UI/OptionsUI.hpp>
#include <Poseidon/UI/Controls/UIControlsBase.hpp>
#include <Poseidon/UI/UITestEngine.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstring>

using namespace Poseidon;
TEST_CASE("optionsUI compiles", "[optionsUI][tier3]")
{
    REQUIRE(sizeof(AbstractOptionsUI) > 0);
}

class TestableOptionsShell : public OptionsShell
{
  public:
    TestableOptionsShell(bool enableSimulation, bool credits) : OptionsShell(nullptr, enableSimulation, credits) {}
};

class TestableOptionsPage : public OptionsPage
{
  public:
    using OptionsPage::ContainsCycleIdc;

    const char* TitleText() const override { return ""; }
    int DefaultFocusIdc() const override { return -1; }
    const char* ResourceClassName() const override { return ""; }
};

class TestSemanticControl : public IControl
{
  public:
    TestSemanticControl() : IControl(nullptr, 42) {}

    int GetType() override { return 0; }
    int GetStyle() override { return 0; }
    bool IsInside(float, float) override { return false; }
    void Move(float, float) override {}
    void OnDraw(float) override {}
};

class TestHtmlContainer : public CHTMLContainer
{
  public:
    void SelectSection(const char* name) { _currentSection = FindSection(name); }

    float GetPageWidth() const override { return 1000; }
    float GetPageHeight() const override { return 1000; }
    float GetTextWidth(float, Font*, const char* text) const override { return std::strlen(text); }
};

TEST_CASE("OptionsShell propagates the simulation flag to the display base", "[optionsUI][UI]")
{
    TestableOptionsShell pausedShell(false, false);
    CHECK(pausedShell.EnableSimulation() == false);
    CHECK(pausedShell.SimulationEnabled() == false);

    TestableOptionsShell runningShell(true, false);
    CHECK(runningShell.EnableSimulation() == true);
    CHECK(runningShell.SimulationEnabled() == true);
}

TEST_CASE("OptionsScrollList maps every slot-local control IDC back to its slot", "[optionsUI][UI]")
{
    for (int digit = 0; digit <= 9; ++digit)
        CHECK(OptionsScrollList::SlotForControlIdc(540 + digit) == 4);

    CHECK(OptionsScrollList::SlotForControlIdc(499) == -1);
    CHECK(OptionsScrollList::SlotForControlIdc(590) == -1);
    CHECK(OptionsScrollList::SlotForControlIdc(700) == -1);
}

TEST_CASE("OptionsScrollList row policy keeps disabled rows focusable but inert", "[optionsUI][UI]")
{
    CHECK(OptionsScrollList::CanRowReceiveFocus(OptionsScrollList::KindHeader) == false);
    CHECK(OptionsScrollList::CanRowReceiveFocus(OptionsScrollList::KindAction) == true);
    CHECK(OptionsScrollList::CanRowReceiveFocus(OptionsScrollList::KindSlider) == true);

    CHECK(OptionsScrollList::CanRowAdjustValue(OptionsScrollList::KindStepper, false) == true);
    CHECK(OptionsScrollList::CanRowAdjustValue(OptionsScrollList::KindBoolean, false) == true);
    CHECK(OptionsScrollList::CanRowAdjustValue(OptionsScrollList::KindSlider, false) == true);
    CHECK(OptionsScrollList::CanRowAdjustValue(OptionsScrollList::KindAction, false) == false);
    CHECK(OptionsScrollList::CanRowAdjustValue(OptionsScrollList::KindBinding, false) == false);
    CHECK(OptionsScrollList::CanRowAdjustValue(OptionsScrollList::KindStepper, true) == false);

    CHECK(OptionsScrollList::CanRowInvokeAction(OptionsScrollList::KindAction, false) == true);
    CHECK(OptionsScrollList::CanRowInvokeAction(OptionsScrollList::KindAction, true) == false);
    CHECK(OptionsScrollList::CanRowOpenBinding(OptionsScrollList::KindBinding, false) == true);
    CHECK(OptionsScrollList::CanRowOpenBinding(OptionsScrollList::KindBinding, true) == false);
}

TEST_CASE("UITestEngine returns semantic text when controls render a clipped marquee", "[optionsUI][UI]")
{
    TestSemanticControl ctrl;

    UITestEngine::SetSemanticControlText(&ctrl, "Particles & Volumetrics");
    CHECK(UITestEngine::GetControlText(&ctrl) == "Particles & Volumetrics");

    UITestEngine::ClearSemanticControlText(&ctrl);
    CHECK(UITestEngine::GetControlText(&ctrl).empty());
}

TEST_CASE("UITestEngine returns text from the current HTML section", "[ui][html]")
{
    TestHtmlContainer html;
    html.LoadBuffer("inline.html", R"html(<html><body>
        <h1><a name="End1"></a>Mission complete</h1>
        <p>Selected result</p>
        <hr>
        <h1><a name="End2"></a>Hidden alternate result</h1>
    </body></html>)html");

    REQUIRE(html.NSections() == 2);
    html.SelectSection("End1");
    CHECK(UITestEngine::GetHtmlText(html) == "Mission complete Selected result");
    html.SelectSection("End2");
    CHECK(UITestEngine::GetHtmlText(html) == "Hidden alternate result");
    html.SelectSection("Missing");
    CHECK(UITestEngine::GetHtmlText(html).empty());
}

TEST_CASE("OptionsScrollList::FormatCell clips idle cells and marquees focused overflow", "[optionsUI][UI]")
{
    char buf[80];

    // A value within the budget is passed through untouched, idle or focused.
    OptionsScrollList::FormatCell("UP", OptionsScrollList::kBindingAltInnerChars, false, 0, buf, sizeof(buf));
    CHECK(std::string(buf) == "UP");
    OptionsScrollList::FormatCell("UP", OptionsScrollList::kBindingAltInnerChars, true, 0, buf, sizeof(buf));
    CHECK(std::string(buf) == "UP");

    // "Left Shift" (10) overflows the 6-char alt budget: clipped to the head
    // when idle, and also at the start of the marquee cycle (offset 0).
    OptionsScrollList::FormatCell("Left Shift", OptionsScrollList::kBindingAltInnerChars, false, 0, buf, sizeof(buf));
    CHECK(std::string(buf) == "Left S");
    OptionsScrollList::FormatCell("Left Shift", OptionsScrollList::kBindingAltInnerChars, true, 0, buf, sizeof(buf));
    CHECK(std::string(buf) == "Left S");

    // After the start pause plus a few scroll steps the focused window has
    // advanced past the head, so it no longer reads "Left S".
    const DWORD advanced = (DWORD)OptionsScrollList::kPauseMs + (DWORD)OptionsScrollList::kScrollPeriodMs * 4;
    OptionsScrollList::FormatCell("Left Shift", OptionsScrollList::kBindingAltInnerChars, true, advanced, buf,
                                  sizeof(buf));
    CHECK(std::string(buf) != "Left S");

    // An idle cell never advances no matter the elapsed time.
    OptionsScrollList::FormatCell("Left Shift", OptionsScrollList::kBindingAltInnerChars, false, advanced, buf,
                                  sizeof(buf));
    CHECK(std::string(buf) == "Left S");
}

TEST_CASE("OptionsPage cycle membership helper only accepts listed IDCs", "[optionsUI][UI]")
{
    const int cycle[] = {1101, 1104, 1107};
    TestableOptionsPage page;

    CHECK(page.ContainsCycleIdc(1101, cycle, 3));
    CHECK(page.ContainsCycleIdc(1107, cycle, 3));
    CHECK_FALSE(page.ContainsCycleIdc(1105, cycle, 3));
    CHECK_FALSE(page.ContainsCycleIdc(-1, cycle, 3));
    CHECK_FALSE(page.ContainsCycleIdc(1101, nullptr, 3));
}
