#pragma once

#include <cstddef>
#include <optional>

namespace Poseidon::render::gl33
{

constexpr std::optional<std::size_t> TerrainSegmentIndex(int cellX, int cellZ, int segmentSize, int segmentRange)
{
    if (cellX < 0 || cellZ < 0 || segmentSize <= 0 || segmentRange <= 0)
    {
        return std::nullopt;
    }

    const int segmentX = cellX / segmentSize;
    const int segmentZ = cellZ / segmentSize;
    if (segmentX >= segmentRange || segmentZ >= segmentRange)
    {
        return std::nullopt;
    }

    return static_cast<std::size_t>(segmentZ) * segmentRange + segmentX;
}

} // namespace Poseidon::render::gl33
