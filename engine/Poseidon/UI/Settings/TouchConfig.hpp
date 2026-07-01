#pragma once

// Touch user-settings persisted to <user-dir>/touch.cfg.
// The touch module owns runtime state; this config only stores scalar tuning.

#include <string>

namespace Poseidon
{
class TouchConfig
{
  public:
    float aimSensitivity = 1.0f;
    float cursorSensitivity = 1.0f;
    // 0 = Auto (show/hide based on which input was most recently used),
    // 1 = AlwaysOn, 2 = AlwaysOff. Int rather than an enum class so the
    // ParamFile round-trip (Load/Save) stays a plain numeric field like the
    // sensitivities above; TouchInput.hpp's TouchDisplayMode enum mirrors
    // these same values.
    int displayMode = 0;

    void LoadDefaults();
    bool Normalize();
    bool Load(const std::string& path);
    bool Save(const std::string& path) const;
};

} // namespace Poseidon
