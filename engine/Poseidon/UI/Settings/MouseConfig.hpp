#pragma once

// Mouse user-settings persisted to <user-dir>/mouse.cfg.
//
// Sibling to AudioConfig — same Defaults / Normalize / Load / Save
// pattern.  Holds the four binding-screen scalars: Y-invert, button swap, and
// per-axis sensitivity.
//
// Lifecycle:
//   1. Boot: try Load(path).  Missing/corrupt → LoadDefaults() +
//            Save(path).
//   2. Boot: Normalize() clamps sensitivities to [0.5, 2.0] (the
//            range the original sensitivity sliders shipped with).
//   3. UI:   MousePage applies values live and Saves on Unmount.

#include <string>


namespace Poseidon
{
class MouseConfig
{
public:
    // Schema version.  A file with no `version` key is treated as v1 and
    // migrated to v2 on load (see Load).  Bump on every schema change.
    static constexpr int kCurrentVersion = 2;
    // baseScale the legacy v1 sensitivities were authored against (MigrateSensitivity).
    static constexpr float kLegacyBaseScale = 1.5f;

    // --- v1 fields (classic; unchanged on disk for downgrade safety) ---
    bool  reverseY            = false;
    bool  buttonsReversed     = false;
    float sensitivityX        = 1.0f;
    float sensitivityY        = 1.0f;

    // --- v2 fields (HiDPI tuning; all default to classic / no-op feel) ---
    float baseScale       = 1.5f;  // master look scale
    bool  dpiNormalize    = false;
    int   mouseDpi        = 1600;
    int   referenceDpi    = 1600;  // DPI the feel is calibrated to
    float smoothing       = 0.0f;
    bool  acceleration    = false;
    float accelExponent   = 1.0f;
    float menuCursorScale = 1.0f;
    bool  extendedRange   = false;

    // Version actually parsed from the file (kCurrentVersion for a fresh
    // instance / a v2 file; 1 for a legacy file that was migrated).
    int   version         = kCurrentVersion;

    // Reset every field to factory defaults.
    void LoadDefaults();

    // Clamp sensitivities (range depends on extendedRange) and the v2 tuning
    // fields into their supported ranges.  Returns true if any field changed.
    bool Normalize();

    // True on successful parse, false if the file doesn't exist or is
    // unparseable.  A v1 file is migrated to v2 on load.
    bool Load(const std::string& path);

    // Writes via ParamFile (both v1 and v2 keys, so an older build can still
    // read the v1 fields).  Returns false on I/O error.
    bool Save(const std::string& path) const;

    // Rescale a legacy sensitivity to preserve feel under a changed baseScale:
    // newSens = oldSens * (legacyBaseScale / newBaseScale).  Identity while baseScale stays 1.5.
    static float MigrateSensitivity(float legacySens, float legacyBaseScale, float newBaseScale);
};

} // namespace Poseidon
