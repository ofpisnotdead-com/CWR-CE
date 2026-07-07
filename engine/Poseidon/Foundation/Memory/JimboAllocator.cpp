#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Foundation/Memory/JimboAllocator.hpp>
#include <Poseidon/Foundation/Memory/MemHeap.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Common/Win.h>
#include <stdlib.h>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Memory/CheckMem.hpp>
#include <Poseidon/Foundation/Memory/MemFreeReq.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>
#ifndef _WIN32
#include <unistd.h>
#endif

#pragma warning(disable : 4074)
#pragma init_seg(compiler)

// Static instances for global use - must be created in compiler init segment
// to ensure they exist before any other static constructors run
namespace Poseidon::Foundation
{
#ifdef _WIN32
static JimboAllocator GJimboAllocatorInstance;
static JimboAllocatorLocked GSafeJimboAllocatorInstance;
#else
// On Linux: use init_priority(101) to ensure early initialization
// before any user-defined static constructors (default priority ~65535)
static JimboAllocator GJimboAllocatorInstance __attribute__((init_priority(101)));
static JimboAllocatorLocked GSafeJimboAllocatorInstance __attribute__((init_priority(102)));
#endif

MemFunctions* GMemFunctionsPtr = &GJimboAllocatorInstance;
MemFunctions* GSafeMemFunctionsPtr = &GSafeJimboAllocatorInstance;

JimboAllocator::JimboAllocator() : _heap(nullptr), _allocCount(0), _outOfMemory(false)
{
    // Use default heap which is thread-safe via thread-local storage
    // IMPORTANT: mi_heap_new() creates a thread-specific heap that cannot be
    // shared across threads. The default heap internally uses thread-local
    // heaps and is safe for multi-threaded use.
#if MI_MALLOC_VERSION >= 30000
    // mimalloc 3 split the thread-default-heap accessor into a separate
    // mi_theap_t* type; mi_heap_collect/etc. still accept nullptr to mean
    // "current thread's default heap", so we leave _heap nullptr here.
    _heap = nullptr;
#else
    _heap = mi_heap_get_default();
#endif
}

JimboAllocator::~JimboAllocator()
{
    // Never destroy the default heap
    _heap = nullptr;
}

void* JimboAllocator::New(size_t size)
{
    if (size == 0)
    {
        return nullptr;
    }

    // Sanity check - reject absurdly large allocations
    if (size >= kMaxSingleAllocation)
    {
        LOG_DEBUG(Memory, "JimboAllocator: Rejecting allocation of %zu bytes (too large)", size);
        return nullptr;
    }

    // Process-wide budget accounting. With no hard ceiling (the default) this is
    // one atomic add. With a ceiling, Reserve evicts caches via FreeOnDemand when
    // over budget but always admits — a cap that returned null would crash the
    // engine's many allocation sites that assume `new` never fails.
    _budget.Reserve(size);

    // Allocate with 16-byte alignment (required for SSE and x64 vtables).
    // Zero on alloc — callers rely on `new` returning zeroed memory.
    // Use mi_zalloc_aligned (not mi_heap_zalloc_aligned) so mimalloc routes
    // through thread-local heaps automatically for thread safety.
    void* mem = mi_zalloc_aligned(size, 16);

    if (!mem)
    {
        // Memory allocation failed - try FreeOnDemand
        size_t freed = FreeOnDemand(size);
        if (freed > 0)
        {
            mem = mi_zalloc_aligned(size, 16);
        }
    }

    if (!mem)
    {
        _budget.Release(size); // hand the reservation back — the alloc failed
        _outOfMemory = true;
        LOG_DEBUG(Memory, "JimboAllocator: Out of memory allocating %zu bytes", size);
        return nullptr;
    }

    // Reconcile the reservation (request size) with mimalloc's real usable size.
    _budget.Reconcile(size, mi_usable_size(mem));
    _allocCount++;
    _outOfMemory = false;

    return mem;
}

void JimboAllocator::Delete(void* mem)
{
    if (!mem)
    {
        return;
    }

    if (!mi_is_in_heap_region(mem))
    {
        free(mem);
        return;
    }

    _budget.Release(mi_usable_size(mem));
    if (_allocCount > 0)
    {
        _allocCount--;
    }

    mi_free(mem);
}

void* JimboAllocator::HeapBase()
{
    // mimalloc doesn't expose a single base pointer
    return nullptr;
}

size_t JimboAllocator::HeapUsed()
{
    return _budget.Used();
}

int JimboAllocator::FreeBlocks()
{
    // mimalloc manages fragmentation internally
    // Return 1 to indicate healthy state
    return 1;
}

int JimboAllocator::MemoryAllocatedBlocks()
{
    return static_cast<int>(_allocCount.load());
}

bool JimboAllocator::CheckIntegrity()
{
#if MI_MALLOC_VERSION >= 30000
    // mimalloc 3 removed mi_heap_check_owned; mi_check_owned(p) is the
    // replacement but takes a pointer, not a heap. The original code
    // unconditionally returned true anyway, so just match that.
    return true;
#else
    return mi_heap_check_owned(_heap, nullptr) || true; // mimalloc always valid
#endif
}

bool JimboAllocator::IsOutOfMemory()
{
    return _outOfMemory.load();
}

void JimboAllocator::CleanUp()
{
    // Collect any cached memory that mimalloc is holding
#if MI_MALLOC_VERSION >= 30000
    // _heap is nullptr on v3; mi_collect() targets the calling thread's heap.
    mi_collect(true);
#else
    mi_heap_collect(_heap, true);
#endif
}

void JimboAllocator::RegisterFreeOnDemand(IMemoryFreeOnDemand* object)
{
    _freeOnDemand.Register(object);
}

size_t JimboAllocator::FreeOnDemand(size_t size)
{
    return _freeOnDemand.Free(size);
}

size_t JimboAllocator::FreeOnDemandAll()
{
    return _freeOnDemand.FreeAll();
}

size_t JimboAllocator::FrameMaintenance()
{
    const size_t soft = _budget.SoftLimit();
    const size_t hard = _budget.HardLimit();
    const size_t used = _budget.Used();

    const bool overSoft = soft != 0 && used > soft;
    const bool overHard = hard != 0 && used > hard;
    if (!overSoft && !overHard)
    {
        return 0; // common case: under both watermarks — a few atomic loads, done
    }

    // Over soft: trim each domain back to its own declared budget (gentle, cheap).
    size_t freed = _freeOnDemand.EnforceBudgets();

    // Over hard: claw the remaining overflow back with cost-ordered FreeOnDemand
    // eviction (cheapest-to-recreate caches first). Batched here, once per frame —
    // never per allocation (that was the over-budget stutter). Eviction lowers
    // _budget via the freed objects' Delete()/Release(), so re-read Used().
    if (hard != 0)
    {
        const size_t now = _budget.Used();
        if (now > hard)
        {
            freed += _freeOnDemand.Free(now - hard);
        }
    }
    return freed;
}

// JimboAllocatorLocked - thread-safe wrapper
// Note: mimalloc is already thread-safe, this is for API compatibility

#ifdef _WIN32
static CRITICAL_SECTION& GetAllocatorCS()
{
    static CRITICAL_SECTION cs;
    static bool initialized = false;
    if (!initialized)
    {
        InitializeCriticalSection(&cs);
        initialized = true;
    }
    return cs;
}
#define ALLOC_LOCK() EnterCriticalSection(&GetAllocatorCS())
#define ALLOC_UNLOCK() LeaveCriticalSection(&GetAllocatorCS())
#else
#include <mutex>
static std::mutex& GetAllocatorMutex()
{
    static std::mutex m;
    return m;
}
#define ALLOC_LOCK() GetAllocatorMutex().lock()
#define ALLOC_UNLOCK() GetAllocatorMutex().unlock()
#endif

void* JimboAllocatorLocked::New(size_t size)
{
    ALLOC_LOCK();
    void* result = JimboAllocator::New(size);
    ALLOC_UNLOCK();
    return result;
}

void JimboAllocatorLocked::Delete(void* mem)
{
    ALLOC_LOCK();
    JimboAllocator::Delete(mem);
    ALLOC_UNLOCK();
}

void JimboAllocatorLocked::CleanUp()
{
    ALLOC_LOCK();
    JimboAllocator::CleanUp();
    ALLOC_UNLOCK();
}

// Global registration function used throughout the codebase
void RegisterMemoryFreeOnDemand(IMemoryFreeOnDemand* object)
{
    // Register with the main allocator instance
    GJimboAllocatorInstance.RegisterFreeOnDemand(object);
}

// Free memory on demand - called when system is under memory pressure
size_t MemoryFreeOnDemand(size_t size)
{
    return GJimboAllocatorInstance.FreeOnDemand(size);
}

void SetProcessMemoryLimits(size_t softBytes, size_t hardBytes)
{
    // Apply to both the general heap (global new) and the MT-safe heap so the
    // process budget covers every path through the allocator.
    GJimboAllocatorInstance.SetBudgetLimits(softBytes, hardBytes);
    GSafeJimboAllocatorInstance.SetBudgetLimits(softBytes, hardBytes);
}

ProcessMemoryStats MemoryProcessStats()
{
    ProcessMemoryStats s;
    s.used = GJimboAllocatorInstance.HeapUsed();
    s.peak = GJimboAllocatorInstance.PeakUsed();
    s.softLimit = GJimboAllocatorInstance.SoftLimit();
    s.hardLimit = GJimboAllocatorInstance.HardLimit();
    MemoryDomainStat scratch[32];
    s.registrants = GJimboAllocatorInstance.SnapshotDomains(scratch, 32);
    return s;
}

int MemorySnapshotDomains(MemoryDomainStat* out, int maxOut)
{
    return GJimboAllocatorInstance.SnapshotDomains(out, maxOut);
}

size_t MemoryEnforceBudgets()
{
    return GJimboAllocatorInstance.EnforceBudgets();
}

size_t MemoryFrameMaintenance()
{
    // Domains register on the main instance (RegisterMemoryFreeOnDemand); the
    // MT-safe heap shares the limits but holds no registry, so this is the one
    // that matters.
    return GJimboAllocatorInstance.FrameMaintenance();
}

size_t QueryPhysicalMemoryBytes()
{
#ifdef _WIN32
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status))
    {
        return static_cast<size_t>(status.ullTotalPhys);
    }
    return 0;
#else
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pages > 0 && pageSize > 0)
    {
        return static_cast<size_t>(pages) * static_cast<size_t>(pageSize);
    }
    return 0;
#endif
}

} // namespace Poseidon::Foundation

// Global-scope symbols referenced by other TUs (declared in AppFrame.hpp / InitBridge.hpp).

#include <Poseidon/Foundation/Platform/AppFrameExt.hpp>

// AppFrameFunctions - logging infrastructure
static CWRFrameFunctions GCWRFrameFunctions INIT_PRIORITY_URGENT;
// selectany: tests can provide a stronger (non-selectany) override
#ifdef _MSC_VER
__declspec(selectany)
#endif
Poseidon::Foundation::AppFrameFunctions* Poseidon::Foundation::CurrentAppFrameFunctions = &GCWRFrameFunctions;

// Guard against allocations before main() - helps detect SIOF issues
static bool g_MemorySystemReady = false;

void SetMemorySystemReady(bool ready)
{
    g_MemorySystemReady = ready;
}

// Memory cleanup - called when switching levels etc.
void MemoryCleanUp()
{
    Poseidon::Foundation::GJimboAllocatorInstance.CleanUp();
    Poseidon::Foundation::GSafeJimboAllocatorInstance.CleanUp();
}

namespace Poseidon::Foundation
{
// safeHeap stub: server stats read it; the real numbers come from JimboAllocatorLocked.
RefD<MemHeapLocked> safeHeap INIT_PRIORITY_URGENT;

// MemHeap stubs: main.cpp calls these on safeHeap.
size_t MemHeap::MaxFreeLeft() const
{
    return 0;
}
size_t MemHeap::TotalFreeLeft() const
{
    return 0;
}
int MemHeap::CountFreeLeft() const
{
    return 0;
}
size_t MemHeap::TotalAllocated() const
{
    return GSafeJimboAllocatorInstance.HeapUsed();
}
void MemHeap::LogAllocStats() const
{
    LOG_DEBUG(Memory, "JimboAllocator: %zu bytes allocated", GSafeJimboAllocatorInstance.HeapUsed());
}
} // namespace Poseidon::Foundation
