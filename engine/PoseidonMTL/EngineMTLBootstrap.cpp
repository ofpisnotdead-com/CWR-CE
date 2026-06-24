#include <PoseidonMTL/EngineMTLBootstrap.hpp>

// metal-cpp implementation macros live in MetalCppImpl.cpp (one definition
// per binary); this file only needs the declarations.
#include <Foundation/Foundation.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <Metal/Metal.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>

#include <cstring>
#include <vector>

// Log.hpp itself only pulls in <spdlog/spdlog.h> -- it does NOT transitively
// include the Poseidon headers (Types.hpp's `using Poseidon::Object;`,
// Memtype.h's `typedef int BOOL`) that actually collide with metal-cpp's
// NS::Object/BOOL. So unlike the rest of Poseidon's headers, this one is
// safe to include here, and real LOG_ERROR calls replace the old
// stderr-only fprintf (which a host process redirecting/capturing stderr
// could silently swallow -- see IsPipelineReady()'s doc comment in the
// header, now stale).
#include <Poseidon/Foundation/Framework/Log.hpp>

namespace Poseidon
{

namespace
{
// Manual vertex fetch by vertex_id (no MTLVertexDescriptor) -- the simplest
// correct setup for a single fixed vertex layout. Vertex2D's MSL layout
// (float2 + float2 + float4, natural alignment) matches Vertex2DMTL's C++
// layout byte-for-byte: position@0, uv@8, color@16, size 32.
const char* kShaderSource2D = R"(
#include <metal_stdlib>
using namespace metal;

struct Vertex2D {
    float2 position;
    float2 uv;
    float4 color;
};

struct VSOut {
    float4 position [[position]];
    float2 uv;
    float4 color;
};

vertex VSOut vs2d(uint vid [[vertex_id]], const device Vertex2D* verts [[buffer(0)]])
{
    Vertex2D v = verts[vid];
    VSOut out;
    out.position = float4(v.position, 0.0, 1.0);
    out.uv = v.uv;
    out.color = v.color;
    return out;
}

fragment float4 fs2d(VSOut in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler samp [[sampler(0)]])
{
    float4 texColor = tex.sample(samp, in.uv);
    return texColor * in.color;
}
)";

// Hardware T&L mesh shader: GPU does the model->view->projection transform
// (unlike the 2D/legacy-TL paths, where the CPU pre-transforms to screen
// space). Per-vertex (Gouraud) lighting -- sun diffuse+ambient+emissive only
// for v1, no local lights/specular/shadows (ported from GL33's
// vsTransformGLSL/psNormalGLSL, minus those features -- see
// METAL_PORT_PROGRESS.md). Matrices are row-major (GfxMatrix layout, v' = v*M
// with translation in row 3) -- multiplied explicitly via mulRowVec rather
// than MSL's float4x4, to sidestep any ambiguity about that type's assumed
// major-order.
const char* kShaderSourceMesh = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexMesh {
    float3 pos;
    float3 norm;
    float2 uv;
};

struct Mat4Rows {
    float4 r0, r1, r2, r3;
};

struct FrameConstants {
    Mat4Rows view;
    Mat4Rows projection;
    float4 sunDirAndEnabled;
    float4 fogParams;
    float4 fogColor;
};

struct ObjectConstants {
    Mat4Rows world;
    float4 ambient;
    float4 diffuse;
    float4 emissive;
};

static inline float4 mulRowVec4(float4 p, Mat4Rows m)
{
    return float4(dot(p, float4(m.r0.x, m.r1.x, m.r2.x, m.r3.x)), dot(p, float4(m.r0.y, m.r1.y, m.r2.y, m.r3.y)),
                  dot(p, float4(m.r0.z, m.r1.z, m.r2.z, m.r3.z)), dot(p, float4(m.r0.w, m.r1.w, m.r2.w, m.r3.w)));
}

static inline float3 mulRowVec3(float3 p, Mat4Rows m)
{
    return float3(p.x * m.r0.x + p.y * m.r1.x + p.z * m.r2.x, p.x * m.r0.y + p.y * m.r1.y + p.z * m.r2.y,
                  p.x * m.r0.z + p.y * m.r1.z + p.z * m.r2.z);
}

struct VSOutMesh {
    float4 position [[position]];
    float2 uv;
    float4 color;
    float fogFactor;
};

vertex VSOutMesh vsMesh(uint vid [[vertex_id]], const device VertexMesh* verts [[buffer(0)]],
                        constant ObjectConstants& obj [[buffer(1)]], constant FrameConstants& frame [[buffer(2)]])
{
    VertexMesh v = verts[vid];

    // World transform is already camera-relative (translation has the
    // camera position subtracted on the CPU side), so the result here is
    // directly usable as a camera-relative distance for fog, with no
    // separate view-space step needed for that.
    float4 worldPos4 = mulRowVec4(float4(v.pos, 1.0), obj.world);
    float3 worldNorm = normalize(mulRowVec3(v.norm, obj.world));

    float4 viewPos = mulRowVec4(worldPos4, frame.view);
    float4 clipPos = mulRowVec4(viewPos, frame.projection);

    // ambient/diffuse already have the sun's color baked in on the CPU side
    // (EngineMTL::SetMaterial, mirroring GL33's UploadVSMaterialConstants) --
    // that combination happens unconditionally, not gated by EnableSunLight,
    // so the shader doesn't re-gate them here either (verified empirically:
    // the main menu's 3D preview prop has sunEnabled=false yet renders lit
    // under GL33). Only the literal per-vertex NdotL falloff depends on the
    // sun direction actually being meaningful, which sunEnabled still guards.
    float sunEnabled = frame.sunDirAndEnabled.w;
    float NdotL = max(dot(worldNorm, frame.sunDirAndEnabled.xyz), 0.0) * sunEnabled;
    float3 lit = obj.ambient.rgb + NdotL * obj.diffuse.rgb + obj.emissive.rgb;

    float dist = length(worldPos4.xyz);
    float fogFactor =
        frame.fogParams.z > 0.5 ? clamp((dist - frame.fogParams.x) * frame.fogParams.y, 0.0, 1.0) : 0.0;

    VSOutMesh out;
    out.position = clipPos;
    out.uv = v.uv;
    out.color = float4(lit, 1.0);
    out.fogFactor = fogFactor;
    return out;
}

fragment float4 fsMesh(VSOutMesh in [[stage_in]], constant FrameConstants& frame [[buffer(0)]],
                       texture2d<float> tex [[texture(0)]], sampler samp [[sampler(0)]])
{
    float4 texColor = tex.sample(samp, in.uv);
    float3 litColor = texColor.rgb * in.color.rgb;
    float3 finalColor = mix(litColor, frame.fogColor.rgb, in.fogFactor);
    // v1 opaque-only -- always 1, not texColor.a. Diffuse-only legacy
    // textures commonly leave the alpha channel at 0 (no real alpha data),
    // which combined with the pipeline's blending state composited as fully
    // transparent (showing the black clear color through it) -- this was the
    // actual cause of "still black" after the ambient/diffuse lighting fix.
    return float4(finalColor, 1.0);
}
)";
} // namespace

struct EngineMTLBootstrap::Impl
{
    SDL_MetalView metalView = nullptr;
    CA::MetalLayer* layer = nullptr;
    MTL::Device* device = nullptr;
    MTL::CommandQueue* commandQueue = nullptr;

    MTL::RenderPipelineState* pipelineState = nullptr;
    MTL::SamplerState* samplerState = nullptr;
    MTL::Texture* fallbackWhite = nullptr;
    std::vector<MTL::Texture*> textures; // handle = index + 1; 0 reserved for "none"

    // 3D hardware T&L mesh pipeline (separate from the 2D one above -- the
    // vertex layout differs, pos/norm/uv vs. the 2D path's screen-space
    // pos/uv/color).
    MTL::RenderPipelineState* pipelineStateTL = nullptr;
    // Depth test+write for 3D mesh draws, vs. always-pass/no-write for 2D UI
    // draws sharing the same encoder/pass -- both pipelines declare the same
    // depthAttachmentPixelFormat (required by Metal once the pass has a
    // depth attachment), but only TL draws should actually test/write it.
    MTL::DepthStencilState* depthStateTL = nullptr;
    MTL::DepthStencilState* depthStateDisabled = nullptr;
    MTL::Texture* depthTexture = nullptr;
    std::vector<MTL::Buffer*> meshBuffers; // handle = index + 1; 0 reserved for "none"

    // Open between BeginFrame/EndFrame.
    CA::MetalDrawable* currentDrawable = nullptr;
    MTL::CommandBuffer* currentCommandBuffer = nullptr;
    MTL::RenderCommandEncoder* currentEncoder = nullptr;
    bool frameHadColorClear = false;

    int drawableWidth = 0;
    int drawableHeight = 0;
};

EngineMTLBootstrap::EngineMTLBootstrap() : _impl(new Impl()) {}

EngineMTLBootstrap::~EngineMTLBootstrap()
{
    Shutdown();
    delete _impl;
}

bool EngineMTLBootstrap::Init(const char* title, int width, int height)
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        LOG_ERROR(Graphics, "EngineMTLBootstrap: SDL_InitSubSystem(VIDEO) failed: {}", SDL_GetError());
        return false;
    }

    _window = SDL_CreateWindow(title, width, height, SDL_WINDOW_METAL | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (_window == nullptr)
    {
        LOG_ERROR(Graphics, "EngineMTLBootstrap: SDL_CreateWindow failed: {}", SDL_GetError());
        return false;
    }
    _ownsWindow = true;

    return SetupDevice();
}

bool EngineMTLBootstrap::AttachToWindow(SDL_Window* window)
{
    _window = window;
    _ownsWindow = false;
    return SetupDevice();
}

bool EngineMTLBootstrap::SetupDevice()
{
    _impl->metalView = SDL_Metal_CreateView(_window);
    if (_impl->metalView == nullptr)
    {
        LOG_ERROR(Graphics, "EngineMTLBootstrap: SDL_Metal_CreateView failed: {}", SDL_GetError());
        return false;
    }

    _impl->layer = static_cast<CA::MetalLayer*>(SDL_Metal_GetLayer(_impl->metalView));
    if (_impl->layer == nullptr)
    {
        LOG_ERROR(Graphics, "EngineMTLBootstrap: SDL_Metal_GetLayer returned null");
        return false;
    }

    _impl->device = MTL::CreateSystemDefaultDevice();
    if (_impl->device == nullptr)
    {
        LOG_ERROR(Graphics, "EngineMTLBootstrap: MTL::CreateSystemDefaultDevice() returned null");
        return false;
    }

    _impl->layer->setDevice(_impl->device);
    _impl->layer->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    // Engine code skips the color clear on some Clear() calls (e.g. additive
    // animation/trail effects) on the assumption that the buffer it's about
    // to draw into already holds exactly last frame's image -- true under
    // GL33's strict double-buffered swap. CAMetalLayer defaults to a deeper
    // 3-drawable rotation, so a skipped-clear frame can land on a buffer
    // that's 2 frames stale instead of 1, ghosting multiple recent
    // positions instead of just the last one. Pin it to 2 to match.
    _impl->layer->setMaximumDrawableCount(2);
    // Without this, nextDrawable()/presentDrawable() never pace to the
    // display's refresh interval -- BeginFrame/EndFrame still succeed every
    // call, just thousands of times per second, so World::Simulate's deltaT
    // stays ~0 forever and the visible scene (and any deltaT-driven
    // animation/cutscene timeline) appears completely frozen while the CPU
    // pins at 100% re-simulating the same instant. GL33 doesn't need this --
    // SDL_GL_SwapWindow blocks on vsync on its own.
    _impl->layer->setDisplaySyncEnabled(true);

    int pxWidth = 0, pxHeight = 0;
    SDL_GetWindowSizeInPixels(_window, &pxWidth, &pxHeight);
    _impl->layer->setDrawableSize(CGSizeMake(static_cast<CGFloat>(pxWidth), static_cast<CGFloat>(pxHeight)));
    _impl->drawableWidth = pxWidth;
    _impl->drawableHeight = pxHeight;
    EnsureDepthTarget(pxWidth, pxHeight);

    _impl->commandQueue = _impl->device->newCommandQueue();
    if (_impl->commandQueue == nullptr)
    {
        LOG_ERROR(Graphics, "EngineMTLBootstrap: newCommandQueue() returned null");
        return false;
    }

    return true;
}

void EngineMTLBootstrap::RenderClearAndPresent(float r, float g, float b, float a, bool clear)
{
    if (_impl->layer == nullptr || _impl->commandQueue == nullptr)
        return;

    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

    CA::MetalDrawable* drawable = _impl->layer->nextDrawable();
    if (drawable == nullptr)
    {
        pool->release();
        return;
    }

    MTL::RenderPassDescriptor* passDesc = MTL::RenderPassDescriptor::alloc()->init();
    MTL::RenderPassColorAttachmentDescriptor* colorAttachment = passDesc->colorAttachments()->object(0);
    colorAttachment->setTexture(drawable->texture());
    colorAttachment->setLoadAction(clear ? MTL::LoadActionClear : MTL::LoadActionLoad);
    colorAttachment->setStoreAction(MTL::StoreActionStore);
    colorAttachment->setClearColor(MTL::ClearColor::Make(r, g, b, a));

    MTL::CommandBuffer* cmdBuf = _impl->commandQueue->commandBuffer();
    MTL::RenderCommandEncoder* encoder = cmdBuf->renderCommandEncoder(passDesc);
    encoder->endEncoding();

    cmdBuf->presentDrawable(drawable);
    cmdBuf->commit();

    passDesc->release();
    pool->release();
}

void EngineMTLBootstrap::OnWindowResized(int width, int height)
{
    if (_impl->layer == nullptr)
        return;
    _impl->drawableWidth = width;
    _impl->drawableHeight = height;
    _impl->layer->setDrawableSize(CGSizeMake(static_cast<CGFloat>(width), static_cast<CGFloat>(height)));
    EnsureDepthTarget(width, height);
}

void EngineMTLBootstrap::EnsureDepthTarget(int width, int height)
{
    if (_impl->device == nullptr || width <= 0 || height <= 0)
        return;
    if (_impl->depthTexture != nullptr && static_cast<int>(_impl->depthTexture->width()) == width &&
        static_cast<int>(_impl->depthTexture->height()) == height)
        return;

    if (_impl->depthTexture != nullptr)
    {
        _impl->depthTexture->release();
        _impl->depthTexture = nullptr;
    }

    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::texture2DDescriptor(
        MTL::PixelFormatDepth32Float, static_cast<NS::UInteger>(width), static_cast<NS::UInteger>(height), false);
    desc->setUsage(MTL::TextureUsageRenderTarget);
    desc->setStorageMode(MTL::StorageModePrivate);
    _impl->depthTexture = _impl->device->newTexture(desc);
    desc->release();
}

std::string EngineMTLBootstrap::GetRendererName() const
{
    if (_impl->device == nullptr)
        return {};
    return _impl->device->name()->utf8String();
}

bool EngineMTLBootstrap::IsPipelineReady() const
{
    return _impl->pipelineState != nullptr;
}

void EngineMTLBootstrap::EnsurePipeline()
{
    if (_impl->pipelineState != nullptr || _impl->device == nullptr)
        return;

    NS::Error* error = nullptr;
    NS::String* src = NS::String::string(kShaderSource2D, NS::StringEncoding::UTF8StringEncoding);
    MTL::Library* library = _impl->device->newLibrary(src, nullptr, &error);
    if (library == nullptr)
    {
        LOG_ERROR(Graphics, "EngineMTLBootstrap: shader compile failed: {}",
                  error ? error->localizedDescription()->utf8String() : "(unknown)");
        return;
    }

    MTL::Function* vsFn = library->newFunction(NS::String::string("vs2d", NS::StringEncoding::UTF8StringEncoding));
    MTL::Function* fsFn = library->newFunction(NS::String::string("fs2d", NS::StringEncoding::UTF8StringEncoding));

    MTL::RenderPipelineDescriptor* desc = MTL::RenderPipelineDescriptor::alloc()->init();
    desc->setVertexFunction(vsFn);
    desc->setFragmentFunction(fsFn);
    MTL::RenderPipelineColorAttachmentDescriptor* colorDesc = desc->colorAttachments()->object(0);
    colorDesc->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    colorDesc->setBlendingEnabled(true);
    colorDesc->setRgbBlendOperation(MTL::BlendOperationAdd);
    colorDesc->setAlphaBlendOperation(MTL::BlendOperationAdd);
    colorDesc->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);
    colorDesc->setSourceAlphaBlendFactor(MTL::BlendFactorSourceAlpha);
    colorDesc->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
    colorDesc->setDestinationAlphaBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
    // Required once the shared render pass carries a depth attachment (added
    // for the TL mesh pipeline) -- every pipeline state used within that
    // encoder must declare a matching format, even this one, which always
    // binds depthStateDisabled (no actual test/write) when drawing.
    desc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);

    _impl->pipelineState = _impl->device->newRenderPipelineState(desc, &error);
    if (_impl->pipelineState == nullptr)
    {
        LOG_ERROR(Graphics, "EngineMTLBootstrap: pipeline state creation failed: {}",
                  error ? error->localizedDescription()->utf8String() : "(unknown)");
    }

    desc->release();
    vsFn->release();
    fsFn->release();
    library->release();

    MTL::SamplerDescriptor* sampDesc = MTL::SamplerDescriptor::alloc()->init();
    sampDesc->setMinFilter(MTL::SamplerMinMagFilterLinear);
    sampDesc->setMagFilter(MTL::SamplerMinMagFilterLinear);
    sampDesc->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
    sampDesc->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
    _impl->samplerState = _impl->device->newSamplerState(sampDesc);
    sampDesc->release();

    MTL::DepthStencilDescriptor* depthDescTL = MTL::DepthStencilDescriptor::alloc()->init();
    depthDescTL->setDepthCompareFunction(MTL::CompareFunctionLessEqual);
    depthDescTL->setDepthWriteEnabled(true);
    _impl->depthStateTL = _impl->device->newDepthStencilState(depthDescTL);
    depthDescTL->release();

    MTL::DepthStencilDescriptor* depthDescOff = MTL::DepthStencilDescriptor::alloc()->init();
    depthDescOff->setDepthCompareFunction(MTL::CompareFunctionAlways);
    depthDescOff->setDepthWriteEnabled(false);
    _impl->depthStateDisabled = _impl->device->newDepthStencilState(depthDescOff);
    depthDescOff->release();
}

void EngineMTLBootstrap::EnsureTLPipeline()
{
    if (_impl->pipelineStateTL != nullptr || _impl->device == nullptr)
        return;

    NS::Error* error = nullptr;
    NS::String* src = NS::String::string(kShaderSourceMesh, NS::StringEncoding::UTF8StringEncoding);
    MTL::Library* library = _impl->device->newLibrary(src, nullptr, &error);
    if (library == nullptr)
    {
        LOG_ERROR(Graphics, "EngineMTLBootstrap: mesh shader compile failed: {}",
                  error ? error->localizedDescription()->utf8String() : "(unknown)");
        return;
    }

    MTL::Function* vsFn = library->newFunction(NS::String::string("vsMesh", NS::StringEncoding::UTF8StringEncoding));
    MTL::Function* fsFn = library->newFunction(NS::String::string("fsMesh", NS::StringEncoding::UTF8StringEncoding));

    MTL::RenderPipelineDescriptor* desc = MTL::RenderPipelineDescriptor::alloc()->init();
    desc->setVertexFunction(vsFn);
    desc->setFragmentFunction(fsFn);
    MTL::RenderPipelineColorAttachmentDescriptor* colorDesc = desc->colorAttachments()->object(0);
    colorDesc->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    // v1 opaque-only: alpha is always 1 from fsMesh, so this blend setup is
    // visually a no-op but keeps the descriptor consistent with the 2D
    // pipeline. Alpha test / blend (Cutout/Transparent passes) is a known
    // follow-up -- see METAL_PORT_PROGRESS.md.
    colorDesc->setBlendingEnabled(true);
    colorDesc->setRgbBlendOperation(MTL::BlendOperationAdd);
    colorDesc->setAlphaBlendOperation(MTL::BlendOperationAdd);
    colorDesc->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);
    colorDesc->setSourceAlphaBlendFactor(MTL::BlendFactorSourceAlpha);
    colorDesc->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
    colorDesc->setDestinationAlphaBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
    desc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);

    _impl->pipelineStateTL = _impl->device->newRenderPipelineState(desc, &error);
    if (_impl->pipelineStateTL == nullptr)
    {
        LOG_ERROR(Graphics, "EngineMTLBootstrap: mesh pipeline state creation failed: {}",
                  error ? error->localizedDescription()->utf8String() : "(unknown)");
    }

    desc->release();
    vsFn->release();
    fsFn->release();
    library->release();
}

void EngineMTLBootstrap::EnsureFallbackResources()
{
    if (_impl->fallbackWhite != nullptr || _impl->device == nullptr)
        return;

    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, 1, 1, false);
    desc->setUsage(MTL::TextureUsageShaderRead);
    desc->setStorageMode(MTL::StorageModeShared);
    _impl->fallbackWhite = _impl->device->newTexture(desc);
    desc->release();

    const uint8_t whitePixel[4] = {255, 255, 255, 255};
    _impl->fallbackWhite->replaceRegion(MTL::Region::Make2D(0, 0, 1, 1), 0, whitePixel, 4);
}

bool EngineMTLBootstrap::BeginFrame(float r, float g, float b, float a, bool clear, bool clearZ)
{
    if (_impl->layer == nullptr || _impl->commandQueue == nullptr)
        return false;

    EnsurePipeline();
    EnsureFallbackResources();
    if (_impl->pipelineState == nullptr)
        return false;

    // Local pool just to drain incidental temporaries from this call -- it
    // must NOT be relied on to keep the drawable/command buffer/encoder
    // alive past this function returning. Those three are stored in _impl
    // for DrawTriangles2D/EndFrame to use across separate later calls, but
    // commandQueue->commandBuffer() and commandBuffer->renderCommandEncoder()
    // (like nextDrawable()) return autoreleased objects, not owned (+1)
    // references -- draining ANY pool that was active when they were
    // created can deallocate them. That's exactly what an earlier version
    // of this function did (pool scoped to this call, released at the
    // bottom): Metal's own validation caught the encoder being deallocated
    // without endEncoding() ever having been called on it and called
    // abort() (SIGABRT, reproduced by selecting a main-menu item). Explicit
    // retain()/release() below -- the same manual-ownership pattern already
    // used for vbuf/ibuf/passDesc/textures in this file -- makes their
    // lifetime independent of pool timing entirely.
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

    const bool firstPassThisFrame = (_impl->currentDrawable == nullptr);

    if (firstPassThisFrame)
    {
        // First Clear() of this displayed frame: acquire the drawable and
        // open the frame's one command buffer. This must happen exactly
        // once per frame -- nextDrawable() hands back a recycled texture
        // from CAMetalLayer's small swap pool (commonly 3 buffers), not
        // "the same buffer earlier draws this frame already went into".
        // Engine code legitimately calls Clear() more than once per frame
        // (e.g. UIContainers.cpp clears Z only, mid-frame, before a 3D
        // preview pass) -- calling nextDrawable() again on those calls used
        // to hand later draws a different, stale-content texture and
        // silently drop the in-flight command buffer from earlier in the
        // frame, which is what produced the ghosting/stale-content
        // artifacts.
        _impl->currentDrawable = _impl->layer->nextDrawable();
        if (_impl->currentDrawable == nullptr)
        {
            pool->release();
            return false;
        }
        _impl->currentDrawable->retain();
        _impl->currentCommandBuffer = _impl->commandQueue->commandBuffer();
        _impl->currentCommandBuffer->retain();
        _impl->frameHadColorClear = false;
    }
    else if (_impl->currentEncoder != nullptr)
    {
        // Mid-frame Clear(): reuse the same drawable/command buffer, just
        // end the previous pass's encoder before opening a new one on it.
        _impl->currentEncoder->endEncoding();
        _impl->currentEncoder->release();
        _impl->currentEncoder = nullptr;
    }

    MTL::RenderPassDescriptor* passDesc = MTL::RenderPassDescriptor::alloc()->init();
    MTL::RenderPassColorAttachmentDescriptor* colorAttachment = passDesc->colorAttachments()->object(0);
    colorAttachment->setTexture(_impl->currentDrawable->texture());
    // GL's Clear(false, false) or Clear(true, false) on the first draw of a
    // frame still targets the current backbuffer after swap. Under Metal,
    // LoadActionLoad on the first pass reads an undefined recycled drawable.
    // If the world scene did not draw before a UI 3D-object depth clear, that
    // recycled content shows as menu ghosting. Protect the first pass with a
    // color clear, while preserving mid-frame depth-only clears after color
    // content has already been drawn.
    const bool clearColorThisPass = clear || firstPassThisFrame || !_impl->frameHadColorClear;
    colorAttachment->setLoadAction(clearColorThisPass ? MTL::LoadActionClear : MTL::LoadActionLoad);
    colorAttachment->setStoreAction(MTL::StoreActionStore);
    colorAttachment->setClearColor(MTL::ClearColor::Make(r, g, b, a));
    if (clearColorThisPass)
        _impl->frameHadColorClear = true;

    // Depth attachment for the TL mesh pipeline -- a single persistent
    // texture (not swapchain-rotated like the color drawable), so `Load`
    // here correctly retrieves whatever an earlier encoder this frame wrote,
    // as long as that encoder's depth attachment used StoreActionStore (it
    // does, unconditionally, same as the color attachment above -- Apple's
    // tile-based GPUs would otherwise discard the tile contents at
    // end-of-encoder even though the underlying texture object persists).
    if (_impl->depthTexture != nullptr)
    {
        MTL::RenderPassDepthAttachmentDescriptor* depthAttachment = passDesc->depthAttachment();
        depthAttachment->setTexture(_impl->depthTexture);
        depthAttachment->setLoadAction(clearZ ? MTL::LoadActionClear : MTL::LoadActionLoad);
        depthAttachment->setStoreAction(MTL::StoreActionStore);
        depthAttachment->setClearDepth(1.0);
    }

    _impl->currentEncoder = _impl->currentCommandBuffer->renderCommandEncoder(passDesc);
    _impl->currentEncoder->retain();
    MTL::Viewport viewport;
    viewport.originX = 0.0;
    viewport.originY = 0.0;
    viewport.width = static_cast<double>(_impl->drawableWidth);
    viewport.height = static_cast<double>(_impl->drawableHeight);
    viewport.znear = 0.0;
    viewport.zfar = 1.0;
    _impl->currentEncoder->setViewport(viewport);
    _impl->currentEncoder->setRenderPipelineState(_impl->pipelineState);
    _impl->currentEncoder->setDepthStencilState(_impl->depthStateDisabled);
    _impl->currentEncoder->setFragmentSamplerState(_impl->samplerState, 0);

    passDesc->release();
    pool->release();
    return true;
}

void EngineMTLBootstrap::DrawTriangles2D(const Vertex2DMTL* verts, int vertCount, const uint16_t* indices,
                                         int indexCount, int textureHandle, int clipX, int clipY, int clipW,
                                         int clipH)
{
    if (_impl->currentEncoder == nullptr || vertCount <= 0 || indexCount <= 0)
        return;

    // Explicit rebind, not inherited from BeginFrame's initial bind -- a
    // DrawSectionTL call earlier in this same encoder would otherwise leave
    // the mesh pipeline/depth-test state bound for this 2D draw.
    _impl->currentEncoder->setRenderPipelineState(_impl->pipelineState);
    _impl->currentEncoder->setDepthStencilState(_impl->depthStateDisabled);

    // Clamp to the drawable -- Metal's setScissorRect raises a validation
    // error if the rect extends past the render target.
    int x0 = clipX < 0 ? 0 : clipX;
    int y0 = clipY < 0 ? 0 : clipY;
    int x1 = clipX + clipW;
    int y1 = clipY + clipH;
    if (x1 > _impl->drawableWidth)
        x1 = _impl->drawableWidth;
    if (y1 > _impl->drawableHeight)
        y1 = _impl->drawableHeight;
    if (x1 <= x0 || y1 <= y0)
        return; // fully clipped

    MTL::ScissorRect scissor;
    scissor.x = static_cast<NS::UInteger>(x0);
    scissor.y = static_cast<NS::UInteger>(y0);
    scissor.width = static_cast<NS::UInteger>(x1 - x0);
    scissor.height = static_cast<NS::UInteger>(y1 - y0);
    _impl->currentEncoder->setScissorRect(scissor);

    MTL::Buffer* vbuf = _impl->device->newBuffer(verts, static_cast<NS::UInteger>(vertCount) * sizeof(Vertex2DMTL),
                                                 MTL::ResourceStorageModeShared);
    MTL::Buffer* ibuf = _impl->device->newBuffer(indices, static_cast<NS::UInteger>(indexCount) * sizeof(uint16_t),
                                                 MTL::ResourceStorageModeShared);

    MTL::Texture* tex = _impl->fallbackWhite;
    if (textureHandle > 0 && static_cast<size_t>(textureHandle) <= _impl->textures.size())
    {
        MTL::Texture* found = _impl->textures[textureHandle - 1];
        if (found != nullptr)
            tex = found;
    }

    _impl->currentEncoder->setVertexBuffer(vbuf, 0, 0);
    _impl->currentEncoder->setFragmentTexture(tex, 0);
    _impl->currentEncoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, static_cast<NS::UInteger>(indexCount),
                                                 MTL::IndexTypeUInt16, ibuf, 0);

    vbuf->release();
    ibuf->release();
}

void EngineMTLBootstrap::EndFrame()
{
    if (_impl->currentEncoder == nullptr)
        return;

    // Local pool just for incidental temporaries -- see BeginFrame()'s
    // comment. The encoder/command buffer/drawable were explicitly
    // retain()'d when stored, so release them explicitly here too, after
    // they're done being used (endEncoding/presentDrawable/commit), rather
    // than relying on any pool's drain timing.
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

    _impl->currentEncoder->endEncoding();

    _impl->currentCommandBuffer->presentDrawable(_impl->currentDrawable);
    _impl->currentCommandBuffer->commit();

    _impl->currentEncoder->release();
    _impl->currentCommandBuffer->release();
    _impl->currentDrawable->release();

    _impl->currentEncoder = nullptr;
    _impl->currentCommandBuffer = nullptr;
    _impl->currentDrawable = nullptr;
    _impl->frameHadColorClear = false;

    pool->release();
}

int EngineMTLBootstrap::CreateMeshBuffer(const void* data, size_t byteSize, bool dynamic)
{
    if (_impl->device == nullptr || data == nullptr || byteSize == 0)
        return 0;

    MTL::Buffer* buf = _impl->device->newBuffer(data, static_cast<NS::UInteger>(byteSize),
                                                MTL::ResourceStorageModeShared);
    if (buf == nullptr)
        return 0;
    (void)dynamic; // storage mode is the same either way; kept for caller-side bookkeeping only

    _impl->meshBuffers.push_back(buf);
    return static_cast<int>(_impl->meshBuffers.size());
}

void EngineMTLBootstrap::UpdateMeshBuffer(int handle, const void* data, size_t byteSize)
{
    if (handle <= 0 || static_cast<size_t>(handle) > _impl->meshBuffers.size() || data == nullptr)
        return;
    MTL::Buffer* buf = _impl->meshBuffers[handle - 1];
    if (buf == nullptr || byteSize > buf->length())
        return;
    std::memcpy(buf->contents(), data, byteSize);
}

void EngineMTLBootstrap::DestroyMeshBuffer(int handle)
{
    if (handle <= 0 || static_cast<size_t>(handle) > _impl->meshBuffers.size())
        return;
    MTL::Buffer*& slot = _impl->meshBuffers[handle - 1];
    if (slot != nullptr)
    {
        slot->release();
        slot = nullptr;
    }
}

void EngineMTLBootstrap::DrawSectionTL(int vertexBufferHandle, int indexBufferHandle, int firstIndex, int indexCount,
                                       int textureHandle, const ObjectConstantsMTL& obj, const FrameConstantsMTL& frame)
{
    if (_impl->currentEncoder == nullptr || indexCount <= 0)
        return;

    EnsureTLPipeline();
    if (_impl->pipelineStateTL == nullptr)
        return;

    if (vertexBufferHandle <= 0 || static_cast<size_t>(vertexBufferHandle) > _impl->meshBuffers.size())
        return;
    if (indexBufferHandle <= 0 || static_cast<size_t>(indexBufferHandle) > _impl->meshBuffers.size())
        return;
    MTL::Buffer* vbuf = _impl->meshBuffers[vertexBufferHandle - 1];
    MTL::Buffer* ibuf = _impl->meshBuffers[indexBufferHandle - 1];
    if (vbuf == nullptr || ibuf == nullptr)
        return;

    MTL::Texture* tex = _impl->fallbackWhite;
    if (textureHandle > 0 && static_cast<size_t>(textureHandle) <= _impl->textures.size())
    {
        MTL::Texture* found = _impl->textures[textureHandle - 1];
        if (found != nullptr)
            tex = found;
    }

    // Explicit rebind -- see DrawTriangles2D's matching comment: draw order
    // between the two paths within one encoder is not guaranteed.
    _impl->currentEncoder->setRenderPipelineState(_impl->pipelineStateTL);
    _impl->currentEncoder->setDepthStencilState(_impl->depthStateTL);
    // Metal's cull mode defaults to None (draw both faces) and was never set
    // anywhere in this backend -- meshes with closed/solid hulls (e.g. the
    // M113's interior) showed their back-facing interior walls through gaps
    // since nothing was hiding them. Matches GL33's default convention
    // (EngineGL33_Queue.cpp: cull::Back() + cull::FrontFaceCW()).
    _impl->currentEncoder->setCullMode(MTL::CullModeBack);
    _impl->currentEncoder->setFrontFacingWinding(MTL::WindingClockwise);
    // 3D content draws full-frame -- reset the scissor rect in case an
    // earlier 2D draw in this encoder left a smaller clip rect active.
    MTL::ScissorRect scissor;
    scissor.x = 0;
    scissor.y = 0;
    scissor.width = static_cast<NS::UInteger>(_impl->drawableWidth);
    scissor.height = static_cast<NS::UInteger>(_impl->drawableHeight);
    _impl->currentEncoder->setScissorRect(scissor);

    _impl->currentEncoder->setVertexBuffer(vbuf, 0, 0);
    _impl->currentEncoder->setVertexBytes(&obj, sizeof(obj), 1);
    _impl->currentEncoder->setVertexBytes(&frame, sizeof(frame), 2);
    _impl->currentEncoder->setFragmentBytes(&frame, sizeof(frame), 0);
    _impl->currentEncoder->setFragmentTexture(tex, 0);

    const NS::UInteger offsetBytes = static_cast<NS::UInteger>(firstIndex) * sizeof(uint16_t);
    _impl->currentEncoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, static_cast<NS::UInteger>(indexCount),
                                                 MTL::IndexTypeUInt16, ibuf, offsetBytes);
}

int EngineMTLBootstrap::CreateTexture(int width, int height, const uint8_t* rgba)
{
    if (_impl->device == nullptr || width <= 0 || height <= 0 || rgba == nullptr)
        return 0;

    MTL::TextureDescriptor* desc =
        MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, static_cast<NS::UInteger>(width),
                                                    static_cast<NS::UInteger>(height), false);
    desc->setUsage(MTL::TextureUsageShaderRead);
    desc->setStorageMode(MTL::StorageModeShared);

    MTL::Texture* tex = _impl->device->newTexture(desc);
    desc->release();
    if (tex == nullptr)
        return 0;

    MTL::Region region = MTL::Region::Make2D(0, 0, static_cast<NS::UInteger>(width), static_cast<NS::UInteger>(height));
    tex->replaceRegion(region, 0, rgba, static_cast<NS::UInteger>(width) * 4);

    _impl->textures.push_back(tex);
    return static_cast<int>(_impl->textures.size());
}

void EngineMTLBootstrap::UpdateTexture(int handle, int width, int height, const uint8_t* rgba)
{
    if (handle <= 0 || static_cast<size_t>(handle) > _impl->textures.size() || rgba == nullptr)
        return;
    MTL::Texture* tex = _impl->textures[handle - 1];
    if (tex == nullptr)
        return;

    MTL::Region region = MTL::Region::Make2D(0, 0, static_cast<NS::UInteger>(width), static_cast<NS::UInteger>(height));
    tex->replaceRegion(region, 0, rgba, static_cast<NS::UInteger>(width) * 4);
}

void EngineMTLBootstrap::DestroyTexture(int handle)
{
    if (handle <= 0 || static_cast<size_t>(handle) > _impl->textures.size())
        return;
    MTL::Texture*& slot = _impl->textures[handle - 1];
    if (slot != nullptr)
    {
        slot->release();
        slot = nullptr;
    }
}

void EngineMTLBootstrap::Shutdown()
{
    for (MTL::Texture*& tex : _impl->textures)
    {
        if (tex != nullptr)
        {
            tex->release();
            tex = nullptr;
        }
    }
    _impl->textures.clear();
    for (MTL::Buffer*& buf : _impl->meshBuffers)
    {
        if (buf != nullptr)
        {
            buf->release();
            buf = nullptr;
        }
    }
    _impl->meshBuffers.clear();
    if (_impl->fallbackWhite != nullptr)
    {
        _impl->fallbackWhite->release();
        _impl->fallbackWhite = nullptr;
    }
    if (_impl->samplerState != nullptr)
    {
        _impl->samplerState->release();
        _impl->samplerState = nullptr;
    }
    if (_impl->pipelineState != nullptr)
    {
        _impl->pipelineState->release();
        _impl->pipelineState = nullptr;
    }
    if (_impl->pipelineStateTL != nullptr)
    {
        _impl->pipelineStateTL->release();
        _impl->pipelineStateTL = nullptr;
    }
    if (_impl->depthStateTL != nullptr)
    {
        _impl->depthStateTL->release();
        _impl->depthStateTL = nullptr;
    }
    if (_impl->depthStateDisabled != nullptr)
    {
        _impl->depthStateDisabled->release();
        _impl->depthStateDisabled = nullptr;
    }
    if (_impl->depthTexture != nullptr)
    {
        _impl->depthTexture->release();
        _impl->depthTexture = nullptr;
    }
    if (_impl->commandQueue != nullptr)
    {
        _impl->commandQueue->release();
        _impl->commandQueue = nullptr;
    }
    if (_impl->device != nullptr)
    {
        _impl->device->release();
        _impl->device = nullptr;
    }
    _impl->layer = nullptr; // owned by the SDL_MetalView, not us

    if (_impl->metalView != nullptr)
    {
        SDL_Metal_DestroyView(_impl->metalView);
        _impl->metalView = nullptr;
    }
    // Only destroy the window if Init() created it. AttachToWindow() callers
    // (EngineMTL) own their own window's lifecycle.
    if (_ownsWindow && _window != nullptr)
    {
        SDL_DestroyWindow(_window);
    }
    _window = nullptr;
}

} // namespace Poseidon
