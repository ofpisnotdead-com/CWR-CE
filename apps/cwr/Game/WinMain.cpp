#ifdef _WIN32

#include <windows.h>
#include <cstring>
#include "GameApplication.hpp"
#include <Poseidon/Core/ProgressSystem.hpp> // Needed for complete type in Application
#include <Poseidon/Foundation/Common/ConsoleUtils.hpp>
#include <Poseidon/Foundation/Platform/CrashHandler.hpp>

int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR szCmdLine, int sw)
{
    Poseidon::Foundation::InstallCrashHandler(nullptr);
    if (!strstr(szCmdLine, "--check"))
        Poseidon::Foundation::attachParentConsole();
    GameApplication app;
    return app.Run(hInst, szCmdLine, sw);
}

#else // Linux/macOS/iOS

#include "GameApplication.hpp"
#include <Poseidon/Foundation/Platform/CrashHandler.hpp>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
// iOS requires the app entry point to go through SDL3's own UIKit shim
// rather than a bare main() -- see apps/tools/MetalSmokeTest/main.cpp for
// the same requirement on the Metal smoke test.
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_filesystem.h>
#include <cstring>
#include <vector>
#define POSEIDON_IOS_AUTO_WORKDIR 1
#endif
#endif

int main(int argc, char* argv[])
{
    Poseidon::Foundation::InstallCrashHandler(nullptr);
    GameApplication app;

#ifdef POSEIDON_IOS_AUTO_WORKDIR
    // A normal Home Screen launch passes no arguments at all, so the game
    // must find its own bundled data without a -C flag. Game assets are
    // bundled directly into the .app (see apps/cwr/Game/CMakeLists.txt),
    // and SDL_GetBasePath() returns that bundle's root on iOS -- inject it
    // as -C unless the caller (e.g. a devicectl test launch) already
    // passed an explicit one.
    bool hasWorkDirArg = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "-C") == 0 || std::strcmp(argv[i], "--work-dir") == 0)
        {
            hasWorkDirArg = true;
            break;
        }
    }

    std::vector<char*> iosArgv(argv, argv + argc);
    if (!hasWorkDirArg)
    {
        if (const char* basePath = SDL_GetBasePath())
        {
            iosArgv.push_back(const_cast<char*>("-C"));
            iosArgv.push_back(const_cast<char*>(basePath));
        }
    }
    return app.Run(static_cast<int>(iosArgv.size()), iosArgv.data());
#else
    return app.Run(argc, argv);
#endif
}

#endif
