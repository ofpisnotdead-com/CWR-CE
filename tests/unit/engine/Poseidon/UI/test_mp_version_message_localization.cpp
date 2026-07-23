#include <catch2/catch_test_macros.hpp>

#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>

#include <filesystem>
#include <string>

using namespace Poseidon;

namespace
{
std::string PackageStringtablePath(const char* package)
{
    return (std::filesystem::path(TESTS_ROOT_DIR).parent_path() / "packages" / package / "BIN" / "STRINGTABLE.CSV")
        .string();
}

void CheckMpVersionMessage(const char* package)
{
    Poseidon::ClearStringtable();
    GLanguage = "German";
    Poseidon::LoadStringtable("global", PackageStringtablePath(package).c_str(), 0, true);

    const int versionMessageId = Poseidon::RegisterString("STR_MSG_MP_VERSION");
    REQUIRE(versionMessageId >= 0);
    REQUIRE(std::string(Poseidon::LocalizeString(versionMessageId).Data()) ==
            "Fehlerhafte Version. Der Server hat die Verbindung abgelehnt.");

    REQUIRE(Poseidon::SetLanguage("Czech"));

    const std::string czechByName = Poseidon::LocalizeString("STR_MSG_MP_VERSION").Data();
    const std::string czechById = Poseidon::LocalizeString(versionMessageId).Data();

    REQUIRE(czechByName == "Špatná verze, server odmítl připojení.");
    REQUIRE(czechById == czechByName);
}

std::string FixturePath(const char* name)
{
    return (std::filesystem::path(TESTS_ROOT_DIR) / "fixtures" / "stringtable" / name).string();
}
} // namespace

TEST_CASE("Legacy MP version row keeps columns aligned after an unquoted French comma", "[ui][mp][localization]")
{
    Poseidon::ClearStringtable();
    GLanguage = "German";
    Poseidon::LoadStringtable("global", FixturePath("stringtable_mp_version_malformed.csv").c_str(), 0, true);

    const int versionMessageId = Poseidon::RegisterString("STR_MSG_MP_VERSION");
    REQUIRE(versionMessageId >= 0);
    REQUIRE(std::string(Poseidon::LocalizeString(versionMessageId).Data()) == "German version");

    REQUIRE(Poseidon::SetLanguage("Czech"));
    REQUIRE(std::string(Poseidon::LocalizeString(versionMessageId).Data()) == "Czech version");
}

TEST_CASE("MP version rejection message follows Czech language selection", "[ui][mp][localization][GameData]")
{
    SECTION("Game")
    {
        CheckMpVersionMessage("Game");
    }
    SECTION("Demo")
    {
        CheckMpVersionMessage("Demo");
    }
}
