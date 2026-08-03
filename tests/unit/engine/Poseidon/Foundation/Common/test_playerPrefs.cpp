#include <catch2/catch_test_macros.hpp>
#include <Poseidon/Foundation/Common/PlayerPrefs.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace
{

struct ScopedEnv
{
    std::string name;
    std::string oldValue;
    bool hadOld;

    ScopedEnv(const char* varName, const std::string& value) : name(varName)
    {
#ifdef _WIN32
        const char* prev = std::getenv(varName);
        hadOld = (prev != nullptr);
        if (hadOld)
            oldValue = prev;
        _putenv_s(varName, value.c_str());
#else
        const char* prev = getenv(varName);
        hadOld = (prev != nullptr);
        if (hadOld)
            oldValue = prev;
        setenv(varName, value.c_str(), 1);
#endif
    }

    ~ScopedEnv()
    {
#ifdef _WIN32
        _putenv_s(name.c_str(), hadOld ? oldValue.c_str() : "");
#else
        if (hadOld)
            setenv(name.c_str(), oldValue.c_str(), 1);
        else
            unsetenv(name.c_str());
#endif
    }
};

struct TempDir
{
    fs::path path;

    explicit TempDir(const char* name) : path(fs::temp_directory_path() / name) { fs::create_directories(path); }

    ~TempDir() { fs::remove_all(path); }
};

} // anonymous namespace

TEST_CASE("PlayerPrefs: missing key returns the supplied default", "[playerPrefs]")
{
    TempDir dir("cwr_playerprefs_test_missing");
    ScopedEnv env("POSEIDON_USER_DIR", dir.path.string());

    CHECK(Poseidon::Foundation::prefsGetString("TestApp", "NoSuchKey", "fallback") == "fallback");
}

TEST_CASE("PlayerPrefs: set then get round-trips a value", "[playerPrefs]")
{
    TempDir dir("cwr_playerprefs_test_roundtrip");
    ScopedEnv env("POSEIDON_USER_DIR", dir.path.string());

    Poseidon::Foundation::prefsSetString("TestApp", "LastProfile", "aaaa");
    CHECK(Poseidon::Foundation::prefsGetString("TestApp", "LastProfile") == "aaaa");
}

TEST_CASE("PlayerPrefs: a later set overwrites an earlier one for the same key", "[playerPrefs]")
{
    TempDir dir("cwr_playerprefs_test_overwrite");
    ScopedEnv env("POSEIDON_USER_DIR", dir.path.string());

    Poseidon::Foundation::prefsSetString("TestApp", "LastProfile", "first");
    Poseidon::Foundation::prefsSetString("TestApp", "LastProfile", "second");
    CHECK(Poseidon::Foundation::prefsGetString("TestApp", "LastProfile") == "second");
}

TEST_CASE("PlayerPrefs: value persists across a fresh load (simulated restart)", "[playerPrefs]")
{
    TempDir dir("cwr_playerprefs_test_persist");
    ScopedEnv env("POSEIDON_USER_DIR", dir.path.string());

    Poseidon::Foundation::prefsSetString("TestApp", "LastProfile", "bbbb");

    REQUIRE(fs::exists(dir.path / "prefs.cfg"));

    CHECK(Poseidon::Foundation::prefsGetString("TestApp", "LastProfile") == "bbbb");
}

TEST_CASE("PlayerPrefs: distinct app names use distinct prefs files", "[playerPrefs]")
{
    TempDir dirA("cwr_playerprefs_test_appA");
    TempDir dirB("cwr_playerprefs_test_appB");

    {
        ScopedEnv env("POSEIDON_USER_DIR", dirA.path.string());
        Poseidon::Foundation::prefsSetString("AppA", "LastProfile", "from-a");
    }
    {
        ScopedEnv env("POSEIDON_USER_DIR", dirB.path.string());
        Poseidon::Foundation::prefsSetString("AppB", "LastProfile", "from-b");
    }
    {
        ScopedEnv env("POSEIDON_USER_DIR", dirA.path.string());
        CHECK(Poseidon::Foundation::prefsGetString("AppA", "LastProfile") == "from-a");
    }
    {
        ScopedEnv env("POSEIDON_USER_DIR", dirB.path.string());
        CHECK(Poseidon::Foundation::prefsGetString("AppB", "LastProfile") == "from-b");
    }
}

TEST_CASE("PlayerPrefs: a value containing '=' round-trips intact", "[playerPrefs]")
{
    TempDir dir("cwr_playerprefs_test_equals");
    ScopedEnv env("POSEIDON_USER_DIR", dir.path.string());

    Poseidon::Foundation::prefsSetString("TestApp", "Key", "a=b");
    CHECK(Poseidon::Foundation::prefsGetString("TestApp", "Key") == "a=b");
}
