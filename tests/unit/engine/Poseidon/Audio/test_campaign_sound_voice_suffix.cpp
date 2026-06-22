#include <catch2/catch_test_macros.hpp>

#include <Poseidon/IO/ParamFileExt.hpp>
#include <Poseidon/UI/OptionsUI.hpp>
#include <Poseidon/UI/Settings/GameSettingsConfig.hpp>

#include <filesystem>
#include <string>

using namespace Poseidon;

namespace Poseidon
{
void SetBaseDirectory(RString dir);
}

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
