#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <Poseidon/Core/DownloadProgress.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using Poseidon::DownloadProgress;

TEST_CASE("DownloadProgress tracks current + overall by bytes", "[download][progress]")
{
    DownloadProgress p(3.0);
    p.SetItems({{"@a", 100}, {"@b", 100}});
    REQUIRE(p.ItemCount() == 2);
    REQUIRE(p.OverallTotal() == 200);

    p.BeginItem(0, 0.0);
    p.OnBytes(50, 1.0);
    CHECK(p.CurrentIndex() == 0);
    CHECK(p.CurrentLabel() == "@a");
    CHECK_THAT(p.CurrentFraction(), WithinAbs(0.5f, 1e-4f));
    CHECK_THAT(p.OverallFraction(), WithinAbs(0.25f, 1e-4f));
    CHECK(p.OverallReceived() == 50);

    // 50 bytes over 1 second.
    CHECK_THAT(p.SpeedBytesPerSec(), WithinAbs(50.0, 1e-6));
    CHECK_THAT(p.EtaSeconds(), WithinAbs(3.0, 1e-6)); // (200-50)/50

    p.CompleteItem(2.0);
    CHECK(p.OverallReceived() == 100);
    CHECK_THAT(p.OverallFraction(), WithinAbs(0.5f, 1e-4f));

    p.BeginItem(1, 2.0);
    CHECK(p.CurrentLabel() == "@b");
    CHECK_THAT(p.CurrentFraction(), WithinAbs(0.0f, 1e-4f));
    CHECK(p.OverallReceived() == 100); // item 0 done, item 1 not started

    p.OnBytes(50, 3.0);
    CHECK(p.OverallReceived() == 150);
    CHECK_THAT(p.OverallFraction(), WithinAbs(0.75f, 1e-4f));
    CHECK_THAT(p.SpeedBytesPerSec(), WithinAbs(50.0, 1e-6)); // 150 bytes over 3 s
    CHECK_THAT(p.EtaSeconds(), WithinAbs(1.0, 1e-6));        // (200-150)/50
}

TEST_CASE("DownloadProgress: a single item behaves like MP mission download", "[download][progress]")
{
    DownloadProgress p;
    p.SetItems({{"mission.pbo", 1000}});
    p.BeginItem(0, 0.0);
    p.OnBytes(500, 1.0);
    // current == overall when there's one file.
    CHECK_THAT(p.CurrentFraction(), WithinAbs(0.5f, 1e-4f));
    CHECK_THAT(p.OverallFraction(), WithinAbs(0.5f, 1e-4f));
    CHECK_THAT(p.SpeedBytesPerSec(), WithinAbs(500.0, 1e-6));
    CHECK_THAT(p.EtaSeconds(), WithinAbs(1.0, 1e-6));
    p.CompleteItem(2.0);
    p.Finish();
    CHECK(p.IsDone());
    CHECK_THAT(p.OverallFraction(), WithinAbs(1.0f, 1e-4f));
}

TEST_CASE("DownloadProgress: unknown total yields no fraction/ETA, never crashes", "[download][progress]")
{
    DownloadProgress p;
    p.SetItems({{"@x", 0}});
    p.BeginItem(0, 0.0);
    p.OnBytes(4096, 1.0);
    CHECK_THAT(p.CurrentFraction(), WithinAbs(0.0f, 1e-4f)); // total unknown
    CHECK_THAT(p.OverallFraction(), WithinAbs(0.0f, 1e-4f));
    CHECK(p.EtaSeconds() < 0.0); // unknown

    // Empty job is a no-op, not a crash.
    DownloadProgress empty;
    empty.SetItems({});
    CHECK(empty.ItemCount() == 0);
    CHECK_THAT(empty.OverallFraction(), WithinAbs(0.0f, 1e-4f));
    CHECK(empty.CurrentLabel().empty());
}

TEST_CASE("DownloadProgress: speed averages over the window, not all history", "[download][progress]")
{
    DownloadProgress p(3.0); // 3-second window
    p.SetItems({{"@a", 1000000}});
    p.BeginItem(0, 0.0);
    p.OnBytes(0, 0.0);
    // Slow for 4 s (older than the window), then a fast burst.
    p.OnBytes(100, 4.0);
    p.OnBytes(1100, 5.0); // +1000 B in the last 1 s
    // Oldest sample inside the 3-s window is t=4 (t=0 pruned); 1000 B over 1 s = 1000 B/s,
    // not the ~220 B/s an all-history average would give.
    CHECK(p.SpeedBytesPerSec() > 900.0);
    CHECK(p.SpeedBytesPerSec() < 1100.0);
}

TEST_CASE("DownloadProgress::FormatBytes is human readable", "[download][format]")
{
    CHECK(DownloadProgress::FormatBytes(0) == "0 B");
    CHECK(DownloadProgress::FormatBytes(900) == "900 B");
    CHECK(DownloadProgress::FormatBytes(1023) == "1023 B");
    CHECK(DownloadProgress::FormatBytes(1024) == "1 KB");
    CHECK(DownloadProgress::FormatBytes(524288) == "512 KB");
    CHECK(DownloadProgress::FormatBytes(939649) == "918 KB");
    CHECK(DownloadProgress::FormatBytes(1048576) == "1.0 MB");
    CHECK(DownloadProgress::FormatBytes(2202010) == "2.1 MB");
    CHECK(DownloadProgress::FormatBytes(1073741824) == "1.0 GB");
    CHECK(DownloadProgress::FormatBytes(1610612736) == "1.5 GB");
}

TEST_CASE("DownloadProgress::FormatSpeed / FormatEta", "[download][format]")
{
    CHECK(DownloadProgress::FormatSpeed(2202010.0) == "2.1 MB/s");
    CHECK(DownloadProgress::FormatSpeed(0.0) == "--");

    CHECK(DownloadProgress::FormatEta(-1.0) == "--");
    CHECK(DownloadProgress::FormatEta(0.0) == "0:00");
    CHECK(DownloadProgress::FormatEta(42.0) == "0:42");
    CHECK(DownloadProgress::FormatEta(83.0) == "1:23");
    CHECK(DownloadProgress::FormatEta(3723.0) == "1:02:03");
}
