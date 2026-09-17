// Unit tests for POSIX _findfirst/_findnext/_findclose helpers in PoseidonBase platform support
// Tests the *.* → * normalization and basic glob matching.

#include <catch2/catch_test_macros.hpp>
#include <Poseidon/Foundation/platform.hpp>
#include <Poseidon/IO/Filesystem/Utf8Paths.hpp>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <set>
#include <stdint.h>
#include <system_error>

namespace fs = std::filesystem;

#ifndef _WIN32 // These tests exercise the POSIX compat layer

namespace
{

/// RAII helper to create a temporary directory with test files/dirs.
struct TempTestDir
{
    fs::path root;

    explicit TempTestDir(const char* name)
    {
        root = fs::temp_directory_path() / name;
        fs::create_directories(root);
    }

    void createFile(const char* name) { std::ofstream(root / name).put(' '); }

    void createDir(const char* name) { fs::create_directories(root / name); }

    ~TempTestDir()
    {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

/// Collect all names from _findfirst/_findnext into a set.
std::set<std::string> findAll(const std::string& pattern)
{
    std::set<std::string> results;
    _finddata_t fd;
    intptr_t h = _findfirst(pattern.c_str(), &fd);
    if (h != -1)
    {
        do
        {
            results.insert(fd.name);
        } while (_findnext(h, &fd) == 0);
        _findclose(h);
    }
    return results;
}

} // anonymous namespace

TEST_CASE("_findfirst: *.* matches files without dots", "[platform][findfirst]")
{
    TempTestDir dir("test_findfirst_stardotstar");
    dir.createFile("readme.txt");
    dir.createFile("Makefile");  // no dot
    dir.createDir("Resistance"); // directory, no dot

    auto results = findAll(dir.root.string() + "/*.*");

    // *.* should behave like * (Windows compat) — match everything
    CHECK(results.count("readme.txt") == 1);
    CHECK(results.count("Makefile") == 1);
    CHECK(results.count("Resistance") == 1);
}

TEST_CASE("_findfirst: * matches all entries", "[platform][findfirst]")
{
    TempTestDir dir("test_findfirst_star");
    dir.createFile("data.pbo");
    dir.createFile("config");
    dir.createDir("subdir");

    auto results = findAll(dir.root.string() + "/*");

    CHECK(results.count("data.pbo") == 1);
    CHECK(results.count("config") == 1);
    CHECK(results.count("subdir") == 1);
}

TEST_CASE("_findfirst: *.pbo matches only .pbo files", "[platform][findfirst]")
{
    TempTestDir dir("test_findfirst_ext");
    dir.createFile("addon1.pbo");
    dir.createFile("addon2.pbo");
    dir.createFile("readme.txt");
    dir.createDir("subdir");

    auto results = findAll(dir.root.string() + "/*.pbo");

    CHECK(results.count("addon1.pbo") == 1);
    CHECK(results.count("addon2.pbo") == 1);
    CHECK(results.count("readme.txt") == 0);
    CHECK(results.count("subdir") == 0);
}

TEST_CASE("_findfirst: case-insensitive matching", "[platform][findfirst]")
{
    TempTestDir dir("test_findfirst_case");
    dir.createFile("Addon.PBO");
    dir.createFile("config.CPP");

    auto results = findAll(dir.root.string() + "/*.pbo");

    CHECK(results.count("Addon.PBO") == 1);
    CHECK(results.count("config.CPP") == 0);
}

TEST_CASE("_findfirst: skips . and .. entries", "[platform][findfirst]")
{
    TempTestDir dir("test_findfirst_dots");
    dir.createFile("file.txt");

    auto results = findAll(dir.root.string() + "/*");

    CHECK(results.count(".") == 0);
    CHECK(results.count("..") == 0);
    CHECK(results.count("file.txt") == 1);
}

TEST_CASE("_findfirst: nonexistent directory returns -1", "[platform][findfirst]")
{
    _finddata_t fd;
    intptr_t h = _findfirst("/tmp/nonexistent_dir_xyzzy/*", &fd);
    CHECK(h == -1);
}

TEST_CASE("_findfirst: empty directory returns -1", "[platform][findfirst]")
{
    TempTestDir dir("test_findfirst_empty");
    // No files created

    _finddata_t fd;
    intptr_t h = _findfirst((dir.root.string() + "/*").c_str(), &fd);
    CHECK(h == -1);
}

TEST_CASE("_findclose: closing -1 handle is safe", "[platform][findfirst]")
{
    int result = _findclose(-1);
    CHECK(result == -1);
}

#endif // !_WIN32

// Runs on every platform: a name a scan hands back has to reopen the entry it came from.
TEST_CASE("directory scan returns names that reopen the same entry", "[platform][findfirst][utf8]")
{
    // A name carrying characters outside any single legacy codepage. Windows best-fit maps what
    // the active ANSI codepage cannot encode, so a converted name points at a directory of its own.
    const std::string folder = "\xC3\x84\xC5\x92sla";
    const std::string marker = "marker.txt";

    const std::string root = (fs::temp_directory_path() / "cwr_scan_roundtrip").string();
    std::error_code ec;
    fs::remove_all(root, ec);
    REQUIRE(Poseidon::CreateDirectoryUtf8(root.c_str()));
    const std::string nested = root + "/" + folder;
    REQUIRE(Poseidon::CreateDirectoryUtf8(nested.c_str()));
    REQUIRE(Poseidon::WriteFileUtf8((nested + "/" + marker).c_str(), "x", 1));

    std::string scanned;
    _finddata_t entry;
    intptr_t handle = _findfirst((root + "/*").c_str(), &entry);
    REQUIRE(handle != -1);
    do
    {
        if ((entry.attrib & _A_SUBDIR) != 0 && entry.name[0] != '.')
        {
            scanned = entry.name;
        }
    } while (_findnext(handle, &entry) == 0);
    _findclose(handle);

    CHECK(scanned == folder);

    bool foundMarker = false;
    handle = _findfirst((root + "/" + scanned + "/*").c_str(), &entry);
    if (handle != -1)
    {
        do
        {
            if (marker == entry.name)
            {
                foundMarker = true;
            }
        } while (_findnext(handle, &entry) == 0);
        _findclose(handle);
    }
    CHECK(foundMarker);

    fs::remove_all(root, ec);
}
