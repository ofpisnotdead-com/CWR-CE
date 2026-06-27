// Runtime language switching tests.
// Covers StringTable::SetLanguage, RelocalizeRegistered, callback registry.
#include <catch2/catch_test_macros.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <cstring>
#include <sys/types.h>
#include <string>
#include <utility>
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

using Poseidon::LanguageChangedCallback;
using namespace Poseidon;

using Poseidon::GetLanguage;
using Poseidon::LocalizeString;
using Poseidon::RegisterLanguageChangedCallback;
using Poseidon::UnregisterLanguageChangedCallback;
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

// RAII guard so a failed REQUIRE doesn't leave dangling stack-captured
// callbacks in the static registry (next test would SEGV when calling them).
struct LangCbGuard
{
    int token;
    explicit LangCbGuard(Poseidon::LanguageChangedCallback cb)
        : token(Poseidon::RegisterLanguageChangedCallback(std::move(cb)))
    {
    }
    ~LangCbGuard() { Poseidon::UnregisterLanguageChangedCallback(token); }
    LangCbGuard(const LangCbGuard&) = delete;
    LangCbGuard& operator=(const LangCbGuard&) = delete;
};

// Poseidon::SetLanguage — tables reload so by-name lookups follow the new column

TEST_CASE("Poseidon::SetLanguage switches by-name lookup to new column", "[stringtable][switch]")
{
    Poseidon::ClearStringtable();
    GLanguage = "English";
    Poseidon::LoadStringtable("global", FixturePath("stringtable_cp1252.csv").c_str(), 0, true);

    REQUIRE(std::string(Poseidon::LocalizeString("STR_GREETING").Data()) == "Hello");

    REQUIRE(Poseidon::SetLanguage("French"));
    REQUIRE(std::string(Poseidon::LocalizeString("STR_GREETING").Data()) == "Bonjour");

    REQUIRE(Poseidon::SetLanguage("German"));
    // "Grüß Gott" — ü=C3 BC, ß=C3 9F after CP1252 transcode
    REQUIRE(std::string(Poseidon::LocalizeString("STR_GREETING").Data()) == "Gr\xC3\xBC\xC3\x9F Gott");

    REQUIRE(Poseidon::SetLanguage("English"));
    REQUIRE(std::string(Poseidon::LocalizeString("STR_GREETING").Data()) == "Hello");
}

TEST_CASE("Poseidon::SetLanguage to same language is a no-op but returns true", "[stringtable][switch]")
{
    Poseidon::ClearStringtable();
    GLanguage = "English";
    Poseidon::LoadStringtable("global", FixturePath("stringtable_cp1252.csv").c_str(), 0, true);

    REQUIRE(Poseidon::SetLanguage("English"));
    REQUIRE(std::string(Poseidon::LocalizeString("STR_GREETING").Data()) == "Hello");
}

TEST_CASE("Poseidon::SetLanguage empty string fails and keeps current language", "[stringtable][switch]")
{
    Poseidon::ClearStringtable();
    GLanguage = "English";
    Poseidon::LoadStringtable("global", FixturePath("stringtable_cp1252.csv").c_str(), 0, true);

    REQUIRE_FALSE(Poseidon::SetLanguage(""));
    REQUIRE(std::string(Poseidon::LocalizeString("STR_GREETING").Data()) == "Hello");
}

// Poseidon::SetLanguage — registered ID snapshots refresh via RelocalizeRegistered

TEST_CASE("Registered string id reflects new language after Poseidon::SetLanguage", "[stringtable][switch][register]")
{
    Poseidon::ClearStringtable();
    GLanguage = "English";
    Poseidon::LoadStringtable("global", FixturePath("stringtable_cp1252.csv").c_str(), 0, true);

    int id = Poseidon::RegisterString("STR_GREETING");
    REQUIRE(id >= 0);
    REQUIRE(std::string(Poseidon::LocalizeString(id).Data()) == "Hello");

    REQUIRE(Poseidon::SetLanguage("French"));
    REQUIRE(std::string(Poseidon::LocalizeString(id).Data()) == "Bonjour");

    REQUIRE(Poseidon::SetLanguage("Spanish"));
    REQUIRE(std::string(Poseidon::LocalizeString(id).Data()) == "Hola");
}

TEST_CASE("Multiple registered ids refresh independently", "[stringtable][switch][register]")
{
    Poseidon::ClearStringtable();
    GLanguage = "English";
    Poseidon::LoadStringtable("global", FixturePath("stringtable_cp1252.csv").c_str(), 0, true);

    int greet = Poseidon::RegisterString("STR_GREETING");
    int ascii = Poseidon::RegisterString("STR_ASCII");
    REQUIRE(std::string(Poseidon::LocalizeString(greet).Data()) == "Hello");
    REQUIRE(std::string(Poseidon::LocalizeString(ascii).Data()) == "plain");

    REQUIRE(Poseidon::SetLanguage("French"));
    REQUIRE(std::string(Poseidon::LocalizeString(greet).Data()) == "Bonjour");
    REQUIRE(std::string(Poseidon::LocalizeString(ascii).Data()) == "plain"); // ASCII is same in all columns
}

// Callback registry

TEST_CASE("Language-changed callback fires on Poseidon::SetLanguage", "[stringtable][switch][callback]")
{
    Poseidon::ClearStringtable();
    GLanguage = "English";
    Poseidon::LoadStringtable("global", FixturePath("stringtable_cp1252.csv").c_str(), 0, true);

    int fired = 0;
    {
        LangCbGuard g([&fired]() { fired++; });

        REQUIRE(Poseidon::SetLanguage("French"));
        REQUIRE(fired == 1);

        REQUIRE(Poseidon::SetLanguage("German"));
        REQUIRE(fired == 2);
    } // guard unregisters here

    REQUIRE(Poseidon::SetLanguage("English"));
    REQUIRE(fired == 2); // unregistered — no further fires
}

TEST_CASE("Same-language Poseidon::SetLanguage does not fire callbacks", "[stringtable][switch][callback]")
{
    Poseidon::ClearStringtable();
    GLanguage = "English";
    Poseidon::LoadStringtable("global", FixturePath("stringtable_cp1252.csv").c_str(), 0, true);

    int fired = 0;
    LangCbGuard g([&fired]() { fired++; });

    REQUIRE(Poseidon::SetLanguage("English"));
    REQUIRE(fired == 0);
}

TEST_CASE("Multiple callbacks fire in registration order", "[stringtable][switch][callback]")
{
    Poseidon::ClearStringtable();
    GLanguage = "English";
    Poseidon::LoadStringtable("global", FixturePath("stringtable_cp1252.csv").c_str(), 0, true);

    std::string order;
    LangCbGuard g1([&order]() { order += "A"; });
    {
        LangCbGuard g2([&order]() { order += "B"; });
        LangCbGuard g3([&order]() { order += "C"; });

        REQUIRE(Poseidon::SetLanguage("French"));
        REQUIRE(order == "ABC");
    } // g2, g3 unregister here; g1 remains

    order.clear();
    REQUIRE(Poseidon::SetLanguage("English"));
    REQUIRE(order == "A");
}

TEST_CASE("Callback observes refreshed values via Poseidon::LocalizeString", "[stringtable][switch][callback]")
{
    Poseidon::ClearStringtable();
    GLanguage = "English";
    Poseidon::LoadStringtable("global", FixturePath("stringtable_cp1252.csv").c_str(), 0, true);

    int id = Poseidon::RegisterString("STR_GREETING");
    std::string observed;
    LangCbGuard g([&observed, id]() { observed = std::string(Poseidon::LocalizeString(id).Data()); });

    REQUIRE(Poseidon::SetLanguage("French"));
    REQUIRE(observed == "Bonjour");

    REQUIRE(Poseidon::SetLanguage("Spanish"));
    REQUIRE(observed == "Hola");
}

// Tracked-files list is deduped across repeated loads

TEST_CASE("Repeated Poseidon::LoadStringtable on same file does not duplicate replays", "[stringtable][switch][loaded]")
{
    Poseidon::ClearStringtable();
    GLanguage = "English";
    // Load same file twice — should not result in duplicate work on Poseidon::SetLanguage.
    Poseidon::LoadStringtable("global", FixturePath("stringtable_cp1252.csv").c_str(), 0, true);
    Poseidon::LoadStringtable("global", FixturePath("stringtable_cp1252.csv").c_str(), 0, false);

    int id = Poseidon::RegisterString("STR_GREETING");
    REQUIRE(Poseidon::SetLanguage("French"));
    REQUIRE(std::string(Poseidon::LocalizeString(id).Data()) == "Bonjour");
    REQUIRE(Poseidon::SetLanguage("English"));
    REQUIRE(std::string(Poseidon::LocalizeString(id).Data()) == "Hello");
}

TEST_CASE("Poseidon::SetLanguage reloads mixed legacy and UTF-8 shards", "[stringtable][switch][loaded][shards]")
{
    Poseidon::ClearStringtable();
    GLanguage = "English";
    Poseidon::LoadStringtable("global", FixturePath("stringtable_shards.csv").c_str(), 0, true);

    int id = Poseidon::RegisterString("STR_OVERRIDE_CHAIN");
    REQUIRE(id >= 0);
    REQUIRE(std::string(Poseidon::LocalizeString(id).Data()) == "final-thirty");

    REQUIRE(Poseidon::SetLanguage("Czech"));
    REQUIRE(std::string(Poseidon::LocalizeString("STR_BASE_ONLY").Data()) == "Základ");
    REQUIRE(std::string(Poseidon::LocalizeString("STR_LEGACY_ONLY").Data()) == "Žába");
    REQUIRE(std::string(Poseidon::LocalizeString("STR_PAIR_SOURCE").Data()) == "utf8 pár");
    REQUIRE(std::string(Poseidon::LocalizeString("STR_UTF8_ONLY").Data()) == "Příliš");
    REQUIRE(std::string(Poseidon::LocalizeString("STR_FINAL_ONLY").Data()) == "Třicet");
    REQUIRE(std::string(Poseidon::LocalizeString(id).Data()) == "třicet");

    REQUIRE(Poseidon::SetLanguage("Russian"));
    REQUIRE(std::string(Poseidon::LocalizeString("STR_BASE_ONLY").Data()) == "База");
    REQUIRE(std::string(Poseidon::LocalizeString("STR_LEGACY_ONLY").Data()) == "Щит");
    REQUIRE(std::string(Poseidon::LocalizeString("STR_PAIR_SOURCE").Data()) == "utf8 пара");
    REQUIRE(std::string(Poseidon::LocalizeString("STR_UTF8_ONLY").Data()) == "современный");
    REQUIRE(std::string(Poseidon::LocalizeString("STR_FINAL_ONLY").Data()) == "Финал");
    REQUIRE(std::string(Poseidon::LocalizeString(id).Data()) == "тридцать");
}
