// Unit tests for GamePaths (Poseidon/Foundation/Common/GamePaths.hpp)
// GamePaths is a singleton — we can only Initialize() once per process.
// ctest runs each test case in a separate process, so each test that
// needs an initialized singleton must call Initialize() itself.

#include <catch2/catch_test_macros.hpp>
#include <Poseidon/Foundation/Common/GamePaths.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace
{

// Helper: ensure singleton is initialized for tests that need it.
// Initialize() is idempotent — second call is a no-op.
void EnsureInitialized()
{
    // Redirect user content to a temp dir so the test never creates folders in
    // the real Documents / data dir. Doesn't affect UserDir-based assertions.
    static const std::string content = (fs::temp_directory_path() / "cwr_gamepaths_test_content").string();
#ifdef _WIN32
    _putenv_s("POSEIDON_USER_CONTENT_DIR", content.c_str());
#else
    setenv("POSEIDON_USER_CONTENT_DIR", content.c_str(), 1);
#endif
    GamePaths::Instance().Initialize("TestGamePaths", "TestConfig");
}

bool StartsWithPath(const std::string& path, const fs::path& root)
{
    const std::string lhs = fs::path(path).lexically_normal().generic_string();
    const std::string rhs = root.lexically_normal().generic_string();
    return lhs.rfind(rhs, 0) == 0;
}

#ifndef _WIN32
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
        setenv(varName, value, 1);
    }

    ~ScopedEnv()
    {
        if (hadOld)
            setenv(name.c_str(), oldValue.c_str(), 1);
        else
            unsetenv(name.c_str());
    }
};
#endif

} // anonymous namespace

TEST_CASE("GamePaths: not initialized before Initialize()", "[gamePaths]")
{
    // Fresh process — singleton not yet initialized
    auto& gp = GamePaths::Instance();
    REQUIRE_FALSE(gp.IsInitialized());
    CHECK(gp.Codename().empty());
    CHECK(gp.CfgName().empty());
    CHECK(gp.UserDir().empty());
    CHECK(gp.CacheDir().empty());
    CHECK(gp.TempDir().empty());
}

TEST_CASE("GamePaths: ResolveUserDir honours POSEIDON_USER_DIR before Initialize", "[gamePaths]")
{
#ifndef _WIN32
    ScopedEnv env("POSEIDON_USER_DIR", "/tmp/cwr_resolve_test");
    CHECK(GamePaths::ResolveUserDir("CWR") == "/tmp/cwr_resolve_test/");
#endif
}

TEST_CASE("GamePaths: ResolveUserDir falls back to the platform user dir", "[gamePaths]")
{
#ifndef _WIN32
    ScopedEnv env("POSEIDON_USER_DIR", "");
    const std::string dir = GamePaths::ResolveUserDir("CWR");
    CHECK(dir.find("/CWR") != std::string::npos);
    REQUIRE_FALSE(dir.empty());
    CHECK(dir.back() == '/');
#endif
}

TEST_CASE("GamePaths: ResolveUserContentDir honours its overrides before Initialize", "[gamePaths]")
{
#ifndef _WIN32
    {
        ScopedEnv content("POSEIDON_USER_CONTENT_DIR", "/tmp/cwr_content_test");
        CHECK(GamePaths::ResolveUserContentDir("CWR", "Cold War Assault") == "/tmp/cwr_content_test/");
    }
    {
        ScopedEnv content("POSEIDON_USER_CONTENT_DIR", "");
        ScopedEnv user("POSEIDON_USER_DIR", "/tmp/cwr_sandbox");
        CHECK(GamePaths::ResolveUserContentDir("CWR", "Cold War Assault") == "/tmp/cwr_sandbox/content/");
    }
#endif
}

TEST_CASE("GamePaths: Initialize sets codename and cfg name", "[gamePaths]")
{
    auto& gp = GamePaths::Instance();
    gp.Initialize("TestGamePaths", "TestConfig");

    REQUIRE(gp.IsInitialized());
    CHECK(gp.Codename() == "TestGamePaths");
    CHECK(gp.CfgName() == "TestConfig.cfg");
}

TEST_CASE("GamePaths: directories exist after Initialize", "[gamePaths]")
{
    EnsureInitialized();
    auto& gp = GamePaths::Instance();

    CHECK(fs::is_directory(gp.UserDir()));
    CHECK(fs::is_directory(gp.CacheDir()));
    CHECK(fs::is_directory(gp.TempDir()));
}

TEST_CASE("GamePaths: mods + missions dirs created under user content", "[gamePaths]")
{
    EnsureInitialized();
    auto& gp = GamePaths::Instance();

    CHECK_FALSE(gp.UserContentDir().empty());
    CHECK(fs::is_directory(gp.UserContentDir()));
    CHECK(fs::is_directory(gp.ModsDir()));
    CHECK(fs::is_directory(gp.WorkshopDir()));
    CHECK(fs::is_directory(gp.MissionsDir()));
    CHECK(fs::is_directory(gp.MPMissionsDir()));

    CHECK(gp.UserContentDir().back() == '/');
    CHECK(gp.ModsDir().back() == '/');
    CHECK(gp.WorkshopDir().back() == '/');
    CHECK(gp.MissionsDir().back() == '/');

    // Mods/Workshop/Missions/MPMissions live under the content root.
    CHECK(gp.ModsDir().rfind(gp.UserContentDir(), 0) == 0);
    CHECK(gp.WorkshopDir().rfind(gp.UserContentDir(), 0) == 0);
    CHECK(gp.MissionsDir().rfind(gp.UserContentDir(), 0) == 0);
    CHECK(gp.MPMissionsDir().rfind(gp.UserContentDir(), 0) == 0);

    // Local mods and downloaded mods get distinct folders, so a mod's source is
    // preserved by location.
    CHECK(gp.ModsDir() != gp.WorkshopDir());
    CHECK(gp.ModsDir().find("Mods") != std::string::npos);
    CHECK(gp.WorkshopDir().find("Workshop") != std::string::npos);

    // Editor-mission folder casing must match OptionsUI GetMissionsDir()
    // ("missions/", lowercase) and GetMPMissionsDir() ("MPMissions/") so the
    // boot-created folders ARE the ones the editor reads/writes on case-sensitive
    // filesystems. find() is case-sensitive: "Missions" would not contain "missions".
    CHECK(gp.MissionsDir().find("missions") != std::string::npos);
    CHECK(gp.MPMissionsDir().find("MPMissions") != std::string::npos);
}

TEST_CASE("GamePaths: legacy old paths root profiles and missions in the game directory", "[gamePaths]")
{
    const fs::path gameRoot = fs::temp_directory_path() / "cwr_oldpaths_game_root";

    const Poseidon::Foundation::ResolvedGamePaths paths =
        GamePaths::Resolve("TestGamePaths", "TestConfig", "Test Product", true, gameRoot.string().c_str());

    CHECK(paths.cfgName == "TestConfig.cfg");
    CHECK(paths.oldPaths);
    CHECK(StartsWithPath(paths.userDir, gameRoot));
    CHECK(StartsWithPath(paths.cacheDir, gameRoot));
    CHECK(StartsWithPath(paths.tempDir, gameRoot / "tmp"));
    CHECK(StartsWithPath(paths.userContentDir, gameRoot));
    CHECK(StartsWithPath(paths.modsDir, gameRoot / "Mods"));
    CHECK(StartsWithPath(paths.missionsDir, gameRoot / "missions"));
    CHECK(StartsWithPath(paths.mpMissionsDir, gameRoot / GameDirs::MPMissions));
}

TEST_CASE("GamePaths: directories end with separator", "[gamePaths]")
{
    EnsureInitialized();
    auto& gp = GamePaths::Instance();

    CHECK(gp.UserDir().back() == '/');
    CHECK(gp.CacheDir().back() == '/');
    CHECK(gp.TempDir().back() == '/');
}

TEST_CASE("GamePaths: temp dir uses lowercase codename", "[gamePaths]")
{
    EnsureInitialized();
    auto& gp = GamePaths::Instance();

#ifndef _WIN32
    CHECK(gp.TempDir().find("/tmp/testgamepaths") != std::string::npos);
#endif
}

TEST_CASE("GamePaths: double Initialize is idempotent", "[gamePaths]")
{
    EnsureInitialized();
    auto& gp = GamePaths::Instance();
    std::string origCodename = gp.Codename();
    std::string origCfg = gp.CfgName();

    // Second Initialize with different args should be ignored
    gp.Initialize("DifferentCodename", "DifferentConfig");

    CHECK(gp.Codename() == origCodename);
    CHECK(gp.CfgName() == origCfg);
}

#ifndef _WIN32
TEST_CASE("GamePaths: user dir contains codename", "[gamePaths]")
{
    EnsureInitialized();
    auto& gp = GamePaths::Instance();

    CHECK(gp.UserDir().find("TestGamePaths") != std::string::npos);
}

TEST_CASE("GamePaths: cache dir contains codename", "[gamePaths]")
{
    EnsureInitialized();
    auto& gp = GamePaths::Instance();

    CHECK(gp.CacheDir().find("TestGamePaths") != std::string::npos);
}
#endif
