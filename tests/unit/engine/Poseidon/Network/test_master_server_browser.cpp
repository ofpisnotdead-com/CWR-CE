#include <Poseidon/Foundation/PoseidonPCH.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <Poseidon/Network/MasterServerBrowser.hpp>
#include <Poseidon/Network/MasterServerProtocol.hpp>
#include <Poseidon/Network/MasterServerServiceClient.hpp>

namespace Poseidon
{
using MasterServerBrowserFetchForTest = bool (*)(const char* masterServerHost, const MasterServerBrowserFilter& filter,
                                                 const char* proxyServer,
                                                 std::vector<MasterServerServiceSession>& sessions);
void SetMasterServerBrowserFetchForTest(MasterServerBrowserFetchForTest fetch);

namespace
{
struct FetchOverrideGuard
{
    explicit FetchOverrideGuard(MasterServerBrowserFetchForTest fetch) { SetMasterServerBrowserFetchForTest(fetch); }
    ~FetchOverrideGuard() { SetMasterServerBrowserFetchForTest(nullptr); }
};

std::mutex GFetchMutex;
std::condition_variable GFetchCv;
bool GFetchStarted = false;
bool GFetchRelease = false;
bool GFetchReturned = false;

void ResetBlockingFetch()
{
    std::lock_guard lock(GFetchMutex);
    GFetchStarted = false;
    GFetchRelease = false;
    GFetchReturned = false;
}

bool BlockingFetch(const char*, const MasterServerBrowserFilter&, const char*,
                   std::vector<MasterServerServiceSession>& sessions)
{
    std::unique_lock lock(GFetchMutex);
    GFetchStarted = true;
    GFetchCv.notify_all();
    GFetchCv.wait(lock, [] { return GFetchRelease; });
    lock.unlock();

    MasterServerServiceSession session;
    AssignMasterServerServiceSession(session, "127.0.0.1", 2302, "host", "mission", "sockets", 1, 1, "test", false, 0,
                                     42, 1, 8, 15, "", false);
    sessions.push_back(std::move(session));
    {
        std::lock_guard returnedLock(GFetchMutex);
        GFetchReturned = true;
    }
    GFetchCv.notify_all();
    return true;
}

bool ImmediateFetch(const char*, const MasterServerBrowserFilter&, const char*,
                    std::vector<MasterServerServiceSession>& sessions)
{
    MasterServerServiceSession session;
    AssignMasterServerServiceSession(session, "127.0.0.1", 2302, "host", "mission", "sockets", 1, 1, "test", false, 0,
                                     42, 1, 8, 15, "", false);
    sessions.push_back(std::move(session));
    return true;
}
} // namespace

TEST_CASE("MasterServerBrowser destroy joins an in-flight fetch", "[network][master-server]")
{
    ResetBlockingFetch();
    FetchOverrideGuard fetch(BlockingFetch);

    MasterServerBrowser* browser = CreateMasterServerBrowser("https://master.example", nullptr, nullptr);
    MasterServerBrowserFilter filter{};
    UpdateMasterServerBrowser(browser, filter);

    {
        std::unique_lock lock(GFetchMutex);
        REQUIRE(GFetchCv.wait_for(lock, std::chrono::seconds(2), [] { return GFetchStarted; }));
    }

    std::atomic<bool> destroyed{false};
    std::thread destroyer(
        [&]
        {
            DestroyMasterServerBrowser(browser);
            destroyed.store(true, std::memory_order_release);
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    REQUIRE_FALSE(destroyed.load(std::memory_order_acquire));

    {
        std::lock_guard lock(GFetchMutex);
        GFetchRelease = true;
    }
    GFetchCv.notify_all();
    destroyer.join();

    REQUIRE(destroyed.load(std::memory_order_acquire));
}

TEST_CASE("MasterServerBrowser clear discards an in-flight fetch result", "[network][master-server]")
{
    ResetBlockingFetch();
    FetchOverrideGuard fetch(BlockingFetch);

    MasterServerBrowser* browser = CreateMasterServerBrowser("https://master.example", nullptr, nullptr);
    MasterServerBrowserFilter filter{};
    UpdateMasterServerBrowser(browser, filter);

    {
        std::unique_lock lock(GFetchMutex);
        REQUIRE(GFetchCv.wait_for(lock, std::chrono::seconds(2), [] { return GFetchStarted; }));
        GFetchRelease = true;
    }
    GFetchCv.notify_all();

    {
        std::unique_lock lock(GFetchMutex);
        REQUIRE(GFetchCv.wait_for(lock, std::chrono::seconds(2), [] { return GFetchReturned; }));
    }

    ClearMasterServerBrowser(browser);
    ThinkMasterServerBrowser(browser);

    REQUIRE(GetMasterServerBrowserState(browser) == MasterServerBrowserState::Idle);
    REQUIRE(GetMasterServerBrowserCount(browser) == 0);

    DestroyMasterServerBrowser(browser);
}

TEST_CASE("MasterServerBrowser publishes worker results only from Think", "[network][master-server]")
{
    FetchOverrideGuard fetch(ImmediateFetch);

    MasterServerBrowser* browser = CreateMasterServerBrowser("https://master.example", nullptr, nullptr);
    MasterServerBrowserFilter filter{};
    UpdateMasterServerBrowser(browser, filter);

    for (int i = 0; i < 200 && GetMasterServerBrowserState(browser) != MasterServerBrowserState::Idle; ++i)
    {
        ThinkMasterServerBrowser(browser);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    REQUIRE(GetMasterServerBrowserState(browser) == MasterServerBrowserState::Idle);
    REQUIRE(GetMasterServerBrowserCount(browser) == 1);

    MasterServerSessionInfo info;
    REQUIRE(TryGetMasterServerBrowserSession(browser, 0, info));
    REQUIRE(std::string(info.address) == "127.0.0.1");
    REQUIRE(info.hostPort == 2302);

    DestroyMasterServerBrowser(browser);
}

} // namespace Poseidon
