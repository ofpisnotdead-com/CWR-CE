#include <catch2/catch_test_macros.hpp>
#include <Poseidon/Foundation/Common/PlayerPrefs.hpp>
#include <Poseidon/Foundation/Common/PlatformPaths.hpp>
#include <Poseidon/IO/Filesystem/Utf8Paths.hpp>

#include <cstdlib>
#include <fstream>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

namespace
{

bool dirExists(const std::string& path)
{
    return fs::is_directory(Poseidon::FilesystemPathFromUtf8(path));
}

/// RAII helper to set/restore an environment variable for test isolation.
struct ScopedEnv
{
    std::string name;
    std::string oldValue;
    bool hadOld;

    ScopedEnv(const char* varName, const char* value) : name(varName)
    {
        const char* prev = getenv(varName);
        hadOld = (prev != nullptr);
        if (hadOld)
            oldValue = prev;
#ifdef _WIN32
        _putenv_s(varName, value);
#else
        setenv(varName, value, 1);
#endif
    }

    ~ScopedEnv()
    {
#ifdef _WIN32
        if (hadOld)
            _putenv_s(name.c_str(), oldValue.c_str());
        else
            _putenv_s(name.c_str(), "");
#else
        if (hadOld)
            setenv(name.c_str(), oldValue.c_str(), 1);
        else
            unsetenv(name.c_str());
#endif
    }
};

} // anonymous namespace

TEST_CASE("getUserConfigDir returns non-empty path", "[platformPaths]")
{
    std::string dir = Poseidon::Foundation::getUserConfigDir("TestApp_PlatformPaths");
    REQUIRE(!dir.empty());
    REQUIRE(dirExists(dir));
    // Cleanup
    fs::remove_all(dir);
}

TEST_CASE("getUserDataDir returns non-empty path", "[platformPaths]")
{
    std::string dir = Poseidon::Foundation::getUserDataDir("TestApp_PlatformPaths");
    REQUIRE(!dir.empty());
    REQUIRE(dirExists(dir));
    fs::remove_all(dir);
}

#ifdef _WIN32
TEST_CASE("Windows user directories preserve UTF-8 app names", "[platformPaths][windows][utf8]")
{
    const std::string appName = "\xE6\xB5\x8B\xE8\xAF\x95";
    const std::string dir = Poseidon::Foundation::getUserDataDir(appName.c_str());

    REQUIRE(dir.find(appName) != std::string::npos);
    REQUIRE(dirExists(dir));
    fs::remove_all(Poseidon::FilesystemPathFromUtf8(dir));
}

TEST_CASE("Windows player preferences persist in UTF-8 user directories", "[platformPaths][windows][utf8]")
{
    const std::string root =
        Poseidon::FilesystemPathToUtf8(fs::temp_directory_path()) + "/cwr_prefs_\xE6\xB5\x8B\xE8\xAF\x95";
    REQUIRE(Poseidon::CreateDirectoryUtf8(root.c_str()));

    ScopedEnv userDir("POSEIDON_USER_DIR", root.c_str());
    Poseidon::Foundation::prefsSetString("CWR", "PlayerName", "\xE4\xB8\x81");
    REQUIRE(Poseidon::Foundation::prefsGetString("CWR", "PlayerName") == "\xE4\xB8\x81");

    fs::remove_all(Poseidon::FilesystemPathFromUtf8(root));
}
#endif

TEST_CASE("getUserCacheDir returns non-empty path", "[platformPaths]")
{
    std::string dir = Poseidon::Foundation::getUserCacheDir("TestApp_PlatformPaths");
    REQUIRE(!dir.empty());
    REQUIRE(dirExists(dir));
    fs::remove_all(dir);
}

TEST_CASE("getUserConfigDir creates directory if missing", "[platformPaths]")
{
    const char* appName = "TestApp_CreateDir";
    std::string dir = Poseidon::Foundation::getUserConfigDir(appName);
    REQUIRE(dirExists(dir));
    // Remove and re-create to verify idempotency
    fs::remove_all(dir);
    REQUIRE(!dirExists(dir));
    dir = Poseidon::Foundation::getUserConfigDir(appName);
    REQUIRE(dirExists(dir));
    fs::remove_all(dir);
}

TEST_CASE("getUserConfigDir is writable", "[platformPaths]")
{
    std::string dir = Poseidon::Foundation::getUserConfigDir("TestApp_Writable");
    std::string testFile = dir + "/test.txt";
    {
        std::ofstream ofs(testFile);
        REQUIRE(ofs.is_open());
        ofs << "hello";
    }
    {
        std::ifstream ifs(testFile);
        REQUIRE(ifs.is_open());
        std::string content;
        ifs >> content;
        REQUIRE(content == "hello");
    }
    fs::remove_all(dir);
}

TEST_CASE("Different app names produce different directories", "[platformPaths]")
{
    std::string dir1 = Poseidon::Foundation::getUserConfigDir("TestApp_Alpha");
    std::string dir2 = Poseidon::Foundation::getUserConfigDir("TestApp_Beta");
    REQUIRE(dir1 != dir2);
    REQUIRE(dir1.find("TestApp_Alpha") != std::string::npos);
    REQUIRE(dir2.find("TestApp_Beta") != std::string::npos);
    fs::remove_all(dir1);
    fs::remove_all(dir2);
}

TEST_CASE("Config/data, doc, and cache dirs are distinct on non Windows", "[platformPaths]")
{
    std::string config = Poseidon::Foundation::getUserConfigDir("TestApp_Distinct");
    std::string data = Poseidon::Foundation::getUserDataDir("TestApp_Distinct");
    std::string doc = Poseidon::Foundation::getUserDocumentsDir("TestApp_Distinct");
    std::string cache = Poseidon::Foundation::getUserCacheDir("TestApp_Distinct");

#ifndef _WIN32
    REQUIRE(config == data);
    // On Linux with XDG defaults, these should be different base paths
    REQUIRE(config != doc);
    REQUIRE(config != cache);
    REQUIRE(data != cache);
#endif
    // All should contain the app name
    REQUIRE(config.find("TestApp_Distinct") != std::string::npos);
    REQUIRE(data.find("TestApp_Distinct") != std::string::npos);
    REQUIRE(doc.find("TestApp_Distinct") != std::string::npos);
    REQUIRE(cache.find("TestApp_Distinct") != std::string::npos);

    fs::remove_all(config);
    fs::remove_all(data);
    fs::remove_all(doc);
    fs::remove_all(cache);
}

#ifdef __APPLE__
TEST_CASE("getUserConfigDir uses Library Application Support", "[platformPaths]")
{
    auto tmpHome = fs::temp_directory_path() / "test_home_fallback";
    fs::create_directories(tmpHome);

    ScopedEnv homeEnv("HOME", tmpHome.c_str());
    std::string dir = Poseidon::Foundation::getUserConfigDir("TestApp");
    REQUIRE(dir.find(tmpHome.string()) == 0);
    REQUIRE(dir.find("Library/Application Support") != std::string::npos);
    REQUIRE(dir.find("TestApp") != std::string::npos);
    REQUIRE(dirExists(dir));

    fs::remove_all(tmpHome);
}

TEST_CASE("getUserDataDir uses Library Application Support", "[platformPaths]")
{
    auto tmpHome = fs::temp_directory_path() / "test_home_fallback";
    fs::create_directories(tmpHome);

    ScopedEnv homeEnv("HOME", tmpHome.c_str());
    std::string dir = Poseidon::Foundation::getUserDataDir("TestApp");
    REQUIRE(dir.find(tmpHome.string()) == 0);
    REQUIRE(dir.find("Library/Application Support") != std::string::npos);
    REQUIRE(dir.find("TestApp") != std::string::npos);
    REQUIRE(dirExists(dir));

    fs::remove_all(tmpHome);
}

TEST_CASE("getUserCacheDir uses Library Caches", "[platformPaths]")
{
    auto tmpHome = fs::temp_directory_path() / "test_home_fallback";
    fs::create_directories(tmpHome);

    ScopedEnv homeEnv("HOME", tmpHome.c_str());
    std::string dir = Poseidon::Foundation::getUserCacheDir("TestApp");
    REQUIRE(dir.find(tmpHome.string()) == 0);
    REQUIRE(dir.find("Library/Caches") != std::string::npos);
    REQUIRE(dir.find("TestApp") != std::string::npos);
    REQUIRE(dirExists(dir));

    fs::remove_all(tmpHome);
}

TEST_CASE("getUserDocumentsDir uses Documents", "[platformPaths]")
{
    auto tmpHome = fs::temp_directory_path() / "test_home_fallback";
    fs::create_directories(tmpHome);

    ScopedEnv homeEnv("HOME", tmpHome.c_str());
    std::string dir = Poseidon::Foundation::getUserDocumentsDir("TestApp");
    REQUIRE(dir.find(tmpHome.string()) == 0);
    REQUIRE(dir.find("Documents") != std::string::npos);
    REQUIRE(dir.find("TestApp") != std::string::npos);
    REQUIRE(dirExists(dir));

    fs::remove_all(tmpHome);
}
#elif defined(__linux__)
TEST_CASE("getUserConfigDir respects XDG_CONFIG_HOME", "[platformPaths]")
{
    // Use a temp directory to override XDG_CONFIG_HOME
    auto tmpDir = fs::temp_directory_path() / "test_xdg_config";
    fs::create_directories(tmpDir);

    ScopedEnv env("XDG_CONFIG_HOME", tmpDir.c_str());
    std::string dir = Poseidon::Foundation::getUserConfigDir("TestApp_XDG");
    REQUIRE(dir.find(tmpDir.string()) == 0);
    REQUIRE(dir.find("TestApp_XDG") != std::string::npos);
    REQUIRE(dirExists(dir));

    fs::remove_all(tmpDir);
}

TEST_CASE("getUserDataDir respects XDG_CONFIG_HOME", "[platformPaths]")
{
    auto tmpDir = fs::temp_directory_path() / "test_xdg_data";
    fs::create_directories(tmpDir);

    ScopedEnv env("XDG_CONFIG_HOME", tmpDir.c_str());
    std::string dir = Poseidon::Foundation::getUserDataDir("TestApp_XDG");
    REQUIRE(dir.find(tmpDir.string()) == 0);
    REQUIRE(dir.find("TestApp_XDG") != std::string::npos);
    REQUIRE(dirExists(dir));

    fs::remove_all(tmpDir);
}

TEST_CASE("getUserDocumentsDir respects XDG_DATA_HOME", "[platformPaths]")
{
    auto tmpDir = fs::temp_directory_path() / "test_xdg_data";
    fs::create_directories(tmpDir);

    ScopedEnv env("XDG_DATA_HOME", tmpDir.c_str());
    std::string dir = Poseidon::Foundation::getUserDocumentsDir("TestApp_XDG");
    REQUIRE(dir.find(tmpDir.string()) == 0);
    REQUIRE(dir.find("TestApp_XDG") != std::string::npos);
    REQUIRE(dirExists(dir));

    fs::remove_all(tmpDir);
}

TEST_CASE("getUserCacheDir respects XDG_CACHE_HOME", "[platformPaths]")
{
    auto tmpDir = fs::temp_directory_path() / "test_xdg_cache";
    fs::create_directories(tmpDir);

    ScopedEnv env("XDG_CACHE_HOME", tmpDir.c_str());
    std::string dir = Poseidon::Foundation::getUserCacheDir("TestApp_XDG");
    REQUIRE(dir.find(tmpDir.string()) == 0);
    REQUIRE(dir.find("TestApp_XDG") != std::string::npos);
    REQUIRE(dirExists(dir));

    fs::remove_all(tmpDir);
}

TEST_CASE("getUserConfigDir falls back to HOME/.config when XDG unset", "[platformPaths]")
{
    auto tmpHome = fs::temp_directory_path() / "test_home_fallback";
    fs::create_directories(tmpHome);

    ScopedEnv homeEnv("HOME", tmpHome.c_str());
    ScopedEnv xdgEnv("XDG_CONFIG_HOME", "");
    std::string dir = Poseidon::Foundation::getUserConfigDir("TestApp_Fallback");

    std::string expected = (tmpHome / ".config" / "TestApp_Fallback").string();
    REQUIRE(dir == expected);
    REQUIRE(dirExists(dir));

    fs::remove_all(tmpHome);
}
#endif
