#include <Poseidon/UI/Locale/SupportedLanguages.hpp>
#include <Poseidon/UI/Locale/LanguageRegistry.hpp>

#include <Poseidon/Foundation/Common/Win.h>

#include <cstdlib>
#include <cstring>
#include <vector>
#ifndef _WIN32
#include <strings.h>
#endif

namespace CfgLib
{

static int CaseCmp(const char* a, const char* b)
{
#ifdef _WIN32
    return _stricmp(a, b);
#else
    return strcasecmp(a, b);
#endif
}

int FindSupportedLanguageIndex(const std::string& language)
{
    const auto& langs = LanguageRegistry::Instance().Languages();
    for (int i = 0; i < static_cast<int>(langs.size()); ++i)
    {
        if (CaseCmp(language.c_str(), langs[i].name.c_str()) == 0)
            return i;
    }
    return -1;
}

bool IsSupportedLanguage(const std::string& language)
{
    return FindSupportedLanguageIndex(language) >= 0;
}

std::string NormalizeSupportedLanguage(const std::string& language, const std::string& fallback)
{
    auto& reg = LanguageRegistry::Instance();
    if (const LanguageInfo* l = reg.Find(language))
        return l->name;

    for (const auto& l : reg.Languages())
    {
        if (!l.code.empty() && CaseCmp(language.c_str(), l.code.c_str()) == 0)
            return l.name;
        if (!l.autonym.empty() && CaseCmp(language.c_str(), l.autonym.c_str()) == 0)
            return l.name;
        for (const auto& alias : l.localeAliases)
        {
            const size_t len = alias.size();
            if (len > 0 && language.size() >= len && CaseCmp(language.substr(0, len).c_str(), alias.c_str()) == 0 &&
                (language.size() == len || language[len] == '_' || language[len] == '.' || language[len] == '@' ||
                 language[len] == '-'))
            {
                return l.name;
            }
        }
    }

    if (const LanguageInfo* f = reg.Find(fallback))
        return f->name;
    const auto& langs = reg.Languages();
    return langs.empty() ? std::string("English") : langs.front().name;
}

std::string DetectSystemLanguage()
{
    const auto& langs = LanguageRegistry::Instance().Languages();
#ifdef _WIN32
    const int primary = PRIMARYLANGID(GetUserDefaultUILanguage());
    for (const auto& l : langs)
    {
        if (l.win32PrimaryLang != 0 && l.win32PrimaryLang == primary)
            return l.name;
    }
    return "English";
#else
    const char* locale = std::getenv("LC_MESSAGES");
    if (!locale || !*locale)
        locale = std::getenv("LC_ALL");
    if (!locale || !*locale)
        locale = std::getenv("LANG");
    if (!locale || !*locale)
        return "English";

    const std::string loc(locale);
    for (const auto& l : langs)
    {
        for (const auto& alias : l.localeAliases)
        {
            const size_t len = alias.size();
            if (len > 0 && loc.size() >= len && loc.compare(0, len, alias) == 0 &&
                (loc.size() == len || loc[len] == '_' || loc[len] == '.' || loc[len] == '@'))
                return l.name;
        }
    }
    return "English";
#endif
}

} // namespace CfgLib
