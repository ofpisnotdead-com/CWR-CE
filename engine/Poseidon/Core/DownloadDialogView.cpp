#include <Poseidon/Core/DownloadDialogView.hpp>

#include <Poseidon/Core/DownloadProgress.hpp>

#include <algorithm>
#include <cstdio>

namespace Poseidon
{
namespace
{
std::string PercentText(float fraction01)
{
    const float f = std::clamp(fraction01, 0.0f, 1.0f);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(f * 100.0f + 0.5f));
    return buf;
}

std::string Pluralize(const char* noun, int count)
{
    std::string s = noun ? noun : "item";
    if (count != 1)
        s += "s";
    return s;
}
} // namespace

DownloadDialogView BuildDownloadDialogView(const DownloadSnapshot& snapshot, const char* unitNoun,
                                           const DownloadDialogLabels& labels)
{
    DownloadDialogView view;
    view.currentFraction = snapshot.currentFraction;
    view.overallFraction = snapshot.overallFraction;

    if (!snapshot.currentLabel.empty())
        view.currentLine = snapshot.currentLabel + "   " + PercentText(snapshot.currentFraction);

    if (snapshot.itemCount == 1)
    {
        // A single file (the MP mission case) — no "1 / 1", just the percent.
        view.overallLine = PercentText(snapshot.overallFraction);
    }
    else if (snapshot.itemCount > 1)
    {
        // Completed items: when done, all N; otherwise the in-flight item's 1-based index.
        int doneCount =
            snapshot.done ? snapshot.itemCount : std::clamp(snapshot.currentIndex + 1, 0, snapshot.itemCount);
        char buf[64];
        snprintf(buf, sizeof(buf), "%d / %d %s   %s", doneCount, snapshot.itemCount,
                 Pluralize(unitNoun, snapshot.itemCount).c_str(), PercentText(snapshot.overallFraction).c_str());
        view.overallLine = buf;
    }

    if (snapshot.failed)
    {
        view.statusLine = snapshot.error.empty() ? labels.failed : (std::string(labels.failed) + ": " + snapshot.error);
    }
    else if (snapshot.done)
    {
        view.statusLine = labels.complete;
    }
    else if (snapshot.cancelled)
    {
        view.statusLine = labels.cancelled;
    }
    else if (snapshot.itemCount > 0)
    {
        if (snapshot.speedBytesPerSec > 0.0)
            view.statusLine = DownloadProgress::FormatSpeed(snapshot.speedBytesPerSec) + "   ETA " +
                              DownloadProgress::FormatEta(snapshot.etaSeconds);
        else
            view.statusLine = labels.starting;
    }

    return view;
}

} // namespace Poseidon
