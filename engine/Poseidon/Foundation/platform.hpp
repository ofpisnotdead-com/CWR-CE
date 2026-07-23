#pragma once

const int MaxFileName = 2048;

// portability settings for different compilers/platforms

#ifdef _MSC_VER

#define CCALL __cdecl

// make for variable local
#if _MSC_VER >= 1300
#if defined(__clang__)
// Clang doesn't support #pragma conform
#else
#pragma conform(forScope, on)
#endif
#else
#define for    \
    if (false) \
    {          \
    }          \
    else for
#endif

#define NO_ENUM_FORWARD_DECLARATION 0
#define NO_UNDEF_ENUM_REFERENCE 0

#define ENUM_CAST(Type, value) value

#else

#define CCALL
#define __int64 long long

#define NO_ENUM_FORWARD_DECLARATION 1
#define NO_UNDEF_ENUM_REFERENCE 0

#define ENUM_CAST(Type, value) Type(value)

#define StrToInt(x) (*(int*)x)

#define __forceinline inline

#endif

#ifdef _WIN32

#include <Poseidon/Foundation/Types/Memtype.h> // DWORD/WORD/BYTE, matching <windows.h>

#define strDup strdup
#define getpid _getpid

typedef int socklen_t;

#else

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define _REENTRANT
#define _THREAD_SAFE
#define _IO_MTSAFE_IO

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <typeinfo>

#define USE_MALLOC 1

typedef int HANDLE;
#define INVALID_HANDLE_VALUE -1
typedef int HWND;
typedef int HINSTANCE;
typedef char* LPSTR;
typedef unsigned int DWORD;
typedef size_t SIZE_T;

#define __cdecl
#define WINAPI

#ifdef __APPLE__
// macOS's libc++ doesn't expose the BSD/glibc `finite()` compat function
// (only the standard `isfinite()`); __builtin_isfinite works in both C and
// C++ translation units without requiring <cmath>/<math.h> at the call site.
#define _finite(x) __builtin_isfinite(x)
#else
#define _finite(x) finite(x)
#endif
#define _isnan(x) isnan(x)
extern char* strDup(const char* src);

#include <Poseidon/Foundation/Framework/Log.hpp>
#define OutputDebugString(s)                            \
    {                                                   \
        char dbg[1024] = "Debug: ";                     \
        strncat(dbg, s, sizeof(dbg) - strlen(dbg) - 1); \
        LOG_DEBUG(Core, "{}", dbg);                     \
    }

extern char* CCALL strlwr(char* a);
extern char* CCALL strupr(char* a);
extern int CCALL strcmpi(const char* a, const char* b);
extern int CCALL stricmp(const char* a, const char* b);
extern int CCALL strnicmp(const char* a, const char* b, int n);

// Windows CRT compatibility aliases
#define _stricmp stricmp
#define _strnicmp strnicmp

#define LocalPath(fn, fileName)         \
    char fn[MaxFileName];               \
    strncpy(fn, fileName, MaxFileName); \
    fn[MaxFileName - 1] = (char)0;      \
    unixPath(fn)

#define FILE_BEGIN SEEK_SET
#define FILE_CURRENT SEEK_CUR
#define FILE_END SEEK_END
#define GetCurrentDirectory(len, buf) getcwd(buf, len)
#define SetCurrentDirectory chdir
#define ReadFile(file, buf, len, rd, ovl) (*(rd) = ::read((int)(intptr_t)(file), buf, len), *(rd) != 0xffffffff)
#define WriteFile(file, buf, len, wr, ovl) (*(wr) = ::write((int)(intptr_t)(file), buf, len), *(wr) != 0xffffffff)
#define SetFilePointer(file, pos, x, mode) lseek((int)(intptr_t)(file), pos, mode)
#define CloseHandle(handle) ::close((int)(intptr_t)(handle))
#define CreateDirectory(dir, d) createDirectory(dir)
extern void createDirectory(const char* dir);
#define NEW_DIRECTORY_MODE (S_IREAD | S_IWRITE | S_IEXEC | S_IXGRP | S_IXOTH)
#define DeleteFile(fn) deleteFile(fn)
extern void deleteFile(const char* path);
extern void unixPath(char* path);
extern bool isSuffix(const char* str, const char* suffix);

#define Sleep(ms) ::Poseidon::Foundation::sleepMs(ms)
namespace Poseidon::Foundation
{
extern void sleepMs(unsigned ms);
extern unsigned long long getSystemTime();
}

inline unsigned long long GetTickCount()
{
    return (::Poseidon::Foundation::getSystemTime() / 1000);
}

#define _atoi64(x) atoi64(x)
extern long long atoi64(const char* str);
#define _i64toa(i, buf, radix) i64toa(i, buf, radix)
extern char* i64toa(long long i, char* buf, int radix);
extern bool fileTime(const char* fileName, long long win32Time);
#define CopyFile(src, dst, d) fileCopy(src, dst)
extern bool fileCopy(const char* src, const char* dest);
#define MoveFile(src, dst) fileMove(src, dst)
extern bool fileMove(const char* src, const char* dest);

// MSVC file permission constants → POSIX equivalents
#define _S_IREAD S_IRUSR
#define _S_IWRITE S_IWUSR

extern size_t linuxMemoryUsage();

// POSIX-compatible _findfirst/_findnext/_findclose with case-insensitive directory resolution
#include <cstdint>

#define _A_NORMAL 0x00
#define _A_RDONLY 0x01
#define _A_SUBDIR 0x10

struct _finddata_t
{
    unsigned attrib;
    unsigned long size;
    char name[MaxFileName];
};

intptr_t _findfirst(const char* pattern, _finddata_t* info);
int _findnext(intptr_t handle, _finddata_t* info);
int _findclose(intptr_t handle);

#endif

// Cross-platform file time helpers
#include <ctime>
extern time_t getFileModTime(const char* path);
extern bool getFileLocalTime(const char* path, int& day, int& month, int& year, int& hour, int& minute);

// Cross-platform path separator
#ifdef _WIN32
constexpr char PATH_SEP = '\\';
constexpr const char PATH_SEP_STR[] = "\\";
#else
constexpr char PATH_SEP = '/';
constexpr const char PATH_SEP_STR[] = "/";
#endif

/// Normalize path separators to platform convention in-place.
/// On Windows: '/' -> '\\'.  On Linux: '\\' -> '/'.
inline void platformPath(char* path)
{
    if (!path)
        return;
    for (; *path; ++path)
    {
#ifdef _WIN32
        if (*path == '/')
            *path = '\\';
#else
        if (*path == '\\')
            *path = '/';
#endif
    }
}

/// Return a copy of the string with platform-native separators.
#ifdef __cplusplus
#include <string>
inline std::string platformPath(const std::string& path)
{
    std::string out = path;
    for (char& c : out)
    {
#ifdef _WIN32
        if (c == '/')
            c = '\\';
#else
        if (c == '\\')
            c = '/';
#endif
    }
    return out;
}
#endif

// Cross-platform LocalPath: copy filename into local buffer and normalize separators
#ifdef _WIN32
#define LocalPath(fn, fileName)         \
    char fn[MaxFileName];               \
    strncpy(fn, fileName, MaxFileName); \
    fn[MaxFileName - 1] = (char)0;      \
    platformPath(fn)
#endif

#ifdef __GNUC__

#define INIT_PRIORITY_NORMAL __attribute__((init_priority(10000)))
#define INIT_PRIORITY_HIGH __attribute__((init_priority(9000)))
#define INIT_PRIORITY_URGENT __attribute__((init_priority(8000)))
#define INIT_PRIORITY(prio) __attribute__((init_priority(prio)))

#define PACKED __attribute__((packed))

#else

#define INIT_PRIORITY_NORMAL
#define INIT_PRIORITY_HIGH
#define INIT_PRIORITY_URGENT
#define INIT_PRIORITY(prio)

#define PACKED

#endif

/* __BEGIN_DECLS should be used at the beginning of your declarations,
   so that C++ compilers don't mangle their names.  Use __END_DECLS at
   the end of C declarations.
*/
#undef __BEGIN_DECLS
#undef __END_DECLS
#ifdef __cplusplus
#define __BEGIN_DECLS \
    extern "C"        \
    {
#define __END_DECLS }
#else
#define __BEGIN_DECLS /* empty */
#define __END_DECLS   /* empty */
#endif

