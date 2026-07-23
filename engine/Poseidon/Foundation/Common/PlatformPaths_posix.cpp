#include <Poseidon/Foundation/Common/PlatformPaths.hpp>
#include <cstdlib>
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
            base = std::string("/tmp");
        }
    }
    std::string dir = base + "/" + appName;
    ensureDirectory(dir);
    return dir;
}

} // anonymous namespace

namespace Poseidon::Foundation {

#if defined(__APPLE__)
// Desktop macOS has its own directory conventions, distinct from Linux's XDG
// dirs. No XDG_* env var override here — those aren't a macOS convention.
std::string getUserConfigDir(const char* appName) {
    return getXdgDir("", "Library/Preferences", appName);
}

std::string getUserDataDir(const char* appName) {
    return getXdgDir("", "Library/Application Support", appName);
}

std::string getUserCacheDir(const char* appName) {
    return getXdgDir("", "Library/Caches", appName);
}

std::string getUserDocumentsDir(const char* appName) {
    return getXdgDir("", "Documents", appName);
}
#else
std::string getUserConfigDir(const char* appName) {
    return getXdgDir("XDG_CONFIG_HOME", ".config", appName);
}

std::string getUserDataDir(const char* appName) {
    // While the XDG data dir is the best match by name, we mostly use the
    // user data dir for configuration files, so use the XDG config dir.
    return getXdgDir("XDG_CONFIG_HOME", ".config", appName);
}

std::string getUserCacheDir(const char* appName) {
    return getXdgDir("XDG_CACHE_HOME", ".cache", appName);
}

std::string getUserDocumentsDir(const char* appName) {
    // Linux has no per-game "Documents" convention; the XDG data dir is the
    // correct, non-roaming home for user content (mods, editor missions).
    return getXdgDir("XDG_DATA_HOME", ".local/share", appName);
}
#endif

} // namespace Poseidon::Foundation

