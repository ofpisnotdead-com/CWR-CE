#include <catch2/catch_test_macros.hpp>

#include <Poseidon/UI/Settings/GameSettingsConfig.hpp>
#include <Poseidon/IO/Filesystem/Utf8Paths.hpp>

#include <filesystem>
#include <random>
#include <string>
#include <optional>

using Poseidon::ResolveEffectiveViewDistance;

using Poseidon::GameSettingsConfig;

namespace
{
std::string TmpPath(const char* leaf)
{
    static std::random_device rd;
    static std::mt19937 rng(rd());
    std::uniform_int_distribution<unsigned> dist;
    auto root = std::filesystem::temp_directory_path() / ("gamesettings_test_" + std::to_string(dist(rng)));
    std::filesystem::create_directories(root);
    return (root / leaf).string();
}

struct FakeEnvironment : GameSettingsConfig::Environment
{
    std::string language;
    std::string DetectSystemLanguage() const override { return language; }
};
} // namespace

TEST_CASE("GameSettingsConfig: defaults follow detected language", "[Settings][GameSettings]")
{
    FakeEnvironment env;
    env.language = "Czech";
    GameSettingsConfig cfg;
    cfg.LoadDefaults(env);

    CHECK(cfg.textLanguage == "Czech");
    CHECK(cfg.voiceLanguage == "Czech");
    CHECK(cfg.blood == true);
    CHECK(cfg.preferredViewDistance == 900.0f);
    CHECK(cfg.respectMissionViewDistance == true);
}

TEST_CASE("GameSettingsConfig: defaults normalize unsupported detected language", "[Settings][GameSettings]")
{
    FakeEnvironment env;
    env.language = "Korean";
    GameSettingsConfig cfg;
    cfg.LoadDefaults(env);

    CHECK(cfg.textLanguage == "English");
    CHECK(cfg.voiceLanguage == "English");
    CHECK(cfg.blood == true);
}

TEST_CASE("GameSettingsConfig: first run creates file from detected language", "[Settings][GameSettings]")
{
    const std::string path = TmpPath("first-run.cfg");
    std::filesystem::remove(path);

    FakeEnvironment env;
    env.language = "Polish";

    GameSettingsConfig cfg;
    bool created = false;
    REQUIRE(EnsureGameSettingsFile(cfg, path, env, &created));

    CHECK(created);
    CHECK(cfg.textLanguage == "Polish");
    CHECK(cfg.voiceLanguage == "Polish");
    CHECK(cfg.blood == true);

    GameSettingsConfig roundTrip;
    REQUIRE(roundTrip.Load(path));
    CHECK(roundTrip.textLanguage == "Polish");
    CHECK(roundTrip.voiceLanguage == "Polish");
    CHECK(roundTrip.blood == true);
}

TEST_CASE("GameSettingsConfig: normalize fixes unsupported languages", "[Settings][GameSettings]")
{
    FakeEnvironment env;
    env.language = "French";
    GameSettingsConfig cfg;
    cfg.textLanguage = "Esperanto";
    cfg.voiceLanguage = "Klingon";
    cfg.blood = false;

    CHECK(cfg.Normalize(env));
    CHECK(cfg.textLanguage == "French");
    CHECK(cfg.voiceLanguage == "French");
    CHECK(cfg.blood == false);
}

TEST_CASE("GameSettingsConfig: normalize clamps preferred view distance", "[Settings][GameSettings]")
{
    FakeEnvironment env;
    env.language = "English";
    GameSettingsConfig cfg;
    cfg.preferredViewDistance = 50.0f;

    CHECK(cfg.Normalize(env));
    CHECK(cfg.preferredViewDistance == GameSettingsConfig::kMinViewDistance);

    cfg.preferredViewDistance = 6000.0f;
    CHECK(cfg.Normalize(env));
    CHECK(cfg.preferredViewDistance == GameSettingsConfig::kMaxViewDistance);
}

TEST_CASE("GameSettingsConfig: save and load round-trip every field", "[Settings][GameSettings]")
{
    const std::string path = TmpPath("roundtrip.cfg");
    std::filesystem::remove(path);

    GameSettingsConfig src;
    src.textLanguage = "German";
    src.voiceLanguage = "Russian";
    src.blood = false;
    src.preferredViewDistance = 1700.0f;
    src.respectMissionViewDistance = false;
    REQUIRE(src.Save(path));

    GameSettingsConfig dst;
    REQUIRE(dst.Load(path));
    CHECK(dst.textLanguage == src.textLanguage);
    CHECK(dst.voiceLanguage == src.voiceLanguage);
    CHECK(dst.blood == src.blood);
    CHECK(dst.preferredViewDistance == src.preferredViewDistance);
    CHECK(dst.respectMissionViewDistance == src.respectMissionViewDistance);
}

TEST_CASE("GameSettingsConfig: saves under a UTF-8 user directory", "[Settings][GameSettings][utf8]")
{
    const std::string root = Poseidon::FilesystemPathToUtf8(std::filesystem::temp_directory_path()) +
                             "/gamesettings_\xE6\xB5\x8B\xE8\xAF\x95";
    const std::string path = root + "/game.cfg";

    GameSettingsConfig src;
    src.textLanguage = "Chinese";
    REQUIRE(src.Save(path));

    GameSettingsConfig dst;
    REQUIRE(dst.Load(path));
    CHECK(dst.textLanguage == "Chinese");

    std::filesystem::remove_all(Poseidon::FilesystemPathFromUtf8(root));
}

TEST_CASE("GameSettingsConfig: missing file leaves instance untouched", "[Settings][GameSettings]")
{
    const std::string path = TmpPath("missing.cfg");
    std::filesystem::remove(path);

    GameSettingsConfig cfg;
    cfg.textLanguage = "Polish";
    CHECK_FALSE(cfg.Load(path));
    CHECK(cfg.textLanguage == "Polish");
}

TEST_CASE("GameSettingsConfig: effective view distance respects mission policy", "[Settings][GameSettings]")
{
    CHECK(ResolveEffectiveViewDistance(1500.0f, false, 600.0f) == 1500.0f);
    CHECK(ResolveEffectiveViewDistance(1500.0f, true, 600.0f) == 600.0f);
    CHECK(ResolveEffectiveViewDistance(900.0f, true, std::nullopt) == 900.0f);
}
