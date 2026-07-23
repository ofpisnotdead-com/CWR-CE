
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdint.h>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/platform.hpp>

#ifdef _WIN32
#include <io.h>
#include <stdio.h>
#define USE_MAPPING 1
#else
#include <stdio.h>
#include <unistd.h>
#define POSIX_FILES 1
#ifdef NO_MMAP
#define USE_MAPPING 0
#else
#define USE_MAPPING 1
#endif
#endif

#include <Poseidon/IO/Streams/QStream.hpp>
#include <Poseidon/IO/Streams/FileAccessPolicy.hpp>
#include <Poseidon/IO/Filesystem/FileOps.hpp>

#ifdef POSIX_FILES
#define NO_FILE(file) (file < 0)
#define NO_FILE_SET -1
#else
#define NO_FILE(file) (file == nullptr)
#define NO_FILE_SET nullptr
#endif

namespace Poseidon
{
void QIStream::Close()
{
    _buf = nullptr;
    _len = 0;
    _readFrom = 0;
    _fail = true, _eof = false;
}

void QIStream::Assign(const QIStream& src)
{
    _buf = src._buf;
    _len = src._len;
    _readFrom = src._readFrom;
    _fail = src._fail, _eof = src._eof;
}

bool QIStream::nextLine()
{
    int c1;
    while (!eof())
    {
        if ((c1 = get()) == 0x0D || c1 == 0x0A)
        {
            if (!eof() && c1 == 0x0D)
            {
                int c2 = get();
                if (c2 != 0x0A)
                {
                    unget();
                }
            }
            return true;
        }
    }
    return false;
}

bool QIStream::readLine(char* buf, int bufLen)
{
    PoseidonAssert(buf);
    int left = bufLen - 1; // regular chars to read
    int c1;
    while (!eof())
    {
        if ((c1 = get()) == 0x0D || c1 == 0x0A)
        {
            // EOLN:
            if (!eof() && c1 == 0x0D)
            {
                int c2 = get();
                if (c2 != 0x0A)
                {
                    unget();
                }
            }
            *buf = (char)0;
            return true;
        }
        // regular char:
        if (bufLen > 0 && left == 0)
        { // buffer overflow
            *buf = (char)0;
            return nextLine();
        }
        *buf++ = c1;
        left--;
    }
    *buf = (char)0;
    return false; // EOF reached before EOLN
}

void QIFStream::DoConstruct(const QIFStream& from)
{
    _sharedData = from._sharedData;
    QIStream::Assign(from);
}

#ifndef _WIN32

int FileSize(int handle)
{
    struct stat buf;
    fstat(handle, &buf);
    return buf.st_size;
}

#endif

#include <Poseidon/IO/Streams/FileMapping.hpp>

#ifndef POSIX_FILES
#include <Poseidon/IO/Streams/FileCompress.hpp>

#endif

FileBufferLoaded::FileBufferLoaded(const char* name)
{
#ifdef POSIX_FILES
    HANDLE file = OpenFileForRead(name);
    if (file != INVALID_HANDLE_VALUE)
    {
        int size = GetOpenFileSize(file);
        if (size != -1)
        {
            _data.Init(size);
            int sizeRead = ::read((int)(intptr_t)file, _data.Data(), size);
            if (sizeRead != size)
            {
                _data.Delete();
                Foundation::WarningMessage("File '%s' read error", name);
            }
        }
        CloseHandle(file);
    }
#else
    HANDLE file = ::CreateFile(name, GENERIC_READ, FILE_SHARE_READ,
                               nullptr, // security
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                               nullptr // template
    );
    if (file != INVALID_HANDLE_VALUE)
    {
        DWORD size = ::GetFileSize(file, nullptr);
        if (size > 0 && size != 0xffffffff)
        {
            _data.Init(size);
            DWORD sizeRead;
            ::ReadFile(file, _data.Data(), size, &sizeRead, nullptr);
            if (sizeRead != size)
            {
                _data.Delete();
                Foundation::WarningMessage("File '%s' read error", name);
            }
        }
        ::CloseHandle(file);
    }
#endif
}

FileBufferLoaded::~FileBufferLoaded()
{
    _data.Delete();
}

void QIFStream::open(const char* name)
{
    _fail = true;
    _error = LSUnknownError;
    _sharedData = QFileAccess::OpenFileBufferAuto(name);
    if (!_sharedData->GetError())
    {
        QIStream::init(_sharedData->GetData(), _sharedData->GetSize());
    }
}

void QIFStream::OpenBuffer(Ref<IFileBuffer> buffer)
{
    _error = LSUnknownError;
    _sharedData = buffer;
    QIStream::init(_sharedData->GetData(), _sharedData->GetSize());
}

void QIFStream::DoDestruct()
{
    _sharedData.Free();
    _len = _readFrom = 0;
}

QIFStream::~QIFStream()
{
    DoDestruct();
}

static inline char normSlash(char c)
{
    return (c == '\\') ? '/' : c;
}

int CmpStartStr(const char* str, const char* start)
{
    while (*start)
    {
        if (normSlash(myLower(*str++)) != normSlash(myLower(*start++)))
        {
            return 1;
        }
    }
    return 0;
}

bool QIFStream::FileReadOnly(const char* name)
{
#ifdef POSIX_FILES
    LocalPath(fn, name);
    struct stat st;
    if (stat(fn, &st))
        return false;
    return ((st.st_mode & S_IWUSR) == 0);
#else
    DWORD attrib = ::GetFileAttributes(name);
    // check for cases where write would fail
    if (attrib & FILE_ATTRIBUTE_READONLY)
    {
        return true;
    }
    if (attrib & FILE_ATTRIBUTE_DIRECTORY)
    {
        return true;
    }
    if (attrib & FILE_ATTRIBUTE_SYSTEM)
    {
        return true;
    }
    return false; // no file
#endif
}

bool QIFStream::FileExists(const char* name)
{
#ifdef POSIX_FILES
    return FilePathExists(name);
#else
    HANDLE check = ::CreateFile(name, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (check == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    ::CloseHandle(check);
#endif
    return true;
}

#define WIN_DIR '\\'
#define UNIX_DIR '/'

#if __GNUC__
#define INVAL_DIR WIN_DIR
#define VAL_DIR UNIX_DIR
#else
#define INVAL_DIR UNIX_DIR
#define VAL_DIR WIN_DIR
#endif

static RString ConvertFileName(const char* name, int inval, int valid)
{
    if (!strchr(name, inval))
    {
        return name; // no conversion required
    }
    char cname[512];
    snprintf(cname, sizeof(cname), "%s", (const char*)name);
    for (char* cc = cname; *cc; cc++)
    {
        if (*cc == inval)
        {
            *cc = valid;
        }
    }
    return cname;
}

inline RString PlatformFileName(const char* name)
{
    return ConvertFileName(name, INVAL_DIR, VAL_DIR);
}

inline RString UniversalFileName(const char* name)
{ // filenames are normally stored with backslash '\\'
    return ConvertFileName(name, UNIX_DIR, WIN_DIR);
}

void QOFStream::open(const char* file)
{
    _file = PlatformFileName(file);
    _fail = false;
    _error = LSUnknownError;
    rewind();
}

#ifndef POSIX_FILES
static LSError LSErrorCode(DWORD eCode)
{
    switch (eCode)
    {
        case ERROR_HANDLE_DISK_FULL:
            return LSDiskFull;
        case ERROR_NETWORK_ACCESS_DENIED:
        case ERROR_LOCK_VIOLATION:
        case ERROR_SHARING_VIOLATION:
        case ERROR_WRITE_PROTECT:
        case ERROR_ACCESS_DENIED:
            return LSAccessDenied;
        case ERROR_FILE_NOT_FOUND:
            return LSFileNotFound;
        case ERROR_READ_FAULT:
        case ERROR_WRITE_FAULT:
        case ERROR_CRC:
            return LSDiskError;
        default:
        {
            char buffer[256];
            FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM,
                          nullptr, // source
                          eCode,   // requested message identifier
                          0,       // languague
                          buffer, sizeof(buffer), nullptr);
            Log("Unknown error %d %s", eCode, buffer);
        }
            return LSUnknownError;
    }
}

#endif

void QIFStream::import(const char* name)
{
    open(name);
    // Note: import from the Windows clipboard
}

// Constant-init nullptr (SIOF-safe); DefaultClipboardFunctions() falls back to a
// no-op base instance until a real implementation registers.
ClipboardFunctions* QOFStream::_defaultClipboardFunctions = nullptr;

void QOFStream::export_clip(const char* name)
{
#ifndef _WIN32
    LocalPath(fn, name);
    int handle = ::open(fn, O_CREAT | O_APPEND | O_WRONLY, S_IREAD | S_IWRITE);
#else
    int handle = ::open(name, O_CREAT | O_APPEND | O_BINARY | O_WRONLY, S_IREAD | S_IWRITE);
#endif
    if (handle >= 0)
    {
        ::write(handle, _buffer.Data(), _buffer.Size());
        ::close(handle);
    }

    DefaultClipboardFunctions()->Copy(_buffer.Data(), _buffer.Size());
}

void QOFStream::close(const void* header, int headerSize)
{
    if (!_file)
    {
        return; // no file
    }
    _error = LSOK;
#ifdef POSIX_FILES
#ifndef _WIN32
    LocalPath(fn, (const char*)_file);
    int file = ::open(fn, O_CREAT | O_WRONLY | O_TRUNC, S_IREAD | S_IWRITE);
#else
    int file = ::open(_file, O_CREAT | O_BINARY | O_WRONLY | O_TRUNC, S_IREAD | S_IWRITE);
#endif
    if (file >= 0)
    {
        if (header && headerSize > 0)
        {
            int sizeWritten = ::write(file, header, headerSize);
            if (sizeWritten != headerSize)
            {
                _fail = true;
                goto Error;
            }
        }
        {
            int sizeWritten = ::write(file, _buffer.Data(), _buffer.Size());
            if (sizeWritten != _buffer.Size())
            {
                _fail = true;
                goto Error;
            }
        }
    Error:
        int success = ::close(file);
        if (success < 0)
            _fail = true;
        if (_fail)
        {
            _error = LSUnknownError;
        }
    }
    else
    {
        // open failed
        _fail = true;
        _error = LSFileNotFound;
    }
#else
    DWORD eCode = 0;
    HANDLE file = OpenFileForWrite(_file, true);
    if (file != INVALID_HANDLE_VALUE)
    {
        DWORD sizeWritten;
        ::WriteFile(file, header, headerSize, &sizeWritten, nullptr);
        if ((int)sizeWritten != headerSize)
        {
            _fail = true;
            goto Error;
        }
        {
            const char* data = _buffer.Data();
            int size = _buffer.Size();
            // Write in 1 MB chunks rather than one huge WriteFile.
            static const int writeAtOnce = 1024 * 1024;
            while (size > writeAtOnce)
            {
                ::WriteFile(file, data, writeAtOnce, &sizeWritten, nullptr);
                if ((int)sizeWritten != writeAtOnce)
                {
                    _fail = true;
                    goto Error;
                }
                data += writeAtOnce;
                size -= writeAtOnce;
            }
            ::WriteFile(file, data, size, &sizeWritten, nullptr);
            if ((int)sizeWritten != size)
            {
                _fail = true;
                goto Error;
            }
        }
    Error:
        DWORD eCode = 0;
        if (_fail)
        {
            eCode = ::GetLastError();
        }
        BOOL success = ::CloseHandle(file);
        if (!success)
        {
            if (eCode == 0)
            {
                eCode = ::GetLastError();
            }
            _fail = true;
        }
    }
    else
    {
        eCode = ::GetLastError();
        _fail = true;
    }
    if (eCode)
    {
        _error = LSErrorCode(eCode);
    }
#endif
    rewind();
    _file = nullptr;
}

void QOFStream::close()
{
    close(nullptr, 0);
}

QOFStream::~QOFStream() {}

} // namespace Poseidon
