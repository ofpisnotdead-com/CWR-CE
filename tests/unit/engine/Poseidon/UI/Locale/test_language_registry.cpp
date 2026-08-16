#include <catch2/catch_test_macros.hpp>

#include <Poseidon/UI/Locale/LanguageRegistry.hpp>
#include <Poseidon/UI/Locale/SupportedLanguages.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>

#include <cstring>
#include <string>
#include <vector>

using CfgLib::LanguageInfo;
using CfgLib::LanguageRegistry;

TEST_CASE("LanguageRegistry - built-in defaults", "[locale][language]")
{
    auto& reg = LanguageRegistry::Instance();
    reg.ResetToDefaults();

    REQUIRE(reg.Count() == 8);

    const LanguageInfo* en = reg.Find("english"); // case-insensitive
    REQUIRE(en != nullptr);
    REQUIRE(en->codepage == Poseidon::Codepage::CP1252);
    REQUIRE(en->voiceSuffix.empty());

    const LanguageInfo* cz = reg.Find("Czech");
    REQUIRE(cz != nullptr);
    REQUIRE(cz->codepage == Poseidon::Codepage::CP1250);
    REQUIRE(cz->voiceSuffix == "cz");

    const LanguageInfo* ru = reg.Find("Russian");
    REQUIRE(ru != nullptr);
    REQUIRE(ru->codepage == Poseidon::Codepage::CP1251);

    REQUIRE(reg.Find("Klingon") == nullptr);
}

TEST_CASE("LanguageRegistry - normalize accepts language aliases", "[locale][language]")
{
    auto& reg = LanguageRegistry::Instance();
    reg.ResetToDefaults();

    REQUIRE(CfgLib::NormalizeSupportedLanguage("CZ") == "Czech");
    REQUIRE(CfgLib::NormalizeSupportedLanguage("cz") == "Czech");
    REQUIRE(CfgLib::NormalizeSupportedLanguage("cs_CZ.UTF-8") == "Czech");
    REQUIRE(CfgLib::NormalizeSupportedLanguage("\xC4\x8C"
                                               "e\xC5\xA1"
                                               "tina") == "Czech");
    REQUIRE(CfgLib::NormalizeSupportedLanguage("DE") == "German");
    REQUIRE(CfgLib::NormalizeSupportedLanguage("de-DE") == "German");
}

// The Game-settings Voice Language picker offered the full text-language
// set; it must list only languages the game provides dubbing for (hasVoice). The
// Remaster ships voiceover for English (base) + Czech only.
TEST_CASE("LanguageRegistry - voice-capable subset (hasVoice)", "[locale][language][voice]")
{
    auto& reg = LanguageRegistry::Instance();
    reg.ResetToDefaults();

    REQUIRE(reg.Count() == 8);      // full text set unchanged
    REQUIRE(reg.VoiceCount() == 2); // voice picker shows only English + Czech

    const int en = reg.VoiceIndexOf("English");
    const int cz = reg.VoiceIndexOf("czech"); // case-insensitive
    REQUIRE(en >= 0);
    REQUIRE(cz >= 0);
    REQUIRE(en != cz);
    REQUIRE(std::string(reg.VoiceNameAt(en)) == "English"); // round-trips to canonical name
    REQUIRE(std::string(reg.VoiceNameAt(cz)) == "Czech");

    // Text-only languages are absent from the voice subset.
    REQUIRE(reg.VoiceIndexOf("German") == -1);
    REQUIRE(reg.VoiceIndexOf("Russian") == -1);
    REQUIRE(reg.VoiceIndexOf("nope") == -1);

    // Out-of-range VoiceNameAt is empty, never a crash.
    REQUIRE(std::string(reg.VoiceNameAt(-1)).empty());
    REQUIRE(std::string(reg.VoiceNameAt(reg.VoiceCount())).empty());
}

// The Options language pickers showed the canonical English names
// ("Russian", "English") untranslated. The fix gives each language an autonym
// (its name in its own script) that DisplayNamePtrs() exposes for the pickers,
// while `name` stays the English identity key used by SetLanguage/Find.
TEST_CASE("LanguageRegistry - autonyms drive the display name list", "[locale][language]")
{
    auto& reg = LanguageRegistry::Instance();
    reg.ResetToDefaults();

    REQUIRE(reg.Find("Russian")->autonym == "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9"); // Русский
    REQUIRE(reg.Find("Czech")->autonym == "\xC4\x8C"
                                          "e\xC5\xA1"
                                          "tina"); // Čeština
    REQUIRE(reg.Find("English")->autonym == "English");

    // DisplayNamePtrs is in Languages() order and carries autonyms, not the English names.
    const char* const* display = reg.DisplayNamePtrs();
    const char* const* names = reg.NamePtrs();
    const auto& langs = reg.Languages();
    int ruIdx = -1;
    for (int i = 0; i < reg.Count(); i++)
        if (langs[i].name == "Russian")
            ruIdx = i;
    REQUIRE(ruIdx >= 0);
    REQUIRE(std::string(names[ruIdx]) == "Russian");
    REQUIRE(std::string(display[ruIdx]) == "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9");

    SECTION("a config language without an autonym falls back to its name")
    {
        const char* cfg = "class CfgLanguages\n"
                          "{\n"
                          "  languages[] = {\"English\", \"Klingon\"};\n"
                          "  class Klingon { code=\"KL\"; codepage=\"UTF8\"; };\n"
                          "};\n";
        ParamFile pf;
        QIStream in(cfg, std::strlen(cfg));
        pf.Parse(in);
        reg.LoadFromConfig(*pf.FindEntry("CfgLanguages"));

        const char* const* d = reg.DisplayNamePtrs();
        REQUIRE(std::string(d[1]) == "Klingon"); // no autonym in config → name
        reg.ResetToDefaults();
    }
}

TEST_CASE("LanguageRegistry - CfgLanguages overrides the per-game set", "[locale][language]")
{
    auto& reg = LanguageRegistry::Instance();
    reg.ResetToDefaults();

    // A game that ships only three languages, one of them brand new, plus a metadata override.
    const char* cfg =
        "class CfgLanguages\n"
        "{\n"
        "  languages[] = {\"English\", \"Czech\", \"Klingon\"};\n"
        "  class English { code=\"EN\"; codepage=\"CP1252\"; voice=1; };\n"
        "  class Czech { code=\"CZ\"; codepage=\"CP1250\"; voiceSuffix=\"cz\"; localeAliases[]={\"cs\"}; };\n"
        "  class Klingon { code=\"KL\"; codepage=\"UTF8\"; voiceSuffix=\"kl\"; voice=0; localeAliases[]={\"tlh\"}; };\n"
        "};\n";

    ParamFile pf;
    QIStream in(cfg, std::strlen(cfg));
    pf.Parse(in);

    const ParamEntry* cfgLangs = pf.FindEntry("CfgLanguages");
    REQUIRE(cfgLangs != nullptr);
    reg.LoadFromConfig(*cfgLangs);

    // The set is now the game's three languages, in order.
    REQUIRE(reg.Count() == 3);
    REQUIRE(reg.Find("French") == nullptr); // a default language not listed by this game

    const LanguageInfo* kl = reg.Find("Klingon");
    REQUIRE(kl != nullptr);
    REQUIRE(kl->code == "KL");
    REQUIRE(kl->codepage == Poseidon::Codepage::Utf8);
    REQUIRE(kl->voiceSuffix == "kl");
    REQUIRE(kl->hasVoice == false);
    REQUIRE(kl->localeAliases.size() == 1);
    REQUIRE(kl->localeAliases[0] == "tlh");

    // NamePtrs reflects the new set.
    const char* const* names = reg.NamePtrs();
    REQUIRE(std::string(names[2]) == "Klingon");

    reg.ResetToDefaults(); // leave the singleton clean for other tests
    REQUIRE(reg.Count() == 8);
}
