#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Core/GameDataArchive.hpp>

#include <zip.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace Poseidon;

namespace
{
std::filesystem::path MakeTempDir()
{
    std::random_device rd;
    auto dir = std::filesystem::temp_directory_path() /
               ("cwr_gamedataarchive_" + std::to_string(rd()) + "_" + std::to_string(rd()));
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

// Builds a real zip file at zipPath with the given (entryName, content) pairs
// via libzip's own write API -- entries ending in '/' are added as empty
// directory entries.
void MakeZip(const std::filesystem::path& zipPath, const std::vector<std::pair<std::string, std::string>>& entries)
{
    int err = 0;
    zip_t* za = zip_open(zipPath.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    REQUIRE(za != nullptr);

    for (const auto& [name, content] : entries)
    {
        if (!name.empty() && name.back() == '/')
        {
            REQUIRE(zip_dir_add(za, name.c_str(), ZIP_FL_ENC_UTF_8) >= 0);
            continue;
        }
        zip_source_t* src = zip_source_buffer(za, content.data(), content.size(), 0);
        REQUIRE(src != nullptr);
        REQUIRE(zip_file_add(za, name.c_str(), src, ZIP_FL_ENC_UTF_8) >= 0);
    }

    REQUIRE(zip_close(za) == 0);
}

std::string ReadFileText(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
} // namespace

TEST_CASE("GameDataArchive::Unpack extracts nested files with their content", "[gamedata][archive]")
{
    const auto root = MakeTempDir();
    const auto zipPath = root / "fixture.zip";
    const auto destDir = root / "unpacked";
    MakeZip(zipPath, {
                         {"BIN/CONFIG.BIN", "config-bytes"},
                         {"DTA/Fonts.pbo", "font-bytes"},
                         {"AddOns/O.pbo", "addon-bytes"},
                     });

    std::string error;
    REQUIRE(GameDataArchive::Unpack(zipPath.string().c_str(), destDir.string().c_str(), {}, &error));
    CHECK(error.empty());

    CHECK(ReadFileText(destDir / "BIN" / "CONFIG.BIN") == "config-bytes");
    CHECK(ReadFileText(destDir / "DTA" / "Fonts.pbo") == "font-bytes");
    CHECK(ReadFileText(destDir / "AddOns" / "O.pbo") == "addon-bytes");

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

// Real-world case: zipping a folder (Finder "Compress", `zip -r Combined/`)
// wraps every entry in that folder's name -- must be stripped so DTA/AddOns/
// BIN land at destDir's root, the same way a normal unarchiver would.
TEST_CASE("GameDataArchive::Unpack strips a single common top-level wrapping folder", "[gamedata][archive]")
{
    const auto root = MakeTempDir();
    const auto zipPath = root / "fixture.zip";
    const auto destDir = root / "unpacked";
    MakeZip(zipPath, {
                         {"Combined/", ""},
                         {"Combined/BIN/CONFIG.BIN", "config-bytes"},
                         {"Combined/DTA/Fonts.pbo", "font-bytes"},
                         {"Combined/AddOns/O.pbo", "addon-bytes"},
                     });

    std::string error;
    REQUIRE(GameDataArchive::Unpack(zipPath.string().c_str(), destDir.string().c_str(), {}, &error));
    CHECK(error.empty());

    CHECK(ReadFileText(destDir / "BIN" / "CONFIG.BIN") == "config-bytes");
    CHECK(ReadFileText(destDir / "DTA" / "Fonts.pbo") == "font-bytes");
    CHECK(ReadFileText(destDir / "AddOns" / "O.pbo") == "addon-bytes");
    CHECK_FALSE(std::filesystem::exists(destDir / "Combined"));

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("GameDataArchive::Unpack does not strip when entries already live at the archive root",
         "[gamedata][archive]")
{
    const auto root = MakeTempDir();
    const auto zipPath = root / "fixture.zip";
    const auto destDir = root / "unpacked";
    // Mixed: one entry at the root alongside a subdirectory -- no single
    // common top-level folder, so nothing should be stripped.
    MakeZip(zipPath, {{"BIN/CONFIG.BIN", "config-bytes"}, {"readme.txt", "hi"}});

    std::string error;
    REQUIRE(GameDataArchive::Unpack(zipPath.string().c_str(), destDir.string().c_str(), {}, &error));
    CHECK(ReadFileText(destDir / "BIN" / "CONFIG.BIN") == "config-bytes");
    CHECK(ReadFileText(destDir / "readme.txt") == "hi");

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

// Finder's "Compress" and plain `zip -r` (without -X) add AppleDouble
// resource-fork shadow files and .DS_Store -- never part of the real game
// data, must be silently skipped rather than written out.
TEST_CASE("GameDataArchive::Unpack skips __MACOSX and .DS_Store noise entries", "[gamedata][archive]")
{
    const auto root = MakeTempDir();
    const auto zipPath = root / "fixture.zip";
    const auto destDir = root / "unpacked";
    MakeZip(zipPath, {
                         {"Combined/", ""},
                         {"Combined/.DS_Store", "junk"},
                         {"Combined/BIN/CONFIG.BIN", "config-bytes"},
                         {"__MACOSX/Combined/._BIN", "resource-fork-junk"},
                     });

    std::string error;
    REQUIRE(GameDataArchive::Unpack(zipPath.string().c_str(), destDir.string().c_str(), {}, &error));
    CHECK(error.empty());

    CHECK(ReadFileText(destDir / "BIN" / "CONFIG.BIN") == "config-bytes");
    CHECK_FALSE(std::filesystem::exists(destDir / ".DS_Store"));
    CHECK_FALSE(std::filesystem::exists(destDir / "__MACOSX"));

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("GameDataArchive::Unpack materializes explicit directory entries", "[gamedata][archive]")
{
    const auto root = MakeTempDir();
    const auto zipPath = root / "fixture.zip";
    const auto destDir = root / "unpacked";
    MakeZip(zipPath, {{"EmptyDir/", ""}, {"BIN/CONFIG.BIN", "x"}});

    std::string error;
    REQUIRE(GameDataArchive::Unpack(zipPath.string().c_str(), destDir.string().c_str(), {}, &error));
    CHECK(std::filesystem::is_directory(destDir / "EmptyDir"));

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("GameDataArchive::Unpack invokes the progress callback for every entry", "[gamedata][archive]")
{
    const auto root = MakeTempDir();
    const auto zipPath = root / "fixture.zip";
    const auto destDir = root / "unpacked";
    MakeZip(zipPath, {{"a.txt", "a"}, {"b.txt", "b"}, {"c.txt", "c"}});

    int64_t lastDone = 0, lastTotal = 0, calls = 0;
    std::string error;
    REQUIRE(GameDataArchive::Unpack(
        zipPath.string().c_str(), destDir.string().c_str(),
        [&](int64_t done, int64_t total)
        {
            ++calls;
            lastDone = done;
            lastTotal = total;
        },
        &error));

    CHECK(calls == 3);
    CHECK(lastDone == 3);
    CHECK(lastTotal == 3);

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

// Zip-slip: an entry path containing ".." must be rejected outright, not
// merely normalized -- the archive is untrusted (a URL or a user-picked
// file), unlike ModArchive's closed PBO format.
TEST_CASE("GameDataArchive::Unpack rejects a zip-slip entry", "[gamedata][archive]")
{
    const auto root = MakeTempDir();
    const auto zipPath = root / "fixture.zip";
    const auto destDir = root / "unpacked";
    MakeZip(zipPath, {{"BIN/CONFIG.BIN", "ok"}, {"../../escape.txt", "malicious"}});

    std::string error;
    CHECK_FALSE(GameDataArchive::Unpack(zipPath.string().c_str(), destDir.string().c_str(), {}, &error));
    CHECK_FALSE(error.empty());

    // Nothing escaped destDir -- the malicious entry never got written anywhere.
    std::error_code ec;
    CHECK_FALSE(std::filesystem::exists(root / "escape.txt", ec));
    CHECK_FALSE(std::filesystem::exists(root.parent_path() / "escape.txt", ec));

    std::filesystem::remove_all(root, ec);
}

TEST_CASE("GameDataArchive::Unpack rejects an absolute-path entry", "[gamedata][archive]")
{
    const auto root = MakeTempDir();
    const auto zipPath = root / "fixture.zip";
    const auto destDir = root / "unpacked";
    MakeZip(zipPath, {{"/etc/passwd", "malicious"}});

    std::string error;
    CHECK_FALSE(GameDataArchive::Unpack(zipPath.string().c_str(), destDir.string().c_str(), {}, &error));
    CHECK_FALSE(error.empty());

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("GameDataArchive::Unpack fails cleanly on a missing archive", "[gamedata][archive]")
{
    const auto root = MakeTempDir();
    const auto destDir = root / "unpacked";

    std::string error;
    CHECK_FALSE(GameDataArchive::Unpack((root / "nope.zip").string().c_str(), destDir.string().c_str(), {}, &error));
    CHECK_FALSE(error.empty());

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}
