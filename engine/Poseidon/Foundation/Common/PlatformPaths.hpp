#pragma once

#include <string>

namespace Poseidon::Foundation
{

/// Returns the platform-specific user config directory for the given app.
/// Linux:   $XDG_CONFIG_HOME/<appName> (defaults to ~/.config/<appName>)
/// Windows: %APPDATA%/<appName>
/// The directory is created if it does not exist.
std::string getUserConfigDir(const char* appName);

/// Returns the platform-specific user data directory for the given app.
/// Linux:   $XDG_DATA_HOME/<appName> (defaults to ~/.local/share/<appName>)
/// Windows: %APPDATA%/<appName>
/// The directory is created if it does not exist.
std::string getUserDataDir(const char* appName);

/// Returns the platform-specific user cache directory for the given app.
/// Linux:   $XDG_CACHE_HOME/<appName> (defaults to ~/.cache/<appName>)
/// Windows: %LOCALAPPDATA%/<appName>/cache
/// The directory is created if it does not exist.
std::string getUserCacheDir(const char* appName);

/// Returns a discoverable, NON-roaming "documents"/user-content directory — the
/// place to put bulky, user-facing content like mods and editor missions.
/// Windows: %USERPROFILE%/Documents/<appName>  (NOT %APPDATA%, which roams)
/// Linux:   $XDG_DATA_HOME/<appName> (no Documents convention; data dir is right)
/// The directory is created if it does not exist.
std::string getUserDocumentsDir(const char* appName);

/// Returns the current OS login name encoded as UTF-8.
std::string getCurrentUserName();

} // namespace Poseidon::Foundation
