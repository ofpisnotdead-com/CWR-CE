#ifdef _WIN32

#include <windows.h>
#include <cstdio>
#include <cstring>
#include "GameDemoApplication.hpp"
#include <Poseidon/Core/ProgressSystem.hpp>
#include <Poseidon/Foundation/Common/ConsoleUtils.hpp>
#include <Poseidon/Foundation/Platform/CppRuntimeWarning.hpp>
#include <Poseidon/Foundation/Platform/CrashHandler.hpp>

int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR szCmdLine, int sw)
{
    Poseidon::Foundation::WarnIfCppRuntimeIsOlder();
    Poseidon::Foundation::InstallCrashHandler(nullptr);
    if (!strstr(szCmdLine, "--check"))
        Poseidon::Foundation::attachParentConsole();
    GameDemoApplication app;
    return app.Run(hInst, szCmdLine, sw);
}

#else // Linux

#include "GameDemoApplication.hpp"
#include <Poseidon/Foundation/Platform/CrashHandler.hpp>

int main(int argc, char* argv[])
{
    Poseidon::Foundation::InstallCrashHandler(nullptr);
    GameDemoApplication app;
    return app.Run(argc, argv);
}

#endif
