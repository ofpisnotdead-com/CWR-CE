#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <Poseidon/Core/DownloadWorker.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <stdint.h>
#include <catch2/matchers/catch_matchers.hpp>
#include <functional>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using Poseidon::DownloadFileFn;
using Poseidon::DownloadFileResult;
using Poseidon::DownloadProgress;
using Poseidon::DownloadSnapshot;
using Poseidon::DownloadTask;
using Poseidon::DownloadWorker;
using Poseidon::RunDownloadJobs;

namespace
{
// A clock the fake transport advances deterministically.
struct FakeClock
{
    double t = 0.0;
    std::function<double()> Fn()
    {
        return [this]() { return t; };
    }
};

// Streams `total` bytes in two halves, advancing the clock 1 s per half.
DownloadFileFn TwoHalfDownload(FakeClock& clock)
{
    return [&clock](const DownloadTask& task, const std::function<void(int64_t, int64_t)>& onBytes,
                    const std::function<bool()>& /*cancelled*/, std::string& /*error*/) -> DownloadFileResult
    {
        clock.t += 1.0;
        onBytes(task.expectedBytes / 2, task.expectedBytes);
        clock.t += 1.0;
        onBytes(task.expectedBytes, task.expectedBytes);
        return DownloadFileResult::Success;
    };
}
} // namespace

TEST_CASE("RunDownloadJobs completes a multi-file job and finishes at 100%", "[download][worker]")
{
    FakeClock clock;
    DownloadProgress progress;
    std::mutex mtx;
    std::atomic<bool> cancel{false};

    std::vector<DownloadTask> tasks = {{"@a", "http://a", "a.pbo", 100, {}}, {"@b", "http://b", "b.pbo", 300, {}}};

    RunDownloadJobs(tasks, progress, mtx, TwoHalfDownload(clock), clock.Fn(), cancel);

    CHECK(progress.IsDone());
    CHECK_FALSE(progress.IsFailed());
    CHECK(progress.ItemCount() == 2);
    CHECK(progress.OverallReceived() == 400);
    CHECK_THAT(progress.OverallFraction(), WithinAbs(1.0f, 1e-4f));
}

TEST_CASE("RunDownloadJobs feeds streamed bytes into the shared progress", "[download][worker]")
{
    FakeClock clock;
    DownloadProgress progress;
    std::mutex mtx;
    std::atomic<bool> cancel{false};

    // Mid-stream, the overall received must already reflect the bytes seen so far.
    DownloadFileFn observing = [&](const DownloadTask& task, const std::function<void(int64_t, int64_t)>& onBytes,
                                   const std::function<bool()>&, std::string&) -> DownloadFileResult
    {
        clock.t += 1.0;
        onBytes(40, task.expectedBytes);
        CHECK(progress.OverallReceived() == 40);
        CHECK_THAT(progress.CurrentFraction(), WithinAbs(0.4f, 1e-4f));
        onBytes(task.expectedBytes, task.expectedBytes);
        return DownloadFileResult::Success;
    };

    std::vector<DownloadTask> tasks = {{"@only", "http://x", "x.pbo", 100, {}}};
    RunDownloadJobs(tasks, progress, mtx, observing, clock.Fn(), cancel);
    CHECK(progress.IsDone());
}

TEST_CASE("RunDownloadJobs runs a task post-step after the file lands", "[download][worker]")
{
    FakeClock clock;
    DownloadProgress progress;
    std::mutex mtx;
    std::atomic<bool> cancel{false};

    bool unpacked = false;
    DownloadTask task{"@a", "http://a", "a.pbo", 100, {}};
    task.postStep = [&](const DownloadTask& t, std::string&) -> bool
    {
        // The file must already be fully downloaded when the post-step runs.
        CHECK(t.destPath == "a.pbo");
        CHECK(progress.CurrentReceived() == 100);
        unpacked = true;
        return true;
    };

    RunDownloadJobs({task}, progress, mtx, TwoHalfDownload(clock), clock.Fn(), cancel);
    CHECK(unpacked);
    CHECK(progress.IsDone());
}

TEST_CASE("RunDownloadJobs fails and skips the rest on a permanent download error", "[download][worker]")
{
    FakeClock clock;
    DownloadProgress progress;
    std::mutex mtx;
    std::atomic<bool> cancel{false};

    int calls = 0;
    DownloadFileFn failFirst = [&](const DownloadTask&, const std::function<void(int64_t, int64_t)>&,
                                   const std::function<bool()>&, std::string& error) -> DownloadFileResult
    {
        ++calls;
        error = "connection reset";
        return DownloadFileResult::PermanentFailure;
    };

    std::vector<DownloadTask> tasks = {{"@a", "http://a", "a.pbo", 100, {}}, {"@b", "http://b", "b.pbo", 100, {}}};
    RunDownloadJobs(tasks, progress, mtx, failFirst, clock.Fn(), cancel);

    CHECK(calls == 1); // second task never attempted
    CHECK(progress.IsFailed());
    CHECK_FALSE(progress.IsDone());
    CHECK(progress.Error() == "connection reset");
}

TEST_CASE("RunDownloadJobs retries a transient download error", "[download][worker][retry]")
{
    FakeClock clock;
    DownloadProgress progress;
    std::mutex mtx;
    std::atomic<bool> cancel{false};

    int attempts = 0;
    int64_t partialBytes = 0;
    DownloadFileFn interrupted = [&](const DownloadTask& task, const std::function<void(int64_t, int64_t)>& onBytes,
                                     const std::function<bool()>&, std::string& error) -> DownloadFileResult
    {
        ++attempts;
        if (attempts == 1)
        {
            partialBytes = 40;
            onBytes(partialBytes, task.expectedBytes);
            error = "connection reset";
            return DownloadFileResult::TransientFailure;
        }
        onBytes(task.expectedBytes, task.expectedBytes);
        return DownloadFileResult::Success;
    };

    RunDownloadJobs({{"@a", "http://a", "a.pbo", 100, {}}}, progress, mtx, interrupted, clock.Fn(), cancel);

    CHECK(partialBytes == 40);
    CHECK(attempts == 2);
    CHECK(progress.IsDone());
    CHECK_FALSE(progress.IsFailed());
}

TEST_CASE("RunDownloadJobs continues after repeated transient download errors", "[download][worker][retry]")
{
    FakeClock clock;
    DownloadProgress progress;
    std::mutex mtx;
    std::atomic<bool> cancel{false};

    int attempts = 0;
    std::vector<int> retryNumbers;
    DownloadFileFn interrupted = [&](const DownloadTask& task, const std::function<void(int64_t, int64_t)>& onBytes,
                                     const std::function<bool()>&, std::string& error) -> DownloadFileResult
    {
        ++attempts;
        if (attempts == 6)
        {
            onBytes(task.expectedBytes, task.expectedBytes);
            return DownloadFileResult::Success;
        }
        error = "connection reset";
        return DownloadFileResult::TransientFailure;
    };

    auto recordWait = [&](int retryNumber, const std::function<bool()>&) { retryNumbers.push_back(retryNumber); };
    RunDownloadJobs({{"@a", "http://a", "a.pbo", 100, {}}}, progress, mtx, interrupted, clock.Fn(), cancel, nullptr,
                    recordWait);

    CHECK(attempts == 6);
    CHECK(retryNumbers == std::vector<int>{1, 2, 3, 3, 3});
    CHECK(progress.IsDone());
    CHECK_FALSE(progress.IsFailed());
}

TEST_CASE("RunDownloadJobs does not retry a cancelled transfer", "[download][worker][retry]")
{
    FakeClock clock;
    DownloadProgress progress;
    std::mutex mtx;
    std::atomic<bool> cancel{false};

    int attempts = 0;
    DownloadFileFn cancelled = [&](const DownloadTask&, const std::function<void(int64_t, int64_t)>&,
                                   const std::function<bool()>&, std::string&) -> DownloadFileResult
    {
        ++attempts;
        return DownloadFileResult::Cancelled;
    };

    RunDownloadJobs({{"@a", "http://a", "a.pbo", 100, {}}}, progress, mtx, cancelled, clock.Fn(), cancel);

    CHECK(attempts == 1);
    CHECK_FALSE(progress.IsDone());
    CHECK_FALSE(progress.IsFailed());
}

TEST_CASE("RunDownloadJobs stops when cancelled during retry backoff", "[download][worker][retry]")
{
    FakeClock clock;
    DownloadProgress progress;
    std::mutex mtx;
    std::atomic<bool> cancel{false};

    int attempts = 0;
    DownloadFileFn interrupted = [&](const DownloadTask&, const std::function<void(int64_t, int64_t)>&,
                                     const std::function<bool()>&, std::string&) -> DownloadFileResult
    {
        ++attempts;
        return DownloadFileResult::TransientFailure;
    };
    auto cancelDuringWait = [&](int, const std::function<bool()>&) { cancel.store(true); };

    RunDownloadJobs({{"@a", "http://a", "a.pbo", 100, {}}}, progress, mtx, interrupted, clock.Fn(), cancel, nullptr,
                    cancelDuringWait);

    CHECK(attempts == 1);
    CHECK_FALSE(progress.IsDone());
    CHECK_FALSE(progress.IsFailed());
}

TEST_CASE("RunDownloadJobs fails when a post-step fails", "[download][worker]")
{
    FakeClock clock;
    DownloadProgress progress;
    std::mutex mtx;
    std::atomic<bool> cancel{false};

    DownloadTask task{"@a", "http://a", "a.pbo", 100, {}};
    task.postStep = [&](const DownloadTask&, std::string& error) -> bool
    {
        error = "corrupt archive";
        return false;
    };

    RunDownloadJobs({task}, progress, mtx, TwoHalfDownload(clock), clock.Fn(), cancel);
    CHECK(progress.IsFailed());
    CHECK_FALSE(progress.IsDone());
    CHECK(progress.Error() == "corrupt archive");
    // The bytes fully streamed (bar reads 100%); it is the install that failed,
    // surfaced via IsFailed rather than by rewinding the bar.
    CHECK_THAT(progress.OverallFraction(), WithinAbs(1.0f, 1e-4f));
}

TEST_CASE("RunDownloadJobs stops cleanly when cancelled between files", "[download][worker]")
{
    FakeClock clock;
    DownloadProgress progress;
    std::mutex mtx;
    std::atomic<bool> cancel{false};

    int calls = 0;
    DownloadFileFn cancelAfterFirst = [&](const DownloadTask& task,
                                          const std::function<void(int64_t, int64_t)>& onBytes,
                                          const std::function<bool()>&, std::string&) -> DownloadFileResult
    {
        ++calls;
        onBytes(task.expectedBytes, task.expectedBytes);
        cancel.store(true); // user hit Cancel during the first file
        return DownloadFileResult::Success;
    };

    std::vector<DownloadTask> tasks = {{"@a", "http://a", "a.pbo", 100, {}}, {"@b", "http://b", "b.pbo", 100, {}}};
    RunDownloadJobs(tasks, progress, mtx, cancelAfterFirst, clock.Fn(), cancel);

    CHECK(calls == 1);              // second file never started
    CHECK_FALSE(progress.IsDone()); // cancelled, not finished
    CHECK_FALSE(progress.IsFailed());
}

TEST_CASE("RunDownloadJobs finishes immediately for an empty job", "[download][worker]")
{
    FakeClock clock;
    DownloadProgress progress;
    std::mutex mtx;
    std::atomic<bool> cancel{false};

    int calls = 0;
    DownloadFileFn never = [&](const DownloadTask&, const std::function<void(int64_t, int64_t)>&,
                               const std::function<bool()>&, std::string&) -> DownloadFileResult
    {
        ++calls;
        return DownloadFileResult::Success;
    };

    RunDownloadJobs({}, progress, mtx, never, clock.Fn(), cancel);
    CHECK(calls == 0);
    CHECK(progress.IsDone());
}

TEST_CASE("DownloadWorker runs the job on a thread and Poll reports completion", "[download][worker][thread]")
{
    FakeClock clock;
    DownloadWorker worker(TwoHalfDownload(clock), clock.Fn());

    std::vector<DownloadTask> tasks = {{"@a", "http://a", "a.pbo", 100, {}}, {"@b", "http://b", "b.pbo", 100, {}}};
    worker.Start(tasks);
    worker.Wait(); // join — the fake transport returns promptly

    DownloadSnapshot s = worker.Poll();
    CHECK(s.done);
    CHECK_FALSE(s.failed);
    CHECK(s.itemCount == 2);
    CHECK_THAT(s.overallFraction, WithinAbs(1.0f, 1e-4f));
    CHECK_FALSE(worker.Running());
}

TEST_CASE("DownloadWorker reports post-processing after download completion", "[download][worker][thread]")
{
    FakeClock clock;
    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};
    DownloadTask task{"@a", "http://a", "a.pbo", 100, {}};
    task.postStep = [&](const DownloadTask&, std::string&) -> bool
    {
        entered.store(true);
        while (!release.load())
            std::this_thread::yield();
        return true;
    };

    DownloadWorker worker(TwoHalfDownload(clock), clock.Fn());
    worker.Start({task});
    while (!entered.load())
        std::this_thread::yield();

    const DownloadSnapshot unpacking = worker.Poll();
    CHECK(unpacking.postProcessing);
    CHECK_FALSE(unpacking.done);
    CHECK_THAT(unpacking.currentFraction, WithinAbs(1.0f, 1e-4f));

    release.store(true);
    worker.Wait();
    const DownloadSnapshot done = worker.Poll();
    CHECK_FALSE(done.postProcessing);
    CHECK(done.done);
}

TEST_CASE("DownloadWorker: teardown does not block on a stuck download (cancel freeze)", "[download][worker][thread]")
{
    // A download stuck in a blocking transport that doesn't honour cancel
    // promptly — it sleeps a fixed spell. Destroying the worker mid-download must
    // DETACH such a thread, never join it: joining froze the game on Cancel (the
    // UI thread waited for the whole transfer to finish).
    std::atomic<bool> entered{false};
    DownloadFileFn stuck = [&entered](const DownloadTask&, const std::function<void(int64_t, int64_t)>&,
                                      const std::function<bool()>&, std::string&) -> DownloadFileResult
    {
        entered.store(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        return DownloadFileResult::Success;
    };

    const auto start = std::chrono::steady_clock::now();
    {
        DownloadWorker worker(stuck);
        worker.Start({{"@x", "http://x", "x.pbo", 1000, {}}});
        while (!entered.load()) // ensure the thread is inside the blocking transport
            std::this_thread::yield();
        worker.Cancel();
    } // ~DownloadWorker here — must return at once (detach), not wait the 800ms transport
    const auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

    // Detach -> teardown is immediate; the old join-on-destroy waited ~800ms (the
    // freeze). Generous bound so it can't flake on a busy CI box.
    CHECK(elapsedMs < 400);

    // Let the detached thread (still alive on its shared Session) run out before
    // the test ends, so it doesn't outlive `entered`.
    std::this_thread::sleep_for(std::chrono::milliseconds(900));
}
