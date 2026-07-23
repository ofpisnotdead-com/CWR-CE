
#include <Poseidon/UI/Locale/MissionHtmlLocalization.hpp>
#include <Poseidon/UI/Locale/MissionLanguageDetector.hpp>
#include <Poseidon/UI/Locale/SupportedLanguages.hpp>
#include <Poseidon/Game/Mission/MissionTemplateCatalog.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <Poseidon/Foundation/Strings/RString.hpp>

using namespace Poseidon;

using Poseidon::FormatLocalizedBriefingTitle;
using Poseidon::LoadLocalizedMissionBriefingName;
using Poseidon::LocalizeString;

using Poseidon::FindLocalizedMissionHtmlFile;
using Poseidon::GetLanguage;
using Poseidon::GetMissionStringtableFileForHtml;
using Poseidon::ListMissionTemplates;
using Poseidon::LoadLocalizedMissionHtmlUtf8;
using Poseidon::LoadStringtable;
using Poseidon::MissionTemplateEntry;
using Poseidon::ResolveMissionTemplateDisplayName;
using Poseidon::SetLanguage;

namespace
{

std::filesystem::path UiLocaleFixtureRoot()
{
    return std::filesystem::path(TESTS_ROOT_DIR) / "unit" / "engine" / "Poseidon" / "UI" / "Locale" / "fixtures";
}

const MissionLanguageDetector::MissionLanguageFlags& FlagsFor(const MissionLanguageDetector::MissionLanguageInfo& info,
                                                              const char* language)
{
    const int idx = CfgLib::FindSupportedLanguageIndex(language);
    REQUIRE(idx >= 0);
    return info.flags[idx];
}

std::string FixtureMissionPath()
{
    return (UiLocaleFixtureRoot() / "mission_language_detector" / "CoverageSample.Noe").string();
}

std::string DemoSoundSubdirFixtureMissionPath()
{
    return (UiLocaleFixtureRoot() / "mission_language_detector" / "DemoSoundSubdir.Noe").string();
}

std::string HtmlStringtableFixtureMissionPath()
{
    return (UiLocaleFixtureRoot() / "mission_language_detector" / "HtmlStringtable.Noe").string();
}

std::string HtmlSelectionFixtureMissionPath()
{
    return (UiLocaleFixtureRoot() / "mission_language_detector" / "HtmlSelection.Noe").string();
}

std::string HtmlLegacyOnlyFixtureMissionPath()
{
    return (UiLocaleFixtureRoot() / "mission_language_detector" / "HtmlLegacyOnly.Noe").string();
}

void LoadMainMenuStringtable()
{
    static bool loaded = false;
    if (loaded)
    {
        SetLanguage("English");
        return;
    }

    const std::filesystem::path csv =
        std::filesystem::path(TESTS_ROOT_DIR) / "fixtures" / "stringtable" / "STRINGTABLE_MAINMENU.utf8.csv";
    LoadStringtable("global", csv.string().c_str(), 0.0f, true);
    SetLanguage("English");
    loaded = true;
}

void LoadFixtureMissionStringtable(const std::filesystem::path& root)
{
    LoadMainMenuStringtable();
    LoadStringtable("mission", (root / "stringtable.csv").string().c_str(), 2.0f, true);
}

void LoadRemasterStringtable()
{
    static bool loaded = false;
    if (loaded)
    {
        SetLanguage("English");
        return;
    }

    const std::filesystem::path csv =
        std::filesystem::path(TESTS_ROOT_DIR) / "fixtures" / "stringtable" / "STRINGTABLE_BRIEFING_TITLE.utf8.csv";
    LoadStringtable("global", csv.string().c_str(), 0.0f, true);
    SetLanguage("English");
    loaded = true;
}

} // namespace

TEST_CASE("mission language detector reports per-language asset coverage", "[ui][missions][languages]")
{
    const auto info = MissionLanguageDetector::Detect(FixtureMissionPath().c_str());

    const auto& english = FlagsFor(info, "English");
    REQUIRE(english.stringtable);
    REQUIRE(english.briefing);
    REQUIRE_FALSE(english.overview);
    REQUIRE(english.voice);

    const auto& czech = FlagsFor(info, "Czech");
    REQUIRE(czech.stringtable);
    REQUIRE(czech.briefing);
    REQUIRE(czech.overview);
    REQUIRE(czech.voice);

    const auto& polish = FlagsFor(info, "Polish");
    REQUIRE_FALSE(polish.stringtable);
    REQUIRE(polish.briefing);
    REQUIRE_FALSE(polish.overview);
    REQUIRE(polish.voice);

    const auto& russian = FlagsFor(info, "Russian");
    REQUIRE(russian.stringtable);
    REQUIRE_FALSE(russian.briefing);
    REQUIRE(russian.overview);
    REQUIRE_FALSE(russian.voice);

    const auto& german = FlagsFor(info, "German");
    REQUIRE_FALSE(german.Any());

    REQUIRE(MissionLanguageDetector::FormatTextLanguages(info) == RString("EN CZ PL RU"));
    REQUIRE(MissionLanguageDetector::FormatVoiceLanguages(info) == RString("EN CZ PL"));
}

TEST_CASE("mission preview detects authored view distance", "[ui][missions][languages]")
{
    const auto preview = MissionLanguageDetector::DetectPreview(FixtureMissionPath().c_str());

    REQUIRE(preview.missionViewDistance.has_value());
    CHECK(*preview.missionViewDistance == 600.0f);
    CHECK(MissionLanguageDetector::FormatViewDistance(preview) == RString("600 m"));
}

TEST_CASE("mission language detector finds voice in stock sound subdir layout", "[ui][missions][languages]")
{
    const auto info = MissionLanguageDetector::Detect(DemoSoundSubdirFixtureMissionPath().c_str());

    CHECK(MissionLanguageDetector::FormatVoiceLanguages(info) == RString("EN CZ"));
    CHECK(FlagsFor(info, "English").voice);
    CHECK(FlagsFor(info, "Czech").voice);
}

TEST_CASE("mission language detector finds differently cased loose sound files", "[ui][missions][languages]")
{
    const auto root = std::filesystem::temp_directory_path() /
                      ("cwr-mission-language-case-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".Noe");
    std::filesystem::create_directories(root / "sound");
    std::ofstream(root / "description.ext") << "class CfgSounds { class Line { sound[] = {\"RADIO.ogg\", 1, 1}; }; };";
    std::ofstream(root / "sound" / "radio.ogg") << "dummy";
    std::ofstream(root / "sound" / "radio.Czech.ogg") << "dummy";

    const auto info = MissionLanguageDetector::Detect(root.string().c_str());
    std::filesystem::remove_all(root);

    CHECK(FlagsFor(info, "English").voice);
    CHECK(FlagsFor(info, "Czech").voice);
}

// The TXT/VO/VD localization-status block is a debug overlay;
// players saw raw "$STR_"-style availability strings when it was always shown.
// It must be hidden by default and only emitted when the dev-panel toggle is on.
TEST_CASE("mission preview html hides localization block by default", "[ui][missions][languages]")
{
    LoadMainMenuStringtable();
    MissionLanguageDetector::SetShowLocalizationDebugInfo(false);

    const auto preview = MissionLanguageDetector::DetectPreview(FixtureMissionPath().c_str());
    const RString overview =
        "<html><body><p align=\"center\"><img src=\"preview.paa\"></p><p>Overview body</p></body></html>";
    const RString html = MissionLanguageDetector::BuildPreviewHtml(preview, overview);
    const std::string text = (const char*)html;

    // Plain overview returned verbatim — none of the debug labels leak through.
    CHECK(text.find("<b>TXT</b>") == std::string::npos);
    CHECK(text.find("<b>VO</b>") == std::string::npos);
    CHECK(text.find("<b>VD</b>") == std::string::npos);
    CHECK(text == (const char*)overview);
}

TEST_CASE("mission preview html renders metadata block when debug toggle on", "[ui][missions][languages]")
{
    LoadMainMenuStringtable();
    MissionLanguageDetector::SetShowLocalizationDebugInfo(true);

    const auto preview = MissionLanguageDetector::DetectPreview(FixtureMissionPath().c_str());
    const RString html = MissionLanguageDetector::BuildPreviewHtml(
        preview, "<html><body><p align=\"center\"><img src=\"preview.paa\"></p><p>Overview body</p></body></html>");
    const std::string text = (const char*)html;

    MissionLanguageDetector::SetShowLocalizationDebugInfo(false);

    CHECK(text.find("<b>TXT</b>") != std::string::npos);
    CHECK(text.find("<b>VO</b>") != std::string::npos);
    CHECK(text.find("<b>VD</b>") != std::string::npos);
    CHECK(text.find("EN CZ PL RU") != std::string::npos);
    CHECK(text.find("EN CZ PL") != std::string::npos);
    CHECK(text.find("600 m") != std::string::npos);
    CHECK(text.find("<table>") != std::string::npos);
    CHECK(text.find("width=\"36\"") != std::string::npos);
    CHECK(text.find("<nobr>") != std::string::npos);
    CHECK(text.find("Overview body") != std::string::npos);
}

TEST_CASE("mission html localization expands overview tokens from sibling stringtable", "[ui][missions][languages]")
{
    GLanguage = "English";
    const std::filesystem::path root = HtmlStringtableFixtureMissionPath();

    const RString csv = GetMissionStringtableFileForHtml((root / "overview.utf8.html").string().c_str());
    REQUIRE(csv.GetLength() > 0);

    const RString html = LoadLocalizedMissionHtmlUtf8((root / "overview.utf8.html").string().c_str());
    const std::string text = (const char*)html;

    CHECK(text.find("Ambush") != std::string::npos);
    CHECK(text.find("Those enemy positions are getting taken out this morning.") != std::string::npos);
    CHECK(text.find("$STR_NOT_DEFINED") != std::string::npos);
}

TEST_CASE("mission html localization resolves sibling stringtable for backslash paths", "[ui][missions][languages]")
{
    GLanguage = "English";
    std::string root = HtmlStringtableFixtureMissionPath();
    std::replace(root.begin(), root.end(), '/', '\\');
    const std::string htmlPath = root + "\\overview.utf8.html";

    const RString csv = GetMissionStringtableFileForHtml(htmlPath.c_str());
    REQUIRE(csv.GetLength() > 0);

    const RString html = LoadLocalizedMissionHtmlUtf8(htmlPath.c_str());
    const std::string text = (const char*)html;

    CHECK(text.find("$STR_DEMO_OVERVIEW_TITLE") == std::string::npos);
    CHECK(text.find("$STR_DEMO_BRIEF_BG_BODY_1") == std::string::npos);
    CHECK(text.find("Ambush") != std::string::npos);
}

TEST_CASE("mission html localization respects language and preserves mission links", "[ui][missions][languages]")
{
    GLanguage = "Czech";
    const std::filesystem::path root = HtmlStringtableFixtureMissionPath();

    const RString html = LoadLocalizedMissionHtmlUtf8((root / "briefing.utf8.html").string().c_str());
    const std::string text = (const char*)html;

    CHECK(text.find("Souvislosti") != std::string::npos);
    CHECK(text.find("Řádek jedna<br><br>Řádek dva") != std::string::npos);
    CHECK(text.find("href=\"marker:Start\"") != std::string::npos);
    CHECK(text.find(">pláž<") != std::string::npos);
    CHECK(text.find("name=\"OBJ_0\"") != std::string::npos);
}

TEST_CASE("mission html file selection prefers language-specific utf8 then legacy then generic",
          "[ui][missions][languages]")
{
    const std::filesystem::path root = HtmlSelectionFixtureMissionPath();

    GLanguage = "Czech";
    CHECK(FindLocalizedMissionHtmlFile(root.string().c_str(), "briefing") ==
          RString((root / "briefing.Czech.utf8.html").string().c_str()));

    GLanguage = "Russian";
    CHECK(FindLocalizedMissionHtmlFile(root.string().c_str(), "briefing") ==
          RString((root / "briefing.Russian.html").string().c_str()));

    GLanguage = "German";
    CHECK(FindLocalizedMissionHtmlFile(root.string().c_str(), "briefing") ==
          RString((root / "briefing.utf8.html").string().c_str()));
}

TEST_CASE("mission html file selection falls back to generic legacy html", "[ui][missions][languages]")
{
    const std::filesystem::path root = HtmlLegacyOnlyFixtureMissionPath();

    GLanguage = "Italian";
    CHECK(FindLocalizedMissionHtmlFile(root.string().c_str(), "briefing") ==
          RString((root / "briefing.html").string().c_str()));
    CHECK(FindLocalizedMissionHtmlFile(root.string().c_str(), "overview") ==
          RString((root / "overview.html").string().c_str()));
}

TEST_CASE("mission html localization decodes legacy language-specific briefing html", "[ui][missions][languages]")
{
    GLanguage = "Russian";
    const std::filesystem::path root = HtmlSelectionFixtureMissionPath();

    const RString html = LoadLocalizedMissionHtmlUtf8((root / "briefing.Russian.html").string().c_str());
    CHECK(std::string((const char*)html).find("Русский брифинг") != std::string::npos);
}

TEST_CASE("mission html localization decodes legacy generic overview html", "[ui][missions][languages]")
{
    GLanguage = "French";
    const std::filesystem::path root = HtmlLegacyOnlyFixtureMissionPath();

    const RString html = LoadLocalizedMissionHtmlUtf8((root / "overview.html").string().c_str());
    CHECK(std::string((const char*)html).find("Résumé générique") != std::string::npos);
}

TEST_CASE("mission briefing name reload uses raw sqm token after language switch", "[ui][missions][languages]")
{
    const std::filesystem::path root = HtmlStringtableFixtureMissionPath();
    LoadFixtureMissionStringtable(root);

    SetLanguage("English");
    CHECK(LoadLocalizedMissionBriefingName(root.string().c_str(), "fallback") == RString("Ambush (Demo)"));

    SetLanguage("Czech");
    CHECK(LoadLocalizedMissionBriefingName(root.string().c_str(), "fallback") == RString("Přepadení (Demo)"));
}

TEST_CASE("mission template display name follows mission-local stringtable language",
          "[ui][missions][languages][templates]")
{
    const std::filesystem::path root = HtmlStringtableFixtureMissionPath();
    LoadFixtureMissionStringtable(root);

    SetLanguage("English");
    CHECK(ResolveMissionTemplateDisplayName(root.string().c_str(), "HtmlStringtable") == RString("Ambush (Demo)"));

    SetLanguage("Czech");
    CHECK(ResolveMissionTemplateDisplayName(root.string().c_str(), "HtmlStringtable") == RString("Přepadení (Demo)"));

    SetLanguage("English");
}

TEST_CASE("mission template catalog lists matching directories before banks", "[ui][missions][templates]")
{
    const auto uniqueId = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path root = std::filesystem::temp_directory_path() / ("ofpr-template-catalog-" + uniqueId);
    std::filesystem::create_directories(root / "Templates" / "DirectoryMission.Eden");
    std::filesystem::create_directories(root / "Templates" / "WrongWorld.Noe");
    std::ofstream(root / "Templates" / "BankMission.Eden.pbo").put('x');
    std::ofstream(root / "Templates" / "TextFile.Eden.txt").put('x');

    const std::filesystem::path originalPath = std::filesystem::current_path();
    std::filesystem::current_path(root);

    AutoArray<MissionTemplateEntry> templates;
    ListMissionTemplates(templates, true, "Eden");

    std::filesystem::current_path(originalPath);
    std::filesystem::remove_all(root);

    REQUIRE(templates.Size() == 2);
    CHECK(templates[0].name == RString("DirectoryMission"));
    CHECK(templates[0].basePath == RString("Templates\\DirectoryMission.Eden"));
    CHECK_FALSE(templates[0].bank);
    CHECK(templates[1].name == RString("BankMission"));
    CHECK(templates[1].basePath == RString("Templates\\BankMission.Eden"));
    CHECK(templates[1].bank);
}

// The campaign-load screen lists missions by their briefingName, but it
// never loads a mission's per-mission stringtable into the global table.
// LoadLocalizedMissionBriefingName must therefore resolve the token from
// the mission's own stringtable.csv.  This fixture's key is loaded nowhere
// globally; without the per-mission CSV lookup the function misses and
// returns the "fallback" (the bug — the UI then showed the raw template).
//
// It must also re-resolve every call so a language switch is reflected:
// finish missions in Czech, switch to English, and the campaign menu
// shows English mission names.
TEST_CASE("campaign mission briefing name resolves per-mission and follows a language switch",
          "[ui][missions][languages][campaign]")
{
    const std::filesystem::path root = UiLocaleFixtureRoot() / "campaign_briefing" / "Flashpoint.Eden";
    const RString dir = root.string().c_str();

    // Played in Czech.
    GLanguage = "Czech";
    CHECK(LoadLocalizedMissionBriefingName(dir, "fallback") == RString("Blesk"));

    // Switched to English — the same mission now renders its English name.
    GLanguage = "English";
    CHECK(LoadLocalizedMissionBriefingName(dir, "fallback") == RString("Flashpoint"));

    // And back, to prove it tracks GLanguage rather than caching.
    GLanguage = "Czech";
    CHECK(LoadLocalizedMissionBriefingName(dir, "fallback") == RString("Blesk"));

    GLanguage = "English";
}

TEST_CASE("mission briefing name reload handles binary mission sqm", "[ui][missions][languages]")
{
    LoadMainMenuStringtable();

    const auto uniqueId = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path root = std::filesystem::temp_directory_path() / ("ofpr-binary-briefing-" + uniqueId);
    std::filesystem::create_directories(root);

    const std::filesystem::path sourceSqm = root / "source_mission.sqm";
    const std::filesystem::path binarySqm = root / "mission.sqm";
    const std::filesystem::path stringtable = root / "stringtable.csv";

    {
        std::ofstream out(sourceSqm);
        REQUIRE(out.good());
        out << "class Mission\n"
               "{\n"
               "    class Intel\n"
               "    {\n"
               "        briefingName = \"$STR_BIN_BRIEF\";\n"
               "    };\n"
               "};\n";
    }

    {
        std::ofstream out(stringtable);
        REQUIRE(out.good());
        out << "Language,English,Czech\n"
               "STR_BIN_BRIEF,Binary Demo,Binarni Demo\n";
    }

    ParamFile sqm;
    REQUIRE(sqm.Parse(sourceSqm.string().c_str()) == LSOK);
    REQUIRE(sqm.SaveBin(binarySqm.string().c_str()));
    LoadStringtable("mission", stringtable.string().c_str(), 2.0f, true);

    SetLanguage("English");
    CHECK(LoadLocalizedMissionBriefingName(root.string().c_str(), "fallback") == RString("Binary Demo"));

    SetLanguage("Czech");
    CHECK(LoadLocalizedMissionBriefingName(root.string().c_str(), "fallback") == RString("Binarni Demo"));

    std::filesystem::remove_all(root);
}

TEST_CASE("briefing title fallback restores localized Russian title format", "[ui][missions][languages]")
{
    LoadRemasterStringtable();

    SetLanguage("Russian");
    const RString russianTitle = FormatLocalizedBriefingTitle("retro", "Charlie Black", 2);
    const std::string russianText = (const char*)russianTitle;
    const std::string russianPrefix = (const char*)LocalizeString("STR_DISP_BRIEF_TITLE");
    REQUIRE(russianText != "retro");
    CHECK(russianText.find("retro") != std::string::npos);
    CHECK(russianText.find("Charlie Black 2") != std::string::npos);
    CHECK(russianText.rfind(russianPrefix + " (", 0) == 0);

    SetLanguage("English");
    CHECK(FormatLocalizedBriefingTitle("retro", "Charlie Black", 2) == RString("Briefing (retro, Charlie Black 2)"));
}
