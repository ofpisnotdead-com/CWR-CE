#include <Poseidon/Core/CloudSync/CloudSync.hpp>

#include <algorithm>
#include <unordered_map>

namespace Poseidon
{
namespace
{
std::unordered_map<std::string, double> ToMap(std::vector<std::pair<std::string, double>> entries)
{
    std::unordered_map<std::string, double> map;
    map.reserve(entries.size());
    for (auto& [relPath, mtime] : entries)
        map.emplace(std::move(relPath), mtime);
    return map;
}
} // namespace

// Pure orchestration -- identical on every platform, since it only ever calls
// through the injected SyncOpsEnv. Compiled unconditionally (see
// CloudSync.mm for why only IsAvailable()/ContainerPath()/
// MakeAppleSyncOpsEnv() below are platform-gated instead of splitting this
// logic into a third file).
void RunSyncJobs(const std::vector<SyncPair>& pairs, SyncDirection direction, SyncProgress& progress, std::mutex& mtx,
                 const SyncOpsEnv& env, const std::atomic<bool>& cancel)
{
    // Build the flat list of (pair index, relPath, pull?, push?) up front so
    // progress.itemCount is known before any copying starts.
    struct PlannedOp
    {
        int pairIndex;
        std::string relPath;
        bool pull;
        bool push;
    };
    std::vector<PlannedOp> ops;

    for (int i = 0; i < static_cast<int>(pairs.size()); ++i)
    {
        if (cancel.load())
            return;

        const SyncPair& pair = pairs[i];
        std::unordered_map<std::string, double> local = ToMap(env.listLocal(pair.localDir));
        std::unordered_map<std::string, double> remote = ToMap(env.listContainer(pair.containerRelPath));

        std::vector<std::string> relPaths;
        for (const auto& [relPath, mtime] : local)
            relPaths.push_back(relPath);
        for (const auto& [relPath, mtime] : remote)
            if (!local.count(relPath))
                relPaths.push_back(relPath);
        std::sort(relPaths.begin(), relPaths.end());

        for (const std::string& relPath : relPaths)
        {
            const auto localIt = local.find(relPath);
            const auto remoteIt = remote.find(relPath);
            const bool hasLocal = localIt != local.end();
            const bool hasRemote = remoteIt != remote.end();

            bool pull = false;
            bool push = false;
            if (direction == SyncDirection::Pull)
                pull = hasRemote && (!hasLocal || remoteIt->second > localIt->second);
            else if (direction == SyncDirection::Push)
                push = hasLocal && (!hasRemote || localIt->second > remoteIt->second);
            else // Both: newest side wins, missing side always copies
            {
                if (!hasLocal && hasRemote)
                    pull = true;
                else if (hasLocal && !hasRemote)
                    push = true;
                else if (hasLocal && hasRemote)
                {
                    if (localIt->second > remoteIt->second)
                        push = true;
                    else if (remoteIt->second > localIt->second)
                        pull = true;
                    // equal mtimes: already in sync, nothing to do
                }
            }

            if (pull || push)
                ops.push_back({i, relPath, pull, push});
        }
    }

    {
        std::lock_guard<std::mutex> g(mtx);
        progress.itemCount = static_cast<int>(ops.size());
    }

    for (int i = 0; i < static_cast<int>(ops.size()); ++i)
    {
        if (cancel.load())
            return; // cancelled — leave progress neither done nor failed

        const PlannedOp& op = ops[i];
        const SyncPair& pair = pairs[op.pairIndex];

        {
            std::lock_guard<std::mutex> g(mtx);
            progress.currentIndex = i;
            progress.currentLabel = op.relPath;
        }

        std::string error;
        const bool ok = op.pull ? env.pullFile(pair.localDir, pair.containerRelPath, op.relPath, error)
                                 : env.pushFile(pair.localDir, pair.containerRelPath, op.relPath, error);

        if (cancel.load())
            return; // aborted mid-file — treated as cancel, not failure

        if (!ok)
        {
            std::lock_guard<std::mutex> g(mtx);
            progress.failed = true;
            progress.error = error.empty() ? "sync failed" : error;
            return;
        }
    }

    std::lock_guard<std::mutex> g(mtx);
    progress.done = true;
}

SyncWorker::SyncWorker(SyncOpsEnv env) : _env(std::move(env)) {}

SyncWorker::~SyncWorker()
{
    // Request a stop and DETACH — never join. The thread holds its own
    // shared_ptr to the Session, so it runs out safely after we're gone
    // instead of blocking teardown/quit on in-flight cloud I/O.
    if (_session)
        _session->cancel.store(true);
    if (_thread.joinable())
        _thread.detach();
}

void SyncWorker::Start(std::vector<SyncPair> pairs, SyncDirection direction)
{
    // A run already in flight: don't cancel it. RunSyncJobs snapshots the
    // file list up front, so anything written after that snapshot -- which
    // is exactly what a Start() call arriving mid-run represents -- would
    // never be seen by the in-flight copy. Flag it to loop once more instead
    // once it finishes, so those newer changes still get synced. (Previously
    // this cancelled + detached the in-flight run, which meant a burst of
    // saves in quick succession -- e.g. NextMission() then AddMission() --
    // could cancel every push before any of them completed.)
    //
    // Deliberately keeps running with the ORIGINAL pairs/direction rather
    // than swapping in the new call's arguments: session->pairs/direction
    // are read by the worker thread without a lock, and every actual caller
    // in this codebase (PushInBackground's static worker) always passes the
    // same DefaultSyncPairs()+Push, so there's nothing to reconcile.
    if (_session && _session->active.load())
    {
        _session->rerunRequested.store(true);
        return;
    }

    if (_thread.joinable())
        _thread.detach();

    auto session = std::make_shared<Session>();
    session->env = _env;
    session->pairs = std::move(pairs);
    session->direction = direction;
    session->active.store(true);
    _session = session;

    _thread = std::thread(
        [session]()
        {
            do
            {
                session->rerunRequested.store(false);
                {
                    std::lock_guard<std::mutex> g(session->mtx);
                    session->progress = SyncProgress{};
                }
                RunSyncJobs(session->pairs, session->direction, session->progress, session->mtx, session->env,
                           session->cancel);
            } while (session->rerunRequested.load() && !session->cancel.load());
            session->active.store(false);
        });
}

void SyncWorker::Cancel()
{
    if (_session)
        _session->cancel.store(true);
}

void SyncWorker::Wait()
{
    if (_thread.joinable())
        _thread.join();
}

bool SyncWorker::Running() const
{
    return _session && _session->active.load();
}

SyncSnapshot SyncWorker::Poll() const
{
    SyncSnapshot s;
    if (!_session)
        return s;
    std::lock_guard<std::mutex> g(_session->mtx);
    s = _session->progress;
    if (_session->cancel.load() && !s.done && !s.failed)
        s.cancelled = true;
    return s;
}

#if !defined(__APPLE__)
// Non-Apple fallback: no iCloud on Windows/Linux. The Apple implementation
// of this trio lives in CloudSync.mm, additively compiled only for Apple
// targets -- see engine/Poseidon/CMakeLists.txt.
namespace CloudSync
{
bool IsAvailable()
{
    return false;
}

std::string ContainerPath()
{
    return {};
}

SyncOpsEnv MakeAppleSyncOpsEnv()
{
    return {};
}
} // namespace CloudSync
#endif

} // namespace Poseidon
