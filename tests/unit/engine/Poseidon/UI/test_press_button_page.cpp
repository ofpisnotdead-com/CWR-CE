#include <Poseidon/UI/Options/PressButtonPage.hpp>
#include <Poseidon/UI/Options/OptionsShell.hpp>
#include <Poseidon/Input/InputDeviceConstants.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <catch2/catch_test_macros.hpp>

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace Poseidon;
namespace
{
class TestableOptionsShell : public OptionsShell
{
  public:
    TestableOptionsShell() : OptionsShell(nullptr, true, false) {}
};

struct CaptureResult
{
    int savedCode = -1;
    int savedModifier = -1;
    bool replaceConflict = false;
    int saveCalls = 0;
};

class SyntheticStickEdge
{
  public:
    SyntheticStickEdge(int index, bool pov) : m_index(index), m_pov(pov)
    {
        auto& input = InputSubsystem::Instance();
        if (m_pov)
            input.SetSyntheticStickPov(m_index, false);
        else
            input.SetSyntheticStickButton(m_index, false);
    }

    ~SyntheticStickEdge()
    {
        auto& input = InputSubsystem::Instance();
        if (m_pov)
            input.SetSyntheticStickPov(m_index, false);
        else
            input.SetSyntheticStickButton(m_index, false);
    }

    void Set()
    {
        auto& input = InputSubsystem::Instance();
        if (m_pov)
            input.SetSyntheticStickPov(m_index, true);
        else
            input.SetSyntheticStickButton(m_index, true);
    }

  private:
    int m_index;
    bool m_pov;
};

class SyntheticLeftStickMove
{
  public:
    ~SyntheticLeftStickMove()
    {
        auto& input = InputSubsystem::Instance();
        input.SetSyntheticLeftStick(0.0f, 0.0f);
        input.ConsumeAxisBigActive(0);
        input.ConsumeAxisBigActive(1);
    }

    void Set(float x, float y) { InputSubsystem::Instance().SetSyntheticLeftStick(x, y); }
};

class TestPressButtonPage : public PressButtonPage
{
  public:
    explicit TestPressButtonPage(CaptureResult& result)
        : PressButtonPage(
              "Test Action", "Primary",
              [&result](int packedCode, int modifier, bool replaceConflict)
              {
                  result.savedCode = packedCode;
                  result.savedModifier = modifier;
                  result.replaceConflict = replaceConflict;
                  ++result.saveCalls;
              },
              [](int, int) { return std::vector<UserAction>{}; })
    {
    }

    bool Listening() const { return IsListening(); }
    bool KeyDown(OptionsShell& shell, unsigned nChar) { return PressButtonPage::OnKeyDown(shell, nChar); }
    void Simulate(OptionsShell& shell) { PressButtonPage::OnSimulate(shell); }
    Result Interpret(unsigned nChar, int& outPackedCode, int& outModifier) const
    {
        return PressButtonPage::InterpretKey(nChar, outPackedCode, outModifier);
    }
};
} // namespace

TEST_CASE("PressButtonPage simulate captures a synthetic stick button", "[UI][PressButtonPage]")
{
    TestableOptionsShell shell;
    CaptureResult result;
    SyntheticStickEdge edge(2, false);
    auto page = std::make_unique<TestPressButtonPage>(result);
    TestPressButtonPage* raw = page.get();

    shell.PushPage(std::move(page));

    edge.Set();
    raw->Simulate(shell);
    REQUIRE_FALSE(raw->Listening());

    raw->OnButtonClicked(shell, 9301);

    CHECK(result.saveCalls == 1);
    CHECK(result.savedCode == INPUT_DEVICE_STICK + 2);
    CHECK(result.savedModifier == -1);
    CHECK_FALSE(result.replaceConflict);
}

TEST_CASE("PressButtonPage prefers a synthetic gamepad edge over Esc while listening", "[UI][PressButtonPage]")
{
    TestableOptionsShell shell;
    CaptureResult result;
    SyntheticStickEdge edge(1, false);
    auto page = std::make_unique<TestPressButtonPage>(result);
    TestPressButtonPage* raw = page.get();

    shell.PushPage(std::move(page));

    edge.Set();
    CHECK(raw->KeyDown(shell, SDLK_ESCAPE));
    REQUIRE_FALSE(raw->Listening());

    raw->OnButtonClicked(shell, 9301);

    CHECK(result.saveCalls == 1);
    CHECK(result.savedCode == INPUT_DEVICE_STICK + 1);
    CHECK(result.savedModifier == -1);
}

TEST_CASE("PressButtonPage simulate captures a synthetic POV edge", "[UI][PressButtonPage]")
{
    TestableOptionsShell shell;
    CaptureResult result;
    SyntheticStickEdge edge(4, true);
    auto page = std::make_unique<TestPressButtonPage>(result);
    TestPressButtonPage* raw = page.get();

    shell.PushPage(std::move(page));

    edge.Set();
    raw->Simulate(shell);
    REQUIRE_FALSE(raw->Listening());

    raw->OnButtonClicked(shell, 9301);

    CHECK(result.saveCalls == 1);
    CHECK(result.savedCode == INPUT_DEVICE_STICK_POV + 4);
    CHECK(result.savedModifier == -1);
}

TEST_CASE("PressButtonPage simulate captures a synthetic left-stick axis", "[UI][PressButtonPage]")
{
    TestableOptionsShell shell;
    CaptureResult result;
    SyntheticLeftStickMove stick;
    auto page = std::make_unique<TestPressButtonPage>(result);
    TestPressButtonPage* raw = page.get();

    shell.PushPage(std::move(page));

    stick.Set(1.0f, 0.0f);
    raw->Simulate(shell);
    REQUIRE_FALSE(raw->Listening());

    raw->OnButtonClicked(shell, 9301);

    CHECK(result.saveCalls == 1);
    CHECK(result.savedCode == INPUT_DEVICE_STICK_AXIS + 0);
    CHECK(result.savedModifier == -1);
}

TEST_CASE("PressButtonPage keyboard fallback refuses modifiers and accepts scancodes", "[UI][PressButtonPage]")
{
    CaptureResult result;
    TestPressButtonPage page(result);
    int packedCode = -1;
    int modifier = -2;

    CHECK(page.Interpret(SDLK_LSHIFT, packedCode, modifier) == CapturePage::Result::Refused);

    CHECK(page.Interpret(SDLK_J, packedCode, modifier) == CapturePage::Result::Main);
    CHECK(packedCode == (int)SDL_SCANCODE_J);
    CHECK(modifier == -1);
}
