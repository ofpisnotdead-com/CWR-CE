#include <catch2/catch_test_macros.hpp>

#include <Poseidon/UI/GameDataDownloadSupport.hpp>

#include <zip.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <system_error>

using namespace Poseidon;

namespace
{
std::filesystem::path MakeTempDir()
{
    std::random_device rd;
    auto dir = std::filesystem::temp_directory_path() /
               ("cwr_gamedatadownload_" + std::to_string(rd()) + "_" + std::to_string(rd()));
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

void MakeZip(const std::filesystem::path& zipPath)
{
    int err = 0;
    zip_t* za = zip_open(zipPath.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    REQUIRE(za != nullptr);
    for (const auto& [name, content] : std::initializer_list<std::pair<const char*, const char*>>{
             {"BIN/CONFIG.BIN", "config"}, {"DTA/Fonts.pbo", "fonts"}, {"AddOns/O.pbo", "addon"}})
    {
        zip_source_t* src = zip_source_buffer(za, content, std::string(content).size(), 0);
        REQUIRE(src != nullptr);
        REQUIRE(zip_file_add(za, name, src, ZIP_FL_ENC_UTF_8) >= 0);
    }
    REQUIRE(zip_close(za) == 0);
}
} // namespace

// MakeGameDataDownloadTask's postStep is exercised directly, standing in for
// the real DownloadWorker/transport (already covered generically by
// test_download_worker.cpp) -- this covers the part specific to game data:
// unpack -> manifest -> cleanup wiring, without a real network fetch.
TEST_CASE("MakeGameDataDownloadTask's postStep unpacks, writes the manifest, and cleans up", "[gamedata][download]")
{
    const auto root = MakeTempDir();
    const auto gameDataDir = (root / "GameData").string();
    const std::string url = "https://example.com/gamedata.zip";

    DownloadTask task = MakeGameDataDownloadTask(url, gameDataDir);
    CHECK(task.destPath == gameDataDir + ".zip.download");
    MakeZip(task.destPath);

    std::string error;
    REQUIRE(task.postStep(task, error));
    CHECK(error.empty());

    CHECK(DetectGameDataStatus(gameDataDir) == GameDataStatus::Ready);
    CHECK_FALSE(std::filesystem::exists(task.destPath)); // temp archive removed

    std::ifstream manifest(std::filesystem::path(gameDataDir) / "manifest.json");
    const std::string text((std::istreambuf_iterator<char>(manifest)), std::istreambuf_iterator<char>());
    CHECK(text.find(url) != std::string::npos);

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("MakeGameDataDownloadTask's postStep leaves Partial status on a bad archive", "[gamedata][download]")
{
    const auto root = MakeTempDir();
    const auto gameDataDir = (root / "GameData").string();

    DownloadTask task = MakeGameDataDownloadTask("https://example.com/gamedata.zip", gameDataDir);
    std::ofstream(task.destPath, std::ios::binary) << "not a zip file";

    std::string error;
    CHECK_FALSE(task.postStep(task, error));
    CHECK_FALSE(error.empty());
    CHECK(DetectGameDataStatus(gameDataDir) != GameDataStatus::Ready);

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}
