#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Core/ModCollection.hpp>
#include <Poseidon/Core/ModSystem.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Game/Mission/MissionTemplateCatalog.hpp>
#include <Poseidon/Network/NetworkServerCommon.hpp>
#include <Poseidon/UI/DisplayUI.hpp>
#include <Poseidon/UI/OptionsUICommon.hpp>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace Poseidon;
namespace fs = std::filesystem;

namespace
{
std::string SlashLower(const std::string& in)
{
    std::string out = in;
    for (char& c : out)
    {
        if (c == '\\')
            c = '/';
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool AnyContains(const std::vector<std::string>& dirs, const std::string& needle)
{
    const std::string want = SlashLower(needle);
    for (const std::string& d : dirs)
    {
        if (SlashLower(d).find(want) != std::string::npos)
            return true;
    }
    return false;
}

std::vector<std::string> ToStrings(const std::vector<RString>& dirs)
{
    std::vector<std::string> out;
    out.reserve(dirs.size());
    for (const RString& d : dirs)
        out.emplace_back((const char*)d);
    return out;
}

bool ArcadeHas(const char* world, const char* name)
{
    AutoArray<RString> names = CollectArcadeWorldMissions(world);
    for (int i = 0; i < names.Size(); ++i)
    {
        if (names[i] == RString(name))
            return true;
    }
    return false;
}

bool TemplateListHas(bool multiplayer, const char* world, const char* name)
{
    AutoArray<MissionTemplateEntry> templates;
    ListMissionTemplates(templates, multiplayer, world);
    for (int i = 0; i < templates.Size(); ++i)
    {
        if (templates[i].name == RString(name))
            return true;
    }
    return false;
}

// A temp game root with one @triContent mod carrying every hostable part (the mod-composition guide's
// worked example). Enters it (cwd for base-relative scans, active mod path for mod scans), restores on
// teardown.
struct ContentModFixture
{
    fs::path root;
    fs::path modDir;
    fs::path prevCwd;

    ContentModFixture()
    {
        std::random_device rd;
        root = fs::temp_directory_path() / ("cwr_addon_content_" + std::to_string(rd()) + "_" + std::to_string(rd()));
        modDir = root / "@triContent";

        fs::create_directories(modDir / "AddOns");
        std::ofstream(modDir / "AddOns" / "trikit_core.pbo").put('x');
        std::ofstream(modDir / "mod.json") << R"({"modId":"trikit","name":"Tri Kit","version":"1.0"})";
        fs::create_directories(modDir / "Campaigns" / "TriCampaign");
        std::ofstream(modDir / "Campaigns" / "TriCampaign" / "description.ext").put('x');
        fs::create_directories(modDir / "Anims" / "intro.tri");
        std::ofstream(modDir / "Anims" / "intro.tri" / "mission.sqm").put('x');
        fs::create_directories(root / "Missions" / "base_arc.tri");
        fs::create_directories(modDir / "Missions" / "mod_arc.tri");
        fs::create_directories(modDir / "Missions" / "CSLA");
        std::ofstream(modDir / "Missions" / "CSLA" / "mod_spsub.tri.pbo").put('x');
        fs::create_directories(modDir / "MPMissions" / "CSLA");
        std::ofstream(modDir / "MPMissions" / "mod_mp.tri.pbo").put('x');
        std::ofstream(modDir / "MPMissions" / "CSLA" / "mod_mpsub.tri.pbo").put('x');
        fs::create_directories(modDir / "Templates");
        std::ofstream(modDir / "Templates" / "mod_tpl.tri.pbo").put('x');
        fs::create_directories(modDir / "SPTemplates");
        std::ofstream(modDir / "SPTemplates" / "mod_sptpl.tri.pbo").put('x');

        prevCwd = fs::current_path();
        fs::current_path(root);
        ModSystem::SetModPath(modDir.string().c_str());
    }

    ~ContentModFixture()
    {
        ModSystem::SetModPath("");
        std::error_code ec;
        fs::current_path(prevCwd);
        fs::remove_all(root, ec);
    }
};
} // namespace

TEST_CASE("a content mod carrying only player content is recognized", "[core][mod]")
{
    ContentModFixture mod;

    // Every hostable root (AddOns/Campaigns/Missions/MPMissions/Templates/SPTemplates/Anims + mod.json)
    // marks the folder as a mod; a bare content pack needs no addon or engine dir.
    CHECK(LooksLikeMod(mod.modDir.string()));
}

TEST_CASE("arcade Custom Game tree lists a mod's single mission alongside the base game", "[ui][missions][arcade]")
{
    ContentModFixture mod;

    // Both the mod and base unpacked missions surface for the world.
    CHECK(ArcadeHas("tri", "mod_arc"));
    CHECK(ArcadeHas("tri", "base_arc"));
}

TEST_CASE("SP mission browser scans a mod's Missions and its subfolders", "[ui][missions][sp]")
{
    ContentModFixture mod;

    // Browsing the mission root and a category subfolder both resolve the mod's Missions/ tree, so a
    // mod can host single missions flat or under Missions/<Category>/.
    CHECK(AnyContains(ToStrings(GetSPMissionLookupDirs(RString(""))), "@tricontent/missions"));
    CHECK(AnyContains(ToStrings(GetSPMissionLookupDirs(RString("CSLA\\"))), "@tricontent/missions/csla"));
    CHECK(AnyContains({(const char*)ResolveSPMissionSubdir("", "mod_arc.tri")}, "@tricontent/missions"));
    CHECK(ResolveSPMissionSubdir("", "base_arc.tri") == RString("Missions\\"));
}

TEST_CASE("a content mod surfaces MP missions and both template kinds", "[ui][missions][templates][network]")
{
    ContentModFixture mod;

    const std::vector<std::string> mp = GetMPMissionLookupDirectories();
    CHECK(AnyContains(mp, "@tricontent/mpmissions"));
    CHECK(AnyContains(mp, "@tricontent/mpmissions/csla"));

    CHECK(TemplateListHas(true, "tri", "mod_tpl"));
    CHECK(TemplateListHas(false, "tri", "mod_sptpl"));
}

TEST_CASE("a mod's Anims folder hosts a bare-name island intro", "[ui][cutscene][anims]")
{
    ContentModFixture mod;

    // A bare cutscenes[] name resolves to the mod's Anims/<name>.<world>/.
    const std::string hosted = (const char*)ResolveCutsceneAnimsSubdir(RString("tri"), RString("intro"));
    CHECK(hosted.find("@triContent") != std::string::npos);

    // Windows-authored configs differ in case from the on-disk folder, so the loose resolution must fold
    // case (world "TRI"/name "Intro" against the on-disk intro.tri).
    const std::string folded = (const char*)ResolveCutsceneAnimsSubdir(RString("TRI"), RString("Intro"));
    CHECK(folded.find("@triContent") != std::string::npos);

    // The "..\addons\.." packed form and an unknown intro both fall back to the base anims/ bank.
    const std::string packed =
        (const char*)ResolveCutsceneAnimsSubdir(RString("tri"), RString("..\\addons\\triisl\\intro"));
    const std::string missing = (const char*)ResolveCutsceneAnimsSubdir(RString("noe"), RString("intro"));
    CHECK(packed.find("@triContent") == std::string::npos);
    CHECK(missing.find("@triContent") == std::string::npos);
}
