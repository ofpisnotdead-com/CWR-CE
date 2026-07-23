#ifdef _MSC_VER
#pragma once
#endif

#ifndef __RENDERSTATE_HPP
#define __RENDERSTATE_HPP

#include <Poseidon/Graphics/Core/MatrixConversion.hpp>
#include <Poseidon/Core/Types.hpp>
#include <Poseidon/Graphics/Rendering/RenderFlags.hpp>
#include <cstdint>

// Per-frame camera/environment state — changes once per frame.

namespace Poseidon
{
struct FrameState
{
    GfxMatrix view = {};           // rotation only (translation zeroed for camera-relative)
    GfxMatrix projection = {};     // base projection (before bias)
    GfxMatrix viewProjection = {}; // view * projection (precomputed)
    float cameraPos[3] = {};       // world-space camera position (for camera-relative offset)
    float viewport[4] = {};        // x, y, width, height
    float fogParams[4] = {};       // start, invRange, enabled, 0
    float fogColor[4] = {};        // rgb, a=1
    float sunDir[4] = {};          // sun direction (xyz, w=0)
    float nightEffect = 0.0f;      // local-light strength (0 = day, 1 = night)
    bool sunEnabled = false;
};

enum class BlendModeV4
{
    Opaque,
    AlphaBlend,
    Additive,
    Shadow
};

enum class DepthModeV4
{
    Normal,
    ReadOnly,
    Disabled,
    Shadow
};

enum class FogMode
{
    Enabled,
    Disabled
};

enum class PassId
{
    Opaque,
    Cutout,
    Transparent,
    Shadow,
    Light,
    OnSurface,
    Cockpit,
    Sky,
    Water,
    ScreenSpace
};

// Stable label for a PassId — used for GL_KHR_debug pass groups so
// RenderDoc / Nsight captures show the pass structure around the draws.
constexpr const char* PassIdName(PassId id)
{
    switch (id)
    {
        case PassId::Opaque:
            return "Opaque";
        case PassId::Cutout:
            return "Cutout";
        case PassId::Transparent:
            return "Transparent";
        case PassId::Shadow:
            return "Shadow";
        case PassId::Light:
            return "Light";
        case PassId::OnSurface:
            return "OnSurface";
        case PassId::Cockpit:
            return "Cockpit";
        case PassId::Sky:
            return "Sky";
        case PassId::Water:
            return "Water";
        case PassId::ScreenSpace:
            return "ScreenSpace";
    }
    return "Unknown";
}

// Per-pass render state — changes per render pass.
struct PassState
{
    GfxMatrix projection = {}; // projection variant (may differ per pass)
    DepthModeV4 depthMode = DepthModeV4::Normal;
    BlendModeV4 blendMode = BlendModeV4::Opaque;
    FogMode fogMode = FogMode::Enabled;
    int shaderPipeline = 0; // vertex/pixel shader combination ID
    uint32_t passFlags = 0; // additional pass-specific flags
};

// Per-object draw submission — changes per submitted object.
struct DrawItem
{
    GfxMatrix worldMatrix = {}; // camera-relative world transform
    PassId passId = PassId::Opaque;
    int bias = 0;                      // z-buffer bias
    render::LegacySpec specFlags = {}; // typed per-object routing / material / backend bits
    void* texture = nullptr;           // Texture* (backend-specific)
    int textureLevel = 0;              // mip level
    int sectionBegin = 0;              // first section index
    int sectionEnd = 0;                // past-last section index
    // Resolved index-buffer range — what `glDrawElements` actually
    // receives.  `firstIndex` is the index-buffer-relative first
    // index (i.e. `_sections[sectionBegin].beg`); `indexCount` is
    // `_sections[sectionEnd-1].end - _sections[sectionBegin].beg`.
    // Captured at TL draw time so the frame layer doesn't need to walk the
    // backend's section table to translate.
    int firstIndex = 0;
    int indexCount = 0;
    void* vertexBuffer = nullptr; // mesh/vertex buffer reference
    bool isTLDraw = false;        // true = DrawSectionTL path, false = queued path
    // Backend-specific opaque mesh handle (GL: VAO id; D3D: pointer cast).
    // Captured at draw time so the frame layer's SceneExtractor can resolve the mesh
    // to a typed `render::frame::MeshHandle.vao` without dereferencing
    // `vertexBuffer` or knowing the backend's concrete buffer class.
    std::uint32_t backendMeshHandle = 0;
    // Backend-specific texture handle for TEXTURE0 (GL: texture id).
    // Captured when the backend's `SetTexture` rebinds, before the
    // next TL draw observes the snapshot.  the frame layer's SceneExtractor lands
    // it in `SceneDraw.textures[0]` so the frame layer's `EmitDraw` can rebind the
    // same texture the legacy path used.
    std::uint32_t backendTextureHandle = 0;
    // Backend-specific texture handle for TEXTURE1 (multi-texture
    // slot: detail / grass / specular).  Tracked across the legacy
    // `SetMultiTexturing` early-out so the captured DrawItem always
    // has the *currently bound* multi-tex, not just the most
    // recently rebound one.  the frame layer's EmitDraw maps this to
    // `SceneDraw.textures[1]` and rebinds TEXTURE1.
    std::uint32_t backendTexture1Handle = 0;
};

// Map backend spec to PassId.  Pass selection depends entirely on Backend
// bits (blend mode, depth, alpha, shadow / water family hints); Routing
// and Material don't influence which pass bucket a draw lands in.
inline PassId SpecToPassId(const render::LegacySpec& spec)
{
    const render::Backend backend = spec.backend;
    if (render::Has(backend, render::Backend::IsShadow))
        return PassId::Shadow;
    if (render::Has(backend, render::Backend::IsLight))
        return PassId::Light;
    if (render::Has(backend, render::Backend::IsWater))
        return PassId::Water;
    if (render::Has(backend, render::Backend::IsAlphaFog))
        return PassId::Transparent;
    if (render::Has(backend, render::Backend::IsAlpha))
        return PassId::Transparent;
    if (render::Has(backend, render::Backend::IsTransparent))
        return PassId::Cutout;
    if (render::Has(backend, render::Backend::NoZBuf))
        return PassId::Sky;
    if (render::Has(backend, render::Backend::NoZWrite))
        return PassId::Transparent;
    return PassId::Opaque;
}

// Legacy int overload — forwards to the typed version via SplitLegacy.
// Kept while non-migrated call sites still hand off raw ints.
inline PassId SpecToPassId(int specFlags)
{
    return SpecToPassId(render::SplitLegacy(specFlags));
}

} // namespace Poseidon
#endif
