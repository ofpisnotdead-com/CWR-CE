
#include <Poseidon/Foundation/Memory/MemGrow.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Foundation/Common/Win.h>
#if defined(__linux__)
#include <linux/sysinfo.h>
#include <sys/sysinfo.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h> // vm.swapusage -- macOS has no sysinfo()/struct sysinfo
#endif
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#ifndef _WIN32
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace Poseidon::Foundation
{
void MemGrow::DoConstruct()
{
    _data = nullptr;
    _reserved = 0;
    _commited = 0;
    _error = false;

#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    _pageSize = info.dwPageSize;
#else
    _pageSize = (size_t)sysconf(_SC_PAGESIZE);
#endif

    // Minimal grow - avoid resizing swap file many times with 4KB increments
    const size_t MIN_GROW = 256 * 1024;
    if (_pageSize < MIN_GROW)
    {
        _pageSize = MIN_GROW;
    }

    // Round up to power of two
    size_t pageSizeLog = 0;
    while ((static_cast<size_t>(1) << pageSizeLog) < _pageSize)
    {
        pageSizeLog++;
    }
    _pageSize = (static_cast<size_t>(1) << pageSizeLog);
}

void MemGrow::DoConstruct(size_t size)
{
    DoConstruct();
    Reserve(size);
}

void MemGrow::DoDestruct()
{
    if (_data)
    {
#ifdef _WIN32
        ::VirtualFree(_data, _commited, MEM_DECOMMIT);
        ::VirtualFree(_data, 0, MEM_RELEASE);
#else
        munmap(_data, _reserved);
#endif
        _data = nullptr;
        _commited = _reserved = 0;
    }
}

void MemoryErrorReported();

void MemGrow::Reserve(size_t size)
{
    DoDestruct();
    size += _pageSize - 1;
    size &= ~(_pageSize - 1);

#ifdef _WIN32
    _data = ::VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_READWRITE);
#else
    _data = mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (_data == MAP_FAILED)
        _data = nullptr;
#endif
    if (!_data)
    {
        ErrorMessage("Cannot reserve virtual memory (%llu MB).", static_cast<unsigned long long>(size / (1024 * 1024)));
    }
    else
    {
        _reserved = size;
    }
}

inline size_t ConvertToMB(size_t x)
{
    const size_t oneMB = 1024 * 1024;
    return (x + oneMB - 1) / oneMB;
}

bool MemGrow::Commit(size_t size)
{
    size += _pageSize - 1;
    size &= ~(_pageSize - 1);

    if (_commited < size)
    {
        if (size > _reserved)
        {
            _error = true;
            MemoryErrorReported();
            ErrorMessage("Cannot allocate more than was reserved (%llu MB)",
                         static_cast<unsigned long long>(_reserved / (1024 * 1024)));
            return false;
        }

#ifdef _WIN32
        bool commitOk = (::VirtualAlloc(_data, size, MEM_COMMIT, PAGE_READWRITE) == _data);
#else
        bool commitOk = (mprotect(_data, size, PROT_READ | PROT_WRITE) == 0);
#endif
        if (!commitOk)
        {
            _error = true;
            MemoryErrorReported();
#ifdef _WIN32
            MEMORYSTATUS mstat;
            mstat.dwLength = sizeof(mstat);
            GlobalMemoryStatus(&mstat);

            ErrorMessage("Cannot increase memory pool to %llu MB.\\n"
                         "Current memory pool size is %llu MB.\\n"
                         "You probably have your hard disk full.\\n"
                         "If you control your swap file size manually,\\n"
                         "consider setting it bigger.\\n\\n"
                         "Current swap-file settings:\\n"
                         "   Total:     %6llu MB\\n"
                         "   Available: %6llu MB\\n"
                         "   Used:      %6llu MB\\n",
                         static_cast<unsigned long long>(ConvertToMB(size)),
                         static_cast<unsigned long long>(ConvertToMB(_commited)),
                         static_cast<unsigned long long>(ConvertToMB(mstat.dwTotalPageFile)),
                         static_cast<unsigned long long>(ConvertToMB(mstat.dwAvailPageFile)),
                         static_cast<unsigned long long>(ConvertToMB(mstat.dwTotalPageFile - mstat.dwAvailPageFile)));
#elif defined(__linux__)
            struct sysinfo si;
            sysinfo(&si);
            ErrorMessage("Cannot increase memory pool to %llu MB.\\n"
                         "Current memory pool size is %llu MB.\\n"
                         "Total swap: %llu MB, Free swap: %llu MB",
                         static_cast<unsigned long long>(ConvertToMB(size)),
                         static_cast<unsigned long long>(ConvertToMB(_commited)),
                         static_cast<unsigned long long>(ConvertToMB((size_t)si.totalswap * si.mem_unit)),
                         static_cast<unsigned long long>(ConvertToMB((size_t)si.freeswap * si.mem_unit)));
#else // __APPLE__
            struct xsw_usage xsu;
            size_t xsuLen = sizeof(xsu);
            sysctlbyname("vm.swapusage", &xsu, &xsuLen, nullptr, 0);
            ErrorMessage("Cannot increase memory pool to %llu MB.\\n"
                         "Current memory pool size is %llu MB.\\n"
                         "Total swap: %llu MB, Free swap: %llu MB",
                         static_cast<unsigned long long>(ConvertToMB(size)),
                         static_cast<unsigned long long>(ConvertToMB(_commited)),
                         static_cast<unsigned long long>(ConvertToMB(xsu.xsu_total)),
                         static_cast<unsigned long long>(ConvertToMB(xsu.xsu_avail)));
#endif
            return false;
        }

        _commited = size;
    }

    return true;
}

} // namespace Poseidon::Foundation
