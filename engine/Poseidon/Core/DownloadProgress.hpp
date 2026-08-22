#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace Poseidon
{
enum class DownloadFailureKind
{
    None,
    Download,
    Install
};

/// Agnostic progress model for a sequential multi-file download — a set of mod PBOs,
/// or a single MP mission file (one item). Pure: no I/O, no threading, no clock. The
/// download worker feeds it (BeginItem / OnBytes / CompleteItem) passing a monotonic
/// time in seconds; the UI reads the derived current/overall fractions, speed and
/// ETA. One Item == one downloaded file; "current" tracks the active file, "overall"
/// the whole job by bytes.
class DownloadProgress
{
  public:
    struct Item
    {
        std::string label;      ///< shown for the current item (e.g. "@csla")
        int64_t totalBytes = 0; ///< expected size; 0 = unknown
    };

    /// speedWindowSeconds = how many seconds of byte-rate history are averaged for
    /// the speed (and hence ETA) readout.
    explicit DownloadProgress(double speedWindowSeconds = 3.0);

    void SetItems(std::vector<Item> items);
    void Reset();

    // --- worker feed (now = monotonically increasing seconds) ---
    void BeginItem(int index, double now);
    void OnBytes(int64_t receivedForCurrentItem, double now);
    void CompleteItem(double now);
    void Finish(); ///< the whole job succeeded
    void SetFailed(std::string error, DownloadFailureKind kind = DownloadFailureKind::Download);

    // --- derived state (the UI reads these) ---
    int ItemCount() const { return static_cast<int>(_items.size()); }
    int CurrentIndex() const { return _current; }
    std::string CurrentLabel() const;
    int64_t CurrentReceived() const { return _currentReceived; }
    int64_t CurrentTotal() const;
    float CurrentFraction() const; ///< 0..1 for the current item (0 if its total is unknown)
    int64_t OverallReceived() const { return _overallReceived; }
    int64_t OverallTotal() const { return _overallTotal; }
    float OverallFraction() const;   ///< 0..1 across all items, by bytes
    double SpeedBytesPerSec() const; ///< averaged over the window; 0 if too few samples
    double EtaSeconds() const;       ///< remaining / speed; < 0 if unknown
    bool IsDone() const { return _done; }
    bool IsFailed() const { return _failed; }
    DownloadFailureKind FailureKind() const { return _failureKind; }
    const std::string& Error() const { return _error; }

    // --- formatting (pure, static) ---
    static std::string FormatBytes(int64_t bytes);      ///< "900 B", "512 KB", "2.1 MB", "1.4 GB"
    static std::string FormatSpeed(double bytesPerSec); ///< "2.1 MB/s"
    static std::string FormatEta(double seconds);       ///< "0:42", "1:23:45", "--"

  private:
    void RecordSample(double now);

    std::vector<Item> _items;
    int _current = -1;
    int64_t _currentReceived = 0;
    int64_t _completedBytes = 0; ///< sum of completed items' totals
    int64_t _overallReceived = 0;
    int64_t _overallTotal = 0;
    bool _done = false;
    bool _failed = false;
    DownloadFailureKind _failureKind = DownloadFailureKind::None;
    std::string _error;

    double _speedWindow = 3.0;
    struct Sample
    {
        double time;
        int64_t bytes;
    };
    std::deque<Sample> _samples; ///< (time, overallReceived) within the window
};
} // namespace Poseidon
