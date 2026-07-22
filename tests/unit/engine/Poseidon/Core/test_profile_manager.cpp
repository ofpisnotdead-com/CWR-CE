// ProfileManager Tests
// Verifies profile CRUD operations using ephemeral temp directories

#include <catch2/catch_test_macros.hpp>
#include <Poseidon/Core/Profile/ProfileManager.hpp>
#include <Poseidon/Core/Profile/UserConfig.hpp>
#include <Poseidon/IO/Filesystem/Utf8Paths.hpp>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

static fs::path pathFromUtf8(const std::string& path)
{
#ifdef _WIN32
    return fs::path(Poseidon::Utf8PathToWide(path.c_str()));
#else
    return fs::path(path);
#endif
}

static std::string makeTempDir()
{
    auto path = fs::temp_directory_path() / ("cwr_test_" +
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

TEST_CASE("Poseidon::ProfileManager::IsValidProfileName", "[profile]")
{
    REQUIRE(Poseidon::ProfileManager::IsValidProfileName("Player") == true);
    REQUIRE(Poseidon::ProfileManager::IsValidProfileName("My Player 1") == true);
    REQUIRE(Poseidon::ProfileManager::IsValidProfileName("Šárka Říha") == true);
    REQUIRE(Poseidon::ProfileManager::IsValidProfileName("") == false);
    REQUIRE(Poseidon::ProfileManager::IsValidProfileName(".") == false);
    REQUIRE(Poseidon::ProfileManager::IsValidProfileName("..") == false);
    REQUIRE(Poseidon::ProfileManager::IsValidProfileName("a/b") == false);
    REQUIRE(Poseidon::ProfileManager::IsValidProfileName("a\\b") == false);
    REQUIRE(Poseidon::ProfileManager::IsValidProfileName("a:b") == false);
    REQUIRE(Poseidon::ProfileManager::IsValidProfileName("a*b") == false);
}

TEST_CASE("Poseidon::ProfileManager::CreateProfile", "[profile]")
{
    TempDirGuard tmp;

    SECTION("creates directory and UserInfo.cfg")
    {
        REQUIRE(Poseidon::ProfileManager::CreateProfile(tmp.path, "TestPlayer"));
        REQUIRE(fs::is_directory(tmp.path + "/Users/TestPlayer"));
        REQUIRE(fs::is_regular_file(tmp.path + "/Users/TestPlayer/UserInfo.cfg"));
    }

    SECTION("creates directory for UTF-8 profile names")
    {
        REQUIRE(Poseidon::ProfileManager::CreateProfile(tmp.path, "Šárka"));
        fs::path profileDir = fs::path(tmp.path) / "Users" / pathFromUtf8("Šárka");
        REQUIRE(fs::is_directory(profileDir));
        REQUIRE(fs::is_regular_file(profileDir / "UserInfo.cfg"));
    }

    SECTION("fails if profile already exists")
    {
        REQUIRE(Poseidon::ProfileManager::CreateProfile(tmp.path, "Dupe"));
        REQUIRE_FALSE(Poseidon::ProfileManager::CreateProfile(tmp.path, "Dupe"));
    }

    SECTION("fails for invalid name")
    {
        REQUIRE_FALSE(Poseidon::ProfileManager::CreateProfile(tmp.path, ""));
        REQUIRE_FALSE(Poseidon::ProfileManager::CreateProfile(tmp.path, ".."));
        REQUIRE_FALSE(Poseidon::ProfileManager::CreateProfile(tmp.path, "a/b"));
    }
}

TEST_CASE("Poseidon::ProfileManager::EnumerateProfiles", "[profile]")
{
    TempDirGuard tmp;

    SECTION("empty when no profiles")
    {
        auto profiles = Poseidon::ProfileManager::EnumerateProfiles(tmp.path);
        REQUIRE(profiles.empty());
    }

    SECTION("lists created profiles sorted")
    {
        Poseidon::ProfileManager::CreateProfile(tmp.path, "Zeta");
        Poseidon::ProfileManager::CreateProfile(tmp.path, "Alpha");
        Poseidon::ProfileManager::CreateProfile(tmp.path, "Middle");

        auto profiles = Poseidon::ProfileManager::EnumerateProfiles(tmp.path);
        REQUIRE(profiles.size() == 3);
        REQUIRE(profiles[0].name == "Alpha");
        REQUIRE(profiles[1].name == "Middle");
        REQUIRE(profiles[2].name == "Zeta");
    }

    SECTION("cfgPath points to UserInfo.cfg")
    {
        Poseidon::ProfileManager::CreateProfile(tmp.path, "Test");
        auto profiles = Poseidon::ProfileManager::EnumerateProfiles(tmp.path);
        REQUIRE(profiles.size() == 1);
        REQUIRE(profiles[0].cfgPath.find("UserInfo.cfg") != std::string::npos);
    }
}

TEST_CASE("Poseidon::ProfileManager::DeleteProfile", "[profile]")
{
    TempDirGuard tmp;
    Poseidon::ProfileManager::CreateProfile(tmp.path, "ToDelete");
    REQUIRE(fs::exists(tmp.path + "/Users/ToDelete"));

    REQUIRE(Poseidon::ProfileManager::DeleteProfile(tmp.path, "ToDelete"));
    REQUIRE_FALSE(fs::exists(tmp.path + "/Users/ToDelete"));

    // Delete non-existent
    REQUIRE_FALSE(Poseidon::ProfileManager::DeleteProfile(tmp.path, "DoesNotExist"));
}

TEST_CASE("Poseidon::ProfileManager::RenameProfile", "[profile]")
{
    TempDirGuard tmp;
    Poseidon::ProfileManager::CreateProfile(tmp.path, "OldName");

    SECTION("renames directory")
    {
        REQUIRE(Poseidon::ProfileManager::RenameProfile(tmp.path, "OldName", "NewName"));
        REQUIRE_FALSE(fs::exists(tmp.path + "/Users/OldName"));
        REQUIRE(fs::exists(tmp.path + "/Users/NewName"));
        REQUIRE(fs::exists(tmp.path + "/Users/NewName/UserInfo.cfg"));
    }

    SECTION("renames UTF-8 profile directory")
    {
        REQUIRE(Poseidon::ProfileManager::RenameProfile(tmp.path, "OldName", "Řehoř"));
        REQUIRE_FALSE(fs::exists(tmp.path + "/Users/OldName"));
        REQUIRE(fs::exists(fs::path(tmp.path) / "Users" / pathFromUtf8("Řehoř")));
    }

    SECTION("fails if target exists")
    {
        Poseidon::ProfileManager::CreateProfile(tmp.path, "Existing");
        REQUIRE_FALSE(Poseidon::ProfileManager::RenameProfile(tmp.path, "OldName", "Existing"));
    }

    SECTION("fails if source doesn't exist")
    {
        REQUIRE_FALSE(Poseidon::ProfileManager::RenameProfile(tmp.path, "Ghost", "NewName"));
    }
}

TEST_CASE("Poseidon::ProfileManager::CreateDefaultProfileIfNeeded", "[profile]")
{
    TempDirGuard tmp;

    SECTION("creates profile with OS username when empty")
    {
        auto name = Poseidon::ProfileManager::CreateDefaultProfileIfNeeded(tmp.path);
        REQUIRE_FALSE(name.empty());
        REQUIRE(fs::exists(tmp.path + "/Users/" + name + "/UserInfo.cfg"));
        // Should be OS username if valid, otherwise "Player"
        const char* user = std::getenv("USER");
        if (!user)
            user = std::getenv("USERNAME");
        if (user && Poseidon::ProfileManager::IsValidProfileName(user))
            REQUIRE(name == user);
        else
            REQUIRE(name == "Player");
    }

    SECTION("does nothing when profiles exist")
    {
        Poseidon::ProfileManager::CreateProfile(tmp.path, "Existing");
        auto name = Poseidon::ProfileManager::CreateDefaultProfileIfNeeded(tmp.path);
        REQUIRE(name.empty());
        REQUIRE_FALSE(fs::exists(tmp.path + "/Users/Player"));
    }
}

TEST_CASE("ProfileManager preserves unknown fields", "[profile]")
{
    TempDirGuard tmp;
    Poseidon::ProfileManager::CreateProfile(tmp.path, "Preserve");

    std::string cfgPath = tmp.path + "/Users/Preserve/UserInfo.cfg";

    // Write some extra fields that UserConfig doesn't know about
    {
        std::ofstream ofs(cfgPath);
        ofs << "customField=\"hello\";\n";
        ofs << "diffArmor[]={0,1};\n";
        ofs << "easyMode=1;\n";
        ofs << "viewDistance=1500;\n";
        ofs << "gamma=2.0;\n";
    }

    // Load and save via UserConfig
    Poseidon::UserConfig uc;
    uc.LoadFromFile(cfgPath.c_str());
    REQUIRE(uc.cadetDifficulty[Poseidon::DTArmor] == false);
    REQUIRE(uc.easyMode == true);

    // Modify a difficulty setting
    uc.cadetDifficulty[Poseidon::DTArmor] = true;
    uc.SaveToFile(cfgPath.c_str());

    // Verify customField is preserved
    std::ifstream ifs(cfgPath);
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    REQUIRE(content.find("customField") != std::string::npos);
    REQUIRE(content.find("viewDistance") != std::string::npos);
    REQUIRE(content.find("gamma") != std::string::npos);
}

TEST_CASE("Poseidon::ProfileManager::GetProfileCfgPath", "[profile]")
{
    REQUIRE(Poseidon::ProfileManager::GetProfileCfgPath("/base", "Test") == "/base/Users/Test/UserInfo.cfg");
    REQUIRE(Poseidon::ProfileManager::GetProfileCfgPath("/base/", "Test") == "/base/Users/Test/UserInfo.cfg");
}

TEST_CASE("Poseidon::ProfileManager::GetProfileDirPath", "[profile]")
{
    std::string path = Poseidon::ProfileManager::GetProfileDirPath("/base", "Test");
    REQUIRE(path == "/base/Users/Test/");

    // Trailing separator on basePath should not double up
    std::string path2 = Poseidon::ProfileManager::GetProfileDirPath("/base/", "Test");
    REQUIRE(path2 == "/base/Users/Test/");
}

TEST_CASE("Poseidon::ProfileManager::EnumerateProfiles edge cases", "[profile]")
{
    SECTION("non-existent basePath returns empty")
    {
        auto profiles = Poseidon::ProfileManager::EnumerateProfiles("/nonexistent_path_42");
        REQUIRE(profiles.empty());
    }

    SECTION("ignores dot-prefixed directories")
    {
        TempDirGuard tmp;
        // Create a normal profile and a hidden one
        Poseidon::ProfileManager::CreateProfile(tmp.path, "Visible");
        fs::create_directories(tmp.path + "/Users/.hidden");

        auto profiles = Poseidon::ProfileManager::EnumerateProfiles(tmp.path);
        REQUIRE(profiles.size() == 1);
        REQUIRE(profiles[0].name == "Visible");
    }

    SECTION("ignores regular files in Users directory")
    {
        TempDirGuard tmp;
        Poseidon::ProfileManager::CreateProfile(tmp.path, "Real");
        // Create a stray file in Users/
        std::ofstream(tmp.path + "/Users/stray_file.txt") << "noise";

        auto profiles = Poseidon::ProfileManager::EnumerateProfiles(tmp.path);
        REQUIRE(profiles.size() == 1);
        REQUIRE(profiles[0].name == "Real");
    }
}
