#ifdef _WIN32

#include <windows.h>
#include <cstring>
#include "GameApplication.hpp"
#include <Poseidon/Core/ProgressSystem.hpp> // Needed for complete type in Application
#include <Poseidon/Foundation/Common/ConsoleUtils.hpp>
#include <Poseidon/Foundation/Platform/CppRuntimeWarning.hpp>
#include <Poseidon/Foundation/Platform/CrashHandler.hpp>

int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR szCmdLine, int sw)
{
    Poseidon::Foundation::WarnIfCppRuntimeIsOlder();
    Poseidon::Foundation::InstallCrashHandler(nullptr);
    if (!strstr(szCmdLine, "--check"))
        Poseidon::Foundation::attachParentConsole();
    GameApplication app;
    return app.Run(hInst, szCmdLine, sw);
}

#else // Linux

#include "GameApplication.hpp"
#include <Poseidon/Foundation/Platform/CrashHandler.hpp>

int main(int argc, char* argv[])
{
    Poseidon::Foundation::InstallCrashHandler(nullptr);
    GameApplication app;
    return app.Run(argc, argv);
}

#endif
