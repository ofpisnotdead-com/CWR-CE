// ProfileService Tests
// Covers the pure startup-selection policy (DecideStartupProfile) and the
// ProfileService orchestration against injected fake boundaries + a temp user
// dir. The load-bearing case: an existing profile must be selected at startup
// rather than having a fresh OS-login/default profile created alongside it.

#include <catch2/catch_test_macros.hpp>
#include <Poseidon/Core/Profile/ProfileService.hpp>
#include <Poseidon/Core/Profile/ProfileManager.hpp>
#include <filesystem>
#include <string>
#include <stdlib.h>
#include <functional>
#include <system_error>
#include <vector>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace Poseidon;

namespace
{
std::string makeTempDir()
{
    auto path = fs::temp_directory_path() / ("cwr_profsvc_" +
                                             std::to_string(
#ifdef _WIN32
                                                 _getpid()
#else
                                                 getpid()
#endif
                                                     ) +
                                             "_" + std::to_string(rand()));
    fs::create_directories(path);
    return path.string();
}

struct TempDirGuard
{
    std::string path;
    TempDirGuard() : path(makeTempDir()) {}
    ~TempDirGuard()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};
} // namespace

// ── Pure policy: DecideStartupProfile ──────────────────────────────────────

TEST_CASE("DecideStartupProfile uses the persisted profile when it still exists", "[profile]")
{
    auto c = DecideStartupProfile("Alice", {"Alice", "Bob"}, "loginuser");
    REQUIRE(c.name == "Alice");
    REQUIRE_FALSE(c.create);
    REQUIRE_FALSE(c.persist);
}

TEST_CASE("DecideStartupProfile prefers the single existing profile over a new default", "[profile]")
{
    // No persisted name, but a profile already exists: select and persist it,
    // never create the OS-login/default profile alongside it.
    auto c = DecideStartupProfile("", {"Alice"}, "loginuser");
    REQUIRE(c.name == "Alice");
    REQUIRE_FALSE(c.create);
    REQUIRE(c.persist);
}

TEST_CASE("DecideStartupProfile falls back to an existing profile when the persisted one is stale", "[profile]")
{
    // Persisted "Ghost" no longer exists: pick an existing profile, do not recreate "Ghost".
    auto c = DecideStartupProfile("Ghost", {"Alice", "Bob"}, "loginuser");
    REQUIRE(c.name == "Alice"); // deterministic: enumeration is sorted
    REQUIRE_FALSE(c.create);
    REQUIRE(c.persist);
}

TEST_CASE("DecideStartupProfile picks deterministically among multiple profiles with no usable persist", "[profile]")
{
    auto c = DecideStartupProfile("", {"Alice", "Bob", "Carol"}, "loginuser");
    REQUIRE(c.name == "Alice");
    REQUIRE_FALSE(c.create);
    REQUIRE(c.persist);
}

TEST_CASE("DecideStartupProfile creates the OS-login default when no profiles exist", "[profile]")
{
    auto c = DecideStartupProfile("", {}, "loginuser");
    REQUIRE(c.name == "loginuser");
    REQUIRE(c.create);
    REQUIRE(c.persist);
}

TEST_CASE("DecideStartupProfile creates Player when no profiles exist and OS login is unusable", "[profile]")
{
    auto invalid = DecideStartupProfile("", {}, "bad/name");
    REQUIRE(invalid.name == "Player");
    REQUIRE(invalid.create);

    auto empty = DecideStartupProfile("", {}, "");
    REQUIRE(empty.name == "Player");
    REQUIRE(empty.create);
}

// ── ProfileService orchestration ───────────────────────────────────────────

TEST_CASE("ProfileService selects an existing profile without creating a default", "[profile]")
{
    TempDirGuard tmp;
    REQUIRE(ProfileManager::CreateProfile(tmp.path, "Alice"));

    std::string persisted; // nothing saved yet
    std::string saved;
    ProfileService svc({tmp.path, [&] { return persisted; }, [&](const std::string& n) { saved = n; },
                        [] { return std::string("loginuser"); }});

    REQUIRE(svc.ResolveStartupProfile() == "Alice");

    // The OS-login/default profile must NOT have been created.
    auto profiles = ProfileManager::EnumerateProfiles(tmp.path);
    REQUIRE(profiles.size() == 1);
    REQUIRE(profiles[0].name == "Alice");
    // And the selection was remembered as the last-used profile.
    REQUIRE(saved == "Alice");
}

TEST_CASE("ProfileService creates and persists a default when no profiles exist", "[profile]")
{
    TempDirGuard tmp;
    std::string saved;
    ProfileService svc({tmp.path, [] { return std::string(); }, [&](const std::string& n) { saved = n; },
                        [] { return std::string("bob"); }});

    REQUIRE(svc.ResolveStartupProfile() == "bob");

    auto profiles = ProfileManager::EnumerateProfiles(tmp.path);
    REQUIRE(profiles.size() == 1);
    REQUIRE(profiles[0].name == "bob");
    REQUIRE(saved == "bob");
}

TEST_CASE("ProfileService reuses the persisted profile and creates nothing", "[profile]")
{
    TempDirGuard tmp;
    REQUIRE(ProfileManager::CreateProfile(tmp.path, "Alice"));
    REQUIRE(ProfileManager::CreateProfile(tmp.path, "Bob"));

    bool savedCalled = false;
    ProfileService svc({tmp.path, [] { return std::string("Bob"); }, [&](const std::string&) { savedCalled = true; },
                        [] { return std::string("loginuser"); }});

    REQUIRE(svc.ResolveStartupProfile() == "Bob");
    REQUIRE_FALSE(savedCalled);                                       // already the persisted profile
    REQUIRE(ProfileManager::EnumerateProfiles(tmp.path).size() == 2); // nothing created
}

// ── Reserved server profile must never become the last-used profile ─────────
// The dedicated server runs under the "__SERVER__" profile (Glob_SetPlayerName) and creates
// Users/__SERVER__ as a side effect. It is a system profile: it must never be enumerated for
// the login picker nor auto-selected/persisted as the last-used player profile.

TEST_CASE("EnumerateProfiles excludes the reserved server profile", "[profile]")
{
    TempDirGuard tmp;
    REQUIRE(ProfileManager::CreateProfile(tmp.path, "Alice"));
    fs::create_directories(fs::path(tmp.path) / "Users" / ProfileManager::kServerProfileName);

    auto profiles = ProfileManager::EnumerateProfiles(tmp.path);
    REQUIRE(profiles.size() == 1);
    REQUIRE(profiles[0].name == "Alice");
}

TEST_CASE("ProfileService never persists the server profile as last-used", "[profile]")
{
    // Only the server profile exists on disk; "__SERVER__" sorts before a lowercase login name,
    // so without the exclusion it would be picked as the first existing profile and persisted.
    TempDirGuard tmp;
    fs::create_directories(fs::path(tmp.path) / "Users" / ProfileManager::kServerProfileName);

    std::string saved;
    ProfileService svc({tmp.path, [] { return std::string(); }, [&](const std::string& n) { saved = n; },
                        [] { return std::string("tonyhawk"); }});

    REQUIRE(svc.ResolveStartupProfile() == "tonyhawk"); // a real default, not the server profile
    REQUIRE(saved == "tonyhawk");
    REQUIRE(saved != ProfileManager::kServerProfileName);
}

TEST_CASE("ProfileService self-heals a persisted server profile name", "[profile]")
{
    // The persisted value points at the reserved server profile.
    TempDirGuard tmp;
    REQUIRE(ProfileManager::CreateProfile(tmp.path, "Alice"));
    fs::create_directories(fs::path(tmp.path) / "Users" / ProfileManager::kServerProfileName);

    std::string saved;
    ProfileService svc({tmp.path, [] { return std::string(ProfileManager::kServerProfileName); },
                        [&](const std::string& n) { saved = n; }, [] { return std::string("loginuser"); }});

    REQUIRE(svc.ResolveStartupProfile() == "Alice"); // falls back to the real profile
    REQUIRE(saved == "Alice");
}
