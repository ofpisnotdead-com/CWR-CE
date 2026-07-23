#pragma once

#include <cctype>
#include <Poseidon/Foundation/Strings/RString.hpp>


// Convert a version string like "1.96" to integer 1960.

namespace Poseidon
{
inline int VersionToInt(const char* ptr)
{
    int version = 0;
    while (isdigit(*ptr))
    {
        version *= 10;
        version += *ptr - '0';
        ptr++;
    }
    version *= 1000;
    if (*ptr++ == '.')
    {
        int fr = 100;
        while (fr && isdigit(*ptr))
        {
            version += fr * (*ptr - '0');
            fr /= 10;
            ptr++;
        }
    }
    return version;
}

inline int VersionToInt(const RString& str)
{
    return VersionToInt((const char*)str);
}

// Engine version string (e.g. "1.99"); defined in OptionsUI.cpp.
RString GetAppVersion();

//! The build's version tag with the runtime --dev suffix applied for non-release
//! builds. Resolves BUILD_VERSION_TAG ("release" -> empty, else the tag); when
//! unset, the git sha. This is the string MP join-matching compares.
RString GetVersionTag();

//! Full version string for display: APP_VERSION_TEXT plus the tag
//! (e.g. "3.0", "3.0-rc1", "3.0-d83a-dev").
RString GetVersionString();

//! Full version string for an explicit runtime state. Used by early CLI paths before
//! AppConfig has stored parsed flags.
RString GetVersionStringForState(bool devMode, bool isDemo = false);

//! Pure tag resolution (unit-testable). buildTag = BUILD_VERSION_TAG or nullptr;
//! gitSha = BUILD_GIT_SHA or nullptr; isDemo inserts "-demo"; devMode then appends
//! "-dev" (so a full tag is e.g. "d83a-demo-dev"; either suffix alone when empty).
RString ResolveVersionTag(const char* buildTag, const char* gitSha, bool devMode, bool isDemo = false);

//! Pure compose (unit-testable): baseVersion plus "-<effectiveTag>" when non-empty.
RString ComposeVersionString(const char* baseVersion, const char* effectiveTag);

} // namespace Poseidon
