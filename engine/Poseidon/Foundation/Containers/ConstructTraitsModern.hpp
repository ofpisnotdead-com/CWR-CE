#pragma once

// Trait dispatch backed by std::is_trivially_*; container internals consume this via the typedef-CTraits pattern.

#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

namespace Poseidon::Foundation
{

// ModernTraits destroys the concrete stored Type in place (DestructArray / MoveData) — never
// through a base pointer, so there is no slicing. An explicit destructor call on a type that
// has virtual functions but a deliberately non-virtual destructor (e.g. GameValue) is therefore
// correct here. Mirrors the same suppression at GameValue's definition in Evaluator/express.hpp.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdelete-non-abstract-non-virtual-dtor"

template <class Type>
struct ModernTraits
{
    static void Construct(Type& dst)
    {
        if constexpr (std::is_trivially_default_constructible_v<Type>)
            std::memset(static_cast<void*>(&dst), 0, sizeof(Type));
        else if constexpr (std::is_default_constructible_v<Type>)
            ::new (static_cast<void*>(&dst)) Type();
        else
            std::memset(static_cast<void*>(&dst), 0, sizeof(Type));
    }

    static void ConstructArray(Type* dst, int count)
    {
        if constexpr (std::is_trivially_default_constructible_v<Type>)
            std::memset(static_cast<void*>(dst), 0, count * sizeof(Type));
        else if constexpr (std::is_default_constructible_v<Type>)
            for (int i = 0; i < count; ++i)
                ::new (static_cast<void*>(&dst[i])) Type();
        else
            // Non-default-constructible types (legacy TypeIs*Zeroed) — zero-init is their valid empty state.
            std::memset(static_cast<void*>(dst), 0, count * sizeof(Type));
    }

    static void Destruct(Type& dst)
    {
        if constexpr (!std::is_trivially_destructible_v<Type>)
            dst.~Type();
    }

    static void DestructArray(Type* dst, int n)
    {
        if constexpr (!std::is_trivially_destructible_v<Type>)
            for (int i = 0; i < n; ++i)
                dst[i].~Type();
    }

    static void CopyConstruct(Type* dst, Type const* src, int n)
    {
        if constexpr (std::is_trivially_copyable_v<Type>)
            std::memcpy(static_cast<void*>(dst), src, n * sizeof(Type));
        else
            for (int i = 0; i < n; ++i)
                ::new (static_cast<void*>(&dst[i])) Type(src[i]);
    }

    static void CopyConstruct(Type& dst, Type const& src)
    {
        ::new (static_cast<void*>(&dst)) Type(src);
    }

    static void CopyData(Type* dst, Type const* src, int n)
    {
        if constexpr (std::is_trivially_copyable_v<Type>)
        {
            if (n > 0) // memcpy requires non-null args even for 0 bytes; an empty AutoArray has null data
                std::memcpy(static_cast<void*>(dst), src, n * sizeof(Type));
        }
        else
            for (int i = 0; i < n; ++i)
                dst[i] = src[i];
    }

    static void MoveData(Type* dst, Type* src, int n)
    {
        if constexpr (std::is_trivially_copyable_v<Type>)
        {
            if (n > 0) // memmove requires non-null args even for 0 bytes
                std::memmove(static_cast<void*>(dst), src, n * sizeof(Type));
        }
        else
        {
            for (int i = 0; i < n; ++i)
            {
                ::new (static_cast<void*>(&dst[i])) Type(std::move(src[i]));
                src[i].~Type();
            }
        }
    }

    static void InsertData(Type* dst, int n, int count = 1)
    {
        if constexpr (std::is_trivially_copyable_v<Type>)
        {
            std::memmove(static_cast<void*>(dst + count), dst, (n - count) * sizeof(Type));
        }
        else
        {
            // Per-element shift right: assign-down, then destruct the shifted-out slot.
            for (int i = n - count; --i >= 0;)
                dst[i + count] = dst[i];
            DestructArray(dst, count);
        }
    }

    static void DeleteData(Type* dst, int n, int count)
    {
        if constexpr (std::is_trivially_copyable_v<Type>)
        {
            std::memmove(static_cast<void*>(dst), dst + count, (n - count) * sizeof(Type));
        }
        else
        {
            // Per-element shift left: construct gap, copy-shift, destruct the tail.
            for (int i = 0; i < count; ++i)
                Construct(dst[i]);
            for (int i = count; i < n; ++i)
                dst[i - count] = dst[i];
            for (int i = n - count; i < n; ++i)
                Destruct(dst[i]);
        }
    }
};

template <class Type>
struct LegacyMovableTraits
{
    static void Construct(Type& dst)
    {
        if constexpr (std::is_trivially_default_constructible_v<Type>)
            std::memset(static_cast<void*>(&dst), 0, sizeof(Type));
        else if constexpr (std::is_default_constructible_v<Type>)
            ::new (static_cast<void*>(&dst)) Type();
        else
            std::memset(static_cast<void*>(&dst), 0, sizeof(Type));
    }

    static void ConstructArray(Type* dst, int count)
    {
        if constexpr (std::is_trivially_default_constructible_v<Type>)
            std::memset(static_cast<void*>(dst), 0, count * sizeof(Type));
        else if constexpr (std::is_default_constructible_v<Type>)
            for (int i = 0; i < count; ++i)
                ::new (static_cast<void*>(&dst[i])) Type();
        else
            std::memset(static_cast<void*>(dst), 0, count * sizeof(Type));
    }

    static void Destruct(Type& dst)
    {
        if constexpr (!std::is_trivially_destructible_v<Type>)
            dst.~Type();
    }

    static void DestructArray(Type* dst, int n)
    {
        if constexpr (!std::is_trivially_destructible_v<Type>)
            for (int i = 0; i < n; ++i)
                dst[i].~Type();
    }

    static void CopyConstruct(Type* dst, Type const* src, int n)
    {
        if constexpr (std::is_trivially_copyable_v<Type>)
            std::memcpy(static_cast<void*>(dst), src, n * sizeof(Type));
        else
            for (int i = 0; i < n; ++i)
                ::new (static_cast<void*>(&dst[i])) Type(src[i]);
    }

    static void CopyConstruct(Type& dst, Type const& src)
    {
        ::new (static_cast<void*>(&dst)) Type(src);
    }

    static void CopyData(Type* dst, Type const* src, int n)
    {
        if constexpr (std::is_trivially_copyable_v<Type>)
        {
            if (n > 0)
                std::memcpy(static_cast<void*>(dst), src, n * sizeof(Type));
        }
        else
            for (int i = 0; i < n; ++i)
                dst[i] = src[i];
    }

    static void MoveData(Type* dst, Type* src, int n)
    {
        if (n > 0)
            std::memmove(static_cast<void*>(dst), src, n * sizeof(Type));
    }

    static void InsertData(Type* dst, int n, int count = 1)
    {
        DestructArray(dst + n - count, count);
        if (n > count)
            std::memmove(static_cast<void*>(dst + count), dst, (n - count) * sizeof(Type));
    }

    static void DeleteData(Type* dst, int n, int count)
    {
        if (n > count)
            std::memmove(static_cast<void*>(dst), dst + count, (n - count) * sizeof(Type));
    }
};

#pragma clang diagnostic pop

} // namespace Poseidon::Foundation
