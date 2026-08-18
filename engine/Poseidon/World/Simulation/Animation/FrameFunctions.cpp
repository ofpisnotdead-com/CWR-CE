#include <Poseidon/Foundation/Platform/AppFrameExt.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Dev/Debug/DebugTrap.hpp>
#include <SDL3/SDL_mouse.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>

extern void DDTerm();

namespace Poseidon
{
using namespace Dev;
} // namespace Poseidon

#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Strings/Bstring.hpp>

#include <SDL3/SDL.h>

namespace Poseidon
{

#define ENABLE_WARNINGS 1
#define SINGLE_WARNING 1

static Foundation::ErrorMessageLevel WarningLevel = Foundation::EMNote;
static RString WarningText;

Foundation::ErrorMessageLevel GetMaxError()
{
    return WarningLevel;
}

RString GetMaxErrorMessage()
{
    return WarningText;
}

void ResetErrors()
{
    WarningLevel = Foundation::EMNote;
    WarningText = RString();
}

} // namespace Poseidon

namespace Poseidon
{
void WarningMessageLevel(Foundation::ErrorMessageLevel level, const char* format, va_list argptr)
{
#if SINGLE_WARNING
    if (WarningLevel >= level)
    {
        return;
    }
    // InitVehicles refuses a mission once this reads EMError, so the latch records the level
    // actually reached.
    WarningLevel = level;
#endif

    static int avoidRecursion = 0;
    if (avoidRecursion > 0)
    {
        return;
    }

    avoidRecursion++;
    GDebugger.NextAliveExpected(15 * 60 * 1000);

    BString<256> buf;
    vsprintf(buf, format, argptr);

#if SINGLE_WARNING
    WarningText = buf.cstr();
#endif

    Foundation::WarningMessage("Warning Message: %s", (const char*)buf);

    avoidRecursion--;
}

void EnableDesktopCursor(bool enable)
{
    if (ENGINE_CONFIG.hideCursor)
    {
        if (enable)
            SDL_ShowCursor();
        else
            SDL_HideCursor();
    }
}

void CWRFrameFunctions::ErrorMessage(const char* format, va_list argptr)
{
    static int avoidRecursion = 0;
    if (avoidRecursion++ != 0)
    {
        return;
    }

    BString<256> buf;
    vsprintf(buf, format, argptr);

    // Critical engine error. Fatal under --strict (tests catch it); under --no-strict
    // (the rwdi/rel build players run) it is logged and the app keeps running instead
    // of exit(1). The ERROR log also trips the strict latch under --strict.
    LOG_ERROR(Physics, "Critical error: {}", (const char*)buf);

    if (!Foundation::LoggingSystem::IsStrictMode())
    {
        avoidRecursion = 0; // a later, distinct critical error must still report
        return;
    }

    GDebugger.NextAliveExpected(15 * 60 * 1000);
    DDTerm();
    if (ENGINE_CONFIG.hideCursor)
    {
        SDL_ShowCursor();
    }
    fprintf(stderr, "FATAL: %s\n", (const char*)buf);
    exit(1);
}

void CWRFrameFunctions::WarningMessage(const char* format, va_list argptr)
{
    WarningMessageLevel(Foundation::EMWarning, format, argptr);
}

void CWRFrameFunctions::ErrorMessage(Foundation::ErrorMessageLevel level, const char* format, va_list argptr)
{
    if (level <= Foundation::EMError)
    {
        WarningMessageLevel(level, format, argptr);
    }
    else
    {
        ErrorMessage(format, argptr);
    }
}
} // namespace Poseidon
