// Global operator new/delete redirected to the custom allocator (BMemory).
//
// IMPORTANT: this file must NOT use #pragma init_seg(compiler) — it would
// interfere with BMemory static initialization.

#include <cstdlib>
#include <Poseidon/Foundation/Memory/CheckMem.hpp>
#include <Poseidon/Foundation/platform.hpp>

// GMemFunctions() and operator declarations are in Poseidon/Foundation/Memory/CheckMem.hpp (via PoseidonPCH.hpp)

// Undefine debug new macro so we can define the actual operators
#undef new

// Under AddressSanitizer the custom heap (BMemory) hides every allocation
// behind one big malloc, so ASan cannot place redzones around individual
// objects or track per-object alloc/free provenance. Route every global
// new/delete straight to malloc/free so ASan instruments each object.
// This is sanitizer-build-only; production keeps the custom heap.
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define POSEIDON_ASAN_GLOBAL_NEW 1
#endif
#endif

#ifndef POSEIDON_ASAN_GLOBAL_NEW

// Guard: during early dynamic library init (e.g. OpenAL), GMemFunctionsPtr may
// exist but not be fully constructed (vtable still null). Fall back to malloc/free.
//
// On Apple platforms this guard is forced permanently "not ready": Apple ships
// libc++ as a shared dylib (libc++.dylib), and several basic_string members
// (e.g. __grow_by_and_replace) are kept out-of-line in that dylib for ABI
// stability rather than inlined into our binary. Code executing inside that
// dylib resolves operator delete/free to the *system* allocator, not this
// override -- so a string buffer allocated here via the custom heap
// (mimalloc-backed JimboAllocator) can end up freed by system malloc when
// libc++.dylib grows it, which system malloc correctly rejects as a pointer
// it never allocated (abort: BUG_IN_CLIENT_OF_LIBMALLOC_POINTER_BEING_FREED_
// WAS_NOT_ALLOCATED). Confirmed as the cause of a 100%-reproducible launch
// crash in GamePaths::Resolve -> getXdgDir on iOS. Windows/Linux statically
// link their C++ runtime, so no separate-image boundary exists there and the
// custom heap remains safe to use globally on those platforms.
//
// TODO(apple-alloc): this permanently gives up JimboAllocator's budget
// tracking/FreeOnDemand cache eviction on iOS/macOS, not just its speed.
// Properly restoring mimalloc here would mean registering it as a real
// malloc_zone_t (malloc_zone_register), which is a materially bigger and
// riskier piece of platform-specific work than this bypass -- not worth it
// right after finding this bug. If eviction-under-pressure is ever actually
// needed on iOS, wire FreeOnDemand to UIApplicationDidReceiveMemoryWarning-
// Notification directly instead of trying to bring mimalloc along.
#if defined(__APPLE__)
#define POSEIDON_APPLE_GLOBAL_NEW_BYPASS 1
#endif

namespace Poseidon
{
static inline bool GMemReady()
{
#if defined(POSEIDON_APPLE_GLOBAL_NEW_BYPASS)
    return false;
#else
    if (!Foundation::GMemFunctionsPtr)
        return false;
    // Check vtable pointer (first member of the object)
    const void* vtable = *reinterpret_cast<void* const*>(Foundation::GMemFunctionsPtr);
    return vtable != nullptr;
#endif
}
} // namespace Poseidon

// Global operator new/delete implementations (must be at global scope)
void* CCALL operator new(size_t size)
{
    if (!Poseidon::GMemReady())
        return malloc(size);
    return Poseidon::Foundation::GMemFunctions()->New(size);
}

void* CCALL operator new[](size_t size)
{
    if (!Poseidon::GMemReady())
        return malloc(size);
    return Poseidon::Foundation::GMemFunctions()->New(size);
}

void CCALL operator delete(void* ptr) noexcept
{
    if (!Poseidon::GMemReady())
    {
        free(ptr);
        return;
    }
    Poseidon::Foundation::GMemFunctions()->Delete(ptr);
}

void CCALL operator delete[](void* ptr) noexcept
{
    if (!Poseidon::GMemReady())
    {
        free(ptr);
        return;
    }
    Poseidon::Foundation::GMemFunctions()->Delete(ptr);
}

// Sized delete operators (C++14)
void CCALL operator delete(void* ptr, size_t size) noexcept
{
    (void)size;
    if (!Poseidon::GMemReady())
    {
        free(ptr);
        return;
    }
    Poseidon::Foundation::GMemFunctions()->Delete(ptr);
}

void CCALL operator delete[](void* ptr, size_t size) noexcept
{
    (void)size;
    if (!Poseidon::GMemReady())
    {
        free(ptr);
        return;
    }
    Poseidon::Foundation::GMemFunctions()->Delete(ptr);
}

#else // POSEIDON_ASAN_GLOBAL_NEW

// Define nothing: let AddressSanitizer's own operator new/delete own every
// global allocation. They route straight through ASan's instrumented allocator
// (native redzones, per-object alloc/free provenance) — strictly better than a
// malloc passthrough — and the ASan static runtime thunk expects to intercept
// them, so a competing definition here trips lld-link ("operator new was
// replaced"). This is sanitizer-build-only; production keeps the custom heap.

#endif // POSEIDON_ASAN_GLOBAL_NEW
