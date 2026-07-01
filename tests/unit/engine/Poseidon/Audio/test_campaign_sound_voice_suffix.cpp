#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Audio/DynSound.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/IO/ParamFileExt.hpp>
#include <Poseidon/UI/OptionsUI.hpp>
#include <Poseidon/UI/Settings/GameSettingsConfig.hpp>

#include <cstring>
#include <filesystem>
#include <string>

using namespace Poseidon;

namespace Poseidon
{
void SetBaseDirectory(RString dir);
void SetMission(RString world, RString mission, RString subdir);
} // namespace Poseidon

namespace
{
struct ScopedVoiceLanguage
{
    std::string previous;

    explicit ScopedVoiceLanguage(const char* lang) : previous(GetSelectedVoiceLanguage())
    {
        SetSelectedVoiceLanguage(lang);
    }

    ~ScopedVoiceLanguage() { SetSelectedVoiceLanguage(previous.c_str()); }
};

struct ScopedBaseDirectory
{
    explicit ScopedBaseDirectory(const char* dir) { SetBaseDirectory(RString(dir)); }

    ~ScopedBaseDirectory() { SetBaseDirectory(RString("")); }
};
} // namespace

TEST_CASE("FindSound resolves campaign CfgSounds through existing voice-language override",
          "[Audio][voice-lang][campaign]")
{
    namespace fs = std::filesystem;

    const fs::path campaignDir = fs::path(TESTS_ROOT_DIR) / "fixtures" / "audio" / "voice-lang-campaign";
    REQUIRE(fs::exists(campaignDir / "description.ext"));
    REQUIRE(fs::exists(campaignDir / "dtaExt" / "sound" / "test_campaign.ogg"));
    REQUIRE(fs::exists(campaignDir / "dtaExt" / "sound" / "test_campaign.Czech.ogg"));

    const std::string baseDirectory = campaignDir.string() + "/";
    ScopedBaseDirectory campaign(baseDirectory.c_str());

    ScopedVoiceLanguage scoped("Czech");

    SoundPars pars;
    const ParamEntry* entry = FindSound(RString("test_campaign"), pars);
    REQUIRE(entry != nullptr);

    const std::string resolved = (const char*)pars.name;
    CHECK(resolved.find("dtaExt") != std::string::npos);
    CHECK(resolved.find("sound") != std::string::npos);
    CHECK(resolved.find("test_campaign.Czech.ogg") != std::string::npos);
}

TEST_CASE("FindSFX resolves campaign CfgSFX through existing voice-language override",
          "[Audio][voice-lang][campaign][sfx]")
{
    namespace fs = std::filesystem;

    const fs::path campaignDir = fs::path(TESTS_ROOT_DIR) / "fixtures" / "audio" / "voice-lang-campaign";
    REQUIRE(fs::exists(campaignDir / "description.ext"));
    REQUIRE(fs::exists(campaignDir / "dtaExt" / "sound" / "test_campaign.ogg"));
    REQUIRE(fs::exists(campaignDir / "dtaExt" / "sound" / "test_campaign.Czech.ogg"));

    const std::string baseDirectory = campaignDir.string() + "/";
    ScopedBaseDirectory campaign(baseDirectory.c_str());

    ScopedVoiceLanguage scoped("Czech");

    SoundEntry emptySound;
    Foundation::AutoArray<SoundEntry> sounds;
    FindSFX(RString("radio_sfx"), emptySound, sounds);

    REQUIRE(sounds.Size() == 1);
    const std::string resolved = (const char*)sounds[0].name;
    CHECK(resolved.find("dtaExt") != std::string::npos);
    CHECK(resolved.find("sound") != std::string::npos);
    CHECK(resolved.find("test_campaign.Czech.ogg") != std::string::npos);
}

TEST_CASE("FindSFX resolves mission CfgSFX through existing voice-language override",
          "[Audio][voice-lang][mission][sfx]")
{
    namespace fs = std::filesystem;

    const fs::path missionsParent = fs::path(TESTS_ROOT_DIR) / "fixtures" / "audio" / "voice-lang-mission";
    const fs::path missionDir = missionsParent / "voice_test.demo";

    REQUIRE(fs::exists(missionDir / "description.ext"));
    REQUIRE(fs::exists(missionDir / "sound" / "test_voice.ogg"));
    REQUIRE(fs::exists(missionDir / "sound" / "test_voice.Czech.ogg"));

    SetBaseDirectory(RString(""));
    std::strncpy(Glob.header.filename, "voice_test", sizeof(Glob.header.filename) - 1);
    std::strncpy(Glob.header.worldname, "demo", sizeof(Glob.header.worldname) - 1);
    Glob.header.filename[sizeof(Glob.header.filename) - 1] = '\0';
    Glob.header.worldname[sizeof(Glob.header.worldname) - 1] = '\0';
    Glob.header.filenameReal = "voice_test";

    const std::string subdir = missionsParent.string() + "/";
    SetMission(RString("demo"), RString("voice_test"), RString(subdir.c_str()));

    ScopedVoiceLanguage scoped("Czech");

    SoundEntry emptySound;
    Foundation::AutoArray<SoundEntry> sounds;
    FindSFX(RString("radio_sfx"), emptySound, sounds);

    REQUIRE(sounds.Size() == 1);
    const std::string resolved = (const char*)sounds[0].name;
    CHECK(resolved.find("voice_test.demo") != std::string::npos);
    CHECK(resolved.find("sound") != std::string::npos);
    CHECK(resolved.find("test_voice.Czech.ogg") != std::string::npos);
}
