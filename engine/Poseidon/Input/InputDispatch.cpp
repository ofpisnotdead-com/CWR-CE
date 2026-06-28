#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Input/TouchInput.hpp>
#include <Poseidon/Foundation/Types/Memtype.h>

namespace Poseidon
{
} // namespace Poseidon

// SDL bridge — these stay at global scope so SDL event callbacks and
// GameLoop / ServerLoop can call them via plain extern decls.
void ProcessMouse_SDL(DWORD timeDelta);
void ProcessKeyboard_SDL(DWORD sysTime, DWORD timeDelta);
void ProcessJoystick_SDL();
void SetSkipKeys_SDL(bool skip);

void ProcessMouse(DWORD timeDelta)
{
    ProcessMouse_SDL(timeDelta);
}

void ProcessKeyboard(DWORD sysTime, DWORD timeDelta)
{
    ProcessKeyboard_SDL(sysTime, timeDelta);
}

void ProcessJoystick()
{
    ProcessJoystick_SDL();
}

void ProcessTouch(int viewportWidth, int viewportHeight)
{
    Poseidon::TouchInput_ProcessFrame(viewportWidth, viewportHeight);
}

void SetSkipKeys(bool skip)
{
    SetSkipKeys_SDL(skip);
}
