#include <Poseidon/IO/Filesystem/DirScanner.hpp>
#include <io.h>
#include <cstdio>
#include <string.h>
#include <cstring>

namespace Poseidon
{
DirScanner::DirScanner() : _info(nullptr), _handle(-1)
{
    _wild[0] = '\0';
}

DirScanner::~DirScanner()
{
    Close();
}

namespace
{
bool IsDotEntry(const _finddata_t* info)
{
    return info->name[0] == '.' && (info->name[1] == '\0' || (info->name[1] == '.' && info->name[2] == '\0'));
}
} // namespace

bool DirScanner::First(const char* dir, const char* ext)
{
    Close();
    // Build wildcard pattern: "dir\*.ext"
    snprintf(_wild, sizeof(_wild), "%s\\*%s", dir, ext ? ext : "");
    _info = new _finddata_t;
    _handle = _findfirst(_wild, (_finddata_t*)_info);
    if (_handle == -1)
    {
        delete (_finddata_t*)_info;
        _info = nullptr;
        return false;
    }
    // Skip `.` and `..` — _findfirst returns them on Windows but
    // callers (and the POSIX impl) treat directory listings as
    // "files/subdirs only".
    while (IsDotEntry((_finddata_t*)_info))
    {
        if (_findnext(_handle, (_finddata_t*)_info) != 0)
        {
            Close();
            return false;
        }
    }
    return true;
}

bool DirScanner::Next()
{
    if (!_info || _handle == -1)
        return false;
    while (_findnext(_handle, (_finddata_t*)_info) == 0)
    {
        if (!IsDotEntry((_finddata_t*)_info))
            return true;
    }
    return false;
}

void DirScanner::Close()
{
    if (_info)
    {
        delete (_finddata_t*)_info;
        _info = nullptr;
    }
    if (_handle != -1)
    {
        _findclose(_handle);
        _handle = -1;
    }
    _wild[0] = '\0';
}

const char* DirScanner::GetName() const
{
    if (!_info || _handle == -1)
        return "";
    return ((_finddata_t*)_info)->name;
}

bool DirScanner::IsDirectory() const
{
    if (!_info || _handle == -1)
        return false;
    return (((_finddata_t*)_info)->attrib & _A_SUBDIR) != 0;
}

} // namespace Poseidon
