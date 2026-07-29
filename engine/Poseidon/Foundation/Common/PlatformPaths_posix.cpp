#include <Poseidon/Foundation/Common/PlatformPaths.hpp>
#include <cstdlib>
#include <filesystem>
#include <sys/stat.h>
#include <string>

namespace {

void ensureDirectory(const std::string& path) {
    if (path.empty()) return;
    for (size_t i = 1; i < path.size(); ++i) {
        if (path[i] == '/') {
            std::string partial = path.substr(0, i);
            mkdir(partial.c_str(), 0755);
        }
    }
    mkdir(path.c_str(), 0755);
}

#ifdef __APPLE__
std::string getHomeDir(const char* subpath, const char* appName) {
    std::string base;
    const char* home = getenv("HOME");
    if (home && home[0] != '\0') {
        base = std::string(home) + "/" + subpath;
    } else {
        base = std::filesystem::temp_directory_path();
    }
    std::string dir = base + "/" + appName;
    ensureDirectory(dir);
    return dir;
}
#else
std::string getXdgDir(const char* envVar, const char* defaultSuffix, const char* appName) {
    std::string base;
    const char* envVal = getenv(envVar);
    if (envVal && envVal[0] != '\0') {
        base = envVal;
    } else {
        const char* home = getenv("HOME");
        if (home && home[0] != '\0') {
            base = std::string(home) + "/" + defaultSuffix;
        } else {
            base = std::filesystem::temp_directory_path();
        }
    }
    std::string dir = base + "/" + appName;
    ensureDirectory(dir);
    return dir;
}
#endif

} // anonymous namespace

namespace Poseidon::Foundation {

std::string getUserConfigDir(const char* appName) {
#ifdef __APPLE__
    return getHomeDir("Library/Application Support", appName);
#else
    return getXdgDir("XDG_CONFIG_HOME", ".config", appName);
#endif
}

std::string getUserDataDir(const char* appName) {
#ifdef __APPLE__
    return getHomeDir("Library/Application Support", appName);
#else
    // While the XDG data dir is the best match by name, we mostly use the
    // user data dir for configuration files, so use the XDG config dir.
    return getXdgDir("XDG_CONFIG_HOME", ".config", appName);
#endif
}

std::string getUserCacheDir(const char* appName) {
#ifdef __APPLE__
    return getHomeDir("Library/Caches", appName);
#else
    return getXdgDir("XDG_CACHE_HOME", ".cache", appName);
#endif
}

std::string getUserDocumentsDir(const char* appName) {
#ifdef __APPLE__
    return getHomeDir("Documents", appName);
#else
    // Linux has no per-game "Documents" convention; the XDG data dir is the
    // correct, non-roaming home for user content (mods, editor missions).
    return getXdgDir("XDG_DATA_HOME", ".local/share", appName);
#endif
}

} // namespace Poseidon::Foundation

