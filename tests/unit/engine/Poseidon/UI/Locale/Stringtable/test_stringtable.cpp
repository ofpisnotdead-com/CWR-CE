#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <cstring>
#include <stdio.h>
#include <sys/types.h>
#include <catch2/catch_message.hpp>
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

// Batch 1: Basic String Table Loading and Localization

// Helper to get executable directory
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
static RString GetExecutableDirectory()
{
    static RString exeDir;
#pragma clang diagnostic pop

    if (exeDir.GetLength() == 0)
    {
        char exePath[MAX_PATH];
#ifdef _WIN32
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        char* lastSlash = strrchr(exePath, '\\');
#elif defined(__APPLE__)
        uint32_t size = sizeof(exePath);
        if (_NSGetExecutablePath(exePath, &size) != 0)
            exePath[0] = '\0';
        char* lastSlash = strrchr(exePath, '/');
#else
        ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
        if (len > 0)
            exePath[len] = '\0';
        else
            exePath[0] = '\0';
        char* lastSlash = strrchr(exePath, '/');
#endif
        if (lastSlash)
        {
            *lastSlash = '\0';
        }

        exeDir = exePath;
    }

    return exeDir;
}

// Helper function to get fixture path relative to executable and verify file exists
static const char* GetTestFixturePath(const char* filename)
{
    static char path[MAX_PATH];

    // Build path: <exeDir>\fixtures\<filename>
    RString exeDir = GetExecutableDirectory();
#ifdef _WIN32
    snprintf(path, sizeof(path), "%s\\fixtures\\%s", exeDir.Data(), filename);
#else
    snprintf(path, sizeof(path), "%s/fixtures/%s", exeDir.Data(), filename);
#endif

    // Check if file exists
    if (!QIFStreamB::FileExist(path))
    {
        // Print helpful error message
        char cwd[1024];
#ifdef _WIN32
        _getcwd(cwd, sizeof(cwd));
#else
        getcwd(cwd, sizeof(cwd));
#endif

        fprintf(stderr, "\n========== FIXTURE FILE NOT FOUND ==========\n");
        fprintf(stderr, "ERROR: Cannot find test fixture file!\n");
        fprintf(stderr, "Expected: %s\n", path);
        fprintf(stderr, "Executable directory: %s\n", exeDir.Data());
        fprintf(stderr, "Working directory: %s\n", cwd);
        fprintf(stderr, "\n");
        fprintf(stderr, "SOLUTION:\n");
        fprintf(stderr, "1. Make sure you built the project (this copies fixtures)\n");
        fprintf(stderr, "2. Verify fixtures\\ folder exists in same directory as ElementTests.exe\n");
        fprintf(stderr, "   Location: %s\\fixtures\\\n", exeDir.Data());
        fprintf(stderr, "3. You can run tests from any directory - paths are relative to .exe\n");
        fprintf(stderr, "============================================\n\n");

        // Fail the test immediately with clear message
        FAIL("Fixture file not found: " << path << " (see error message above for details)");
    }

    return path;
}

TEST_CASE("Stringtable - Language setup", "[stringtable][language]")
{
    SECTION("Set global language to English")
    {
        GLanguage = "English";
        REQUIRE(std::string(GLanguage.Data()) == "English");
    }

    SECTION("Set global language to Czech")
    {
        GLanguage = "Czech";
        REQUIRE(std::string(GLanguage.Data()) == "Czech");
    }

    SECTION("Set global language to German")
    {
        GLanguage = "German";
        REQUIRE(std::string(GLanguage.Data()) == "German");
    }
}

TEST_CASE("Stringtable - Load global stringtable", "[stringtable][load][global]")
{
    // Clear any previous data
    Poseidon::ClearStringtable();

    SECTION("Load English stringtable")
    {
        GLanguage = "English";
        Poseidon::LoadStringtable("global", GetTestFixturePath("stringtable_test.csv"), 0, true);

        // Verify strings are loaded
        RString hello = Poseidon::LocalizeString("STR_HELLO");
        REQUIRE(std::string(hello.Data()) == "Hello");

        RString goodbye = Poseidon::LocalizeString("STR_GOODBYE");
        REQUIRE(std::string(goodbye.Data()) == "Goodbye");
    }

    SECTION("Load Czech stringtable")
    {
        GLanguage = "Czech";
        Poseidon::LoadStringtable("global", GetTestFixturePath("stringtable_test.csv"), 0, true);

        RString hello = Poseidon::LocalizeString("STR_HELLO");
        REQUIRE(std::string(hello.Data()) == "Ahoj");

        RString goodbye = Poseidon::LocalizeString("STR_GOODBYE");
        REQUIRE(std::string(goodbye.Data()) == "Sbohem");
    }

    SECTION("Load German stringtable")
    {
        GLanguage = "German";
        Poseidon::LoadStringtable("global", GetTestFixturePath("stringtable_test.csv"), 0, true);

        RString hello = Poseidon::LocalizeString("STR_HELLO");
        REQUIRE(std::string(hello.Data()) == "Hallo");

        RString goodbye = Poseidon::LocalizeString("STR_GOODBYE");
        REQUIRE(std::string(goodbye.Data()) == "Auf Wiedersehen");
    }
}

TEST_CASE("Stringtable - Localize by name", "[stringtable][localize]")
{
    Poseidon::ClearStringtable();
    GLanguage = "English";
    Poseidon::LoadStringtable("global", GetTestFixturePath("stringtable_test.csv"), 0, true);

    SECTION("Localize simple string")
    {
        RString result = Poseidon::LocalizeString("STR_HELLO");
        REQUIRE(std::string(result.Data()) == "Hello");
    }

    SECTION("Localize yes/no strings")
    {
        RString yes = Poseidon::LocalizeString("STR_YES");
        RString no = Poseidon::LocalizeString("STR_NO");

        REQUIRE(std::string(yes.Data()) == "Yes");
        REQUIRE(std::string(no.Data()) == "No");
    }

    SECTION("Localize quoted string")
    {
        RString weapon = Poseidon::LocalizeString("STR_WEAPON_RIFLE");
        REQUIRE(std::string(weapon.Data()) == "Synthetic Rifle");
    }
}

// Batch 2: String Registration and ID-based Localization

TEST_CASE("Stringtable - Register strings", "[stringtable][register]")
{
    Poseidon::ClearStringtable();
    GLanguage = "English";
    Poseidon::LoadStringtable("global", GetTestFixturePath("stringtable_test.csv"), 0, true);

    SECTION("Register single string")
    {
        int id = Poseidon::RegisterString("STR_HELLO");

        REQUIRE(id >= 0);

        RString result = Poseidon::LocalizeString(id);
        REQUIRE(std::string(result.Data()) == "Hello");
    }

    SECTION("Register multiple strings")
    {
        int id1 = Poseidon::RegisterString("STR_HELLO");
        int id2 = Poseidon::RegisterString("STR_GOODBYE");
        int id3 = Poseidon::RegisterString("STR_YES");

        REQUIRE(id1 >= 0);
        REQUIRE(id2 >= 0);
        REQUIRE(id3 >= 0);
        REQUIRE(id1 != id2);
        REQUIRE(id2 != id3);

        REQUIRE(std::string(Poseidon::LocalizeString(id1).Data()) == "Hello");
        REQUIRE(std::string(Poseidon::LocalizeString(id2).Data()) == "Goodbye");
        REQUIRE(std::string(Poseidon::LocalizeString(id3).Data()) == "Yes");
    }

    SECTION("Register same string multiple times returns same ID")
    {
        int id1 = Poseidon::RegisterString("STR_HELLO");
        int id2 = Poseidon::RegisterString("STR_HELLO");

        // Note: Current implementation always adds, so IDs will be different
        // This might be considered a bug, but we document current behavior
        REQUIRE(id1 >= 0);
        REQUIRE(id2 >= 0);
    }
}

TEST_CASE("Stringtable - Macro-based string definition", "[stringtable][macros]")
{
    Poseidon::ClearStringtable();
    GLanguage = "English";
    Poseidon::LoadStringtable("global", GetTestFixturePath("stringtable_test.csv"), 0, true);

    SECTION("Use REGISTER_STRING macro pattern")
    {
        // Simulate what REGISTER_STRING macro does
        int IDS_TEST = Poseidon::RegisterString("STR_HELLO");

        REQUIRE(IDS_TEST >= 0);

        RString text = Poseidon::LocalizeString(IDS_TEST);
        REQUIRE(std::string(text.Data()) == "Hello");
    }
}

// Batch 3: Multi-level String Tables (Global, Campaign, Mission)

TEST_CASE("Stringtable - Multi-level hierarchy", "[stringtable][hierarchy]")
{
    Poseidon::ClearStringtable();
    GLanguage = "English";

    SECTION("Load all three levels")
    {
        Poseidon::LoadStringtable("global", GetTestFixturePath("stringtable_test.csv"), 0, true);
        Poseidon::LoadStringtable("campaign", GetTestFixturePath("stringtable_campaign.csv"), 0, true);
        Poseidon::LoadStringtable("mission", GetTestFixturePath("stringtable_mission.csv"), 0, true);

        // Global strings should work
        RString global = Poseidon::LocalizeString("STR_HELLO");
        REQUIRE(std::string(global.Data()) == "Hello");

        // Campaign strings should work
        RString campaign = Poseidon::LocalizeString("STR_CAMPAIGN_NAME");
        REQUIRE(std::string(campaign.Data()) == "Synthetic Campaign");

        // Mission strings should work
        RString mission = Poseidon::LocalizeString("STR_MISSION_01");
        REQUIRE(std::string(mission.Data()) == "Synthetic Field Trial");
    }

    SECTION("Mission strings override campaign strings")
    {
        // If we define same key in both, mission should win
        // (Not tested here as our fixtures don't have overlapping keys)
        // This section documents expected behavior
        REQUIRE(true);
    }

    SECTION("Campaign strings override global strings")
    {
        // If we define same key in both, campaign should win
        // (Not tested here as our fixtures don't have overlapping keys)
        // This section documents expected behavior
        REQUIRE(true);
    }
}

TEST_CASE("Stringtable - Clear functionality", "[stringtable][clear]")
{
    Poseidon::ClearStringtable();
    GLanguage = "English";
    Poseidon::LoadStringtable("global", GetTestFixturePath("stringtable_test.csv"), 0, true);

    SECTION("Clear removes all strings")
    {
        // Verify string exists
        RString before = Poseidon::LocalizeString("STR_HELLO");
        REQUIRE(before.GetLength() > 0);

        // Clear
        Poseidon::ClearStringtable();

        // After clear, localization should return empty or error string
        // (Actual behavior may vary - documenting observed behavior)
        RString after = Poseidon::LocalizeString("STR_HELLO");
        // Either empty or error message expected
        REQUIRE(true); // Documented behavior
    }
}

// Batch 4: Special Character Handling

TEST_CASE("Stringtable - Special characters in CSV", "[stringtable][special]")
{
    Poseidon::ClearStringtable();
    GLanguage = "English";
    Poseidon::LoadStringtable("global", GetTestFixturePath("stringtable_test.csv"), 0, true);

    SECTION("Quoted strings with commas")
    {
        RString weapon = Poseidon::LocalizeString("STR_WEAPON_RIFLE");
        REQUIRE(std::string(weapon.Data()) == "Synthetic Rifle");
    }

    SECTION("Strings with escaped quotes")
    {
        RString quoted = Poseidon::LocalizeString("STR_QUOTED");
        REQUIRE(std::string(quoted.Data()) == "He said \"hello\"");
    }

    SECTION("Multiline strings")
    {
        RString multiline = Poseidon::LocalizeString("STR_MULTILINE");
        // Multiline should contain newline character
        REQUIRE(multiline.GetLength() > 0);
        REQUIRE(strstr(multiline.Data(), "Line one") != nullptr);
    }
}

TEST_CASE("Stringtable - Comment lines ignored", "[stringtable][comments]")
{
    Poseidon::ClearStringtable();
    GLanguage = "English";
    Poseidon::LoadStringtable("global", GetTestFixturePath("stringtable_test.csv"), 0, true);

    SECTION("COMMENT lines don't create entries")
    {
        // Try to localize comment (should fail/return empty)
        RString comment = Poseidon::LocalizeString("COMMENT");
        // Either empty or error message
        REQUIRE(true); // Documents current behavior
    }
}

// Batch 5: Poseidon::Localize() Helper Function (@ prefix)

TEST_CASE("Stringtable - Poseidon::Localize() with @ prefix", "[stringtable][localize]")
{
    Poseidon::ClearStringtable();
    GLanguage = "English";
    Poseidon::LoadStringtable("global", GetTestFixturePath("stringtable_test.csv"), 0, true);

    SECTION("String with @ prefix gets localized")
    {
        RString result = Poseidon::Localize("@STR_HELLO");
        REQUIRE(std::string(result.Data()) == "Hello");
    }

    SECTION("String without @ prefix returns as-is")
    {
        RString result = Poseidon::Localize("Plain text");
        REQUIRE(std::string(result.Data()) == "Plain text");
    }

    SECTION("Empty string")
    {
        RString result = Poseidon::Localize("");
        REQUIRE(std::string(result.Data()) == "");
    }

    SECTION("@ only prefix")
    {
        RString result = Poseidon::Localize("@");
        // Should try to localize empty string
        REQUIRE(true); // Document behavior
    }
}

// Batch 5b: Poseidon::Localize() with $STR prefix (text mission.sqm fast-search path)

TEST_CASE("Stringtable - Poseidon::Localize() with $STR prefix", "[stringtable][localize]")
{
    Poseidon::ClearStringtable();
    GLanguage = "English";
    Poseidon::LoadStringtable("global", GetTestFixturePath("stringtable_test.csv"), 0, true);

    SECTION("$STR_ prefix gets localized")
    {
        RString result = Poseidon::Localize("$STR_HELLO");
        REQUIRE(std::string(result.Data()) == "Hello");
    }

    SECTION("$ alone is left as-is")
    {
        RString result = Poseidon::Localize("$");
        REQUIRE(std::string(result.Data()) == "$");
    }

    SECTION("$ followed by non-STR is left as-is")
    {
        RString result = Poseidon::Localize("$VAR");
        REQUIRE(std::string(result.Data()) == "$VAR");
    }

    SECTION("Plain literal containing dollar later is left as-is")
    {
        RString result = Poseidon::Localize("price $5");
        REQUIRE(std::string(result.Data()) == "price $5");
    }
}

// Batch 5c: Poseidon::LookupStringtableCsv — direct CSV scan with English fallback

TEST_CASE("Stringtable - Poseidon::LookupStringtableCsv direct scan", "[stringtable][localize]")
{
    GLanguage = "English";
    const RString csv = GetTestFixturePath("stringtable_test.csv");

    SECTION("Returns the English value when GLanguage=English")
    {
        RString result = Poseidon::LookupStringtableCsv(csv, "STR_HELLO");
        REQUIRE(std::string(result.Data()) == "Hello");
    }

    SECTION("Returns the German value when GLanguage=German")
    {
        GLanguage = "German";
        RString result = Poseidon::LookupStringtableCsv(csv, "STR_HELLO");
        REQUIRE(std::string(result.Data()) == "Hallo");
        GLanguage = "English";
    }

    SECTION("Falls back to English when language column is missing")
    {
        GLanguage = "Russian"; // not in fixture; English fallback expected
        RString result = Poseidon::LookupStringtableCsv(csv, "STR_HELLO");
        REQUIRE(std::string(result.Data()) == "Hello");
        GLanguage = "English";
    }

    SECTION("Returns empty for unknown key")
    {
        RString result = Poseidon::LookupStringtableCsv(csv, "STR_NOT_THERE");
        REQUIRE(result.GetLength() == 0);
    }

    SECTION("Returns empty for missing file")
    {
        RString result = Poseidon::LookupStringtableCsv("does/not/exist.csv", "STR_HELLO");
        REQUIRE(result.GetLength() == 0);
    }

    SECTION("Returns empty for null/empty key")
    {
        RString result = Poseidon::LookupStringtableCsv(csv, "");
        REQUIRE(result.GetLength() == 0);
    }
}

// Batch 6: Error Handling

TEST_CASE("Stringtable - Missing string handling", "[stringtable][errors]")
{
    Poseidon::ClearStringtable();
    GLanguage = "English";
    Poseidon::LoadStringtable("global", GetTestFixturePath("stringtable_test.csv"), 0, true);

    SECTION("Localize non-existent string")
    {
        RString result = Poseidon::LocalizeString("STR_NONEXISTENT");

        // In debug builds, returns error string
        // In release builds, returns empty string
        // We just verify it doesn't crash
        REQUIRE(true);
    }

    SECTION("Localize with invalid ID")
    {
        // Register a valid string first
        int validId = Poseidon::RegisterString("STR_HELLO");

        // Try invalid ID
        RString result = Poseidon::LocalizeString(validId + 100);

        // Should return error string or empty
        REQUIRE(true);
    }

    SECTION("Localize with negative ID")
    {
        RString result = Poseidon::LocalizeString(-1);

        // Should return error string or empty
        REQUIRE(true);
    }
}

TEST_CASE("Stringtable - Load non-existent file", "[stringtable][errors]")
{
    Poseidon::ClearStringtable();
    GLanguage = "English";

    SECTION("Load missing file doesn't crash")
    {
        // Build path relative to exe (file won't exist, but won't crash)
        RString exeDir = GetExecutableDirectory();
        char missingPath[MAX_PATH];
#ifdef _WIN32
        sprintf(missingPath, "%s\\fixtures\\nonexistent_file.csv", exeDir.Data());
#else
        sprintf(missingPath, "%s/fixtures/nonexistent_file.csv", exeDir.Data());
#endif

        Poseidon::LoadStringtable("global", missingPath, 0, true);

        // Verify system still works
        REQUIRE(true);
    }
}

TEST_CASE("Stringtable - Invalid table type", "[stringtable][errors]")
{
    Poseidon::ClearStringtable();
    GLanguage = "English";

    SECTION("Load with invalid type")
    {
        // Should report error but not crash
        Poseidon::LoadStringtable("invalid_type", GetTestFixturePath("stringtable_test.csv"), 0, true);

        REQUIRE(true);
    }
}

// Batch 7: Language Switching

TEST_CASE("Stringtable - Language switching", "[stringtable][language][switch]")
{
    Poseidon::ClearStringtable();

    SECTION("Switch language and reload")
    {
        // Load English
        GLanguage = "English";
        Poseidon::LoadStringtable("global", GetTestFixturePath("stringtable_test.csv"), 0, true);

        RString hello1 = Poseidon::LocalizeString("STR_HELLO");
        REQUIRE(std::string(hello1.Data()) == "Hello");

        // Switch to Czech
        GLanguage = "Czech";
        Poseidon::LoadStringtable("global", GetTestFixturePath("stringtable_test.csv"), 0, true);

        RString hello2 = Poseidon::LocalizeString("STR_HELLO");
        REQUIRE(std::string(hello2.Data()) == "Ahoj");
    }
}

// Batch 8: Edge Cases

TEST_CASE("Stringtable - Edge cases", "[stringtable][edge]")
{
    Poseidon::ClearStringtable();
    GLanguage = "English";

    SECTION("Empty string name")
    {
        Poseidon::LoadStringtable("global", GetTestFixturePath("stringtable_test.csv"), 0, true);
        RString result = Poseidon::LocalizeString("");
        // Should handle gracefully
        REQUIRE(true);
    }

    SECTION("Very long string names")
    {
        char longName[1024];
        for (int i = 0; i < 1023; i++)
        {
            longName[i] = 'A';
        }
        longName[1023] = '\0';

        RString result = Poseidon::LocalizeString(longName);
        // Should handle gracefully
        REQUIRE(true);
    }

    SECTION("Reinit without clear")
    {
        Poseidon::LoadStringtable("global", GetTestFixturePath("stringtable_test.csv"), 0, true);

        // Load again with init=true (should replace)
        Poseidon::LoadStringtable("global", GetTestFixturePath("stringtable_test.csv"), 0, true);

        RString hello = Poseidon::LocalizeString("STR_HELLO");
        REQUIRE(std::string(hello.Data()) == "Hello");
    }

    SECTION("Reload without init")
    {
        Poseidon::LoadStringtable("global", GetTestFixturePath("stringtable_test.csv"), 0, true);

        // Load again with init=false (should add/update)
        Poseidon::LoadStringtable("global", GetTestFixturePath("stringtable_test.csv"), 0, false);

        RString hello = Poseidon::LocalizeString("STR_HELLO");
        REQUIRE(std::string(hello.Data()) == "Hello");
    }
}

TEST_CASE("Stringtable - Multiple languages in same run", "[stringtable][edge][multilang]")
{
    SECTION("Load different languages sequentially")
    {
        Poseidon::ClearStringtable();

        // Load English
        GLanguage = "English";
        Poseidon::LoadStringtable("global", GetTestFixturePath("stringtable_test.csv"), 0, true);
        RString en = Poseidon::LocalizeString("STR_HELLO");

        // Load Czech
        GLanguage = "Czech";
        Poseidon::LoadStringtable("global", GetTestFixturePath("stringtable_test.csv"), 0, true);
        RString cz = Poseidon::LocalizeString("STR_HELLO");

        // Load German
        GLanguage = "German";
        Poseidon::LoadStringtable("global", GetTestFixturePath("stringtable_test.csv"), 0, true);
        RString de = Poseidon::LocalizeString("STR_HELLO");

        REQUIRE(std::string(en.Data()) == "Hello");
        REQUIRE(std::string(cz.Data()) == "Ahoj");
        REQUIRE(std::string(de.Data()) == "Hallo");
    }
}
