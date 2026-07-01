#include <Poseidon/IO/Filesystem/DirScanner.hpp>
#include <Poseidon/Foundation/platform.hpp>

#include <dirent.h>
#include <string.h>
#include <strings.h>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <errno.h>
#include <sys/stat.h>

namespace Poseidon
{
static DIR* ci_opendir(const char* path)
{
    // Fast path: exact case match
    DIR* d = opendir(path);
    if (d)
        return d;
    if (errno != ENOENT)
        return nullptr;

    // CI fallback: resolve directory components case-insensitively
    char work[MaxFileName];
    snprintf(work, sizeof(work), "%s", path);
    unixPath(work);

    char resolved[MaxFileName];
    bool absolute = (work[0] == '/');
    snprintf(resolved, sizeof(resolved), absolute ? "/" : ".");
    char* p = work + (absolute ? 1 : 0);

    while (*p)
    {
        char* slash = strchr(p, '/');
        size_t compLen = slash ? (size_t)(slash - p) : strlen(p);
        if (compLen == 0)
        {
            p++;
            continue;
        }

        char component[MaxFileName];
        snprintf(component, sizeof(component), "%.*s", (int)compLen, p);
        p += compLen;
        if (*p == '/')
            p++;

        // Exact match first
        char exact[MaxFileName];
        snprintf(exact, sizeof(exact), "%s/%s", resolved, component);
        DIR* tryDir = opendir(exact);
        if (tryDir)
        {
            closedir(tryDir);
            snprintf(resolved, sizeof(resolved), "%s", exact);
            continue;
        }

        // CI scan
        DIR* parent = opendir(resolved);
        if (!parent)
            return nullptr;
        bool found = false;
        struct dirent* entry;
        while ((entry = readdir(parent)))
        {
            if (strcasecmp(entry->d_name, component) == 0)
            {
                snprintf(exact, sizeof(exact), "%s/%s", resolved, entry->d_name);
                snprintf(resolved, sizeof(resolved), "%s", exact);
                found = true;
                break;
            }
        }
        closedir(parent);
        if (!found)
            return nullptr;
    }

    return opendir(resolved);
}

DirScanner::DirScanner() : _dir(nullptr), _entry(nullptr)
{
    _ext[0] = '\0';
    _path[0] = '\0';
}

DirScanner::~DirScanner()
{
    Close();
}

bool DirScanner::First(const char* dir, const char* ext)
{
    Close();

    // Store extension filter (lowercase for case-insensitive compare)
    if (ext && ext[0])
    {
        size_t i = 0;
        for (; ext[i] && i < sizeof(_ext) - 1; ++i)
            _ext[i] = (char)tolower((unsigned char)ext[i]);
        _ext[i] = '\0';
    }
    else
    {
        _ext[0] = '\0';
    }

    _dir = ci_opendir(dir);
    if (!_dir)
        return false;
    snprintf(_path, sizeof(_path), "%s", dir);

    return Next();
}

bool DirScanner::Next()
{
    if (!_dir)
        return false;

    struct dirent* entry;
    while ((entry = readdir((DIR*)_dir)))
    {
        if (!_ext[0])
        {
            // No filter — return all non-dot entries
            if (entry->d_name[0] != '.')
            {
                _entry = entry;
                return true;
            }
            continue;
        }

        // Extension filter (case-insensitive)
        int len = (int)strlen(entry->d_name);
        int extLen = (int)strlen(_ext);
        if (len > extLen && strcasecmp(entry->d_name + len - extLen, _ext) == 0)
        {
            _entry = entry;
            return true;
        }
    }

    closedir((DIR*)_dir);
    _dir = nullptr;
    _entry = nullptr;
    return false;
}

void DirScanner::Close()
{
    if (_dir)
    {
        closedir((DIR*)_dir);
        _dir = nullptr;
        _entry = nullptr;
        _path[0] = '\0';
    }
}

const char* DirScanner::GetName() const
{
    if (!_dir || !_entry)
        return "";
    return ((struct dirent*)_entry)->d_name;
}

bool DirScanner::IsDirectory() const
{
    if (!_dir || !_entry)
        return false;

    const struct dirent* entry = (const struct dirent*)_entry;
#ifdef DT_DIR
    if (entry->d_type == DT_DIR)
        return true;
    if (entry->d_type != DT_UNKNOWN)
        return false;
#endif

    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", _path, entry->d_name);
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

} // namespace Poseidon
