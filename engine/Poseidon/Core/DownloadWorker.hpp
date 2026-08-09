#pragma once

// Agnostic sequential download worker.  Runs a list of DownloadTasks one after
// another on a background thread, feeding a DownloadProgress that the UI polls
// each frame.  Knows nothing about mods or the master server: the transport
// (how a file is fetched) and the clock are injected, so the same worker drives
// the MODS addon download (N PBOs, each unpacked into a mod dir via the task's
// post-step) and the multiplayer single-mission transfer (one PBO, no post-step).
//
// The orchestration lives in RunDownloadJobs() — a pure, synchronous function
// with every effect injected, so it unit-tests without a thread or real I/O.
// DownloadWorker is the thin thread + mutex wrapper around it; the UI only
// touches Poll().

#include <Poseidon/Core/DownloadProgress.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Poseidon
{
struct DownloadTask
{
    std::string label;         ///< shown in the UI (e.g. "@ww4" or "mission.pbo")
    std::string url;           ///< remote source
    std::string destPath;      ///< local destination for the downloaded file
    int64_t expectedBytes = 0; ///< total for the progress bar; 0 == unknown (indeterminate)
    /// Optional step run after the file lands (e.g. unpack a PBO into a mod dir).
    /// Empty for a plain download such as the MP mission transfer.  Returns false
    /// (and sets `error`) to fail the whole job.
    std::function<bool(const DownloadTask& task, std::string& error)> postStep;
};

// Transport: fetch task.url -> task.destPath.  Call onBytes(received, total) as
// the file streams (received/total are for THIS file).  Poll cancelled() and
// abort early if it returns true.  Return false and set `error` on failure.
using DownloadFileFn =
    std::function<bool(const DownloadTask& task, const std::function<void(int64_t received, int64_t total)>& onBytes,
                       const std::function<bool()>& cancelled, std::string& error)>;

// Pure orchestration: drives `progress` (guarded by `mtx` so a concurrent
// reader can Poll it) through every task using the injected `download` and
// `now`.  Stops early — leaving progress neither done nor failed — when
// `cancel` flips true between or during a file.  Sets progress failed on the
// first download/post-step failure and skips the rest.  Finishes when all
// tasks complete.
void RunDownloadJobs(const std::vector<DownloadTask>& tasks, DownloadProgress& progress, std::mutex& mtx,
                     const DownloadFileFn& download, const std::function<double()>& now,
                     const std::atomic<bool>& cancel, std::atomic<bool>* postProcessing = nullptr);

struct DownloadSnapshot
{
    int itemCount = 0;
    int currentIndex = -1;
    std::string currentLabel;
    float currentFraction = 0.0f;
    float overallFraction = 0.0f;
    int64_t overallReceived = 0;
    int64_t overallTotal = 0;
    double speedBytesPerSec = 0.0;
    double etaSeconds = -1.0;
    bool done = false;
    bool failed = false;
    bool cancelled = false;
    bool postProcessing = false;
    std::string error;
};

class DownloadWorker
{
  public:
    // `now` defaults to a monotonic steady-clock seconds source when empty.
    explicit DownloadWorker(DownloadFileFn download, std::function<double()> now = {});
    ~DownloadWorker();

    DownloadWorker(const DownloadWorker&) = delete;
    DownloadWorker& operator=(const DownloadWorker&) = delete;

    void Start(std::vector<DownloadTask> tasks); ///< spawns the worker thread
    void Cancel();                               ///< requests an early stop
    void Wait();                                 ///< joins the worker thread

    bool Running() const;

    // Thread-safe snapshot of the derived progress for the UI.
    DownloadSnapshot Poll() const;

  private:
    // Everything the worker thread touches, held by shared_ptr so that on
    // teardown a still-running thread can be DETACHED rather than joined: the
    // detached thread keeps its own copy of the Session alive until it returns,
    // so the UI thread never blocks waiting for a download stuck in a blocking
    // transport (which froze the game when the user cancelled mid-download).
    struct Session
    {
        DownloadFileFn download;
        std::function<double()> now;
        std::vector<DownloadTask> tasks;
        mutable std::mutex mtx;
        DownloadProgress progress;
        std::atomic<bool> cancel{false};
        std::atomic<bool> active{false};
        std::atomic<bool> postProcessing{false};
    };

    DownloadFileFn _download;
    std::function<double()> _now;
    std::shared_ptr<Session> _session; ///< current run; null until Start()
    std::thread _thread;
};

} // namespace Poseidon
