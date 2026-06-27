// Integration tests: real CSV fixtures loaded through the engine's
// Poseidon::LoadStringtable() -> transcoder -> UTF-8 RString pipeline.
// Uses legacy CP1252/CP1250/CP1251 fixtures and a modern .utf8.csv fixture.
#include <catch2/catch_test_macros.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <sys/types.h>
#include <string>
#include <Poseidon/Foundation/Strings/RString.hpp>
#ifdef _WIN32
#include <direct.h>
#include <Windows.h>
#else
#include <unistd.h>
#include <limits.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

using Poseidon::GetLanguage;
using Poseidon::LocalizeString;
using namespace Poseidon;
#ifndef MAX_PATH
#define MAX_PATH PATH_MAX
#endif
#endif

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
static RString GetExeDir()
{
    static RString exeDir;
#pragma clang diagnostic pop

    if (exeDir.GetLength() == 0)
    {
        char p[MAX_PATH];
#ifdef _WIN32
        GetModuleFileNameA(nullptr, p, MAX_PATH);
        char* slash = strrchr(p, '\\');
#elif defined(__APPLE__)
        uint32_t size = sizeof(p);
        if (_NSGetExecutablePath(p, &size) != 0)
            p[0] = '\0';
        char* slash = strrchr(p, '/');
#else
        ssize_t n = readlink("/proc/self/exe", p, sizeof(p) - 1);
        if (n > 0)
            p[n] = '\0';
        else
            p[0] = '\0';
        char* slash = strrchr(p, '/');
#endif
        if (slash)
        {
            *slash = '\0';
        }
        exeDir = p;
    }
    return exeDir;
}

static std::string FixturePath(const char* name)
{
    std::string s = GetExeDir().Data();
#ifdef _WIN32
    s += "\\fixtures\\";
#else
    s += "/fixtures/";
#endif
    s += name;
    return s;
}

// CP1252 (Western European) -- the legacy shipped STRINGTABLE.CSV encoding

TEST_CASE("Stringtable encoding - CP1252 copyright symbol", "[stringtable][encoding][cp1252]")
{
    Poseidon::ClearStringtable();
    GLanguage = "English";
    Poseidon::LoadStringtable("global", FixturePath("stringtable_cp1252.csv").c_str(), 0, true);

    // © is byte 0xA9 in CP1252, must become 0xC2 0xA9 in UTF-8
    RString copyright = Poseidon::LocalizeString("STR_COPYRIGHT");
    REQUIRE(std::string(copyright.Data()) == "\xC2\xA9 2026 Fixture Lab");
}

TEST_CASE("Stringtable encoding - CP1252 French accents", "[stringtable][encoding][cp1252]")
{
    Poseidon::ClearStringtable();
    GLanguage = "French";
    Poseidon::LoadStringtable("global", FixturePath("stringtable_cp1252.csv").c_str(), 0, true);

    // "café naïve" -- é=C3 A9, ï=C3 AF in UTF-8
    RString accent = Poseidon::LocalizeString("STR_ACCENT");
    REQUIRE(std::string(accent.Data()) == "caf\xC3\xA9 na\xC3\xAF"
                                          "ve");
}

TEST_CASE("Stringtable encoding - CP1252 German umlauts", "[stringtable][encoding][cp1252]")
{
    Poseidon::ClearStringtable();
    GLanguage = "German";
    Poseidon::LoadStringtable("global", FixturePath("stringtable_cp1252.csv").c_str(), 0, true);

    // "Grüß Gott" -- ü=C3 BC, ß=C3 9F
    RString greeting = Poseidon::LocalizeString("STR_GREETING");
    REQUIRE(std::string(greeting.Data()) == "Gr\xC3\xBC\xC3\x9F Gott");
}

TEST_CASE("Stringtable encoding - CP1252 Spanish n", "[stringtable][encoding][cp1252]")
{
    Poseidon::ClearStringtable();
    GLanguage = "Spanish";
    Poseidon::LoadStringtable("global", FixturePath("stringtable_cp1252.csv").c_str(), 0, true);

    // "senor" -- n=C3 B1
    RString accent = Poseidon::LocalizeString("STR_ACCENT");
    REQUIRE(std::string(accent.Data()) == "se\xC3\xB1"
                                          "or");
}

TEST_CASE("Stringtable encoding - CP1252 Euro sign", "[stringtable][encoding][cp1252]")
{
    Poseidon::ClearStringtable();
    GLanguage = "French";
    Poseidon::LoadStringtable("global", FixturePath("stringtable_cp1252.csv").c_str(), 0, true);

    // € = 0x80 in CP1252 -> U+20AC -> UTF-8 E2 82 AC
    RString euro = Poseidon::LocalizeString("STR_EURO");
    REQUIRE(std::string(euro.Data()) == "100\xE2\x82\xAC");
}

TEST_CASE("Stringtable encoding - ASCII content unaffected by codepage", "[stringtable][encoding][ascii]")
{
    Poseidon::ClearStringtable();
    GLanguage = "English";
    Poseidon::LoadStringtable("global", FixturePath("stringtable_cp1252.csv").c_str(), 0, true);

    // Pure ASCII passes through every codepage unchanged
    RString plain = Poseidon::LocalizeString("STR_ASCII");
    REQUIRE(std::string(plain.Data()) == "plain");
}

// CP1250 (Central European) -- Czech, Polish

TEST_CASE("Stringtable encoding - CP1250 Czech diacritics", "[stringtable][encoding][cp1250]")
{
    Poseidon::ClearStringtable();
    GLanguage = "Czech";
    Poseidon::LoadStringtable("global", FixturePath("stringtable_cp1250.csv").c_str(), 0, true);

    // "čtyři pátů" -- č=C4 8D, ř=C5 99, á=C3 A1, ů=C5 AF
    RString cz = Poseidon::LocalizeString("STR_CZECH_CHARS");
    REQUIRE(std::string(cz.Data()) == "\xC4\x8D"
                                      "ty\xC5\x99"
                                      "i p\xC3\xA1"
                                      "t\xC5\xAF");
}

TEST_CASE("Stringtable encoding - CP1250 Polish Czesc", "[stringtable][encoding][cp1250]")
{
    Poseidon::ClearStringtable();
    GLanguage = "Polish";
    Poseidon::LoadStringtable("global", FixturePath("stringtable_cp1250.csv").c_str(), 0, true);

    // "Czesc" -- s=C5 9B, c=C4 87
    RString pl = Poseidon::LocalizeString("STR_POLISH");
    REQUIRE(std::string(pl.Data()) == "Cze\xC5\x9B\xC4\x87");
}

TEST_CASE("Stringtable encoding - credits names stay Central European in English", "[stringtable][encoding][credits]")
{
    Poseidon::ClearStringtable();
    GLanguage = "English";
    Poseidon::LoadStringtable("global", FixturePath("stringtable_credits_cp1250.csv").c_str(), 0, true);

    // Legacy CP1250 fixture: shared Czech proper names in non-Czech columns
    // must decode as Central European text, not Western-European mojibake.
    REQUIRE(std::string(Poseidon::LocalizeString("STR_CREDITSN01").Data()) == "Krad "
                                                                              "\xC5\xA0"
                                                                              "t"
                                                                              "\xC4\x9B"
                                                                              "p"
                                                                              "\xC4\x9B"
                                                                              "l");
    REQUIRE(std::string(Poseidon::LocalizeString("STR_CREDITSN07").Data()) == "Flar "
                                                                              "\xC5\x98"
                                                                              "ek"
                                                                              "\xC3\xA1"
                                                                              "n\\nBubo"
                                                                              "\xC5\xA1"
                                                                              " P"
                                                                              "\xC4\x9B"
                                                                              "cal");
}

TEST_CASE("Stringtable encoding - shared CP1250 rows stay Central European in matching language columns",
          "[stringtable][encoding][credits]")
{
    const char* languages[] = {"English", "Czech"};
    for (const char* language : languages)
    {
        CAPTURE(language);
        Poseidon::ClearStringtable();
        GLanguage = language;
        Poseidon::LoadStringtable("global", FixturePath("stringtable_credits_cp1250.csv").c_str(), 0, true);

        CHECK(std::string(Poseidon::LocalizeString("STR_CREDITSN07").Data()) == "Flar "
                                                                                "\xC5\x98"
                                                                                "ek"
                                                                                "\xC3\xA1"
                                                                                "n\\nBubo"
                                                                                "\xC5\xA1"
                                                                                " P"
                                                                                "\xC4\x9B"
                                                                                "cal");
        CHECK(std::string(Poseidon::LocalizeString("STR_DN_CREDITSN07").Data()) == "Qend "
                                                                                   "\xC5\x98"
                                                                                   "at"
                                                                                   "\xC4\x9B"
                                                                                   "j\\nVorbi Kov"
                                                                                   "\xC3\xA1"
                                                                                   "c\\nFlowshot");
    }
}

TEST_CASE("Stringtable encoding - legacy English mod column can carry Central European names",
          "[stringtable][encoding][mods][cp1250]")
{
    namespace fs = std::filesystem;

    const fs::path path = fs::temp_directory_path() / "ofpr_stringtable_legacy_cp1250_mod.csv";
    {
        std::ofstream out(path, std::ios::binary);
        out << "LANGUAGE,English,Czech\n";
        out << "STR_CAMP_NAME,\xC8"
               "MOD - P\xF8"
               "iklad,\xC8"
               "MOD\n";
    }

    Poseidon::ClearStringtable();
    GLanguage = "English";
    Poseidon::LoadStringtable("global", path.string().c_str(), 0, true);

    REQUIRE(std::string(Poseidon::LocalizeString("STR_CAMP_NAME").Data()) == "\xC4\x8C"
                                                                             "MOD - P\xC5\x99"
                                                                             "iklad");

    fs::remove(path);
}

// CP1251 (Cyrillic) -- synthetic legacy-encoded fixture

TEST_CASE("Stringtable encoding - CP1251 Russian days of week", "[stringtable][encoding][cp1251]")
{
    Poseidon::ClearStringtable();
    GLanguage = "Russian";
    Poseidon::LoadStringtable("global", FixturePath("stringtable_ru_cp1251.csv").c_str(), 0, true);

    // "Воскресенье" (Sunday) -- each letter is 2 bytes in UTF-8
    RString light_disc = Poseidon::LocalizeString("STR_SUNDAY");
    std::string expected = "\xD0\x92\xD0\xBE\xD1\x81\xD0\xBA\xD1\x80\xD0\xB5"
                           "\xD1\x81\xD0\xB5\xD0\xBD\xD1\x8C\xD0\xB5";
    REQUIRE(std::string(light_disc.Data()) == expected);

    // "Понедельник" (Monday)
    RString mon = Poseidon::LocalizeString("STR_MONDAY");
    REQUIRE(mon.GetLength() > 0);
    // Starts with П (U+041F) = UTF-8 D0 9F
    REQUIRE(mon.Data()[0] == (char)0xD0);
    REQUIRE(mon.Data()[1] == (char)0x9F);
}

// .utf8.csv variant -- modern opt-in UTF-8 file

TEST_CASE("Stringtable encoding - UTF-8 file passes through", "[stringtable][encoding][utf8]")
{
    Poseidon::ClearStringtable();
    GLanguage = "English";
    Poseidon::LoadStringtable("global", FixturePath("stringtable_modern.utf8.csv").c_str(), 0, true);

    // File is UTF-8; © is already 0xC2 0xA9 on disk and should not be re-encoded
    RString copyright = Poseidon::LocalizeString("STR_COPYRIGHT");
    REQUIRE(std::string(copyright.Data()) == "\xC2\xA9 2025 Modern");
}

TEST_CASE("Stringtable encoding - UTF-8 preserves Czech content", "[stringtable][encoding][utf8]")
{
    Poseidon::ClearStringtable();
    GLanguage = "Czech";
    Poseidon::LoadStringtable("global", FixturePath("stringtable_modern.utf8.csv").c_str(), 0, true);

    // "© 2025 Modernní" in UTF-8 -- í = C3 AD
    RString copyright = Poseidon::LocalizeString("STR_COPYRIGHT");
    REQUIRE(std::string(copyright.Data()) == "\xC2\xA9 2025 Modernn\xC3\xAD");
}

TEST_CASE("Stringtable encoding - UTF-8 preserves Russian content", "[stringtable][encoding][utf8]")
{
    Poseidon::ClearStringtable();
    GLanguage = "Russian";
    Poseidon::LoadStringtable("global", FixturePath("stringtable_modern.utf8.csv").c_str(), 0, true);

    // "щит" -- щ=D1 89, и=D0 B8, т=D1 82
    RString emoji = Poseidon::LocalizeString("STR_EMOJI");
    REQUIRE(std::string(emoji.Data()) == "\xD1\x89\xD0\xB8\xD1\x82");
}

// .utf8.csv preference -- both files exist, UTF-8 variant wins

TEST_CASE("Stringtable encoding - prefer .utf8.csv over .csv when both exist", "[stringtable][encoding][preference]")
{
    Poseidon::ClearStringtable();
    GLanguage = "English";

    // Caller passes the legacy path. stringtable_pair.utf8.csv sits alongside
    // stringtable_pair.csv -- the loader should pick the UTF-8 variant.
    Poseidon::LoadStringtable("global", FixturePath("stringtable_pair.csv").c_str(), 0, true);

    RString src = Poseidon::LocalizeString("STR_SOURCE");
    REQUIRE(std::string(src.Data()) == "MODERN (\xC2\xA9 UTF-8)");
}

TEST_CASE("Stringtable encoding - sibling shards merge in deterministic order", "[stringtable][encoding][shards]")
{
    Poseidon::ClearStringtable();
    GLanguage = "English";

    // Base legacy file plus sibling stringtable_shards_*.csv / *.utf8.csv files.
    // Expected load order:
    //   1. stringtable_shards.csv
    //   2. stringtable_shards_10_legacy.csv
    //   3. stringtable_shards_20_pair.utf8.csv   (preferred over same-basename .csv)
    //   4. stringtable_shards_30_final.csv
    Poseidon::LoadStringtable("global", FixturePath("stringtable_shards.csv").c_str(), 0, true);

    REQUIRE(std::string(Poseidon::LocalizeString("STR_BASE_ONLY").Data()) == "Base");
    REQUIRE(std::string(Poseidon::LocalizeString("STR_LEGACY_ONLY").Data()) == "Legacy \xC2\xA9");
    REQUIRE(std::string(Poseidon::LocalizeString("STR_PAIR_SOURCE").Data()) == "utf8 pair \xC2\xA9");
    REQUIRE(std::string(Poseidon::LocalizeString("STR_UTF8_ONLY").Data()) == "Modern ✓");
    REQUIRE(std::string(Poseidon::LocalizeString("STR_FINAL_ONLY").Data()) == "Final legacy");

    // Later shards override earlier ones, even when legacy and UTF-8 files mix.
    REQUIRE(std::string(Poseidon::LocalizeString("STR_OVERRIDE_CHAIN").Data()) == "final-thirty");
}

TEST_CASE("Stringtable encoding - UTF-8 rows survive later legacy reloads", "[stringtable][encoding][preference]")
{
    Poseidon::ClearStringtable();
    GLanguage = "English";

    Poseidon::LoadStringtable("global", FixturePath("stringtable_utf8_wins_first.utf8.csv").c_str(), 0, true);
    Poseidon::LoadStringtable("global", FixturePath("stringtable_utf8_wins_later.csv").c_str(), 0, false);

    REQUIRE(std::string(Poseidon::LocalizeString("STR_SHARED").Data()) == "UTF8 \xC5\x99");
    REQUIRE(std::string(Poseidon::LocalizeString("STR_UTF8_ONLY").Data()) == "Modern only");
    REQUIRE(std::string(Poseidon::LocalizeString("STR_LEGACY_ONLY").Data()) == "Legacy only");
}

TEST_CASE("Stringtable encoding - package-style stringtable loads sibling main menu shard",
          "[stringtable][encoding][shards]")
{
    Poseidon::ClearStringtable();
    GLanguage = "English";

    Poseidon::LoadStringtable("global", FixturePath("stringtable_package.csv").c_str(), 0, true);

    REQUIRE(std::string(Poseidon::LocalizeString("STR_PACKAGE_BASE_ONLY").Data()) == "Base package");
    REQUIRE(std::string(Poseidon::LocalizeString("STR_DISP_MAIN_OPT_AUDIO").Data()) == "Audio");
    REQUIRE(std::string(Poseidon::LocalizeString("STR_DISP_MAIN_OPT_GAME").Data()) == "Game");
    REQUIRE(std::string(Poseidon::LocalizeString("STR_DISP_MAIN_OPT_AUDIO_OUTPUT").Data()) == "Output device");
}

// Same bytes, different interpretation -- proves per-column codepage is honored

TEST_CASE("Stringtable encoding - same byte interpreted by column language", "[stringtable][encoding][per_column]")
{
    // Byte 0xE8 means different things depending on language column:
    //   - CP1252 (French) : è (U+00E8)
    //   - CP1250 (Czech)  : č (U+010D)
    // We don't have a single fixture with both columns, but we can prove the
    // transcoder uses the right codepage by loading two separate fixtures:

    Poseidon::ClearStringtable();
    GLanguage = "French";
    Poseidon::LoadStringtable("global", FixturePath("stringtable_cp1252.csv").c_str(), 0, true);
    RString fr = Poseidon::LocalizeString("STR_ACCENT");
    // "café naïve" -- starts with "caf" then é (C3 A9)
    REQUIRE(fr.Data()[3] == (char)0xC3);
    REQUIRE(fr.Data()[4] == (char)0xA9);

    Poseidon::ClearStringtable();
    GLanguage = "Czech";
    Poseidon::LoadStringtable("global", FixturePath("stringtable_cp1250.csv").c_str(), 0, true);
    RString cz = Poseidon::LocalizeString("STR_CZECH_CHARS");
    // starts with č (C4 8D)
    REQUIRE(cz.Data()[0] == (char)0xC4);
    REQUIRE(cz.Data()[1] == (char)0x8D);
}
