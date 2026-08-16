#pragma once

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#include <limits.h>
#ifndef MAX_PATH
#define MAX_PATH PATH_MAX
#if __APPLE__
#include <mach-o/dyld.h>
#endif
#endif
#endif
#include <stdio.h>
#include <string.h>
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
#else
        char exePath[MAX_PATH];
#if __APPLE__
        uint32_t len = sizeof(exePath) - 1;
        _NSGetExecutablePath(exePath, &len);
#else
        ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
#endif
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

inline const char* GetTestFixturePath(const char* filename)
{
    static char path[MAX_PATH];
#ifdef _WIN32
    snprintf(path, sizeof(path), "%s\\fixtures\\%s", GetExecutableDirectory(), filename);
#else
    snprintf(path, sizeof(path), "%s/fixtures/%s", GetExecutableDirectory(), filename);
#endif
    return path;
}

inline std::string GetFixtureDir(const char* subdir)
{
    char path[MAX_PATH];
#ifdef _WIN32
    snprintf(path, sizeof(path), "%s\\fixtures\\%s", GetExecutableDirectory(), subdir);
#else
    snprintf(path, sizeof(path), "%s/fixtures/%s", GetExecutableDirectory(), subdir);
#endif
    return path;
}

} // namespace TestFixtures

#define GET_FIXTURE(filename) TestFixtures::GetTestFixturePath(filename)
#define GET_FIXTURE_DIR(subdir) TestFixtures::GetFixtureDir(subdir)
