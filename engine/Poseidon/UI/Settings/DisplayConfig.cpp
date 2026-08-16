#include <Poseidon/UI/Settings/DisplayConfig.hpp>

#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/UI/Settings/SettingsFile.hpp>

#include <algorithm>
#include <Poseidon/Foundation/Strings/RString.hpp>

namespace Poseidon
{

namespace
{
AspectRatio::DisplayStyle MigrateLegacyDisplayStyle(bool hasLegacyAspectPolicy, AspectRatio::Preset legacyPreset)
{
    if (!hasLegacyAspectPolicy)
        return AspectRatio::Modern;
    return legacyPreset == AspectRatio::Auto ? AspectRatio::Modern : AspectRatio::Legacy;
}

bool ResolutionListed(int w, int h, const std::vector<std::pair<int, int>>& list)
{
    // Empty list means "any" — Dummy backend / unspecified Environment.
    if (list.empty())
        return true;
    return std::find(list.begin(), list.end(), std::pair<int, int>{w, h}) != list.end();
}

bool RefreshListed(int hz, const std::vector<int>& list)
{
    if (list.empty())
        return true;
    return std::find(list.begin(), list.end(), hz) != list.end();
}
} // namespace

void DisplayConfig::LoadDefaults()
{
    *this = DisplayConfig{};
}

const std::vector<std::pair<int, int>>& DisplayConfig::WindowedSizes()
{
    static const std::vector<std::pair<int, int>> sizes = {
        {800, 600}, {1024, 768}, {1280, 720}, {1280, 800}, {1366, 768}, {1600, 900}, {1920, 1080},
    };
    return sizes;
}

bool DisplayConfig::IsWindowedSize(int w, int h)
{
    const auto& s = WindowedSizes();
    return std::find(s.begin(), s.end(), std::pair<int, int>{w, h}) != s.end();
}

bool DisplayConfig::Normalize(const Environment& env)
{
    bool changed = false;

    if (monitor < 0 || monitor >= env.GetMonitorCount())
    {
        monitor = 0;
        changed = true;
    }

    if (windowMode != Fullscreen && windowMode != Borderless && windowMode != Windowed)
    {
        windowMode = Borderless;
        changed = true;
    }

    // Resolution: (0, 0) is always valid (native).  A non-zero pair
    // must appear in the live list for the selected monitor.  The two
    // fields normalize together — one zero forces both to zero so the
    // native sentinel is never half-set.
    if (resolutionWidth != 0 || resolutionHeight != 0)
    {
        // In Windowed mode a curated window size (800x600 and friends) is valid even
        // when it is not an exclusive-fullscreen mode the monitor enumerates.
        const bool listed = ResolutionListed(resolutionWidth, resolutionHeight, env.ListResolutions(monitor)) ||
                            (windowMode == Windowed && IsWindowedSize(resolutionWidth, resolutionHeight));
        if (resolutionWidth <= 0 || resolutionHeight <= 0 || !listed)
        {
            resolutionWidth = 0;
            resolutionHeight = 0;
            changed = true;
        }
    }

    // Refresh rate: 0 always valid (system default).  Validated against
    // the rate list at the *current* (monitor, w, h) — including the
    // (0, 0) native pair, where the Environment is expected to return
    // the rates of the monitor's native resolution.
    if (refreshRate != 0)
    {
        if (refreshRate < 0 ||
            !RefreshListed(refreshRate, env.ListRefreshRates(monitor, resolutionWidth, resolutionHeight)))
        {
            refreshRate = 0;
            changed = true;
        }
    }

    if (displayStyle < AspectRatio::Modern || displayStyle >= AspectRatio::DisplayStyleCount)
    {
        displayStyle = AspectRatio::Modern;
        changed = true;
    }

    if (ultrawideClamp < AspectRatio::ClampOff || ultrawideClamp >= AspectRatio::UltrawideClampCount)
    {
        ultrawideClamp = AspectRatio::Clamp21x9;
        changed = true;
    }

    return changed;
}

bool DisplayConfig::Load(const std::string& path)
{
    ParamFile cfg;
    if (!ReadSettingsFile(path, cfg))
        return false;

    // Forward-compat: each field optional, missing keys keep current value.
    if (auto* e = cfg.FindEntry("monitor"))
        monitor = (int)*e;
    if (auto* e = cfg.FindEntry("windowMode"))
        windowMode = static_cast<WindowMode>((int)*e);
    if (auto* e = cfg.FindEntry("resolutionWidth"))
        resolutionWidth = (int)*e;
    if (auto* e = cfg.FindEntry("resolutionHeight"))
        resolutionHeight = (int)*e;
    if (auto* e = cfg.FindEntry("refreshRate"))
        refreshRate = (int)*e;

    bool hasDisplayStyle = false;
    if (auto* e = cfg.FindEntry("displayStyle"))
    {
        displayStyle = static_cast<AspectRatio::DisplayStyle>((int)*e);
        hasDisplayStyle = true;
    }

    bool hasUltrawideClamp = false;
    if (auto* e = cfg.FindEntry("ultrawideClamp"))
    {
        ultrawideClamp = static_cast<AspectRatio::UltrawideClamp>((int)*e);
        hasUltrawideClamp = true;
    }

    bool legacyAspectPolicySeen = false;
    AspectRatio::Preset legacyPreset = AspectRatio::Auto;
    if (auto* e = cfg.FindEntry("aspectPreset"))
    {
        legacyPreset = static_cast<AspectRatio::Preset>((int)*e);
        legacyAspectPolicySeen = true;
    }
    if (cfg.FindEntry("customAspectWidth") || cfg.FindEntry("customAspectHeight"))
        legacyAspectPolicySeen = true;

    if (!hasDisplayStyle)
        displayStyle = MigrateLegacyDisplayStyle(legacyAspectPolicySeen, legacyPreset);
    if (!hasUltrawideClamp)
        ultrawideClamp = AspectRatio::Clamp21x9;

    return true;
}

bool DisplayConfig::Save(const std::string& path) const
{
    ParamFile cfg;
    cfg.Add("monitor", monitor);
    cfg.Add("windowMode", static_cast<int>(windowMode));
    cfg.Add("resolutionWidth", resolutionWidth);
    cfg.Add("resolutionHeight", resolutionHeight);
    cfg.Add("refreshRate", refreshRate);
    cfg.Add("displayStyle", static_cast<int>(displayStyle));
    cfg.Add("ultrawideClamp", static_cast<int>(ultrawideClamp));

    return WriteSettingsFile(path, cfg);
}

} // namespace Poseidon
