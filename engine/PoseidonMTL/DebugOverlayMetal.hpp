#pragma once

#include <Metal/Metal.hpp>

struct SDL_Window;

namespace Poseidon::Dev::DebugOverlayMetal
{
    bool Init(MTL::Device* device);
    void NewFrame(MTL::RenderPassDescriptor* renderPassDescriptor);
    void Render(MTL::CommandBuffer* commandBuffer, MTL::RenderCommandEncoder* commandEncoder);
    void Shutdown();
} // namespace Poseidon::Dev::DebugOverlayMetal
