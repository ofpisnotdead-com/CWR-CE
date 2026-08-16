#pragma once
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <cstring>

// Pure authorization predicates for the dedicated server, extracted verbatim
// from the inline logic in NetworkServerMsgOnMessage.cpp so they can be named,
// reused by the dispatch table, and unit-tested directly.
//
// Most of these are BEHAVIOR-PRESERVING extractions of the inline NCMT* gates.
// AdminLoginPasswordAccepted is the exception: a missing or empty `passwordAdmin`
// now grants no remote admin instead of accepting any password.

namespace Poseidon
{
// True when the dedicated server accepts an admin login for `providedPassword`.
// Requires a configured, non-empty `passwordAdmin` and an exact match: a missing
// or empty entry grants no remote admin.
inline bool AdminLoginPasswordAccepted(const ParamFile& serverCfg, const char* providedPassword)
{
    const ParamEntry* entry = serverCfg.FindEntry("passwordAdmin");
    if (!entry)
    {
        return false;
    }
    RStringB realPassword = *entry;
    if (realPassword.GetLength() == 0)
    {
        return false;
    }
    return strcmp(providedPassword, realPassword.Data()) == 0;
}

// Whether the server will even consider an NCMTLogin: dedicated, and either no game
// master is logged in (`gameMaster == noGameMaster`, i.e. AI_PLAYER) or the current one
// is only a voted admin. Extracted verbatim from the NCMTLogin precondition.
inline bool AdminLoginAllowed(bool dedicated, int gameMaster, bool votedAdmin, int noGameMaster)
{
    return dedicated && (gameMaster == noGameMaster || votedAdmin);
}

// --- Command authorization predicates (the MsgAuth classes from the redesign) ---
// Extracted verbatim from the NCMT* command gates in NetworkServerMsgOnMessage.cpp.
// These name the authorization decisions today scattered across the switch; the
// dispatch table will consult exactly these.

// GameMaster-class commands (mission/restart/reassign/init/monitor/logout): dedicated
// server, sender is the current game master. Behavior-preserving (does NOT add any check).
inline bool CommandFromGameMaster(bool dedicated, int from, int gameMaster)
{
    return dedicated && from == gameMaster;
}

// Password-admin-only commands (shutdown, lockSession): a *voted* admin is excluded.
inline bool CommandFromPasswordAdmin(bool dedicated, int from, int gameMaster, bool votedAdmin)
{
    return dedicated && from == gameMaster && !votedAdmin;
}

// Server-side remoteExec (NMTRemoteExec target == 0) authorization. The server runs the
// supplied script only for the logged-in game master; an unprivileged client must never
// trigger server-side execution. `noGameMaster` is AI_PLAYER at the call site (no admin
// logged in). The relay-to-clients targets (2 / -2 / specific) are unaffected.
inline bool RemoteExecServerAuthorized(int from, int gameMaster, int noGameMaster)
{
    return gameMaster != noGameMaster && from == gameMaster;
}

enum class RemoteExecPolicyMode
{
    DenyClient = 0,
    AllowListed = 1,
    AllowAll = 2,
};

inline bool RemoteExecNameAllowed(const AutoArray<RString>& allowedNames, RString name)
{
    for (int i = 0; i < allowedNames.Size(); ++i)
    {
        if (strcmpi(allowedNames[i], name) == 0)
        {
            return true;
        }
    }
    return false;
}

inline bool RemoteExecClientAuthorized(int from, int gameMaster, int botClient, int noGameMaster,
                                       RemoteExecPolicyMode mode, const AutoArray<RString>& allowedNames, RString name)
{
    if (from == botClient)
    {
        return true;
    }
    if (gameMaster != noGameMaster && from == gameMaster)
    {
        return true;
    }
    if (mode == RemoteExecPolicyMode::AllowAll)
    {
        return true;
    }
    if (mode == RemoteExecPolicyMode::AllowListed)
    {
        return RemoteExecNameAllowed(allowedNames, name);
    }
    return false;
}

// Recompute a player's admin rights bits: clear both admin bits, then if the player is
// the game master set the voted-admin or password-admin bit. `adminBit`/`votedAdminBit`
// are PRAdmin/PRVotedAdmin at the call site (passed in to keep this header light).
// Extracted verbatim from UpdateAdminState.
inline int ComputeAdminRights(int rights, bool isGameMaster, bool votedAdmin, int adminBit, int votedAdminBit)
{
    rights &= ~(adminBit | votedAdminBit);
    if (isGameMaster)
    {
        rights |= votedAdmin ? votedAdminBit : adminBit;
    }
    return rights;
}

// Kick: accepted from the bot client or the current game master.
inline bool CommandFromAdminOrBot(int from, int gameMaster, int botClient)
{
    return from == botClient || from == gameMaster;
}

// NMTSelectPlayer: who may bind player `player` to a person and transfer object
// ownership on its behalf. A client may only act for its own slot; an admin or the
// bot client may assign any player (server-driven setup / JIP). This gates the
// ownership transfer to the acting slot. Note: this gates acting *for another
// player*, not which person a player binds to itself — that is governed by the
// role/spawn system.
inline bool SelectPlayerAuthorized(int from, int player, int gameMaster, int botClient)
{
    return from == player || CommandFromAdminOrBot(from, gameMaster, botClient);
}

// Voting is open only on a dedicated server with no game master logged in
// (`gameMaster == noGameMaster`, where noGameMaster is AI_PLAYER at the call site).
inline bool VotingOpen(bool dedicated, int gameMaster, int noGameMaster)
{
    return dedicated && gameMaster == noGameMaster;
}

// Vote tally test (collective authorization): true once cast votes `sum` exceed the
// `threshold` fraction of the `n` present players. Extracted verbatim from Voting::Check.
inline bool VoteThresholdMet(int sum, int n, float threshold)
{
    return sum > threshold * n;
}

// Selection vote completion: the leading choice can no longer be overtaken once it has
// at least as many votes as the runner-up plus everyone who has not voted yet.
// Extracted verbatim from Voting::Check.
inline bool VoteSelectionComplete(int first, int second, int rest)
{
    return first >= second + rest;
}

// --- Player-role assignment (OnMessagePlayerRole) ---

// Unlocked-path accept condition: may a client move the target slot from `currentRolePlayer`
// to `newPlayer`? Allowed when claiming a free/AI slot for yourself, vacating your own slot,
// or toggling a slot between AI and disabled. aiPlayer/noPlayer are AI_PLAYER/NO_PLAYER at
// the call site. Extracted verbatim (operator precedence preserved).
inline bool RoleSwapAllowed(int currentRolePlayer, int newPlayer, int from, int aiPlayer, int noPlayer)
{
    return (currentRolePlayer == aiPlayer || currentRolePlayer == noPlayer) && newPlayer == from ||
           currentRolePlayer == from && (newPlayer == aiPlayer || newPlayer == noPlayer) ||
           currentRolePlayer == aiPlayer && newPlayer == noPlayer ||
           currentRolePlayer == noPlayer && newPlayer == aiPlayer;
}

inline bool RoleSelfRefreshAllowed(int currentRolePlayer, int newPlayer, int from)
{
    return currentRolePlayer == from && newPlayer == from;
}

inline bool RoleDuplicateClearRequired(int foundIndex, int targetIndex)
{
    return foundIndex >= 0 && foundIndex != targetIndex;
}

inline bool DelayedRoleShouldStartMissionCatchUp(bool acceptedDelayedRole, int serverState, int playerState,
                                                 bool hasRole, int transferMissionState, int briefingState)
{
    return acceptedDelayedRole && serverState >= transferMissionState && serverState <= briefingState &&
           playerState < briefingState && hasRole;
}

inline bool ShouldIgnoreStaleMissionReadyState(int requestedState, int playerState, int serverState, int prepareOkState,
                                               int transferMissionState, int briefingState)
{
    return serverState >= transferMissionState && serverState <= briefingState && playerState >= transferMissionState &&
           requestedState <= prepareOkState;
}

inline bool ShouldBroadcastNetworkMissionState(int targetState, bool joiningInProgress, int playerState,
                                               bool missionParticipant, int transferMissionState, int loadIslandState,
                                               int briefingState, int playState)
{
    if (!missionParticipant && targetState >= transferMissionState)
    {
        return false;
    }
    if (!joiningInProgress)
    {
        return true;
    }
    if (targetState == briefingState)
    {
        return playerState >= loadIslandState;
    }
    if (targetState == playState)
    {
        return playerState >= briefingState;
    }
    return true;
}

inline bool ClientShouldAcceptTransferState(int clientState, int loadIslandState)
{
    return clientState < loadIslandState;
}

inline bool ClientShouldPrepareLoadIsland(int clientState, int loadIslandState)
{
    return clientState < loadIslandState;
}

// Whether a freshly-placed role should be marked locked (a real player took a real, non-own slot).
// Extracted verbatim from OnMessagePlayerRole.
inline bool ShouldLockRole(int player, int from, int aiPlayer, int noPlayer)
{
    return player != aiPlayer && player != noPlayer && player != from;
}

// --- Upload path confinement ---

// True if a path contains a parent-directory escape ("..") sequence. A prefix match
// alone does not confine an upload, since a path can satisfy the sandbox prefix yet
// still contain "..". Conservative — any ".." rejects, which is acceptable for a
// server upload sandbox.
inline bool PathHasParentEscape(const char* path)
{
    return path != nullptr && std::strstr(path, "..") != nullptr;
}

// --- Message relay ---

// Relay eligibility: a peer receives a relayed message when it is not the original sender
// and has reached at least `minState`. Mirrors the `if (dpid==from) continue; if (state<min)
// continue;` guard pair repeated across the relay loops.
inline bool RelayEligible(int playerDpid, int playerState, int from, int minState)
{
    return playerDpid != from && playerState >= minState;
}
} // namespace Poseidon
