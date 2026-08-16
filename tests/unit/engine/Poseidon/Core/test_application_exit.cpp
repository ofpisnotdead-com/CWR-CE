#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Core/Application.hpp>

using Poseidon::ResolveMultiplayerAutomationExitCode;
using Poseidon::ResolveMultiplayerAutoTestExitCode;

TEST_CASE("multiplayer auto-test only normalizes an intentional successful end",
          "[core][application][multiplayer][exit]")
{
    CHECK(ResolveMultiplayerAutoTestExitCode(2, true, true) == 0);

    CHECK(ResolveMultiplayerAutoTestExitCode(2, false, true) == 2);
    CHECK(ResolveMultiplayerAutoTestExitCode(2, true, false) == 2);
}

TEST_CASE("multiplayer auto-test preserves post-play failures", "[core][application][multiplayer][exit]")
{
    for (const int failure : {1, 2, 3, 4, 44})
    {
        CAPTURE(failure);
        CHECK(ResolveMultiplayerAutoTestExitCode(failure, true, false) == failure);
    }

    for (const int failure : {1, 3, 4, 44})
    {
        CAPTURE(failure);
        CHECK(ResolveMultiplayerAutoTestExitCode(failure, true, true) == failure);
    }
}

TEST_CASE("multiplayer automation outcome preserves an already-requested failure",
          "[core][application][multiplayer][exit]")
{
    for (const int failure : {1, 2, 3, 4, 44})
    {
        CAPTURE(failure);
        CHECK(ResolveMultiplayerAutomationExitCode(failure, true, true) == failure);
        CHECK(ResolveMultiplayerAutomationExitCode(failure, true, false) == failure);
    }
}

TEST_CASE("multiplayer disconnect reports normal mission outcome", "[core][application][multiplayer][exit]")
{
    CHECK(ResolveMultiplayerAutomationExitCode(2, false, true) == 0);
    CHECK(ResolveMultiplayerAutomationExitCode(2, false, false) == 2);
}
