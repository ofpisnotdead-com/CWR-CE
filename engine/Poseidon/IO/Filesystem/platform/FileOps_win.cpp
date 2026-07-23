#include <Poseidon/IO/Filesystem/FileOps.hpp>
#include <Poseidon/IO/Filesystem/Utf8Paths.hpp>
#include <windows.h>

namespace Poseidon
{
HANDLE OpenFileForRead(const char* path)
{
    const std::wstring wide = Utf8PathToWide(path);
    HANDLE h = wide.empty() ? INVALID_HANDLE_VALUE
                            : ::CreateFileW(wide.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                            FILE_FLAG_RANDOM_ACCESS, nullptr);
    return (h == INVALID_HANDLE_VALUE) ? INVALID_HANDLE_VALUE : h;
}

HANDLE OpenFileForWrite(const char* path, bool truncate)
{
    DWORD creation = truncate ? CREATE_ALWAYS : OPEN_ALWAYS;
    const std::wstring wide = Utf8PathToWide(path);
    HANDLE h = wide.empty()
                   ? INVALID_HANDLE_VALUE
                   : ::CreateFileW(wide.c_str(), GENERIC_WRITE, 0, nullptr, creation, FILE_ATTRIBUTE_NORMAL, nullptr);
    return (h == INVALID_HANDLE_VALUE) ? INVALID_HANDLE_VALUE : h;
}

HANDLE OpenFileForAppend(const char* path)
{
    const std::wstring wide = Utf8PathToWide(path);
    HANDLE h = wide.empty() ? INVALID_HANDLE_VALUE
                            : ::CreateFileW(wide.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                                            FILE_ATTRIBUTE_NORMAL, nullptr);
    return (h == INVALID_HANDLE_VALUE) ? INVALID_HANDLE_VALUE : h;
}

int GetOpenFileSize(HANDLE f)
{
    DWORD size = ::GetFileSize(f, nullptr);
    return (size == INVALID_FILE_SIZE) ? -1 : (int)size;
}

bool FilePathExists(const char* path)
{
    const std::wstring wide = Utf8PathToWide(path);
    return !wide.empty() && ::GetFileAttributesW(wide.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool ResolveFilePath(const char* path, char* out, size_t outLen)
{
    // Windows' filesystem is case-insensitive; the path opens regardless of case.
    if (outLen == 0 || !FilePathExists(path))
        return false;
    ::lstrcpynA(out, path, (int)outLen); // copies up to outLen-1 chars and null-terminates
    return true;
}

} // namespace Poseidon
