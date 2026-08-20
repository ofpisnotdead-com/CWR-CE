#include <catch2/catch_test_macros.hpp>

#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/IO/ParamFileExt.hpp>
#include <Poseidon/UI/Locale/LanguageRegistry.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>

#include "test_fixtures.hpp"

#include <filesystem>

namespace Poseidon
{
bool ParseConfig(RStringB dir, void* context);
bool ParseResource(RStringB dir, void* context);
bool IsConfigOverriddenByMod();
bool IsMenuOverriddenByMod();
void MergeBaseConfigExtra();
} // namespace Poseidon

using Poseidon::ParamEntry;

namespace
{
// Replay the enumeration for one mod: mod before base, stop at the first true. Returns true when
// the mod's bin/config won (the base is then never loaded).
bool LoadConfigModThenBase(const char* modDir)
{
    if (Poseidon::ParseConfig(modDir, nullptr))
        return true;
    Poseidon::ParseConfig("", nullptr);
    return false;
}

bool LoadResourceModThenBase(const char* modDir)
{
    if (Poseidon::ParseResource(modDir, nullptr))
        return true;
    Poseidon::ParseResource("", nullptr);
    return false;
}

struct CwdGuard
{
    std::filesystem::path prev;
    explicit CwdGuard(const std::filesystem::path& to) : prev(std::filesystem::current_path())
    {
        std::filesystem::current_path(to);
    }
    ~CwdGuard() { std::filesystem::current_path(prev); }
};

// Resolve the fixture root via a known file inside it (GetTestFixturePath validates a regular
// file), then step up out of bin/.
std::filesystem::path FixtureRoot()
{
    return std::filesystem::path(TestFixtures::GetTestFixturePath("config-replace/bin/config.cpp"))
        .parent_path()
        .parent_path();
}

const ParamEntry* Child(const ParamEntry* parent, const char* name)
{
    return parent ? parent->FindEntry(name) : nullptr;
}
} // namespace

// A total-conversion mod's bin/config is the complete master config. config-extra
// (CfgLanguages, marker "Tundra") restores on top.
TEST_CASE("a config-replacement mod's bin/config replaces the base, then config-extra is restored",
          "[config][mods][replace]")
{
    // ParseConfig("") resolves bin/config.cpp relative to the cwd; the fixture keeps the base
    // config + config-extra in bin/ and the mod config in mod/bin/, so run from the fixture root.
    CwdGuard cwd(FixtureRoot());

    const bool modWon = LoadConfigModThenBase("mod");
    // Keep checking the loaded tree if the enumeration result is wrong.
    CHECK(modWon);

    const ParamEntry* vehicles = Pars.FindEntry("CfgVehicles");
    REQUIRE(vehicles != nullptr);
    CHECK(vehicles->FindEntry("ModTank") != nullptr);
    CHECK(vehicles->FindEntry("VanillaJeep") == nullptr);

    const ParamEntry* infantry = Child(Child(Pars.FindEntry("CfgGroups"), "West"), "Infantry");
    REQUIRE(infantry != nullptr);
    CHECK(infantry->FindEntry("GrpModOnly") != nullptr);
    CHECK(infantry->FindEntry("GrpShared") != nullptr);
    CHECK(infantry->FindEntry("GrpVanilla") == nullptr);

    // The replaced config carries no CfgLanguages; the mod override flag is set.
    CHECK(Poseidon::IsConfigOverriddenByMod());
    CHECK(Pars.FindEntry("CfgLanguages") == nullptr);

    // Restore the base config-extra on top, exactly as Configuration.cpp does after the enum.
    Poseidon::MergeBaseConfigExtra();
    REQUIRE(Pars.FindEntry("CfgLanguages") != nullptr);

    auto& registry = CfgLib::LanguageRegistry::Instance();
    registry.ResetToDefaults();
    if (const ParamEntry* cfgLangs = Pars.FindEntry("CfgLanguages"))
        registry.LoadFromConfig(*cfgLangs);
    // The marker language proves config-extra reached the registry (not just the 8 defaults).
    CHECK(registry.Find("Tundra") != nullptr);
    registry.ResetToDefaults();
}

TEST_CASE("with no config-replacement mod the base loads and applies its own config-extra", "[config][mods][replace]")
{
    CwdGuard cwd(FixtureRoot());

    REQUIRE(Poseidon::ParseConfig("", nullptr));
    CHECK_FALSE(Poseidon::IsConfigOverriddenByMod());

    const ParamEntry* vehicles = Pars.FindEntry("CfgVehicles");
    REQUIRE(vehicles != nullptr);
    CHECK(vehicles->FindEntry("VanillaJeep") != nullptr);

    const ParamEntry* infantry = Child(Child(Pars.FindEntry("CfgGroups"), "West"), "Infantry");
    REQUIRE(infantry != nullptr);
    CHECK(infantry->FindEntry("GrpVanilla") != nullptr);

    // config-extra.cpp in the same bin/ is applied during ParseConfig, so the base config carries
    // CfgLanguages even though config.cpp itself does not.
    REQUIRE(Pars.FindEntry("CfgLanguages") != nullptr);
    auto& registry = CfgLib::LanguageRegistry::Instance();
    registry.ResetToDefaults();
    if (const ParamEntry* cfgLangs = Pars.FindEntry("CfgLanguages"))
        registry.LoadFromConfig(*cfgLangs);
    CHECK(registry.Find("Tundra") != nullptr);
    registry.ResetToDefaults();
}

TEST_CASE("a malformed mod resource falls back to the base resource", "[config][mods][replace]")
{
    CwdGuard cwd(FixtureRoot());

    CHECK_FALSE(LoadResourceModThenBase("mod"));
    CHECK_FALSE(Poseidon::IsMenuOverriddenByMod());
    CHECK(Res.FindEntry("RscDisplayLoadMission") != nullptr);
}
