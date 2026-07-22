#include <Poseidon/Foundation/Platform/StartupError.hpp>

#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <Poseidon/Foundation/Platform/AppConfig.hpp>

#include <SDL3/SDL_messagebox.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace Poseidon::Foundation
{
namespace
{
bool EnvSet(const char* name)
{
    const char* v = std::getenv(name);
    return v && v[0];
}

bool TestHarnessEnv()
{
    const char* v = std::getenv("POSEIDON_TEST");
    return v && v[0] && strcmp(v, "0") != 0 && strcmp(v, "false") != 0 && strcmp(v, "off") != 0;
}

// The user-content folder holding the log/crash files. Resolves the same folder
// without the singleton, for failures before GamePaths is initialized.
std::string DiagnosticsDir()
{
    if (GamePaths::Instance().IsInitialized())
        return GamePaths::Instance().UserContentDir();
    return GamePaths::ResolveUserContentDir("CWR", "Cold War Assault");
}
} // namespace

bool HasInteractiveConsole()
{
#ifdef _WIN32
    return GetConsoleWindow() != nullptr;
#else
    return isatty(STDERR_FILENO) != 0;
#endif
}

bool ShouldWriteAutoLog()
{
    if (TestHarnessEnv())
        return false;
    const AppConfig& cfg = AppConfig::Instance();
    if (cfg.CheckInitAndExit() || cfg.GetHarnessPort() >= 0)
        return false;
    // Screenshot capture-and-exit runs headless with a display but no user, so a
    // blocking dialog would hang it.
    if (!cfg.GetScreenshotPath().empty() || !cfg.GetAutoScreenshot().empty())
        return false;
    return !HasInteractiveConsole();
}

bool ShouldShowGuiError()
{
    if (!ShouldWriteAutoLog())
        return false;
#ifndef _WIN32
    // Headless (no display): the SDL box would fail; the stderr write covers it.
    if (!EnvSet("DISPLAY") && !EnvSet("WAYLAND_DISPLAY"))
        return false;
#endif
    return true;
}

void ShowStartupError(const char* title, const char* message)
{
    if (!title || !title[0])
        title = "Startup Error";
    if (!message || !message[0])
        message = "The game could not start.";

    const char* logPath = LoggingSystem::GetLogFilePath();
    const std::string diagDir = DiagnosticsDir();

    // diagDir carries a trailing separator, so append leaf names directly.
    std::string body = message;
    body += "\n\n";
    if (logPath && logPath[0])
        body += std::string("Log file:\n") + logPath;
    else
        body += "Logs:\n" + diagDir + "logs";
    body += "\n\n";
    body += "Crash reports:\n" + diagDir + "crashes";

    fprintf(stderr, "\n%s\n%s\n\n", title, body.c_str());
    fflush(stderr);

    if (ShouldShowGuiError())
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, body.c_str(), nullptr);
}
} // namespace Poseidon::Foundation
