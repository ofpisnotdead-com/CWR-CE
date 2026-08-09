#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Core/ModInstall.hpp>

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <random>
#include <set>
#include <string>
#include <system_error>

using namespace Poseidon;

namespace
{
std::filesystem::path MakeTempDir()
{
    std::random_device rd;
    auto dir = std::filesystem::temp_directory_path() /
               ("cwr_modinstall_" + std::to_string(rd()) + "_" + std::to_string(rd()));
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

void WriteInstalledMod(const std::filesystem::path& modsRoot, const std::string& modId, const std::string& version)
{
    const auto dir = modsRoot / ("@" + modId);
    std::filesystem::create_directories(dir);
    std::ofstream out(dir / "mod.json", std::ios::binary);
    out << "{\"modId\":\"" << modId << "\",\"name\":\"X\",\"version\":\"" << version << "\"}";
}

// Create <root>/<folder> carrying the given marker subdirectories (e.g. "addons").
std::filesystem::path MakeModFolder(const std::filesystem::path& root, const std::string& folder,
                                    std::initializer_list<const char*> markers)
{
    const auto dir = root / folder;
    std::filesystem::create_directories(dir);
    for (const char* m : markers)
        std::filesystem::create_directories(dir / m);
    return dir;
}

std::set<std::string> ScannedIds(const std::vector<ScannedMod>& mods)
{
    std::set<std::string> ids;
    for (const auto& m : mods)
        ids.insert(m.modId);
    return ids;
}
} // namespace

TEST_CASE("GetModInstallStatus reflects presence and version", "[mods][install]")
{
    const auto root = MakeTempDir();
    const std::string rootStr = root.string();
    WriteInstalledMod(root, "ecp", "1.085");

    CHECK(GetModInstallStatus(rootStr, "ecp", "1.085") == ModInstallStatus::Installed);
    CHECK(GetModInstallStatus(rootStr, "ecp", "1.1") == ModInstallStatus::UpdateAvailable);
    CHECK(GetModInstallStatus(rootStr, "ffur", "2.1") == ModInstallStatus::NotInstalled);

    CHECK(ReadInstalledModVersion(rootStr, "ecp") == "1.085");
    CHECK(ReadInstalledModVersion(rootStr, "ffur").empty());

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("GetModInstallStatus compares package revisions before author versions", "[mods][install]")
{
    const auto root = MakeTempDir();
    const auto dir = root / "@stable";
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "mod.json", std::ios::binary)
        << R"({"modId":"stable","version":"1.0","packageRevision":2,"sha256":"abcd"})";

    CHECK(GetModInstallStatus(root.string(), "stable", "1.0", 2, "abcd") == ModInstallStatus::Installed);
    CHECK(GetModInstallStatus(root.string(), "stable", "1.0", 3, "ffff") == ModInstallStatus::UpdateAvailable);
    CHECK(GetModInstallStatus(root.string(), "stable", "9.9", 1, "ffff") == ModInstallStatus::InstalledAhead);
    CHECK(ReadInstalledPackageRevision(root.string(), "stable") == 2);
    CHECK(ReadInstalledArtifactHash(root.string(), "stable") == "abcd");

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("downloaded mod artifacts require the advertised size and SHA-256", "[mods][install][hash]")
{
    const auto root = MakeTempDir();
    const auto artifact = root / "package.zst";
    std::ofstream(artifact, std::ios::binary) << "abc";
    const std::string digest = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    std::string error;
    CHECK(VerifyModArtifact(artifact.string(), 3, digest, &error));
    CHECK_FALSE(VerifyModArtifact(artifact.string(), 4, digest, &error));
    CHECK_FALSE(VerifyModArtifact(artifact.string(), 3, std::string(64, '0'), &error));

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("staged mod installs replace stale files and roll back as one transaction", "[mods][install][transaction]")
{
    const auto root = MakeTempDir();
    const auto first = root / "@first";
    const auto second = root / "@second";
    std::filesystem::create_directories(first);
    std::filesystem::create_directories(second);
    std::ofstream(first / "old.txt") << "first-old";
    std::ofstream(first / "stale.txt") << "stale";
    std::ofstream(second / "old.txt") << "second-old";

    std::vector<StagedModInstall> installs = {MakeStagedModInstall(first.string()),
                                              MakeStagedModInstall(second.string())};
    std::filesystem::create_directories(installs[0].stagingDir);
    std::filesystem::create_directories(installs[1].stagingDir);
    std::ofstream(std::filesystem::path(installs[0].stagingDir) / "new.txt") << "first-new";
    std::ofstream(std::filesystem::path(installs[1].stagingDir) / "new.txt") << "second-new";

    REQUIRE(SwapStagedModInstalls(installs));
    CHECK(std::filesystem::exists(first / "new.txt"));
    CHECK_FALSE(std::filesystem::exists(first / "stale.txt"));
    RestoreStagedModInstalls(installs);
    CHECK(std::filesystem::exists(first / "old.txt"));
    CHECK(std::filesystem::exists(first / "stale.txt"));
    CHECK(std::filesystem::exists(second / "old.txt"));
    CHECK_FALSE(std::filesystem::exists(second / "new.txt"));

    DiscardStagedModInstalls(installs);
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("a failed staged swap restores every earlier destination", "[mods][install][transaction]")
{
    const auto root = MakeTempDir();
    const auto first = root / "@first";
    const auto second = root / "@second";
    std::filesystem::create_directories(first);
    std::filesystem::create_directories(second);
    std::ofstream(first / "marker.txt") << "first-old";
    std::ofstream(second / "marker.txt") << "second-old";

    std::vector<StagedModInstall> installs = {MakeStagedModInstall(first.string()),
                                              MakeStagedModInstall(second.string())};
    std::filesystem::create_directories(installs[0].stagingDir);
    std::ofstream(std::filesystem::path(installs[0].stagingDir) / "new.txt") << "new";

    std::string error;
    CHECK_FALSE(SwapStagedModInstalls(installs, &error));
    CHECK_FALSE(error.empty());
    CHECK(std::filesystem::exists(first / "marker.txt"));
    CHECK(std::filesystem::exists(second / "marker.txt"));
    CHECK_FALSE(std::filesystem::exists(first / "new.txt"));

    DiscardStagedModInstalls(installs);
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("ModInstallDir composes the @modId path", "[mods][install]")
{
    CHECK(ModInstallDir("/x/mods", "csla") == "/x/mods/@csla");
    CHECK(ModInstallDir("/x/mods", "carwars-2.5", "carwars2.5") == "/x/mods/carwars2.5");
}

TEST_CASE("install status resolves catalog modId from a different folderName", "[mods][install]")
{
    const auto root = MakeTempDir();
    const auto dir = root / "carwars2.5";
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "mod.json", std::ios::binary)
        << R"({"modId":"carwars-2.5","name":"CarWars","version":"2.5","folderName":"carwars2.5"})";

    CHECK(FindInstalledModDir(root.string(), "carwars-2.5") == dir.string());
    CHECK(ReadInstalledModVersion(root.string(), "carwars-2.5") == "2.5");
    CHECK(GetModInstallStatus(root.string(), "carwars-2.5", "2.5") == ModInstallStatus::Installed);

    const auto mods = ScanLocalMods(root.string());
    REQUIRE(mods.size() == 1);
    CHECK(mods[0].modId == "carwars-2.5");
    CHECK(mods[0].folderName == "carwars2.5");

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("GetModInstallStatus treats a versionless install as Installed", "[mods][install]")
{
    const auto root = MakeTempDir();
    const auto dir = root / "@bare";
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "mod.json", std::ios::binary) << "{\"modId\":\"bare\"}"; // no version field

    CHECK(GetModInstallStatus(root.string(), "bare", "9.9") == ModInstallStatus::Installed);

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

// Regression: a mod whose folder has no '@' prefix (e.g. CSLA copied straight in)
// must still be detected. Before the fix ScanLocalMods skipped every folder whose
// name didn't start with '@', so such a mod never appeared in the MODS list and
// could not be enabled — the user's CSLA symptom.
TEST_CASE("ScanLocalMods detects a mod folder with no '@' prefix", "[mods][scan]")
{
    const auto root = MakeTempDir();
    MakeModFolder(root, "CSLA", {"addons"});        // bare name — the bug: never listed
    MakeModFolder(root, "@fixturemod", {"addons"}); // classic '@' name still works

    const auto mods = ScanLocalMods(root.string());
    REQUIRE(mods.size() == 2);
    // Sorted by display name: "CSLA" < "fixturemod".
    CHECK(mods[0].modId == "CSLA"); // id is the folder name verbatim (the mount path uses it)
    CHECK(mods[0].folderName == "CSLA");
    CHECK(mods[0].name == "CSLA"); // no '@' to trim
    CHECK(mods[1].modId == "@fixturemod");
    CHECK(mods[1].folderName == "@fixturemod");
    CHECK(mods[1].name == "fixturemod"); // '@' trimmed for display only

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

// A folder is a mod by virtue of its CONTENT, not its name: any of addons/dta/
// bin/Campaigns (case-insensitive) or a mod.json marks it; a folder with none is
// ignored so stray directories don't pollute the list.
TEST_CASE("ScanLocalMods recognizes mods by content, not name", "[mods][scan]")
{
    const auto root = MakeTempDir();
    MakeModFolder(root, "WithAddons", {"addons"});
    MakeModFolder(root, "WithBin", {"bin"});
    MakeModFolder(root, "WithDta", {"dta"});
    MakeModFolder(root, "WithCampaigns", {"Campaigns"});
    MakeModFolder(root, "CapsMarker", {"AddOns"}); // marker matched case-insensitively
    {
        const auto d = root / "ManifestOnly";
        std::filesystem::create_directories(d);
        std::ofstream(d / "mod.json", std::ios::binary) << "{\"name\":\"Manifest Mod\"}";
    }
    {
        const auto d = root / "NotAMod"; // no mod marker — must be ignored
        std::filesystem::create_directories(d);
        std::ofstream(d / "readme.txt", std::ios::binary) << "hi";
    }

    const auto mods = ScanLocalMods(root.string());
    const auto ids = ScannedIds(mods);
    CHECK(ids.count("WithAddons") == 1);
    CHECK(ids.count("WithBin") == 1);
    CHECK(ids.count("WithDta") == 1);
    CHECK(ids.count("WithCampaigns") == 1);
    CHECK(ids.count("CapsMarker") == 1);
    CHECK(ids.count("ManifestOnly") == 1);
    CHECK(ids.count("NotAMod") == 0);
    CHECK(mods.size() == 6);

    // mod.json "name" wins over the folder name.
    for (const auto& m : mods)
        if (m.modId == "ManifestOnly")
            CHECK(m.name == "Manifest Mod");

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}
