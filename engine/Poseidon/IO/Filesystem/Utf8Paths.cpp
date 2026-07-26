#include <Poseidon/IO/Filesystem/Utf8Paths.hpp>

#include <Poseidon/Foundation/platform.hpp>
#include <Poseidon/IO/Filesystem/FileOps.hpp>

#include <filesystem>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace Poseidon
{

std::filesystem::path FilesystemPathFromUtf8(const std::string& path)
{
#ifdef _WIN32
    return std::filesystem::path(Utf8PathToWide(path.c_str()));
#else
    return std::filesystem::path(path);
#endif
}

std::string FilesystemPathToUtf8(const std::filesystem::path& path)
{
#ifdef _WIN32
    return WidePathToUtf8(path.wstring().c_str());
#else
    return path.string();
#endif
}

#ifdef _WIN32
std::wstring Utf8PathToWide(const char* path)
{
    if (path == nullptr || path[0] == '\0')
    {
        return std::wstring();
    }

    int size = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, nullptr, 0);
    unsigned codepage = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (size == 0)
    {
        codepage = CP_ACP;
        flags = 0;
        size = ::MultiByteToWideChar(codepage, flags, path, -1, nullptr, 0);
    }
    if (size == 0)
    {
        return std::wstring();
    }

    std::wstring wide(static_cast<size_t>(size - 1), L'\0');
    ::MultiByteToWideChar(codepage, flags, path, -1, wide.data(), size);
    return wide;
}

std::string WidePathToUtf8(const wchar_t* path)
{
    if (path == nullptr || path[0] == L'\0')
    {
        return std::string();
    }

    const int size = ::WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
    if (size == 0)
    {
        return std::string();
    }

    std::string utf8(static_cast<size_t>(size - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8.data(), size, nullptr, nullptr);
    return utf8;
}

namespace
{

std::wstring JoinFindPattern(std::wstring dir)
{
    if (!dir.empty() && dir.back() != L'\\' && dir.back() != L'/')
    {
        dir.push_back(L'\\');
    }
    dir.push_back(L'*');
    return dir;
}

bool IsDotEntry(const wchar_t* name)
{
    return name[0] == L'.' && (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0'));
}

} // namespace

std::vector<DirectoryEntryUtf8> ListDirectoryEntriesUtf8(const char* dir)
{
    std::vector<DirectoryEntryUtf8> entries;
    const std::wstring wideDir = Utf8PathToWide(dir);
    if (wideDir.empty())
    {
        return entries;
    }

    WIN32_FIND_DATAW data;
    const std::wstring pattern = JoinFindPattern(wideDir);
    HANDLE handle = ::FindFirstFileW(pattern.c_str(), &data);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return entries;
    }

    do
    {
        if (IsDotEntry(data.cFileName))
        {
            continue;
        }

        DirectoryEntryUtf8 entry;
        entry.name = WidePathToUtf8(data.cFileName);
        entry.isDirectory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        entry.size = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        if (!entry.name.empty())
        {
            entries.push_back(std::move(entry));
        }
    } while (::FindNextFileW(handle, &data) != 0);

    ::FindClose(handle);
    return entries;
}

bool CopyFileUtf8(const char* src, const char* dst, bool failIfExists)
{
    const std::wstring wideSrc = Utf8PathToWide(src);
    const std::wstring wideDst = Utf8PathToWide(dst);
    if (wideSrc.empty() || wideDst.empty())
    {
        return false;
    }
    return ::CopyFileW(wideSrc.c_str(), wideDst.c_str(), failIfExists ? TRUE : FALSE) != FALSE;
}

bool RepairMissingFilePermissionsUtf8(const char* path)
{
    return FileExistsUtf8(path);
}

bool CreateDirectoryUtf8(const char* path)
{
    const std::wstring widePath = Utf8PathToWide(path);
    if (widePath.empty())
    {
        return false;
    }

    std::error_code ec;
    const std::filesystem::path fsPath(widePath);
    if (std::filesystem::is_directory(fsPath, ec))
    {
        return true;
    }
    return std::filesystem::create_directories(fsPath, ec) || std::filesystem::is_directory(fsPath, ec);
}

bool FileExistsUtf8(const char* path)
{
    const std::wstring widePath = Utf8PathToWide(path);
    if (widePath.empty())
    {
        return false;
    }
    const DWORD attrs = ::GetFileAttributesW(widePath.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::FILE* OpenFileUtf8(const char* path, const char* mode)
{
    const std::wstring widePath = Utf8PathToWide(path);
    const std::wstring wideMode = Utf8PathToWide(mode);
    if (widePath.empty() || wideMode.empty())
    {
        return nullptr;
    }
    return ::_wfopen(widePath.c_str(), wideMode.c_str());
}

std::vector<char> ReadFileUtf8(const char* path)
{
    const std::wstring widePath = Utf8PathToWide(path);
    if (widePath.empty())
    {
        return {};
    }

    std::ifstream input(std::filesystem::path(widePath), std::ios::binary);
    if (!input)
    {
        return {};
    }

    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0)
    {
        return {};
    }
    input.seekg(0, std::ios::beg);

    std::vector<char> data(static_cast<std::size_t>(size));
    if (!data.empty())
    {
        input.read(data.data(), static_cast<std::streamsize>(data.size()));
    }
    return input ? data : std::vector<char>();
}

bool WriteFileUtf8(const char* path, const void* data, std::size_t size)
{
    const std::wstring widePath = Utf8PathToWide(path);
    if (widePath.empty() || (data == nullptr && size > 0))
    {
        return false;
    }

    const std::filesystem::path fsPath(widePath);
    std::error_code ec;
    std::filesystem::create_directories(fsPath.parent_path(), ec);

    std::ofstream output(fsPath, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        return false;
    }
    if (size > 0)
    {
        output.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    }
    return static_cast<bool>(output);
}

#else

std::vector<DirectoryEntryUtf8> ListDirectoryEntriesUtf8(const char* dir)
{
    std::vector<DirectoryEntryUtf8> entries;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(dir ? dir : ""), ec))
    {
        DirectoryEntryUtf8 item;
        item.name = entry.path().filename().string();
        item.isDirectory = entry.is_directory(ec);
        item.size = item.isDirectory ? 0 : entry.file_size(ec);
        entries.push_back(std::move(item));
    }
    return entries;
}

bool CopyFileUtf8(const char* src, const char* dst, bool failIfExists)
{
    if (!src || !src[0] || !dst || !dst[0])
    {
        return false;
    }

    LocalPath(nativeSrc, src);
    LocalPath(nativeDst, dst);
    char resolvedSrc[MaxFileName];
    if (!ResolveFilePath(nativeSrc, resolvedSrc, sizeof(resolvedSrc)))
    {
        return false;
    }

    std::filesystem::path destination(nativeDst);
    char resolvedDst[MaxFileName];
    if (ResolveFilePath(nativeDst, resolvedDst, sizeof(resolvedDst)))
    {
        destination = resolvedDst;
    }
    else if (const std::filesystem::path parent = destination.parent_path();
             !parent.empty() && ResolveFilePath(parent.string().c_str(), resolvedDst, sizeof(resolvedDst)))
    {
        destination = std::filesystem::path(resolvedDst) / destination.filename();
    }

    std::error_code ec;
    const auto options =
        failIfExists ? std::filesystem::copy_options::none : std::filesystem::copy_options::overwrite_existing;
    return std::filesystem::copy_file(std::filesystem::path(resolvedSrc), destination, options, ec);
}

bool RepairMissingFilePermissionsUtf8(const char* path)
{
    LocalPath(nativePath, path ? path : "");
    char resolvedPath[MaxFileName];
    if (!ResolveFilePath(nativePath, resolvedPath, sizeof(resolvedPath)))
    {
        return false;
    }
    struct stat info{};
    if (stat(resolvedPath, &info) != 0 || !S_ISREG(info.st_mode))
    {
        return false;
    }
    if ((info.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO)) != 0)
    {
        return true;
    }
    return chmod(resolvedPath, S_IRUSR | S_IWUSR) == 0;
}

bool CreateDirectoryUtf8(const char* path)
{
    std::error_code ec;
    const std::filesystem::path fsPath(path ? path : "");
    if (std::filesystem::is_directory(fsPath, ec))
    {
        return true;
    }
    return std::filesystem::create_directories(fsPath, ec) || std::filesystem::is_directory(fsPath, ec);
}

bool FileExistsUtf8(const char* path)
{
    std::error_code ec;
    return std::filesystem::is_regular_file(std::filesystem::path(path ? path : ""), ec);
}

std::FILE* OpenFileUtf8(const char* path, const char* mode)
{
    return std::fopen(path, mode);
}

std::vector<char> ReadFileUtf8(const char* path)
{
    std::ifstream input(std::filesystem::path(path ? path : ""), std::ios::binary);
    if (!input)
    {
        return {};
    }

    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0)
    {
        return {};
    }
    input.seekg(0, std::ios::beg);

    std::vector<char> data(static_cast<std::size_t>(size));
    if (!data.empty())
    {
        input.read(data.data(), static_cast<std::streamsize>(data.size()));
    }
    return input ? data : std::vector<char>();
}

bool WriteFileUtf8(const char* path, const void* data, std::size_t size)
{
    if (data == nullptr && size > 0)
    {
        return false;
    }

    const std::filesystem::path fsPath(path ? path : "");
    std::error_code ec;
    std::filesystem::create_directories(fsPath.parent_path(), ec);

    std::ofstream output(fsPath, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        return false;
    }
    if (size > 0)
    {
        output.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    }
    return static_cast<bool>(output);
}

#endif

} // namespace Poseidon
