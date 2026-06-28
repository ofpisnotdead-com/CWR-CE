#include <Poseidon/UI/Options/FlatMenuPage.hpp>
#include <Poseidon/UI/Options/OptionsShell.hpp>
#include <catch2/catch_test_macros.hpp>

#include <SDL3/SDL_keycode.h>

#include <memory>
#include <utility>

using namespace Poseidon;
namespace
{
class TestableOptionsShell : public OptionsShell
{
  public:
    TestableOptionsShell() : OptionsShell(nullptr, true, false) {}
};

class DummyChildPage : public OptionsPage
{
  public:
    const char* TitleText() const override { return ""; }
    int DefaultFocusIdc() const override { return -1; }
    const char* ResourceClassName() const override { return ""; }
};

class TestFlatMenuPage : public FlatMenuPage
{
  public:
    TestFlatMenuPage() : FlatMenuPage(kCycle, kCycleSize) {}

    const char* TitleText() const override { return ""; }
    int DefaultFocusIdc() const override { return 1401; }
    const char* ResourceClassName() const override { return ""; }

    void Mount(OptionsShell& shell) override
    {
        shell.DebugSetNotebookMountedIdcs({1401, 1402, 1405, 1406, 1407, 1403, 1404});
    }
    void Unmount(OptionsShell& shell) override { shell.DebugClearNotebookMountedIdcs(); }

  protected:
    bool OnNav(OptionsShell& shell, int) override
    {
        shell.PushPage(std::make_unique<DummyChildPage>());
        return true;
    }

  private:
    static constexpr int kCycle[] = {1401, 1402, 1405, 1406, 1407, 1403, 1404};
    static constexpr int kCycleSize = sizeof(kCycle) / sizeof(kCycle[0]);
};
} // namespace

TEST_CASE("FlatMenuPage wraps focus upward from the first row to the close row", "[UI][FlatMenuPage]")
{
    TestableOptionsShell shell;
    auto page = std::make_unique<TestFlatMenuPage>();
    TestFlatMenuPage* raw = page.get();

    shell.PushPage(std::move(page));

    REQUIRE(shell.GetFocusedNotebookIdc() == 1401);
    CHECK(raw->OnKeyDown(shell, SDLK_UP));
    CHECK(shell.GetFocusedNotebookIdc() == 1404);
}

TEST_CASE("FlatMenuPage wraps focus downward from the close row to the first row", "[UI][FlatMenuPage]")
{
    TestableOptionsShell shell;
    auto page = std::make_unique<TestFlatMenuPage>();
    TestFlatMenuPage* raw = page.get();

    shell.PushPage(std::move(page));
    REQUIRE(shell.FocusNotebookCtrl(1404));

    REQUIRE(shell.GetFocusedNotebookIdc() == 1404);
    CHECK(raw->OnKeyDown(shell, SDLK_DOWN));
    CHECK(shell.GetFocusedNotebookIdc() == 1401);
}

TEST_CASE("FlatMenuPage restores the last activated nav row when reshown", "[UI][FlatMenuPage]")
{
    TestableOptionsShell shell;
    auto page = std::make_unique<TestFlatMenuPage>();
    TestFlatMenuPage* raw = page.get();

    shell.PushPage(std::move(page));

    REQUIRE(raw->OnButtonClicked(shell, 1406));
    shell.PopPage();

    CHECK(shell.GetFocusedNotebookIdc() == 1406);
}
