#pragma once

// Presentation for the download dialog, derived from a DownloadSnapshot.
// Pure (no UI types): the two bar fractions and the three text lines the dialog
// shows. Unit-testable and reused by the multiplayer mission-download dialog.
//
//   currentLine: the file in flight        e.g. "@ww4   50%"
//   overallLine: progress across the job    e.g. "2 / 5   40%"
//   statusLine:  speed + ETA, or terminal   e.g. "1.2 MB/s   ETA 0:42" / "Complete"

#include <Poseidon/Core/DownloadWorker.hpp>

#include <cstdint>
#include <string>

namespace Poseidon
{
struct DownloadDialogView
{
    float currentFraction = 0.0f; ///< top bar fill (0..1)
    float overallFraction = 0.0f; ///< bottom bar fill (0..1)
    std::string currentLine;
    std::string overallLine;
    std::string statusLine;
};

// Terminal status-line labels. Kept out of the pure builder so it stays UI-free and
// unit-testable; the caller passes localized text (defaults are the English source
// strings).
struct DownloadDialogLabels
{
    const char* complete = "Complete";
    const char* cancelled = "Cancelled";
    const char* starting = "Starting...";
    const char* failed = "Download failed";
    const char* installFailed = "Installation failed";
    const char* eta = "ETA";
};

DownloadDialogView BuildDownloadDialogView(const DownloadSnapshot& snapshot, const DownloadDialogLabels& labels = {});

std::string FormatAnimatedActivity(const char* label, uint64_t elapsedMs);

} // namespace Poseidon
