#include <PoseidonMTL/EngineMTL.hpp>

#include <SDL3/SDL.h>

#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Dev/Debug/DebugOverlay.hpp>
#include <Poseidon/Graphics/Shared/WindowPlacement.hpp>
#include <Poseidon/Graphics/Core/TLVertex.hpp>
#include <Poseidon/Graphics/Core/MatrixConversion.hpp>
#include <Poseidon/Graphics/Rendering/BuildRenderPassDescriptor.hpp>
#include <Poseidon/Graphics/Rendering/Shape/ClipShape.hpp>
#include <Poseidon/Graphics/Rendering/Shape/Shape.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <PoseidonMTL/TextBankMTL.hpp>
#include <PoseidonMTL/TextureMTL.hpp>
#include <PoseidonMTL/VertexBufferMTL.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace Poseidon
{

namespace
{
int GpuHandleOf(Texture* tex)
{
    TextureMTL* mtlTex = dynamic_cast<TextureMTL*>(tex);
    return mtlTex ? mtlTex->GpuHandle() : 0;
}
} // namespace

EngineMTL::EngineMTL(int width, int height, bool windowed, int bpp)
{
    _w = width;
    _h = height;
    _windowed = windowed;
    _windowedRestoreW = width;
    _windowedRestoreH = height;
    _pixelSize = bpp;
    _refreshRate = 60;

    LOG_INFO(Graphics, "MTL: Initializing engine — bootstrap {}x{} {}bpp {}", _w, _h, _pixelSize,
             _windowed ? "windowed" : "fullscreen");

    CreateWindowAndDevice();

    _textBank = new TextBankMTL(&_bootstrap);
}

void EngineMTL::CreateWindowAndDevice()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        LOG_ERROR(Graphics, "MTL: SDL_Init failed: {}", SDL_GetError());
        return;
    }

    // Resolve final placement the same way GL33 does — keeps the
    // "borderless covers monitor" / windowed-vs-fullscreen rules in one
    // shared resolver instead of duplicating them per backend.
    auto& engineCfg = GApp->GetConfig().GetEngineConfig();
    DisplayPlacementInput displayCfg;
    displayCfg.displayMode = engineCfg.displayMode;
    if (_windowed && displayCfg.displayMode != "windowed")
        displayCfg.displayMode = "windowed";
    if (!_windowed && displayCfg.displayMode == "windowed")
        displayCfg.displayMode = "borderless";
    displayCfg.width = _w;
    displayCfg.height = _h;

    int desktopW = 0, desktopH = 0, desktopRefresh = 0;
    if (const SDL_DisplayMode* dm = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay()))
    {
        desktopW = dm->w;
        desktopH = dm->h;
        desktopRefresh = (int)(dm->refresh_rate + 0.5f);
    }
    const WindowPlacement placement = ResolveWindowPlacement(displayCfg, desktopW, desktopH, desktopRefresh);
    _windowMode = placement.mode;

    Uint32 flags = SDL_WINDOW_METAL | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#ifdef POSEIDON_TARGET_IOS
    flags |= SDL_WINDOW_FULLSCREEN | SDL_WINDOW_BORDERLESS;
    _windowMode = WindowMode::Fullscreen;
    _windowed = false;
#else
    switch (placement.mode)
    {
        case WindowMode::Fullscreen:
        case WindowMode::Borderless:
            flags |= SDL_WINDOW_BORDERLESS;
            break;
        case WindowMode::Windowed:
            flags |= SDL_WINDOW_RESIZABLE;
            break;
    }
#endif

    _sdlWindow = SDL_CreateWindow("Poseidon [Metal]", placement.width, placement.height, flags);
    if (!_sdlWindow)
    {
        LOG_ERROR(Graphics, "MTL: SDL_CreateWindow failed: {}", SDL_GetError());
        return;
    }

#ifdef POSEIDON_TARGET_IOS
    if (!SDL_SetWindowFullscreen(_sdlWindow, true))
        LOG_WARN(Graphics, "MTL: SDL_SetWindowFullscreen(true) failed for iOS startup: {}", SDL_GetError());
#else
    if (placement.mode == WindowMode::Borderless)
    {
        SDL_SetWindowFullscreenMode(_sdlWindow, nullptr);
        if (!SDL_SetWindowFullscreen(_sdlWindow, true))
            LOG_WARN(Graphics, "MTL: SDL_SetWindowFullscreen(true) failed for borderless startup: {}", SDL_GetError());
    }
    else if (placement.posX != WindowPlacement::kCentered)
    {
        SDL_SetWindowPosition(_sdlWindow, placement.posX, placement.posY);
    }
#endif

    if (placement.refreshHz > 0)
        _refreshRate = placement.refreshHz;

    if (!_bootstrap.AttachToWindow(_sdlWindow))
    {
        LOG_ERROR(Graphics, "MTL: EngineMTLBootstrap::AttachToWindow failed");
        return;
    }

    int cw = 0, ch = 0;
    SDL_GetWindowSizeInPixels(_sdlWindow, &cw, &ch);
    _w = cw;
    _h = ch;

    LOG_INFO(Graphics, "MTL: Metal device — {}", _bootstrap.GetRendererName().c_str());
    LOG_INFO(Graphics, "MTL: surface resolved to {}x{} {}", _w, _h, _windowed ? "windowed" : "fullscreen");

    Dev::DebugOverlay::InitForMetal(_sdlWindow);
    if (!_bootstrap.InitDebugOverlayRenderer())
        LOG_WARN(Graphics, "MTL: DebugOverlay Metal renderer init failed");

    // Hook SDL events to the engine — same helper GL33 uses, input
    // forwarding/focus/Alt+Enter logic is identical across backends.
    _eventWindow.Attach(_sdlWindow, _w, _h);

    LoadConfig();
}

EngineMTL::~EngineMTL()
{
    LOG_INFO(Graphics, "MTL: Destroying engine");
    SaveConfig();

    delete _textBank;
    _textBank = nullptr;

    _bootstrap.ShutdownDebugOverlayRenderer();
    Dev::DebugOverlay::Shutdown();

    _bootstrap.Shutdown(); // releases device/queue/layer; AttachToWindow means it does NOT own _sdlWindow

    if (_sdlWindow)
    {
        SDL_DestroyWindow(_sdlWindow);
        _sdlWindow = nullptr;
    }
}

void EngineMTL::Clear(bool clearZ, bool clear, PackedColor color)
{
    bool began = _bootstrap.BeginFrame(color.R8() / 255.0f, color.G8() / 255.0f, color.B8() / 255.0f,
                                       color.A8() / 255.0f, clear, clearZ);
    if (!began)
        LOG_WARN(Graphics, "MTL: Clear's BeginFrame failed (no drawable -- window minimized/occluded?)");
}

void EngineMTL::InitDraw(bool clear, PackedColor color)
{
    if (_frameOpen)
    {
        LOG_DEBUG(Graphics, "MTL: InitDraw done twice");
        return;
    }

    bool began = _bootstrap.BeginFrame(color.R8() / 255.0f, color.G8() / 255.0f, color.B8() / 255.0f,
                                       color.A8() / 255.0f, clear, /*clearZ=*/true);
    if (!began)
    {
        LOG_WARN(Graphics, "MTL: InitDraw's BeginFrame failed (no drawable -- window minimized/occluded?)");
        return;
    }

    if (_textBank)
        _textBank->StartFrame();

    _bootstrap.BeginDebugOverlayFrame();
    Dev::DebugOverlay::NewFrame();

    Engine::InitDraw(clear, color);
    _frameOpen = true;
}

void EngineMTL::FinishDraw()
{
    if (!_frameOpen)
        return;

    Engine::FinishDraw();
    DrawFinishTexts();
    Dev::DebugOverlay::Render();
    _bootstrap.RenderDebugOverlay();

    if (_textBank)
        _textBank->FinishFrame();

    _frameOpen = false;
}

void EngineMTL::NextFrame()
{
    _bootstrap.EndFrame();
    Engine::NextFrame();
}

void EngineMTL::PixelToNDC(float px, float py, float& ndcX, float& ndcY) const
{
    ndcX = _w > 0 ? (px / static_cast<float>(_w)) * 2.0f - 1.0f : 0.0f;
    // Pixel Y grows downward (origin top-left); NDC Y grows upward.
    ndcY = _h > 0 ? 1.0f - (py / static_cast<float>(_h)) * 2.0f : 0.0f;
}

void EngineMTL::DrawFan2D(const float* xy, const float* z, const float* rhw, const float* uv, const float* uv1,
                          const PackedColor* colors, int n, int textureHandle, int secondaryTextureHandle,
                          const Rect2DAbs& clip, render::DepthMode depthMode, render::BlendMode blendMode,
                          render::SamplerMode sampler, render::SurfaceMode surface, render::ShaderFamily shader,
                          const PackedColor* specular, float detailMode)
{
    if (n < 3 || n > kMaxPolyVerts)
        return;

    Vertex2DMTL verts[kMaxPolyVerts];
    for (int i = 0; i < n; i++)
    {
        float ndcX = 0.0f;
        float ndcY = 0.0f;
        PixelToNDC(xy[i * 2], xy[i * 2 + 1], ndcX, ndcY);
        const float clipW = (rhw && rhw[i] != 0.0f) ? (1.0f / rhw[i]) : 1.0f;
        verts[i].x = ndcX * clipW;
        verts[i].y = ndcY * clipW;
        verts[i].z = (z ? z[i] : 0.0f) * clipW;
        verts[i].w = clipW;
        verts[i].u = uv[i * 2];
        verts[i].v = uv[i * 2 + 1];
        // GL33's vsScreen: `vFogTC = aSpecular.a` -- same convention here.
        verts[i].fogTC = specular ? (specular[i].A8() / 255.0f) : 1.0f;
        verts[i].detailMode = detailMode;
        verts[i].r = colors[i].R8() / 255.0f;
        verts[i].g = colors[i].G8() / 255.0f;
        verts[i].b = colors[i].B8() / 255.0f;
        verts[i].a = colors[i].A8() / 255.0f;
        verts[i].u1 = uv1 ? uv1[i * 2] : verts[i].u;
        verts[i].v1 = uv1 ? uv1[i * 2 + 1] : verts[i].v;
        verts[i].pad0 = 0.0f;
        verts[i].pad1 = 0.0f;
    }

    uint16_t indices[(kMaxPolyVerts - 2) * 3];
    int idxCount = 0;
    for (int i = 1; i < n - 1; i++)
    {
        indices[idxCount++] = 0;
        indices[idxCount++] = static_cast<uint16_t>(i);
        indices[idxCount++] = static_cast<uint16_t>(i + 1);
    }

    const float fogColor[3] = {_fogColor.R(), _fogColor.G(), _fogColor.B()};
    _bootstrap.DrawTriangles2D(verts, n, indices, idxCount, textureHandle, secondaryTextureHandle,
                               static_cast<int>(clip.x),
                               static_cast<int>(clip.y), static_cast<int>(clip.w), static_cast<int>(clip.h),
                               z != nullptr, depthMode, blendMode, sampler, surface, shader, fogColor);
}

void EngineMTL::Draw2D(const Draw2DPars& pars, const Rect2DAbs& rect, const Rect2DAbs& clip)
{
    if (!pars.mip.IsOK())
        return;

    const float xy[8] = {
        rect.x,          rect.y,          rect.x + rect.w, rect.y,
        rect.x + rect.w, rect.y + rect.h, rect.x,          rect.y + rect.h,
    };
    const float uv[8] = {
        pars.uTL, pars.vTL, pars.uTR, pars.vTR, pars.uBR, pars.vBR, pars.uBL, pars.vBL,
    };
    const PackedColor colors[4] = {pars.colorTL, pars.colorTR, pars.colorBR, pars.colorBL};

    const render::RenderPassDescriptor d = render::BuildRenderPassDescriptor(render::SplitLegacy(pars.spec));
    DrawFan2D(xy, nullptr, nullptr, uv, nullptr, colors, 4, GpuHandleOf(pars.mip._texture), 0, clip, d.depth, d.blend,
              d.sampler, d.surface, d.shader);
}

void EngineMTL::DrawPoly(const MipInfo& mip, const Vertex2DAbs* vertices, int n, const Rect2DAbs& clip,
                         int specFlags)
{
    if (n < 3 || n > kMaxPolyVerts)
        return;

    float xy[kMaxPolyVerts * 2];
    float uv[kMaxPolyVerts * 2];
    PackedColor colors[kMaxPolyVerts];
    for (int i = 0; i < n; i++)
    {
        xy[i * 2] = vertices[i].x;
        xy[i * 2 + 1] = vertices[i].y;
        uv[i * 2] = vertices[i].u;
        uv[i * 2 + 1] = vertices[i].v;
        colors[i] = vertices[i].color;
    }

    const render::RenderPassDescriptor d = render::BuildRenderPassDescriptor(render::SplitLegacy(specFlags));
    DrawFan2D(xy, nullptr, nullptr, uv, nullptr, colors, n, mip.IsOK() ? GpuHandleOf(mip._texture) : 0, 0, clip,
              d.depth, d.blend, d.sampler, d.surface, d.shader);
}

void EngineMTL::DrawPoly(const MipInfo& mip, const Vertex2DPixel* vertices, int n, const Rect2DPixel& clip,
                         int specFlags)
{
    if (n < 3 || n > kMaxPolyVerts)
        return;

    const float x2d = Left2D();
    const float y2d = Top2D();

    float xy[kMaxPolyVerts * 2];
    float uv[kMaxPolyVerts * 2];
    PackedColor colors[kMaxPolyVerts];
    for (int i = 0; i < n; i++)
    {
        xy[i * 2] = vertices[i].x + x2d;
        xy[i * 2 + 1] = vertices[i].y + y2d;
        uv[i * 2] = vertices[i].u;
        uv[i * 2 + 1] = vertices[i].v;
        colors[i] = vertices[i].color;
    }

    Rect2DAbs clipAbs;
    Convert(clipAbs, clip);
    const render::RenderPassDescriptor d = render::BuildRenderPassDescriptor(render::SplitLegacy(specFlags));
    DrawFan2D(xy, nullptr, nullptr, uv, nullptr, colors, n, mip.IsOK() ? GpuHandleOf(mip._texture) : 0, 0, clipAbs, d.depth,
              d.blend, d.sampler, d.surface, d.shader);
}

void EngineMTL::DrawDecal(Vector3Par screen, float /*rhw*/, float sizeX, float sizeY, PackedColor color,
                          const MipInfo& mip, int specFlags)
{
    if (!mip.IsOK())
        return;

    float xBeg = screen.X() - sizeX;
    float xEnd = screen.X() + sizeX;
    float yBeg = screen.Y() - sizeY;
    float yEnd = screen.Y() + sizeY;
    float uBeg = 0.0f;
    float vBeg = 0.0f;
    float uEnd = 1.0f;
    float vEnd = 1.0f;

    if (xBeg < 0.0f)
    {
        uBeg = -xBeg / (2.0f * sizeX);
        xBeg = 0.0f;
    }
    if (xEnd > static_cast<float>(_w))
    {
        uEnd = 1.0f - (xEnd - static_cast<float>(_w)) / (2.0f * sizeX);
        xEnd = static_cast<float>(_w);
    }
    if (yBeg < 0.0f)
    {
        vBeg = -yBeg / (2.0f * sizeY);
        yBeg = 0.0f;
    }
    if (yEnd > static_cast<float>(_h))
    {
        vEnd = 1.0f - (yEnd - static_cast<float>(_h)) / (2.0f * sizeY);
        yEnd = static_cast<float>(_h);
    }
    if (xBeg >= xEnd || yBeg >= yEnd)
        return;

    const float xy[8] = {xBeg, yBeg, xEnd, yBeg, xEnd, yEnd, xBeg, yEnd};
    const float uv[8] = {uBeg, vBeg, uEnd, vBeg, uEnd, vEnd, uBeg, vEnd};
    const PackedColor colors[4] = {color, color, color, color};
    const Rect2DAbs clip(0, 0, static_cast<float>(_w), static_cast<float>(_h));

    const render::RenderPassDescriptor d = render::BuildRenderPassDescriptor(render::SplitLegacy(specFlags));
    DrawFan2D(xy, nullptr, nullptr, uv, nullptr, colors, 4, GpuHandleOf(mip._texture), 0, clip, d.depth, d.blend,
              d.sampler, d.surface, d.shader);
}

void EngineMTL::DrawLine(const Line2DAbs& line, PackedColor c0, PackedColor c1, const Rect2DAbs& clip)
{
    // Solid colored quad approximating the line -- GL33 samples a dedicated
    // soft-edged line texture (EngineGL33_2D.cpp:112) for antialiasing; this
    // is a deliberately simpler stand-in (untextured, fallback-white quad).
    const float x0 = line.beg.x, y0 = line.beg.y;
    const float x1 = line.end.x, y1 = line.end.y;
    const float dx = x1 - x0, dy = y1 - y0;
    const float lenSq = dx * dx + dy * dy;
    const float invLen = lenSq > 0 ? 1.0f / std::sqrt(lenSq) : 1.0f;
    const float pdx = dy * invLen, pdy = -dx * invLen;
    const float halfW = 1.5f; // ~3px wide

    const float xy[8] = {
        x0 - pdx * halfW, y0 - pdy * halfW, x1 - pdx * halfW, y1 - pdy * halfW,
        x1 + pdx * halfW, y1 + pdy * halfW, x0 + pdx * halfW, y0 + pdy * halfW,
    };
    const float uv[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    const PackedColor colors[4] = {c0, c0, c1, c1};

    DrawFan2D(xy, nullptr, nullptr, uv, nullptr, colors, 4, 0, 0, clip);
}

void EngineMTL::DrawIndexedFan3D(const VertexIndex* indices, int n)
{
    if (_mesh == nullptr || n < 3 || n > kMaxPolyVerts)
        return;

    float xy[kMaxPolyVerts * 2];
    float z[kMaxPolyVerts];
    float rhw[kMaxPolyVerts];
    float uv[kMaxPolyVerts * 2];
    float uv1[kMaxPolyVerts * 2];
    PackedColor colors[kMaxPolyVerts];
    PackedColor specular[kMaxPolyVerts];
    for (int k = 0; k < n; k++)
    {
        const TLVertex& v = _mesh->GetVertex(indices[k]);
        xy[k * 2] = v.pos.X();
        xy[k * 2 + 1] = v.pos.Y();
        z[k] = v.pos.Z();
        rhw[k] = v.rhw;
        uv[k * 2] = v.t0.u;
        uv[k * 2 + 1] = v.t0.v;
        uv1[k * 2] = v.t1.u;
        uv1[k * 2 + 1] = v.t1.v;
        colors[k] = v.color;
        specular[k] = v.specular;
    }

    const Rect2DAbs fullScreen(0, 0, static_cast<float>(_w), static_cast<float>(_h));
    DrawFan2D(xy, z, rhw, uv, uv1, colors, n, _currentTriTexture, _currentTriSecondaryTexture, fullScreen,
              _currentTriDepthMode, _currentTriBlendMode, _currentTriSampler, _currentTriSurfaceMode,
              _currentTriShader, specular, _currentTriDetailMode);
}

// GL33's equivalent is the legacy/queued path's FlushQueue -> ApplyPassState
// -> BuildRenderPassDescriptor -> ApplyPipeline chain (EngineGL33_Queue.cpp):
// every draw, hardware-TL or legacy/software, resolves through the same
// descriptor builder so state (depth/blend/stencil) is decoupled from how the
// vertices get to the GPU. This was previously the gap that let shadow polys
// without a hardware vertex buffer (most real casters, see Shadow.cpp's
// Object::DrawShadow) bypass the TL path's stencil exclusion entirely --
// PrepareTriangle ignored specFlags outright.
void EngineMTL::PrepareTriangle(const MipInfo& mip, int specFlags)
{
    _currentTriTexture = mip.IsOK() ? GpuHandleOf(mip._texture) : 0;
    _currentTriSecondaryTexture = 0;
    _currentTriDetailMode = 0.0f;

    const render::LegacySpec spec = render::SplitLegacy(specFlags);
    render::BuildContext ctx;
    ctx.isIn3DPass = false;
    ctx.isMultitexturing = IsMultitexturing();
    const render::RenderPassDescriptor d = render::BuildRenderPassDescriptor(spec, ctx);
    _currentTriDepthMode = d.depth;
    _currentTriBlendMode = d.blend;
    _currentTriSampler = d.sampler;
    _currentTriSurfaceMode = d.surface;
    _currentTriShader = d.shader;

    if (d.shader == render::ShaderFamily::Detail || d.shader == render::ShaderFamily::Grass)
    {
        const bool isGrassTagged = render::Has(spec.backend, render::Backend::GrassTexture);
        const bool isDetailTagged = render::Has(spec.backend, render::Backend::DetailTexture);
        TextureMTL* secondary = nullptr;
        if (_textBank)
        {
            if (isGrassTagged)
                secondary = _textBank->GetGrassTexture();
            else if (isDetailTagged)
                secondary = _textBank->GetDetailTexture();
            else
                secondary = _textBank->GetSpecularTexture();
            if (secondary)
            {
                _textBank->UseMipmap(secondary, 0, 0);
                _currentTriSecondaryTexture = secondary->GpuHandle();
            }
        }
        if (isGrassTagged)
            _currentTriDetailMode = 2.0f;
        else if (isDetailTagged)
            _currentTriDetailMode = 1.0f;
    }
}

VertexBuffer* EngineMTL::CreateVertexBuffer(const Shape& src, VBType type)
{
    auto* buf = new VertexBufferMTL(_bootstrap);
    if (buf->Init(src, type))
        return buf;
    delete buf;
    return nullptr;
}

// GL33's UploadVSMaterialConstants (EngineGL33_Shaders.cpp): pre-combine the
// sun with the material on the CPU side, same formula -- ambient folds in
// `forcedDiffuse` (the difuse-to-ambient transfer used by half-lit special
// cases), diffuse is straight sun-diffuse * material-diffuse. The vertex
// shader (kShaderSourceMesh) just does NdotL*diffuse + ambient + emissive.
void EngineMTL::SetMaterial(const TLMaterial& mat, const LightList& lights, const render::LegacySpec& spec)
{
    if (GScene == nullptr)
        return;
    LightSun* sun = GScene->MainLight();
    if (sun == nullptr)
        return;

    const Color dif = sun->Diffuse() * mat.diffuse;
    const Color amb = sun->Ambient() * mat.ambient + sun->Diffuse() * mat.forcedDiffuse;

    _tlObject.ambient[0] = amb.R();
    _tlObject.ambient[1] = amb.G();
    _tlObject.ambient[2] = amb.B();
    _tlObject.ambient[3] = amb.A();
    _tlObject.diffuse[0] = dif.R();
    _tlObject.diffuse[1] = dif.G();
    _tlObject.diffuse[2] = dif.B();
    _tlObject.diffuse[3] = dif.A();
    _tlObject.emissive[0] = mat.emmisive.R();
    _tlObject.emissive[1] = mat.emmisive.G();
    _tlObject.emissive[2] = mat.emmisive.B();
    _tlObject.emissive[3] = mat.emmisive.A();

    // Local point/spot lights -- ported from GL33's UploadVSLights
    // (EngineGL33_Shaders.cpp). Night-gated exactly like GL33: local lights
    // (street lamps, vehicle headlights) only ever illuminate geometry once
    // the sun's NightEffect kicks in, except DisableSun materials (cockpit
    // interiors etc.), which the legacy SetupLights forces to full night
    // regardless of the actual time of day.
    float night = sun->NightEffect();
    if (render::Has(spec.material, render::Material::DisableSun))
        night = 1.0f;

    int n = 0;
    if (night > 0.0f && GScene->GetCamera() != nullptr)
    {
        const Vector3 camPos = GScene->GetCamera()->Position();
        const Color matDif = mat.diffuse * night;
        const Color matAmb = mat.ambient * night;
        for (int i = 0; i < lights.Size() && n < kMaxLocalLightsMTL; i++)
        {
            Light* light = lights[i];
            if (!light)
                continue;
            LightDescription desc;
            light->GetDescription(desc);
            const bool isSpot = desc.type == LTSpotLight;
            if (desc.type != LTPoint && !isSpot)
                continue; // point + spot lights; directional (sun) handled separately

            LightMTL& l = _tlObject.lights[n];
            // Camera-relative, matching the world matrix's convention (every
            // other position-like field in ObjectConstantsMTL is already in
            // this space).
            l.posAndAtten[0] = static_cast<float>(desc.pos.X() - camPos.X());
            l.posAndAtten[1] = static_cast<float>(desc.pos.Y() - camPos.Y());
            l.posAndAtten[2] = static_cast<float>(desc.pos.Z() - camPos.Z());
            l.posAndAtten[3] = desc.startAtten;

            Vector3 beam = desc.dir;
            beam.Normalize();
            l.dirAndIsSpot[0] = static_cast<float>(beam.X());
            l.dirAndIsSpot[1] = static_cast<float>(beam.Y());
            l.dirAndIsSpot[2] = static_cast<float>(beam.Z());
            l.dirAndIsSpot[3] = isSpot ? 1.0f : 0.0f;

            const Color ldif = desc.diffuse * matDif;
            l.diffuse[0] = ldif.R();
            l.diffuse[1] = ldif.G();
            l.diffuse[2] = ldif.B();
            l.diffuse[3] = 0.0f;

            const Color lamb = desc.ambient * matAmb;
            l.ambient[0] = lamb.R();
            l.ambient[1] = lamb.G();
            l.ambient[2] = lamb.B();
            l.ambient[3] = 0.0f;

            n++;
        }
    }
    _tlObject.lightCount[0] = static_cast<float>(n);

    // Specular: sun-direction-only (GL33 doesn't apply specular from local
    // lights either) -- EngineGL33::DoSetMaterial's SelectPixelShaderSpecular
    // split, ported as a uniform flag instead of a separate shader variant.
    const Color specCol = sun->Diffuse() * mat.specular;
    _tlObject.specular[0] = specCol.R();
    _tlObject.specular[1] = specCol.G();
    _tlObject.specular[2] = specCol.B();
    _tlObject.specular[3] = static_cast<float>(mat.specularPower);
    _tlObject.specEnabled[0] = mat.specularPower > 0 ? 1.0f : 0.0f;
}

// Per-section texture hook -- Shape::Draw (via ShapeSection::PrepareTL) calls
// this right before each DrawSectionTL, same role PrepareTriangle plays for
// the legacy path (GL33's equivalent is the SetTexture call PolyProperties::
// PrepareTL makes via PrepareTriangleTL).
void EngineMTL::PrepareTriangleTL(const MipInfo& mip, const render::LegacySpec& spec)
{
    _tlCurrentTexture = mip.IsOK() ? GpuHandleOf(mip._texture) : 0;
    // Runs after SetMaterial (Shape::Draw's PrepareTL call order), right
    // before DrawSectionTL consumes _tlObject/_tlSectionBlendMode -- see
    // fsMeshOpaque/fsMeshBlend's comments for why opaque, cutout and blend
    // textures each need different treatment.
    Texture* tex = mip.IsOK() ? mip._texture : nullptr;
    const bool isAlpha = tex && tex->IsAlpha();
    const bool isCutout = tex && tex->IsTransparent();
    _tlObject.flags[0] = isCutout ? 1.0f : 0.0f;
    _tlObject.flags[1] = 0.0f;
    _tlSecondaryTexture = 0;

    // Same mapping BuildRenderPassDescriptor.hpp uses for d.sampler -- no
    // competing signal to special-case around here (unlike blend mode's
    // texture-stat override above), so this is a direct, unconditional
    // mirror of the spec-bit -> SamplerMode translation.
    _tlSectionSampler.filter =
        render::Has(spec.backend, render::Backend::PointSampling) ? render::SamplerFilter::Point
                                                                    : render::SamplerFilter::Linear;
    _tlSectionSampler.clampU = render::Has(spec.backend, render::Backend::ClampU);
    _tlSectionSampler.clampV = render::Has(spec.backend, render::Backend::ClampV);
    // GL33's ApplyPassState always passes isIn3DPass=true and
    // isMultitexturing=IsMultitexturing() for the mesh/TL vertex input (see
    // EngineGL33_Queue.cpp:248-251) -- the omitted BuildContext here meant
    // d.shader could never resolve to Detail/Grass even when a section's
    // spec carried those bits, since the default BuildContext has
    // isMultitexturing=false.
    render::BuildContext ctx;
    ctx.isIn3DPass = true;
    ctx.isMultitexturing = IsMultitexturing();
    const render::RenderPassDescriptor d = render::BuildRenderPassDescriptor(spec, ctx);
    _tlSectionSurfaceMode = d.surface;
    _tlSectionShader = d.shader;
    if (d.shader == render::ShaderFamily::Detail)
    {
        // d.shader==Detail covers both DetailTexture- and SpecularTexture-
        // tagged sections (BuildRenderPassDescriptor.hpp's generic mtMask
        // branch collapses both to Detail when GrassTexture isn't set).
        // GL33's SetTexture (EngineGL33_State.cpp) re-checks the original
        // bits to pick which texture actually goes on unit 1 -- water tiles
        // carry SpecularTexture (LandscapeRender.cpp's WaterFlags), not
        // DetailTexture, and need the specular/bump texture there, not the
        // ground detail texture.
        const bool isDetailTagged = render::Has(spec.backend, render::Backend::DetailTexture);
        // Only flag true ground-Detail sections for fsMeshOpaque/Blend's
        // detail-multiply (baseLit * detailTex.a * 2) -- confirmed via a
        // magenta-marker test that water/SpecularTexture sections go through
        // this exact branch today (no dedicated water shader yet, see
        // EngineMTLBootstrap.cpp's Water/Detail/Grass pipeline TODO). That
        // formula treats the secondary texture's alpha as a ground-texture
        // brightness multiplier; for a bump/normal map (water's case) alpha
        // is unrelated and can crush the lit water color toward black. GL33's
        // dedicated PSWater shader instead adds a bump-driven specular
        // highlight, never multiplying toward black. Leaving flags[1] at its
        // default 0 here falls through to applyDetailMode's plain-baseLit
        // branch -- correct brightness, just without the specular sparkle,
        // until a real water shader lands.
        if (isDetailTagged)
            _tlObject.flags[1] = 1.0f;
        if (_textBank)
        {
            TextureMTL* tex = isDetailTagged ? _textBank->GetDetailTexture() : _textBank->GetSpecularTexture();
            if (tex)
            {
                _textBank->UseMipmap(tex, 0, 0);
                _tlSecondaryTexture = tex->GpuHandle();
            }
        }
    }
    else if (d.shader == render::ShaderFamily::Grass)
    {
        _tlObject.flags[1] = 2.0f;
        if (_textBank)
        {
            TextureMTL* grass = _textBank->GetGrassTexture();
            if (grass)
            {
                _textBank->UseMipmap(grass, 0, 0);
                _tlSecondaryTexture = grass->GpuHandle();
            }
        }
    }

    // Shadow polys (Shadow.cpp's MakeShadow) carry no texture and always
    // resolve to BuildRenderPassDescriptor's isShadow branch
    // (BuildRenderPassDescriptor.hpp: DepthMode::Shadow + BlendMode::Shadow,
    // checked before IsAlpha/IsTransparent) regardless of the
    // texture-classified isAlpha/isCutout below -- see fsShadow's doc
    // comment for the single-pass stencil-exclusion scheme this selects.
    // Every other section's blend choice still comes from the texture's
    // measured alpha stats (Texture::IsAlpha/IsTransparent), not the spec's
    // IsAlpha/IsTransparent bits BuildRenderPassDescriptor would otherwise
    // use: that's a deliberate, separately-tested Metal-specific signal (see
    // METAL_PORT_PROGRESS.md's alpha-to-coverage note and the chroma-key
    // threading fix), not something this milestone replaces.
    if (render::Has(spec.backend, render::Backend::IsShadow))
    {
        _tlSectionDepthMode = render::DepthMode::Shadow;
        _tlSectionBlendMode = render::BlendMode::Shadow;
    }
    else
    {
        // IsAlphaFog (cloudlets, bullet impacts, craters, HUD-ish fades) and
        // plain IsAlpha (vertex-color-alpha-blended effects/materials with an
        // otherwise-opaque texture) are both explicit alpha-blended material
        // modes. Do not let texture classification override either: GL33
        // routes the spec bit itself to AlphaBlend regardless of texture
        // alpha stats (BuildRenderPassDescriptor.hpp's Has(backend,
        // Backend::IsAlpha) branch sets d.blend unconditionally), so Metal
        // must too -- relying on texture stats here misses shapes whose
        // alpha comes from vertex color rather than the bound texture, which
        // would otherwise silently render as a hard-edged opaque shape
        // instead of fading.
        //
        // But the legacy IsAlpha spec bit is set generically for "this
        // material has alpha" and does NOT distinguish cutout from blend the
        // way Metal's texture-measured isCutout does (that split is the M113
        // transparency fix, b0e99f8) -- a hard-cutout texture (e.g. the jeep
        // steering wheel's spoke holes) can still carry the IsAlpha spec bit.
        // isCutout is the authoritative signal and must gate every blend-
        // forcing reason below, not just one arm of the OR -- otherwise a
        // future addition here can reintroduce this same class of bug.
        // Cutout sections always keep fsMeshOpaque's alpha-test discard
        // instead of being forced onto fsMeshBlend, which has no discard and
        // instead blends the cutout edges' partial-alpha texels straight
        // over the background -- visible as a fringe right at the corners.
        const bool forceBlend = d.blend == render::BlendMode::AlphaBlend &&
                                (d.fog == render::FogMode::AlphaFog ||
                                 render::Has(spec.backend, render::Backend::IsAlpha));
        const bool isBlend = !isCutout && (isAlpha || forceBlend);
        _tlSectionBlendMode = isBlend ? render::BlendMode::AlphaBlend : render::BlendMode::Opaque;
        // Match GL33/BuildRenderPassDescriptor: alpha/blend does not by
        // itself disable depth writes. Only NoZWrite/NoZBuf/shadow change
        // depth mode. Turning every Blend texture into ReadOnly makes
        // layered alpha surfaces inside one object bleed through each other
        // (e.g. jeep steering wheel vs windscreen).
        _tlSectionDepthMode = d.depth;
    }
}

// Ported from GL33's PrepareMeshTL/PrepareMeshTLImpl (EngineGL33_Mesh.cpp).
// v1 simplification: rebuilds the per-frame constants (camera/sun/fog) on
// every call instead of caching them across a pass's whole run of draws --
// correctness over performance for this milestone.
void EngineMTL::PrepareMeshTL(const LightList& /*lights*/, const Matrix4& modelToWorld,
                              const render::LegacySpec& spec)
{
    // GL33's PrepareMeshTLImpl (EngineGL33_Mesh.cpp) re-asserts this on every
    // TL draw -- without it, _sunEnabled stays stuck at whatever Shadow.cpp's
    // GEngine->EnableSunLight(false) last set it to (true initially, but
    // permanently false after the first shadow pass of the session, since
    // nothing on Metal ever flipped it back on). That silently zeroed every
    // per-vertex NdotL/specular term scene-wide, while ambient/diffuse still
    // looked lit because SetMaterial bakes the sun's color into them
    // unconditionally (see PrepareMeshTL's vertex-shader comment).
    EnableSunLight(!render::Has(spec.material, render::Material::DisableSun));
    if (GScene == nullptr)
        return;
    Camera* camera = GScene->GetCamera();
    LightSun* sun = GScene->MainLight();
    if (camera == nullptr || sun == nullptr)
        return;

    // Per-frame: view (rotation only -- camera-relative rendering zeroes the
    // translation) and projection.
    GfxMatrix view;
    ConvertMatrix(view, camera->InverseScaled());
    view._41 = view._42 = view._43 = 0;
    std::memcpy(_tlFrame.view.m, &view, sizeof(view));

    GfxMatrix projection;
    ConvertProjectionMatrix(projection, camera->ProjectionNormal(), CanZBias() ? 0 : _bias);
    std::memcpy(_tlFrame.projection.m, &projection, sizeof(projection));

    const Vector3 sunDir = sun->Direction();
    _tlFrame.sunDirAndEnabled[0] = static_cast<float>(sunDir.X());
    _tlFrame.sunDirAndEnabled[1] = static_cast<float>(sunDir.Y());
    _tlFrame.sunDirAndEnabled[2] = static_cast<float>(sunDir.Z());
    _tlFrame.sunDirAndEnabled[3] = _sunEnabled ? 1.0f : 0.0f;

    const float fogStart = GScene->GetFogMinRange();
    const float fogEnd = GScene->GetFogMaxRange();
    _tlFrame.fogParams[0] = fogStart;
    _tlFrame.fogParams[1] = (fogEnd > fogStart) ? 1.0f / (fogEnd - fogStart) : 0.0f;
    _tlFrame.fogParams[2] = 1.0f;
    _tlFrame.fogParams[3] = 0.0f;
    _tlFrame.fogColor[0] = _fogColor.R();
    _tlFrame.fogColor[1] = _fogColor.G();
    _tlFrame.fogColor[2] = _fogColor.B();
    _tlFrame.fogColor[3] = 1.0f;
    // GL33 uploads zero to this shader slot for ordinary mesh draws; keep
    // Metal's constant-buffer layout identical without feeding the absolute
    // camera position into camera-relative lighting math.
    _tlFrame.gl33CamPosZero[0] = 0.0f;
    _tlFrame.gl33CamPosZero[1] = 0.0f;
    _tlFrame.gl33CamPosZero[2] = 0.0f;
    _tlFrame.gl33CamPosZero[3] = 0.0f;

    // Per-object: camera-relative world matrix (translation has the camera
    // position subtracted), same as GL33's PrepareMeshTLImpl.
    GfxMatrix world;
    ConvertMatrix(world, modelToWorld);
    const Vector3 camPos = camera->Position();
    world._41 -= static_cast<float>(camPos.X());
    world._42 -= static_cast<float>(camPos.Y());
    world._43 -= static_cast<float>(camPos.Z());
    std::memcpy(_tlObject.world.m, &world, sizeof(world));
}

void EngineMTL::BeginMeshTL(const Shape& sMesh, int /*spec*/, bool dynamic)
{
    if (VertexBuffer* buf = sMesh.GetVertexBuffer())
        buf->Update(sMesh, dynamic);
}

void EngineMTL::DrawSectionTL(const Shape& sMesh, int beg, int end)
{
    auto* buf = static_cast<VertexBufferMTL*>(sMesh.GetVertexBuffer());
    int firstIndex = 0, indexCount = 0;
    if (buf == nullptr)
        return;
    if (!buf->ResolveRange(beg, end, firstIndex, indexCount))
        return;

    _bootstrap.DrawSectionTL(buf->VertexBufferHandle(), buf->IndexBufferHandle(), firstIndex, indexCount,
                             _tlCurrentTexture, _tlSecondaryTexture, _tlObject, _tlFrame, _tlSectionDepthMode, _tlSectionBlendMode,
                             _tlSectionSampler, _tlSectionSurfaceMode, _tlSectionShader);
}

void EngineMTL::DrawPolygon(const VertexIndex* i, int n)
{
    DrawIndexedFan3D(i, n);
}

void EngineMTL::DrawSection(const FaceArray& face, Offset beg, Offset end)
{
    for (Offset i = beg; i < end; face.Next(i))
    {
        const Poly& f = face[i];
        DrawIndexedFan3D(f.GetVertexList(), f.N());
    }
}

void EngineMTL::DrawLine(int beg, int end)
{
    if (_mesh == nullptr)
        return;

    // Thin quad approximating the line, same approach as the 2D DrawLine
    // overload (and GL33's DrawLine(int,int), EngineGL33_2D.cpp:392).
    const TLVertex& v0 = _mesh->GetVertex(beg);
    const TLVertex& v1 = _mesh->GetVertex(end);

    const float x0 = v0.pos.X(), y0 = v0.pos.Y();
    const float x1 = v1.pos.X(), y1 = v1.pos.Y();
    const float dx = x1 - x0, dy = y1 - y0;
    const float lenSq = dx * dx + dy * dy;
    const float invLen = lenSq > 0 ? 1.0f / std::sqrt(lenSq) : 1.0f;
    const float pdx = dy * invLen, pdy = -dx * invLen;
    const float halfW = 1.5f;

    const float xy[8] = {
        x0 - pdx * halfW, y0 - pdy * halfW, x1 - pdx * halfW, y1 - pdy * halfW,
        x1 + pdx * halfW, y1 + pdy * halfW, x0 + pdx * halfW, y0 + pdy * halfW,
    };
    const float z[4] = {v0.pos.Z(), v1.pos.Z(), v1.pos.Z(), v0.pos.Z()};
    const float rhw[4] = {v0.rhw, v1.rhw, v1.rhw, v0.rhw};
    const float uv[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    const PackedColor colors[4] = {v0.color, v0.color, v1.color, v1.color};

    const Rect2DAbs fullScreen(0, 0, static_cast<float>(_w), static_cast<float>(_h));
    DrawFan2D(xy, z, rhw, uv, nullptr, colors, 4, 0, 0, fullScreen);
}

bool EngineMTL::SwitchRes(int w, int h, int bpp)
{
    _pixelSize = bpp;
    if (_sdlWindow && _windowed)
        SDL_SetWindowSize(_sdlWindow, w, h);

    int cw = 0, ch = 0;
    if (_sdlWindow)
        SDL_GetWindowSizeInPixels(_sdlWindow, &cw, &ch);
    _w = cw > 0 ? cw : w;
    _h = ch > 0 ? ch : h;
    _bootstrap.OnWindowResized(_w, _h);
    return true;
}

bool EngineMTL::SwitchRefreshRate(int refresh)
{
    if (refresh == 0)
        return false;
    _refreshRate = refresh;
    return true;
}

bool EngineMTL::SetWindowMode(WindowMode mode)
{
    if (!_sdlWindow)
        return false;

#ifdef POSEIDON_TARGET_IOS
    mode = WindowMode::Fullscreen;
#endif

    if (_windowed && mode != WindowMode::Windowed)
        SDL_GetWindowSize(_sdlWindow, &_windowedRestoreW, &_windowedRestoreH);

    _windowMode = mode;
    SDL_SetWindowFullscreen(_sdlWindow, mode != WindowMode::Windowed);
    SDL_SetWindowBordered(_sdlWindow, mode == WindowMode::Windowed);
    _windowed = (mode == WindowMode::Windowed);

    if (mode == WindowMode::Windowed && _windowedRestoreW > 0)
        SDL_SetWindowSize(_sdlWindow, _windowedRestoreW, _windowedRestoreH);

    int cw = 0, ch = 0;
    SDL_GetWindowSizeInPixels(_sdlWindow, &cw, &ch);
    _w = cw;
    _h = ch;
    _bootstrap.OnWindowResized(_w, _h);

    OnFullscreenChanged(_windowed);
    return true;
}

RString EngineMTL::GetDebugName() const
{
    return "Metal";
}

RString EngineMTL::GetRendererName() const
{
    std::string name = _bootstrap.GetRendererName();
    return name.empty() ? "Metal" : name.c_str();
}

void EngineMTL::ListResolutions(FindArray<ResolutionInfo>& ret)
{
    ret.Clear();
    if (_windowed)
        return;

    SDL_DisplayID display = _sdlWindow ? SDL_GetDisplayForWindow(_sdlWindow) : SDL_GetPrimaryDisplay();
    if (!display)
        return;

    int count = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &count);
    if (!modes)
        return;

    for (int i = 0; i < count; i++)
    {
        ResolutionInfo info;
        info.w = modes[i]->w;
        info.h = modes[i]->h;
        info.bpp = SDL_BITSPERPIXEL(modes[i]->format);
        ret.AddUnique(info);
    }
    SDL_free(modes);
}

void EngineMTL::ListRefreshRates(FindArray<int>& ret)
{
    ret.Clear();
    if (_windowed)
    {
        ret.Add(0);
        return;
    }

    SDL_DisplayID display = _sdlWindow ? SDL_GetDisplayForWindow(_sdlWindow) : SDL_GetPrimaryDisplay();
    if (!display)
        return;

    int count = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &count);
    if (!modes)
        return;

    for (int i = 0; i < count; i++)
    {
        if (modes[i]->w == Width() && modes[i]->h == Height())
            ret.AddUnique(static_cast<int>(modes[i]->refresh_rate));
    }
    SDL_free(modes);
}

AbstractTextBank* EngineMTL::TextBank()
{
    return _textBank;
}

bool EngineMTL::IsResizable() const
{
    return _sdlWindow && (SDL_GetWindowFlags(_sdlWindow) & SDL_WINDOW_RESIZABLE) != 0;
}

int EngineMTL::AFrameTime() const
{
    return 0; // matches GL33::FrameTime() (also stubbed to 0)
}

} // namespace Poseidon
