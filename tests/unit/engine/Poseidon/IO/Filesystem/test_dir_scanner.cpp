#include <catch2/catch_test_macros.hpp>

#include <Poseidon/IO/Filesystem/DirScanner.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace
{

struct TempScanDir
{
    fs::path root;

    explicit TempScanDir(const char* label)
    {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        root = fs::temp_directory_path() / (std::string("poseidon_base_scan_") + label + "_" + std::to_string(nonce));
        fs::create_directories(root);
    }

    ~TempScanDir()
    {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

std::set<std::string> scanNames(const std::string& dir, const char* ext = ".pbo")
{
    std::set<std::string> names;
    DirScanner scanner;
    if (!scanner.First(dir.c_str(), ext))
        return names;

    do
    {
        names.insert(scanner.GetName());
    } while (scanner.Next());

    return names;
}

std::set<std::string> scanDirectories(const std::string& dir)
{
    std::set<std::string> names;
    DirScanner scanner;
    if (!scanner.First(dir.c_str(), nullptr))
        return names;

    do
    {
        if (scanner.IsDirectory())
            names.insert(scanner.GetName());
    } while (scanner.Next());

    return names;
}

} // namespace

TEST_CASE("DirScanner filters extensions case-insensitively", "[poseidon-base][io][dirscanner]")
{
    TempScanDir dir("ext_filter");
    std::ofstream(dir.root / "AddonOne.pbo").put('a');
    std::ofstream(dir.root / "AddonTwo.PBO").put('b');
    std::ofstream(dir.root / "readme.txt").put('c');

    const auto names = scanNames(dir.root.string(), ".pbo");

    REQUIRE(names.size() == 2);
    REQUIRE(names.count("AddonOne.pbo") == 1);
    REQUIRE(names.count("AddonTwo.PBO") == 1);
    REQUIRE(names.count("readme.txt") == 0);
}

TEST_CASE("DirScanner returns all non-dot entries when no extension filter is provided",
          "[poseidon-base][io][dirscanner]")
{
    TempScanDir dir("all_entries");
    std::ofstream(dir.root / "mission.sqm").put('x');
    fs::create_directories(dir.root / "Campaign");

    const auto names = scanNames(dir.root.string(), nullptr);

    REQUIRE(names.count("mission.sqm") == 1);
    REQUIRE(names.count("Campaign") == 1);
    REQUIRE(names.count(".") == 0);
    REQUIRE(names.count("..") == 0);
}

TEST_CASE("DirScanner reports failure for a missing directory", "[poseidon-base][io][dirscanner]")
{
    DirScanner scanner;
    REQUIRE_FALSE(scanner.First("/tmp/poseidon_base_dirscanner_missing_xyz", ".pbo"));
}

TEST_CASE("DirScanner exposes directory entries", "[poseidon-base][io][dirscanner]")
{
    TempScanDir dir("directory_entries");
    fs::create_directories(dir.root / "Mission.Eden");
    std::ofstream(dir.root / "Mission.Eden.pbo").put('x');

    const auto directories = scanDirectories(dir.root.string());

    REQUIRE(directories.count("Mission.Eden") == 1);
    REQUIRE(directories.count("Mission.Eden.pbo") == 0);
}

#ifndef _WIN32
TEST_CASE("DirScanner resolves mixed-case directories on POSIX", "[poseidon-base][io][dirscanner]")
{
    TempScanDir dir("case_fallback");
    fs::create_directories(dir.root / "DTA");
    std::ofstream(dir.root / "DTA" / "Abel.PBO").put('x');

    const auto names = scanNames((dir.root / "dta").string(), ".pbo");

    REQUIRE(names.size() == 1);
    REQUIRE(names.count("Abel.PBO") == 1);
}
#endif
