#include <catch2/catch_test_macros.hpp>

#include <Poseidon/IO/Filesystem/FileOps.hpp>
#include <Poseidon/IO/Filesystem/Utf8Paths.hpp>
#include <Poseidon/IO/Streams/QStream.hpp>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <iterator>
#include <system_error>
#include <set>
#include <Poseidon/Foundation/platform.hpp>

namespace fs = std::filesystem;

namespace
{

struct TempIoDir
{
    fs::path root;

    explicit TempIoDir(const char* label)
    {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        root = fs::temp_directory_path() / (std::string("poseidon_base_io_") + label + "_" + std::to_string(nonce));
        fs::create_directories(root);
    }

    ~TempIoDir()
    {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

std::string readText(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::string readTextUtf8(const std::string& path)
{
    HANDLE file = OpenFileForRead(path.c_str());
    REQUIRE(file != INVALID_HANDLE_VALUE);

    const int size = GetOpenFileSize(file);
    REQUIRE(size >= 0);

    std::string output(static_cast<size_t>(size), '\0');
    if (size > 0)
    {
        DWORD read = 0;
        REQUIRE(ReadFile(file, output.data(), static_cast<DWORD>(output.size()), &read, nullptr));
        REQUIRE(read == output.size());
    }
    CloseHandle(file);
    return output;
}

} // namespace

TEST_CASE("FileOps opens, writes, appends, and reports file existence", "[poseidon-base][io][fileops]")
{
    TempIoDir dir("fileops_basic");
    const fs::path outputPath = dir.root / "output.txt";
    const std::string outputPathString = outputPath.string();

    SECTION("open_write truncates and open_append preserves existing content")
    {
        HANDLE file = OpenFileForWrite(outputPathString.c_str(), true);
        REQUIRE(file != INVALID_HANDLE_VALUE);

        constexpr char initial[] = "Alpha";
        DWORD written = 0;
        REQUIRE(WriteFile(file, initial, std::strlen(initial), &written, nullptr));
        REQUIRE(written == std::strlen(initial));
        CloseHandle(file);

        file = OpenFileForAppend(outputPathString.c_str());
        REQUIRE(file != INVALID_HANDLE_VALUE);

        constexpr char suffix[] = "Beta";
        written = 0;
        REQUIRE(WriteFile(file, suffix, std::strlen(suffix), &written, nullptr));
        REQUIRE(written == std::strlen(suffix));
        CloseHandle(file);

        REQUIRE(readText(outputPath) == "AlphaBeta");
    }

    SECTION("open_read and file_size work on an existing file")
    {
        std::ofstream(outputPath, std::ios::binary) << "Payload";

        HANDLE file = OpenFileForRead(outputPathString.c_str());
        REQUIRE(file != INVALID_HANDLE_VALUE);
        REQUIRE(GetOpenFileSize(file) == 7);
        CloseHandle(file);
    }

    SECTION("path_exists distinguishes existing and missing paths")
    {
        std::ofstream(outputPath, std::ios::binary) << "x";
        REQUIRE(FilePathExists(outputPathString.c_str()));

        const std::string missingPath = (dir.root / "missing.txt").string();
        REQUIRE_FALSE(FilePathExists(missingPath.c_str()));
    }
}

TEST_CASE("FileOps preserves UTF-8 filenames across supported custom sound alphabets",
          "[poseidon-base][io][fileops][utf8]")
{
    TempIoDir dir("fileops_utf8");
    const std::string root = dir.root.string();
    const std::string srcDir = root + "/Sound";
    const std::string dstDir = root + "/Copy";
    fs::create_directories(srcDir);
    fs::create_directories(dstDir);

    const std::set<std::string> names = {
        "BO\xC5\xBD"
        "E, TO JE ALE SPOU\xC5\xA0\xC5\xA4.ogg",
        "cafe_\xC3\xA9_naive_\xC3\xB6.ogg",
        "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82_\xD0\xBC\xD0\xB8\xD1\x80.ogg",
    };

    for (const std::string& name : names)
    {
        const std::string path = srcDir + "/" + name;
        HANDLE file = OpenFileForWrite(path.c_str(), true);
        REQUIRE(file != INVALID_HANDLE_VALUE);

        constexpr char payload[] = "ogg";
        DWORD written = 0;
        REQUIRE(WriteFile(file, payload, std::strlen(payload), &written, nullptr));
        REQUIRE(written == std::strlen(payload));
        CloseHandle(file);
        REQUIRE(FilePathExists(path.c_str()));
    }

    std::set<std::string> listed;
    for (const Poseidon::DirectoryEntryUtf8& entry : Poseidon::ListDirectoryEntriesUtf8(srcDir.c_str()))
    {
        if (!entry.isDirectory)
        {
            listed.insert(entry.name);
            REQUIRE(entry.size == 3);
        }
    }
    REQUIRE(listed == names);

    for (const std::string& name : listed)
    {
        const std::string src = srcDir + "/" + name;
        const std::string dst = dstDir + "/" + name;
        REQUIRE(Poseidon::CopyFileUtf8(src.c_str(), dst.c_str(), false));
        REQUIRE(readTextUtf8(dst) == "ogg");
    }
}

TEST_CASE("UTF-8 file helpers read and write non-ASCII binary paths", "[poseidon-base][io][fileops][utf8]")
{
    TempIoDir dir("utf8_binary_helpers");
    const std::string path = dir.root.string() + "/nested/BO\xC5\xBD"
                                                 "E, TO JE ALE SPOU\xC5\xA0\xC5\xA4.ogg";

    const char payload[] = {'o', 'g', 'g', '\0', static_cast<char>(0x80), static_cast<char>(0xff)};
    REQUIRE(Poseidon::WriteFileUtf8(path.c_str(), payload, sizeof(payload)));
    REQUIRE(Poseidon::FileExistsUtf8(path.c_str()));

    const std::vector<char> read = Poseidon::ReadFileUtf8(path.c_str());
    REQUIRE(read == std::vector<char>(payload, payload + sizeof(payload)));
}

TEST_CASE("UTF-8 directory helper creates non-ASCII mission folders", "[poseidon-base][io][fileops][utf8]")
{
    TempIoDir dir("utf8_mission_directory");
    const std::string missionsDir = dir.root.string() + "/missions";
    const std::string missionName = "V\xC5\xA1"
                                    "echny vehikly - test.Intro";
    const std::string missionDir = missionsDir + "/" + missionName;
    const std::string missionFile = missionDir + "/mission.sqm";

    REQUIRE(Poseidon::CreateDirectoryUtf8(missionDir.c_str()));
    REQUIRE(Poseidon::WriteFileUtf8(missionFile.c_str(), "ok", 2));
    REQUIRE(Poseidon::FileExistsUtf8(missionFile.c_str()));

    bool found = false;
    for (const Poseidon::DirectoryEntryUtf8& entry : Poseidon::ListDirectoryEntriesUtf8(missionsDir.c_str()))
    {
        found = found || (entry.isDirectory && entry.name == missionName);
    }
    REQUIRE(found);
}

TEST_CASE("QOFStream writes UTF-8 filenames through platform file helpers", "[poseidon-base][io][fileops][utf8]")
{
    TempIoDir dir("qofstream_utf8");
    const std::string path = dir.root.string() + "/BO\xC5\xBD"
                                                 "E_TO_JE_ALE_SPOU\xC5\xA0\xC5\xA4.dat";

    QOFStream output;
    output.open(path.c_str());
    constexpr char payload[] = "stream-payload";
    output.write(payload, std::strlen(payload));
    output.close();

    REQUIRE_FALSE(output.fail());
    REQUIRE(FilePathExists(path.c_str()));
    REQUIRE(readTextUtf8(path) == payload);
}

#ifndef _WIN32
TEST_CASE("FileOps resolves mixed-case paths on POSIX", "[poseidon-base][io][fileops]")
{
    TempIoDir dir("fileops_ci");
    fs::create_directories(dir.root / "DTA");
    const fs::path actualPath = dir.root / "DTA" / "Config.BIN";
    std::ofstream(actualPath, std::ios::binary) << "CaseSensitive";

    const std::string mixedCasePath = (dir.root / "dta" / "config.bin").string();

    REQUIRE(FilePathExists(mixedCasePath.c_str()));

    HANDLE file = OpenFileForRead(mixedCasePath.c_str());
    REQUIRE(file != INVALID_HANDLE_VALUE);
    REQUIRE(GetOpenFileSize(file) == 13);
    CloseHandle(file);
}

// Regression: an empty wrong-case sibling directory must not shadow the populated real
// one. The single-mission launch builds a lowercase "missions\..." path while the data
// ships as "Missions/"; an empty lowercase "missions/" (auto-created elsewhere) used to
// make ci_resolve_path commit to the exact-case empty dir and report ENOENT, so the
// mission never loaded and the game dropped back to the menu. Without the backtracking
// fix this resolves to nothing; with it, the file in "Missions/" is found.
TEST_CASE("FileOps backtracks past an empty wrong-case sibling directory", "[poseidon-base][io][fileops]")
{
    TempIoDir dir("fileops_shadow");
    fs::create_directories(dir.root / "missions");                       // empty, lowercase — the shadow
    fs::create_directories(dir.root / "Missions" / "01TakeTheCar.ABEL"); // real, capitalised
    const fs::path actualPath = dir.root / "Missions" / "01TakeTheCar.ABEL" / "mission.sqm";
    std::ofstream(actualPath, std::ios::binary) << "version=11";

    const std::string lowercasePath = (dir.root / "missions" / "01TakeTheCar.ABEL" / "mission.sqm").string();

    REQUIRE(FilePathExists(lowercasePath.c_str()));

    HANDLE file = OpenFileForRead(lowercasePath.c_str());
    REQUIRE(file != INVALID_HANDLE_VALUE);
    REQUIRE(GetOpenFileSize(file) == 10);
    CloseHandle(file);
}
#endif
