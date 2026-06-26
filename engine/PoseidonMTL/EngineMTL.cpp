#include <PoseidonMTL/EngineMTL.hpp>

#include <SDL3/SDL.h>

#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
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

    _sdlWindow = SDL_CreateWindow("Poseidon [Metal]", placement.width, placement.height, flags);
    if (!_sdlWindow)
    {
        LOG_ERROR(Graphics, "MTL: SDL_CreateWindow failed: {}", SDL_GetError());
        return;
    }

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
        LOG_WARN(Graphics, "MTL: InitDraw's BeginFrame failed (no drawable -- window minimized/occluded?)");

    if (_textBank)
        _textBank->StartFrame();

    Engine::InitDraw(clear, color);
    _frameOpen = true;
}

void EngineMTL::FinishDraw()
{
    if (!_frameOpen)
        return;

    Engine::FinishDraw();

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

void EngineMTL::DrawFan2D(const float* xy, const float* uv, const PackedColor* colors, int n, int textureHandle,
                          const Rect2DAbs& clip, render::DepthMode depthMode, render::BlendMode blendMode,
                          render::SamplerMode sampler)
{
    if (n < 3 || n > kMaxPolyVerts)
        return;

    Vertex2DMTL verts[kMaxPolyVerts];
    for (int i = 0; i < n; i++)
    {
        PixelToNDC(xy[i * 2], xy[i * 2 + 1], verts[i].x, verts[i].y);
        verts[i].u = uv[i * 2];
        verts[i].v = uv[i * 2 + 1];
        verts[i].r = colors[i].R8() / 255.0f;
        verts[i].g = colors[i].G8() / 255.0f;
        verts[i].b = colors[i].B8() / 255.0f;
        verts[i].a = colors[i].A8() / 255.0f;
    }

    uint16_t indices[(kMaxPolyVerts - 2) * 3];
    int idxCount = 0;
    for (int i = 1; i < n - 1; i++)
    {
        indices[idxCount++] = 0;
        indices[idxCount++] = static_cast<uint16_t>(i);
        indices[idxCount++] = static_cast<uint16_t>(i + 1);
    }

    _bootstrap.DrawTriangles2D(verts, n, indices, idxCount, textureHandle, static_cast<int>(clip.x),
                               static_cast<int>(clip.y), static_cast<int>(clip.w), static_cast<int>(clip.h), depthMode,
                               blendMode, sampler);
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

    DrawFan2D(xy, uv, colors, 4, GpuHandleOf(pars.mip._texture), clip);
}

void EngineMTL::DrawPoly(const MipInfo& mip, const Vertex2DAbs* vertices, int n, const Rect2DAbs& clip,
                         int /*specFlags*/)
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

    DrawFan2D(xy, uv, colors, n, mip.IsOK() ? GpuHandleOf(mip._texture) : 0, clip);
}

void EngineMTL::DrawPoly(const MipInfo& mip, const Vertex2DPixel* vertices, int n, const Rect2DPixel& clip,
                         int /*specFlags*/)
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
    DrawFan2D(xy, uv, colors, n, mip.IsOK() ? GpuHandleOf(mip._texture) : 0, clipAbs);
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

    DrawFan2D(xy, uv, colors, 4, 0, clip);
}

void EngineMTL::DrawIndexedFan3D(const VertexIndex* indices, int n)
{
    if (_mesh == nullptr || n < 3 || n > kMaxPolyVerts)
        return;

    float xy[kMaxPolyVerts * 2];
    float uv[kMaxPolyVerts * 2];
    PackedColor colors[kMaxPolyVerts];
    for (int k = 0; k < n; k++)
    {
        const TLVertex& v = _mesh->GetVertex(indices[k]);
        xy[k * 2] = v.pos.X();
        xy[k * 2 + 1] = v.pos.Y();
        uv[k * 2] = v.t0.u;
        uv[k * 2 + 1] = v.t0.v;
        colors[k] = v.color;
    }

    const Rect2DAbs fullScreen(0, 0, static_cast<float>(_w), static_cast<float>(_h));
    DrawFan2D(xy, uv, colors, n, _currentTriTexture, fullScreen, _currentTriDepthMode, _currentTriBlendMode,
              _currentTriSampler);
}

// GL33's equivalent is the legacy/queued path's FlushQueue -> ApplyPassState
// -> BuildRenderPassDescriptor -> ApplyPipeline chain (EngineGL33_Queue.cpp):
// every draw, hardware-TL or legacy/software, resolves through the same
// descriptor builder so state (depth/blend/stencil) is decoupled from how the
// vertices get to the GPU. This was previously the gap that let shadow polys
// without a hardware vertex buffer (most real casters, see Shadow.cpp's
// Object::DrawShadow) bypass the TL path's stencil exclusion entirely --
// PrepareTriangle ignored specFlags outright. Default BuildContext is correct
// here: this path never multitextures, and PassKindHint isn't set anywhere
// for it (matches GL33, which also doesn't branch on PassKind directly).
void EngineMTL::PrepareTriangle(const MipInfo& mip, int specFlags)
{
    _currentTriTexture = mip.IsOK() ? GpuHandleOf(mip._texture) : 0;
    const render::RenderPassDescriptor d = render::BuildRenderPassDescriptor(render::SplitLegacy(specFlags));
    _currentTriDepthMode = d.depth;
    _currentTriBlendMode = d.blend;
    _currentTriSampler = d.sampler;
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
void EngineMTL::SetMaterial(const TLMaterial& mat, const LightList& /*lights*/, const render::LegacySpec& /*spec*/)
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

    // Same mapping BuildRenderPassDescriptor.hpp uses for d.sampler -- no
    // competing signal to special-case around here (unlike blend mode's
    // texture-stat override above), so this is a direct, unconditional
    // mirror of the spec-bit -> SamplerMode translation.
    _tlSectionSampler.filter =
        render::Has(spec.backend, render::Backend::PointSampling) ? render::SamplerFilter::Point
                                                                    : render::SamplerFilter::Linear;
    _tlSectionSampler.clampU = render::Has(spec.backend, render::Backend::ClampU);
    _tlSectionSampler.clampV = render::Has(spec.backend, render::Backend::ClampV);

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
        const bool isBlend = isAlpha && !isCutout;
        _tlSectionBlendMode = isBlend ? render::BlendMode::AlphaBlend : render::BlendMode::Opaque;
        _tlSectionDepthMode = (isBlend || render::Has(spec.backend, render::Backend::NoZWrite))
                                  ? render::DepthMode::ReadOnly
                                  : render::DepthMode::Normal;
    }
}

// Ported from GL33's PrepareMeshTL/PrepareMeshTLImpl (EngineGL33_Mesh.cpp).
// v1 simplification: rebuilds the per-frame constants (camera/sun/fog) on
// every call instead of caching them across a pass's whole run of draws --
// correctness over performance for this milestone.
void EngineMTL::PrepareMeshTL(const LightList& /*lights*/, const Matrix4& modelToWorld,
                              const render::LegacySpec& /*spec*/)
{
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
                             _tlCurrentTexture, _tlObject, _tlFrame, _tlSectionDepthMode, _tlSectionBlendMode,
                             _tlSectionSampler);
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
    const float uv[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    const PackedColor colors[4] = {v0.color, v0.color, v1.color, v1.color};

    const Rect2DAbs fullScreen(0, 0, static_cast<float>(_w), static_cast<float>(_h));
    DrawFan2D(xy, uv, colors, 4, 0, fullScreen);
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
