#pragma once

// Mirrors profile/save/mission directories into and out of an iCloud ubiquity
// container so the same profile data is available across a player's Apple
// devices. Local disk stays authoritative; the container is sync transport,
// not the working directory -- gameplay never blocks on cloud I/O beyond the
// explicit Pull()/Push() calls made at launch/quit.
//
// Same shape as DownloadWorker.hpp: a pure, synchronous orchestration
// function (RunSyncJobs) with every effect injected via SyncOpsEnv, so the
// file-diffing logic unit-tests without a thread, a real container, or real
// I/O. SyncWorker is the thin thread + mutex wrapper the caller actually
// polls.
//
// CloudSync.cpp (non-Apple) stubs IsAvailable()/ContainerPath()/
// MakeAppleSyncOpsEnv() out entirely -- callers should check IsAvailable()
// before starting a SyncWorker. CloudSync.mm (Apple) is the real
// NSFileManager/NSMetadataQuery/NSFileCoordinator-backed implementation, used
// identically on macOS and iOS since the ubiquity-container APIs are plain
// Foundation, not UIKit.

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Poseidon
{

// One directory to mirror: a local disk path <-> a path relative to the
// ubiquity container root. Both sides are walked recursively.
struct SyncPair
{
    std::string localDir;         ///< e.g. GamePaths::UserDir() + "Users/"
    std::string containerRelPath; ///< e.g. "Users" (relative to container root)
};

// Injected effects RunSyncJobs needs -- real implementations come from
// MakeAppleSyncOpsEnv() (CloudSync.mm); tests supply fakes directly.
struct SyncOpsEnv
{
    // Recursively lists files under a directory as (relative path, mtime
    // seconds since epoch) pairs. `listLocal` walks a real local dir;
    // `listContainer` walks the container-relative subpath (may trigger
    // iCloud metadata lookups in the real implementation).
    std::function<std::vector<std::pair<std::string, double>>(const std::string& dir)> listLocal;
    std::function<std::vector<std::pair<std::string, double>>(const std::string& containerRelDir)> listContainer;

    // Copies one file. `relPath` is relative to the pair's roots on both
    // sides. Return false (and set `error`) on failure.
    std::function<bool(const std::string& localDir, const std::string& containerRelPath, const std::string& relPath,
                       std::string& error)>
        pullFile; ///< container -> local
    std::function<bool(const std::string& localDir, const std::string& containerRelPath, const std::string& relPath,
                       std::string& error)>
        pushFile; ///< local -> container
};

struct SyncProgress
{
    int itemCount = 0;
    int currentIndex = -1;
    std::string currentLabel;
    bool done = false;
    bool failed = false;
    bool cancelled = false;
    std::string error;
};

enum class SyncDirection
{
    Pull, ///< container -> local only
    Push, ///< local -> container only
    Both  ///< per-file, newest side wins
};

// Pure orchestration: for each pair, lists both sides, diffs by mtime per
// `direction`, and copies through the injected pull/push functions, updating
// `progress` (guarded by `mtx`) as it goes. Stops early -- leaving progress
// neither done nor failed -- when `cancel` flips true between files. Sets
// progress failed on the first copy failure and skips the rest.
void RunSyncJobs(const std::vector<SyncPair>& pairs, SyncDirection direction, SyncProgress& progress, std::mutex& mtx,
                 const SyncOpsEnv& env, const std::atomic<bool>& cancel);

using SyncSnapshot = SyncProgress;

class SyncWorker
{
  public:
    explicit SyncWorker(SyncOpsEnv env);
    ~SyncWorker();

    SyncWorker(const SyncWorker&) = delete;
    SyncWorker& operator=(const SyncWorker&) = delete;

    // Spawns the worker thread. If a run is already in flight (same worker
    // instance), does NOT cancel it -- RunSyncJobs snapshots its file list
    // up front, so anything written after that snapshot but before the copy
    // finishes would otherwise never get synced by that run. Instead, flags
    // the in-flight run to loop once more on completion so newer changes
    // still get picked up. See PushInBackground(), whose static SyncWorker
    // can be re-triggered several times in quick succession by back-to-back
    // saves.
    void Start(std::vector<SyncPair> pairs, SyncDirection direction);
    void Cancel();                                                    ///< requests an early stop
    void Wait();                                                      ///< joins the worker thread

    bool Running() const;

    // Thread-safe snapshot of the derived progress for the caller to poll.
    SyncSnapshot Poll() const;

  private:
    // Detached-on-teardown, matching DownloadWorker::Session: a still-running
    // thread keeps its own copy of the session alive rather than the caller
    // blocking a shutdown/quit path on in-flight cloud I/O.
    struct Session
    {
        SyncOpsEnv env;
        std::vector<SyncPair> pairs;
        SyncDirection direction;
        mutable std::mutex mtx;
        SyncProgress progress;
        std::atomic<bool> cancel{false};
        std::atomic<bool> active{false};
        std::atomic<bool> rerunRequested{false}; ///< set by Start() when called while already active
    };

    SyncOpsEnv _env;
    std::shared_ptr<Session> _session; ///< current run; null until Start()
    std::thread _thread;
};

namespace CloudSync
{

// True when signed into iCloud with this app's ubiquity container reachable.
bool IsAvailable();

// Local filesystem path to the ubiquity container root, or empty if
// unavailable. Callers build SyncPair::containerRelPath relative to this.
std::string ContainerPath();

// Real Apple-backed SyncOpsEnv (NSFileManager/NSFileCoordinator). Only
// meaningful when IsAvailable() is true.
SyncOpsEnv MakeAppleSyncOpsEnv();

} // namespace CloudSync

} // namespace Poseidon
