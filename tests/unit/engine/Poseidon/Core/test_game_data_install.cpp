#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Core/GameDataInstall.hpp>

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
               ("cwr_gamedatainstall_" + std::to_string(rd()) + "_" + std::to_string(rd()));
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

// Writes every RequiredGameDataPaths() entry (as a non-empty file, or a
// directory with one file in it) under dir, so DetectGameDataStatus only fails
// on whatever the manifest step itself controls.
void WriteRequiredPaths(const std::filesystem::path& dir)
{
    for (const std::string& relative : RequiredGameDataPaths())
    {
        const auto full = dir / relative;
        // Heuristic matching RequiredGameDataPaths() today: an entry with a
        // '.' in its final component is a file, otherwise a directory.
        if (full.filename().string().find('.') != std::string::npos)
        {
            std::filesystem::create_directories(full.parent_path());
            std::ofstream(full, std::ios::binary) << "x";
        }
        else
        {
            std::filesystem::create_directories(full);
            std::ofstream(full / "placeholder.pbo", std::ios::binary) << "x";
        }
    }
}
} // namespace

TEST_CASE("DetectGameDataStatus is Missing when the directory doesn't exist", "[gamedata][install]")
{
    const auto root = MakeTempDir();
    const auto dir = (root / "GameData").string();

    CHECK(DetectGameDataStatus(dir) == GameDataStatus::Missing);

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("DetectGameDataStatus is Partial when required files are present but no manifest", "[gamedata][install]")
{
    const auto root = MakeTempDir();
    const auto dir = root / "GameData";
    WriteRequiredPaths(dir);

    CHECK(DetectGameDataStatus(dir.string()) == GameDataStatus::Partial);

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("DetectGameDataStatus is Partial when the manifest exists but a required path is missing",
         "[gamedata][install]")
{
    const auto root = MakeTempDir();
    const auto dir = root / "GameData";
    WriteRequiredPaths(dir);
    // Remove one required entry after writing the manifest, simulating a
    // tampered/partially-deleted install.
    std::string error;
    REQUIRE(WriteGameDataManifest(dir.string(), "", 100, 3, &error));
    std::error_code ec;
    std::filesystem::remove(dir / "BIN" / "CONFIG.BIN", ec);

    CHECK(DetectGameDataStatus(dir.string()) == GameDataStatus::Partial);

    std::filesystem::remove_all(root, ec);
}

TEST_CASE("DetectGameDataStatus is Ready once the manifest and every required path exist", "[gamedata][install]")
{
    const auto root = MakeTempDir();
    const auto dir = root / "GameData";
    WriteRequiredPaths(dir);
    std::string error;
    REQUIRE(WriteGameDataManifest(dir.string(), "https://example.com/data.zip", 12345, 42, &error));

    CHECK(DetectGameDataStatus(dir.string()) == GameDataStatus::Ready);

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("WriteGameDataManifest omits sourceUrl when empty and creates the directory", "[gamedata][install]")
{
    const auto root = MakeTempDir();
    const auto dir = (root / "GameData").string();
    std::string error;

    REQUIRE(WriteGameDataManifest(dir, "", 0, 0, &error));
    CHECK(std::filesystem::exists(std::filesystem::path(dir) / "manifest.json"));

    std::ifstream in(std::filesystem::path(dir) / "manifest.json");
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(text.find("sourceUrl") == std::string::npos);
    CHECK(text.find("schemaVersion") != std::string::npos);

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("DeleteGameData removes the directory and resets status to Missing", "[gamedata][install]")
{
    const auto root = MakeTempDir();
    const auto dir = root / "GameData";
    WriteRequiredPaths(dir);
    std::string error;
    REQUIRE(WriteGameDataManifest(dir.string(), "", 1, 1, &error));
    REQUIRE(DetectGameDataStatus(dir.string()) == GameDataStatus::Ready);

    REQUIRE(DeleteGameData(dir.string(), &error));
    CHECK_FALSE(std::filesystem::exists(dir));
    CHECK(DetectGameDataStatus(dir.string()) == GameDataStatus::Missing);

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("DeleteGameData on an already-missing directory is not an error", "[gamedata][install]")
{
    const auto root = MakeTempDir();
    const auto dir = (root / "GameData").string();
    std::string error;

    CHECK(DeleteGameData(dir, &error));
    CHECK(error.empty());

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}
