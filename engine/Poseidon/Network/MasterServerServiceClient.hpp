#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <Poseidon/Network/MasterServerBrowser.hpp>
#include <Poseidon/Network/MasterServerProtocol.hpp>

struct cJSON;

namespace Poseidon
{

struct MasterServerServiceSession
{
    std::string app;
    std::string address;
    int hostPort = 0;
    std::string hostName;
    std::string mission;
    std::string transportImplementation;
    int actualVersion = 0;
    int requiredVersion = 0;
    std::string versionTag;
    bool password = false;
    int gameState = 0;
    int ping = 0;
    int numPlayers = 0;
    int maxPlayers = 0;
    int timeLeft = 15;
    int stateElapsedSeconds = 0;
    std::string mod;
    bool equalModRequired = false;
};

struct MasterServerServiceRegistration
{
    std::string app;
    std::string serverId;
    std::string address;
    int hostPort = 0;
    std::string hostName;
    std::string mission;
    std::string transportImplementation;
    int actualVersion = 0;
    int requiredVersion = 0;
    std::string versionTag;
    bool password = false;
    int gameState = 0;
    int numPlayers = 0;
    int maxPlayers = 0;
    int timeLeft = 0;
    int stateElapsedSeconds = 0;
    std::string mod;
    bool equalModRequired = false;
    std::string platform;
    std::string island;
    bool cadetMode = false;
    // Active difficulty flags packed as a bitmask: bit i == difficulty[i]
    // (DifficultyType order, DTArmor=0 .. DTUltraAI=11).
    int difficulty = 0;
    bool joinInProgress = false;
    bool disabledAI = false;
    int respawn = 0;
    int respawnDelay = 0;
    bool sessionLocked = false;
    bool dedicated = false;
    std::string description;
    std::string param1;
    std::string param2;
    std::string requiredAddons;
};

struct MasterServerServicePlayer
{
    std::string name;
    std::string role;
};

struct MasterServerServiceModReference
{
    std::string modId;
    std::string name;
    bool known = false;
    std::string version;
    std::string description;
    std::string homepageUrl;
    std::string downloadUrl;
};

struct MasterServerServiceModCatalogEntry
{
    std::string modId;
    std::string app;
    int actualVersion = 0;
    std::string versionTag;
    bool compatible = false;
    std::string name;
    std::string version;
    std::string folderName;
    std::string description;
    std::vector<std::string> authors;
    std::string homepageUrl;
    std::string downloadUrl;
    int64_t sizeBytes = 0;
};

struct MasterServerServiceModUsageServer
{
    std::string serverId;
    std::string hostName;
    std::string mission;
    int players = 0;
    int maxPlayers = 0;
    bool password = false;
};

struct MasterServerServicePopulationSample
{
    int64_t observedUnixMs = 0;
    int players = 0;
};

struct MasterServerServiceRecentSession
{
    std::string mission;
    std::string label;
    int playedMinutes = 0;
    int peakPlayers = 0;
    int64_t endedUnixMs = 0;
};

struct MasterServerServiceServerDetail
{
    MasterServerServiceSession server;
    std::vector<MasterServerServicePlayer> players;
    std::vector<MasterServerServiceModReference> mods;
    std::vector<MasterServerServicePopulationSample> playerHistory;
    std::vector<MasterServerServiceRecentSession> recentSessions;
};

struct MasterServerServiceEndpoint
{
    bool secure = false;
    std::string scheme;
    std::string host;
    std::string authority;
    int port = 0;
    std::string path;
};

struct MasterServerServiceHttpRequest
{
    std::string url;
    const char* method = nullptr;
    const char* contentType = nullptr;
    std::string body;
    std::string authToken; // sent as "Authorization: Bearer <token>" when non-empty
    std::string userAgent;
};

inline bool TryParseMasterServerAuthority(const std::string& authority, std::string& host, int& port)
{
    host.clear();
    port = 0;
    if (authority.empty())
    {
        return false;
    }

    if (authority.front() == '[')
    {
        const size_t closing = authority.find(']');
        if (closing == std::string::npos || closing == 1)
        {
            return false;
        }

        host = authority.substr(1, closing - 1);
        if (closing + 1 == authority.size())
        {
            return true;
        }
        if (authority[closing + 1] != ':' || closing + 2 >= authority.size())
        {
            return false;
        }

        port = atoi(authority.substr(closing + 2).c_str());
        return port > 0;
    }

    const size_t separator = authority.rfind(':');
    if (separator == std::string::npos || authority.find(':') != separator)
    {
        host = authority;
        return !host.empty();
    }

    host = authority.substr(0, separator);
    port = atoi(authority.substr(separator + 1).c_str());
    return !host.empty() && port > 0;
}

inline std::string UrlEncodeMasterServerServiceValue(const char* value)
{
    if (value == nullptr)
    {
        return {};
    }

    static const char Hex[] = "0123456789ABCDEF";
    std::string encoded;
    for (const unsigned char ch : std::string(value))
    {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' ||
            ch == '_' || ch == '.' || ch == '~')
        {
            encoded.push_back(static_cast<char>(ch));
            continue;
        }

        encoded.push_back('%');
        encoded.push_back(Hex[(ch >> 4) & 0x0F]);
        encoded.push_back(Hex[ch & 0x0F]);
    }

    return encoded;
}

inline void AppendMasterServerServiceQueryParameter(std::string& url, bool& hasQuery, const char* key,
                                                    const std::string& value)
{
    if (value.empty())
    {
        return;
    }

    url.push_back(hasQuery ? '&' : '?');
    hasQuery = true;
    url += key;
    url.push_back('=');
    url += value;
}

inline void AppendMasterServerServiceQueryParameterEvenWhenEmpty(std::string& url, bool& hasQuery, const char* key,
                                                                 const std::string& value)
{
    url.push_back(hasQuery ? '&' : '?');
    hasQuery = true;
    url += key;
    url.push_back('=');
    url += value;
}

inline void AppendMasterServerServiceQueryParameter(std::string& url, bool& hasQuery, const char* key, int value)
{
    AppendMasterServerServiceQueryParameter(url, hasQuery, key, std::to_string(value));
}

inline std::string NormalizeMasterServerServiceBaseUrl(const char* masterServerHost)
{
    std::string url = masterServerHost != nullptr ? masterServerHost : "";
    if (!url.empty() && url.find("://") == std::string::npos)
    {
        url = "https://" + url;
    }
    if (!url.empty() && url.back() == '/')
    {
        url.pop_back();
    }
    return url;
}

inline void AssignMasterServerServiceSession(MasterServerServiceSession& session, const char* address, int hostPort,
                                             const char* hostName, const char* mission,
                                             const char* transportImplementation, int actualVersion,
                                             int requiredVersion, const char* versionTag, bool password, int gameState,
                                             int ping, int numPlayers, int maxPlayers, int timeLeft, const char* mod,
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

inline bool TryGetMasterServerServiceSessionStringField(const MasterServerServiceSession& session, const char* key,
                                                        const char*& value)
{
    if (strcmp(key, MasterServerFieldImplementation) == 0)
    {
        value = session.transportImplementation.c_str();
        return true;
    }
    if (strcmp(key, MasterServerFieldHostName) == 0)
    {
        value = session.hostName.c_str();
        return true;
    }
    if (strcmp(key, MasterServerFieldGameType) == 0)
    {
        value = session.mission.c_str();
        return true;
    }
    if (strcmp(key, MasterServerFieldMod) == 0)
    {
        value = session.mod.c_str();
        return true;
    }
    if (strcmp(key, MasterServerFieldVersionTag) == 0)
    {
        value = session.versionTag.c_str();
        return true;
    }
    return false;
}

inline bool TryGetMasterServerServiceSessionIntField(const MasterServerServiceSession& session, const char* key,
                                                     int& value)
{
    if (strcmp(key, MasterServerFieldPassword) == 0)
    {
        value = session.password ? 1 : 0;
        return true;
    }
    if (strcmp(key, MasterServerFieldHostPort) == 0)
    {
        value = session.hostPort;
        return true;
    }
    if (strcmp(key, MasterServerFieldActualVersion) == 0)
    {
        value = session.actualVersion;
        return true;
    }
    if (strcmp(key, MasterServerFieldRequiredVersion) == 0)
    {
        value = session.requiredVersion;
        return true;
    }
    if (strcmp(key, MasterServerFieldState) == 0)
    {
        value = session.gameState;
        return true;
    }
    if (strcmp(key, MasterServerFieldNumPlayers) == 0)
    {
        value = session.numPlayers;
        return true;
    }
    if (strcmp(key, MasterServerFieldMaxPlayers) == 0)
    {
        value = session.maxPlayers;
        return true;
    }
    if (strcmp(key, MasterServerFieldTimeLeft) == 0)
    {
        value = session.timeLeft;
        return true;
    }
    if (strcmp(key, MasterServerFieldEqualModRequired) == 0)
    {
        value = session.equalModRequired ? 1 : 0;
        return true;
    }
    return false;
}

inline bool IsSuccessfulMasterServerServiceStatus(long statusCode)
{
    return statusCode >= 200 && statusCode < 300;
}
inline bool IsIgnorableMasterServerServiceUnregisterStatus(long statusCode)
{
    return statusCode == 404;
}
bool TryParseMasterServerServiceUrl(const char* url, MasterServerServiceEndpoint& endpoint);
bool TryParseMasterServerProxyAddress(const char* proxyServer, std::string& host, int& port);
bool TryParseMasterServerServiceServerAddress(const char* serverUrl, std::string& address, int& hostPort);
std::string BuildMasterServerServiceServerId(const std::string& address, int hostPort);
void ApplyMasterServerServiceRegistrationFields(MasterServerServiceRegistration& registration, const char* sessionName,
                                                int gameState, bool includeMission, const char* missionName,
                                                int actualVersion, int requiredVersion, const char* versionTag,
                                                bool password, int numPlayers, int maxPlayers, const char* modList,
                                                bool equalModRequired, const char* platform);
bool BuildMasterServerServiceRegistrationFromServerUrl(const char* serverUrl, const char* sessionName, int gameState,
                                                       bool includeMission, const char* missionName, int actualVersion,
                                                       int requiredVersion, const char* versionTag, bool password,
                                                       int numPlayers, int maxPlayers, const char* modList,
                                                       bool equalModRequired, const char* platform,
                                                       MasterServerServiceRegistration& registration);
void AssignMasterServerServiceSessionInfo(const MasterServerServiceSession& source, MasterServerSessionInfo& session);
const char* GetMasterServerServiceSessionStringValue(const MasterServerServiceSession& session, const char* key,
                                                     const char* fallback);
int GetMasterServerServiceSessionIntValue(const MasterServerServiceSession& session, const char* key, int fallback);
int GetMasterServerServiceSessionPing(const MasterServerServiceSession& session);
const char* GetMasterServerServiceSessionAddress(const MasterServerServiceSession& session);
const cJSON* GetMasterServerServiceJsonItem(const cJSON* object, const char* key);
std::string GetMasterServerServiceJsonString(const cJSON* object, const char* key);
int GetMasterServerServiceJsonInt(const cJSON* object, const char* key, int fallback);
int64_t GetMasterServerServiceJsonInt64(const cJSON* object, const char* key, int64_t fallback);
bool GetMasterServerServiceJsonBool(const cJSON* object, const char* key, bool fallback);
std::string BuildMasterServerServiceUserAgent(const char* role);

template <class FetchFn>
bool RefreshMasterServerServiceBrowser(const char* masterServerHost, const MasterServerBrowserFilter& filter,
                                       const char* proxyServer, std::vector<MasterServerServiceSession>& sessions,
                                       MasterServerBrowserState& state, void* instance,
                                       MasterServerBrowserProgressCallback progressCallback, FetchFn&& fetchSessions)
{
    state = MasterServerBrowserState::Querying;
    if (progressCallback != nullptr)
    {
        progressCallback(instance, 0);
    }

    sessions.clear();
    const bool ok = std::forward<FetchFn>(fetchSessions)(masterServerHost, filter, proxyServer, sessions);

    if (progressCallback != nullptr)
    {
        progressCallback(instance, 100);
    }
    state = MasterServerBrowserState::Idle;
    return ok;
}

std::string BuildMasterServerServiceListUrl(const char* masterServerHost, const MasterServerBrowserFilter& filter);
std::string BuildMasterServerServiceResourceUrl(const char* masterServerHost, const char* resourcePath);
std::string BuildMasterServerServiceServerDetailUrl(const char* masterServerHost, const char* serverId);
std::string BuildMasterServerServiceModListUrl(const char* masterServerHost, const char* query);
std::string BuildMasterServerServiceModDetailUrl(const char* masterServerHost, const char* modId);
std::string BuildMasterServerServiceModVersionsUrl(const char* masterServerHost, const char* modId);
std::string BuildMasterServerServiceModServersUrl(const char* masterServerHost, const char* modId);
std::string BuildMasterServerServiceRegisterUrl(const char* masterServerHost);
std::string BuildMasterServerServiceHeartbeatUrl(const char* masterServerHost);
std::string BuildMasterServerServiceUnregisterUrl(const char* masterServerHost, const char* serverId);
std::string BuildMasterServerServiceRegistrationJson(const MasterServerServiceRegistration& registration);
bool TryParseMasterServerServiceSession(const cJSON* object, MasterServerServiceSession& session);
bool TryParseMasterServerServiceModCatalogEntry(const cJSON* object, MasterServerServiceModCatalogEntry& entry);
bool ParseMasterServerServiceModDetailResponse(const char* json, MasterServerServiceModCatalogEntry& detail);
bool ParseMasterServerServiceServerDetailResponse(const char* json, MasterServerServiceServerDetail& detail);
bool ParseMasterServerServiceModListResponse(const char* json, std::vector<MasterServerServiceModCatalogEntry>& mods);
bool ParseMasterServerServiceModUsageResponse(const char* json,
                                              std::vector<MasterServerServiceModUsageServer>& servers);

inline const char* GetMasterServerServicePublishAction(bool heartbeat)
{
    return heartbeat ? "heartbeat" : "register";
}

inline bool BuildMasterServerServicePublishRequest(const char* masterServerHost,
                                                   const MasterServerServiceRegistration& registration, bool heartbeat,
                                                   MasterServerServiceHttpRequest& request)
{
    request = {};
    request.url = heartbeat ? BuildMasterServerServiceHeartbeatUrl(masterServerHost)
                            : BuildMasterServerServiceRegisterUrl(masterServerHost);
    request.method = "POST";
    request.contentType = "application/json";
    request.body = BuildMasterServerServiceRegistrationJson(registration);
    request.userAgent = BuildMasterServerServiceUserAgent(heartbeat ? "server-heartbeat" : "server");
    return !request.url.empty() && !request.body.empty();
}

inline bool BuildMasterServerServiceUnregisterRequest(const char* masterServerHost, const char* serverId,
                                                      MasterServerServiceHttpRequest& request)
{
    request = {};
    request.url = BuildMasterServerServiceUnregisterUrl(masterServerHost, serverId);
    request.method = "DELETE";
    request.userAgent = BuildMasterServerServiceUserAgent("server");
    return !request.url.empty();
}

inline bool BuildMasterServerServiceGetRequest(const std::string& url, MasterServerServiceHttpRequest& request)
{
    request = {};
    request.url = url;
    request.method = "GET";
    request.userAgent = BuildMasterServerServiceUserAgent("client");
    return !request.url.empty();
}

template <class SendFn>
bool ExecuteMasterServerServiceHttpRequest(const MasterServerServiceHttpRequest& request, const char* proxyServer,
                                           std::string& responseBody, long& statusCode, SendFn&& sendRequest)
{
    if (request.url.empty() || request.method == nullptr)
    {
        return false;
    }

    return std::forward<SendFn>(sendRequest)(request.url.c_str(), proxyServer, request.method, request.contentType,
                                             request.body, request.authToken.c_str(), request.userAgent.c_str(),
                                             responseBody, statusCode);
}

template <class ExecuteFn>
bool FetchMasterServerServiceJson(const std::string& url, const char* proxyServer, std::string& responseBody,
                                  ExecuteFn&& executeRequest)
{
    MasterServerServiceHttpRequest request;
    if (!BuildMasterServerServiceGetRequest(url, request))
    {
        responseBody.clear();
        return false;
    }

    long statusCode = 0;
    if (!std::forward<ExecuteFn>(executeRequest)(request, proxyServer, responseBody, statusCode))
    {
        responseBody.clear();
        return false;
    }

    return true;
}

bool ParseMasterServerServiceListResponse(const char* json, std::vector<MasterServerServiceSession>& sessions);

template <class ExecuteFn>
bool FetchMasterServerServiceList(const char* masterServerHost, const MasterServerBrowserFilter& filter,
                                  const char* proxyServer, std::vector<MasterServerServiceSession>& sessions,
                                  ExecuteFn&& executeRequest)
{
    const std::string url = BuildMasterServerServiceListUrl(masterServerHost, filter);
    if (url.empty())
    {
        sessions.clear();
        return false;
    }

    std::string json;
    if (!FetchMasterServerServiceJson(url, proxyServer, json, std::forward<ExecuteFn>(executeRequest)))
    {
        sessions.clear();
        return false;
    }

    return ParseMasterServerServiceListResponse(json.c_str(), sessions);
}

bool FetchMasterServerServiceList(const char* masterServerHost, const MasterServerBrowserFilter& filter,
                                  const char* proxyServer, std::vector<MasterServerServiceSession>& sessions);

template <class ExecuteFn>
bool FetchMasterServerServiceServerDetail(const char* masterServerHost, const char* serverId, const char* proxyServer,
                                          MasterServerServiceServerDetail& detail, ExecuteFn&& executeRequest)
{
    const std::string url = BuildMasterServerServiceServerDetailUrl(masterServerHost, serverId);
    if (url.empty())
    {
        detail = {};
        return false;
    }

    std::string json;
    if (!FetchMasterServerServiceJson(url, proxyServer, json, std::forward<ExecuteFn>(executeRequest)))
    {
        detail = {};
        return false;
    }

    return ParseMasterServerServiceServerDetailResponse(json.c_str(), detail);
}

bool FetchMasterServerServiceServerDetail(const char* masterServerHost, const char* serverId, const char* proxyServer,
                                          MasterServerServiceServerDetail& detail);

template <class ExecuteFn>
bool FetchMasterServerServiceModDetail(const char* masterServerHost, const char* modId, const char* proxyServer,
                                       MasterServerServiceModCatalogEntry& detail, ExecuteFn&& executeRequest)
{
    const std::string url = BuildMasterServerServiceModDetailUrl(masterServerHost, modId);
    if (url.empty())
    {
        detail = {};
        return false;
    }

    std::string json;
    if (!FetchMasterServerServiceJson(url, proxyServer, json, std::forward<ExecuteFn>(executeRequest)))
    {
        detail = {};
        return false;
    }

    return ParseMasterServerServiceModDetailResponse(json.c_str(), detail);
}

template <class ExecuteFn>
bool FetchMasterServerServiceModVersions(const char* masterServerHost, const char* modId, const char* proxyServer,
                                         std::vector<MasterServerServiceModCatalogEntry>& versions,
                                         ExecuteFn&& executeRequest)
{
    const std::string url = BuildMasterServerServiceModVersionsUrl(masterServerHost, modId);
    if (url.empty())
    {
        versions.clear();
        return false;
    }

    std::string json;
    if (!FetchMasterServerServiceJson(url, proxyServer, json, std::forward<ExecuteFn>(executeRequest)))
    {
        versions.clear();
        return false;
    }

    return ParseMasterServerServiceModListResponse(json.c_str(), versions);
}

template <class ExecuteFn>
bool FetchMasterServerServiceModServers(const char* masterServerHost, const char* modId, const char* proxyServer,
                                        std::vector<MasterServerServiceModUsageServer>& servers,
                                        ExecuteFn&& executeRequest)
{
    const std::string url = BuildMasterServerServiceModServersUrl(masterServerHost, modId);
    if (url.empty())
    {
        servers.clear();
        return false;
    }

    std::string json;
    if (!FetchMasterServerServiceJson(url, proxyServer, json, std::forward<ExecuteFn>(executeRequest)))
    {
        servers.clear();
        return false;
    }

    return ParseMasterServerServiceModUsageResponse(json.c_str(), servers);
}

template <class ExecuteFn>
bool FetchMasterServerServiceModList(const char* masterServerHost, const char* query, const char* proxyServer,
                                     std::vector<MasterServerServiceModCatalogEntry>& mods, ExecuteFn&& executeRequest)
{
    const std::string url = BuildMasterServerServiceModListUrl(masterServerHost, query);
    if (url.empty())
    {
        mods.clear();
        return false;
    }

    std::string json;
    if (!FetchMasterServerServiceJson(url, proxyServer, json, std::forward<ExecuteFn>(executeRequest)))
    {
        mods.clear();
        return false;
    }

    return ParseMasterServerServiceModListResponse(json.c_str(), mods);
}

bool FetchMasterServerServiceModList(const char* masterServerHost, const char* query, const char* proxyServer,
                                     std::vector<MasterServerServiceModCatalogEntry>& mods);
bool FetchMasterServerServiceModDetail(const char* masterServerHost, const char* modId, const char* proxyServer,
                                       MasterServerServiceModCatalogEntry& detail);
bool FetchMasterServerServiceModVersions(const char* masterServerHost, const char* modId, const char* proxyServer,
                                         std::vector<MasterServerServiceModCatalogEntry>& versions);
bool FetchMasterServerServiceModServers(const char* masterServerHost, const char* modId, const char* proxyServer,
                                        std::vector<MasterServerServiceModUsageServer>& servers);

// Streams an HTTP GET body straight to destPath (written to "<destPath>.part"
// then atomically renamed on a 2xx response). progress, when non-null, is
// called with (instance, bytesReceived, contentLength) — contentLength is 0
// when the server sends no Content-Length. Returns true only on a fully
// received 2xx download. HTTPS is unsupported on non-Windows (mirrors the JSON
// transport).
using MasterServerServiceDownloadProgressCallback = void (*)(void* instance, int64_t received, int64_t total);

// Polled during the transfer; return true to abort the download promptly (so a
// cancelled download stops instead of running to completion). nullptr = never abort.
using MasterServerServiceDownloadCancelCheck = bool (*)(void* instance);

bool DownloadMasterServerServiceFile(const char* url, const char* proxyServer, const char* destPath, void* instance,
                                     MasterServerServiceDownloadProgressCallback progress,
                                     MasterServerServiceDownloadCancelCheck cancelCheck = nullptr);

template <class FetchFn>
bool RefreshMasterServerServiceBrowserFromDirectory(const char* masterServerHost,
                                                    const MasterServerBrowserFilter& filter, const char* proxyServer,
                                                    std::vector<MasterServerServiceSession>& sessions,
                                                    MasterServerBrowserState& state, void* instance,
                                                    MasterServerBrowserProgressCallback progressCallback,
                                                    FetchFn&& fetchSessions)
{
    return RefreshMasterServerServiceBrowser(
        masterServerHost, filter, proxyServer, sessions, state, instance, progressCallback,
        [&](const char* currentMasterServerHost, const MasterServerBrowserFilter& fetchedFilter,
            const char* fetchedProxyServer, std::vector<MasterServerServiceSession>& fetchedSessions)
        {
            return std::forward<FetchFn>(fetchSessions)(currentMasterServerHost, fetchedFilter, fetchedProxyServer,
                                                        fetchedSessions);
        });
}

inline bool RefreshMasterServerServiceBrowserFromDirectory(const char* masterServerHost,
                                                           const MasterServerBrowserFilter& filter,
                                                           const char* proxyServer,
                                                           std::vector<MasterServerServiceSession>& sessions,
                                                           MasterServerBrowserState& state, void* instance,
                                                           MasterServerBrowserProgressCallback progressCallback)
{
    return RefreshMasterServerServiceBrowserFromDirectory(
        masterServerHost, filter, proxyServer, sessions, state, instance, progressCallback,
        [](const char* currentMasterServerHost, const MasterServerBrowserFilter& fetchedFilter,
           const char* fetchedProxyServer, std::vector<MasterServerServiceSession>& fetchedSessions) {
            return FetchMasterServerServiceList(currentMasterServerHost, fetchedFilter, fetchedProxyServer,
                                                fetchedSessions);
        });
}

bool PublishMasterServerServiceRegistration(const char* masterServerHost, const char* proxyServer,
                                            const MasterServerServiceRegistration& registration, bool heartbeat);
bool UnregisterMasterServerService(const char* masterServerHost, const char* proxyServer, const char* serverId);

// Point the client at a file where it persists the server token issued on register. The
// token is loaded now, sent on register/heartbeat/unregister, and re-saved when rotated.
// Pass nullptr/"" to disable persistence (token kept in memory for the process only).
void SetMasterServerServiceTokenStore(const char* tokenFilePath);

} // namespace Poseidon
