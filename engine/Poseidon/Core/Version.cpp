#include <Poseidon/Core/Version.hpp>

#include <cstdio>
#include <cstring>
#include <Poseidon/Core/BuildInfo.hpp>
#include <Poseidon/Foundation/platform.hpp>           // stricmp
#include <Poseidon/Foundation/Platform/AppConfig.hpp> // DevMode
#include <Poseidon/Foundation/Platform/VersionNo.h>   // APP_VERSION_TEXT
#include <Poseidon/Core/Application.hpp>              // GApp->IsDemo()

namespace Poseidon
{

RString ResolveVersionTag(const char* buildTag, const char* gitSha, bool devMode, bool isDemo)
{
    char tag[64] = {0};
    if (buildTag != nullptr && buildTag[0] != '\0')
    {
        // An explicit "release" build carries no visible tag (version is bare 3.00).
        if (stricmp(buildTag, "release") != 0)
            strncpy(tag, buildTag, sizeof(tag) - 1);
    }
    else if (gitSha != nullptr)
    {
        strncpy(tag, gitSha, sizeof(tag) - 1);
    }

    // The demo build marks itself with "-demo", before any --dev suffix.
    if (isDemo)
    {
        char buf[80];
        snprintf(buf, sizeof(buf), "%s%sdemo", tag, tag[0] != '\0' ? "-" : "");
        strncpy(tag, buf, sizeof(tag) - 1);
        tag[sizeof(tag) - 1] = '\0';
    }

    if (devMode)
    {
        // --dev must not be join-compatible with a non-dev server, so the suffix is
        // part of the tag the handshake compares, not just cosmetic.
        char buf[80];
        if (tag[0] != '\0')
            snprintf(buf, sizeof(buf), "%s-dev", tag);
        else
            snprintf(buf, sizeof(buf), "dev");
        return RString(buf);
    }
    return RString(tag);
}

RString ComposeVersionString(const char* baseVersion, const char* effectiveTag)
{
    if (effectiveTag != nullptr && effectiveTag[0] != '\0')
    {
        char buf[96];
        snprintf(buf, sizeof(buf), "%s-%s", baseVersion, effectiveTag);
        return RString(buf);
    }
    return RString(baseVersion);
}

RString GetVersionTag()
{
    const char* buildTag = BuildInfo::VersionTag;
    const char* gitSha = BuildInfo::GitSha;
    const bool isDemo = GApp != nullptr && GApp->IsDemo();
    const bool devMode = !BuildInfo::ReleaseBuild && AppConfig::Instance().DevMode();
    return ResolveVersionTag(buildTag, gitSha, devMode, isDemo);
}

RString GetVersionString()
{
    return ComposeVersionString(APP_VERSION_TEXT, GetVersionTag());
}

RString GetVersionStringForState(bool devMode, bool isDemo)
{
    const bool effectiveDevMode = !BuildInfo::ReleaseBuild && devMode;
    return ComposeVersionString(APP_VERSION_TEXT,
                                ResolveVersionTag(BuildInfo::VersionTag, BuildInfo::GitSha, effectiveDevMode, isDemo));
}

} // namespace Poseidon
