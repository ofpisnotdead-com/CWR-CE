#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Core/Application.hpp>

using Poseidon::ShouldHonorWindowClose;
using Poseidon::ShouldReportInGameplayForWindowClose;

// Alt+F4 must quit from non-game contexts (menus, briefing, the Esc dialog) but
// stay a valid in-game shortcut (Alt = freelook, F4 = select unit 4) during active
// gameplay. On Windows the OS turns Alt+F4 into a window-close request, so the
// window layer decides whether to honour a close from (altDown, inGameplay,
// userInputEnabled): honour unless Alt is held AND the player is in active gameplay
// AND the game can actually receive the shortcut.
//
// ShouldHonorWindowClose is the pure predicate tested here.  IsInGameplay() is
// the runtime gate that feeds inGameplay — tested via triInGameplay in the
// altf4_menu_context and altf4_esc_menu_context integration tests.
//
// Broken-state delta: the old handler ignored the close whenever Alt was held, in
// ANY context — so Alt+F4 in a menu did nothing. That is the (altDown, !inGameplay)
// row: it must honour (quit), and did not before the inGameplay gate was added.
// A second regression: IsInGameplay() returned true in GModeIntro (main menu) because
// DisplayMain has _enableUI=true by default; fixed by the GetMode() != GModeIntro guard.

TEST_CASE("Alt+F4 window close is honoured everywhere except active gameplay", "[core][application][window]")
{
    // No Alt: always a deliberate close (title-bar X, taskbar, menu Quit).
    REQUIRE(ShouldHonorWindowClose(false, false, true)); // menu, plain close
    REQUIRE(ShouldHonorWindowClose(false, true, true));  // in-game, deliberate X / taskbar

    // Alt held: quit from menus/briefing/Esc; reserved as a shortcut only in gameplay.
    REQUIRE(ShouldHonorWindowClose(true, false, true));      // the fix: Alt+F4 in a menu quits
    REQUIRE_FALSE(ShouldHonorWindowClose(true, true, true)); // Alt+F4 in gameplay = shortcut, ignored
}

TEST_CASE("Alt+F4 quits while a script holds disableUserInput", "[core][application][window]")
{
    // KeyboardState::Update drops the whole key buffer while input is disabled, so the
    // F4 half of the shortcut never reaches the game and Esc cannot open the interrupt
    // menu either. Keeping the close ignored there strands a fullscreen player, which is
    // what a CTI mission does to anyone it blocks.
    REQUIRE(ShouldHonorWindowClose(/*altDown*/ true, /*inGameplay*/ true, /*userInputEnabled*/ false));

    // Input disabled outside gameplay is unchanged: the close was already honoured.
    REQUIRE(ShouldHonorWindowClose(/*altDown*/ true, /*inGameplay*/ false, /*userInputEnabled*/ false));
    REQUIRE(ShouldHonorWindowClose(/*altDown*/ false, /*inGameplay*/ true, /*userInputEnabled*/ false));
}

TEST_CASE("startup splash/progress is not treated as active gameplay for Alt+F4",
          "[core][application][window][splash][fizzy242]")
{
    // Startup progress frames already have a world and can have UI enabled before the
    // menu intro is fully settled. They are still loading/splash, not player control,
    // so Alt+F4 must be honoured there.
    REQUIRE_FALSE(ShouldReportInGameplayForWindowClose(/*hasWorld*/ true, /*introMode*/ false,
                                                       /*uiEnabled*/ true, /*startupProgressActive*/ true));
    REQUIRE(ShouldHonorWindowClose(/*altDown*/ true,
                                   ShouldReportInGameplayForWindowClose(/*hasWorld*/ true, /*introMode*/ false,
                                                                        /*uiEnabled*/ true,
                                                                        /*startupProgressActive*/ true),
                                   /*userInputEnabled*/ true));

    // Once progress is gone, the same non-intro world with UI enabled is real gameplay;
    // this preserves the existing Alt+F4 shortcut behavior.
    REQUIRE(ShouldReportInGameplayForWindowClose(/*hasWorld*/ true, /*introMode*/ false,
                                                 /*uiEnabled*/ true, /*startupProgressActive*/ false));
    REQUIRE_FALSE(ShouldHonorWindowClose(/*altDown*/ true,
                                         ShouldReportInGameplayForWindowClose(/*hasWorld*/ true,
                                                                              /*introMode*/ false,
                                                                              /*uiEnabled*/ true,
                                                                              /*startupProgressActive*/ false),
                                         /*userInputEnabled*/ true));
}
