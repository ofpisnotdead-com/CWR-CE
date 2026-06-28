#pragma once

// Live, session mouse-feel knobs shared by MouseState (consumer),
// InputSubsystem (load/save bridge to mouse.cfg) and the dev "Mouse" tab.
// Every field defaults to classic behavior, so a default-constructed instance
// keeps mouse.cfg backwards compatible.

namespace Poseidon
{

// Mouse DPI presets for the "Mouse DPI" selector.  Index 0 = "Off" (off).
// Adjacent non-Off entries stay within 4x (the 0.5..2.0 sensitivity span), so
// sensitivity can bridge any gap with no dead zone — locked by a unit test.
inline constexpr int kMouseDpiPresets[] = {0, 400, 800, 1200, 1600, 2400, 3200, 6400, 12000, 16000, 24000, 32000};
inline constexpr int kMouseDpiPresetCount = sizeof(kMouseDpiPresets) / sizeof(kMouseDpiPresets[0]);
inline constexpr const char* kMouseDpiLabels[] = {"Off",  "400",  "800",   "1200",  "1600",  "2400",
                                                  "3200", "6400", "12000", "16000", "24000", "32000"};

enum class MouseAimMode : int
{
    Classic = 0,
    Reduced = 1,
    Direct = 2,
    Custom = 3,
};

struct MouseTuning
{
    static constexpr float kInputDeadZoneMin = 0.0f;
    static constexpr float kInputDeadZoneMax = 2.0f;
    static constexpr float kClassicFreeAimZoneX = 0.8f;
    static constexpr float kClassicFreeAimZoneY = 0.5f;
    static constexpr float kReducedFreeAimZoneX = 0.3f;
    static constexpr float kReducedFreeAimZoneY = 0.2f;

    float baseScale = 1.5f; // master look scale

    bool dpiNormalize = false; // scale raw counts by referenceDpi/mouseDpi (eDPI)
    int mouseDpi = 1600;       // player's hardware DPI
    int referenceDpi = 1600;   // DPI the feel is calibrated to

    float inputDeadZone = 0.0f; // normalized counts at reference DPI, [0,2]; 0 = off
    float smoothing = 0.0f;     // low-pass on per-frame counts, [0,0.95]; 0 = off

    bool acceleration = false;
    float accelExponent = 1.0f; // [1,2]; 1 = linear

    float menuCursorScale = 1.0f; // menu cursor speed vs look; 1 = classic

    bool extendedRange = false; // config compatibility only; not exposed by the Mouse page

    MouseAimMode aimMode = MouseAimMode::Classic;
    float freeAimZoneX = kClassicFreeAimZoneX; // normalized cursor units, horizontal
    float freeAimZoneY = kClassicFreeAimZoneY; // normalized cursor units, vertical

    // Factor applied to raw counts (1.0 unless normalization is on).
    float DpiFactor() const
    {
        return (dpiNormalize && mouseDpi > 0) ? static_cast<float>(referenceDpi) / static_cast<float>(mouseDpi) : 1.0f;
    }

    float SensMin() const { return extendedRange ? 0.05f : 0.5f; }
    float SensMax() const { return extendedRange ? 3.0f : 2.0f; }

    static void AimZonesForMode(MouseAimMode mode, float& outX, float& outY)
    {
        switch (mode)
        {
            case MouseAimMode::Reduced:
                outX = kReducedFreeAimZoneX;
                outY = kReducedFreeAimZoneY;
                return;
            case MouseAimMode::Direct:
                outX = 0.0f;
                outY = 0.0f;
                return;
            case MouseAimMode::Classic:
            case MouseAimMode::Custom:
            default:
                outX = kClassicFreeAimZoneX;
                outY = kClassicFreeAimZoneY;
                return;
        }
    }

    float EffectiveFreeAimZoneX() const
    {
        float x = freeAimZoneX;
        float y = freeAimZoneY;
        if (aimMode != MouseAimMode::Custom)
            AimZonesForMode(aimMode, x, y);
        return x;
    }

    float EffectiveFreeAimZoneY() const
    {
        float x = freeAimZoneX;
        float y = freeAimZoneY;
        if (aimMode != MouseAimMode::Custom)
            AimZonesForMode(aimMode, x, y);
        return y;
    }

    // Clamp every field into range.  Returns true if anything changed.
    bool Normalize()
    {
        bool changed = false;
        auto clampF = [&](float& v, float lo, float hi)
        {
            float c = v < lo ? lo : (v > hi ? hi : v);
            if (c != v)
            {
                v = c;
                changed = true;
            }
        };
        auto clampI = [&](int& v, int lo, int hi)
        {
            int c = v < lo ? lo : (v > hi ? hi : v);
            if (c != v)
            {
                v = c;
                changed = true;
            }
        };
        clampF(baseScale, 0.1f, 3.0f);
        clampI(mouseDpi, 100, 32000);
        clampI(referenceDpi, 100, 32000);
        clampF(inputDeadZone, kInputDeadZoneMin, kInputDeadZoneMax);
        clampF(smoothing, 0.0f, 0.95f);
        clampF(accelExponent, 1.0f, 2.0f);
        clampF(menuCursorScale, 0.1f, 4.0f);
        clampF(freeAimZoneX, 0.0f, kClassicFreeAimZoneX);
        clampF(freeAimZoneY, 0.0f, kClassicFreeAimZoneY);
        const int mode = static_cast<int>(aimMode);
        if (mode < static_cast<int>(MouseAimMode::Classic) || mode > static_cast<int>(MouseAimMode::Custom))
        {
            aimMode = MouseAimMode::Classic;
            changed = true;
        }
        return changed;
    }
};

} // namespace Poseidon
