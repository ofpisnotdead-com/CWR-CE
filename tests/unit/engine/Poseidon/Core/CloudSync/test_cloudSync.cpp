#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <Poseidon/Core/CloudSync/CloudSync.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <unordered_map>

using Poseidon::RunSyncJobs;
using Poseidon::SyncDirection;
using Poseidon::SyncOpsEnv;
using Poseidon::SyncPair;
using Poseidon::SyncProgress;
using Poseidon::SyncSnapshot;
using Poseidon::SyncWorker;

namespace
{
// An in-memory stand-in for a local dir <-> container dir pair. Each side is
// just relPath -> mtime; pull/push "copy" by moving the mtime across (real
// content never matters to RunSyncJobs, which only compares mtimes).
struct FakeFs
{
    std::unordered_map<std::string, double> local;
    std::unordered_map<std::string, double> remote;
    std::vector<std::string> pulled; // relPaths, in copy order
    std::vector<std::string> pushed;
};

SyncOpsEnv MakeFakeEnv(FakeFs& fs)
{
    SyncOpsEnv env;
    env.listLocal = [&](const std::string&) -> std::vector<std::pair<std::string, double>>
    { return {fs.local.begin(), fs.local.end()}; };
    env.listContainer = [&](const std::string&) -> std::vector<std::pair<std::string, double>>
    { return {fs.remote.begin(), fs.remote.end()}; };
    env.pullFile = [&](const std::string&, const std::string&, const std::string& relPath, std::string&) -> bool
    {
        fs.local[relPath] = fs.remote.at(relPath);
        fs.pulled.push_back(relPath);
        return true;
    };
    env.pushFile = [&](const std::string&, const std::string&, const std::string& relPath, std::string&) -> bool
    {
        fs.remote[relPath] = fs.local.at(relPath);
        fs.pushed.push_back(relPath);
        return true;
    };
    return env;
}

const std::vector<SyncPair> kOnePair = {{"local/Users", "Users"}};
} // namespace

TEST_CASE("RunSyncJobs pulls a remote-only file", "[cloudSync][worker]")
{
    FakeFs fs;
    fs.remote["a.cfg"] = 100.0;
    SyncOpsEnv env = MakeFakeEnv(fs);
    SyncProgress progress;
    std::mutex mtx;
    std::atomic<bool> cancel{false};

    RunSyncJobs(kOnePair, SyncDirection::Pull, progress, mtx, env, cancel);

    CHECK(progress.done);
    CHECK_FALSE(progress.failed);
    CHECK(fs.local.count("a.cfg") == 1);
    CHECK(fs.pulled == std::vector<std::string>{"a.cfg"});
    CHECK(fs.pushed.empty());
}

TEST_CASE("RunSyncJobs pushes a local-only file", "[cloudSync][worker]")
{
    FakeFs fs;
    fs.local["a.cfg"] = 100.0;
    SyncOpsEnv env = MakeFakeEnv(fs);
    SyncProgress progress;
    std::mutex mtx;
    std::atomic<bool> cancel{false};

    RunSyncJobs(kOnePair, SyncDirection::Push, progress, mtx, env, cancel);

    CHECK(progress.done);
    CHECK(fs.remote.count("a.cfg") == 1);
    CHECK(fs.pushed == std::vector<std::string>{"a.cfg"});
    CHECK(fs.pulled.empty());
}

TEST_CASE("RunSyncJobs Pull direction never pushes, even if local is newer", "[cloudSync][worker]")
{
    FakeFs fs;
    fs.local["a.cfg"] = 200.0;
    fs.remote["a.cfg"] = 100.0;
    SyncOpsEnv env = MakeFakeEnv(fs);
    SyncProgress progress;
    std::mutex mtx;
    std::atomic<bool> cancel{false};

    RunSyncJobs(kOnePair, SyncDirection::Pull, progress, mtx, env, cancel);

    CHECK(progress.done);
    CHECK(fs.pulled.empty()); // remote is older, nothing to pull
    CHECK(fs.pushed.empty()); // Pull never pushes regardless of mtimes
    CHECK(fs.local["a.cfg"] == 200.0);
}

TEST_CASE("RunSyncJobs Both direction: newest side wins per file", "[cloudSync][worker]")
{
    FakeFs fs;
    fs.local["newer_local.cfg"] = 200.0;
    fs.remote["newer_local.cfg"] = 100.0;
    fs.local["newer_remote.cfg"] = 100.0;
    fs.remote["newer_remote.cfg"] = 200.0;
    fs.local["in_sync.cfg"] = 150.0;
    fs.remote["in_sync.cfg"] = 150.0;
    SyncOpsEnv env = MakeFakeEnv(fs);
    SyncProgress progress;
    std::mutex mtx;
    std::atomic<bool> cancel{false};

    RunSyncJobs(kOnePair, SyncDirection::Both, progress, mtx, env, cancel);

    CHECK(progress.done);
    CHECK(fs.pushed == std::vector<std::string>{"newer_local.cfg"});
    CHECK(fs.pulled == std::vector<std::string>{"newer_remote.cfg"});
    CHECK(progress.itemCount == 2); // in_sync.cfg needed no operation at all
}

TEST_CASE("RunSyncJobs Both direction: missing side always copies regardless of direction of missingness",
         "[cloudSync][worker]")
{
    FakeFs fs;
    fs.local["only_local.cfg"] = 50.0;
    fs.remote["only_remote.cfg"] = 50.0;
    SyncOpsEnv env = MakeFakeEnv(fs);
    SyncProgress progress;
    std::mutex mtx;
    std::atomic<bool> cancel{false};

    RunSyncJobs(kOnePair, SyncDirection::Both, progress, mtx, env, cancel);

    CHECK(fs.pushed == std::vector<std::string>{"only_local.cfg"});
    CHECK(fs.pulled == std::vector<std::string>{"only_remote.cfg"});
}

TEST_CASE("RunSyncJobs fails and skips the rest on a copy error", "[cloudSync][worker]")
{
    FakeFs fs;
    fs.remote["a.cfg"] = 1.0;
    fs.remote["b.cfg"] = 1.0;
    SyncOpsEnv env = MakeFakeEnv(fs);
    env.pullFile = [](const std::string&, const std::string&, const std::string&, std::string& error) -> bool
    {
        error = "container unavailable";
        return false;
    };
    SyncProgress progress;
    std::mutex mtx;
    std::atomic<bool> cancel{false};

    RunSyncJobs(kOnePair, SyncDirection::Pull, progress, mtx, env, cancel);

    CHECK(progress.failed);
    CHECK_FALSE(progress.done);
    CHECK(progress.error == "container unavailable");
}

TEST_CASE("RunSyncJobs stops cleanly when cancelled between files", "[cloudSync][worker]")
{
    FakeFs fs;
    fs.remote["a.cfg"] = 1.0;
    fs.remote["b.cfg"] = 1.0;
    SyncOpsEnv env = MakeFakeEnv(fs);
    std::atomic<bool> cancel{false};
    env.pullFile = [&](const std::string&, const std::string&, const std::string& relPath, std::string&) -> bool
    {
        fs.pulled.push_back(relPath);
        cancel.store(true); // "user" cancels mid-file
        return true;
    };
    SyncProgress progress;
    std::mutex mtx;

    RunSyncJobs(kOnePair, SyncDirection::Pull, progress, mtx, env, cancel);

    CHECK(fs.pulled.size() == 1); // second file never attempted
    CHECK_FALSE(progress.done);
    CHECK_FALSE(progress.failed);
}

TEST_CASE("RunSyncJobs finishes immediately with no pairs", "[cloudSync][worker]")
{
    FakeFs fs;
    SyncOpsEnv env = MakeFakeEnv(fs);
    SyncProgress progress;
    std::mutex mtx;
    std::atomic<bool> cancel{false};

    RunSyncJobs({}, SyncDirection::Both, progress, mtx, env, cancel);

    CHECK(progress.done);
    CHECK(progress.itemCount == 0);
}

TEST_CASE("SyncWorker runs the job on a thread and Poll reports completion", "[cloudSync][worker][thread]")
{
    FakeFs fs;
    fs.remote["a.cfg"] = 1.0;
    SyncWorker worker(MakeFakeEnv(fs));

    worker.Start(kOnePair, SyncDirection::Pull);
    worker.Wait(); // join — the fake copy returns promptly

    SyncSnapshot s = worker.Poll();
    CHECK(s.done);
    CHECK_FALSE(s.failed);
    CHECK(s.itemCount == 1);
    CHECK_FALSE(worker.Running());
}

TEST_CASE("SyncWorker: teardown does not block on a stuck copy", "[cloudSync][worker][thread]")
{
    // Mirrors DownloadWorker's equivalent test: a copy stuck in a blocking
    // call that doesn't honour cancel promptly. Destroying the worker
    // mid-copy must DETACH such a thread, never join it, so quitting the
    // game can't hang on a stuck iCloud daemon.
    FakeFs fs;
    fs.remote["a.cfg"] = 1.0;
    std::atomic<bool> entered{false};
    SyncOpsEnv env = MakeFakeEnv(fs);
    env.pullFile = [&](const std::string&, const std::string&, const std::string&, std::string&) -> bool
    {
        entered.store(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        return true;
    };

    const auto start = std::chrono::steady_clock::now();
    {
        SyncWorker worker(std::move(env));
        worker.Start(kOnePair, SyncDirection::Pull);
        while (!entered.load())
            std::this_thread::yield();
        worker.Cancel();
    } // ~SyncWorker here — must return at once (detach), not wait the 800ms copy
    const auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

    CHECK(elapsedMs < 400);

    // Let the detached thread run out before the test ends.
    std::this_thread::sleep_for(std::chrono::milliseconds(900));
}

TEST_CASE("SyncWorker: Start() while running coalesces a rerun instead of cancelling",
         "[cloudSync][worker][thread]")
{
    // Regression test for the CloudSync push bug found via issue #88: a burst
    // of saves in quick succession (NextMission() then AddMission()) each
    // called PushInBackground(), and the old Start() cancelled+detached
    // whatever push was already in flight -- so a big enough save could mean
    // NONE of several attempted pushes ever completed. Start() must instead
    // let the in-flight run finish and then loop once more, so a file that
    // only exists because of the second Start() call still gets synced.
    FakeFs fs;
    fs.remote["a.cfg"] = 1.0;
    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};
    SyncOpsEnv env = MakeFakeEnv(fs);
    env.pullFile = [&](const std::string&, const std::string&, const std::string& relPath, std::string&) -> bool
    {
        entered.store(true);
        while (!release.load())
            std::this_thread::yield();
        fs.local[relPath] = fs.remote.at(relPath);
        fs.pulled.push_back(relPath);
        return true;
    };

    SyncWorker worker(std::move(env));
    worker.Start(kOnePair, SyncDirection::Pull);
    while (!entered.load())
        std::this_thread::yield();

    // A change lands only now, after the first run's file-list snapshot was
    // already taken -- simulating a save that happens mid-push.
    fs.remote["b.cfg"] = 1.0;
    worker.Start(kOnePair, SyncDirection::Pull); // must coalesce, not cancel

    CHECK(worker.Running()); // still the same in-flight run, not restarted
    release.store(true);     // let the first file's copy finish
    worker.Wait();           // joins only once the coalesced rerun also finishes

    SyncSnapshot s = worker.Poll();
    CHECK(s.done);
    CHECK_FALSE(s.failed);
    CHECK(fs.local.count("a.cfg") == 1);
    CHECK(fs.local.count("b.cfg") == 1); // picked up by the coalesced rerun
}
