#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <Poseidon/Foundation/Common/PlatformPaths.hpp>
#include <Poseidon/IO/Filesystem/Utf8Paths.hpp>
#include <cstdlib>
#include <filesystem>
#include <algorithm>
#include <ctype.h>
#include <system_error>
#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace Poseidon::Foundation
{

static std::string resolveDir(const char* envVar, std::string defaultPath)
{
    const char* envVal = std::getenv(envVar);
    if (envVal && envVal[0] != '\0')
    {
        std::string path = envVal;
        if (!path.empty() && path.back() != '/' && path.back() != '\\')
            path += '/';
        return path;
    }
    if (!defaultPath.empty() && defaultPath.back() != '/' && defaultPath.back() != '\\')
        defaultPath += '/';
    return defaultPath;
}

static std::string getSystemTempDir(const char* codename)
{
    std::string lower(codename);
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
#ifdef _WIN32
    wchar_t tmp[MAX_PATH + 1] = {};
    const DWORD size = ::GetTempPathW(MAX_PATH, tmp);
    if (size > 0 && size <= MAX_PATH)
        return Poseidon::WidePathToUtf8(tmp) + lower;
    return "C:\\Temp\\" + lower;
#else
    return "/tmp/" + lower;
#endif
}

GamePaths& GamePaths::Instance()
{
    static GamePaths instance;
    return instance;
}

std::string GamePaths::ResolveUserDir(const char* codename)
{
    return resolveDir("POSEIDON_USER_DIR", getUserDataDir(codename));
}

std::string GamePaths::ResolveUserContentDir(const char* codename, const char* product)
{
    if (const char* env = std::getenv("POSEIDON_USER_CONTENT_DIR"); env && env[0] != '\0')
        return resolveDir("POSEIDON_USER_CONTENT_DIR", "");
    if (const char* env = std::getenv("POSEIDON_USER_DIR"); env && env[0] != '\0')
        return resolveDir("", ResolveUserDir(codename) + "content");
    return resolveDir("", getUserDocumentsDir(product));
}

ResolvedGamePaths GamePaths::Resolve(const char* codename, const char* cfgBase, const char* productName, bool oldPaths,
                                     const char* oldPathsRoot)
{
    ResolvedGamePaths resolved;
    resolved.codename = codename;
    resolved.cfgName = std::string(cfgBase) + ".cfg";
    const std::string product = (productName && productName[0] != '\0') ? productName : cfgBase;
    resolved.oldPaths = oldPaths;

    if (oldPaths)
    {
        std::string root;
        if (oldPathsRoot && oldPathsRoot[0] != '\0')
            root = oldPathsRoot;
        else
            root = Poseidon::FilesystemPathToUtf8(fs::current_path());

        resolved.userDir = resolveDir("", root);
        resolved.userContentDir = resolved.userDir;
        resolved.cacheDir = resolveDir("", resolved.userDir);
        resolved.tempDir = resolveDir("", resolved.userDir + "tmp");
        resolved.modsDir = resolveDir("", resolved.userContentDir + "Mods");
        resolved.workshopDir = resolveDir("", resolved.userContentDir + "Workshop");
        resolved.missionsDir = resolveDir("", resolved.userContentDir + "missions");
        resolved.mpMissionsDir = resolveDir("", resolved.userContentDir + GameDirs::MPMissions);
        return resolved;
    }

    resolved.cacheDir = resolveDir("POSEIDON_CACHE_DIR", getUserCacheDir(codename));
    resolved.tempDir = resolveDir("POSEIDON_TEMP_DIR", getSystemTempDir(codename));
    resolved.userDir = ResolveUserDir(codename);

    // Bulky, user-facing content (mods, editor missions) lives in a discoverable,
    // non-roaming root — Documents on Windows, XDG-data on Linux — NOT the
    // (roaming) UserDir. Resolution order:
    //   1. explicit POSEIDON_USER_CONTENT_DIR;
    //   2. sandboxed runs (POSEIDON_USER_DIR set, e.g. tests) keep content inside
    //      that sandbox so we never create folders in the real Documents;
    //   3. otherwise the platform Documents / XDG-data folder.
    resolved.userContentDir = ResolveUserContentDir(codename, product.c_str());

    // ModsDir is independently overridable (POSEIDON_MODS_DIR) — the mod loader
    // reads it directly. Editor-mission folders, by contrast, are derived from
    // UserContentDir to mirror the editor's own base + "missions/" / "MPMissions/"
    // path convention (see OptionsUI GetMissionsDir/GetMPMissionsDir), so they
    // follow POSEIDON_USER_CONTENT_DIR rather than a per-folder env var. The
    // casing here must match the editor's so the boot-created folders ARE the
    // ones the editor reads/writes on case-sensitive filesystems.
    resolved.modsDir = resolveDir("POSEIDON_MODS_DIR", resolved.userContentDir + "Mods");
    resolved.workshopDir = resolveDir("POSEIDON_WORKSHOP_DIR", resolved.userContentDir + "Workshop");
    resolved.missionsDir = resolveDir("", resolved.userContentDir + "missions");
    resolved.mpMissionsDir = resolveDir("", resolved.userContentDir + GameDirs::MPMissions);
    return resolved;
}

void GamePaths::Initialize(const char* codename, const char* cfgBase, const char* productName, bool oldPaths,
                           const char* oldPathsRoot)
{
    if (m_initialized)
        return;

    const ResolvedGamePaths resolved = Resolve(codename, cfgBase, productName, oldPaths, oldPathsRoot);

    m_codename = resolved.codename;
    m_cfgName = resolved.cfgName;
    m_userDir = resolved.userDir;
    m_cacheDir = resolved.cacheDir;
    m_tempDir = resolved.tempDir;
    m_userContentDir = resolved.userContentDir;
    m_modsDir = resolved.modsDir;
    m_workshopDir = resolved.workshopDir;
    m_missionsDir = resolved.missionsDir;
    m_mpMissionsDir = resolved.mpMissionsDir;
    m_oldPaths = resolved.oldPaths;

    Poseidon::CreateDirectoryUtf8(m_userDir.c_str());
    Poseidon::CreateDirectoryUtf8(m_cacheDir.c_str());
    Poseidon::CreateDirectoryUtf8(m_tempDir.c_str());
    Poseidon::CreateDirectoryUtf8(m_userContentDir.c_str());
    Poseidon::CreateDirectoryUtf8(m_modsDir.c_str());
    Poseidon::CreateDirectoryUtf8(m_workshopDir.c_str());
    Poseidon::CreateDirectoryUtf8(m_missionsDir.c_str());
    Poseidon::CreateDirectoryUtf8(m_mpMissionsDir.c_str());

    m_initialized = true;
}

} // namespace Poseidon::Foundation
