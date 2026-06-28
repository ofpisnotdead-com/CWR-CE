#include <Poseidon/Foundation/Common/PlatformPaths.hpp>
#include <cstdlib>
#include <sys/stat.h>
#include <string>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

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

#if defined(__APPLE__) && TARGET_OS_IPHONE
// Real iOS hardware sandboxes app containers more strictly than the
// Simulator: creating a new dot-directory (".cache", ".local") directly at
// the container root fails with EPERM (confirmed on-device -- the
// Simulator allows it, masking this). Apps are only guaranteed to be able
// to write under the standard Library/Documents/tmp scaffolding Apple
// already creates, so use that instead of the desktop XDG convention.
std::string getUserConfigDir(const char* appName) {
    return getXdgDir("XDG_CONFIG_HOME", "Library/Preferences", appName);
}

std::string getUserDataDir(const char* appName) {
    return getXdgDir("XDG_DATA_HOME", "Library/Application Support", appName);
}

std::string getUserCacheDir(const char* appName) {
    return getXdgDir("XDG_CACHE_HOME", "Library/Caches", appName);
}

std::string getUserDocumentsDir(const char* appName) {
    return getXdgDir("XDG_DATA_HOME", "Documents", appName);
}
#else
std::string getUserConfigDir(const char* appName) {
    return getXdgDir("XDG_CONFIG_HOME", ".config", appName);
}

std::string getUserDataDir(const char* appName) {
    return getXdgDir("XDG_DATA_HOME", ".local/share", appName);
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

