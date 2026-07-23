#pragma once

#include <Poseidon/Network/NetworkImpl.hpp>
#include <Poseidon/Network/NetworkConfig.hpp>
#include <Poseidon/Game/Chat.hpp>
#include <cfloat>
#include <string>
#include <vector>

// Forward declarations shared across the networkServer split files.
// These UI/Game helpers are defined in namespace Poseidon (OptionsUI.cpp /
// DisplayUI.cpp); declare them here unconditionally so they are visible on
// every platform.
namespace Poseidon
{
void SetBaseDirectory(RString dir);
void SetMission(RString world, RString mission, RString subdir);
RString& GetMPMissionsDir();
RString GetUserDirectory();
RString GetMissionDirectory();
std::vector<std::string> GetMPMissionLookupDirectories();
RString ResolveMPMissionTemplateBase(RString mission, RString world);
void RunInitScript();
} // namespace Poseidon

// Helper builders / reporters used across the server-side split files.
inline RString BuildNetworkServerPlayerMessage(const char* format, const char* playerName)
{
    return format != nullptr && playerName != nullptr ? Format(format, playerName) : RString();
}

inline RString BuildNetworkServerValidationMessage(const char* format, const char* playerName, const char* detail)
{
    char message[512];
    snprintf(message, sizeof(message), "%s", "");
    if (format != nullptr && playerName != nullptr)
    {
        snprintf(message, sizeof(message), format, playerName);
        if (detail != nullptr && detail[0] != 0)
        {
            sprintf(message + strlen(message), " - %s", detail);
        }
    }
    return message;
}

inline void ReportNetworkServerGlobalMessage(const char* senderName, const RString& message)
{
    RefArray<NetworkObject> dummy;
    GNetworkManager.Chat(CCGlobal, senderName != nullptr ? RString(senderName) : RString(), dummy, message);
    GChatList.Add(CCGlobal, senderName != nullptr ? RString(senderName) : RString(), message, false, true);
}

struct NetworkMissionParams
{
    float param1;
    float param2;
};

inline NetworkMissionParams ResolveNetworkMissionParams(float currentParam1, float currentParam2, float defaultParam1,
                                                        float defaultParam2)
{
    return {
        currentParam1 == FLT_MAX ? defaultParam1 : currentParam1,
        currentParam2 == FLT_MAX ? defaultParam2 : currentParam2,
    };
}

namespace Poseidon
{
inline int SelectNetworkVoiceTargetPlayerId(int dpid, int)
{
    return dpid;
}

inline bool ShouldRefreshOtherNetworkVoiceRoute(int routeOwner, int changedPlayer, int ownerState, int ownerChannel,
                                                int minimumState, int noneChannel)
{
    return routeOwner != changedPlayer && ownerState >= minimumState && ownerChannel != noneChannel;
}

inline bool ShouldNotifyJoinedPlayerAboutActiveDirectSpeaker(int routeOwner, int changedPlayer, int ownerState,
                                                             int ownerChannel, int minimumState, int directChannel)
{
    return routeOwner != changedPlayer && ownerState >= minimumState && ownerChannel == directChannel;
}
} // namespace Poseidon

// Free functions defined in networkServer.cpp
bool IsUpdateTransport(NetworkMessageType type);
QFBank* FindBank(const char* prefix);
void LoadBanList(RString filename, FindArray<__int64>& list);
void SaveBanList(RString filename, const FindArray<__int64>& list);
// Dynamic (runtime) ban-entry count on the local server, or -1 if not a server.
int GetServerBanCount();
// First runtime id-ban entry as a decimal string on the local server, or "" if none / not a server.
RString GetServerFirstBanId();
// Whether the local server session is locked (rejecting new connections).
bool GetServerLocked();
void AddMissionList(NetworkCommandMessage& answer);
