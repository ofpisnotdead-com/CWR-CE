#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <Poseidon/Core/DownloadDialogView.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <string>

using Catch::Matchers::WithinAbs;
using Poseidon::BuildDownloadDialogView;
using Poseidon::DownloadDialogLabels;
using Poseidon::DownloadDialogView;
using Poseidon::DownloadFailureKind;
using Poseidon::DownloadSnapshot;
using Poseidon::FormatAnimatedActivity;

TEST_CASE("DownloadDialogView: a running multi-addon job shows both bars, speed and ETA", "[download][view]")
{
    DownloadSnapshot s;
    s.itemCount = 5;
    s.currentIndex = 1; // second addon in flight
    s.currentLabel = "@ww4";
    s.currentFraction = 0.5f;
    s.overallFraction = 0.4f;
    s.speedBytesPerSec = 2202010.0; // ~2.1 MB/s
    s.etaSeconds = 42.0;

    DownloadDialogView v = BuildDownloadDialogView(s);
    CHECK_THAT(v.currentFraction, WithinAbs(0.5f, 1e-4f));
    CHECK_THAT(v.overallFraction, WithinAbs(0.4f, 1e-4f));
    CHECK(v.currentLine == "@ww4   50%");
    CHECK(v.overallLine == "2 / 5   40%");
    CHECK(v.statusLine == "2.1 MB/s   ETA 0:42");
}

TEST_CASE("DownloadDialogView: a single file omits the N/N count (MP mission shape)", "[download][view]")
{
    DownloadSnapshot s;
    s.itemCount = 1;
    s.currentIndex = 0;
    s.currentLabel = "mission.pbo";
    s.currentFraction = 0.5f;
    s.overallFraction = 0.5f;
    s.speedBytesPerSec = 0.0; // no rate yet

    DownloadDialogView v = BuildDownloadDialogView(s);
    CHECK(v.currentLine == "mission.pbo   50%");
    CHECK(v.overallLine == "50%"); // no "1 / 1 missions"
    CHECK(v.statusLine == "Starting...");
}

TEST_CASE("DownloadDialogView: completion shows 100% and Complete", "[download][view]")
{
    DownloadSnapshot s;
    s.itemCount = 3;
    s.currentIndex = 2;
    s.currentFraction = 1.0f;
    s.overallFraction = 1.0f;
    s.done = true;

    DownloadDialogView v = BuildDownloadDialogView(s);
    CHECK(v.overallLine == "3 / 3   100%");
    CHECK(v.statusLine == "Complete");
}

TEST_CASE("DownloadDialogView: a failure does not expose the technical error", "[download][view]")
{
    DownloadSnapshot s;
    s.itemCount = 2;
    s.currentIndex = 0;
    s.currentLabel = "@a";
    s.failed = true;
    s.error = "connection reset";

    DownloadDialogView v = BuildDownloadDialogView(s);
    CHECK(v.statusLine == "Download failed");
}

TEST_CASE("DownloadDialogView: cancellation is reported", "[download][view]")
{
    DownloadSnapshot s;
    s.itemCount = 2;
    s.currentIndex = 0;
    s.cancelled = true;

    DownloadDialogView v = BuildDownloadDialogView(s);
    CHECK(v.statusLine == "Cancelled");
}

TEST_CASE("DownloadDialogView: terminal status labels come from the caller (localization)", "[download][view]")
{
    // The UI passes localized text; the builder must not hardcode English. Czech here.
    DownloadDialogLabels labels;
    labels.complete = "Dokon\xC4\x8D"
                      "eno"; // "Dokončeno"
    labels.cancelled = "Zru\xC5\xA1"
                       "eno";                              // "Zrušeno"
    labels.starting = "Spou\xC5\xA1t\xC4\x9Bn\xC3\xAD..."; // "Spouštění..."
    labels.failed = "Stahov\xC3\xA1n\xC3\xAD selhalo";     // "Stahování selhalo"
    labels.installFailed = "Instalace selhala";
    labels.eta = "zb\xC3\xBDv\xC3\xA1";

    DownloadSnapshot done;
    done.itemCount = 1;
    done.done = true;
    CHECK(BuildDownloadDialogView(done, labels).statusLine == "Dokon\xC4\x8D"
                                                              "eno");

    DownloadSnapshot cancelled;
    cancelled.itemCount = 1;
    cancelled.cancelled = true;
    CHECK(BuildDownloadDialogView(cancelled, labels).statusLine == "Zru\xC5\xA1"
                                                                   "eno");

    DownloadSnapshot starting;
    starting.itemCount = 1;
    starting.speedBytesPerSec = 0.0;
    CHECK(BuildDownloadDialogView(starting, labels).statusLine == "Spou\xC5\xA1t\xC4\x9Bn\xC3\xAD...");

    DownloadSnapshot failed;
    failed.itemCount = 1;
    failed.failed = true;
    failed.error = "reset";
    CHECK(BuildDownloadDialogView(failed, labels).statusLine == "Stahov\xC3\xA1n\xC3\xAD selhalo");

    failed.failureKind = DownloadFailureKind::Install;
    CHECK(BuildDownloadDialogView(failed, labels).statusLine == "Instalace selhala");

    DownloadSnapshot running;
    running.itemCount = 2;
    running.currentIndex = 0;
    running.speedBytesPerSec = 1024.0;
    running.etaSeconds = 42.0;
    const DownloadDialogView runningView = BuildDownloadDialogView(running, labels);
    CHECK(runningView.overallLine == "1 / 2   0%");
    CHECK(runningView.statusLine == "1 KB/s   zb\xC3\xBDv\xC3\xA1 0:42");
}

TEST_CASE("DownloadDialogView: an empty/idle snapshot yields blank lines", "[download][view]")
{
    DownloadSnapshot s; // default: itemCount 0
    DownloadDialogView v = BuildDownloadDialogView(s);
    CHECK(v.currentLine.empty());
    CHECK(v.overallLine.empty());
    CHECK(v.statusLine.empty());
}

TEST_CASE("Download dialog activity cycles one to three dots", "[download][view]")
{
    CHECK(FormatAnimatedActivity("Unpacking", 0) == "Unpacking.");
    CHECK(FormatAnimatedActivity("Unpacking", 400) == "Unpacking..");
    CHECK(FormatAnimatedActivity("Unpacking", 800) == "Unpacking...");
    CHECK(FormatAnimatedActivity("Unpacking", 1200) == "Unpacking.");
}
