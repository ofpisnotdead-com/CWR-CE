// Unit tests for PoseidonBase platform path utilities

#include <catch2/catch_test_macros.hpp>
#include <Poseidon/Foundation/platform.hpp>
#include <string.h>
#include <string>

#ifndef _WIN32
#include "../../Support/test_fixtures.hpp"
#include <fcntl.h>
#include <sys/stat.h>
#endif

TEST_CASE("PATH_SEP is correct for platform", "[platform]")
{
#ifdef _WIN32
    REQUIRE(PATH_SEP == '\\');
#else
    REQUIRE(PATH_SEP == '/');
#endif
}

TEST_CASE("platformPath (char*) normalizes separators", "[platform]")
{
    SECTION("backslashes to forward slashes on Linux")
    {
        char path[] = "MPMissions\\__cur_mp.eden\\";
        platformPath(path);
#ifdef _WIN32
        REQUIRE(strcmp(path, "MPMissions\\__cur_mp.eden\\") == 0);
#else
        REQUIRE(strcmp(path, "MPMissions/__cur_mp.eden/") == 0);
#endif
    }

    SECTION("forward slashes to backslashes on Windows")
    {
        char path[] = "MPMissions/__cur_mp.eden/";
        platformPath(path);
#ifdef _WIN32
        REQUIRE(strcmp(path, "MPMissions\\__cur_mp.eden\\") == 0);
#else
        REQUIRE(strcmp(path, "MPMissions/__cur_mp.eden/") == 0);
#endif
    }

    SECTION("mixed separators")
    {
        char path[] = "Users\\retro/Saved\\mpmissions/file.pbo";
        platformPath(path);
#ifdef _WIN32
        REQUIRE(strcmp(path, "Users\\retro\\Saved\\mpmissions\\file.pbo") == 0);
#else
        REQUIRE(strcmp(path, "Users/retro/Saved/mpmissions/file.pbo") == 0);
#endif
    }

    SECTION("empty string is safe")
    {
        char path[] = "";
        platformPath(path);
        REQUIRE(strcmp(path, "") == 0);
    }

    SECTION("null pointer is safe")
    {
        platformPath(nullptr);
    }

    SECTION("no separators unchanged")
    {
        char path[] = "file.pbo";
        platformPath(path);
        REQUIRE(strcmp(path, "file.pbo") == 0);
    }
}

TEST_CASE("platformPath (std::string) normalizes separators", "[platform]")
{
    SECTION("backslashes normalized")
    {
        std::string result = platformPath(std::string("MPMissions\\test.eden\\mission.sqm"));
#ifdef _WIN32
        REQUIRE(result == "MPMissions\\test.eden\\mission.sqm");
#else
        REQUIRE(result == "MPMissions/test.eden/mission.sqm");
#endif
    }

    SECTION("forward slashes normalized")
    {
        std::string result = platformPath(std::string("MPMissions/test.eden/mission.sqm"));
#ifdef _WIN32
        REQUIRE(result == "MPMissions\\test.eden\\mission.sqm");
#else
        REQUIRE(result == "MPMissions/test.eden/mission.sqm");
#endif
    }

    SECTION("empty string returns empty")
    {
        REQUIRE(platformPath(std::string("")).empty());
    }
}

#ifndef _WIN32
TEST_CASE("fileCopy", "[platform][filecopy]")
{
    const char* srcPath = TestFixtures::GetTempFilePath("filecopy_src.dat");
    const char* dstPath = TestFixtures::GetTempFilePath("filecopy_dst.dat");

    const char* content = "fileCopy regression payload";
    int fd = open(srcPath, O_CREAT | O_WRONLY | O_TRUNC, S_IREAD | S_IWRITE);
    REQUIRE(fd >= 0);
    REQUIRE(write(fd, content, strlen(content)) == (ssize_t)strlen(content));
    close(fd);

    SECTION("copies content and grants owner read+write")
    {
        // Regression for a real bug: the destination open() was missing its mode
        // argument, so O_CREAT read an unspecified vararg for the permission bits.
        // Observed in practice as a copied continue.fps missing the owner-read bit
        // entirely, which made every subsequent attempt to load it fail silently.
        REQUIRE(fileCopy(srcPath, dstPath) == true);

        struct stat st;
        REQUIRE(stat(dstPath, &st) == 0);
        REQUIRE((st.st_mode & S_IRUSR) != 0);
        REQUIRE((st.st_mode & S_IWUSR) != 0);

        int rfd = open(dstPath, O_RDONLY);
        REQUIRE(rfd >= 0);
        char buf[64] = {};
        ssize_t n = read(rfd, buf, sizeof(buf) - 1);
        close(rfd);
        REQUIRE(n == (ssize_t)strlen(content));
        REQUIRE(strcmp(buf, content) == 0);
    }

    SECTION("no leftover .tmp file after a successful copy")
    {
        REQUIRE(fileCopy(srcPath, dstPath) == true);
        std::string tmpPath = std::string(dstPath) + ".tmp";
        struct stat st;
        REQUIRE(stat(tmpPath.c_str(), &st) != 0); // must not exist
    }

    TestFixtures::CleanupTempFile(srcPath);
    TestFixtures::CleanupTempFile(dstPath);
}
#endif
