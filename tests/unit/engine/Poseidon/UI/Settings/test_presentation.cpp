#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Poseidon/UI/Settings/Presentation.hpp"

// Presentation::Resolve — the single aspect decision point's rule table,
// exercised through the public entry (the AspectRatio resolvers are
// implementation detail; see test_presentation_single_entry.cpp).

TEST_CASE("Presentation: 16:9 resolves full-width undistorted UI", "[Settings][Presentation]")
{
    Poseidon::Presentation::SetPolicy(Poseidon::AspectRatio::Modern, Poseidon::AspectRatio::Clamp21x9);
    const Poseidon::AspectRatio::Settings s = Poseidon::Presentation::Resolve(1920, 1080);
    CHECK(s.leftFOV == Catch::Approx(4.0f / 3.0f));
    CHECK(s.topFOV == Catch::Approx(0.75f));
    CHECK(s.uiTopLeftX == Catch::Approx(0.0f));
    CHECK(s.uiBottomRightX == Catch::Approx(1.0f));
    CHECK(s.worldLeft == Catch::Approx(0.0f));
    CHECK(s.worldRight == Catch::Approx(1.0f));
}

TEST_CASE("Presentation: 32:9 Adaptive 21:9 keeps full-width world FOV, clamps HUD width", "[Settings][Presentation]")
{
    Poseidon::Presentation::SetPolicy(Poseidon::AspectRatio::Modern, Poseidon::AspectRatio::Clamp21x9);
    const Poseidon::AspectRatio::Settings s = Poseidon::Presentation::Resolve(3840, 1080);
    CHECK(s.leftFOV == Catch::Approx(0.75f * (32.0f / 9.0f)));
    CHECK(s.worldLeft == Catch::Approx(0.0f));
    CHECK(s.worldRight == Catch::Approx(1.0f));
    const float uiW = (s.uiBottomRightX - s.uiTopLeftX) * 3840.0f;
    const float uiH = (s.uiBottomRightY - s.uiTopLeftY) * 1080.0f;
    CHECK(uiW / uiH == Catch::Approx(21.0f / 9.0f).epsilon(0.001f));
    CHECK(s.uiTopLeftX == Catch::Approx(0.171875f));
    CHECK(s.uiTopLeftX == Catch::Approx(1.0f - s.uiBottomRightX)); // centered
}

TEST_CASE("Presentation: 32:9 Adaptive 16:9 uses the narrower HUD width limit", "[Settings][Presentation]")
{
    Poseidon::Presentation::SetPolicy(Poseidon::AspectRatio::Modern, Poseidon::AspectRatio::Clamp16x9);
    const Poseidon::AspectRatio::Settings s = Poseidon::Presentation::Resolve(3840, 1080);

    CHECK(s.leftFOV == Catch::Approx(0.75f * (32.0f / 9.0f)));
    CHECK(s.uiTopLeftX == Catch::Approx(0.25f));
    CHECK(s.uiBottomRightX == Catch::Approx(0.75f));

    Poseidon::Presentation::SetPolicy(Poseidon::AspectRatio::Modern, Poseidon::AspectRatio::Clamp21x9);
}

TEST_CASE("Presentation: applied policy can select Original stretched HUD", "[Settings][Presentation]")
{
    Poseidon::Presentation::SetPolicy(Poseidon::AspectRatio::Legacy, Poseidon::AspectRatio::Clamp16x9);
    const Poseidon::AspectRatio::Settings s = Poseidon::Presentation::Resolve(3840, 1080);

    CHECK(s.leftFOV == Catch::Approx(0.75f * (32.0f / 9.0f)));
    CHECK(s.topFOV == Catch::Approx(0.75f));
    CHECK(s.uiTopLeftX == Catch::Approx(0.0f));
    CHECK(s.uiBottomRightX == Catch::Approx(1.0f));
    CHECK(s.worldLeft == Catch::Approx(0.0f));
    CHECK(s.worldRight == Catch::Approx(1.0f));

    Poseidon::Presentation::SetPolicy(Poseidon::AspectRatio::Modern, Poseidon::AspectRatio::Clamp21x9);
}

TEST_CASE("Presentation: Adaptive can turn HUD width limit off", "[Settings][Presentation]")
{
    Poseidon::Presentation::SetPolicy(Poseidon::AspectRatio::Modern, Poseidon::AspectRatio::ClampOff);
    const Poseidon::AspectRatio::Settings s = Poseidon::Presentation::Resolve(3840, 1080);

    CHECK(s.leftFOV == Catch::Approx(0.75f * (32.0f / 9.0f)));
    CHECK(s.uiTopLeftX == Catch::Approx(0.0f));
    CHECK(s.uiBottomRightX == Catch::Approx(1.0f));

    Poseidon::Presentation::SetPolicy(Poseidon::AspectRatio::Modern, Poseidon::AspectRatio::Clamp21x9);
}

TEST_CASE("Presentation: live dev override drives the resolve", "[Settings][Presentation]")
{
    Poseidon::AspectRatio::LiveControls& live = Poseidon::AspectRatio::Live();
    const Poseidon::AspectRatio::LiveControls saved = live;

    live.overrideEnabled = true;
    live.manualRect = true;
    live.rectL = 0.25f;
    live.rectT = 0.0f;
    live.rectR = 0.75f;
    live.rectB = 1.0f;

    const Poseidon::AspectRatio::Settings s = Poseidon::Presentation::Resolve(1920, 1080);
    CHECK(s.worldLeft == Catch::Approx(0.25f));
    CHECK(s.worldRight == Catch::Approx(0.75f));
    // The world FOV matches the flat_quad's aspect — no stretch.
    const float rectW = (s.worldRight - s.worldLeft) * 1920.0f;
    const float rectH = (s.worldBottom - s.worldTop) * 1080.0f;
    CHECK(s.leftFOV / s.topFOV == Catch::Approx(rectW / rectH).epsilon(0.001f));

    live = saved;
    // Override off again: back to the fixed policy.
    Poseidon::Presentation::SetPolicy(Poseidon::AspectRatio::Modern, Poseidon::AspectRatio::Clamp21x9);
    const Poseidon::AspectRatio::Settings p = Poseidon::Presentation::Resolve(1920, 1080);
    CHECK(p.worldLeft == Catch::Approx(0.0f));
    CHECK(p.worldRight == Catch::Approx(1.0f));
}
