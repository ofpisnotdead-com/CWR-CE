
#include <Poseidon/IO/Streams/FileMapping.hpp>
#include <stdint.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <Poseidon/Foundation/Common/FltOpts.hpp>

#if defined _WIN32

#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/IO/Filesystem/FileOps.hpp>

namespace Poseidon
{
void FileBufferMapped::Open(HANDLE fileHandle, int start = 0, int size = INT_MAX)
{
    int fileSize = ::GetFileSize(fileHandle, nullptr);

    saturate(start, 0, fileSize);
    saturate(size, 0, fileSize - start);

    _mapHandle = ::CreateFileMapping(fileHandle, nullptr, PAGE_READONLY, 0, 0, // all file
                                     nullptr);
    if (_mapHandle)
    {
        _view = ::MapViewOfFile(_mapHandle, FILE_MAP_READ, 0, start, size);
        _size = size;
    }
}

FileBufferMapped::FileBufferMapped(HANDLE file, int start, int size)
{
    _size = 0;
    _mapHandle = nullptr;
    _view = nullptr;
    _fileHandle = nullptr;
    if (size <= 0)
    {
        return; // zero sized - no data
    }

    Open(file, start, size);
}

FileBufferMapped::FileBufferMapped(const char* name, int start, int size)
{
    _size = 0;
    _mapHandle = nullptr;
    _view = nullptr;
    _fileHandle = nullptr;
    if (size <= 0)
    {
        return; // zero sized - no data
    }
    _fileHandle = OpenFileForRead(name);
    if (_fileHandle == INVALID_HANDLE_VALUE)
    {
        _fileHandle = nullptr;
    }
    else
    {
        Open(_fileHandle, start, size);
    }
}

FileBufferMapped::~FileBufferMapped()
{
    if (_view)
    {
        ::UnmapViewOfFile(_view), _view = nullptr;
    }
    if (_fileHandle)
    {
        ::CloseHandle(_fileHandle), _fileHandle = nullptr;
    }
    if (_mapHandle)
    {
        ::CloseHandle(_mapHandle), _mapHandle = nullptr;
    }
}

bool FileBufferMapped::GetError() const
{
    // no view opened means mapping failed
    return _mapHandle == nullptr || _view == nullptr;
}

bool FileBufferMapped::IsFromBank(QFBank* bank) const
{
    return bank->BufferOwned(this);
}

bool FileBufferMapped::IsReady() const
{
    return true;
}

HANDLE FileBufferMapped::GetFileHandle() const
{
    return _fileHandle;
}

#else

// POSIX implementation:
#include <sys/mman.h>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/IO/Filesystem/FileOps.hpp>

namespace Poseidon
{

void FileBufferMapped::Open(HANDLE fileHandle, int start, int size)
{
    int fileSize = FileSize(fileHandle);

    saturate(start, 0, fileSize);
    saturate(size, 0, fileSize - start);

    _view = mmap(0, size, PROT_READ, MAP_PRIVATE, fileHandle, start);
    if (_view == MAP_FAILED)
        _view = nullptr;
    else
    {
        _size = size;
#ifdef LOG_FILEMAP
        LOG_DEBUG(Core, "OK mapping: start={}, size={}", start, size);
#endif
    }
}

FileBufferMapped::FileBufferMapped(HANDLE file, int start, int size)
{
    _size = 0;
    _view = nullptr;
    _fileHandle = 0;
    if (size <= 0)
        return; // zero sized - no data

    Open(file, start, size);
}

FileBufferMapped::FileBufferMapped(const char* name, int start, int size)
{
    _size = 0;
    _view = nullptr;
    _fileHandle = 0;
    if (size <= 0)
        return; // zero sized - no data
    HANDLE h = OpenFileForRead(name);
    if (h == INVALID_HANDLE_VALUE)
        return;
    _fileHandle = (int)(intptr_t)h;
#ifdef LOG_FILEMAP
    LOG_DEBUG(Core, "Mapping: '{}'", name);
#endif
    Open(_fileHandle, start, size);
}

FileBufferMapped::~FileBufferMapped()
{
    if (_view)
        munmap(_view, _size), _size = 0, _view = nullptr;
    if (_fileHandle)
        close(_fileHandle), _fileHandle = 0;
}

bool FileBufferMapped::GetError() const
{
    return (_view == nullptr);
}

bool FileBufferMapped::IsFromBank(QFBank* bank) const
{
    return bank->BufferOwned(this);
}

bool FileBufferMapped::IsReady() const
{
    return true;
}

HANDLE FileBufferMapped::GetFileHandle() const
{
    return _fileHandle;
}

#endif

} // namespace Poseidon
