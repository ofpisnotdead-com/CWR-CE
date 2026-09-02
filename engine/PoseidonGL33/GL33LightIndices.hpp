#pragma once

#include <array>
#include <cstdint>

namespace Poseidon::GL33LightIndices
{

inline constexpr int Capacity = 8;

constexpr std::array<std::uint32_t, 4> Pack(const int* indices, int count)
{
    if (count < 0)
    {
        count = 0;
    }
    if (count > Capacity)
    {
        count = Capacity;
    }

    std::array<std::uint32_t, 4> packed{};
    for (int i = 0; i < count; ++i)
    {
        packed[i >> 2] |= static_cast<std::uint32_t>(indices[i] & 0xff) << (8 * (i & 3));
    }
    packed[2] = static_cast<std::uint32_t>(count);
    return packed;
}

} // namespace Poseidon::GL33LightIndices
