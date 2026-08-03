#include <Poseidon/UI/Settings/GraphicsConfig.hpp>

#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/UI/Settings/SettingsFile.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <Poseidon/Foundation/Strings/RString.hpp>

namespace Poseidon
{

namespace
{
// Per-tier bundles for the four preset-driven rows.  Indexed by Preset
// (Low/Medium/High/Ultra); Custom is never used here.
struct TierBundle
{
    GraphicsConfig::Tier terrain;
    GraphicsConfig::Tier objectLod;
    GraphicsConfig::Tier shadow;
    GraphicsConfig::Tier particles;
};
constexpr TierBundle kTierBundles[4] = {
    // Low
    {GraphicsConfig::TierLow, GraphicsConfig::TierLow, GraphicsConfig::TierOff, GraphicsConfig::TierOff},
    // Medium
    {GraphicsConfig::TierMedium, GraphicsConfig::TierMedium, GraphicsConfig::TierLow, GraphicsConfig::TierLow},
    // High
    {GraphicsConfig::TierHigh, GraphicsConfig::TierHigh, GraphicsConfig::TierMedium, GraphicsConfig::TierLow},
    // Ultra — Shadow caps at High because the "Ultra shadow" tier
    // needs a shadow-distance bias hook that doesn't exist yet.
    {GraphicsConfig::TierUltra, GraphicsConfig::TierUltra, GraphicsConfig::TierHigh, GraphicsConfig::TierHigh},
};

bool IsValidTier(int v, bool allowOff)
{
    if (allowOff && v == GraphicsConfig::TierOff)
        return true;
    return v == GraphicsConfig::TierLow || v == GraphicsConfig::TierMedium || v == GraphicsConfig::TierHigh ||
           v == GraphicsConfig::TierUltra;
}

bool IsValidVsync(int v)
{
    return v == GraphicsConfig::VsyncOff || v == GraphicsConfig::VsyncOn || v == GraphicsConfig::VsyncAdaptive;
}

int NearestAllowedFps(int v)
{
    // Allowed: 0 (Unlimited), 30, 60, 90, 120, 144, 240.  Snap any
    // out-of-set value to the nearest allowed (treating 0 as
    // Unlimited and matching the row's stepper choices).
    constexpr int kAllowed[] = {0, 30, 60, 90, 120, 144, 240};
    if (v <= 0)
        return 0;
    int best = kAllowed[0];
    int bestDiff = std::abs(v - best);
    for (int allowed : kAllowed)
    {
        int diff = std::abs(v - allowed);
        if (diff < bestDiff)
        {
            best = allowed;
            bestDiff = diff;
        }
    }
    return best;
}

bool IsAllowedFps(int v)
{
    constexpr int kAllowed[] = {0, 30, 60, 90, 120, 144, 240};
    for (int allowed : kAllowed)
        if (v == allowed)
            return true;
    return false;
}
} // namespace

void GraphicsConfig::LoadDefaults()
{
    *this = GraphicsConfig{};
}

bool GraphicsConfig::Migrate()
{
    if (version >= kVersion)
        return false;

    // Only a file older than the version stamp reaches here, and its gamma is
    // whatever first boot wrote.  1.0 is that stamped default and disables
    // correction entirely; any other value was chosen, so it stays.
    if (gamma == 1.0f)
        gamma = 1.2f;

    version = kVersion;
    return true;
}

GraphicsConfig::Preset GraphicsConfig::PickPresetFromRam(int ramMB)
{
    // Bucket boundaries in MB: 4 GB, 8 GB, 16 GB.  ramMB == 0 (unknown)
    // returns High — middle-of-the-road on uncertain input.
    if (ramMB <= 0)
        return PresetHigh;
    if (ramMB < 4 * 1024)
        return PresetLow;
    if (ramMB < 8 * 1024)
        return PresetMedium;
    if (ramMB < 16 * 1024)
        return PresetHigh;
    return PresetUltra;
}

void GraphicsConfig::ApplyPresetToTiers(Preset preset)
{
    if (preset < PresetLow || preset > PresetUltra)
        return;
    const TierBundle& b = kTierBundles[preset];
    terrainDetail = b.terrain;
    objectLod = b.objectLod;
    shadowQuality = b.shadow;
    particlesQuality = b.particles;
}

GraphicsConfig::Preset GraphicsConfig::DerivePresetFromTiers() const
{
    for (int p = PresetLow; p <= PresetUltra; ++p)
    {
        const TierBundle& b = kTierBundles[p];
        if (terrainDetail == b.terrain && objectLod == b.objectLod && shadowQuality == b.shadow &&
            particlesQuality == b.particles)
            return static_cast<Preset>(p);
    }
    return PresetCustom;
}

bool GraphicsConfig::Normalize(const Environment& /*env*/)
{
    bool changed = false;

    // Quality Preset.  Out-of-range → re-derive from current tiers
    // (lets "Custom" be self-validating even if it slipped in via a
    // hand-edited file).
    if (qualityPreset < PresetLow || qualityPreset > PresetCustom)
    {
        qualityPreset = DerivePresetFromTiers();
        changed = true;
    }

    // Tier rows.  Off allowed only for Shadow + Particles.
    if (!IsValidTier(terrainDetail, /*allowOff=*/false))
    {
        terrainDetail = TierUltra;
        changed = true;
    }
    if (!IsValidTier(objectLod, /*allowOff=*/false))
    {
        objectLod = TierUltra;
        changed = true;
    }
    if (!IsValidTier(shadowQuality, /*allowOff=*/true))
    {
        shadowQuality = TierHigh;
        changed = true;
    }
    if (!IsValidTier(particlesQuality, /*allowOff=*/true))
    {
        particlesQuality = TierHigh;
        changed = true;
    }

    // Per-user knobs.
    if (!IsValidVsync(vsync))
    {
        vsync = VsyncOn;
        changed = true;
    }
    if (!IsAllowedFps(fpsCap))
    {
        fpsCap = NearestAllowedFps(fpsCap);
        changed = true;
    }

    // Float clamps with "changed" iff the value moved.
    auto clampFloat = [&changed](float& v, float lo, float hi)
    {
        float c = std::clamp(v, lo, hi);
        if (c != v)
        {
            v = c;
            changed = true;
        }
    };
    clampFloat(brightness, 0.4f, 1.8f);
    clampFloat(gamma, 0.5f, 2.3f);
    clampFloat(renderScale, 1.0f, 2.0f);
    if (msaaSamples != 0 && msaaSamples != 2 && msaaSamples != 4 && msaaSamples != 8)
    {
        msaaSamples = msaaSamples >= 8 ? 8 : msaaSamples >= 4 ? 4 : msaaSamples >= 2 ? 2 : 0;
        changed = true;
    }

    return changed;
}

bool GraphicsConfig::Load(const std::string& path)
{
    ParamFile cfg;
    if (!ReadSettingsFile(path, cfg))
        return false;

    version = 0;
    if (auto* e = cfg.FindEntry("version"))
        version = (int)*e;

    if (auto* e = cfg.FindEntry("qualityPreset"))
        qualityPreset = static_cast<Preset>((int)*e);
    if (auto* e = cfg.FindEntry("terrainDetail"))
        terrainDetail = static_cast<Tier>((int)*e);
    if (auto* e = cfg.FindEntry("objectLod"))
        objectLod = static_cast<Tier>((int)*e);
    if (auto* e = cfg.FindEntry("shadowQuality"))
        shadowQuality = static_cast<Tier>((int)*e);
    if (auto* e = cfg.FindEntry("particlesQuality"))
        particlesQuality = static_cast<Tier>((int)*e);
    if (auto* e = cfg.FindEntry("vsync"))
        vsync = static_cast<VsyncMode>((int)*e);
    if (auto* e = cfg.FindEntry("fpsCap"))
        fpsCap = (int)*e;
    if (auto* e = cfg.FindEntry("alphaToCoverage"))
        alphaToCoverage = (int)*e != 0;
    if (auto* e = cfg.FindEntry("renderScale"))
        renderScale = (float)*e;
    if (auto* e = cfg.FindEntry("msaaSamples"))
        msaaSamples = (int)*e;
    if (auto* e = cfg.FindEntry("multitexturing"))
        multitexturing = (int)*e != 0;
    if (auto* e = cfg.FindEntry("brightness"))
        brightness = (float)*e;
    if (auto* e = cfg.FindEntry("gamma"))
        gamma = (float)*e;

    return true;
}

bool GraphicsConfig::Save(const std::string& path) const
{
    ParamFile cfg;
    cfg.Add("qualityPreset", static_cast<int>(qualityPreset));
    cfg.Add("terrainDetail", static_cast<int>(terrainDetail));
    cfg.Add("objectLod", static_cast<int>(objectLod));
    cfg.Add("shadowQuality", static_cast<int>(shadowQuality));
    cfg.Add("particlesQuality", static_cast<int>(particlesQuality));
    cfg.Add("vsync", static_cast<int>(vsync));
    cfg.Add("fpsCap", fpsCap);
    cfg.Add("alphaToCoverage", alphaToCoverage ? 1 : 0);
    cfg.Add("renderScale", renderScale);
    cfg.Add("msaaSamples", msaaSamples);
    cfg.Add("multitexturing", multitexturing ? 1 : 0);
    cfg.Add("brightness", brightness);
    cfg.Add("gamma", gamma);
    cfg.Add("version", version);

    return WriteSettingsFile(path, cfg);
}

} // namespace Poseidon
