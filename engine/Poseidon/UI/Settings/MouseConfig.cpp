#include <Poseidon/UI/Settings/MouseConfig.hpp>

#include <Poseidon/IO/ParamFile/ParamFile.hpp>

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <Poseidon/Foundation/Strings/RString.hpp>

namespace Poseidon
{

void MouseConfig::LoadDefaults()
{
    *this = MouseConfig{};
}

float MouseConfig::MigrateSensitivity(float legacySens, float legacyBaseScale, float newBaseScale)
{
    // Keep newSens * newBaseScale == legacySens * legacyBaseScale.
    if (newBaseScale <= 0.0f)
        return legacySens;
    return legacySens * (legacyBaseScale / newBaseScale);
}

bool MouseConfig::Normalize()
{
    bool changed = false;
    auto clampF = [&](float& v, float lo, float hi)
    {
        float c = std::clamp(v, lo, hi);
        if (c != v)
        {
            v = c;
            changed = true;
        }
    };
    auto clampI = [&](int& v, int lo, int hi)
    {
        int c = std::clamp(v, lo, hi);
        if (c != v)
        {
            v = c;
            changed = true;
        }
    };

    // Sensitivity range widens when extendedRange is enabled.
    const float sensLo = extendedRange ? 0.05f : 0.5f;
    const float sensHi = extendedRange ? 3.0f : 2.0f;
    clampF(sensitivityX, sensLo, sensHi);
    clampF(sensitivityY, sensLo, sensHi);

    // v2 tuning fields — keep these ranges in sync with MouseTuning::Normalize.
    clampF(baseScale, 0.1f, 3.0f);
    clampI(mouseDpi, 100, 32000);
    clampI(referenceDpi, 100, 32000);
    clampF(smoothing, 0.0f, 0.95f);
    clampF(accelExponent, 1.0f, 2.0f);
    clampF(menuCursorScale, 0.1f, 4.0f);

    return changed;
}

bool MouseConfig::Load(const std::string& path)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return false;

    ParamFile cfg;
    cfg.Parse(RString(path.c_str()));

    // Schema version: absent ⇒ legacy v1 file (written before the tuning fields).
    int ver = 1;
    if (auto* e = cfg.FindEntry("version"))
        ver = (int)*e;
    version = ver;

    // v1 keys — present in every version.
    if (auto* e = cfg.FindEntry("reverseY"))
        reverseY = (bool)*e;
    if (auto* e = cfg.FindEntry("buttonsReversed"))
        buttonsReversed = (bool)*e;
    if (auto* e = cfg.FindEntry("sensitivityX"))
        sensitivityX = (float)*e;
    if (auto* e = cfg.FindEntry("sensitivityY"))
        sensitivityY = (float)*e;

    if (ver >= 2)
    {
        // lookSens* is authoritative (the v1 sensitivityX/Y are a legacy-clamped
        // mirror for old builds); each tuning key is optional.
        if (auto* e = cfg.FindEntry("lookSensX"))
            sensitivityX = (float)*e;
        if (auto* e = cfg.FindEntry("lookSensY"))
            sensitivityY = (float)*e;

        if (auto* e = cfg.FindEntry("baseScale"))
            baseScale = (float)*e;
        if (auto* e = cfg.FindEntry("dpiNormalize"))
            dpiNormalize = (bool)*e;
        if (auto* e = cfg.FindEntry("mouseDpi"))
            mouseDpi = (int)*e;
        if (auto* e = cfg.FindEntry("referenceDpi"))
            referenceDpi = (int)*e;
        if (auto* e = cfg.FindEntry("smoothing"))
            smoothing = (float)*e;
        if (auto* e = cfg.FindEntry("acceleration"))
            acceleration = (bool)*e;
        if (auto* e = cfg.FindEntry("accelExponent"))
            accelExponent = (float)*e;
        if (auto* e = cfg.FindEntry("menuCursorScale"))
            menuCursorScale = (float)*e;
        if (auto* e = cfg.FindEntry("extendedRange"))
            extendedRange = (bool)*e;
    }
    else
    {
        // Legacy (v1) file: tuning fields keep their classic defaults; rescale
        // sensitivity for the current baseScale and stamp the schema to v2.
        sensitivityX = MigrateSensitivity(sensitivityX, kLegacyBaseScale, baseScale);
        sensitivityY = MigrateSensitivity(sensitivityY, kLegacyBaseScale, baseScale);
        if (baseScale != kLegacyBaseScale && (sensitivityX > 2.0f || sensitivityY > 2.0f))
            extendedRange = true; // keep the migrated value out of the legacy clamp
        version = kCurrentVersion;
    }

    return true;
}

bool MouseConfig::Save(const std::string& path) const
{
    std::error_code ec;
    std::filesystem::path p(path);
    if (p.has_parent_path())
        std::filesystem::create_directories(p.parent_path(), ec);

    ParamFile cfg;
    cfg.Add("version", kCurrentVersion);

    // v1 keys, clamped to the legacy range so a downgraded build reads a sane value.
    cfg.Add("reverseY", reverseY);
    cfg.Add("buttonsReversed", buttonsReversed);
    cfg.Add("sensitivityX", std::clamp(sensitivityX, 0.5f, 2.0f));
    cfg.Add("sensitivityY", std::clamp(sensitivityY, 0.5f, 2.0f));

    // Authoritative full-range sensitivity for v2+ readers.
    cfg.Add("lookSensX", sensitivityX);
    cfg.Add("lookSensY", sensitivityY);

    cfg.Add("baseScale", baseScale);
    cfg.Add("dpiNormalize", dpiNormalize);
    cfg.Add("mouseDpi", mouseDpi);
    cfg.Add("referenceDpi", referenceDpi);
    cfg.Add("smoothing", smoothing);
    cfg.Add("acceleration", acceleration);
    cfg.Add("accelExponent", accelExponent);
    cfg.Add("menuCursorScale", menuCursorScale);
    cfg.Add("extendedRange", extendedRange);

    cfg.Save(RString(path.c_str()));
    return std::filesystem::exists(path, ec);
}

} // namespace Poseidon
