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

    void LoadDefaults();
    bool Normalize();
    bool Load(const std::string& path);
    bool Save(const std::string& path) const;
};

} // namespace Poseidon
