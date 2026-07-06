#include <Poseidon/Graphics/GraphicsEngineFactory.hpp>
#include <Poseidon/Graphics/Core/EngineFactory.hpp>
#include <PoseidonMTL/EngineMTL.hpp>

using Poseidon::Engine;

using Poseidon::GraphicsBackendDescriptor;

using Poseidon::GraphicsEngineParams;
using Poseidon::GraphicsEngineFactory;

using Poseidon::CreateEngineMTL;

namespace
{
Engine* CreateMTLBackend(const GraphicsEngineParams& params)
{
    return CreateEngineMTL(params.width, params.height, params.useWindow, params.bitsPerPixel);
}

bool IsMTLAvailable()
{
    return true;
}
} // namespace

namespace Poseidon
{
Engine* CreateEngineMTL(int w, int h, bool windowed, int bpp)
{
    return new EngineMTL(w, h, windowed, bpp);
}

void RegisterMetalGraphicsBackend()
{
    // Priority 110 (> GL33's 100): on macOS, "Auto" backend selection must
    // prefer Metal over GL33 every time -- GL33 registers unconditionally
    // and always reports available, but OpenGL is deprecated/removed on
    // this platform (and entirely absent on Apple Silicon), so letting it
    // win an Auto tie here would pick a backend that can't actually render.
    GraphicsEngineFactory::Register(GraphicsBackendDescriptor{
        "mtl",
        "Metal (SDL3)",
        110,
        &CreateMTLBackend,
        &IsMTLAvailable,
    });
}
} // namespace Poseidon
