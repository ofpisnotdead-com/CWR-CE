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

struct MasterServerBrowserFetchJob
{
    std::string host;
    std::vector<MasterServerServiceSession> sessions;
    std::atomic<bool> done{false};
    std::string filterServerName;
    std::string filterMissionName;
    std::string proxyServer;
    MasterServerBrowserFilter filter{};
};

struct MasterServerBrowser
{
    void* instance = nullptr;
    MasterServerBrowserProgressCallback progressCallback = nullptr;
    std::string host;
    std::vector<MasterServerServiceSession> serviceSessions;
    MasterServerBrowserState serviceState = MasterServerBrowserState::Idle;

    // Async fetch: UpdateMasterServerBrowser() spawns `worker` to run the
    // blocking HTTP query off the main thread. The worker owns its fetch job
    // until done; ThinkMasterServerBrowser() then swaps its sessions into
    // serviceSessions on the main thread so UI-visible state is not mutated
    // from shutdown/HTTP worker code.
    std::thread worker;
    std::shared_ptr<MasterServerBrowserFetchJob> fetchJob;

    void joinWorker()
    {
        if (worker.joinable())
            worker.join();
    }
};

namespace
{
using MasterServerBrowserFetchFn = bool (*)(const char* masterServerHost, const MasterServerBrowserFilter& filter,
                                            const char* proxyServer, std::vector<MasterServerServiceSession>& sessions);

MasterServerBrowserFetchFn GMasterServerBrowserFetchOverride = nullptr;

RString BuildBrowserFilterString(const MasterServerBrowserFilter& filter)
{
    RString value;
    VisitMasterServerBrowserFilterTerms(
        filter, [&](const MasterServerBrowserFilterTerm& term)
        { AppendMasterServerBrowserFilterCondition(value, BuildMasterServerBrowserFilterCondition(term)); });

    return value;
}

bool FetchMasterServerBrowserSessions(const char* masterServerHost, const MasterServerBrowserFilter& filter,
                                      const char* proxyServer, std::vector<MasterServerServiceSession>& sessions)
{
    if (GMasterServerBrowserFetchOverride != nullptr)
    {
        return GMasterServerBrowserFetchOverride(masterServerHost, filter, proxyServer, sessions);
    }

    MasterServerBrowserState state = MasterServerBrowserState::ListTransfer;
    return RefreshMasterServerServiceBrowserFromDirectory(masterServerHost, filter, proxyServer, sessions, state,
                                                          nullptr, nullptr);
}
} // namespace

void SetMasterServerBrowserFetchForTest(MasterServerBrowserFetchFn fetch)
{
    GMasterServerBrowserFetchOverride = fetch;
}

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
        browser->fetchJob.reset();
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
        browser->joinWorker();
        browser->fetchJob.reset();
        browser->serviceSessions.clear();
        browser->serviceState = MasterServerBrowserState::Idle;
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
    browser->fetchJob.reset();

    auto job = std::make_shared<MasterServerBrowserFetchJob>();
    job->host = browser->host;
    job->filterServerName = filter.serverName != nullptr ? filter.serverName : "";
    job->filterMissionName = filter.missionName != nullptr ? filter.missionName : "";
    job->filter = filter;
    job->filter.serverName = job->filterServerName.c_str();
    job->filter.missionName = job->filterMissionName.c_str();
    RString proxy = GetNetworkProxy();
    job->proxyServer = proxy.GetLength() > 0 ? static_cast<const char*>(proxy) : "";

    browser->fetchJob = job;
    browser->serviceState = MasterServerBrowserState::ListTransfer;

    browser->worker = std::thread(
        [job]()
        {
            const char* proxyServer = job->proxyServer.empty() ? nullptr : job->proxyServer.c_str();
            if (!FetchMasterServerBrowserSessions(job->host.c_str(), job->filter, proxyServer, job->sessions))
            {
                LOG_WARN(Network, "Failed to refresh master server service browser from '{}'", job->host);
            }
            job->done.store(true, std::memory_order_release);
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
    auto job = browser->fetchJob;
    if (job == nullptr || !job->done.load(std::memory_order_acquire))
    {
        return; // still fetching
    }

    browser->joinWorker();
    browser->serviceSessions = std::move(job->sessions);
    browser->fetchJob.reset();
    browser->serviceState = MasterServerBrowserState::Idle;
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
