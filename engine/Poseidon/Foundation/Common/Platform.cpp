#ifndef _WIN32


#include <strings.h>
#include <sys/types.h>
#include <utime.h>
#include <ctime>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/platform.hpp>

int strcmpi(const char* a, const char* b)
{
    return strcasecmp(a, b);
}

int stricmp(const char* a, const char* b)
{
    return strcasecmp(a, b);
}

int strnicmp(const char* a, const char* b, int n)
{
    return strncasecmp(a, b, (size_t)n);
}

char* strlwr(char* a)
{
    if (!a)
        return nullptr;
    for (char* p = a; *p; ++p)
        *p = (char)tolower((unsigned char)*p);
    return a;
}

char* strupr(char* a)
{
    if (!a)
        return nullptr;
    for (char* p = a; *p; ++p)
        *p = (char)toupper((unsigned char)*p);
    return a;
}

void unixPath(char* path)
{
    if (!path)
        return;
    do
    {
        if (*path == '\\')
            *path = '/';
#ifdef _WIN32
        else if (isupper(*path))
            *path = tolower(*path);
#endif
    } while (*(++path));
}

bool isSuffix(const char* str, const char* suffix)
{
    if (!suffix || !suffix[0])
        return true;
    if (!str || !str[0])
        return false;
    int strLen = strlen(str);
    int suffLen = strlen(suffix);
    if (strLen < suffLen)
        return false;
    return (!strcmp(str + strLen - suffLen, suffix));
}

long long atoi64(const char* str)
{
    if (!str || !str[0])
        return 0;
    long long result = 0;
    while (isdigit(*str))
    {
        result *= 10;
        result += *str++ - '0';
    }
    return result;
}

char* i64toa(long long i, char* buf, int radix)
{
    if (!buf || radix < 2 || radix > 10)
        return nullptr;
    int len = 0;
    bool sign = false;
    if (i < 0)
    {
        sign = true;
        len = 1;
        i = -i;
    }
    long long tmp = i;
    do
        len++;
    while ((tmp /= radix));
    char* ptr = buf + len;
    *ptr-- = (char)0;
    do
        *ptr-- = '0' + (i % radix);
    while ((i /= radix));
    if (sign)
        *ptr = '-';
    return buf;
}

char* strDup(const char* src)
{
    if (!src)
        return nullptr;
    char* res = (char*)malloc(strlen(src) + 1);
    if (res)
        strcpy(res, src);
    return res;
}

const long long Origin = 0x19db1ded53e8000LL;

bool fileTime(const char* fileName, long long win32Time)
// returns 'true' on success
{
    if (!fileName || !fileName[0])
        return false;
    LocalPath(fn, fileName);
    struct stat st;
    stat(fn, &st);
    struct utimbuf utim;
    if (win32Time > Origin)
        win32Time = (win32Time - Origin) / 10000000LL;
    else
        win32Time = 1;
    utim.actime = st.st_atime;
    utim.modtime = (time_t)win32Time;
    return (utime(fn, &utim) == 0);
}

const size_t copyBuf = 1024;

bool fileCopy(const char* src, const char* dest)
// returns 'true' on success
{
    if (!src || !src[0] || !dest || !dest[0])
        return false;
    LocalPath(sfn, src);
    LocalPath(dfn, dest);
    int f1 = open(sfn, O_RDONLY);
    if (f1 < 0)
        return false;
    // Copy through a same-directory ".tmp" sibling, then atomically rename over the
    // real destination once the full copy has landed -- a partial read/write (or a
    // process kill mid-copy) must never leave a truncated file sitting at dest.
    char dtmp[MaxFileName + 8];
    snprintf(dtmp, sizeof(dtmp), "%s.tmp", dfn);
    // O_CREAT without an explicit mode reads an unspecified vararg for the
    // permission bits -- this previously left copied files (e.g. continue.fps
    // regenerated from save.fps) with garbage permissions, sometimes missing
    // the owner-read bit entirely, which made every later attempt to load the
    // "copied" file fail silently at open(). S_IREAD | S_IWRITE matches the
    // mode QOFStream::close() uses for the same class of save file.
    int f2 = open(dtmp, O_CREAT | O_WRONLY | O_TRUNC, S_IREAD | S_IWRITE);
    if (f2 < 0)
    {
        close(f1);
        return false;
    }
    char buf[copyBuf];
    bool result = true;
    do
    {
        size_t rd = read(f1, buf, copyBuf);
        if (!rd)
            break;
        size_t wr = write(f2, buf, rd);
        if (wr != rd)
        {
            result = false;
            break;
        }
    } while (true);
    close(f1);
    close(f2);
    if (!result)
    {
        unlink(dtmp);
        return false;
    }
    // copy file attributes onto the temp file before the atomic rename:
    struct stat sstat;
    struct stat dtstat;
    stat(sfn, &sstat);
    stat(dtmp, &dtstat);
    struct utimbuf dtim;
    dtim.actime = dtstat.st_atime; // not changed!
    dtim.modtime = sstat.st_mtime;
    if (utime(dtmp, &dtim) != 0)
    {
        unlink(dtmp);
        return false;
    }
    if (rename(dtmp, dfn) != 0)
    {
        unlink(dtmp);
        return false;
    }
    return true;
}

void createDirectory(const char* dir)
{
    if (!dir || !dir[0])
        return;
    LocalPath(udir, dir);
    if (mkdir(udir, NEW_DIRECTORY_MODE) != 0 && errno == EEXIST)
    {
        // Directory already exists — ensure it has correct permissions
        chmod(udir, NEW_DIRECTORY_MODE);
    }
}

void deleteFile(const char* path)
{
    if (!path || !path[0])
        return;
    LocalPath(upath, path);
    unlink(upath);
}

bool fileMove(const char* src, const char* dest)
{
    if (!src || !src[0] || !dest || !dest[0])
        return false;
    LocalPath(sfn, src);
    LocalPath(dfn, dest);
    return rename(sfn, dfn) == 0;
}

#include <Poseidon/Foundation/Framework/DebugLog.hpp>

size_t linuxMemoryUsage()
{
    char procfn[32];
    snprintf(procfn, sizeof(procfn), "/proc/%d/statm", getpid());
    FILE* f = fopen(procfn, "rt");
    if (!f)
        return 0;
    int size = 0, resident = 0, share = 0, trs = 0, lrs = 0, drs = 0, dt = 0;
    fscanf(f, "%d %d %d %d %d %d %d", &size, &resident, &share, &trs, &lrs, &drs, &dt);
    fclose(f);
    size_t pageSize = getpagesize();
    LOG_DEBUG(Core, "Memory total: {} KB, shared: {} KB, data: {} KB", (size * pageSize) >> 10,
              (share * pageSize) >> 10, (drs * pageSize) >> 10);
    return (drs * pageSize);
}

// --- POSIX _findfirst/_findnext/_findclose with case-insensitive dir resolution ---

#include <cstdlib>
#include <cstring>
#include <fnmatch.h>

struct FindHandle
{
    DIR* dir;
    char dirPath[MaxFileName]; // resolved directory path
    char pattern[MaxFileName]; // glob pattern (filename part only)
};

// Resolve a path with case-insensitive component matching.
// Converts backslashes to '/' and resolves each component via CI scan.
// Returns true on success, writing the resolved path to 'out'.
static bool ci_resolve_path(const char* path, char* out, size_t outSize)
{
    char work[MaxFileName];
    snprintf(work, sizeof(work), "%s", path);
    // Convert backslashes
    for (char* p = work; *p; ++p)
        if (*p == '\\')
            *p = '/';

    bool absolute = (work[0] == '/');
    snprintf(out, outSize, "%s", absolute ? "/" : ".");
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

        // Exact match
        char exact[MaxFileName];
        snprintf(exact, sizeof(exact), "%s/%s", out, component);
        struct stat st;
        if (stat(exact, &st) == 0)
        {
            snprintf(out, outSize, "%s", exact);
            continue;
        }

        // CI scan of parent directory
        DIR* parent = opendir(out);
        if (!parent)
            return false;
        bool found = false;
        struct dirent* entry;
        while ((entry = readdir(parent)))
        {
            if (strcasecmp(entry->d_name, component) == 0)
            {
                snprintf(exact, sizeof(exact), "%s/%s", out, entry->d_name);
                snprintf(out, outSize, "%s", exact);
                found = true;
                break;
            }
        }
        closedir(parent);
        if (!found)
            return false;
    }
    return true;
}

intptr_t _findfirst(const char* pattern, _finddata_t* info)
{
    if (!pattern || !info)
        return -1;

    // Convert to unix path
    char work[MaxFileName];
    snprintf(work, sizeof(work), "%s", pattern);
    for (char* p = work; *p; ++p)
        if (*p == '\\')
            *p = '/';

    // Split into directory + filename pattern
    char dirPart[MaxFileName];
    char filePart[MaxFileName];
    char* lastSlash = strrchr(work, '/');
    if (lastSlash)
    {
        size_t dirLen = (size_t)(lastSlash - work);
        snprintf(dirPart, sizeof(dirPart), "%.*s", (int)dirLen, work);
        snprintf(filePart, sizeof(filePart), "%s", lastSlash + 1);
    }
    else
    {
        snprintf(dirPart, sizeof(dirPart), ".");
        snprintf(filePart, sizeof(filePart), "%s", work);
    }

    // Resolve directory path with CI fallback
    char resolved[MaxFileName];
    if (!ci_resolve_path(dirPart, resolved, sizeof(resolved)))
        return -1;

    DIR* dir = opendir(resolved);
    if (!dir)
        return -1;

    auto* fh = new FindHandle;
    snprintf(fh->dirPath, sizeof(fh->dirPath), "%s", resolved);
    // Windows treats *.* as "match everything" — replicate that behavior
    if (strcmp(filePart, "*.*") == 0)
        snprintf(fh->pattern, sizeof(fh->pattern), "*");
    else
        snprintf(fh->pattern, sizeof(fh->pattern), "%s", filePart);
    fh->dir = dir;

    // Find first matching entry
    if (_findnext((intptr_t)fh, info) == 0)
        return (intptr_t)fh;

    closedir(dir);
    delete fh;
    return -1;
}

int _findnext(intptr_t handle, _finddata_t* info)
{
    if (handle == -1 || !info)
        return -1;
    auto* fh = (FindHandle*)handle;
    if (!fh->dir)
        return -1;

    struct dirent* entry;
    while ((entry = readdir(fh->dir)))
    {
        if (entry->d_name[0] == '.' && (!entry->d_name[1] || (entry->d_name[1] == '.' && !entry->d_name[2])))
            continue;

        // Case-insensitive glob match
        if (fnmatch(fh->pattern, entry->d_name, FNM_CASEFOLD) != 0)
            continue;

        snprintf(info->name, sizeof(info->name), "%s", entry->d_name);
        info->attrib = 0;
        info->size = 0;

        // Determine attributes and size
        char fullPath[MaxFileName];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", fh->dirPath, entry->d_name);
        struct stat st;
        if (stat(fullPath, &st) == 0)
        {
            if (S_ISDIR(st.st_mode))
                info->attrib |= _A_SUBDIR;
            info->size = st.st_size;
        }

        return 0;
    }
    return -1;
}

int _findclose(intptr_t handle)
{
    if (handle == -1)
        return -1;
    auto* fh = (FindHandle*)handle;
    if (fh->dir)
        closedir(fh->dir);
    delete fh;
    return 0;
}

// File time helpers declared in platform.hpp (global namespace)
#include <sys/stat.h>
#include <ctime>

time_t getFileModTime(const char* path)
{
    if (!path || !path[0])
        return 0;
    LocalPath(fn, path);
    struct stat st;
    if (stat(fn, &st) != 0)
        return 0;
    return st.st_mtime;
}

bool getFileLocalTime(const char* path, int& day, int& month, int& year, int& hour, int& minute)
{
    time_t mt = getFileModTime(path);
    if (mt == 0)
        return false;
    struct tm local;
    if (!localtime_r(&mt, &local))
        return false;
    day = local.tm_mday;
    month = local.tm_mon + 1;
    year = local.tm_year + 1900;
    hour = local.tm_hour;
    minute = local.tm_min;
    return true;
}

#endif

// Cross-platform file time helpers
#ifdef _WIN32
#include <sys/types.h>
#include <sys/stat.h>
#include <ctime>

time_t getFileModTime(const char* path)
{
    if (!path || !path[0])
        return 0;
    struct _stat st;
    if (_stat(path, &st) != 0)
        return 0;
    return st.st_mtime;
}

bool getFileLocalTime(const char* path, int& day, int& month, int& year, int& hour, int& minute)
{
    time_t mt = getFileModTime(path);
    if (mt == 0)
        return false;
    struct tm local;
    if (localtime_s(&local, &mt) != 0)
        return false;
    day = local.tm_mday;
    month = local.tm_mon + 1;
    year = local.tm_year + 1900;
    hour = local.tm_hour;
    minute = local.tm_min;
    return true;
}

#endif
