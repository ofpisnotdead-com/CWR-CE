#pragma once

#include <Poseidon/Core/Profile/DifficultyTypes.hpp>

namespace Poseidon
{

class ParamFile;

/// Per-user configuration with difficulty settings and UI preferences.
/// Persisted to Users/{PlayerName}/UserInfo.cfg via ParamFile format.
class UserConfig
{
  public:
    UserConfig();

    /// Initializes defaults first, then overlays parsed values from UserInfo.cfg.
    void LoadFromFile(const char* filepath);

    /// Preserves existing fields not managed by this class (identity, keybindings, etc.)
    void SaveToFile(const char* filepath) const;

    /// Default every setting from the difficulty descriptors.
    void InitDefaults();
    void InitDifficulties();

    /// Whether the toggle is enabled in the current difficulty mode. A server
    /// override (set in multiplayer) takes precedence over the local profile.
    bool IsEnabled(DifficultyType type) const;

    /// Adopt the server's per-flag difficulty so queries are server-authoritative.
    void SetServerDifficulty(const bool* flags);
    /// Drop the server override; queries fall back to the local profile.
    void ClearServerDifficulty();
    bool HasServerDifficulty() const { return _serverDifficultyActive; }

    // --- Difficulty Settings ---
    bool cadetDifficulty[DTN];   // Flags for cadet mode
    bool veteranDifficulty[DTN]; // Flags for veteran mode

    // --- UI Preferences ---
    bool showTitles = true; // Show subtitles
    bool easyMode = true;  // true = cadet, false = veteran

    // --- Aspect / FOV ---
    float fovTop = 0.75f; // Vertical FOV multiplier (default 4:3)
    float fovLeft = 1.0f; // Horizontal FOV multiplier (widescreen = higher)

  private:
    void LoadFromParamFile(const ParamFile& cfg);
    void SaveToParamFile(ParamFile& cfg) const;

    // Server difficulty pushed by the host in multiplayer (session-local, not persisted).
    bool _serverDifficultyActive = false;
    bool _serverDifficulty[DTN] = {};
};
} // namespace Poseidon
