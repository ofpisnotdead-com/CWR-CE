#include <Poseidon/Foundation/platform.hpp>

#undef _findfirst
#undef _findnext
#undef _findclose

#include <Poseidon/IO/Filesystem/Utf8Paths.hpp>

#include <windows.h>

#include <cstring>
#include <string>

namespace Poseidon
{
namespace
{
struct Utf8FindHandle
{
    HANDLE find = INVALID_HANDLE_VALUE;
};

// The name buffer in _finddata_t is fixed, and UTF-8 spends more bytes on a name than the ANSI
// encoding does, so a name too long for it is cut on a code point boundary.
void StoreName(const std::wstring& wide, _finddata_t* info)
{
    const std::string utf8 = WidePathToUtf8(wide.c_str());
    std::size_t length = utf8.size();
    if (length >= sizeof(info->name))
    {
        length = sizeof(info->name) - 1;
        while (length > 0 && (static_cast<unsigned char>(utf8[length]) & 0xC0) == 0x80)
        {
            --length;
        }
    }
    std::memcpy(info->name, utf8.c_str(), length);
    info->name[length] = '\0';
}

void FillEntry(const WIN32_FIND_DATAW& data, _finddata_t* info)
{
    info->attrib = 0;
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        info->attrib |= _A_SUBDIR;
    }
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0)
    {
        info->attrib |= _A_RDONLY;
    }
    info->size = data.nFileSizeLow;
    info->time_create = -1L;
    info->time_access = -1L;
    info->time_write = -1L;
    StoreName(data.cFileName, info);
}
} // namespace

intptr_t Utf8FindFirst(const char* pattern, _finddata_t* info)
{
    if (pattern == nullptr || info == nullptr)
    {
        return -1;
    }

    const std::wstring wide = Utf8PathToWide(pattern);
    if (wide.empty())
    {
        return -1;
    }

    WIN32_FIND_DATAW data;
    const HANDLE find = ::FindFirstFileW(wide.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
    {
        return -1;
    }

    auto* handle = new Utf8FindHandle;
    handle->find = find;
    FillEntry(data, info);
    return reinterpret_cast<intptr_t>(handle);
}

int Utf8FindNext(intptr_t handle, _finddata_t* info)
{
    if (handle == -1 || info == nullptr)
    {
        return -1;
    }

    auto* find = reinterpret_cast<Utf8FindHandle*>(handle);
    WIN32_FIND_DATAW data;
    if (::FindNextFileW(find->find, &data) == 0)
    {
        return -1;
    }
    FillEntry(data, info);
    return 0;
}

int Utf8FindClose(intptr_t handle)
{
    if (handle == -1)
    {
        return -1;
    }

    auto* find = reinterpret_cast<Utf8FindHandle*>(handle);
    if (find->find != INVALID_HANDLE_VALUE)
    {
        ::FindClose(find->find);
    }
    delete find;
    return 0;
}

} // namespace Poseidon
