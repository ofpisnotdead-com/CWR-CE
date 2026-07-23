#include <Poseidon/Network/MasterServerBrowser.hpp>

#include <Poseidon/Network/MasterServerProtocol.hpp>
#include <Poseidon/Network/MasterServerServiceClient.hpp>
#include <Poseidon/Network/NetworkConfig.hpp>
#include <Poseidon/Network/Network.hpp>

#include <atomic>
#include <memory>
#include <thread>
#include <string>
#include <vector>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Strings/StrFormat.hpp>

namespace Poseidon
{

namespace
{
struct MasterServerBrowserFetchResult
{
    std::vector<MasterServerServiceSession> serviceSessions;
};

using MasterServerBrowserFetchForTest = bool (*)(const char* masterServerHost, const MasterServerBrowserFilter& filter,
                                                 const char* proxyServer,
                                                 std::vector<MasterServerServiceSession>& sessions);

std::atomic<MasterServerBrowserFetchForTest> GMasterServerBrowserFetchForTest{nullptr};
} // namespace

struct MasterServerBrowser
{
    void* instance = nullptr;
    MasterServerBrowserProgressCallback progressCallback = nullptr;
    std::string host;
    std::vector<MasterServerServiceSession> serviceSessions;
    MasterServerBrowserState serviceState = MasterServerBrowserState::Idle;

    // Async fetch: UpdateMasterServerBrowser() spawns `worker` to run the
    // blocking HTTP query off the main thread. The worker owns its result
    // bundle; ThinkMasterServerBrowser() joins before adopting it on the UI
    // thread.
    std::thread worker;
    std::shared_ptr<MasterServerBrowserFetchResult> pendingFetch;
    std::atomic<bool> fetchDone{false};

    void joinWorker()
    {
        if (worker.joinable())
            worker.join();
    }
};

namespace
{
RString BuildBrowserFilterString(const MasterServerBrowserFilter& filter)
{
    RString value;
    VisitMasterServerBrowserFilterTerms(
        filter, [&](const MasterServerBrowserFilterTerm& term)
        { AppendMasterServerBrowserFilterCondition(value, BuildMasterServerBrowserFilterCondition(term)); });

    return value;
}
} // namespace

const RString& GetMasterServerBrowserFilterConjunction()
{
    static const RString And(" and ");
    return And;
}

void AppendMasterServerBrowserFilterCondition(RString& filter, RString condition)
{
    if (filter.GetLength() > 0)
    {
        filter = filter + GetMasterServerBrowserFilterConjunction();
    }
    filter = filter + condition;
}

RString BuildMasterServerBrowserFilterCondition(const MasterServerBrowserFilterTerm& term)
{
    switch (term.kind)
    {
        case MasterServerBrowserFilterTermKind::ServerName:
            return Format("%s like '%%%s%%'", MasterServerFieldHostName, term.text);
        case MasterServerBrowserFilterTermKind::MissionName:
            return Format("%s like '%%%s%%'", MasterServerFieldGameType, term.text);
        case MasterServerBrowserFilterTermKind::MinPlayers:
            return Format("%s >= %d", MasterServerFieldNumPlayers, term.number);
        case MasterServerBrowserFilterTermKind::MaxPlayers:
            return Format("%s <= %d", MasterServerFieldNumPlayers, term.number);
        case MasterServerBrowserFilterTermKind::ExcludeFullServers:
            return Format("%s < %s", MasterServerFieldNumPlayers, MasterServerFieldMaxPlayers);
    }

    return {};
}

RString BuildMasterServerBrowserFilterString(const MasterServerBrowserFilter& filter)
{
    return BuildBrowserFilterString(filter);
}

const char* GetMasterServerBrowserProxyServer(const RString& proxyServer)
{
    return proxyServer.GetLength() > 0 ? static_cast<const char*>(proxyServer) : nullptr;
}

void SetMasterServerBrowserFetchForTest(MasterServerBrowserFetchForTest fetch)
{
    GMasterServerBrowserFetchForTest.store(fetch, std::memory_order_release);
}

static auto& GetCurrentMasterServerBrowserServiceSessions(MasterServerBrowser& browser)
{
    return browser.serviceSessions;
}

template <class ReadFn>
static auto ReadCurrentMasterServerBrowserSessionAccess(MasterServerBrowser* browser, int index, ReadFn&& readSession)
{
    return std::forward<ReadFn>(readSession)(browser, index, GetCurrentMasterServerBrowserServiceSessions);
}

MasterServerBrowser* CreateMasterServerBrowser(const char* masterServerHost, void* instance,
                                               MasterServerBrowserProgressCallback progressCallback)
{
    MasterServerBrowser* browser = new MasterServerBrowser();
    browser->instance = instance;
    browser->progressCallback = progressCallback;
    browser->host = masterServerHost != nullptr ? masterServerHost : "";
    browser->serviceState = MasterServerBrowserState::Idle;
    return browser;
}

void DestroyMasterServerBrowser(MasterServerBrowser* browser)
{
    if (browser)
    {
        browser->joinWorker();
    }
    delete browser;
}

MasterServerBrowserState GetMasterServerBrowserState(MasterServerBrowser* browser)
{
    return browser != nullptr ? browser->serviceState : MasterServerBrowserState::Idle;
}

void HaltMasterServerBrowser(MasterServerBrowser* browser)
{
    if (browser != nullptr)
    {
        browser->serviceState = MasterServerBrowserState::Idle;
    }
}

void ClearMasterServerBrowser(MasterServerBrowser* browser)
{
    if (browser != nullptr)
    {
        browser->serviceSessions.clear();
        browser->pendingFetch.reset();
    }
}

void UpdateMasterServerBrowser(MasterServerBrowser* browser, const MasterServerBrowserFilter& filter)
{
    if (browser == nullptr)
    {
        return;
    }

    // A fetch is already in flight — don't stack another; the in-progress one
    // will land via ThinkMasterServerBrowser().
    if (browser->serviceState == MasterServerBrowserState::ListTransfer ||
        browser->serviceState == MasterServerBrowserState::Querying)
    {
        return;
    }

    browser->joinWorker(); // reap any finished worker not yet collected by Think

    std::string filterServerName = filter.serverName != nullptr ? filter.serverName : "";
    std::string filterMissionName = filter.missionName != nullptr ? filter.missionName : "";
    MasterServerBrowserFilter filterSnapshot = filter;
    filterSnapshot.serverName = filterServerName.c_str();
    filterSnapshot.missionName = filterMissionName.c_str();
    RString proxy = GetNetworkProxy();
    std::string proxySnapshot = proxy.GetLength() > 0 ? static_cast<const char*>(proxy) : "";

    browser->pendingFetch = std::make_shared<MasterServerBrowserFetchResult>();
    browser->fetchDone.store(false, std::memory_order_relaxed);
    browser->serviceState = MasterServerBrowserState::ListTransfer;

    const std::string host = browser->host;
    browser->worker = std::thread(
        [browser, host, filterServerName = std::move(filterServerName),
         filterMissionName = std::move(filterMissionName), filterSnapshot, proxySnapshot = std::move(proxySnapshot),
         pendingFetch = browser->pendingFetch]()
        {
            MasterServerBrowserState state = MasterServerBrowserState::ListTransfer;
            MasterServerBrowserFilter workerFilter = filterSnapshot;
            workerFilter.serverName = filterServerName.c_str();
            workerFilter.missionName = filterMissionName.c_str();
            const char* proxyServer = proxySnapshot.empty() ? nullptr : proxySnapshot.c_str();
            // No instance / progress callback off-thread — progress is reflected
            // to the UI via serviceState (ListTransfer → Idle), not a callback.
            auto fetchForTest = GMasterServerBrowserFetchForTest.load(std::memory_order_acquire);
            const bool fetchOk =
                fetchForTest != nullptr
                    ? fetchForTest(host.c_str(), workerFilter, proxyServer, pendingFetch->serviceSessions)
                    : RefreshMasterServerServiceBrowserFromDirectory(host.c_str(), workerFilter, proxyServer,
                                                                     pendingFetch->serviceSessions, state, nullptr,
                                                                     nullptr);
            if (!fetchOk)
            {
                LOG_WARN(Network, "Failed to refresh master server service browser from '{}'", host);
            }
            browser->fetchDone.store(true, std::memory_order_release);
        });
}

void ThinkMasterServerBrowser(MasterServerBrowser* browser)
{
    if (browser == nullptr)
    {
        return;
    }
    if (browser->serviceState != MasterServerBrowserState::ListTransfer &&
        browser->serviceState != MasterServerBrowserState::Querying)
    {
        return;
    }
    if (!browser->fetchDone.load(std::memory_order_acquire))
    {
        return; // still fetching
    }

    browser->joinWorker();
    if (browser->pendingFetch)
    {
        browser->serviceSessions = std::move(browser->pendingFetch->serviceSessions);
        browser->pendingFetch.reset();
    }
    else
    {
        browser->serviceSessions.clear();
    }
    browser->serviceState = MasterServerBrowserState::Idle;
    browser->fetchDone.store(false, std::memory_order_relaxed);
}

int GetMasterServerBrowserCount(MasterServerBrowser* browser)
{
    return browser != nullptr ? static_cast<int>(browser->serviceSessions.size()) : 0;
}

bool TryGetMasterServerBrowserSession(MasterServerBrowser* browser, int index, MasterServerSessionInfo& session)
{
    return ReadCurrentMasterServerBrowserSessionAccess(
        browser, index,
        [&](auto* currentBrowser, int currentIndex, auto&& getSessions)
        {
            auto* serviceSession = GetMasterServerBrowserServiceSession(
                currentBrowser, currentIndex, std::forward<decltype(getSessions)>(getSessions));
            if (serviceSession == nullptr)
            {
                return false;
            }
            AssignMasterServerServiceSessionInfo(*serviceSession, session);
            return true;
        });
}

const char* GetMasterServerBrowserStringValue(MasterServerBrowser* browser, int index, const char* key,
                                              const char* fallback)
{
    return ReadCurrentMasterServerBrowserSessionAccess(
        browser, index,
        [&](auto* currentBrowser, int currentIndex, auto&& getSessions)
        {
            auto* serviceSession = GetMasterServerBrowserServiceSession(
                currentBrowser, currentIndex, std::forward<decltype(getSessions)>(getSessions));
            return serviceSession != nullptr ? GetMasterServerServiceSessionStringValue(*serviceSession, key, fallback)
                                             : fallback;
        });
}

int GetMasterServerBrowserIntValue(MasterServerBrowser* browser, int index, const char* key, int fallback)
{
    return ReadCurrentMasterServerBrowserSessionAccess(
        browser, index,
        [&](auto* currentBrowser, int currentIndex, auto&& getSessions)
        {
            auto* serviceSession = GetMasterServerBrowserServiceSession(
                currentBrowser, currentIndex, std::forward<decltype(getSessions)>(getSessions));
            return serviceSession != nullptr ? GetMasterServerServiceSessionIntValue(*serviceSession, key, fallback)
                                             : fallback;
        });
}

int GetMasterServerBrowserPing(MasterServerBrowser* browser, int index)
{
    return ReadCurrentMasterServerBrowserSessionAccess(
        browser, index,
        [&](auto* currentBrowser, int currentIndex, auto&& getSessions)
        {
            auto* serviceSession = GetMasterServerBrowserServiceSession(
                currentBrowser, currentIndex, std::forward<decltype(getSessions)>(getSessions));
            return serviceSession != nullptr ? GetMasterServerServiceSessionPing(*serviceSession) : 0;
        });
}

const char* GetMasterServerBrowserAddress(MasterServerBrowser* browser, int index)
{
    return ReadCurrentMasterServerBrowserSessionAccess(
        browser, index,
        [&](auto* currentBrowser, int currentIndex, auto&& getSessions)
        {
            auto* serviceSession = GetMasterServerBrowserServiceSession(
                currentBrowser, currentIndex, std::forward<decltype(getSessions)>(getSessions));
            return serviceSession != nullptr ? GetMasterServerServiceSessionAddress(*serviceSession) : "";
        });
}

} // namespace Poseidon
