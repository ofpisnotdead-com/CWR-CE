#pragma once

#include <Poseidon/Foundation/platform.hpp>
#include <cstdint>
#include <Poseidon/Foundation/Containers/TypeOpts.hpp>
#include <Poseidon/Foundation/Containers/ConstructTraitsModern.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>

#if defined _CPPRTTI
#include <typeinfo>
#endif

// Smart pointer for COM (IUnknown) objects: automates AddRef/Release.
namespace Poseidon::Foundation
{
template <class Type>
class ComRef
{
  private:
    Type* _ref;

  public:
    ComRef() { _ref = nullptr; }
    ComRef(Type* ptr) { _ref = ptr; }
    // Assigns a raw pointer; does NOT AddRef (the caller owns that). Use with care.
    void operator=(Type* source)
    {
        Free();
        _ref = source;
    }
    bool NotNull() const { return _ref != nullptr; }
    bool IsNull() const { return _ref == nullptr; }
    ComRef(const ComRef& source)
    {
        _ref = source._ref;
        if (_ref)
        {
            _ref->AddRef();
        }
    }
    void operator=(const ComRef& sRef)
    {
        Type* source = sRef._ref;
        if (source)
        {
            source->AddRef();
        }
        Free();
        _ref = source;
    }
    // Releases the pointer; returns the resulting reference count.
    int Free()
    {
        int ret = 0;
        if (_ref)
        {
            ret = _ref->Release(), _ref = nullptr;
        }
        return ret;
    }
    __forceinline ~ComRef() { Free(); }
    __forceinline Type* operator->() const { return _ref; }
    __forceinline Type* GetRef() const { return _ref; }
    __forceinline operator Type*() const { return _ref; }
    // Clears the pointer and hands back its address for out-parameter init;
    // the caller is responsible for any AddRef.
    Type** Init()
    {
        Free();
        return &_ref;
    }
};

template <class Type>
class InitPtr
{
  private:
    mutable Type* _ref;

  public:
    InitPtr() { _ref = nullptr; }
    InitPtr(Type* source) { _ref = source; }
    void operator=(Type* source) { _ref = source; }

    bool NotNull() const { return _ref != nullptr; }
    bool IsNull() const { return _ref == nullptr; }
    bool operator!() const { return _ref == nullptr; }

    Type* operator->() const { return _ref; }
    operator Type*() const { return _ref; }

};

// delete for a single object
template <class Type>
struct SRefTraits
{
    static void Delete(Type* ptr) { delete ptr; }
};

// delete[] for an array
template <class Type>
struct SRefArrayTraits
{
    static void Delete(Type* ptr) { delete[] ptr; }
};

// Sole-owner pointer: one SRef owns the object and deletes it on destruction.
// Copying transfers ownership and nulls the source.
template <class Type>
class SRef
{
  protected:
    // mutable so a copy can null the source through a const reference
    mutable Type* _ref;

  public:
    __forceinline SRef() { _ref = nullptr; }
    __forceinline SRef(Type* source) { _ref = source; }
    // Assigns a new object; the previous one is deleted.
    void operator=(Type* source)
    {
        if (_ref == source)
        {
            return;
        }
        Free();
        _ref = source;
    }
    SRef(const SRef& source)
    {
        _ref = source._ref;
        source._ref = nullptr; // ownership moves to the new copy
    }
    void operator=(const SRef& source)
    {
        if (source._ref != _ref)
        {
            Free();
            _ref = source._ref;
            source._ref = nullptr; // ownership moves to the new copy
        }
    }
    __forceinline bool NotNull() const { return _ref != nullptr; }
    __forceinline bool IsNull() const { return _ref == nullptr; }
    __forceinline bool operator!() const { return _ref == nullptr; }
    __forceinline ~SRef() { Free(); }
    void Free()
    {
        if (_ref)
        {
            delete _ref, _ref = nullptr;
        }
    }
    __forceinline Type* operator->() const { return _ref; }
    __forceinline operator Type*() const { return _ref; }

};

template <class Type>
class APtr
{
  private:
    // mutable so a copy can null the source through a const reference
    mutable Type* _ref;

    void Alloc(int n)
    {
#if ALLOC_DEBUGGER && defined _CPPRTTI
        _ref = new (FileLine(typeid(Type).name()), sizeof(Type)) Type[n];
#else
        _ref = new Type[n];
#endif
    }

  public:
    APtr() { _ref = nullptr; }
    explicit APtr(int n) { Alloc(n); }

    APtr(const APtr& source)
    {
        _ref = source._ref;
        source._ref = nullptr; // old is invalidated, valid is only new copy
    }
    void operator=(const APtr& source)
    {
        if (source._ref != _ref)
        {
            Free();
            _ref = source._ref;
            source._ref = nullptr; // old is invalidated, valid is only new copy
        }
    }

    void Realloc(int n)
    {
        Free();
        Alloc(n);
    }
    __forceinline bool NotNull() const { return _ref != nullptr; }
    __forceinline bool IsNull() const { return _ref == nullptr; }
    __forceinline bool operator!() const { return _ref == nullptr; }

    ~APtr()
    {
        if (_ref)
        {
            delete[] _ref;
        }
    }
    void Free()
    {
        if (_ref)
        {
            delete[] _ref, _ref = nullptr;
        }
    }
    __forceinline operator Type*() const { return _ref; }

};

// Intrusive reference-counted base. Allocate derived objects with new and hold
// them through Ref<>; the object self-deletes when the last reference drops.
class RefCount
{
  private:
    // Reference count. Atomic: landscape shapes/textures are shared across
    // TaskPool threads during parallel terrain generation (LandCache::Fill -> GenerateSegmentInto
    // -> Shape::Compact / RegisterTexture), and a non-atomic ++/-- loses increments -> under-count
    // -> double-free (the re-mount crash class). GCC __atomic builtins are used instead of
    // std::atomic<int> to avoid pulling libatomic.so.1 as a dynamic dep (conflicts with static
    // ASan runtime on Linux). On x86-64 these 32-bit builtins are always inlined — no libatomic.
    mutable int _count;

  public:
    RefCount() { _count = 0; }
    // copy/assign do not carry the source's reference count
    RefCount(const RefCount& src)
    {
        (void)src;
        _count = 0;
    }
    void operator=(const RefCount& src)
    {
        (void)src;
        _count = 0;
    }
    virtual ~RefCount() = default;
    int AddRef() const { return __atomic_fetch_add(&_count, 1, __ATOMIC_RELAXED) + 1; }
    int Release() const
    {
        // acq_rel so the decrement that reaches 0 happens-after every other thread's release of
        // its reference — the object's writes are visible before we delete it.
        int ret = __atomic_fetch_sub(&_count, 1, __ATOMIC_ACQ_REL) - 1;
        if (ret == 0)
        {
            delete (RefCount*)this;
        }
        return ret;
    }
    int RefCounter() const { return __atomic_load_n(&_count, __ATOMIC_RELAXED); }
    // memory used by the referenced data, excluding sizeof(*this)
    virtual double GetMemoryUsed() const { return 0; }
};

// Shared smart pointer for RefCount-derived objects: AddRef on acquire,
// Release on drop.
template <class Type>
class Ref
{
  protected:
    Type* _ref;

  public:
    __forceinline Ref() { _ref = nullptr; }
    Ref(Type* source)
    {
        if (source)
        {
            source->AddRef();
        }
        _ref = source;
    }
    void operator=(Type* source)
    {
        if (source)
        {
            source->AddRef();
        }
        if (_ref)
        {
            _ref->Release();
        }
        _ref = source;
    }
    Ref(const Ref& sRef)
    {
        Type* source = sRef._ref;
        if (source)
        {
            source->AddRef();
        }
        _ref = source;
    }
    void operator=(const Ref& sRef)
    {
        Type* source = sRef._ref;
        if (source)
        {
            source->AddRef();
        }
        if (_ref)
        {
            _ref->Release();
        }
        _ref = source;
    }
    __forceinline Ref(Ref&& sRef) noexcept
    {
        _ref = sRef._ref;
        sRef._ref = nullptr;
    }
    __forceinline void operator=(Ref&& sRef) noexcept
    {
        if (this != &sRef)
        {
            if (_ref)
            {
                _ref->Release();
            }
            _ref = sRef._ref;
            sRef._ref = nullptr;
        }
    }
    __forceinline bool NotNull() const { return _ref != nullptr; }
    __forceinline bool IsNull() const { return _ref == nullptr; }
    __forceinline ~Ref() { Free(); }
    void Free()
    {
        if (_ref)
        {
            _ref->Release(), _ref = nullptr;
        }
    }
    __forceinline Type* GetRef() const { return _ref; }
    // Sets the raw pointer with no AddRef/Release; mismanaging this leaks or double-frees.
    __forceinline void SetRef(Type* ref) { _ref = ref; }
    __forceinline Type* operator->() const { return _ref; }
    __forceinline operator Type*() const { return _ref; }

    double GetMemoryUsed() const
    {
        // each owner is charged only its share of the shared object
        return double(sizeof(Type) + _ref->GetMemoryUsed()) / _ref->RefCounter();
    }
};

// addresses 0 .. MaxNull-1 are all treated as null
#define MaxNull 0x0004

#define DNull(x) (reinterpret_cast<uintptr_t>(x) < MaxNull)

// Like Ref<> but treats addresses 0..MaxNull-1 as null (DNull) and
// re-initializes the reference after destruction.
template <class Type>
class RefD
{
  protected:
    Type* _ref;

  public:
    __forceinline RefD() { _ref = nullptr; }
    RefD(Type* source)
    {
        if (!DNull(source))
        {
            source->AddRef();
        }
        _ref = source;
    }
    RefD(const RefD& sRef)
    {
        Type* source = sRef._ref;
        if (!DNull(source))
        {
            source->AddRef();
        }
        _ref = source;
    }
    // cross-construct from Ref<>
    RefD(const Ref<Type>& sRef)
    {
        Type* source = sRef.GetRef();
        if (!DNull(source))
        {
            source->AddRef();
        }
        _ref = source;
    }
    void operator=(Type* source)
    {
        if (!DNull(source))
        {
            source->AddRef();
        }
        if (!DNull(_ref))
        {
            _ref->Release();
        }
        _ref = source;
    }
    void operator=(const RefD& sRef)
    {
        Type* source = sRef._ref;
        if (!DNull(source))
        {
            source->AddRef();
        }
        if (!DNull(_ref))
        {
            _ref->Release();
        }
        _ref = source;
    }
    __forceinline bool operator==(const Type* source) const { return _ref == source; }
    __forceinline bool operator!=(const Type* source) const { return _ref != source; }
    __forceinline bool operator==(const Ref<Type>& sRef) const { return _ref == sRef.GetRef(); }
    __forceinline bool operator!=(const Ref<Type>& sRef) const { return _ref != sRef.GetRef(); }
    __forceinline bool operator==(const RefD<Type>& sRef) const { return _ref == sRef._ref; }
    __forceinline bool operator!=(const RefD<Type>& sRef) const { return _ref != sRef._ref; }
    __forceinline operator bool() const { return !DNull(_ref); }
    __forceinline bool operator!() const { return DNull(_ref); }
    __forceinline Type* operator->() const { return _ref; }
    __forceinline operator Type*() const { return _ref; }
    void Free()
    {
        if (!DNull(_ref))
        {
            _ref->Release();
            _ref = nullptr;
        }
    }
    ~RefD() { Free(); }
    __forceinline bool NotNull() const { return !DNull(_ref); }
    __forceinline bool IsNull() const { return DNull(_ref); }
    __forceinline Type* GetRef() const { return _ref; }
};

template <class Type>
class RefN
{
  public:
    // Meyers Singleton for the shared nil object — avoids SIOF.
    static RefCount& GetNil()
    {
        [[clang::no_destroy]] static RefCount _nil;
        return _nil;
    }
#define TypeNull ((Type*)&RefN<Type>::GetNil())

  private:
    Type* _ref;

  public:
    __forceinline RefN() { _ref = TypeNull; }

    RefN(Type* source)
    {
        if (!source)
        {
            // Note: avoid assigning nullptr
            source = TypeNull;
        }
        source->AddRef();
        _ref = source;
    }
    void operator=(Type* source)
    {
        if (!source)
        {
            // Note: avoid assigning nullptr
            source = TypeNull;
        }
        source->AddRef();
        _ref->Release();
        _ref = source;
    }
    RefN(const RefN& sRef)
    {
        Type* source = sRef._ref;
        source->AddRef();
        _ref = source;
    }
    void operator=(const RefN& sRef)
    {
        Type* source = sRef._ref;
        source->AddRef();
        _ref->Release();
        _ref = source;
    }

    __forceinline bool NotNull() const { return _ref != TypeNull; }
    __forceinline bool IsNull() const { return _ref == TypeNull; }

    ~RefN()
    {
        if (_ref)
            _ref->Release();
    }
    void Free() { operator=(TypeNull); }

    // note: may return TypeNull
    __forceinline Type* GetRef() const { return _ref; }
    __forceinline void SetRef(Type* ref) { _ref = ref; } // note: use with caution

    __forceinline Type* operator->() const { return _ref; }
    __forceinline operator Type*() const { return _ref; }

};

// Single-owner array holder: allocates raw storage, constructs/destructs the
// elements, and frees on destruction. Lighter-weight than AutoArray.
template <class Type>
class Temp
{
    typedef Poseidon::Foundation::ModernTraits<Type> CTraits;

  private:
    Type* _obj;
    int _n;
    // allocate space for n items without constructing them
    void DoAlloc(int n)
    {
#if ALLOC_DEBUGGER && defined _CPPRTTI
        char* mem = new (FileLine(typeid(Type).name()), sizeof(Type)) char[n * sizeof(Type)];
#else
        char* mem = new char[n * sizeof(Type)];
#endif
        _obj = (Type*)mem;
        _n = n;
    }
    // allocate and default-construct n items
    void DoConstruct(int n)
    {
        DoAlloc(n);
        CTraits::ConstructArray(_obj, _n);
    }
    // destruct the items and free the storage
    void DoDelete()
    {
        if (_obj)
        {
            CTraits::DestructArray(_obj, _n);
            delete[] (char*)_obj;
            _obj = nullptr;
        }
    }

  public:
    Temp() { _obj = nullptr, _n = 0; }
    explicit Temp(int n) { DoConstruct(n); }
    // copy-construct n items from src
    Temp(const Type* src, int n)
    {
        DoAlloc(n);
        CTraits::CopyConstruct(_obj, src, _n);
    }
    void operator=(const Temp& src)
    {
        DoDelete();
        DoAlloc(src.Size());
        CTraits::CopyConstruct(_obj, src.Data(), src.Size());
    }
    Temp(const Temp& src)
    {
        DoAlloc(src.Size());
        CTraits::CopyConstruct(_obj, src.Data(), src.Size());
    }
    // Hand over the buffer; the caller becomes responsible for deleting it.
    Type* Export()
    {
        Type* ret = _obj;
        _obj = nullptr;
        return ret;
    }
    __forceinline bool NotNull() const { return _obj != nullptr; }
    __forceinline bool IsNull() const { return _obj == nullptr; }
    __forceinline bool operator!() const { return _obj == nullptr; }
    __forceinline ~Temp() { DoDelete(); }
    __forceinline void Free() { DoDelete(); }
    __forceinline operator Type*() { return _obj; }
    __forceinline operator const Type*() const { return _obj; }
    __forceinline Type* Data() { return _obj; }
    __forceinline const Type* Data() const { return _obj; }
    __forceinline int Size() const { return _n; }
    Type& Set(int i)
    {
        AssertDebug(i >= 0 && i < _n);
        return _obj[i];
    }
    const Type& Get(int i) const
    {
        AssertDebug(i >= 0 && i < _n);
        return _obj[i];
    }
    Type& operator[](int i) { return Set(i); }
    const Type& operator[](int i) const { return Get(i); }
    // reallocate to n default-constructed items, discarding the old array
    void Realloc(int n)
    {
        DoDelete();
        DoConstruct(n);
    }
    // reallocate to n items copy-constructed from src, discarding the old array
    void Realloc(const Type* src, int n)
    {
        DoDelete();
        DoAlloc(n);
        CTraits::CopyConstruct(_obj, src, _n);
    }

};

template <class Type>
class Auto
{
    typedef Poseidon::Foundation::ModernTraits<Type> CTraits;

  private:
    Type* _obj;
    int _n;

  private: // No copy
    void operator=(const Auto& src) = delete;
    Auto(const Auto& src) = delete;

  public:
    Auto(int n, void* mem)
    {
        _obj = (Type*)mem;
        _n = n;
        CTraits::ConstructArray(_obj, _n);
    }
    ~Auto() { CTraits::DestructArray(_obj, _n); }

    operator Type*() { return _obj; }
    operator const Type*() const { return _obj; }

    Type* Data() { return _obj; }
    const Type* Data() const { return _obj; }
    int Size() const { return _n; }
};

#define AutoAuto(Type, var, n) Auto<Type> var(n, _alloca(sizeof(Type) * n))

// Owns a heap array plus its length; frees on destruction.
template <class Type>
class Buffer : public RefCount
{
    typedef Poseidon::Foundation::ModernTraits<Type> CTraits;

  private:
    int _length;
    Type* _buffer;

  protected:
    void DoConstruct() { _buffer = nullptr, _length = 0; }
    void DoConstruct(int i)
    {
#if ALLOC_DEBUGGER && defined _CPPRTTI
        _buffer = new (FileLine(typeid(Type).name()), sizeof(Type)) Type[i];
#else
        _buffer = new Type[i];
#endif
        _length = i;
    }

    void DoConstruct(const Type* src, int len)
    {
        DoConstruct(len);
        for (int i = 0; i < len; i++)
        {
            _buffer[i] = src[i];
        }
    }

    void DoConstruct(const Buffer& src, int from = 0, int to = -1)
    {
        if (src._buffer)
        {
            if (to < 0)
            {
                to = src._length;
            }
            DoConstruct(src._buffer + from, to - from);
        }
        else
        {
            _buffer = nullptr, _length = 0;
        }
    }

  public:
    void Delete()
    {
        if (_buffer)
        {
            delete[] _buffer;
        }
        _buffer = nullptr;
        _length = 0;
    }
    Buffer() { DoConstruct(); }
    Buffer(int len) { DoConstruct(len); }
    Buffer(const Type* src, int len) { DoConstruct(src, len); }
    Buffer(const Buffer& src, int from = 0, int to = -1) { DoConstruct(src, from, to); }
    void Init()
    {
        Delete();
        DoConstruct();
    }
    void Init(int len)
    {
        Delete();
        DoConstruct(len);
    }
    void Init(const Type* src, int len)
    {
        Delete();
        DoConstruct(src, len);
    }
    void Realloc(int len) { (void)len; }
    void Resize(int len)
    {
        if (_length != len)
        {
            Delete(), DoConstruct(len);
        }
    }
    void Init(const Buffer& src, int from = 0, int to = -1)
    {
        Delete();
        DoConstruct(src, from, to);
    }
    ~Buffer() override { Delete(); }

    void operator=(const Buffer& src)
    {
        Delete();
        DoConstruct(src);
    }

    __forceinline int Length() const { return _length; }
    __forceinline int Size() const { return _length; }
    __forceinline Type* Data() { return _buffer; }
    __forceinline const Type* Data() const { return _buffer; }
    __forceinline operator Type*() { return _buffer; }
    __forceinline operator const Type*() const { return _buffer; }

    const Type& operator[](int i) const { return _buffer[i]; }
    Type& operator[](int i) { return _buffer[i]; }

};

} // namespace Poseidon::Foundation

using ::Poseidon::Foundation::Ref;
using ::Poseidon::Foundation::RefD;
using ::Poseidon::Foundation::SRef;
using ::Poseidon::Foundation::RefCount;
using ::Poseidon::Foundation::Buffer;
using ::Poseidon::Foundation::InitPtr;
using ::Poseidon::Foundation::ComRef;
using ::Poseidon::Foundation::RefN;
using ::Poseidon::Foundation::APtr;
using ::Poseidon::Foundation::Temp;

