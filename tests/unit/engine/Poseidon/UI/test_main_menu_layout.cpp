#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <Poseidon/UI/MainMenuLayout.hpp>

using Poseidon::CalculateMainMenuModsPlacement;
using Poseidon::MainMenuControlRect;

TEST_CASE("main menu Mods placement sits left of Quit when horizontal space exists", "[ui][main-menu][mods]")
{
    const MainMenuControlRect quit{0.70f, 0.80f, 0.20f, 0.05f, true};

    const auto placement = CalculateMainMenuModsPlacement(quit, {});

    CHECK(placement.rightAlign);
    CHECK(placement.x == Catch::Approx(0.49f));
    CHECK(placement.y == Catch::Approx(quit.y));
    CHECK(placement.w == Catch::Approx(quit.w));
    CHECK(placement.h == Catch::Approx(quit.h));
}

TEST_CASE("main menu Mods placement stacks above left-edge Quit when clear", "[ui][main-menu][mods]")
{
    const MainMenuControlRect quit{0.04f, 0.80f, 0.20f, 0.05f, true};

    const auto placement = CalculateMainMenuModsPlacement(quit, {});

    CHECK_FALSE(placement.rightAlign);
    CHECK(placement.x == Catch::Approx(quit.x));
    CHECK(placement.y == Catch::Approx(0.74f));
}

TEST_CASE("main menu Mods placement avoids foreground overlap above Quit", "[ui][main-menu][mods]")
{
    const MainMenuControlRect quit{0.04f, 0.80f, 0.20f, 0.05f, true};
    const std::vector<MainMenuControlRect> foreground{{0.03f, 0.73f, 0.22f, 0.06f, true}};

    const auto placement = CalculateMainMenuModsPlacement(quit, foreground);

    CHECK_FALSE(placement.rightAlign);
    CHECK(placement.x == Catch::Approx(quit.x));
    CHECK(placement.y == Catch::Approx(0.86f));
}
