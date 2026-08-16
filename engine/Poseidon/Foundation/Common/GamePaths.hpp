#pragma once

#include <string>
#include <Poseidon/Foundation/platform.hpp>

// Game content directory names (canonical casing, platform separator appended)
namespace GameDirs
{
constexpr const char* MPMissions     = "MPMissions";
constexpr const char* MPMissionsCache = "MPMissionsCache";
constexpr const char* Missions       = "Missions";
constexpr const char* Users          = "Users";

/// "MPMissions/" or "MPMissions\\" depending on platform.
inline std::string MPMissionsPath()
{
    return std::string(MPMissions) + PATH_SEP;
}

/// "MPMissionsCache/" or "MPMissionsCache\\" depending on platform.
inline std::string MPMissionsCachePath()
{
    return std::string(MPMissionsCache) + PATH_SEP;
}

/// MP bank prefix for __cur_mp, e.g. "MPMissions/__cur_mp." (platform-native separator).
inline std::string MPCurrentPrefix()
{
    return std::string(MPMissions) + PATH_SEP + "__cur_mp.";
}
} // namespace GameDirs

namespace Poseidon::Foundation
{

struct ResolvedGamePaths
{
	std::string codename;
	std::string cfgName;
	std::string userDir;
	std::string cacheDir;
	std::string tempDir;
	std::string userContentDir;
	std::string modsDir;
	std::string workshopDir;
	std::string missionsDir;
	std::string mpMissionsDir;
	bool oldPaths = false;
};

// Resolves platform-standard game directories with environment overrides for tests,
// portable installs, and legacy same-folder runtime mode.
class GamePaths
{
public:
	static GamePaths& Instance();

	/// Initialize paths from app-provided codename and config base name.
	/// @param codename     Directory name for user/cache paths (e.g. "CWR")
	/// @param cfgBase      Config file base name without extension (e.g. "ColdWarAssault")
	/// @param productName  Friendly name for the user-content (Documents) folder
	///                     (e.g. "Cold War Assault"); defaults to cfgBase.
	/// Creates directories if they don't exist.
	void Initialize(const char* codename, const char* cfgBase, const char* productName = nullptr,
	                bool oldPaths = false, const char* oldPathsRoot = nullptr);

	/// Resolve the directory layout without mutating the singleton. Exposed so
	/// tests can cover legacy path mode without reinitializing GamePaths.
	static ResolvedGamePaths Resolve(const char* codename, const char* cfgBase, const char* productName = nullptr,
	                                 bool oldPaths = false, const char* oldPathsRoot = nullptr);

	/// Resolve the user dir without Initialize() (POSEIDON_USER_DIR or platform
	/// default, trailing-slash). Prefer UserDir() once initialized.
	static std::string ResolveUserDir(const char* codename);

	/// Resolve the user-content dir without Initialize() (Documents / XDG-data, or a
	/// POSEIDON_USER_CONTENT_DIR / POSEIDON_USER_DIR override). Prefer UserContentDir().
	static std::string ResolveUserContentDir(const char* codename, const char* product);

	/// App codename used for directory paths (e.g. "CWR").
	const std::string& Codename() const { return m_codename; }

	/// Config filename (e.g. "ColdWarAssault.cfg").
	const std::string& CfgName() const { return m_cfgName; }

	/// User data directory for profiles, saves, config.
	const std::string& UserDir() const { return m_userDir; }

	/// Cache directory for mission cache, reports, transient data.
	const std::string& CacheDir() const { return m_cacheDir; }

	/// Temp directory for server temp files, PID files.
	const std::string& TempDir() const { return m_tempDir; }

	/// Discoverable, non-roaming user-content root (Documents/<product> on
	/// Windows, XDG-data on Linux). Parent of ModsDir/MissionsDir/MPMissionsDir.
	const std::string& UserContentDir() const { return m_userContentDir; }

	/// Where the user's own (local) mods live: <UserContentDir>/Mods/ (override:
	/// POSEIDON_MODS_DIR). Use this — NOT UserDir() — for bulky mod content.
	const std::string& ModsDir() const { return m_modsDir; }

	/// Where downloaded (workshop) mods are installed: <UserContentDir>/Workshop/
	/// (override: POSEIDON_WORKSHOP_DIR). Kept separate from ModsDir() so a mod's
	/// source — local vs downloaded — is preserved by its location across launches.
	const std::string& WorkshopDir() const { return m_workshopDir; }

	/// User single-player (editor) missions: <UserContentDir>/missions/ (lowercase
	/// to match the editor's GetMissionsDir()). Follows POSEIDON_USER_CONTENT_DIR.
	const std::string& MissionsDir() const { return m_missionsDir; }

	/// User multiplayer (editor) missions: <UserContentDir>/MPMissions/.
	/// Follows POSEIDON_USER_CONTENT_DIR.
	const std::string& MPMissionsDir() const { return m_mpMissionsDir; }

	/// Legacy same-folder runtime mode (-oldpaths).
	bool OldPaths() const { return m_oldPaths; }

	bool IsInitialized() const { return m_initialized; }

private:
	GamePaths() = default;

	std::string m_codename;
	std::string m_cfgName;
	std::string m_userDir;
	std::string m_cacheDir;
	std::string m_tempDir;
	std::string m_userContentDir;
	std::string m_modsDir;
	std::string m_workshopDir;
	std::string m_missionsDir;
	std::string m_mpMissionsDir;
	bool m_oldPaths = false;
	bool m_initialized = false;
};

} // namespace Poseidon::Foundation

using GamePaths = Poseidon::Foundation::GamePaths;
