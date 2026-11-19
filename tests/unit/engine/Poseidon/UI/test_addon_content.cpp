#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Core/ModCollection.hpp>
#include <Poseidon/Core/ModSystem.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Game/Mission/MissionTemplateCatalog.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/Network/NetworkServerCommon.hpp>
#include <Poseidon/UI/DisplayUI.hpp>
#include <Poseidon/UI/OptionsUICommon.hpp>
#include "../Support/test_fixtures.hpp"

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

namespace
{
// A temp game root holding loose island intros in every layout the community packs ship, under the base
// root and under two enabled mods whose folder names differ from the ones the configs were written for.
// Each mission.sqm holds its own location, so a read shows which copy the file banks served.
struct LooseIntroFixture
{
    fs::path root;
    fs::path packMod;
    fs::path renamedMod;
    fs::path prevCwd;

    void WriteIntro(const std::string& introDir)
    {
        fs::create_directories(root / introDir);
        std::ofstream(root / introDir / "mission.sqm") << introDir;
    }

    LooseIntroFixture()
    {
        std::random_device rd;
        root = fs::temp_directory_path() / ("cwr_loose_intro_" + std::to_string(rd()) + "_" + std::to_string(rd()));
        packMod = root / "Mods" / "@TER_IslandPack";
        renamedMod = root / "Mods" / "@renamed";

        WriteIntro("Mods/@TER_IslandPack/Anims/intro.tri");
        WriteIntro("Mods/@TER_IslandPack/AddOns/Tri_Anim/Intro.TRI");
        WriteIntro("Mods/@TER_IslandPack/AddOns/CJE_Anims/Intro1.tri");
        WriteIntro("Mods/@TER_IslandPack/AddOns/saruanims/intro.tri");
        WriteIntro("Mods/@TER_IslandPack/Anims/RAZIsland_intro/intro.tri");
        WriteIntro("Mods/@TER_IslandPack/Dta/HV_anim/intro.tri");
        WriteIntro("Mods/@renamed/DTA/anims/SLX_intro.tri");
        WriteIntro("Mods/@renamed/addons/PMC_anims/intro1.tri");
        WriteIntro("Mods/@renamed/Island_Anims/HC1_Caen_anim/intro.tri");
        WriteIntro("Mods/@renamed/DTA/anims/llx.tri");
        WriteIntro("Mods/@renamed/AddOns/Shared_Anim/intro.tri");
        WriteIntro("addons/Base_Anim/intro.tri");
        WriteIntro("Res/addons/Res_Anim/intro.tri");
        WriteIntro("addons/Shared_Anim/intro.tri");
        WriteIntro("Anims/rootonly.tri");

        prevCwd = fs::current_path();
        fs::current_path(root);
        const std::string mods = packMod.string() + ";" + renamedMod.string();
        ModSystem::SetModPath(mods.c_str());
    }

    ~LooseIntroFixture()
    {
        ModSystem::SetModPath("");
        std::error_code ec;
        fs::current_path(prevCwd);
        fs::remove_all(root, ec);
    }
};

std::string ReadThroughBanks(const std::string& path)
{
    if (!QIFStreamB::FileExist(path.c_str()))
        return {};
    QIFStreamB in;
    in.AutoOpen(path.c_str());
    std::string out(static_cast<size_t>(in.rest()), '\0');
    in.read(out.data(), static_cast<int>(out.size()));
    return out;
}

// The mission.sqm StartRandomCutscene opens for a cutscenes[] entry, read through the file banks.
std::string ReadIntro(const char* world, const char* configName)
{
    const CutsceneLocation location =
        ResolveCutsceneLocation(RString(world), RootAnchoredCutsceneName(RString(configName)));
    return ReadThroughBanks(std::string((const char*)location.subdir) + (const char*)location.name + "." + world +
                            "/mission.sqm");
}
} // namespace

TEST_CASE("a bare island intro name resolves to a mod's Anims folder", "[ui][cutscene][anims]")
{
    LooseIntroFixture fixture;

    CHECK(ReadIntro("tri", "intro") == "Mods/@TER_IslandPack/Anims/intro.tri");
    CHECK(ReadIntro("TRI", "Intro") == "Mods/@TER_IslandPack/Anims/intro.tri");
}

TEST_CASE("an addons-relative island intro resolves to a loose folder in a mod or the game root",
          "[ui][cutscene][anims]")
{
    LooseIntroFixture fixture;

    CHECK(ReadIntro("tri", "..\\ADDONS\\tri_anim\\intro") == "Mods/@TER_IslandPack/AddOns/Tri_Anim/Intro.TRI");
    CHECK(ReadIntro("tri", "..\\addons\\Base_Anim\\intro") == "addons/Base_Anim/intro.tri");
    CHECK(ReadIntro("tri", "..\\ADDONS\\saruanims\\\\intro") == "Mods/@TER_IslandPack/AddOns/saruanims/intro.tri");
    CHECK(ReadIntro("tri", "..\\Dta\\HV_anim\\intro") == "Mods/@TER_IslandPack/Dta/HV_anim/intro.tri");
}

TEST_CASE("a Res-relative island intro resolves below every enabled mod", "[ui][cutscene][anims]")
{
    LooseIntroFixture fixture;

    CHECK(ReadIntro("tri", "..\\Res\\addons\\CJE_Anims\\intro1") == "Mods/@TER_IslandPack/AddOns/CJE_Anims/Intro1.tri");
    CHECK(ReadIntro("tri", "..\\RES\\Addons\\Res_Anim\\intro") == "Res/addons/Res_Anim/intro.tri");
}

TEST_CASE("an island intro naming another mod folder resolves inside a renamed mod", "[ui][cutscene][anims]")
{
    LooseIntroFixture fixture;

    CHECK(ReadIntro("tri", "..\\@SLX\\DTA\\anims\\SLX_intro") == "Mods/@renamed/DTA/anims/SLX_intro.tri");
    CHECK(ReadIntro("tri", "..\\x_pmc\\addons\\PMC_anims\\intro1") == "Mods/@renamed/addons/PMC_anims/intro1.tri");
    CHECK(ReadIntro("tri", "..\\HC1_Europe_Island_Pack\\Island_Anims\\HC1_Caen_anim\\intro") ==
          "Mods/@renamed/Island_Anims/HC1_Caen_anim/intro.tri");
}

TEST_CASE("a root-anchored or anims-relative island intro resolves inside a mod", "[ui][cutscene][anims]")
{
    LooseIntroFixture fixture;

    CHECK(ReadIntro("tri", "\\@LLX\\DTA\\anims\\llx") == "Mods/@renamed/DTA/anims/llx.tri");
    CHECK(ReadIntro("tri", "RAZIsland_intro\\INTRO") == "Mods/@TER_IslandPack/Anims/RAZIsland_intro/intro.tri");
}

TEST_CASE("the game root's loose island intro wins over an enabled mod's", "[ui][cutscene][anims]")
{
    LooseIntroFixture fixture;

    CHECK(ReadIntro("tri", "..\\addons\\Shared_Anim\\intro") == "addons/Shared_Anim/intro.tri");
}

TEST_CASE("an island intro packed in a bank resolves through anims", "[ui][cutscene][anims][pbo]")
{
    REQUIRE_FIXTURE("pbo/mission_fixture.Intro.pbo");
    const std::string pbo =
        fs::absolute(TestFixtures::GetTestFixturePath("pbo/mission_fixture.Intro.pbo")).replace_extension().string();
    LooseIntroFixture fixture;

    const bool origUseBanks = GUseFileBanks;
    GUseFileBanks = true;
    const int origSize = GFileBanks.Size();
    for (const char* prefix : {"triisl\\intro.tri\\", "bin_map1\\intro.tri\\", "anims\\intro1.tri\\"})
    {
        QFBank& bank = GFileBanks.Set(GFileBanks.Add());
        bank.SetPrefix(prefix);
        REQUIRE(bank.open(pbo.c_str(), nullptr, nullptr));
        bank.Lock();
        REQUIRE(bank.FileExists("mission.sqm"));
    }
    const std::string packed = ReadThroughBanks("triisl\\intro.tri\\mission.sqm");
    REQUIRE_FALSE(packed.empty());

    CHECK(ReadIntro("tri", "..\\addons\\triisl\\intro") == packed);
    CHECK(ReadIntro("tri", "..\\Binmod\\addons\\BIN_Map1\\Intro") == packed);
    CHECK(ReadIntro("tri", "..\\@Minustah\\DTA\\Anims\\Intro1") == packed);
    CHECK(ReadIntro("tri", "rootonly") == "Anims/rootonly.tri");

    while (GFileBanks.Size() > origSize)
        GFileBanks.Delete(GFileBanks.Size() - 1);
    GUseFileBanks = origUseBanks;
}

TEST_CASE("an island intro found nowhere stays unresolved", "[ui][cutscene][anims]")
{
    LooseIntroFixture fixture;

    CHECK(ReadIntro("noe", "..\\Res\\addons\\Gone\\intro").empty());
    CHECK(ReadIntro("noe", "intro").empty());
}
