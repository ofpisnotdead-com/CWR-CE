#pragma once

#include <Poseidon/Graphics/Rendering/RenderPassDescriptor.hpp>

namespace Poseidon::GL33Pipeline
{

struct Changes
{
    bool depth;
    bool blend;
    bool polygonOffset;
    bool cull;
    bool frontFace;
};

constexpr Changes Compare(const render::RenderPassDescriptor& previous, const render::RenderPassDescriptor& next,
                          bool previousValid)
{
    const bool force = !previousValid;
    return {
        force || previous.depth != next.depth,
        force || previous.blend != next.blend,
        force || previous.shader != next.shader || previous.surface != next.surface,
        force || previous.cull != next.cull,
        force || previous.frontFace != next.frontFace,
    };
}

} // namespace Poseidon::GL33Pipeline
