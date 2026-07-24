#pragma once

// Context-scoped action binding persistence. Stores one InputProfile per
// InputContext. gamepad.cfg and mouse.cfg store scalar settings only.

#include <Poseidon/Input/InputContext.hpp>
#include <Poseidon/Input/InputProfile.hpp>

#include <array>
#include <string>

namespace Poseidon
{
class ContextControlsConfig
{
  public:
    static constexpr int ContextCount = static_cast<int>(InputContext::Count);

    std::array<InputProfile, ContextCount> profiles;

    // Set by Load when the file predated the current action set and was seeded
    // with defaults for the actions it lacked. The caller re-saves to persist it.
    bool migratedOnLoad = false;

    void LoadDefaults();
    bool Load(const std::string& path);
    bool Save(const std::string& path) const;
};
} // namespace Poseidon
