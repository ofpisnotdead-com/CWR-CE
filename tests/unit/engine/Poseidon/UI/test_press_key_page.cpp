#include <Poseidon/UI/Options/PressKeyPage.hpp>
#include <Poseidon/UI/Options/OptionsShell.hpp>
#include <Poseidon/Input/InputDeviceConstants.hpp>
#include <Poseidon/Input/KeyInput.hpp>
#include <catch2/catch_test_macros.hpp>

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>
#include <memory>
#include <string>
#include <utility>

using namespace Poseidon;
namespace Poseidon
{
extern Input GInput;
}

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

class HeldKey
{
  public:
    explicit HeldKey(SDL_Scancode sc) : m_sc(sc) { GInput.keyboard.keys[m_sc] = 0.0f; }

    ~HeldKey() { GInput.keyboard.keys[m_sc] = 0.0f; }

    void Set() { GInput.keyboard.keys[m_sc] = 1.0f; }

  private:
    SDL_Scancode m_sc;
};

class TestPressKeyPage : public PressKeyPage
{
  public:
    explicit TestPressKeyPage(CaptureResult& result, bool allowMouseWheel = false)
        : PressKeyPage(
              "Test Action", "Primary",
              [&result](int packedCode, int modifier, bool replaceConflict)
              {
                  result.savedCode = packedCode;
                  result.savedModifier = modifier;
                  result.replaceConflict = replaceConflict;
                  ++result.saveCalls;
              },
              [](int, int) { return UAN; }, allowMouseWheel)
    {
    }

    bool Listening() const { return IsListening(); }
    void Simulate(OptionsShell& shell) { PressKeyPage::OnSimulate(shell); }
};
} // namespace

TEST_CASE("PressKeyPage captures a lone modifier key", "[UI][PressKeyPage]")
{
    TestableOptionsShell shell;
    CaptureResult result;
    auto page = std::make_unique<TestPressKeyPage>(result);
    TestPressKeyPage* raw = page.get();

    shell.PushPage(std::move(page));

    // A modifier pressed on its own is a bindable key (Left Shift / Ctrl / Alt),
    // captured with no modifier of its own, not refused.
    CHECK(raw->OnKeyDown(shell, SDLK_LSHIFT));
    REQUIRE_FALSE(raw->Listening());

    raw->OnButtonClicked(shell, 9301);

    CHECK(result.saveCalls == 1);
    CHECK(result.savedCode == (int)SDL_SCANCODE_LSHIFT);
    CHECK(result.savedModifier == -1);
}

TEST_CASE("PressKeyPage captures a plain key without a modifier", "[UI][PressKeyPage]")
{
    TestableOptionsShell shell;
    CaptureResult result;
    auto page = std::make_unique<TestPressKeyPage>(result);
    TestPressKeyPage* raw = page.get();

    shell.PushPage(std::move(page));

    CHECK(raw->OnKeyDown(shell, SDLK_J));
    REQUIRE_FALSE(raw->Listening());

    raw->OnButtonClicked(shell, 9301);

    CHECK(result.saveCalls == 1);
    CHECK(result.savedCode == (int)SDL_SCANCODE_J);
    CHECK(result.savedModifier == -1);
    CHECK_FALSE(result.replaceConflict);
}

TEST_CASE("PressKeyPage upgrades repeated key capture to double-tap binding", "[UI][PressKeyPage]")
{
    TestableOptionsShell shell;
    CaptureResult result;
    auto page = std::make_unique<TestPressKeyPage>(result);
    TestPressKeyPage* raw = page.get();

    shell.PushPage(std::move(page));

    CHECK(raw->OnKeyDown(shell, SDLK_G));
    REQUIRE_FALSE(raw->Listening());
    CHECK(raw->OnKeyDown(shell, SDLK_G));

    raw->OnButtonClicked(shell, 9301);

    CHECK(result.saveCalls == 1);
    CHECK(result.savedCode == InputBindingDoubleTapCode((int)SDL_SCANCODE_G));
    CHECK(result.savedModifier == -1);
}

TEST_CASE("PressKeyPage simulate captures a pending mouse button", "[UI][PressKeyPage]")
{
    TestableOptionsShell shell;
    CaptureResult result;
    auto page = std::make_unique<TestPressKeyPage>(result);
    TestPressKeyPage* raw = page.get();

    shell.PushPage(std::move(page));

    GInput.mouse.buttonsToDo[2] = true;
    raw->Simulate(shell);
    GInput.mouse.buttonsToDo[2] = false;

    REQUIRE_FALSE(raw->Listening());

    raw->OnButtonClicked(shell, 9301);

    CHECK(result.saveCalls == 1);
    CHECK(result.savedCode == INPUT_DEVICE_MOUSE + 2);
    CHECK(result.savedModifier == -1);
}

TEST_CASE("PressKeyPage captures mouse wheel direction when enabled", "[UI][PressKeyPage]")
{
    TestableOptionsShell shell;
    CaptureResult result;
    auto page = std::make_unique<TestPressKeyPage>(result, true);
    TestPressKeyPage* raw = page.get();

    shell.PushPage(std::move(page));

    GInput.mouse.deltaZ = -1.0f;
    raw->Simulate(shell);
    GInput.mouse.deltaZ = 0.0f;

    REQUIRE_FALSE(raw->Listening());
    raw->OnButtonClicked(shell, 9301);

    CHECK(result.saveCalls == 1);
    CHECK(result.savedCode == INPUT_DEVICE_MOUSE_AXIS + INPUT_MOUSE_WHEEL_DOWN);
    CHECK(result.savedModifier == -1);
}

TEST_CASE("PressKeyPage upgrades repeated mouse capture to double-click binding", "[UI][PressKeyPage]")
{
    TestableOptionsShell shell;
    CaptureResult result;
    auto page = std::make_unique<TestPressKeyPage>(result);
    TestPressKeyPage* raw = page.get();

    shell.PushPage(std::move(page));

    GInput.mouse.buttonsToDo[1] = true;
    raw->Simulate(shell);
    raw->Simulate(shell);
    GInput.mouse.buttonsToDo[1] = false;

    REQUIRE_FALSE(raw->Listening());

    raw->OnButtonClicked(shell, 9301);

    CHECK(result.saveCalls == 1);
    CHECK(result.savedCode == InputBindingDoubleTapCode(INPUT_DEVICE_MOUSE + 1));
    CHECK(result.savedModifier == -1);
}

TEST_CASE("PressKeyPage snapshots a held modifier when capturing a key", "[UI][PressKeyPage]")
{
    TestableOptionsShell shell;
    CaptureResult result;
    HeldKey ctrl(SDL_SCANCODE_LCTRL);
    auto page = std::make_unique<TestPressKeyPage>(result);
    TestPressKeyPage* raw = page.get();

    shell.PushPage(std::move(page));

    ctrl.Set();
    CHECK(raw->OnKeyDown(shell, SDLK_J));
    REQUIRE_FALSE(raw->Listening());

    raw->OnButtonClicked(shell, 9301);

    CHECK(result.saveCalls == 1);
    CHECK(result.savedCode == (int)SDL_SCANCODE_J);
    CHECK(result.savedModifier == (int)SDL_SCANCODE_LCTRL);
}

TEST_CASE("PressKeyPage prefers Ctrl over Shift when multiple modifiers are held", "[UI][PressKeyPage]")
{
    TestableOptionsShell shell;
    CaptureResult result;
    HeldKey ctrl(SDL_SCANCODE_LCTRL);
    HeldKey shift(SDL_SCANCODE_LSHIFT);
    auto page = std::make_unique<TestPressKeyPage>(result);
    TestPressKeyPage* raw = page.get();

    shell.PushPage(std::move(page));

    ctrl.Set();
    shift.Set();
    CHECK(raw->OnKeyDown(shell, SDLK_K));
    REQUIRE_FALSE(raw->Listening());

    raw->OnButtonClicked(shell, 9301);

    CHECK(result.saveCalls == 1);
    CHECK(result.savedCode == (int)SDL_SCANCODE_K);
    CHECK(result.savedModifier == (int)SDL_SCANCODE_LCTRL);
}
