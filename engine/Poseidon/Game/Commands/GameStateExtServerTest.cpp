#include <Evaluator/express.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Network/NetworkServerCommon.hpp>
#include <cstdio>
#include <cstdlib>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>

// Server-safe Trident test verbs. These are GL/UI-free (network state + clean exit only),
// so the dedicated server (Core-only, no graphics) can register them via RegisterServerTriCommands
// without dragging in the client-only command module's GL/UI dependencies. The full client
// registers the same handlers from its INIT_MODULE in GameStateExtTestAudio.cpp.

/// triEndTest — request a clean shutdown. Duplicate calls and an already-requested
/// failure shutdown cannot convert a failure into a successful test end.
GameValue TriEndTest(const GameState* /*state*/)
{
    if (GApp)
    {
        const bool cleanEndRequest = !GApp->m_closeRequest;
        if (cleanEndRequest)
        {
            GApp->m_cleanTestEndRequested = true;
            LOG_INFO(Core, "[tri] triEndTest — requesting clean shutdown");
        }
        else
            LOG_INFO(Core, "[tri] triEndTest — shutdown already requested with exit code {}, preserving",
                     GApp->m_exitCode);
        GApp->m_closeRequest = true;
    }
    else
    {
        _exit(0);
    }
    return GameValue();
}

/// triNetCommand <command> — run a chat command (e.g. "#login x") through the real
/// NetworkManager::ProcessCommand path, exactly as typing it in the chat box does. Returns "1"
/// if the command was recognized/dispatched, "0" otherwise (and "0" on the dedicated server,
/// which has no client). Lets a test drive the stock #login admin path end to end.
GameValue TriNetCommand(const GameState* /*state*/, GameValuePar arg)
{
    RString command = static_cast<GameStringType>(arg);
    bool ok = false;
    try
    {
        ok = GetNetworkManager().ProcessCommand(command);
    }
    catch (...)
    {
    }
    return GameValue(ok ? "1" : "0");
}

/// triServerBanCount — number of runtime ban entries (local id + IP lists) held by the
/// local server, or -1 when this process is not a server. Lets a test observe the ban
/// mechanism directly: #ban raises it, #unban lowers it, independent of any client's view.
GameValue TriServerBanCount(const GameState* /*state*/)
{
    return GameValue(static_cast<float>(GetServerBanCount()));
}

/// triServerLocked — whether the local server session is locked (rejecting new joiners),
/// as a "1"/"0" string. Lets a test observe the #lock / #unlock admin path directly.
GameValue TriServerLocked(const GameState* /*state*/)
{
    return GameValue(GetServerLocked() ? "1" : "0");
}

/// triServerBanFirstId — the first runtime id-ban entry on the local server as a decimal
/// string, or "" when none. Lets a test read back the exact id the server recorded so it
/// can drive #unban with it (the banned player is gone, so only its id remains).
GameValue TriServerBanFirstId(const GameState* /*state*/)
{
    return GameValue(GetServerFirstBanId());
}

// Defined in GameStateExtTestGeneric.cpp — a pure value comparison with no GL/UI
// dependencies, so it links into the Core-only dedicated server for server-role asserts.
GameValue TriAssertEq(const GameState*, GameValuePar arg);

/// Register the server-safe Trident verbs into the global game state. Called explicitly by the
/// dedicated server once its GameState exists; the client registers them via its INIT_MODULE.
void RegisterServerTriCommands()
{
    GGameState.NewNularOp(GameNular(GameNothing, "triEndTest", TriEndTest));
    GGameState.NewFunction(GameFunction(GameString, "triNetCommand", TriNetCommand, GameString));
    GGameState.NewNularOp(GameNular(GameScalar, "triServerBanCount", TriServerBanCount));
    GGameState.NewNularOp(GameNular(GameString, "triServerBanFirstId", TriServerBanFirstId));
    GGameState.NewNularOp(GameNular(GameString, "triServerLocked", TriServerLocked));
    GGameState.NewFunction(GameFunction(GameString, "triAssertEq", TriAssertEq, GameArray));
}
