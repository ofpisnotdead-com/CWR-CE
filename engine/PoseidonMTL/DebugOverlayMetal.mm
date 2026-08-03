#include <PoseidonMTL/DebugOverlayMetal.hpp>

#include <Poseidon/Dev/Debug/DebugOverlay.hpp>

#include <imgui.h>
#include <imgui_impl_metal.h>

namespace Poseidon::Dev::DebugOverlayMetal
{
namespace
{
bool s_initialized = false;
}

bool Init(MTL::Device* device)
{
    if (s_initialized)
        return true;
    if (device == nullptr)
        return false;
    s_initialized = ImGui_ImplMetal_Init((__bridge id<MTLDevice>)device);
    return s_initialized;
}

void NewFrame(MTL::RenderPassDescriptor* renderPassDescriptor)
{
    if (!s_initialized || renderPassDescriptor == nullptr)
        return;
    ImGui_ImplMetal_NewFrame((__bridge MTLRenderPassDescriptor*)renderPassDescriptor);
}

void Render(MTL::CommandBuffer* commandBuffer, MTL::RenderCommandEncoder* commandEncoder)
{
    if (!s_initialized || commandBuffer == nullptr || commandEncoder == nullptr)
        return;
    ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), (__bridge id<MTLCommandBuffer>)commandBuffer,
                                   (__bridge id<MTLRenderCommandEncoder>)commandEncoder);
}

void Shutdown()
{
    if (!s_initialized)
        return;
    ImGui_ImplMetal_Shutdown();
    s_initialized = false;
}
} // namespace Poseidon::Dev::DebugOverlayMetal
