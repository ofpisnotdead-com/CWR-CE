// Day/date strings in the profile/stat tables stayed English
// (e.g. "Fri May 29") in localized UIs because the campaign code formatted the date with
// strftime(), whose %a/%b use the C locale rather than the game's stringtable.
//
// FormatLocalizedDate() is the engine's localized formatter: %a -> LocalizeString(IDS_SUNDAY +
// wday), %b -> LocalizeString(IDS_JANUARY + mon). This test loads a fake stringtable whose
// day/month names are distinctive tokens ("T-Fri", "T-May", ...) and asserts the formatter
// emits them. Broken-state delta: a strftime-based formatter would emit the C-locale English
// "Fri May 29" regardless of language, so it would NOT match "T-Fri T-May 29".

#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Core/Global.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>

#include <cstring>
#include <ctime>
#include <string>

#ifdef _WIN32
#include <Windows.h>
#elif defined(__APPLE__)
#include <limits.h>
#include <mach-o/dyld.h>
#ifndef MAX_PATH
#define MAX_PATH PATH_MAX
#endif
#else
#include <limits.h>
#include <unistd.h>
#ifndef MAX_PATH
#define MAX_PATH PATH_MAX
#endif
#endif

using namespace Poseidon;

static std::string DateFixturePath()
{
    char p[MAX_PATH];
#ifdef _WIN32
    GetModuleFileNameA(nullptr, p, MAX_PATH);
    char* slash = strrchr(p, '\\');
    const char* sep = "\\fixtures\\";
#elif defined(__APPLE__)
    uint32_t size = sizeof(p);
    if (_NSGetExecutablePath(p, &size) != 0)
        p[0] = '\0';
    char* slash = strrchr(p, '/');
    const char* sep = "/fixtures/";
#else
    ssize_t n = readlink("/proc/self/exe", p, sizeof(p) - 1);
    p[n > 0 ? n : 0] = '\0';
    char* slash = strrchr(p, '/');
    const char* sep = "/fixtures/";
#endif
    if (slash)
    {
        *slash = '\0';
    }
    return std::string(p) + sep + "stringtable_datefmt.csv";
}

// Register the day/month IDS_ ids against the loaded fixture. The engine normally does this in
// an init module gated on a live stringtable, which a unit test doesn't run. Registered in
// calendar order so FormatLocalizedDate's `IDS_SUNDAY + wday` / `IDS_JANUARY + mon` indexing holds.
static void RegisterDateIds()
{
    IDS_SUNDAY = RegisterString("STR_SUNDAY");
    IDS_MONDAY = RegisterString("STR_MONDAY");
    IDS_TUESDAY = RegisterString("STR_TUESDAY");
    IDS_WEDNESDAY = RegisterString("STR_WEDNESDAY");
    IDS_THURSDAY = RegisterString("STR_THURSDAY");
    IDS_FRIDAY = RegisterString("STR_FRIDAY");
    IDS_SATURDAY = RegisterString("STR_SATURDAY");
    IDS_JANUARY = RegisterString("STR_JANUARY");
    IDS_FEBRUARY = RegisterString("STR_FEBRUARY");
    IDS_MARCH = RegisterString("STR_MARCH");
    IDS_APRIL = RegisterString("STR_APRIL");
    IDS_MAY = RegisterString("STR_MAY");
    IDS_JUNE = RegisterString("STR_JUNE");
    IDS_JULY = RegisterString("STR_JULY");
    IDS_AUGUST = RegisterString("STR_AUGUST");
    IDS_SEPTEMBER = RegisterString("STR_SEPTEMBER");
    IDS_OCTOBER = RegisterString("STR_OCTOBER");
    IDS_NOVEMBER = RegisterString("STR_NOVEMBER");
    IDS_DECEMBER = RegisterString("STR_DECEMBER");
}

static struct tm MakeDate(int wday, int mon, int mday)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_wday = wday;
    t.tm_mon = mon;
    t.tm_mday = mday;
    t.tm_year = 126; // 2026; unused by "%a %b %d" but set for realism
    return t;
}

TEST_CASE("FormatLocalizedDate localizes day/month names from the stringtable (#72)", "[stringtable][date][i18n]")
{
    ClearStringtable();
    GLanguage = "Testish";
    LoadStringtable("global", DateFixturePath().c_str(), 0, true);
    RegisterDateIds();

    char buffer[256];

    // The exact case from the bug report: "Fri May 29". With localization it must use the
    // fixture's tokens, not the C-locale English names strftime would produce.
    struct tm friMay = MakeDate(5, 4, 29);
    FormatLocalizedDate("%a %b %d", friMay, buffer);
    REQUIRE(std::string(buffer) == "T-Fri T-May 29");

    // A second date to guard the wday/mon indexing.
    struct tm wedJan = MakeDate(3, 0, 3);
    FormatLocalizedDate("%a %b %d", wedJan, buffer);
    REQUIRE(std::string(buffer) == "T-Wed T-Jan 3");

    // Literal characters in the format are preserved (Czech-style "%a %d. %b").
    FormatLocalizedDate("%a %d. %b", friMay, buffer);
    REQUIRE(std::string(buffer) == "T-Fri 29. T-May");
}
