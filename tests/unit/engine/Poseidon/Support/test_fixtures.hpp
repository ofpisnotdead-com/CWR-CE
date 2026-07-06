#pragma once

#ifdef _WIN32
#include <Windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <limits.h>
#ifndef MAX_PATH
#define MAX_PATH PATH_MAX
#endif
#else
#include <unistd.h>
#include <limits.h>
#ifndef MAX_PATH
#define MAX_PATH PATH_MAX
#endif
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <filesystem>
#include <string>

namespace TestFixtures
{

inline const char* GetExecutableDirectory()
{
    static char exeDir[MAX_PATH] = {0};
    if (exeDir[0] == 0)
    {
#ifdef _WIN32
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        char* lastSlash = strrchr(exePath, '\\');
        if (lastSlash)
        {
            *lastSlash = '\0';
            strcpy(exeDir, exePath);
        }
#elif defined(__APPLE__)
        char exePath[MAX_PATH];
        uint32_t size = sizeof(exePath);
        if (_NSGetExecutablePath(exePath, &size) == 0)
        {
            char* lastSlash = strrchr(exePath, '/');
            if (lastSlash)
            {
                *lastSlash = '\0';
                strcpy(exeDir, exePath);
            }
        }
#else
        char exePath[MAX_PATH];
        ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
        if (len > 0)
        {
            exePath[len] = '\0';
            char* lastSlash = strrchr(exePath, '/');
            if (lastSlash)
            {
                *lastSlash = '\0';
                strcpy(exeDir, exePath);
            }
        }
#endif
    }
    return exeDir;
}

inline bool FileExists(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

inline std::filesystem::path ResolveFixturePath(const char* filename)
{
    std::filesystem::path exeDir(GetExecutableDirectory());
    std::filesystem::path localFixture = exeDir / "fixtures" / filename;
    if (FileExists(localFixture))
    {
        return localFixture;
    }

    for (std::filesystem::path current = exeDir; !current.empty(); current = current.parent_path())
    {
        std::filesystem::path repoFixture = current / "tests" / "fixtures" / filename;
        if (FileExists(repoFixture))
        {
            return repoFixture;
        }

        if (current == current.root_path())
        {
            break;
        }
    }

    return localFixture;
}

inline const char* GetTestFixturePath(const char* filename)
{
    static std::string path;
    path = ResolveFixturePath(filename).string();
    return path.c_str();
}

inline const char* GetTempFilePath(const char* filename)
{
    static char path[MAX_PATH];
#ifdef _WIN32
    snprintf(path, sizeof(path), "%s\\temp_%s", GetExecutableDirectory(), filename);
#else
    snprintf(path, sizeof(path), "%s/temp_%s", GetExecutableDirectory(), filename);
#endif
    return path;
}

inline bool FixtureExists(const char* filename)
{
    return FileExists(ResolveFixturePath(filename));
}

inline void CleanupTempFile(const char* filepath)
{
#ifdef _WIN32
    DeleteFileA(filepath);
#else
    remove(filepath);
#endif
}

inline void ReportMissingFixture(const char* filename)
{
    const char* path = GetTestFixturePath(filename);

    fprintf(stderr, "\n============================================\n");
    fprintf(stderr, "ERROR: Test fixture file not found!\n");
    fprintf(stderr, "============================================\n");
    fprintf(stderr, "File: %s\n", filename);
    fprintf(stderr, "Expected location: %s\n", path);
    fprintf(stderr, "============================================\n\n");
}

} // namespace TestFixtures

#define GET_FIXTURE(filename) TestFixtures::GetTestFixturePath(filename)
#define GET_TEMP_FILE(filename) TestFixtures::GetTempFilePath(filename)
#define REQUIRE_FIXTURE(filename)                         \
    do                                                    \
    {                                                     \
        if (!TestFixtures::FixtureExists(filename))       \
        {                                                 \
            TestFixtures::ReportMissingFixture(filename); \
            FAIL("Fixture file not found: " << filename); \
        }                                                 \
    } while (0)
