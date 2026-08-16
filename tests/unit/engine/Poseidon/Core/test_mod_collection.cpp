#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Core/ModCollection.hpp>
#include <Poseidon/Core/ModSystem.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

using namespace Poseidon;

namespace
{
std::filesystem::path MakeTempDir()
{
    std::random_device rd;
    auto dir =
        std::filesystem::temp_directory_path() / ("cwr_modcol_" + std::to_string(rd()) + "_" + std::to_string(rd()));
    std::filesystem::create_directories(dir);
    return dir;
}

void MakeModFolder(const std::filesystem::path& root, const std::string& folder, const char* marker)
{
    std::filesystem::create_directories(root / folder / marker);
}

Mod MakeMod(const std::string& id, const std::string& path, ModSource src = ModSource::Local)
{
    Mod m;
    m.id = id;
    m.name = id;
    m.path = path;
    m.source = src;
    return m;
}
} // namespace

TEST_CASE("LooksLikeMod recognizes mod markers, ignores bare folders", "[mods][collection]")
{
    const auto root = MakeTempDir();
    MakeModFolder(root, "withAddons", "addons");
    MakeModFolder(root, "withBin", "Bin"); // markers are case-insensitive
    MakeModFolder(root, "withCampaigns", "Campaigns");
    // Content-only packs: a mod may host player content without any addon/engine dir.
    MakeModFolder(root, "withMissions", "Missions");
    MakeModFolder(root, "withMPMissions", "MPMissions");
    MakeModFolder(root, "withTemplates", "Templates");
    MakeModFolder(root, "withSPTemplates", "SPTemplates");
    MakeModFolder(root, "withAnims", "anims");
    MakeModFolder(root, "withFonts", "Fonts"); // a font-only pack is a valid mod on its own
    std::filesystem::create_directories(root / "manifest");
    std::ofstream(root / "manifest" / "mod.json", std::ios::binary) << "{}";
    std::filesystem::create_directories(root / "empty");
    std::filesystem::create_directories(root / "junk");
    std::ofstream(root / "junk" / "note.txt", std::ios::binary) << "x";

    CHECK(LooksLikeMod((root / "withAddons").string()));
    CHECK(LooksLikeMod((root / "withBin").string()));
    CHECK(LooksLikeMod((root / "withCampaigns").string()));
    CHECK(LooksLikeMod((root / "withMissions").string()));
    CHECK(LooksLikeMod((root / "withMPMissions").string()));
    CHECK(LooksLikeMod((root / "withTemplates").string()));
    CHECK(LooksLikeMod((root / "withSPTemplates").string()));
    CHECK(LooksLikeMod((root / "withAnims").string()));
    CHECK(LooksLikeMod((root / "withFonts").string()));
    CHECK(LooksLikeMod((root / "manifest").string()));
    CHECK_FALSE(LooksLikeMod((root / "empty").string())); // no marker
    CHECK_FALSE(LooksLikeMod((root / "junk").string()));  // stray file is not a marker

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("ScanModsRoot tags path + source and sorts by name", "[mods][collection]")
{
    const auto root = MakeTempDir();
    MakeModFolder(root, "Zulu", "addons");
    MakeModFolder(root, "Alpha", "addons");

    const auto mods = ScanModsRoot(root.string(), ModSource::Workshop);
    REQUIRE(mods.size() == 2);
    CHECK(mods[0].id == "Alpha"); // sorted by display name
    CHECK(mods[1].id == "Zulu");
    for (const auto& m : mods)
    {
        CHECK(m.source == ModSource::Workshop);
        CHECK(m.path == (root / m.id).string()); // absolute folder path captured
    }

    // A non-directory root yields nothing rather than throwing.
    CHECK(ScanModsRoot((root / "does-not-exist").string(), ModSource::Local).empty());

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("ModCollection find is case-insensitive; MountPath joins in order", "[mods][collection]")
{
    ModCollection col;
    CHECK(col.Empty());
    col.Add(MakeMod("CSLA", "/mods/CSLA"));
    col.Add(MakeMod("@fixturemod", "/mods/@fixturemod"));
    CHECK(col.Size() == 2);
    CHECK_FALSE(col.Empty());

    CHECK(col.Contains("csla")); // case-insensitive
    REQUIRE(col.Find("CSLA") != nullptr);
    CHECK(col.Find("csla")->path == "/mods/CSLA");
    CHECK(col.Find("missing") == nullptr);

    // Order follows the enabled list (the load order), not insertion order;
    // ids not in the collection are skipped.
    CHECK(col.MountPath({"@fixturemod", "CSLA"}) == "/mods/@fixturemod;/mods/CSLA");
    CHECK(col.MountPath({"CSLA", "nope"}) == "/mods/CSLA");
    CHECK(col.MountPath({}).empty());
}

TEST_CASE("ModCollection formats mount paths and folder names separately", "[mods][collection]")
{
    ModCollection col;
    CHECK(col.FormatMountPath().empty());
    CHECK(col.FormatNames().empty());

    col.Add(MakeMod("@CSLA", "C:\\Games\\@CSLA"));
    col.Add(MakeMod("@mymod", "C:\\Games\\@mymod"));

    // FormatNames is the mod IDENTITY (verbatim @folder, the VFS prefix) — what a server
    // advertises + the browser shows. FormatMountPath is the local disk path — what the
    // addon loader mounts. The whole point of the split: one crosses the wire, one doesn't.
    CHECK(col.FormatNames() == "@CSLA;@mymod");
    CHECK(col.FormatMountPath() == "C:\\Games\\@CSLA;C:\\Games\\@mymod");
}

TEST_CASE("ModCollection advertises PapaBear catalog id when installed metadata provides one", "[mods][collection]")
{
    ModCollection col;
    Mod m = MakeMod("carwars2.5", "/mods/carwars2.5");
    m.catalogId = "carwars-2.5";
    col.Add(m);

    REQUIRE(col.Find("carwars-2.5") != nullptr);
    CHECK(col.Find("carwars-2.5")->path == "/mods/carwars2.5");
    CHECK(col.MountPath({"carwars-2.5"}) == "/mods/carwars2.5");
    CHECK(col.MountPathForIds({ModId("carwars-2.5")}) == "/mods/carwars2.5");
    CHECK(col.FormatNames() == "carwars-2.5");
}

TEST_CASE("ActiveModsFromMountPath parses a mount-path string into (name, path) mods", "[mods][collection]")
{
    // Windows-style paths: names are the basenames, paths round-trip unchanged.
    {
        const ModCollection col = ActiveModsFromMountPath("C:\\Games\\@CSLA;D:\\m\\@mymod");
        REQUIRE(col.Size() == 2);
        CHECK(col.FormatNames() == "@CSLA;@mymod");
        CHECK(col.FormatMountPath() == "C:\\Games\\@CSLA;D:\\m\\@mymod");
        CHECK(col.All()[0].id == "@CSLA");
        CHECK(col.All()[0].path == "C:\\Games\\@CSLA");
    }
    // POSIX-style paths split on '/' too (the basename helper handles both separators).
    {
        const ModCollection col = ActiveModsFromMountPath("/home/u/.local/Mods/@CSLA;/tmp/@x");
        REQUIRE(col.Size() == 2);
        CHECK(col.FormatNames() == "@CSLA;@x");
    }
    // A bare name (no directory), a trailing separator, and an empty entry between ';;'.
    {
        const ModCollection col = ActiveModsFromMountPath("@bare;/games/@trailing/;;/p/@last");
        REQUIRE(col.Size() == 3); // the empty entry is skipped
        CHECK(col.FormatNames() == "@bare;@trailing;@last");
    }
    CHECK(ActiveModsFromMountPath("").Empty()); // base game -> empty set
}

TEST_CASE("ActiveModsFromMountPath reads PapaBear catalog id from installed mod metadata", "[mods][collection]")
{
    const auto root = MakeTempDir();
    MakeModFolder(root, "carwars2.5", "addons");
    std::ofstream(root / "carwars2.5" / "mod.json", std::ios::binary)
        << R"({"modId":"carwars-2.5","name":"CarWars","version":"2.5"})";

    const ModCollection col = ActiveModsFromMountPath((root / "carwars2.5").string());
    REQUIRE(col.Size() == 1);
    CHECK(col.All()[0].id == "carwars2.5");
    CHECK(col.All()[0].catalogId == "carwars-2.5");
    CHECK(col.FormatNames() == "carwars-2.5");

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("Mod name is the folder name verbatim - '@' neither added nor stripped", "[mods][collection]")
{
    // The advertised/identity name is 1:1 with the on-disk folder name: a folder WITHOUT
    // '@' stays without it (CSLA -> CSLA, never @CSLA), and a folder WITH '@' keeps it.
    // The '@' is an OFP folder-naming convention, not required and never synthesized.
    const ModCollection col = ActiveModsFromMountPath("C:\\g\\CSLA;C:\\g\\@mymod;/p/Dta");
    REQUIRE(col.Size() == 3);
    CHECK(col.All()[0].id == "CSLA");   // not "@CSLA"
    CHECK(col.All()[1].id == "@mymod"); // '@' kept when the folder has it
    CHECK(col.All()[2].id == "Dta");
    CHECK(col.FormatNames() == "CSLA;@mymod;Dta");
}

TEST_CASE("ModSystem exposes the active set as mount paths vs advertised names", "[mods][modsystem]")
{
    ModSystem::SetModPath("C:\\Games\\@CSLA;C:\\Games\\@mymod");
    // GetModList stays the local mount PATHS (loader / re-mount); GetModNames is the
    // folder NAMES (network advertise + browser) — no drive letter crosses the wire.
    CHECK(std::string((const char*)ModSystem::GetModList()) == "C:\\Games\\@CSLA;C:\\Games\\@mymod");
    CHECK(std::string((const char*)ModSystem::GetModNames()) == "@CSLA;@mymod");

    ModSystem::SetModPath(""); // base game -> both empty
    CHECK(std::string((const char*)ModSystem::GetModList()).empty());
    CHECK(std::string((const char*)ModSystem::GetModNames()).empty());
}

TEST_CASE("ModLoader dedupes by id across roots, first root wins", "[mods][collection]")
{
    const auto local = MakeTempDir();
    const auto workshop = MakeTempDir();
    MakeModFolder(local, "Shared", "addons");
    MakeModFolder(local, "LocalOnly", "addons");
    MakeModFolder(workshop, "Shared", "addons"); // same id present in both roots
    MakeModFolder(workshop, "RemoteOnly", "addons");

    ModLoader loader;
    loader.AddRoot(local.string(), ModSource::Local);
    loader.AddRoot(workshop.string(), ModSource::Workshop);
    const ModCollection col = loader.Load();

    CHECK(col.Size() == 3); // "Shared" listed once
    REQUIRE(col.Find("Shared") != nullptr);
    CHECK(col.Find("Shared")->source == ModSource::Local); // first root (local) wins
    CHECK(col.Find("LocalOnly")->source == ModSource::Local);
    CHECK(col.Find("RemoteOnly")->source == ModSource::Workshop);

    std::error_code ec;
    std::filesystem::remove_all(local, ec);
    std::filesystem::remove_all(workshop, ec);
}

TEST_CASE("ResolveModPathList falls back from current directory to managed Mods root", "[mods][resolve]")
{
    const auto current = MakeTempDir();
    const auto managed = MakeTempDir();
    std::filesystem::create_directories(current / "CSLA"); // stray folder, not a mod
    MakeModFolder(managed, "CSLA", "addons");

    ModPathResolveRequest request;
    request.modPaths = "CSLA";
    request.currentDir = current.string();
    request.managedModsDir = managed.string();

    const ModPathResolveResult result = ResolveModPathList(request);
    REQUIRE(result.Ok());
    CHECK(result.mountPath == (managed / "CSLA").string());

    std::error_code ec;
    std::filesystem::remove_all(current, ec);
    std::filesystem::remove_all(managed, ec);
}

TEST_CASE("ResolveModPathList checks game directory between current directory and managed Mods root", "[mods][resolve]")
{
    const auto current = MakeTempDir();
    const auto game = MakeTempDir();
    const auto managed = MakeTempDir();
    std::filesystem::create_directories(current / "CSLA"); // stray folder, not a mod
    MakeModFolder(game, "CSLA", "addons");
    MakeModFolder(managed, "CSLA", "bin");

    ModPathResolveRequest request;
    request.modPaths = "CSLA";
    request.currentDir = current.string();
    request.gameDir = game.string();
    request.managedModsDir = managed.string();

    const ModPathResolveResult result = ResolveModPathList(request);
    REQUIRE(result.Ok());
    CHECK(result.mountPath == (game / "CSLA").string());

    std::error_code ec;
    std::filesystem::remove_all(current, ec);
    std::filesystem::remove_all(game, ec);
    std::filesystem::remove_all(managed, ec);
}

TEST_CASE("ResolveModPathList prefers current directory when it contains a valid mod", "[mods][resolve]")
{
    const auto current = MakeTempDir();
    const auto game = MakeTempDir();
    const auto managed = MakeTempDir();
    MakeModFolder(current, "CSLA", "bin");
    MakeModFolder(game, "CSLA", "dta");
    MakeModFolder(managed, "CSLA", "addons");

    ModPathResolveRequest request;
    request.modPaths = "CSLA";
    request.currentDir = current.string();
    request.gameDir = game.string();
    request.managedModsDir = managed.string();

    const ModPathResolveResult result = ResolveModPathList(request);
    REQUIRE(result.Ok());
    CHECK(result.mountPath == (current / "CSLA").string());

    std::error_code ec;
    std::filesystem::remove_all(current, ec);
    std::filesystem::remove_all(game, ec);
    std::filesystem::remove_all(managed, ec);
}

TEST_CASE("ResolveModPathList uses explicit mods-dir and does not fall back elsewhere", "[mods][resolve]")
{
    const auto current = MakeTempDir();
    const auto game = MakeTempDir();
    const auto explicitRoot = MakeTempDir();
    const auto managed = MakeTempDir();
    MakeModFolder(current, "CSLA", "addons");
    MakeModFolder(game, "CSLA", "dta");
    MakeModFolder(explicitRoot, "CSLA", "campaigns");
    MakeModFolder(managed, "CSLA", "bin");

    ModPathResolveRequest request;
    request.modPaths = "CSLA";
    request.explicitModsDir = explicitRoot.string();
    request.currentDir = current.string();
    request.gameDir = game.string();
    request.managedModsDir = managed.string();

    const ModPathResolveResult result = ResolveModPathList(request);
    REQUIRE(result.Ok());
    CHECK(result.mountPath == (explicitRoot / "CSLA").string());

    std::error_code ec;
    std::filesystem::remove_all(current, ec);
    std::filesystem::remove_all(game, ec);
    std::filesystem::remove_all(explicitRoot, ec);
    std::filesystem::remove_all(managed, ec);
}

TEST_CASE("ResolveModPathList resolves PapaBear catalog id to installed folder name", "[mods][resolve]")
{
    const auto explicitRoot = MakeTempDir();
    MakeModFolder(explicitRoot, "carwars2.5", "addons");
    std::ofstream(explicitRoot / "carwars2.5" / "mod.json", std::ios::binary)
        << R"({"modId":"carwars-2.5","name":"CarWars","version":"2.5","folderName":"carwars2.5"})";

    ModPathResolveRequest request;
    request.modPaths = "carwars-2.5";
    request.explicitModsDir = explicitRoot.string();

    const ModPathResolveResult result = ResolveModPathList(request);
    REQUIRE(result.Ok());
    CHECK(result.mountPath == (explicitRoot / "carwars2.5").string());

    std::error_code ec;
    std::filesystem::remove_all(explicitRoot, ec);
}

TEST_CASE("ResolveModPathList reports missing or invalid mod folders", "[mods][resolve]")
{
    const auto current = MakeTempDir();
    const auto managed = MakeTempDir();
    std::filesystem::create_directories(managed / "CSLA"); // invalid: no mod markers

    ModPathResolveRequest request;
    request.modPaths = "CSLA";
    request.currentDir = current.string();
    request.gameDir = (current / "game").string();
    request.managedModsDir = managed.string();

    const ModPathResolveResult result = ResolveModPathList(request);
    CHECK_FALSE(result.Ok());
    CHECK(result.mountPath.empty());
    REQUIRE(result.errors.size() == 1);
    CHECK(result.errors[0].find("CSLA") != std::string::npos);
    CHECK(result.errors[0].find((current / "game" / "CSLA").string()) != std::string::npos);

    std::error_code ec;
    std::filesystem::remove_all(current, ec);
    std::filesystem::remove_all(managed, ec);
}
