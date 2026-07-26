#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace Poseidon
{

struct DirectoryEntryUtf8
{
    std::string name;
    std::uint64_t size = 0;
    bool isDirectory = false;
};

std::filesystem::path FilesystemPathFromUtf8(const std::string& path);
std::string FilesystemPathToUtf8(const std::filesystem::path& path);
std::vector<DirectoryEntryUtf8> ListDirectoryEntriesUtf8(const char* dir);
bool CopyFileUtf8(const char* src, const char* dst, bool failIfExists);
bool RepairMissingFilePermissionsUtf8(const char* path);
bool CreateDirectoryUtf8(const char* path);
bool FileExistsUtf8(const char* path);
std::FILE* OpenFileUtf8(const char* path, const char* mode);
std::vector<char> ReadFileUtf8(const char* path);
bool WriteFileUtf8(const char* path, const void* data, std::size_t size);

#ifdef _WIN32
std::wstring Utf8PathToWide(const char* path);
std::string WidePathToUtf8(const wchar_t* path);
#endif

} // namespace Poseidon
