#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <Poseidon/UI/Settings/AspectRatio.hpp>
#include <catch2/catch_message.hpp>
#include <initializer_list>

using namespace Poseidon;

TEST_CASE("AspectRatio LateralPillarboxWidth: zero on 4:3 and narrower", "[Settings][AspectRatio][Pillarbox]")
{
    // 4:3 exactly → no lateral bars needed.
    CHECK(AspectRatio::LateralPillarboxWidth(800, 600) == Catch::Approx(0.0f));
    // Narrower than 4:3 (e.g. 5:4 portrait-ish) → still no lateral bars.
    CHECK(AspectRatio::LateralPillarboxWidth(1280, 1024) == Catch::Approx(0.0f));
    // Square viewport → 4:3 model overflows vertically but no lateral bars
    // (function only handles the wider-than-4:3 case).
    CHECK(AspectRatio::LateralPillarboxWidth(1080, 1080) == Catch::Approx(0.0f));
}

TEST_CASE("AspectRatio LateralPillarboxWidth: 16:9 and 21:9 viewports", "[Settings][AspectRatio][Pillarbox]")
{
    // 16:9 at 1920x1080: center band is 1080 * 4/3 = 1440; surplus
    // = 1920 - 1440 = 480, split → 240 px per lateral bar.
    CHECK(AspectRatio::LateralPillarboxWidth(1920, 1080) == Catch::Approx(240.0f));
    // 21:9 at 2560x1080: center band 1440 → surplus 1120 → 560 per bar.
    CHECK(AspectRatio::LateralPillarboxWidth(2560, 1080) == Catch::Approx(560.0f));
    // 16:9 at 1280x720: 720 * 4/3 = 960, surplus 320, 160 per bar.
    CHECK(AspectRatio::LateralPillarboxWidth(1280, 720) == Catch::Approx(160.0f));
}

TEST_CASE("AspectRatio LateralPillarboxWidth: handles bad inputs", "[Settings][AspectRatio][Pillarbox]")
{
    CHECK(AspectRatio::LateralPillarboxWidth(0, 0) == Catch::Approx(0.0f));
    CHECK(AspectRatio::LateralPillarboxWidth(-1, 100) == Catch::Approx(0.0f));
    CHECK(AspectRatio::LateralPillarboxWidth(100, -1) == Catch::Approx(0.0f));
}

TEST_CASE("AspectRatio ArePillarboxBarsEnabled: defaults true, round-trips", "[Settings][AspectRatio][Pillarbox]")
{
    // Default state: enabled.  Captured up front so we can restore at
    // the end regardless of any earlier-in-file mutation.
    const bool before = AspectRatio::ArePillarboxBarsEnabled();
    REQUIRE(before == true);

    AspectRatio::SetPillarboxBarsEnabled(false);
    CHECK(AspectRatio::ArePillarboxBarsEnabled() == false);

    AspectRatio::SetPillarboxBarsEnabled(true);
    CHECK(AspectRatio::ArePillarboxBarsEnabled() == true);

    // Restore.
    AspectRatio::SetPillarboxBarsEnabled(before);
}

TEST_CASE("AspectRatio IsGameplayActive: defaults false, round-trips", "[Settings][AspectRatio][Pillarbox]")
{
    // Default: boot state is menu intro, not gameplay — so pillarbox
    // helper no-ops on the random cutscene behind the main menu.
    // World flips this true on mission boot (InitVehicles for non-
    // Intro modes, InitClient for Netware).
    const bool before = AspectRatio::IsGameplayActive();
    REQUIRE(before == false);

    AspectRatio::SetGameplayActive(true);
    CHECK(AspectRatio::IsGameplayActive() == true);

    AspectRatio::SetGameplayActive(false);
    CHECK(AspectRatio::IsGameplayActive() == false);

    AspectRatio::SetGameplayActive(before);
}

TEST_CASE("AspectRatio widens FOV and centers UI for widescreen", "[Settings][AspectRatio]")
{
    const AspectRatio::Settings settings = AspectRatio::BuildSettingsForRatio(16.0f / 9.0f);

    CHECK(settings.leftFOV == Catch::Approx(4.0f / 3.0f));
    CHECK(settings.topFOV == Catch::Approx(0.75f));
    CHECK(settings.uiTopLeftX == Catch::Approx(0.125f));
    CHECK(settings.uiBottomRightX == Catch::Approx(0.875f));
    CHECK(settings.uiTopLeftY == Catch::Approx(0.0f));
    CHECK(settings.uiBottomRightY == Catch::Approx(1.0f));
}

TEST_CASE("AspectRatio expands vertical framing for taller displays", "[Settings][AspectRatio]")
{
    const AspectRatio::Settings settings = AspectRatio::BuildSettingsForRatio(5.0f / 4.0f);

    CHECK(settings.leftFOV == Catch::Approx(1.0f));
    CHECK(settings.topFOV == Catch::Approx(0.8f));
    CHECK(settings.uiTopLeftX == Catch::Approx(0.0f));
    CHECK(settings.uiBottomRightX == Catch::Approx(1.0f));
    CHECK(settings.uiTopLeftY == Catch::Approx(0.03125f));
    CHECK(settings.uiBottomRightY == Catch::Approx(0.96875f));
}

TEST_CASE("AspectRatio Modern keeps the live viewport ratio when under the clamp", "[Settings][AspectRatio]")
{
    AspectRatio::PolicyInput input;
    input.viewportWidth = 2560;
    input.viewportHeight = 1440;
    input.style = AspectRatio::Modern;
    input.ultrawideClamp = AspectRatio::Clamp21x9;

    const AspectRatio::PolicyResult result = AspectRatio::ResolvePolicy(input);

    CHECK(result.viewportRatio == Catch::Approx(16.0f / 9.0f));
    CHECK(result.effectiveRatio == Catch::Approx(16.0f / 9.0f));
    CHECK(result.settings.uiTopLeftX == Catch::Approx(0.0f));
    CHECK(result.settings.uiBottomRightX == Catch::Approx(1.0f));
}

TEST_CASE("AspectRatio Modern uses full-width UI on 3:2 displays", "[Settings][AspectRatio]")
{
    AspectRatio::PolicyInput input;
    input.viewportWidth = 2880;
    input.viewportHeight = 1920;
    input.style = AspectRatio::Modern;
    input.ultrawideClamp = AspectRatio::Clamp21x9;

    const AspectRatio::PolicyResult result = AspectRatio::ResolvePolicy(input);

    CHECK(result.viewportRatio == Catch::Approx(3.0f / 2.0f));
    CHECK(result.effectiveRatio == Catch::Approx(3.0f / 2.0f));
    CHECK(result.settings.uiTopLeftX == Catch::Approx(0.0f));
    CHECK(result.settings.uiBottomRightX == Catch::Approx(1.0f));
}

TEST_CASE("AspectRatio Modern tolerates 1px-wide surface (1921x1080 NVIDIA quirk)", "[Settings][AspectRatio]")
{
    // Some NVIDIA fullscreen setups resolve the surface to 1921x1080
    // instead of 1920x1080.  Ratio drift: 1.7787 vs canonical
    // 16:9 = 1.7777, a 0.06% surplus.  Without tolerance the Modern
    // full-width UI override is disqualified and the menu ends up
    // pillarboxed despite being on a 16:9 monitor.
    AspectRatio::PolicyInput input;
    input.viewportWidth = 1921;
    input.viewportHeight = 1080;
    input.style = AspectRatio::Modern;
    input.ultrawideClamp = AspectRatio::Clamp21x9;

    const AspectRatio::PolicyResult result = AspectRatio::ResolvePolicy(input);

    CHECK(result.viewportRatio > 16.0f / 9.0f);               // slightly over canonical
    CHECK(result.settings.uiTopLeftX == Catch::Approx(0.0f)); // full-width
    CHECK(result.settings.uiBottomRightX == Catch::Approx(1.0f));
}

TEST_CASE("AspectRatio Modern clamps ultrawide gameplay framing", "[Settings][AspectRatio]")
{
    AspectRatio::PolicyInput input;
    input.viewportWidth = 5120;
    input.viewportHeight = 1440;
    input.style = AspectRatio::Modern;
    input.ultrawideClamp = AspectRatio::Clamp21x9;

    const AspectRatio::PolicyResult result = AspectRatio::ResolvePolicy(input);

    CHECK(result.viewportRatio == Catch::Approx(32.0f / 9.0f));
    CHECK(result.effectiveRatio == Catch::Approx(21.0f / 9.0f));
    CHECK(result.settings.leftFOV == Catch::Approx(0.75f * (21.0f / 9.0f)));
    CHECK(result.settings.uiTopLeftX > 0.0f);
}

TEST_CASE("AspectRatio Modern clamp uses the selected HUD width band", "[Settings][AspectRatio]")
{
    AspectRatio::PolicyInput input = AspectRatio::SafeDefaultPolicy(3840, 1080); // 32:9

    AspectRatio::PolicyResult result = AspectRatio::ResolvePolicy(input);
    CHECK(result.effectiveRatio == Catch::Approx(21.0f / 9.0f));
    CHECK(result.settings.leftFOV == Catch::Approx(0.75f * (21.0f / 9.0f)));
    CHECK(result.settings.uiTopLeftX == Catch::Approx(0.171875f));
    CHECK(result.settings.uiBottomRightX == Catch::Approx(0.828125f));

    input.ultrawideClamp = AspectRatio::Clamp16x9;
    result = AspectRatio::ResolvePolicy(input);
    CHECK(result.effectiveRatio == Catch::Approx(16.0f / 9.0f));
    CHECK(result.settings.leftFOV == Catch::Approx(0.75f * (16.0f / 9.0f)));
    CHECK(result.settings.uiTopLeftX == Catch::Approx(0.25f));
    CHECK(result.settings.uiBottomRightX == Catch::Approx(0.75f));
}

TEST_CASE("AspectRatio Legacy ignores ultrawide clamp", "[Settings][AspectRatio]")
{
    AspectRatio::PolicyInput input;
    input.viewportWidth = 5120;
    input.viewportHeight = 1440;
    input.style = AspectRatio::Legacy;
    input.ultrawideClamp = AspectRatio::Clamp16x9;

    const AspectRatio::PolicyResult result = AspectRatio::ResolvePolicy(input);

    CHECK(result.viewportRatio == Catch::Approx(32.0f / 9.0f));
    CHECK(result.effectiveRatio == Catch::Approx(32.0f / 9.0f));
    CHECK(result.settings.leftFOV == Catch::Approx(0.75f * (32.0f / 9.0f)));
    CHECK(result.settings.uiTopLeftX == Catch::Approx(0.0f));
    CHECK(result.settings.uiBottomRightX == Catch::Approx(1.0f));
    CHECK(result.settings.uiTopLeftY == Catch::Approx(0.0f));
    CHECK(result.settings.uiBottomRightY == Catch::Approx(1.0f));
}

TEST_CASE("AspectRatio SafeDefaultPolicy is Modern + 21:9 and fills standard displays", "[Settings][AspectRatio]")
{
    // Factory defaults use Adaptive + 21:9. On a standard 16:9 viewport that
    // remains full-width; the HUD width limit only adds side bands on wider
    // viewports.
    const AspectRatio::PolicyInput input = AspectRatio::SafeDefaultPolicy(1920, 1080);
    CHECK(input.style == AspectRatio::Modern);
    CHECK(input.ultrawideClamp == AspectRatio::Clamp21x9);
    CHECK(input.viewportWidth == 1920);
    CHECK(input.viewportHeight == 1080);

    const AspectRatio::PolicyResult result = AspectRatio::ResolvePolicy(input);
    CHECK(result.settings.leftFOV == Catch::Approx(4.0f / 3.0f));
    CHECK(result.settings.topFOV == Catch::Approx(0.75f));
    CHECK(result.settings.uiTopLeftX == Catch::Approx(0.0f)); // full-width, not 0.125
    CHECK(result.settings.uiBottomRightX == Catch::Approx(1.0f));
    CHECK(result.settings.uiTopLeftY == Catch::Approx(0.0f));
    CHECK(result.settings.uiBottomRightY == Catch::Approx(1.0f));
}

TEST_CASE("AspectRatio auto resolves from live geometry", "[Settings][AspectRatio]")
{
    // 2560x1440 = 16:9 — the expected ui inset is 0.125 (Faguss
    // 4:3-strip math).  Original test had 2560x1080 (= 21:9)
    // by typo — would produce 0.21875 inset, not 0.125.
    const AspectRatio::Settings legacy{1.0f, 0.75f, 0.0f, 0.0f, 1.0f, 1.0f};
    const AspectRatio::Settings settings = AspectRatio::ResolveSettings(AspectRatio::Auto, 2560, 1440, legacy);

    // leftFOV = 0.75 * ratio = 0.75 * 16/9 = 4/3 (not 16/9 — that
    // was the original test's second bug).
    CHECK(settings.leftFOV == Catch::Approx(4.0f / 3.0f));
    CHECK(settings.topFOV == Catch::Approx(0.75f));
    CHECK(settings.uiTopLeftX == Catch::Approx(0.125f));
    CHECK(settings.uiBottomRightX == Catch::Approx(0.875f));
}

TEST_CASE("AspectRatio infers auto from legacy 4:3 defaults", "[Settings][AspectRatio]")
{
    const AspectRatio::Settings defaults = AspectRatio::BuildSettingsForRatio(4.0f / 3.0f);
    CHECK(AspectRatio::InferPresetFromSettings(defaults) == AspectRatio::Auto);
}

TEST_CASE("AspectRatio preserves unmatched legacy settings as custom", "[Settings][AspectRatio]")
{
    AspectRatio::Settings custom{};
    custom.leftFOV = 1.11f;
    custom.topFOV = 0.75f;
    custom.uiTopLeftX = 0.0f;
    custom.uiTopLeftY = 0.0f;
    custom.uiBottomRightX = 1.0f;
    custom.uiBottomRightY = 1.0f;

    CHECK(AspectRatio::InferPresetFromSettings(custom) == AspectRatio::Custom);
    const AspectRatio::Settings resolved =
        AspectRatio::ResolveSettings(AspectRatio::Custom, 3440, 1440, custom, custom.leftFOV / custom.topFOV);
    CHECK(resolved.leftFOV == Catch::Approx(custom.leftFOV));
    CHECK(resolved.topFOV == Catch::Approx(custom.topFOV));
}

TEST_CASE("AspectRatio custom ratio resolves from width and height", "[Settings][AspectRatio]")
{
    const AspectRatio::Settings legacy = AspectRatio::BuildSettingsForRatio(16.0f / 9.0f);
    const AspectRatio::Settings resolved =
        AspectRatio::ResolveSettings(AspectRatio::Custom, 1920, 1080, legacy, AspectRatio::RatioFromDimensions(4, 3));

    CHECK(resolved.leftFOV == Catch::Approx(1.0f));
    CHECK(resolved.topFOV == Catch::Approx(0.75f));
    CHECK(resolved.uiTopLeftX == Catch::Approx(0.0f));
    CHECK(resolved.uiBottomRightX == Catch::Approx(1.0f));
}

TEST_CASE("AspectRatio suggests compact custom dimensions for an arbitrary ratio", "[Settings][AspectRatio]")
{
    int width = 0;
    int height = 0;
    AspectRatio::SuggestDimensions(1.3333f, width, height);

    CHECK(width == 4);
    CHECK(height == 3);
}

TEST_CASE("AspectRatio preset definitions keep canonical dimensions", "[Settings][AspectRatio]")
{
    const AspectRatio::PresetDefinition& def = AspectRatio::GetPresetDefinition(AspectRatio::Ratio21x9);

    CHECK(def.width == 21);
    CHECK(def.height == 9);
}

// ---- ResolveLive: the live dev-panel / tri aspect controls ----------------
//
// The defining invariant for every mode is "no stretch": the FOV aspect
// (leftFOV/topFOV) must equal the aspect of the world flat_quad the scene is
// rendered into, so a soldier is the same pixel size whether the world is
// full-width or cropped.  leftFOV/topFOV always reduces to the world ratio
// by construction of BuildSettingsForRatio, so the test is that the world
// flat_quad's *pixel* aspect equals leftFOV/topFOV.

static float WorldRectAspect(const AspectRatio::Settings& s, int w, int h)
{
    const float rw = (s.worldRight - s.worldLeft) * static_cast<float>(w);
    const float rh = (s.worldBottom - s.worldTop) * static_cast<float>(h);
    return rw / rh;
}

// Pixel aspect of the 2D UI region (== Engine::Width2D / Height2D).  A 2D
// control's on-screen aspect is its normalized aspect times this, so if this
// changes between modes, every menu entry / logo is distorted.
static float UiRegionAspect(const AspectRatio::Settings& s, int w, int h)
{
    const float rw = (s.uiBottomRightX - s.uiTopLeftX) * static_cast<float>(w);
    const float rh = (s.uiBottomRightY - s.uiTopLeftY) * static_cast<float>(h);
    return rw / rh;
}

TEST_CASE("AspectRatio ResolveLive: hud-clamp keeps world full + HOR+ FOV", "[Settings][AspectRatio][Live]")
{
    AspectRatio::LiveControls c;
    c.overrideEnabled = true;
    c.style = AspectRatio::Modern;
    c.clamp = AspectRatio::Clamp21x9;
    c.hudClamp = true;

    // 16:9 is below the 21:9 clamp → nothing clamps.
    const AspectRatio::Settings s = AspectRatio::ResolveLive(c, 1920, 1080);
    CHECK(s.worldLeft == Catch::Approx(0.0f));
    CHECK(s.worldRight == Catch::Approx(1.0f));
    CHECK(s.worldTop == Catch::Approx(0.0f));
    CHECK(s.worldBottom == Catch::Approx(1.0f));
    CHECK(s.leftFOV == Catch::Approx(0.75f * 16.0f / 9.0f));
    CHECK(s.topFOV == Catch::Approx(0.75f));
    CHECK(s.uiTopLeftX == Catch::Approx(0.0f));
    CHECK(s.uiBottomRightX == Catch::Approx(1.0f));
}

TEST_CASE("AspectRatio ResolveLive: hud-clamp on 32:9 keeps world full, UI in the selected band",
          "[Settings][AspectRatio][Live]")
{
    AspectRatio::LiveControls c;
    c.overrideEnabled = true;
    c.style = AspectRatio::Modern;
    c.clamp = AspectRatio::Clamp21x9;
    c.hudClamp = true;

    const AspectRatio::Settings s = AspectRatio::ResolveLive(c, 3840, 1080); // 32:9
    // World is full-width (immersive), NOT clamped.
    CHECK(s.worldLeft == Catch::Approx(0.0f));
    CHECK(s.worldRight == Catch::Approx(1.0f));
    // FOV uses the true 32:9 ratio → full-width world is un-stretched.
    CHECK(s.leftFOV / s.topFOV == Catch::Approx(3840.0f / 1080.0f).margin(0.02f));
    // UI is centered in the selected HUD width band.
    CHECK(UiRegionAspect(s, 3840, 1080) == Catch::Approx(21.0f / 9.0f).margin(0.02f));
    CHECK(s.uiTopLeftX == Catch::Approx(0.171875f).margin(0.001f));
    CHECK(s.uiTopLeftX == Catch::Approx(1.0f - s.uiBottomRightX).margin(0.001f));
}

TEST_CASE("AspectRatio ResolveLive: pillarbox on 32:9 crops world to band, no stretch", "[Settings][AspectRatio][Live]")
{
    AspectRatio::LiveControls c;
    c.overrideEnabled = true;
    c.style = AspectRatio::Modern;
    c.clamp = AspectRatio::Clamp21x9;
    c.pillarbox = true;

    const AspectRatio::Settings s = AspectRatio::ResolveLive(c, 3840, 1080); // 32:9
    const float bandFrac = (21.0f / 9.0f) / (3840.0f / 1080.0f);
    CHECK(s.worldLeft == Catch::Approx((1.0f - bandFrac) * 0.5f));
    CHECK(s.worldRight == Catch::Approx(1.0f - (1.0f - bandFrac) * 0.5f));
    CHECK(s.worldTop == Catch::Approx(0.0f));
    CHECK(s.worldBottom == Catch::Approx(1.0f));
    // No stretch: cropped flat_quad aspect is 21:9 and matches the FOV.
    CHECK(WorldRectAspect(s, 3840, 1080) == Catch::Approx(21.0f / 9.0f).margin(0.03f));
    CHECK(s.leftFOV / s.topFOV == Catch::Approx(WorldRectAspect(s, 3840, 1080)).margin(0.03f));
}

TEST_CASE("AspectRatio ResolveLive: manual flat_quad drives world + FOV (no stretch)", "[Settings][AspectRatio][Live]")
{
    AspectRatio::LiveControls c;
    c.overrideEnabled = true;
    c.manualRect = true;
    c.rectL = 0.25f;
    c.rectT = 0.0f;
    c.rectR = 0.75f;
    c.rectB = 1.0f;

    const AspectRatio::Settings s = AspectRatio::ResolveLive(c, 1920, 1080);
    CHECK(s.worldLeft == Catch::Approx(0.25f));
    CHECK(s.worldRight == Catch::Approx(0.75f));
    CHECK(s.leftFOV / s.topFOV == Catch::Approx(WorldRectAspect(s, 1920, 1080)).margin(0.01f));
    CHECK(s.uiTopLeftX == Catch::Approx(0.25f));
    CHECK(s.uiBottomRightX == Catch::Approx(0.75f));
}

TEST_CASE("AspectRatio ResolveLive: clamp centers the 2D UI in the selected HUD band", "[Settings][AspectRatio][Live]")
{
    const int w = 3840, h = 1080; // 32:9

    for (const bool pillarbox : {false, true})
    {
        AspectRatio::LiveControls m;
        m.overrideEnabled = true;
        m.style = AspectRatio::Modern;
        m.clamp = AspectRatio::Clamp21x9;
        m.hudClamp = !pillarbox;
        m.pillarbox = pillarbox;
        const AspectRatio::Settings s = AspectRatio::ResolveLive(m, w, h);

        CAPTURE(pillarbox);
        CHECK(UiRegionAspect(s, w, h) == Catch::Approx(21.0f / 9.0f).margin(0.02f));
        // Centered horizontally, full height.
        CHECK(s.uiTopLeftX == Catch::Approx(0.171875f).margin(0.001f));
        CHECK(s.uiTopLeftX == Catch::Approx(1.0f - s.uiBottomRightX).margin(0.001f));
        CHECK(s.uiTopLeftY == Catch::Approx(0.0f));
        CHECK(s.uiBottomRightY == Catch::Approx(1.0f));
    }
}

TEST_CASE("AspectRatio ResolveLive: no-stretch invariant across modes/ratios", "[Settings][AspectRatio][Live]")
{
    const int dims[][2] = {{1920, 1080}, {2560, 1080}, {3840, 1080}, {1440, 1080}, {1280, 1024}};
    for (auto& d : dims)
    {
        for (int mode = 0; mode < 3; ++mode) // 0=hudClamp, 1=pillarbox, 2=manual
        {
            AspectRatio::LiveControls c;
            c.overrideEnabled = true;
            c.style = AspectRatio::Modern;
            c.clamp = AspectRatio::Clamp21x9;
            c.hudClamp = (mode == 0);
            c.pillarbox = (mode == 1);
            if (mode == 2)
            {
                c.manualRect = true;
                c.rectL = 0.2f;
                c.rectT = 0.1f;
                c.rectR = 0.8f;
                c.rectB = 0.9f;
            }
            const AspectRatio::Settings s = AspectRatio::ResolveLive(c, d[0], d[1]);
            // FOV aspect == rendered-flat_quad aspect → objects never stretch.
            CHECK(s.leftFOV / s.topFOV == Catch::Approx(WorldRectAspect(s, d[0], d[1])).margin(0.03f));
        }
    }
}
