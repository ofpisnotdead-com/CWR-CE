#include <Poseidon/UI/InGame/PlayerFeedbackState.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>

using Poseidon::PlayerFeedbackState;

TEST_CASE("Player feedback restores a mission count without a new kill notice", "[UI][PlayerFeedback]")
{
    PlayerFeedbackState state;
    state.Observe(0.25f, true, 4, 10.0f, true);
    CHECK(state.HealthPercent() == 75);
    CHECK(state.Kills() == 4);
    CHECK(state.KillOpacity(10.0f) == 0);
    CHECK(state.DamageOpacity(10.0f) == 0);
    state.Observe(0.25f, true, 5, 11.0f, true);
    CHECK(state.KillOpacity(11.0f) == 1);
    CHECK(state.KillOpacity(13.25f) == Catch::Approx(0.5f));
    CHECK(state.KillOpacity(13.5f) == 0);
}

TEST_CASE("Player feedback clears transient notices on restart or load", "[UI][PlayerFeedback]")
{
    PlayerFeedbackState state;
    state.Observe(0, true, 2, 10, true);
    state.Observe(0.25f, true, 3, 11, true);
    REQUIRE(state.KillOpacity(11) == 1);
    SECTION("mission count decreases")
    {
        state.Observe(0, true, 1, 12, true);
    }
    SECTION("time moves backwards")
    {
        state.Observe(0, true, 3, 2, true);
    }
    SECTION("HUD is reset")
    {
        state.Reset();
        state.Observe(0, true, 3, 12, true);
    }
    CHECK(state.KillOpacity(12) == 0);
    CHECK(state.DamageOpacity(12) == 0);
}

TEST_CASE("Disabled kill feedback tracks kills without replaying them when enabled", "[UI][PlayerFeedback]")
{
    PlayerFeedbackState state;
    state.Observe(0, true, 0, 10, false);
    state.Observe(0, true, 1, 11, false);
    state.Observe(0, true, 1, 12, true);
    CHECK(state.Kills() == 1);
    CHECK(state.KillOpacity(12) == 0);
    state.Observe(0, true, 2, 13, true);
    REQUIRE(state.KillOpacity(13) == 1);
    state.Observe(0, true, 2, 13.1f, false);
    state.Observe(0, true, 2, 13.2f, true);
    CHECK(state.KillOpacity(13.2f) == 0);
}

TEST_CASE("Health feedback clamps damage and flashes only when health decreases", "[UI][PlayerFeedback]")
{
    PlayerFeedbackState state;
    state.Observe(-0.1f, true, 0, 10, true);
    CHECK(state.HealthPercent() == 100);
    state.Observe(0.25f, true, 0, 11, true);
    CHECK(state.HealthPercent() == 75);
    CHECK(state.DamageOpacity(11) == Catch::Approx(1));
    CHECK(state.DamageOpacity(11.3f) == Catch::Approx(0.5f).margin(0.001));
    state.Observe(0, true, 0, 12, true);
    CHECK(state.DamageOpacity(12) == 0);
    state.Observe(0.999f, true, 0, 13, true);
    CHECK(state.HealthPercent() == 1);
    state.Observe(0.5f, false, 0, 14, true);
    CHECK(state.HealthPercent() == 0);
    state.Observe(1.5f, true, 0, 15, true);
    CHECK(state.Health() == 0);
    state.Observe(std::numeric_limits<float>::quiet_NaN(), true, 0, 16, true);
    CHECK(state.HealthPercent() == 0);
}

TEST_CASE("Hidden feedback tracks changes without replaying transient notices", "[UI][PlayerFeedback]")
{
    PlayerFeedbackState state;
    REQUIRE(state.Visible());
    state.Observe(0, true, 0, 10, true);
    state.Observe(0.25f, true, 1, 11, true);
    REQUIRE(state.KillOpacity(11) == 1);
    REQUIRE(state.DamageOpacity(11) == 1);
    state.SetVisible(false);
    CHECK_FALSE(state.Visible());
    CHECK(state.KillOpacity(11) == 0);
    CHECK(state.DamageOpacity(11) == 0);
    state.Observe(0.5f, true, 2, 11.1f, true);
    state.SetVisible(true);
    CHECK(state.Visible());
    CHECK(state.HealthPercent() == 50);
    CHECK(state.Kills() == 2);
    CHECK(state.KillOpacity(11.1f) == 0);
    CHECK(state.DamageOpacity(11.1f) == 0);
    state.Observe(0.75f, true, 3, 12, true);
    CHECK(state.KillOpacity(12) == 1);
    CHECK(state.DamageOpacity(12) == 1);
}

TEST_CASE("HUD resets preserve the player's temporary visibility choice", "[UI][PlayerFeedback]")
{
    PlayerFeedbackState state;
    state.SetVisible(false);
    state.Reset();
    state.Observe(0.25f, true, 4, 10, true);
    CHECK_FALSE(state.Visible());
    CHECK(state.HealthPercent() == 75);
    CHECK(state.Kills() == 4);
    CHECK(state.KillOpacity(10) == 0);
}
