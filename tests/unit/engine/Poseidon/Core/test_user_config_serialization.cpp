#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Core/Difficulty.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include "../test_fixtures.hpp"

TEST_CASE("UserConfig LoadFromFile", "[core][config][serialization]")
{
    SECTION("Missing profile keeps new-player Cadet default")
    {
        const char* tempPath = GET_TEMP_FILE("missing_user_config_defaults.cfg");
        TestFixtures::CleanupTempFile(tempPath);

        Poseidon::UserConfig config;
        config.easyMode = false;
        config.LoadFromFile(tempPath);

        REQUIRE(config.easyMode == true);
        REQUIRE(config.IsEnabled(Poseidon::DTArmor) == true);
    }

    SECTION("Load from fixture file")
    {
        const char* fixturePath = GET_FIXTURE("cfg/user_config_test.cfg");

        Poseidon::UserConfig config;
        config.LoadFromFile(fixturePath);

        REQUIRE(config.cadetDifficulty[Poseidon::DTArmor] == false);
        REQUIRE(config.cadetDifficulty[Poseidon::DTFriendlyTag] == true);
        REQUIRE(config.cadetDifficulty[Poseidon::DTEnemyTag] == false);
        REQUIRE(config.cadetDifficulty[Poseidon::DTHUD] == true);
        REQUIRE(config.cadetDifficulty[Poseidon::DTAutoSpot] == true);
        REQUIRE(config.cadetDifficulty[Poseidon::DTMap] == true);
        REQUIRE(config.cadetDifficulty[Poseidon::DTWeaponCursor] == true);
        REQUIRE(config.cadetDifficulty[Poseidon::DTAutoGuideAT] == false);
        REQUIRE(config.cadetDifficulty[Poseidon::DTClockIndicator] == true);
        REQUIRE(config.cadetDifficulty[Poseidon::DT3rdPersonView] == true);
        REQUIRE(config.cadetDifficulty[Poseidon::DTTracers] == false);
        REQUIRE(config.cadetDifficulty[Poseidon::DTUltraAI] == false);

        // Veteran column = the second value of each diff*[]={cadet,veteran} pair in the fixture.
        // All flags are loaded faithfully, including those with enabledInVeteran=false.
        REQUIRE(config.veteranDifficulty[Poseidon::DTArmor] == true);          // {0,1}
        REQUIRE(config.veteranDifficulty[Poseidon::DTFriendlyTag] == false);   // {1,0}
        REQUIRE(config.veteranDifficulty[Poseidon::DTEnemyTag] == false);      // {0,0}
        REQUIRE(config.veteranDifficulty[Poseidon::DTHUD] == true);            // {1,1}
        REQUIRE(config.veteranDifficulty[Poseidon::DTAutoSpot] == false);      // {1,0}
        REQUIRE(config.veteranDifficulty[Poseidon::DTMap] == true);            // {1,1}
        REQUIRE(config.veteranDifficulty[Poseidon::DTWeaponCursor] == true);   // {1,1}
        REQUIRE(config.veteranDifficulty[Poseidon::DTAutoGuideAT] == false);   // {0,0}
        REQUIRE(config.veteranDifficulty[Poseidon::DTClockIndicator] == true); // {1,1}
        REQUIRE(config.veteranDifficulty[Poseidon::DT3rdPersonView] == true);  // {1,1}
        REQUIRE(config.veteranDifficulty[Poseidon::DTTracers] == true);        // {0,1}
        REQUIRE(config.veteranDifficulty[Poseidon::DTUltraAI] == false);       // {0,0}

        REQUIRE(config.showTitles == false);
    }
}

TEST_CASE("UserConfig SaveToFile", "[core][config][serialization]")
{
    SECTION("Save and load round-trip")
    {
        const char* tempPath = GET_TEMP_FILE("user_config_roundtrip.cfg");

        Poseidon::UserConfig original;
        original.cadetDifficulty[Poseidon::DTArmor] = false;
        original.cadetDifficulty[Poseidon::DTMap] = true;
        original.veteranDifficulty[Poseidon::DTWeaponCursor] = false;
        original.veteranDifficulty[Poseidon::DTTracers] = false;
        original.showTitles = false;

        original.SaveToFile(tempPath);

        Poseidon::UserConfig loaded;
        loaded.LoadFromFile(tempPath);

        REQUIRE(loaded.cadetDifficulty[Poseidon::DTArmor] == false);
        REQUIRE(loaded.cadetDifficulty[Poseidon::DTMap] == true);
        REQUIRE(loaded.veteranDifficulty[Poseidon::DTWeaponCursor] == false);
        REQUIRE(loaded.veteranDifficulty[Poseidon::DTTracers] == false);
        REQUIRE(loaded.showTitles == false);

        TestFixtures::CleanupTempFile(tempPath);
    }

    SECTION("FOV round-trip")
    {
        const char* tempPath = GET_TEMP_FILE("user_config_fov.cfg");

        Poseidon::UserConfig original;
        original.SetCustomFov(1.7778f, 0.75f);

        original.SaveToFile(tempPath);

        Poseidon::UserConfig loaded;
        loaded.LoadFromFile(tempPath);

        REQUIRE(loaded.fovTop == 0.75f);
        REQUIRE(loaded.fovLeft == Catch::Approx(1.7778f));

        TestFixtures::CleanupTempFile(tempPath);
    }

    SECTION("FOV defaults preserved when not in file")
    {
        const char* tempPath = GET_TEMP_FILE("user_config_no_fov.cfg");

        Poseidon::UserConfig original;
        original.SaveToFile(tempPath);

        Poseidon::ParamFile raw;
        REQUIRE(raw.Parse(tempPath) == LSOK);
        const Poseidon::ParamEntry* version = raw.FindEntry("version");
        REQUIRE(version != nullptr);
        CHECK((int)*version == 2);
        CHECK(raw.FindEntry("fovTop") == nullptr);
        CHECK(raw.FindEntry("fovLeft") == nullptr);

        Poseidon::UserConfig loaded;
        loaded.LoadFromFile(tempPath);

        REQUIRE(loaded.fovTop == 0.75f);
        REQUIRE(loaded.fovLeft == 1.0f);

        TestFixtures::CleanupTempFile(tempPath);
    }
}

TEST_CASE("UserConfig profile integration", "[core][config][profile]")
{
    SECTION("Load fixture with FOV fields")
    {
        const char* fixturePath = GET_FIXTURE("cfg/user_config_with_fov.cfg");

        Poseidon::UserConfig config;
        config.LoadFromFile(fixturePath);

        // Difficulty unchanged from base fixture
        REQUIRE(config.cadetDifficulty[Poseidon::DTArmor] == false);
        REQUIRE(config.showTitles == false);

        // FOV loaded from file
        REQUIRE(config.fovTop == 0.75f);
        REQUIRE(config.fovLeft == Catch::Approx(1.7778f));
    }

    SECTION("FOV defaults when fixture lacks FOV keys")
    {
        const char* fixturePath = GET_FIXTURE("cfg/user_config_test.cfg");

        Poseidon::UserConfig config;
        config.LoadFromFile(fixturePath);

        // FOV should be defaults since fixture has no FOV keys
        REQUIRE(config.fovTop == 0.75f);
        REQUIRE(config.fovLeft == 1.0f);
    }

    SECTION("Full profile round-trip: difficulty + FOV + UI prefs")
    {
        const char* tempPath = GET_TEMP_FILE("profile_full_roundtrip.cfg");

        Poseidon::UserConfig original;
        original.easyMode = true;
        original.showTitles = false;
        original.cadetDifficulty[Poseidon::DTArmor] = false;
        original.cadetDifficulty[Poseidon::DTAutoSpot] = false;
        original.veteranDifficulty[Poseidon::DTWeaponCursor] = false;
        original.SetCustomFov(2.3704f, 0.75f);

        original.SaveToFile(tempPath);

        Poseidon::UserConfig loaded;
        loaded.LoadFromFile(tempPath);

        REQUIRE(loaded.easyMode == true);
        REQUIRE(loaded.showTitles == false);
        REQUIRE(loaded.cadetDifficulty[Poseidon::DTArmor] == false);
        REQUIRE(loaded.cadetDifficulty[Poseidon::DTAutoSpot] == false);
        REQUIRE(loaded.veteranDifficulty[Poseidon::DTWeaponCursor] == false);
        REQUIRE(loaded.fovTop == 0.75f);
        REQUIRE(loaded.fovLeft == Catch::Approx(2.3704f));

        TestFixtures::CleanupTempFile(tempPath);
    }

    SECTION("SaveToFile preserves existing non-managed keys")
    {
        const char* tempPath = GET_TEMP_FILE("profile_preserve_keys.cfg");

        // Pre-populate file with extra keys not managed by UserConfig
        {
            Poseidon::UserConfig initial;
            initial.SaveToFile(tempPath);
        }

        // Modify and save again
        Poseidon::UserConfig modified;
        modified.SetCustomFov(3.5556f, 0.75f);
        modified.cadetDifficulty[Poseidon::DTMap] = false;
        modified.SaveToFile(tempPath);

        // Reload and verify modifications took effect
        Poseidon::UserConfig reloaded;
        reloaded.LoadFromFile(tempPath);

        REQUIRE(reloaded.fovLeft == Catch::Approx(3.5556f));
        REQUIRE(reloaded.cadetDifficulty[Poseidon::DTMap] == false);
    }
}

TEST_CASE("UserConfig FOV migration", "[core][config][serialization]")
{
    SECTION("generated version-1 values become automatic")
    {
        const char* tempPath = GET_TEMP_FILE("user_config_fov_v1_auto.cfg");
        Poseidon::ParamFile raw;
        raw.Add("version", 1);
        raw.Add("fovLeft", 4.0f / 3.0f);
        raw.Add("fovTop", 0.75f);
        REQUIRE(raw.Save(tempPath) == LSOK);

        Poseidon::UserConfig config;
        config.LoadFromFile(tempPath);
        REQUIRE(config.MigrateFov(4.0f / 3.0f, 0.75f));
        CHECK_FALSE(config.HasCustomFov());
        config.SaveToFile(tempPath);

        REQUIRE(raw.Parse(tempPath) == LSOK);
        REQUIRE(raw.FindEntry("version") != nullptr);
        CHECK((int)*raw.FindEntry("version") == 2);
        CHECK(raw.FindEntry("fovLeft") == nullptr);
        CHECK(raw.FindEntry("fovTop") == nullptr);
        TestFixtures::CleanupTempFile(tempPath);
    }

    SECTION("custom version-1 values remain custom")
    {
        const char* tempPath = GET_TEMP_FILE("user_config_fov_v1_custom.cfg");
        Poseidon::ParamFile raw;
        raw.Add("version", 1);
        raw.Add("fovLeft", 1.5f);
        raw.Add("fovTop", 0.8f);
        REQUIRE(raw.Save(tempPath) == LSOK);

        Poseidon::UserConfig config;
        config.LoadFromFile(tempPath);
        REQUIRE(config.MigrateFov(4.0f / 3.0f, 0.75f));
        CHECK(config.HasCustomFov());
        CHECK(config.fovLeft == Catch::Approx(1.5f));
        CHECK(config.fovTop == Catch::Approx(0.8f));
        config.SaveToFile(tempPath);

        Poseidon::UserConfig reloaded;
        reloaded.LoadFromFile(tempPath);
        CHECK_FALSE(reloaded.MigrateFov(4.0f / 3.0f, 0.75f));
        CHECK(reloaded.HasCustomFov());
        CHECK(reloaded.fovLeft == Catch::Approx(1.5f));
        CHECK(reloaded.fovTop == Catch::Approx(0.8f));
        TestFixtures::CleanupTempFile(tempPath);
    }

    SECTION("invalid version-2 values become automatic")
    {
        const char* tempPath = GET_TEMP_FILE("user_config_fov_v2_invalid.cfg");
        Poseidon::ParamFile raw;
        raw.Add("version", 2);
        raw.Add("fovLeft", 1.5f);
        REQUIRE(raw.Save(tempPath) == LSOK);

        Poseidon::UserConfig config;
        config.LoadFromFile(tempPath);
        REQUIRE(config.MigrateFov(4.0f / 3.0f, 0.75f));
        CHECK_FALSE(config.HasCustomFov());
        config.SaveToFile(tempPath);

        REQUIRE(raw.Parse(tempPath) == LSOK);
        CHECK(raw.FindEntry("fovLeft") == nullptr);
        CHECK(raw.FindEntry("fovTop") == nullptr);
        TestFixtures::CleanupTempFile(tempPath);
    }
}

// Gameplay difficulty settings must survive a save->load round-trip in BOTH modes.
// SaveToParamFile writes cadet AND veteran for every flag and the difficulty UI lets either mode
// be edited, but LoadFromParamFile used to restore the veteran value only when enabledInVeteran —
// so the 8 flags with enabledInVeteran=false (Armor, FriendlyTag, EnemyTag, HUD, AutoSpot, Map,
// AutoGuideAT, UltraAI) silently reverted to default on reload. The existing round-trip sections
// only ever asserted veteran flags with enabledInVeteran=true (WeaponCursor/Tracers), missing this.
TEST_CASE("UserConfig round-trips every difficulty flag in both modes", "[core][config][serialization]")
{
    const char* tempPath = GET_TEMP_FILE("user_config_all_flags.cfg");

    Poseidon::UserConfig original;
    for (int i = 0; i < Poseidon::DTN; ++i)
    {
        original.cadetDifficulty[i] = true;
        original.veteranDifficulty[i] = true;
    }
    original.SaveToFile(tempPath);

    Poseidon::UserConfig loaded;
    loaded.LoadFromFile(tempPath);

    for (int i = 0; i < Poseidon::DTN; ++i)
    {
        INFO("difficulty flag index " << i);
        REQUIRE(loaded.cadetDifficulty[i] == true);
        // Broken-state delta: false for every enabledInVeteran=false flag.
        REQUIRE(loaded.veteranDifficulty[i] == true);
    }

    TestFixtures::CleanupTempFile(tempPath);
}

TEST_CASE("UserConfig persists enemy info enabled in veteran mode", "[core][config][serialization]")
{
    // TonyHawk's exact report: enable "enemy info" (EnemyTag, enabledInVeteran=false) in veteran
    // mode, restart, it reverts to default-off. EnemyTag default is false in both modes, so a
    // dropped veteran value reads back as disabled.
    const char* tempPath = GET_TEMP_FILE("user_config_enemy_info.cfg");

    Poseidon::UserConfig original;
    original.veteranDifficulty[Poseidon::DTEnemyTag] = true;
    original.SaveToFile(tempPath);

    Poseidon::UserConfig loaded;
    loaded.LoadFromFile(tempPath);

    REQUIRE(loaded.veteranDifficulty[Poseidon::DTEnemyTag] == true);

    TestFixtures::CleanupTempFile(tempPath);
}
