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
#include <SDL3/SDL_hints.h>
#include <Poseidon/Core/GameDataInstall.hpp>          // GameDataDir
#include <Poseidon/UI/Controls/IosGameDataGateScreen.hpp> // RunIosGameDataGate
#include <cstring>
#include <string>
#include <vector>
#define POSEIDON_IOS_AUTO_WORKDIR 1

namespace
{
bool HasArg(int argc, char* argv[], const char* name)
{
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], name) == 0)
            return true;
    }
    return false;
}

}
#endif
#endif

int main(int argc, char* argv[])
{
    Poseidon::Foundation::InstallCrashHandler(nullptr);
    GameApplication app;

#ifdef POSEIDON_IOS_AUTO_WORKDIR
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");

    // A normal Home Screen launch passes no arguments at all, so the game
    // must find its own bundled data without a -C flag. Game assets are
    // bundled directly into the .app (see apps/cwr/Game/CMakeLists.txt),
    // and SDL_GetBasePath() returns that bundle's root on iOS -- inject it
    // as -C unless the caller (e.g. a devicectl test launch) already
    // passed an explicit one.
    const bool hasWorkDirArg = HasArg(argc, argv, "-C") || HasArg(argc, argv, "--work-dir");
    const bool hasSplashArg = HasArg(argc, argv, "--splash") || HasArg(argc, argv, "--no-splash");
    const bool hasFpsArg = HasArg(argc, argv, "--fps") || HasArg(argc, argv, "--show-fps") || HasArg(argc, argv, "--no-fps");
    const bool hasLogFileArg = HasArg(argc, argv, "--log-file");

    std::vector<char*> iosArgv(argv, argv + argc);
    std::string iosLogPath;
    std::string iosPrefPath;
    if (char* prefPath = SDL_GetPrefPath("ColdWarAssault", "CWR"))
    {
        iosPrefPath = prefPath;
        SDL_free(prefPath);
    }

    if (!hasSplashArg)
        iosArgv.push_back(const_cast<char*>("--no-splash"));
    if (!hasFpsArg)
        iosArgv.push_back(const_cast<char*>("--show-fps"));
    if (!hasLogFileArg && !iosPrefPath.empty())
    {
        iosLogPath = iosPrefPath + "ios-launch.log";
        iosArgv.push_back(const_cast<char*>("--log-file"));
        iosArgv.push_back(iosLogPath.data());
    }

    std::string iosGameDataDir;
    if (!hasWorkDirArg)
    {
#if POSEIDON_IOS_BUNDLE_GAME_DATA
        // Dev/simulator convenience build: game data is baked straight into
        // the .app bundle (apps/cwr/Game/CMakeLists.txt), and
        // SDL_GetBasePath() returns that bundle's root on iOS.
        if (const char* basePath = SDL_GetBasePath())
        {
            iosArgv.push_back(const_cast<char*>("-C"));
            iosArgv.push_back(const_cast<char*>(basePath));
        }
#else
        // Distributable/App Store build: the licensed game data isn't in the
        // bundle at all -- block here (real UIKit, safe from inside SDL's
        // iOS main() call stack via a nested run-loop pump; see
        // IosGameDataGateScreen.mm) until the on-device import/download gate
        // reports the data is Ready, then point -C at where it landed.
        Poseidon::RunIosGameDataGate();
        iosGameDataDir = Poseidon::GameDataDir();
        iosArgv.push_back(const_cast<char*>("-C"));
        iosArgv.push_back(const_cast<char*>(iosGameDataDir.c_str()));
#endif
    }
    return app.Run(static_cast<int>(iosArgv.size()), iosArgv.data());
#else
    return app.Run(argc, argv);
#endif
}

#endif
