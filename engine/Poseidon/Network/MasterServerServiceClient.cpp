#include <Poseidon/Network/MasterServerServiceClient.hpp>

#include <Poseidon/Core/Version.hpp>
#include <Poseidon/Network/MasterServerProtocol.hpp>
#include <Poseidon/Network/HttpTestRewrite.hpp>
#include <Poseidon/Network/NetworkConfig.hpp>
#include <Poseidon/Network/XML/Xml.hpp>
#include <Poseidon/Foundation/Platform/VersionNo.h>
#include <cjson/cJSON.h>

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <curl/system.h>
#include <Poseidon/Foundation/Framework/Log.hpp>

#if defined(_WIN32)
#include <windows.h>
#include <wininet.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <curl/curl.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string_view>

namespace Poseidon
{

namespace
{
const cJSON* GetObjectItem(const cJSON* object, const char* key)
{
    return object != nullptr ? cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(object), key) : nullptr;
}

std::string GetStringValue(const cJSON* object, const char* key)
{
    const cJSON* item = GetObjectItem(object, key);
    return cJSON_IsString(item) && item->valuestring != nullptr ? item->valuestring : "";
}

int GetIntValue(const cJSON* object, const char* key, int fallback)
{
    const cJSON* item = GetObjectItem(object, key);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

bool GetBoolValue(const cJSON* object, const char* key, bool fallback)
{
    const cJSON* item = GetObjectItem(object, key);
    if (cJSON_IsBool(item))
    {
        return cJSON_IsTrue(item);
    }
    return fallback;
}

void FreeDownloadedBuffer(char* buffer)
{
    if (buffer == nullptr)
    {
        return;
    }
#ifdef _WIN32
    GlobalFree(buffer);
#else
    free(buffer);
#endif
}

} // namespace

const cJSON* GetMasterServerServiceJsonItem(const cJSON* object, const char* key)
{
    return GetObjectItem(object, key);
}

std::string GetMasterServerServiceJsonString(const cJSON* object, const char* key)
{
    return GetStringValue(object, key);
}

int GetMasterServerServiceJsonInt(const cJSON* object, const char* key, int fallback)
{
    return GetIntValue(object, key, fallback);
}

int64_t GetMasterServerServiceJsonInt64(const cJSON* object, const char* key, int64_t fallback)
{
    const cJSON* item = GetObjectItem(object, key);
    return cJSON_IsNumber(item) ? static_cast<int64_t>(item->valuedouble) : fallback;
}

bool GetMasterServerServiceJsonBool(const cJSON* object, const char* key, bool fallback)
{
    return GetBoolValue(object, key, fallback);
}

bool TryParseMasterServerServiceUrl(const char* url, MasterServerServiceEndpoint& parsed)
{
    parsed = {};
    if (url == nullptr)
    {
        return false;
    }

    std::string value = url;
    const size_t schemePos = value.find("://");
    if (schemePos == std::string::npos)
    {
        return false;
    }

    parsed.scheme = value.substr(0, schemePos);
    parsed.secure = parsed.scheme == "https";
    if (parsed.scheme != "http" && parsed.scheme != "https")
    {
        return false;
    }

    size_t authorityStart = schemePos + 3;
    size_t pathStart = value.find_first_of("/?", authorityStart);
    std::string authority = pathStart == std::string::npos ? value.substr(authorityStart)
                                                           : value.substr(authorityStart, pathStart - authorityStart);
    if (pathStart == std::string::npos)
    {
        parsed.path = "/";
    }
    else if (value[pathStart] == '?')
    {
        parsed.path = "/" + value.substr(pathStart);
    }
    else
    {
        parsed.path = value.substr(pathStart);
    }
    if (authority.empty())
    {
        return false;
    }

    parsed.authority = authority;
    parsed.port = parsed.secure ? 443 : 80;
    int explicitPort = 0;
    if (!TryParseMasterServerAuthority(authority, parsed.host, explicitPort))
    {
        return false;
    }
    if (explicitPort > 0)
    {
        parsed.port = explicitPort;
    }

    if (parsed.host.empty() || parsed.port <= 0)
    {
        return false;
    }

    return true;
}

bool TryParseMasterServerProxyAddress(const char* proxyServer, std::string& host, int& port)
{
    host.clear();
    port = 80;
    if (proxyServer == nullptr || proxyServer[0] == 0)
    {
        return false;
    }

    std::string value = proxyServer;
    int explicitPort = 0;
    if (!TryParseMasterServerAuthority(value, host, explicitPort))
    {
        return false;
    }
    if (explicitPort > 0)
    {
        port = explicitPort;
    }
    return true;
}

namespace
{
constexpr long kDownloadConnectTimeoutSeconds = 10;
constexpr long kDownloadLowSpeedBytesPerSecond = 1;
constexpr long kDownloadLowSpeedTimeoutSeconds = 30;
constexpr long kHttpOk = 200;
constexpr long kHttpPartialContent = 206;
constexpr long kHttpRangeNotSatisfiable = 416;
constexpr std::array<long, 7> kTransientDownloadStatuses = {408, 425, 429, 500, 502, 503, 504};
constexpr size_t kMaximumDownloadValidatorLength = 1024;

size_t AppendResponseBody(char* data, size_t size, size_t count, void* userdata)
{
    const size_t total = size * count;
    static_cast<std::string*>(userdata)->append(data, total);
    return total;
}

// Single cross-platform HTTP/HTTPS client (libcurl) for register / heartbeat / browse.
// curl negotiates TLS via its backend (Schannel on Windows, OpenSSL elsewhere), so https
// works on every build.
bool SendMasterServerServiceRequest(const char* url, const char* proxyServer, const char* method,
                                    const char* contentType, const std::string& body, const char* authToken,
                                    const char* userAgent, std::string& responseBody, long& statusCode)
{
    responseBody.clear();
    statusCode = 0;
    if (url == nullptr || method == nullptr)
    {
        return false;
    }

    static std::once_flag curlInitFlag;
    std::call_once(curlInitFlag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });

    CURL* curl = curl_easy_init();
    if (curl == nullptr)
    {
        return false;
    }

    const std::string defaultUserAgent = BuildMasterServerServiceUserAgent(nullptr);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                     userAgent != nullptr && userAgent[0] != 0 ? userAgent : defaultUserAgent.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, AppendResponseBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);

    RString bindAddress = GetNetworkBindAddress();
    if (bindAddress.GetLength() > 0 && strcmp((const char*)bindAddress, "0.0.0.0") != 0)
    {
        curl_easy_setopt(curl, CURLOPT_INTERFACE, (const char*)bindAddress);
    }

    if (proxyServer != nullptr && proxyServer[0] != 0)
    {
        curl_easy_setopt(curl, CURLOPT_PROXY, proxyServer);
    }

    // Trust a private/internal master-server CA when SSL_CERT_FILE points at one (also how
    // the integration test trusts its self-signed mock). Unset -> the system trust store.
    if (const char* caFile = std::getenv("SSL_CERT_FILE"); caFile != nullptr && caFile[0] != 0)
    {
        curl_easy_setopt(curl, CURLOPT_CAINFO, caFile);
    }

    if (!body.empty())
    {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }

    curl_slist* headers = curl_slist_append(nullptr, "Accept: application/json");
    std::string contentTypeHeader;
    if (contentType != nullptr && contentType[0] != 0)
    {
        contentTypeHeader = std::string("Content-Type: ") + contentType;
        headers = curl_slist_append(headers, contentTypeHeader.c_str());
    }
    std::string authHeader;
    if (authToken != nullptr && authToken[0] != 0)
    {
        authHeader = std::string("Authorization: Bearer ") + authToken;
        headers = curl_slist_append(headers, authHeader.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    const CURLcode result = curl_easy_perform(curl);
    if (result == CURLE_OK)
    {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
    }
    else
    {
        LOG_WARN(Network, "Master server service request to '{}' failed: {}", url, curl_easy_strerror(result));
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return result == CURLE_OK && IsSuccessfulMasterServerServiceStatus(statusCode);
}

// Streaming download — writes the response body straight to a FILE* and reports
// byte progress. Mirrors SendMasterServerServiceRequest's curl setup but with no
// total timeout (mod PBOs are large) and a progress callback.
size_t WriteDownloadChunk(char* data, size_t size, size_t count, void* userdata)
{
    return fwrite(data, 1, size * count, static_cast<FILE*>(userdata));
}

struct DownloadProgressState
{
    void* instance;
    MasterServerServiceDownloadProgressCallback callback;
    MasterServerServiceDownloadCancelCheck cancelCheck;
    int64_t offset;
};

struct DownloadResponseState
{
    long statusCode = 0;
    int64_t contentRangeStart = -1;
    std::string validator;
};

bool HeaderNameEquals(std::string_view lhs, std::string_view rhs)
{
    if (lhs.size() != rhs.size())
        return false;
    for (size_t i = 0; i < lhs.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(lhs[i])) != std::tolower(static_cast<unsigned char>(rhs[i])))
            return false;
    }
    return true;
}

std::string_view TrimHeaderValue(std::string_view value)
{
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        value.remove_prefix(1);
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' '))
        value.remove_suffix(1);
    return value;
}

size_t CaptureDownloadHeader(char* data, size_t size, size_t count, void* userdata)
{
    const size_t total = size * count;
    auto* state = static_cast<DownloadResponseState*>(userdata);
    if (state == nullptr)
        return total;

    const std::string_view line(data, total);
    if (line.size() >= 5 && line.substr(0, 5) == "HTTP/")
    {
        long statusCode = 0;
        const std::string text(line);
        if (sscanf(text.c_str(), "HTTP/%*s %ld", &statusCode) == 1)
        {
            state->statusCode = statusCode;
            state->contentRangeStart = -1;
            state->validator.clear();
        }
        return total;
    }

    const size_t separator = line.find(':');
    if (separator == std::string_view::npos)
        return total;
    const std::string_view name = line.substr(0, separator);
    const std::string_view value = TrimHeaderValue(line.substr(separator + 1));
    if (HeaderNameEquals(name, "ETag"))
    {
        state->validator.assign(value);
    }
    else if (HeaderNameEquals(name, "Last-Modified") && state->validator.empty())
    {
        state->validator.assign(value);
    }
    else if (HeaderNameEquals(name, "Content-Range"))
    {
        long long start = -1;
        const std::string text(value);
        if (sscanf(text.c_str(), "bytes %lld-", &start) == 1)
            state->contentRangeStart = start;
    }
    return total;
}

int ReportDownloadProgress(void* userdata, curl_off_t dlTotal, curl_off_t dlNow, curl_off_t, curl_off_t)
{
    auto* state = static_cast<DownloadProgressState*>(userdata);
    if (state == nullptr)
    {
        return 0;
    }
    // Abort promptly if cancelled — libcurl stops the transfer on a non-zero return
    // (CURLE_ABORTED_BY_CALLBACK), so a cancelled download doesn't run to completion.
    if (state->cancelCheck != nullptr && state->cancelCheck(state->instance))
    {
        return 1;
    }
    if (state->callback != nullptr)
    {
        const int64_t total = dlTotal > 0 ? state->offset + static_cast<int64_t>(dlTotal) : 0;
        state->callback(state->instance, state->offset + static_cast<int64_t>(dlNow), total);
    }
    return 0;
}

CURLcode StreamMasterServerServiceDownload(const char* url, const char* proxyServer, FILE* out, void* instance,
                                           MasterServerServiceDownloadProgressCallback progress,
                                           MasterServerServiceDownloadCancelCheck cancelCheck, int64_t offset,
                                           const std::string& validator, DownloadResponseState& response)
{
    if (url == nullptr || out == nullptr)
    {
        return CURLE_BAD_FUNCTION_ARGUMENT;
    }

    static std::once_flag curlInitFlag;
    std::call_once(curlInitFlag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });

    CURL* curl = curl_easy_init();
    if (curl == nullptr)
    {
        return CURLE_FAILED_INIT;
    }

    DownloadProgressState progressState{instance, progress, cancelCheck, offset};

    const std::string effectiveUrl = ResolveHttpTestUrl(url);
    curl_easy_setopt(curl, CURLOPT_URL, effectiveUrl.c_str());
    const std::string userAgent = BuildMasterServerServiceUserAgent("client");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kDownloadConnectTimeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, kDownloadLowSpeedBytesPerSecond);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, kDownloadLowSpeedTimeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteDownloadChunk);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, CaptureDownloadHeader);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response);
    if (progress != nullptr || cancelCheck != nullptr)
    {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ReportDownloadProgress);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progressState);
    }
    if (proxyServer != nullptr && proxyServer[0] != 0)
    {
        curl_easy_setopt(curl, CURLOPT_PROXY, proxyServer);
    }
    if (const char* caFile = std::getenv("SSL_CERT_FILE"); caFile != nullptr && caFile[0] != 0)
    {
        curl_easy_setopt(curl, CURLOPT_CAINFO, caFile);
    }

    curl_slist* headers = nullptr;
    std::string range;
    std::string ifRange;
    if (offset > 0)
    {
        range = std::to_string(offset) + "-";
        curl_easy_setopt(curl, CURLOPT_RANGE, range.c_str());
        ifRange = "If-Range: " + validator;
        headers = curl_slist_append(headers, ifRange.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    const CURLcode result = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.statusCode);
    if (result != CURLE_OK && result != CURLE_ABORTED_BY_CALLBACK)
    {
        LOG_WARN(Network, "Master server download '{}' failed: {}", url, curl_easy_strerror(result));
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return result;
}

bool IsTransientDownloadError(CURLcode result)
{
    switch (result)
    {
        case CURLE_COULDNT_RESOLVE_PROXY:
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_CONNECT:
        case CURLE_PARTIAL_FILE:
        case CURLE_OPERATION_TIMEDOUT:
        case CURLE_SEND_ERROR:
        case CURLE_RECV_ERROR:
        case CURLE_GOT_NOTHING:
        case CURLE_HTTP2:
        case CURLE_HTTP2_STREAM:
        case CURLE_AGAIN:
            return true;
        default:
            return false;
    }
}

bool IsTransientDownloadStatus(long statusCode)
{
    for (const long transientStatus : kTransientDownloadStatuses)
    {
        if (statusCode == transientStatus)
            return true;
    }
    return false;
}

std::string LoadDownloadValidator(const std::string& path)
{
    std::ifstream input(path);
    std::string validator;
    std::getline(input, validator);
    if (validator.size() > kMaximumDownloadValidatorLength || validator.find_first_of("\r\n") != std::string::npos)
        validator.clear();
    return validator;
}

void StoreDownloadValidator(const std::string& path, const std::string& validator)
{
    if (validator.empty())
        return;
    std::ofstream output(path, std::ios::trunc);
    if (output)
        output << validator;
}

} // namespace

bool BuildMasterServerServiceRegistrationFromServerUrl(const char* serverUrl, const char* sessionName, int gameState,
                                                       bool includeMission, const char* missionName, int actualVersion,
                                                       int requiredVersion, const char* versionTag, bool password,
                                                       int numPlayers, int maxPlayers, const char* modList,
                                                       bool equalModRequired, const char* platform,
                                                       MasterServerServiceRegistration& registration)
{
    registration = {};
    if (serverUrl == nullptr || sessionName == nullptr)
    {
        return false;
    }

    if (!TryParseMasterServerServiceServerAddress(serverUrl, registration.address, registration.hostPort))
    {
        return false;
    }

    registration.serverId = BuildMasterServerServiceServerId(registration.address, registration.hostPort);
    registration.app = APP_NAME_SHORT;
    ApplyMasterServerServiceRegistrationFields(registration, sessionName, gameState, includeMission, missionName,
                                               actualVersion, requiredVersion, versionTag, password, numPlayers,
                                               maxPlayers, modList, equalModRequired, platform);
    return true;
}

bool TryParseMasterServerServiceServerAddress(const char* serverUrl, std::string& address, int& hostPort)
{
    address.clear();
    hostPort = 0;
    if (serverUrl == nullptr)
    {
        return false;
    }

    const std::string value = serverUrl;
    const size_t separator = value.rfind(':');
    if (separator == std::string::npos)
    {
        return false;
    }

    address = value.substr(0, separator);
    hostPort = atoi(value.substr(separator + 1).c_str());
    return !address.empty() && hostPort > 0;
}

std::string BuildMasterServerServiceServerId(const std::string& address, int hostPort)
{
    if (address.empty() || hostPort <= 0)
    {
        return {};
    }

    return address + ":" + std::to_string(hostPort);
}

std::string BuildMasterServerServiceUserAgent(const char* role)
{
    std::string agent = std::string(APP_NAME_SHORT) + "/" + std::to_string(APP_VERSION_NUM);
    if (role == nullptr || role[0] == 0)
    {
        return agent;
    }
    const RString tag = GetVersionTag();
    agent += " (tag=";
    agent += (const char*)tag;
    agent += "; role=";
    agent += role;
    agent += ")";
    return agent;
}

void ApplyMasterServerServiceRegistrationFields(MasterServerServiceRegistration& registration, const char* sessionName,
                                                int gameState, bool includeMission, const char* missionName,
                                                int actualVersion, int requiredVersion, const char* versionTag,
                                                bool password, int numPlayers, int maxPlayers, const char* modList,
                                                bool equalModRequired, const char* platform)
{
    if (registration.app.empty())
    {
        registration.app = APP_NAME_SHORT;
    }
    registration.hostName = sessionName != nullptr ? sessionName : "";
    registration.mission = includeMission && missionName != nullptr ? missionName : "";
    registration.actualVersion = actualVersion;
    registration.requiredVersion = requiredVersion;
    registration.versionTag = versionTag != nullptr ? versionTag : "";
    registration.password = password;
    registration.gameState = gameState;
    registration.numPlayers = numPlayers;
    registration.maxPlayers = maxPlayers;
    registration.mod = modList != nullptr ? modList : "";
    registration.equalModRequired = equalModRequired;
    registration.transportImplementation = MasterServerTransportImplementation;
    registration.platform = platform != nullptr ? platform : "";
}

void AssignMasterServerServiceSessionInfo(const MasterServerServiceSession& source, MasterServerSessionInfo& session)
{
    AssignMasterServerSessionInfo(session, source.address.c_str(), source.hostPort, source.hostName.c_str(),
                                  source.mission.c_str(), source.transportImplementation.c_str(), source.actualVersion,
                                  source.requiredVersion, source.versionTag.c_str(), source.password, source.gameState,
                                  source.ping, source.numPlayers, source.maxPlayers, source.timeLeft,
                                  source.mod.c_str(), source.equalModRequired);
    session.modPackages = &source.modPackages;
}

const char* GetMasterServerServiceSessionStringValue(const MasterServerServiceSession& session, const char* key,
                                                     const char* fallback)
{
    const char* value = fallback;
    return TryGetMasterServerServiceSessionStringField(session, key, value) ? value : fallback;
}

int GetMasterServerServiceSessionIntValue(const MasterServerServiceSession& session, const char* key, int fallback)
{
    int value = fallback;
    return TryGetMasterServerServiceSessionIntField(session, key, value) ? value : fallback;
}

int GetMasterServerServiceSessionPing(const MasterServerServiceSession& session)
{
    return session.ping;
}

const char* GetMasterServerServiceSessionAddress(const MasterServerServiceSession& session)
{
    return session.address.c_str();
}

std::string BuildMasterServerServiceListUrl(const char* masterServerHost, const MasterServerBrowserFilter& filter)
{
    std::string url = BuildMasterServerServiceResourceUrl(masterServerHost, "/v1/servers");
    if (url.empty())
    {
        return url;
    }

    bool hasQuery = false;
    AppendMasterServerServiceQueryParameter(url, hasQuery, "app", UrlEncodeMasterServerServiceValue(APP_NAME_SHORT));
    // The directory hides passworded servers by default; the in-game browser must
    // list them (with the lock marker) or private servers are unjoinable from the UI.
    AppendMasterServerServiceQueryParameter(url, hasQuery, "includePasswordedServers", "true");
    VisitMasterServerBrowserFilterTerms(
        filter,
        [&](const MasterServerBrowserFilterTerm& term)
        {
            switch (term.kind)
            {
                case MasterServerBrowserFilterTermKind::ServerName:
                    AppendMasterServerServiceQueryParameter(url, hasQuery, "hostname",
                                                            UrlEncodeMasterServerServiceValue(term.text));
                    break;
                case MasterServerBrowserFilterTermKind::MissionName:
                    AppendMasterServerServiceQueryParameter(url, hasQuery, "gametype",
                                                            UrlEncodeMasterServerServiceValue(term.text));
                    break;
                case MasterServerBrowserFilterTermKind::MinPlayers:
                    AppendMasterServerServiceQueryParameter(url, hasQuery, "minplayers", term.number);
                    break;
                case MasterServerBrowserFilterTermKind::MaxPlayers:
                    AppendMasterServerServiceQueryParameter(url, hasQuery, "maxplayers", term.number);
                    break;
                case MasterServerBrowserFilterTermKind::ExcludeFullServers:
                    AppendMasterServerServiceQueryParameter(url, hasQuery, "includeFullServers", "false");
                    break;
            }
        });

    return url;
}

std::string BuildMasterServerServiceResourceUrl(const char* masterServerHost, const char* resourcePath)
{
    std::string url = NormalizeMasterServerServiceBaseUrl(masterServerHost);
    if (!url.empty() && resourcePath != nullptr)
    {
        url += resourcePath;
    }
    return url;
}

std::string BuildMasterServerServiceServerDetailUrl(const char* masterServerHost, const char* serverId)
{
    std::string url = BuildMasterServerServiceResourceUrl(masterServerHost, "/v1/servers/");
    if (!url.empty())
    {
        url += UrlEncodeMasterServerServiceValue(serverId);
    }
    return url;
}

std::string BuildMasterServerServiceModDetailUrl(const char* masterServerHost, const char* modId)
{
    std::string url = BuildMasterServerServiceResourceUrl(masterServerHost, "/v1/mods/");
    if (!url.empty())
    {
        url += UrlEncodeMasterServerServiceValue(modId);
    }
    return url;
}

std::string BuildMasterServerServiceModListUrl(const char* masterServerHost, const char* query)
{
    std::string url = BuildMasterServerServiceResourceUrl(masterServerHost, "/v1/mods");
    if (url.empty())
    {
        return url;
    }

    bool hasQuery = false;
    AppendMasterServerServiceQueryParameter(url, hasQuery, "app", UrlEncodeMasterServerServiceValue(APP_NAME_SHORT));
    AppendMasterServerServiceQueryParameter(url, hasQuery, MasterServerFieldActualVersion,
                                            std::to_string(APP_VERSION_NUM));
    AppendMasterServerServiceQueryParameter(url, hasQuery, MasterServerFieldVersionTag,
                                            UrlEncodeMasterServerServiceValue((const char*)GetVersionTag()));
    if (query != nullptr)
    {
        AppendMasterServerServiceQueryParameter(url, hasQuery, "q", UrlEncodeMasterServerServiceValue(query));
    }
    return url;
}

std::string BuildMasterServerServiceModVersionsUrl(const char* masterServerHost, const char* modId)
{
    const std::string url = BuildMasterServerServiceModDetailUrl(masterServerHost, modId);
    return url.empty() ? url : url + "/versions";
}

std::string BuildMasterServerServiceModServersUrl(const char* masterServerHost, const char* modId)
{
    const std::string url = BuildMasterServerServiceModDetailUrl(masterServerHost, modId);
    return url.empty() ? url : url + "/servers";
}

std::string BuildMasterServerServiceRegisterUrl(const char* masterServerHost)
{
    return BuildMasterServerServiceResourceUrl(masterServerHost, "/v1/servers/register");
}

std::string BuildMasterServerServiceHeartbeatUrl(const char* masterServerHost)
{
    return BuildMasterServerServiceResourceUrl(masterServerHost, "/v1/servers/heartbeat");
}

std::string BuildMasterServerServiceUnregisterUrl(const char* masterServerHost, const char* serverId)
{
    std::string url = BuildMasterServerServiceResourceUrl(masterServerHost, "/v1/servers/");
    if (!url.empty())
    {
        url += UrlEncodeMasterServerServiceValue(serverId);
    }
    return url;
}

std::string BuildMasterServerServiceRegistrationJson(const MasterServerServiceRegistration& registration)
{
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr)
    {
        return {};
    }

    const char* app = registration.app.empty() ? APP_NAME_SHORT : registration.app.c_str();
    cJSON_AddStringToObject(root, "app", app);
    cJSON_AddStringToObject(root, "serverId", registration.serverId.c_str());
    cJSON_AddStringToObject(root, "address", registration.address.c_str());
    cJSON_AddNumberToObject(root, MasterServerFieldHostPort, registration.hostPort);
    cJSON_AddStringToObject(root, MasterServerFieldHostName, registration.hostName.c_str());
    cJSON_AddStringToObject(root, MasterServerFieldGameType, registration.mission.c_str());
    cJSON_AddNumberToObject(root, MasterServerFieldActualVersion, registration.actualVersion);
    cJSON_AddNumberToObject(root, MasterServerFieldRequiredVersion, registration.requiredVersion);
    cJSON_AddStringToObject(root, MasterServerFieldVersionTag, registration.versionTag.c_str());
    cJSON_AddNumberToObject(root, MasterServerFieldState, registration.gameState);
    cJSON_AddNumberToObject(root, MasterServerFieldTimeLeft, registration.timeLeft);
    cJSON_AddNumberToObject(root, MasterServerFieldStateElapsed, registration.stateElapsedSeconds);
    cJSON_AddNumberToObject(root, MasterServerFieldNumPlayers, registration.numPlayers);
    cJSON_AddNumberToObject(root, MasterServerFieldMaxPlayers, registration.maxPlayers);
    cJSON_AddBoolToObject(root, MasterServerFieldPassword, registration.password);
    cJSON_AddStringToObject(root, MasterServerFieldMod, registration.mod.c_str());
    cJSON* modPackages = cJSON_AddArrayToObject(root, "modPackages");
    for (const MasterServerServiceModPackage& package : registration.modPackages)
    {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "modId", package.modId.c_str());
        cJSON_AddNumberToObject(item, "packageRevision", static_cast<double>(package.packageRevision));
        cJSON_AddItemToArray(modPackages, item);
    }
    cJSON_AddBoolToObject(root, MasterServerFieldEqualModRequired, registration.equalModRequired);
    cJSON_AddStringToObject(root, MasterServerFieldImplementation, registration.transportImplementation.c_str());
    cJSON_AddStringToObject(root, MasterServerFieldPlatform, registration.platform.c_str());
    cJSON_AddStringToObject(root, MasterServerFieldMapName, registration.island.c_str());
    cJSON_AddBoolToObject(root, MasterServerFieldCadet, registration.cadetMode);
    cJSON_AddNumberToObject(root, MasterServerFieldDifficulty, registration.difficulty);
    cJSON_AddBoolToObject(root, MasterServerFieldJoinInProgress, registration.joinInProgress);
    cJSON_AddBoolToObject(root, MasterServerFieldDisabledAI, registration.disabledAI);
    cJSON_AddNumberToObject(root, MasterServerFieldRespawn, registration.respawn);
    cJSON_AddNumberToObject(root, MasterServerFieldRespawnDelay, registration.respawnDelay);
    cJSON_AddBoolToObject(root, MasterServerFieldLocked, registration.sessionLocked);
    cJSON_AddBoolToObject(root, MasterServerFieldDedicated, registration.dedicated);
    cJSON_AddStringToObject(root, MasterServerFieldDescription, registration.description.c_str());
    cJSON_AddStringToObject(root, MasterServerFieldParam1, registration.param1.c_str());
    cJSON_AddStringToObject(root, MasterServerFieldParam2, registration.param2.c_str());
    cJSON_AddStringToObject(root, MasterServerFieldRequiredAddons, registration.requiredAddons.c_str());

    char* printed = cJSON_PrintUnformatted(root);
    std::string json = printed != nullptr ? printed : "";
    cJSON_free(printed);
    cJSON_Delete(root);
    return json;
}

bool TryParseMasterServerServiceSession(const cJSON* object, MasterServerServiceSession& session)
{
    if (!cJSON_IsObject(object))
    {
        return false;
    }

    AssignMasterServerServiceSession(session, GetMasterServerServiceJsonString(object, "address").c_str(),
                                     GetMasterServerServiceJsonInt(object, "hostport", 0),
                                     GetMasterServerServiceJsonString(object, MasterServerFieldHostName).c_str(),
                                     GetMasterServerServiceJsonString(object, MasterServerFieldGameType).c_str(),
                                     GetMasterServerServiceJsonString(object, MasterServerFieldImplementation).c_str(),
                                     GetMasterServerServiceJsonInt(object, MasterServerFieldActualVersion, 0),
                                     GetMasterServerServiceJsonInt(object, MasterServerFieldRequiredVersion, 0),
                                     GetMasterServerServiceJsonString(object, MasterServerFieldVersionTag).c_str(),
                                     GetMasterServerServiceJsonBool(object, MasterServerFieldPassword, false),
                                     GetMasterServerServiceJsonInt(object, MasterServerFieldState, 0),
                                     GetMasterServerServiceJsonInt(object, "ping", 0),
                                     GetMasterServerServiceJsonInt(object, MasterServerFieldNumPlayers, 0),
                                     GetMasterServerServiceJsonInt(object, MasterServerFieldMaxPlayers, 0),
                                     GetMasterServerServiceJsonInt(object, MasterServerFieldTimeLeft, 15),
                                     GetMasterServerServiceJsonString(object, MasterServerFieldMod).c_str(),
                                     GetMasterServerServiceJsonBool(object, MasterServerFieldEqualModRequired, false));
    session.app = GetMasterServerServiceJsonString(object, "app");
    session.stateElapsedSeconds = GetMasterServerServiceJsonInt(object, MasterServerFieldStateElapsed, 0);
    const cJSON* modPackages = GetMasterServerServiceJsonItem(object, "modPackages");
    if (cJSON_IsArray(modPackages))
    {
        const int count = cJSON_GetArraySize(modPackages);
        for (int i = 0; i < count; ++i)
        {
            const cJSON* item = cJSON_GetArrayItem(modPackages, i);
            const std::string modId = GetMasterServerServiceJsonString(item, "modId");
            if (!modId.empty())
            {
                session.modPackages.push_back({modId, GetMasterServerServiceJsonInt64(item, "packageRevision", 1)});
            }
        }
    }
    return true;
}

bool TryParseMasterServerServiceModCatalogEntry(const cJSON* object, MasterServerServiceModCatalogEntry& entry)
{
    if (!cJSON_IsObject(object))
    {
        return false;
    }

    entry = {};
    entry.modId = GetMasterServerServiceJsonString(object, "modId");
    entry.app = GetMasterServerServiceJsonString(object, "app");
    entry.actualVersion = GetMasterServerServiceJsonInt(object, MasterServerFieldActualVersion, 0);
    entry.versionTag = GetMasterServerServiceJsonString(object, MasterServerFieldVersionTag);
    entry.compatible = GetMasterServerServiceJsonBool(object, "compatible", false);
    entry.name = GetMasterServerServiceJsonString(object, "name");
    entry.version = GetMasterServerServiceJsonString(object, "version");
    entry.packageRevision = GetMasterServerServiceJsonInt64(object, "packageRevision", 1);
    entry.sha256 = GetMasterServerServiceJsonString(object, "sha256");
    entry.publishedUnixMs = GetMasterServerServiceJsonInt64(object, "publishedUnixMs", 0);
    entry.folderName = GetMasterServerServiceJsonString(object, "folderName");
    entry.description = GetMasterServerServiceJsonString(object, "description");
    entry.homepageUrl = GetMasterServerServiceJsonString(object, "homepageUrl");
    entry.downloadUrl = GetMasterServerServiceJsonString(object, "downloadUrl");
    entry.sizeBytes = GetMasterServerServiceJsonInt64(object, "sizeBytes", 0);

    const cJSON* authors = GetMasterServerServiceJsonItem(object, "authors");
    if (cJSON_IsArray(authors))
    {
        const int count = cJSON_GetArraySize(authors);
        entry.authors.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            const cJSON* item = cJSON_GetArrayItem(authors, i);
            if (cJSON_IsString(item) && item->valuestring != nullptr)
            {
                entry.authors.push_back(item->valuestring);
            }
        }
    }

    return true;
}

bool ParseMasterServerServiceModDetailResponse(const char* json, MasterServerServiceModCatalogEntry& detail)
{
    detail = {};
    cJSON* root = cJSON_Parse(json);
    const bool ok = root != nullptr && TryParseMasterServerServiceModCatalogEntry(root, detail);
    cJSON_Delete(root);
    if (!ok)
    {
        detail = {};
    }
    return ok;
}

bool ParseMasterServerServiceListResponse(const char* json, std::vector<MasterServerServiceSession>& sessions)
{
    sessions.clear();
    cJSON* root = cJSON_Parse(json);
    if (!cJSON_IsArray(root))
    {
        cJSON_Delete(root);
        return false;
    }

    const int count = cJSON_GetArraySize(root);
    sessions.reserve(count);
    for (int i = 0; i < count; ++i)
    {
        const cJSON* item = cJSON_GetArrayItem(root, i);
        MasterServerServiceSession session;
        if (!TryParseMasterServerServiceSession(item, session))
        {
            continue;
        }
        sessions.push_back(std::move(session));
    }

    cJSON_Delete(root);
    return true;
}

bool ParseMasterServerServiceModListResponse(const char* json, std::vector<MasterServerServiceModCatalogEntry>& mods)
{
    mods.clear();
    cJSON* root = cJSON_Parse(json);
    if (!cJSON_IsArray(root))
    {
        cJSON_Delete(root);
        return false;
    }

    const int count = cJSON_GetArraySize(root);
    mods.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        const cJSON* item = cJSON_GetArrayItem(root, i);
        MasterServerServiceModCatalogEntry entry;
        if (TryParseMasterServerServiceModCatalogEntry(item, entry))
        {
            mods.push_back(std::move(entry));
        }
    }

    cJSON_Delete(root);
    return true;
}

namespace
{
bool TryParseMasterServerServicePlayer(const cJSON* object, MasterServerServicePlayer& player)
{
    if (!cJSON_IsObject(object))
    {
        return false;
    }

    player.name = GetMasterServerServiceJsonString(object, "name");
    player.role = GetMasterServerServiceJsonString(object, "role");
    return true;
}

bool TryParseMasterServerServiceModReference(const cJSON* object, MasterServerServiceModReference& mod)
{
    if (!cJSON_IsObject(object))
    {
        return false;
    }

    mod.modId = GetMasterServerServiceJsonString(object, "modId");
    mod.name = GetMasterServerServiceJsonString(object, "name");
    mod.known = GetMasterServerServiceJsonBool(object, "known", false);
    mod.version = GetMasterServerServiceJsonString(object, "version");
    mod.description = GetMasterServerServiceJsonString(object, "description");
    mod.homepageUrl = GetMasterServerServiceJsonString(object, "homepageUrl");
    mod.downloadUrl = GetMasterServerServiceJsonString(object, "downloadUrl");
    return true;
}

bool TryParseMasterServerServiceModUsageServer(const cJSON* object, MasterServerServiceModUsageServer& server)
{
    if (!cJSON_IsObject(object))
    {
        return false;
    }

    server.serverId = GetMasterServerServiceJsonString(object, "serverId");
    server.hostName = GetMasterServerServiceJsonString(object, "hostname");
    server.mission = GetMasterServerServiceJsonString(object, "gametype");
    server.players = GetMasterServerServiceJsonInt(object, "players", 0);
    server.maxPlayers = GetMasterServerServiceJsonInt(object, "maxPlayers", 0);
    server.password = GetMasterServerServiceJsonBool(object, "password", false);
    return true;
}

bool TryParseMasterServerServicePopulationSample(const cJSON* object, MasterServerServicePopulationSample& sample)
{
    if (!cJSON_IsObject(object))
    {
        return false;
    }

    sample.observedUnixMs = GetMasterServerServiceJsonInt64(object, "observedUnixMs", 0);
    sample.players = GetMasterServerServiceJsonInt(object, "players", 0);
    return true;
}

bool TryParseMasterServerServiceRecentSession(const cJSON* object, MasterServerServiceRecentSession& session)
{
    if (!cJSON_IsObject(object))
    {
        return false;
    }

    session.mission = GetMasterServerServiceJsonString(object, "mission");
    session.label = GetMasterServerServiceJsonString(object, "label");
    session.playedMinutes = GetMasterServerServiceJsonInt(object, "playedMinutes", 0);
    session.peakPlayers = GetMasterServerServiceJsonInt(object, "peakPlayers", 0);
    session.endedUnixMs = GetMasterServerServiceJsonInt64(object, "endedUnixMs", 0);
    return true;
}

template <class Entry, class ParseFn>
void ParseMasterServerServiceArray(const cJSON* object, const char* key, std::vector<Entry>& out, ParseFn&& parse)
{
    out.clear();
    const cJSON* array = GetMasterServerServiceJsonItem(object, key);
    if (!cJSON_IsArray(array))
    {
        return;
    }

    const int count = cJSON_GetArraySize(array);
    out.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        const cJSON* item = cJSON_GetArrayItem(array, i);
        Entry entry;
        if (std::forward<ParseFn>(parse)(item, entry))
        {
            out.push_back(std::move(entry));
        }
    }
}
} // namespace

bool ParseMasterServerServiceServerDetailResponse(const char* json, MasterServerServiceServerDetail& detail)
{
    detail = {};
    cJSON* root = cJSON_Parse(json);
    if (!cJSON_IsObject(root))
    {
        cJSON_Delete(root);
        return false;
    }

    const cJSON* server = GetMasterServerServiceJsonItem(root, "server");
    if (!TryParseMasterServerServiceSession(server, detail.server))
    {
        cJSON_Delete(root);
        detail = {};
        return false;
    }

    ParseMasterServerServiceArray(root, "players", detail.players, TryParseMasterServerServicePlayer);
    ParseMasterServerServiceArray(root, "mods", detail.mods, TryParseMasterServerServiceModReference);
    ParseMasterServerServiceArray(root, "playerHistory", detail.playerHistory,
                                  TryParseMasterServerServicePopulationSample);
    ParseMasterServerServiceArray(root, "recentSessions", detail.recentSessions,
                                  TryParseMasterServerServiceRecentSession);
    cJSON_Delete(root);
    return true;
}

bool ParseMasterServerServiceModUsageResponse(const char* json, std::vector<MasterServerServiceModUsageServer>& servers)
{
    servers.clear();
    cJSON* root = cJSON_Parse(json);
    if (!cJSON_IsArray(root))
    {
        cJSON_Delete(root);
        return false;
    }

    const int count = cJSON_GetArraySize(root);
    servers.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        const cJSON* item = cJSON_GetArrayItem(root, i);
        MasterServerServiceModUsageServer server;
        if (TryParseMasterServerServiceModUsageServer(item, server))
        {
            servers.push_back(std::move(server));
        }
    }

    cJSON_Delete(root);
    return true;
}

namespace
{
bool ExecuteMasterServerServiceReadRequest(const MasterServerServiceHttpRequest& request, const char* proxyServer,
                                           std::string& responseBody, long& statusCode)
{
    // libcurl handles both http and https (Schannel on Windows, the system trust
    // store / SSL_CERT_FILE elsewhere), so route every read through it. The
    // platform-specific fallback below only runs if libcurl itself fails.
    if (ExecuteMasterServerServiceHttpRequest(request, proxyServer, responseBody, statusCode,
                                              SendMasterServerServiceRequest))
    {
        return true;
    }

#if !defined(_WIN32)
    size_t size = 0;
    char* payload = DownloadFile(request.url.c_str(), size, proxyServer);
    if (payload == nullptr)
    {
        statusCode = 0;
        responseBody.clear();
        return false;
    }

    responseBody.assign(payload, size);
    FreeDownloadedBuffer(payload);
    statusCode = 200;
    return true;
#else
    LOG_WARN(Network, "Master server service read failed: url='{}' status={}", request.url, statusCode);
    return false;
#endif
}
} // namespace

bool FetchMasterServerServiceList(const char* masterServerHost, const MasterServerBrowserFilter& filter,
                                  const char* proxyServer, std::vector<MasterServerServiceSession>& sessions)
{
    return FetchMasterServerServiceList(
        masterServerHost, filter, proxyServer, sessions,
        [](const MasterServerServiceHttpRequest& request, const char* currentProxyServer, std::string& json,
           long& statusCode)
        { return ExecuteMasterServerServiceReadRequest(request, currentProxyServer, json, statusCode); });
}

bool FetchMasterServerServiceServerDetail(const char* masterServerHost, const char* serverId, const char* proxyServer,
                                          MasterServerServiceServerDetail& detail)
{
    const bool ok = FetchMasterServerServiceServerDetail(
        masterServerHost, serverId, proxyServer, detail,
        [](const MasterServerServiceHttpRequest& request, const char* currentProxyServer, std::string& json,
           long& statusCode)
        { return ExecuteMasterServerServiceReadRequest(request, currentProxyServer, json, statusCode); });
    if (!ok)
    {
        LOG_WARN(Network, "Master server service detail failed: serverId='{}' host='{}'", serverId,
                 masterServerHost != nullptr ? masterServerHost : "");
    }
    return ok;
}

bool FetchMasterServerServiceModList(const char* masterServerHost, const char* query, const char* proxyServer,
                                     std::vector<MasterServerServiceModCatalogEntry>& mods)
{
    const bool ok = FetchMasterServerServiceModList(
        masterServerHost, query, proxyServer, mods,
        [](const MasterServerServiceHttpRequest& request, const char* currentProxyServer, std::string& json,
           long& statusCode)
        { return ExecuteMasterServerServiceReadRequest(request, currentProxyServer, json, statusCode); });
    if (!ok)
    {
        LOG_WARN(Network, "Master server service mod list failed: host='{}'",
                 masterServerHost != nullptr ? masterServerHost : "");
    }
    return ok;
}

bool FetchMasterServerServiceModDetail(const char* masterServerHost, const char* modId, const char* proxyServer,
                                       MasterServerServiceModCatalogEntry& detail)
{
    const bool ok = FetchMasterServerServiceModDetail(
        masterServerHost, modId, proxyServer, detail,
        [](const MasterServerServiceHttpRequest& request, const char* currentProxyServer, std::string& json,
           long& statusCode)
        { return ExecuteMasterServerServiceReadRequest(request, currentProxyServer, json, statusCode); });
    if (!ok)
    {
        LOG_WARN(Network, "Master server service mod detail failed: modId='{}' host='{}'", modId,
                 masterServerHost != nullptr ? masterServerHost : "");
    }
    return ok;
}

bool FetchMasterServerServiceModVersions(const char* masterServerHost, const char* modId, const char* proxyServer,
                                         std::vector<MasterServerServiceModCatalogEntry>& versions)
{
    const bool ok = FetchMasterServerServiceModVersions(
        masterServerHost, modId, proxyServer, versions,
        [](const MasterServerServiceHttpRequest& request, const char* currentProxyServer, std::string& json,
           long& statusCode)
        { return ExecuteMasterServerServiceReadRequest(request, currentProxyServer, json, statusCode); });
    if (!ok)
    {
        LOG_WARN(Network, "Master server service mod versions failed: modId='{}' host='{}'", modId,
                 masterServerHost != nullptr ? masterServerHost : "");
    }
    return ok;
}

bool FetchMasterServerServiceModServers(const char* masterServerHost, const char* modId, const char* proxyServer,
                                        std::vector<MasterServerServiceModUsageServer>& servers)
{
    const bool ok = FetchMasterServerServiceModServers(
        masterServerHost, modId, proxyServer, servers,
        [](const MasterServerServiceHttpRequest& request, const char* currentProxyServer, std::string& json,
           long& statusCode)
        { return ExecuteMasterServerServiceReadRequest(request, currentProxyServer, json, statusCode); });
    if (!ok)
    {
        LOG_WARN(Network, "Master server service mod servers failed: modId='{}' host='{}'", modId,
                 masterServerHost != nullptr ? masterServerHost : "");
    }
    return ok;
}

DownloadFileResult DownloadMasterServerServiceFile(const char* url, const char* proxyServer, const char* destPath,
                                                   void* instance, MasterServerServiceDownloadProgressCallback progress,
                                                   MasterServerServiceDownloadCancelCheck cancelCheck,
                                                   std::string* error)
{
    if (error != nullptr)
        error->clear();
    if (url == nullptr || url[0] == 0 || destPath == nullptr || destPath[0] == 0)
    {
        if (error != nullptr)
            *error = "invalid download request";
        return DownloadFileResult::PermanentFailure;
    }

    const std::string tempPath = std::string(destPath) + ".part";
    const std::string validatorPath = tempPath + ".validator";
    std::error_code ec;
    const std::filesystem::path parent = std::filesystem::path(tempPath).parent_path();
    if (!parent.empty())
    {
        std::filesystem::create_directories(parent, ec);
    }
    std::string validator = LoadDownloadValidator(validatorPath);
    int64_t offset = 0;
    if (!validator.empty() && std::filesystem::exists(tempPath, ec))
    {
        const uintmax_t size = std::filesystem::file_size(tempPath, ec);
        if (!ec && size <= static_cast<uintmax_t>(INT64_MAX))
            offset = static_cast<int64_t>(size);
    }
    if (offset <= 0)
    {
        validator.clear();
        remove(tempPath.c_str());
        remove(validatorPath.c_str());
    }

    FILE* out = fopen(tempPath.c_str(), offset > 0 ? "ab" : "wb");
    if (out == nullptr)
    {
        LOG_WARN(Network, "Master server download cannot open temp file '{}'", tempPath);
        if (error != nullptr)
            *error = "cannot create partial download";
        return DownloadFileResult::PermanentFailure;
    }

    DownloadResponseState response;
    const CURLcode result = StreamMasterServerServiceDownload(url, proxyServer, out, instance, progress, cancelCheck,
                                                              offset, validator, response);
    fclose(out);

    const bool cancelled = result == CURLE_ABORTED_BY_CALLBACK && cancelCheck != nullptr && cancelCheck(instance);
    const std::string responseValidator = response.validator.empty() ? validator : response.validator;
    if (result != CURLE_OK)
    {
        if (result == CURLE_RANGE_ERROR && offset > 0)
        {
            remove(tempPath.c_str());
            remove(validatorPath.c_str());
            if (error != nullptr)
                *error = "server rejected download resume";
            return DownloadFileResult::TransientFailure;
        }
        if (!responseValidator.empty())
            StoreDownloadValidator(validatorPath, responseValidator);
        else
            remove(tempPath.c_str());
        if (cancelled)
            return DownloadFileResult::Cancelled;
        if (error != nullptr)
            *error = std::string("download transfer failed: ") + curl_easy_strerror(result);
        return IsTransientDownloadError(result) ? DownloadFileResult::TransientFailure
                                                : DownloadFileResult::PermanentFailure;
    }

    if (offset > 0)
    {
        const bool validatorMatches = response.validator.empty() || response.validator == validator;
        if (response.statusCode != kHttpPartialContent || response.contentRangeStart != offset || !validatorMatches)
        {
            remove(tempPath.c_str());
            remove(validatorPath.c_str());
            if (error != nullptr)
                *error = "download resume was not accepted";
            if (response.statusCode == kHttpOk || response.statusCode == kHttpRangeNotSatisfiable ||
                IsSuccessfulMasterServerServiceStatus(response.statusCode))
                return DownloadFileResult::TransientFailure;
            return IsTransientDownloadStatus(response.statusCode) ? DownloadFileResult::TransientFailure
                                                                  : DownloadFileResult::PermanentFailure;
        }
    }
    else if (!IsSuccessfulMasterServerServiceStatus(response.statusCode) || response.statusCode == kHttpPartialContent)
    {
        remove(tempPath.c_str());
        remove(validatorPath.c_str());
        if (error != nullptr)
            *error = "download server returned HTTP " + std::to_string(response.statusCode);
        return IsTransientDownloadStatus(response.statusCode) ? DownloadFileResult::TransientFailure
                                                              : DownloadFileResult::PermanentFailure;
    }

    remove(validatorPath.c_str());
    remove(destPath);
    if (rename(tempPath.c_str(), destPath) != 0)
    {
        remove(tempPath.c_str());
        LOG_WARN(Network, "Master server download cannot finalize '{}'", destPath);
        if (error != nullptr)
            *error = "cannot finalize downloaded file";
        return DownloadFileResult::PermanentFailure;
    }
    return DownloadFileResult::Success;
}

namespace
{
std::mutex g_serverTokenMutex;
std::string g_serverToken;
std::string g_serverTokenPath;

void LoadServerTokenLocked()
{
    g_serverToken.clear();
    if (g_serverTokenPath.empty())
    {
        return;
    }
    std::ifstream input(g_serverTokenPath);
    if (input)
    {
        std::getline(input, g_serverToken);
    }
    while (!g_serverToken.empty() &&
           (g_serverToken.back() == '\n' || g_serverToken.back() == '\r' || g_serverToken.back() == ' '))
    {
        g_serverToken.pop_back();
    }
}

std::string CurrentServerToken()
{
    std::lock_guard<std::mutex> lock(g_serverTokenMutex);
    return g_serverToken;
}

void StoreIssuedServerToken(const std::string& token)
{
    if (token.empty())
    {
        return;
    }
    std::lock_guard<std::mutex> lock(g_serverTokenMutex);
    g_serverToken = token;
    if (!g_serverTokenPath.empty() && !StoreMasterServerServiceTokenFile(g_serverTokenPath, token))
    {
        LOG_WARN(Network, "Master server token could not be persisted to '{}'", g_serverTokenPath);
    }
}

void CaptureIssuedServerToken(const std::string& responseBody)
{
    cJSON* root = cJSON_Parse(responseBody.c_str());
    if (root != nullptr)
    {
        StoreIssuedServerToken(GetMasterServerServiceJsonString(root, "token"));
        cJSON_Delete(root);
    }
}
} // namespace

bool StoreMasterServerServiceTokenFile(const std::string& path, const std::string& token)
{
    const std::string tempPath = path + ".tmp";
    {
        std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
        output.write(token.data(), static_cast<std::streamsize>(token.size()));
        output.flush();
        if (!output)
        {
            remove(tempPath.c_str());
            return false;
        }
    }
#if defined(_WIN32)
    if (!MoveFileExA(tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
#else
    if (rename(tempPath.c_str(), path.c_str()) != 0)
#endif
    {
        remove(tempPath.c_str());
        return false;
    }
    return true;
}

void SetMasterServerServiceTokenStore(const char* tokenFilePath)
{
    std::lock_guard<std::mutex> lock(g_serverTokenMutex);
    g_serverTokenPath = tokenFilePath != nullptr ? tokenFilePath : "";
    LoadServerTokenLocked();
}

bool PublishMasterServerServiceRegistration(const char* masterServerHost, const char* proxyServer,
                                            const MasterServerServiceRegistration& registration, bool heartbeat)
{
    MasterServerServiceHttpRequest request;
    if (!BuildMasterServerServicePublishRequest(masterServerHost, registration, heartbeat, request))
    {
        return false;
    }
    request.authToken = CurrentServerToken();

    std::string responseBody;
    long statusCode = 0;
    const bool ok = ExecuteMasterServerServiceHttpRequest(request, proxyServer, responseBody, statusCode,
                                                          SendMasterServerServiceRequest);
    if (ok)
    {
        CaptureIssuedServerToken(responseBody);
    }
    else
    {
        LOG_WARN(Network, "Master server service {} failed: url='{}' status={} response='{}'",
                 GetMasterServerServicePublishAction(heartbeat), request.url, statusCode,
                 SanitizeMasterServerServiceError(responseBody));
    }
    return ok;
}

bool UnregisterMasterServerService(const char* masterServerHost, const char* proxyServer, const char* serverId)
{
    MasterServerServiceHttpRequest request;
    if (!BuildMasterServerServiceUnregisterRequest(masterServerHost, serverId, request))
    {
        return false;
    }
    request.authToken = CurrentServerToken();

    std::string responseBody;
    long statusCode = 0;
    const bool ok = ExecuteMasterServerServiceHttpRequest(request, proxyServer, responseBody, statusCode,
                                                          SendMasterServerServiceRequest);
    if (!ok && !IsIgnorableMasterServerServiceUnregisterStatus(statusCode))
    {
        LOG_WARN(Network, "Master server service unregister failed: url='{}' status={} response='{}'", request.url,
                 statusCode, SanitizeMasterServerServiceError(responseBody));
    }
    return ok || IsIgnorableMasterServerServiceUnregisterStatus(statusCode);
}

} // namespace Poseidon
