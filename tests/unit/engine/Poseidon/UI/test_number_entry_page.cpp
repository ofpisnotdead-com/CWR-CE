#include <Poseidon/UI/Options/NumberEntryPage.hpp>
#include <Poseidon/UI/Options/OptionsShell.hpp>
#include <catch2/catch_test_macros.hpp>

#include <SDL3/SDL_keycode.h>
#include <memory>

using namespace Poseidon;
namespace
{
class TestableOptionsShell : public OptionsShell
{
  public:
    TestableOptionsShell() : OptionsShell(nullptr, true, false) {}
};
} // namespace

TEST_CASE("NumberEntryPage parses only finite values inside its range", "[UI][NumberEntryPage]")
{
    REQUIRE(NumberEntryPage::ParseValue(" 900 ", 100.0f, 5000.0f));
    CHECK(*NumberEntryPage::ParseValue(" 900 ", 100.0f, 5000.0f) == 900.0f);
    CHECK_FALSE(NumberEntryPage::ParseValue("", 100.0f, 5000.0f));
    CHECK_FALSE(NumberEntryPage::ParseValue("900 m", 100.0f, 5000.0f));
    CHECK_FALSE(NumberEntryPage::ParseValue("99", 100.0f, 5000.0f));
    CHECK_FALSE(NumberEntryPage::ParseValue("5001", 100.0f, 5000.0f));
    CHECK_FALSE(NumberEntryPage::ParseValue("nan", 100.0f, 5000.0f));
}

TEST_CASE("NumberEntryPage cancel does not apply", "[UI][NumberEntryPage]")
{
    TestableOptionsShell shell;
    int applyCalls = 0;
    auto page = std::make_unique<NumberEntryPage>("Visibility distance", 900.0f, 100.0f, 5000.0f, 0, "m",
                                                  [&applyCalls](float) { ++applyCalls; });
    NumberEntryPage* raw = page.get();
    shell.PushPage(std::move(page));

    CHECK(raw->OnKeyDown(shell, SDLK_ESCAPE));
    CHECK(applyCalls == 0);
}

TEST_CASE("NumberEntryPage cycles actions through ordinary focus", "[UI][NumberEntryPage]")
{
    TestableOptionsShell shell;
    shell.DebugSetNotebookMountedIdcs({9481, 9401, 9402});
    REQUIRE(shell.FocusNotebookCtrl(9401));

    NumberEntryPage page("Visibility distance", 900.0f, 100.0f, 5000.0f, 0, "m", [](float) {});
    CHECK(page.OnKeyDown(shell, SDLK_DOWN));
    CHECK(shell.GetFocusedNotebookIdc() == 9402);
    CHECK(page.OnKeyDown(shell, SDLK_UP));
    CHECK(shell.GetFocusedNotebookIdc() == 9401);
}
