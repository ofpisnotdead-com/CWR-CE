#include <Poseidon/Core/DownloadProgress.hpp>

#include <algorithm>
#include <cstdio>
#include <utility>

namespace Poseidon
{
DownloadProgress::DownloadProgress(double speedWindowSeconds)
    : _speedWindow(speedWindowSeconds > 0.0 ? speedWindowSeconds : 3.0)
{
}

void DownloadProgress::SetItems(std::vector<Item> items)
{
    Reset();
    _items = std::move(items);
    _overallTotal = 0;
    for (const Item& it : _items)
        _overallTotal += (it.totalBytes > 0 ? it.totalBytes : 0);
}

void DownloadProgress::Reset()
{
    _current = -1;
    _currentReceived = 0;
    _completedBytes = 0;
    _overallReceived = 0;
    _done = false;
    _failed = false;
    _failureKind = DownloadFailureKind::None;
    _error.clear();
    _samples.clear();
}

void DownloadProgress::RecordSample(double now)
{
    _samples.push_back({now, _overallReceived});
    // Drop samples older than the window, but always keep at least two so a span exists.
    while (_samples.size() > 2 && (now - _samples.front().time) > _speedWindow)
        _samples.pop_front();
}

void DownloadProgress::BeginItem(int index, double now)
{
    _current = index;
    _currentReceived = 0;
    _overallReceived = _completedBytes;
    RecordSample(now);
}

void DownloadProgress::OnBytes(int64_t receivedForCurrentItem, double now)
{
    if (receivedForCurrentItem < 0)
        receivedForCurrentItem = 0;
    _currentReceived = receivedForCurrentItem;
    _overallReceived = _completedBytes + _currentReceived;
    RecordSample(now);
}

void DownloadProgress::CompleteItem(double now)
{
    if (_current >= 0 && _current < static_cast<int>(_items.size()))
        _completedBytes += (_items[_current].totalBytes > 0 ? _items[_current].totalBytes : _currentReceived);
    _currentReceived = (_current >= 0 && _current < static_cast<int>(_items.size())) ? _items[_current].totalBytes : 0;
    _overallReceived = _completedBytes;
    RecordSample(now);
}

void DownloadProgress::Finish()
{
    _done = true;
}

void DownloadProgress::SetFailed(std::string error, DownloadFailureKind kind)
{
    _failed = true;
    _failureKind = kind;
    _error = std::move(error);
}

std::string DownloadProgress::CurrentLabel() const
{
    if (_current >= 0 && _current < static_cast<int>(_items.size()))
        return _items[_current].label;
    return {};
}

int64_t DownloadProgress::CurrentTotal() const
{
    if (_current >= 0 && _current < static_cast<int>(_items.size()))
        return _items[_current].totalBytes;
    return 0;
}

float DownloadProgress::CurrentFraction() const
{
    const int64_t total = CurrentTotal();
    if (total <= 0)
        return 0.0f;
    float f = static_cast<float>(_currentReceived) / static_cast<float>(total);
    return std::clamp(f, 0.0f, 1.0f);
}

float DownloadProgress::OverallFraction() const
{
    if (_overallTotal <= 0)
        return 0.0f;
    float f = static_cast<float>(_overallReceived) / static_cast<float>(_overallTotal);
    return std::clamp(f, 0.0f, 1.0f);
}

double DownloadProgress::SpeedBytesPerSec() const
{
    if (_samples.size() < 2)
        return 0.0;
    const Sample& oldest = _samples.front();
    const Sample& newest = _samples.back();
    const double span = newest.time - oldest.time;
    if (span <= 0.0)
        return 0.0;
    const double delta = static_cast<double>(newest.bytes - oldest.bytes);
    return delta > 0.0 ? delta / span : 0.0;
}

double DownloadProgress::EtaSeconds() const
{
    const double speed = SpeedBytesPerSec();
    if (speed <= 0.0 || _overallTotal <= 0)
        return -1.0;
    const int64_t remaining = _overallTotal - _overallReceived;
    if (remaining <= 0)
        return 0.0;
    return static_cast<double>(remaining) / speed;
}

std::string DownloadProgress::FormatBytes(int64_t bytes)
{
    if (bytes < 0)
        bytes = 0;
    char buf[32];
    const double kb = 1024.0;
    const double mb = 1024.0 * 1024.0;
    const double gb = 1024.0 * 1024.0 * 1024.0;
    if (bytes < 1024)
        snprintf(buf, sizeof(buf), "%lld B", static_cast<long long>(bytes));
    else if (bytes < 1024LL * 1024)
        snprintf(buf, sizeof(buf), "%.0f KB", bytes / kb);
    else if (bytes < 1024LL * 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.1f MB", bytes / mb);
    else
        snprintf(buf, sizeof(buf), "%.1f GB", bytes / gb);
    return buf;
}

std::string DownloadProgress::FormatSpeed(double bytesPerSec)
{
    if (bytesPerSec <= 0.0)
        return "--";
    return FormatBytes(static_cast<int64_t>(bytesPerSec)) + "/s";
}

std::string DownloadProgress::FormatEta(double seconds)
{
    if (seconds < 0.0)
        return "--";
    long long s = static_cast<long long>(seconds + 0.5);
    const long long h = s / 3600;
    s -= h * 3600;
    const long long m = s / 60;
    s -= m * 60;
    char buf[32];
    if (h > 0)
        snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld", h, m, s);
    else
        snprintf(buf, sizeof(buf), "%lld:%02lld", m, s);
    return buf;
}
} // namespace Poseidon
