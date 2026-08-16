#include <Poseidon/Core/DownloadWorker.hpp>

#include <chrono>
#include <utility>

namespace Poseidon
{
namespace
{
double DefaultMonotonicSeconds()
{
    using namespace std::chrono;
    return duration_cast<duration<double>>(steady_clock::now().time_since_epoch()).count();
}
} // namespace

void RunDownloadJobs(const std::vector<DownloadTask>& tasks, DownloadProgress& progress, std::mutex& mtx,
                     const DownloadFileFn& download, const std::function<double()>& now,
                     const std::atomic<bool>& cancel, std::atomic<bool>* postProcessing)
{
    {
        std::lock_guard<std::mutex> g(mtx);
        std::vector<DownloadProgress::Item> items;
        items.reserve(tasks.size());
        for (const DownloadTask& t : tasks)
            items.push_back({t.label, t.expectedBytes});
        progress.SetItems(std::move(items));
    }

    for (int i = 0; i < static_cast<int>(tasks.size()); ++i)
    {
        if (cancel.load())
            return; // cancelled — leave progress neither done nor failed

        {
            std::lock_guard<std::mutex> g(mtx);
            progress.BeginItem(i, now());
        }

        const DownloadTask& task = tasks[i];
        auto onBytes = [&](int64_t received, int64_t /*total*/)
        {
            std::lock_guard<std::mutex> g(mtx);
            progress.OnBytes(received, now());
        };
        auto cancelled = [&]() { return cancel.load(); };

        std::string error;
        const bool ok = download(task, onBytes, cancelled, error);

        if (cancel.load())
            return; // aborted mid-file — treated as cancel, not failure

        if (!ok)
        {
            std::lock_guard<std::mutex> g(mtx);
            progress.SetFailed(error.empty() ? "download failed" : error);
            return;
        }

        if (task.postStep)
        {
            if (postProcessing != nullptr)
                postProcessing->store(true);
            std::string postError;
            if (!task.postStep(task, postError))
            {
                if (postProcessing != nullptr)
                    postProcessing->store(false);
                std::lock_guard<std::mutex> g(mtx);
                progress.SetFailed(postError.empty() ? "install failed" : postError);
                return;
            }
            if (postProcessing != nullptr)
                postProcessing->store(false);
        }

        {
            std::lock_guard<std::mutex> g(mtx);
            progress.CompleteItem(now());
        }
    }

    std::lock_guard<std::mutex> g(mtx);
    progress.Finish();
}

DownloadWorker::DownloadWorker(DownloadFileFn download, std::function<double()> now)
    : _download(std::move(download)), _now(now ? std::move(now) : DefaultMonotonicSeconds)
{
}

DownloadWorker::~DownloadWorker()
{
    // Request a stop and DETACH — never join. Joining here blocked the UI thread
    // tearing down the dialog until the in-flight download finished, which froze
    // the game on cancel. The thread holds its own shared_ptr to the Session, so
    // it runs out safely after we're gone.
    if (_session)
        _session->cancel.store(true);
    if (_thread.joinable())
        _thread.detach();
}

void DownloadWorker::Start(std::vector<DownloadTask> tasks)
{
    // Abandon any prior run (cancel + detach, never block). A new run gets a
    // fresh Session so a retry doesn't observe the old one's state.
    if (_session)
        _session->cancel.store(true);
    if (_thread.joinable())
        _thread.detach();

    auto session = std::make_shared<Session>();
    session->download = _download;
    session->now = _now;
    session->tasks = std::move(tasks);
    session->active.store(true);
    _session = session;

    _thread = std::thread(
        [session]()
        {
            RunDownloadJobs(session->tasks, session->progress, session->mtx, session->download, session->now,
                            session->cancel, &session->postProcessing);
            session->active.store(false);
        });
}

void DownloadWorker::Cancel()
{
    if (_session)
        _session->cancel.store(true);
}

void DownloadWorker::Wait()
{
    if (_thread.joinable())
        _thread.join();
}

bool DownloadWorker::Running() const
{
    return _session && _session->active.load();
}

DownloadSnapshot DownloadWorker::Poll() const
{
    DownloadSnapshot s;
    if (!_session)
        return s;
    std::lock_guard<std::mutex> g(_session->mtx);
    const DownloadProgress& p = _session->progress;
    s.itemCount = p.ItemCount();
    s.currentIndex = p.CurrentIndex();
    s.currentLabel = p.CurrentLabel();
    s.currentFraction = p.CurrentFraction();
    s.overallFraction = p.OverallFraction();
    s.overallReceived = p.OverallReceived();
    s.overallTotal = p.OverallTotal();
    s.speedBytesPerSec = p.SpeedBytesPerSec();
    s.etaSeconds = p.EtaSeconds();
    s.done = p.IsDone();
    s.failed = p.IsFailed();
    s.cancelled = _session->cancel.load() && !p.IsDone() && !p.IsFailed();
    s.postProcessing = _session->postProcessing.load();
    s.error = p.Error();
    return s;
}

} // namespace Poseidon
