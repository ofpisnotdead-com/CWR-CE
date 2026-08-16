#include <Poseidon/UI/Settings/AspectRatio.hpp>

#include <cmath>
#include <limits>

namespace Poseidon
{
namespace AspectRatio
{
namespace
{
constexpr float kBaseLeftFov = 1.0f;
constexpr float kBaseTopFov = 0.75f;
constexpr float kBaseRatio = 4.0f / 3.0f;
constexpr float kModernFullWidthUiMaxRatio = 16.0f / 9.0f;
// Tolerance band above the 16:9 ratio so a one-or-two-pixel surplus
// in the actual surface (NVIDIA fullscreen sometimes resolves to
// 1921×1080 → ratio 1.7787 vs canonical 1.7777) doesn't flip the
// UI layout out of "Modern full-width" mode.  2% covers any
// reasonable surface-vs-canonical drift; a true ultrawide
// (21:9 = 2.333) is well past the band.
constexpr float kModernFullWidthUiTolerance = 0.02f;
constexpr float kPresetTolerance = 0.01f;

const PresetDefinition kPresetDefinitions[] = {
    {Auto, "Auto", 0.0f, 0, 0},
    {Ratio4x3, "4:3", 4.0f / 3.0f, 4, 3},
    {Ratio5x4, "5:4", 5.0f / 4.0f, 5, 4},
    {Ratio16x10, "16:10", 16.0f / 10.0f, 16, 10},
    {Ratio15x9, "15:9", 15.0f / 9.0f, 15, 9},
    {Ratio16x9, "16:9", 16.0f / 9.0f, 16, 9},
    {Ratio21x9, "21:9", 21.0f / 9.0f, 21, 9},
    {Ratio32x9, "32:9", 32.0f / 9.0f, 32, 9},
    {Custom, "Custom", 0.0f, 0, 0},
};

bool NearlyEqual(float a, float b, float tolerance = kPresetTolerance)
{
    return std::fabs(a - b) <= tolerance;
}
} // namespace

const PresetDefinition* PresetDefinitions()
{
    return kPresetDefinitions;
}

int PresetDefinitionCount()
{
    return static_cast<int>(sizeof(kPresetDefinitions) / sizeof(kPresetDefinitions[0]));
}

const PresetDefinition& GetPresetDefinition(Preset preset)
{
    const int index = (preset >= Auto && preset < Count) ? static_cast<int>(preset) : static_cast<int>(Auto);
    return kPresetDefinitions[index];
}

float SanitizeRatio(float ratio)
{
    return ratio > 0.0f ? ratio : kBaseRatio;
}

float RatioFromDimensions(int width, int height)
{
    if (width <= 0 || height <= 0)
        return kBaseRatio;
    return static_cast<float>(width) / static_cast<float>(height);
}

float RatioForClamp(UltrawideClamp clamp)
{
    switch (clamp)
    {
        case Clamp21x9:
            return 21.0f / 9.0f;
        case Clamp16x9:
            return 16.0f / 9.0f;
        case ClampOff:
        default:
            return 0.0f;
    }
}

void ApplyCenteredUiBand(Settings& settings, float targetRatio, int viewportWidth, int viewportHeight)
{
    if (targetRatio <= 0.0f || viewportWidth <= 0 || viewportHeight <= 0)
        return;

    const float viewportRatio = RatioFromDimensions(viewportWidth, viewportHeight);
    if (viewportRatio > targetRatio)
    {
        const float uiWidth = targetRatio / viewportRatio;
        const float insetX = (1.0f - uiWidth) * 0.5f;
        settings.uiTopLeftX = insetX;
        settings.uiTopLeftY = 0.0f;
        settings.uiBottomRightX = 1.0f - insetX;
        settings.uiBottomRightY = 1.0f;
        return;
    }

    if (viewportRatio < targetRatio)
    {
        const float uiHeight = viewportRatio / targetRatio;
        const float insetY = (1.0f - uiHeight) * 0.5f;
        settings.uiTopLeftX = 0.0f;
        settings.uiTopLeftY = insetY;
        settings.uiBottomRightX = 1.0f;
        settings.uiBottomRightY = 1.0f - insetY;
        return;
    }

    settings.uiTopLeftX = 0.0f;
    settings.uiTopLeftY = 0.0f;
    settings.uiBottomRightX = 1.0f;
    settings.uiBottomRightY = 1.0f;
}

void SuggestDimensions(float ratio, int& width, int& height)
{
    ratio = SanitizeRatio(ratio);

    float bestError = std::numeric_limits<float>::max();
    int bestWidth = 16;
    int bestHeight = 9;
    for (int h = 1; h <= 64; ++h)
    {
        for (int w = 1; w <= 64; ++w)
        {
            const float error = std::fabs(RatioFromDimensions(w, h) - ratio);
            if (error < bestError)
            {
                bestError = error;
                bestWidth = w;
                bestHeight = h;
            }
        }
    }

    width = bestWidth;
    height = bestHeight;
}

Settings BuildSettingsForRatio(float ratio)
{
    ratio = SanitizeRatio(ratio);

    Settings settings{};
    if (ratio >= kBaseRatio)
    {
        settings.leftFOV = kBaseTopFov * ratio;
        settings.topFOV = kBaseTopFov;

        const float uiWidth = kBaseRatio / ratio;
        const float insetX = (1.0f - uiWidth) * 0.5f;
        settings.uiTopLeftX = insetX;
        settings.uiTopLeftY = 0.0f;
        settings.uiBottomRightX = 1.0f - insetX;
        settings.uiBottomRightY = 1.0f;
        return settings;
    }

    settings.leftFOV = kBaseLeftFov;
    settings.topFOV = kBaseLeftFov / ratio;

    const float uiHeight = ratio / kBaseRatio;
    const float insetY = (1.0f - uiHeight) * 0.5f;
    settings.uiTopLeftX = 0.0f;
    settings.uiTopLeftY = insetY;
    settings.uiBottomRightX = 1.0f;
    settings.uiBottomRightY = 1.0f - insetY;
    return settings;
}

PolicyResult ResolvePolicy(const PolicyInput& input)
{
    PolicyResult result{};
    result.viewportRatio = RatioFromDimensions(input.viewportWidth, input.viewportHeight);
    result.effectiveRatio = result.viewportRatio;

    if (input.style == Modern)
    {
        const float clampRatio = RatioForClamp(input.ultrawideClamp);
        if (clampRatio > 0.0f && result.effectiveRatio > clampRatio)
            result.effectiveRatio = clampRatio;
    }

    result.settings = BuildSettingsForRatio(result.viewportRatio);
    if (result.viewportRatio > result.effectiveRatio)
    {
        ApplyCenteredUiBand(result.settings, result.effectiveRatio, input.viewportWidth, input.viewportHeight);
    }
    // Tolerance band on the upper bound: 1921×1080 fullscreen
    // surfaces (NVIDIA on some setups) produce ratio 1.7787 vs
    // canonical 16:9 = 1.7777 → 0.06% drift.  Without slack, the
    // 1-pixel surplus disqualifies Modern full-width and pillarboxes
    // the UI.  +2% covers any reasonable surface-vs-canonical
    // resolution rounding without catching real ultrawides
    // (21:9 = 2.333 is well past the band).
    const float upperBound = kModernFullWidthUiMaxRatio * (1.0f + kModernFullWidthUiTolerance);
    if (input.style == Modern && result.viewportRatio >= kBaseRatio && result.viewportRatio <= upperBound)
    {
        result.settings.uiTopLeftX = 0.0f;
        result.settings.uiBottomRightX = 1.0f;
    }
    if (input.style == Modern && input.ultrawideClamp == ClampOff)
    {
        result.settings.uiTopLeftX = 0.0f;
        result.settings.uiTopLeftY = 0.0f;
        result.settings.uiBottomRightX = 1.0f;
        result.settings.uiBottomRightY = 1.0f;
    }
    if (input.style == Legacy)
    {
        result.settings.uiTopLeftX = 0.0f;
        result.settings.uiTopLeftY = 0.0f;
        result.settings.uiBottomRightX = 1.0f;
        result.settings.uiBottomRightY = 1.0f;
    }
    return result;
}

PolicyInput SafeDefaultPolicy(int viewportWidth, int viewportHeight)
{
    PolicyInput input;
    input.viewportWidth = viewportWidth;
    input.viewportHeight = viewportHeight;
    input.style = Modern;
    input.ultrawideClamp = Clamp21x9;
    return input;
}

Settings ResolveSettings(Preset preset, int width, int height, const Settings& legacySettings, float customRatio)
{
    if (preset == Custom)
        return BuildSettingsForRatio(customRatio);

    float ratio = 0.0f;
    if (preset == Auto)
    {
        if (width > 0 && height > 0)
            ratio = static_cast<float>(width) / static_cast<float>(height);
    }
    else
    {
        ratio = GetPresetDefinition(preset).ratio;
    }

    return BuildSettingsForRatio(ratio);
}

Preset InferPresetFromSettings(const Settings& settings)
{
    const Settings base = BuildSettingsForRatio(kBaseRatio);
    if (NearlyEqual(settings.leftFOV, base.leftFOV) && NearlyEqual(settings.topFOV, base.topFOV))
        return Auto;

    for (int i = static_cast<int>(Ratio4x3); i < static_cast<int>(Custom); ++i)
    {
        const Preset preset = static_cast<Preset>(i);
        const Settings expected = BuildSettingsForRatio(GetPresetDefinition(preset).ratio);
        if (NearlyEqual(settings.leftFOV, expected.leftFOV) && NearlyEqual(settings.topFOV, expected.topFOV))
            return preset;
    }

    return Custom;
}

bool MatchesPreset(float ratio, Preset preset, float tolerance)
{
    if (preset == Auto || preset == Custom)
        return true;

    return std::fabs(SanitizeRatio(ratio) - GetPresetDefinition(preset).ratio) <= tolerance;
}

float LateralPillarboxWidth(int viewportWidth, int viewportHeight)
{
    if (viewportWidth <= 0 || viewportHeight <= 0)
        return 0.0f;
    const float w = static_cast<float>(viewportWidth);
    const float h = static_cast<float>(viewportHeight);
    const float centerW = h * kBaseRatio;
    if (w <= centerW)
        return 0.0f;
    return (w - centerW) * 0.5f;
}

namespace
{
bool s_pillarboxBarsEnabled = true;
bool s_gameplayActive = false;
} // namespace

bool ArePillarboxBarsEnabled()
{
    return s_pillarboxBarsEnabled;
}

void SetPillarboxBarsEnabled(bool enabled)
{
    s_pillarboxBarsEnabled = enabled;
}

bool IsGameplayActive()
{
    return s_gameplayActive;
}

void SetGameplayActive(bool active)
{
    s_gameplayActive = active;
}

namespace
{
float ClampUnit(float v)
{
    if (v < 0.0f)
        return 0.0f;
    if (v > 1.0f)
        return 1.0f;
    return v;
}
} // namespace

LiveControls& Live()
{
    static LiveControls s_live;
    return s_live;
}

Settings ResolveLive(const LiveControls& c, int viewportWidth, int viewportHeight)
{
    const float viewportRatio = RatioFromDimensions(viewportWidth, viewportHeight);

    // Clamp band as a centered width-fraction of the window.  bandFrac==1
    // means the band fills the whole width (nothing to clamp); a true
    // ultrawide gives bandFrac<1 and a centered [bandL..bandR] strip.
    float bandFrac = 1.0f;
    float bandRatio = viewportRatio;
    {
        const float band = (c.style == Modern) ? RatioForClamp(c.clamp) : 0.0f;
        if (band > 0.0f && band < viewportRatio)
        {
            bandFrac = band / viewportRatio;
            bandRatio = band;
        }
    }
    const float bandL = (1.0f - bandFrac) * 0.5f;
    const float bandR = 1.0f - bandL;

    // World crop rect + the ratio its FOV must match (so it never stretches).
    float wl = 0.0f, wt = 0.0f, wr = 1.0f, wb = 1.0f;
    float worldRatio = viewportRatio;
    if (c.manualRect)
    {
        wl = ClampUnit(c.rectL);
        wt = ClampUnit(c.rectT);
        wr = ClampUnit(c.rectR);
        wb = ClampUnit(c.rectB);
        if (wr <= wl)
            wr = ClampUnit(wl + 0.01f);
        if (wb <= wt)
            wb = ClampUnit(wt + 0.01f);
        const float rw = (wr - wl) * static_cast<float>(viewportWidth);
        const float rh = (wb - wt) * static_cast<float>(viewportHeight);
        worldRatio = (rw > 0.0f && rh > 0.0f) ? rw / rh : viewportRatio;
    }
    else if (c.pillarbox)
    {
        wl = bandL;
        wr = bandR;
        worldRatio = bandRatio;
    }
    // else: world fills the viewport; FOV uses the true viewport ratio.

    Settings settings = BuildSettingsForRatio(worldRatio);
    settings.worldLeft = wl;
    settings.worldTop = wt;
    settings.worldRight = wr;
    settings.worldBottom = wb;

    // 2D UI insets.
    if (c.manualRect)
    {
        // Explicit dev override: the UI follows the manual rect.
        settings.uiTopLeftX = wl;
        settings.uiTopLeftY = wt;
        settings.uiBottomRightX = wr;
        settings.uiBottomRightY = wb;
    }
    else if (c.pillarbox || c.hudClamp)
    {
        // On viewports wider than 16:9, center the UI as an UNDISTORTED 4:3
        // box on the ACTUAL viewport — "rendered to the middle only" on
        // wide/ultrawide screens.  <=16:9 keeps full-width UI (common setups
        // stay full-screen).  The key is the VIEWPORT ratio, not the clamp
        // band: BuildSettingsForRatio centers a 4:3 box, the aspect the UI is
        // authored for (the menu logo is a 2:1 canvas that resolves to 2:1 on
        // screen only when the UI region is 4:3).  Banding to the clamp aspect
        // (21:9) is what squished the logo / menu entries.
        const float uiFullWidthMax = kModernFullWidthUiMaxRatio * (1.0f + kModernFullWidthUiTolerance);
        if (viewportRatio > uiFullWidthMax)
        {
            ApplyCenteredUiBand(settings, bandRatio, viewportWidth, viewportHeight);
        }
        else
        {
            settings.uiTopLeftX = 0.0f;
            settings.uiTopLeftY = 0.0f;
            settings.uiBottomRightX = 1.0f;
            settings.uiBottomRightY = 1.0f;
        }
    }
    else
    {
        // No clamp: standard Modern/Legacy UI rule (full-width up to 16:9).
        PolicyInput pin;
        pin.viewportWidth = viewportWidth;
        pin.viewportHeight = viewportHeight;
        pin.style = c.style;
        pin.ultrawideClamp = c.clamp;
        const PolicyResult pr = ResolvePolicy(pin);
        settings.uiTopLeftX = pr.settings.uiTopLeftX;
        settings.uiTopLeftY = pr.settings.uiTopLeftY;
        settings.uiBottomRightX = pr.settings.uiBottomRightX;
        settings.uiBottomRightY = pr.settings.uiBottomRightY;
    }

    return settings;
}

} // namespace AspectRatio

namespace Poseidon
{
}

} // namespace Poseidon
