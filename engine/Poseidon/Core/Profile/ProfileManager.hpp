#pragma once

#include <string>
#include <vector>

/// Profile information

namespace Poseidon
{
struct ProfileInfo
{
    std::string name;    // Directory name (= profile name)
    std::string path;    // Full path to profile directory (with trailing separator)
    std::string cfgPath; // Full path to UserInfo.cfg
};

/// Profile management operations.
/// All methods take a basePath parameter which is the user data directory
/// (e.g., ~/.local/share/CWR/ or %APPDATA%/CWR/).
/// Profiles live in basePath/Users/{name}/.
namespace ProfileManager
{

/// The reserved profile name the dedicated server runs under (set via
/// Glob_SetPlayerName). It is a system profile, never a user profile: excluded
/// from EnumerateProfiles so it is never shown in the login picker nor
/// auto-selected/persisted as the last-used profile.
inline constexpr const char* kServerProfileName = "__SERVER__";

/// True when `name` is the reserved dedicated-server system profile.
inline bool IsServerProfileName(const std::string& name)
{
    return name == kServerProfileName;
}

/// Enumerate all existing user profiles under basePath/Users/.
/// Returns a sorted list of ProfileInfo, excluding the reserved server profile.
std::vector<ProfileInfo> EnumerateProfiles(const std::string& basePath);

/// Get the path to a specific profile's UserInfo.cfg.
/// Returns basePath/Users/{name}/UserInfo.cfg.
std::string GetProfileCfgPath(const std::string& basePath, const std::string& name);

/// Get the path to a specific profile's directory.
/// Returns basePath/Users/{name}/ (with trailing separator).
std::string GetProfileDirPath(const std::string& basePath, const std::string& name);

/// Ensure a profile's directory exists.
bool EnsureProfileDirectory(const std::string& basePath, const std::string& name);

/// Create a new profile with default UserInfo.cfg.
/// Creates the directory and writes default config via UserConfig::SaveToFile().
/// Returns true on success. Fails if profile already exists.
bool CreateProfile(const std::string& basePath, const std::string& name);

/// Delete a profile and all its contents (saves, missions, etc.).
/// Returns true on success.
bool DeleteProfile(const std::string& basePath, const std::string& name);

/// Rename a profile directory.
/// Returns true on success. Fails if newName already exists.
bool RenameProfile(const std::string& basePath, const std::string& oldName, const std::string& newName);

/// Create a default "Player" profile if no profiles exist.
/// Returns the name of the created profile, or empty string if profiles already exist.
std::string CreateDefaultProfileIfNeeded(const std::string& basePath);

/// Validate a profile name (no path separators, not empty, not "." or "..").
bool IsValidProfileName(const std::string& name);

} // namespace ProfileManager
} // namespace Poseidon
