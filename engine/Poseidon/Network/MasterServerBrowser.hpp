#pragma once

#include <type_traits>
#include <utility>
#include <cstdint>
#include <string>
#include <vector>

#include <Poseidon/Network/MasterServerProtocol.hpp>

#include <Poseidon/Foundation/Strings/RString.hpp>

namespace Poseidon
{

struct MasterServerServiceModPackage
{
    std::string modId;
    int64_t packageRevision = 1;
};

struct MasterServerBrowser;

enum class MasterServerBrowserState
{
    Idle,
    ListTransfer,
    LanList,
    Querying,
};

struct MasterServerSessionInfo
{
    const char* address = "";
    int hostPort = 0;
    const char* hostName = "";
    const char* mission = "";
    const char* transportImplementation = "";
    int actualVersion = 0;
    int requiredVersion = 0;
    const char* versionTag = "";
    bool password = false;
    int gameState = 0;
    int ping = 0;
    int numPlayers = 0;
    int maxPlayers = 0;
    int timeLeft = 15;
    const char* mod = "";
    const std::vector<MasterServerServiceModPackage>* modPackages = nullptr;
    bool equalModRequired = false;
};

struct MasterServerBrowserFilter
{
    const char* serverName = "";
    const char* missionName = "";
    int minPlayers = 0;
    int maxPlayers = 0;
    bool includeFullServers = true;
};

enum class MasterServerBrowserFilterTermKind
{
    ServerName,
    MissionName,
    MinPlayers,
    MaxPlayers,
    ExcludeFullServers,
};

struct MasterServerBrowserFilterTerm
{
    MasterServerBrowserFilterTermKind kind;
    const char* text = nullptr;
    int number = 0;
};

using MasterServerBrowserProgressCallback = void (*)(void* instance, int progressPercent);

template <class Sessions>
auto* GetMasterServerServiceSessionAt(Sessions& sessions, int index)
{
    if (index < 0 || index >= static_cast<int>(sessions.size()))
    {
        return static_cast<typename Sessions::value_type*>(nullptr);
    }

    return &sessions[static_cast<size_t>(index)];
}

template <class Browser, class GetSessionsFn>
auto* GetMasterServerBrowserServiceSession(Browser* browser, int index, GetSessionsFn&& getSessions)
{
    if (browser == nullptr)
    {
        using Sessions = std::remove_reference_t<decltype(std::forward<GetSessionsFn>(getSessions)(*browser))>;
        return static_cast<typename Sessions::value_type*>(nullptr);
    }

    return GetMasterServerServiceSessionAt(std::forward<GetSessionsFn>(getSessions)(*browser), index);
}

inline void AssignMasterServerSessionInfo(MasterServerSessionInfo& session, const char* address, int hostPort,
                                          const char* hostName, const char* mission,
                                          const char* transportImplementation, int actualVersion, int requiredVersion,
                                          const char* versionTag, bool password, int gameState, int ping,
                                          int numPlayers, int maxPlayers, int timeLeft, const char* mod,
                                          bool equalModRequired)
{
    session.address = address != nullptr ? address : "";
    session.hostPort = hostPort;
    session.hostName = hostName != nullptr ? hostName : "";
    session.mission = mission != nullptr ? mission : "";
    session.transportImplementation = transportImplementation != nullptr ? transportImplementation : "";
    session.actualVersion = actualVersion;
    session.requiredVersion = requiredVersion;
    session.versionTag = versionTag != nullptr ? versionTag : "";
    session.password = password;
    session.gameState = gameState;
    session.ping = ping;
    session.numPlayers = numPlayers;
    session.maxPlayers = maxPlayers;
    session.timeLeft = timeLeft;
    session.mod = mod != nullptr ? mod : "";
    session.equalModRequired = equalModRequired;
}

const char* GetMasterServerBrowserProxyServer(const RString& proxyServer);

const RString& GetMasterServerBrowserFilterConjunction();
void AppendMasterServerBrowserFilterCondition(RString& filter, RString condition);
RString BuildMasterServerBrowserFilterCondition(const MasterServerBrowserFilterTerm& term);
RString BuildMasterServerBrowserFilterString(const MasterServerBrowserFilter& filter);

template <class VisitFn>
void VisitMasterServerBrowserFilterTerms(const MasterServerBrowserFilter& filter, VisitFn&& visit)
{
    if (filter.serverName != nullptr && filter.serverName[0] != 0)
    {
        std::forward<VisitFn>(visit)({MasterServerBrowserFilterTermKind::ServerName, filter.serverName, 0});
    }
    if (filter.missionName != nullptr && filter.missionName[0] != 0)
    {
        std::forward<VisitFn>(visit)({MasterServerBrowserFilterTermKind::MissionName, filter.missionName, 0});
    }
    if (filter.minPlayers > 0)
    {
        std::forward<VisitFn>(visit)({MasterServerBrowserFilterTermKind::MinPlayers, nullptr, filter.minPlayers});
    }
    if (filter.maxPlayers > 0)
    {
        std::forward<VisitFn>(visit)({MasterServerBrowserFilterTermKind::MaxPlayers, nullptr, filter.maxPlayers});
    }
    if (!filter.includeFullServers)
    {
        std::forward<VisitFn>(visit)({MasterServerBrowserFilterTermKind::ExcludeFullServers, nullptr, 0});
    }
}

MasterServerBrowser* CreateMasterServerBrowser(const char* masterServerHost, void* instance,
                                               MasterServerBrowserProgressCallback progressCallback);
void DestroyMasterServerBrowser(MasterServerBrowser* browser);

MasterServerBrowserState GetMasterServerBrowserState(MasterServerBrowser* browser);
void HaltMasterServerBrowser(MasterServerBrowser* browser);
void ClearMasterServerBrowser(MasterServerBrowser* browser);
void UpdateMasterServerBrowser(MasterServerBrowser* browser, const MasterServerBrowserFilter& filter);
void ThinkMasterServerBrowser(MasterServerBrowser* browser);

int GetMasterServerBrowserCount(MasterServerBrowser* browser);
bool TryGetMasterServerBrowserSession(MasterServerBrowser* browser, int index, MasterServerSessionInfo& session);
const char* GetMasterServerBrowserStringValue(MasterServerBrowser* browser, int index, const char* key,
                                              const char* fallback);
int GetMasterServerBrowserIntValue(MasterServerBrowser* browser, int index, const char* key, int fallback);
int GetMasterServerBrowserPing(MasterServerBrowser* browser, int index);
const char* GetMasterServerBrowserAddress(MasterServerBrowser* browser, int index);

} // namespace Poseidon
