#pragma once

#include <cstdint>
#include <cstring>

#include <Poseidon/Foundation/Containers/Array.hpp>

namespace Poseidon::Foundation
{

// Map a float to a uint32 whose unsigned ordering matches the float's ascending order:
// if the sign bit is set, invert all bits; otherwise set only the sign bit.
__forceinline uint32_t FloatToOrderedU32(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u ^ (uint32_t(0x80000000) | (uint32_t(0) - (u >> 31)));
}

// Reusable buffers so hot call sites sort without allocating each call.
template <class T>
struct RadixSortBuffers
{
    struct Slot
    {
        uint32_t key;
        T value;
    };
    AutoArray<Slot> a;
    AutoArray<Slot> b;
};

// Sort items[0..n) in place, largest float key first, via LSD radix (four 8-bit passes).
template <class T, class KeyFn>
void RadixSortByFloatDesc(T* items, int n, KeyFn key, RadixSortBuffers<T>& buffers)
{
    if (n < 2)
    {
        return;
    }
    using Slot = typename RadixSortBuffers<T>::Slot;
    buffers.a.Resize(n);
    buffers.b.Resize(n);
    Slot* in = buffers.a.Data();
    Slot* out = buffers.b.Data();
    for (int i = 0; i < n; i++)
    {
        in[i].key = FloatToOrderedU32(key(items[i]));
        in[i].value = items[i];
    }
    for (int shift = 0; shift < 32; shift += 8)
    {
        int count[256] = {0};
        for (int i = 0; i < n; i++)
        {
            count[(in[i].key >> shift) & 0xFF]++;
        }
        int sum = 0;
        for (int b = 0; b < 256; b++)
        {
            int c = count[b];
            count[b] = sum;
            sum += c;
        }
        for (int i = 0; i < n; i++)
        {
            out[count[(in[i].key >> shift) & 0xFF]++] = in[i];
        }
        Slot* tmp = in;
        in = out;
        out = tmp;
    }
    // `in` holds ascending order; write back descending (largest first).
    for (int i = 0; i < n; i++)
    {
        items[i] = in[n - 1 - i].value;
    }
}

// Convenience overload allocating its own buffers, for cold or test call sites.
template <class T, class KeyFn>
void RadixSortByFloatDesc(T* items, int n, KeyFn key)
{
    RadixSortBuffers<T> buffers;
    RadixSortByFloatDesc(items, n, key, buffers);
}

} // namespace Poseidon::Foundation
