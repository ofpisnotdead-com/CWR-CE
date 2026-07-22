#include <Poseidon/Foundation/Common/PlayerPrefs.hpp>
#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <cstdlib>
#include <fstream>
#include <unordered_map>
#include <utility>

namespace
{

std::string prefsFilePath(const char* appName)
{
    // Resolve via GamePaths so prefs.cfg honours POSEIDON_USER_DIR and stays with the
    // profiles, instead of leaking to the real appdata under a test override.
    return Poseidon::Foundation::GamePaths::ResolveUserDir(appName) + "prefs.cfg";
}

std::unordered_map<std::string, std::string> loadPrefs(const char* appName)
{
    std::unordered_map<std::string, std::string> prefs;
    std::ifstream file(prefsFilePath(appName));
    if (!file.is_open())
        return prefs;

    std::string line;
    while (std::getline(file, line))
    {
        auto eq = line.find('=');
        if (eq != std::string::npos && eq > 0)
        {
            prefs[line.substr(0, eq)] = line.substr(eq + 1);
        }
    }
    return prefs;
}

void savePrefs(const char* appName, const std::unordered_map<std::string, std::string>& prefs)
{
    std::ofstream file(prefsFilePath(appName));
    if (!file.is_open())
        return;

    for (auto& [key, value] : prefs)
    {
        file << key << '=' << value << '\n';
    }
}

} // anonymous namespace

namespace Poseidon::Foundation
{

std::string prefsGetString(const char* appName, const char* key, const char* defaultValue)
{
    auto prefs = loadPrefs(appName);
    auto it = prefs.find(key);
    if (it != prefs.end())
        return it->second;
    return defaultValue ? defaultValue : "";
}

void prefsSetString(const char* appName, const char* key, const char* value)
{
    auto prefs = loadPrefs(appName);
    prefs[key] = value ? value : "";
    savePrefs(appName, prefs);
}

} // namespace Poseidon::Foundation
