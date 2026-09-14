#include <PoseidonMTL/EngineMTL.hpp>

#include <SDL3/SDL.h>

#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Dev/Debug/DebugOverlay.hpp>
#include <Poseidon/Graphics/Shared/WindowPlacement.hpp>
#include <Poseidon/Graphics/Shared/ScreenshotWriter.hpp>
#include <Poseidon/Graphics/Shared/PNGWriter.hpp>
#include <Poseidon/Graphics/Core/TLVertex.hpp>
#include <Poseidon/Graphics/Core/MatrixConversion.hpp>
#include <Poseidon/Graphics/Rendering/BuildRenderPassDescriptor.hpp>
#include <Poseidon/Graphics/Rendering/Shape/ClipShape.hpp>
#include <Poseidon/Graphics/Rendering/Shape/Shape.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <PoseidonGL33/GL33LightIndices.hpp>
#include <PoseidonMTL/TextBankMTL.hpp>
#include <PoseidonMTL/TextureMTL.hpp>
#include <PoseidonMTL/VertexBufferMTL.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace Poseidon
{

namespace
{
bool ReadDisplayMode(const SDL_DisplayMode* mode, int& w, int& h, int& refresh)
{
    if (!mode)
        return false;
    w = mode->w;
    h = mode->h;
    refresh = (int)(mode->refresh_rate + 0.5f);
    return true;
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

    const char* readback = std::getenv("TRIDENT_METAL_READBACK");
    _tridentReadback = readback != nullptr && readback[0] != '\0' && std::strcmp(readback, "0") != 0;

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
    // "2" = dim the home indicator and defer ALL screen-edge system gestures
    // (SDL_uikitviewcontroller.m: preferredScreenEdgesDeferringSystemGestures
    // returns UIRectEdgeAll only for this value) -- without it, a tap that
    // starts close enough to the bottom edge can be swallowed by iOS's
    // swipe-up-for-home gesture recognizer before SDL ever sees a finger-down
    // event, which is exactly what made some group-bar unit icons untappable.
    // There's a passive default that resolves to the same UIRectEdgeAll for a
    // fullscreen+borderless window (which this always is on iOS), but it
    // depends on window->flags being settled at the exact moment UIKit first
    // queries this property; setting the hint explicitly forces a
    // deterministic re-query instead of relying on that timing.
    SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, "2");
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
    _w = _bootstrap.DrawableWidth() > 0 ? _bootstrap.DrawableWidth() : cw;
    _h = _bootstrap.DrawableHeight() > 0 ? _bootstrap.DrawableHeight() : ch;

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

    _bootstrap.SetReadbackFrame(_tridentReadback || _pendingScreenshotPath.GetLength() > 0);
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

    // Force PrepareMeshTL to rebuild its cached view/sun/fog constants at
    // least once this frame (camera/sun/fog can change between frames).
    _tlFrameValid = false;
    UpdateShadowMapLitState();

    Engine::InitDraw(clear, color);
    _frameOpen = true;
    _drawItems.clear();
    _currentDrawItem = DrawItem{};
}

void EngineMTL::FinishDraw()
{
    if (!_frameOpen)
        return;

    Engine::FinishDraw();
    DrawFinishTexts();
    Dev::DebugOverlay::Render();
    FlushQueues();
    _bootstrap.RenderDebugOverlay();

    if (_textBank)
        _textBank->FinishFrame();

    _frameOpen = false;
}

void EngineMTL::PresentFrame()
{
    if (!_bootstrap.FrameOpen())
        return;

    if (_tridentReadback || _pendingScreenshotPath.GetLength() > 0)
    {
        const RString path = _pendingScreenshotPath;
        std::vector<uint8_t> rgb;
        int width = 0;
        int height = 0;
        if (_bootstrap.EndFrame(&rgb, &width, &height) && !rgb.empty())
        {
            _lastFrameRGB = std::move(rgb);
            _lastFrameWidth = width;
            _lastFrameHeight = height;
            if (path.GetLength() > 0)
            {
                ScreenshotWriter::WriteRGB(path, width, height, _lastFrameRGB.data());
                _pendingScreenshotPath = "";
            }
        }
    }
    else
    {
        _bootstrap.EndFrame();
    }
}

void EngineMTL::NextFrame()
{
    PresentFrame();
    Engine::NextFrame();
}

void EngineMTL::FlushPendingScreenshot()
{
    if (_pendingScreenshotPath.GetLength() == 0 || _frameOpen)
        return;
    if (_bootstrap.FrameOpen())
    {
        PresentFrame();
        return;
    }
    // Between frames there is nothing left to read from the GPU; the cached
    // copy of the last presented frame is what GL33's back buffer holds too.
    if (!_lastFrameRGB.empty())
    {
        ScreenshotWriter::WriteRGB(_pendingScreenshotPath, _lastFrameWidth, _lastFrameHeight, _lastFrameRGB.data());
        _pendingScreenshotPath = "";
    }
}

int EngineMTL::SampleBackBufferNonBlack()
{
    if (!_tridentReadback)
        return -1;
    if (!_frameOpen)
        PresentFrame();
    if (_lastFrameRGB.empty() || _lastFrameWidth <= 0 || _lastFrameHeight <= 0)
        return -1;

    int nonBlack = 0;
    for (int sy = 0; sy < 16; ++sy)
    {
        const int y = _lastFrameHeight * sy / 16;
        for (int sx = 0; sx < 16; ++sx)
        {
            const int x = _lastFrameWidth * sx / 16;
            const size_t offset =
                (static_cast<size_t>(y) * static_cast<size_t>(_lastFrameWidth) + static_cast<size_t>(x)) * 3u;
            if (_lastFrameRGB[offset] > 2 || _lastFrameRGB[offset + 1] > 2 || _lastFrameRGB[offset + 2] > 2)
                ++nonBlack;
        }
    }
    return nonBlack;
}

bool EngineMTL::SamplePixel(int x, int y, uint8_t* outRGB)
{
    if (!_tridentReadback || outRGB == nullptr)
        return false;
    if (!_frameOpen)
        PresentFrame();
    if (_lastFrameRGB.empty() || x < 0 || y < 0 || x >= _lastFrameWidth || y >= _lastFrameHeight)
        return false;

    const size_t offset = (static_cast<size_t>(y) * static_cast<size_t>(_lastFrameWidth) + static_cast<size_t>(x)) * 3u;
    outRGB[0] = _lastFrameRGB[offset];
    outRGB[1] = _lastFrameRGB[offset + 1];
    outRGB[2] = _lastFrameRGB[offset + 2];
    return true;
}

void EngineMTL::DrawTestPattern(const char* name)
{
    if (!_frameOpen || name == nullptr)
        return;

    const Rect2DAbs clip(0.0f, 0.0f, static_cast<float>(_w), static_cast<float>(_h));
    const float w = static_cast<float>(_w);
    const float h = static_cast<float>(_h);
    auto drawQuad = [&](float x0, float y0, float x1, float y1, const DWORD(&argb)[4])
    {
        const float xy[8] = {x0, y0, x1, y0, x1, y1, x0, y1};
        const float uv[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        const PackedColor colors[4] = {PackedColor(argb[0]), PackedColor(argb[1]), PackedColor(argb[2]),
                                       PackedColor(argb[3])};
        DrawFan2D(xy, nullptr, nullptr, uv, nullptr, colors, 4, 0, 0, clip, render::DepthMode::Disabled,
                  render::BlendMode::Opaque);
    };
    auto clearTo = [&](DWORD argb) { Clear(true, true, PackedColor(argb)); };

    if (std::strcmp(name, "gradient3d") == 0)
    {
        drawQuad(0, 0, w, h, {0xFFFF0000, 0xFF00FF00, 0xFFFFFFFF, 0xFF0000FF});
    }
    else if (std::strcmp(name, "colorbar") == 0)
    {
        const DWORD colors[5] = {0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFF00, 0xFFFF00FF};
        const float barW = w / 5.0f;
        for (int i = 0; i < 5; i++)
            drawQuad(barW * i, 0, barW * (i + 1), h, {colors[i], colors[i], colors[i], colors[i]});
    }
    else if (std::strcmp(name, "clear_blue") == 0)
    {
        clearTo(0xFF0040FF);
    }
    else if (std::strcmp(name, "clear_magenta") == 0)
    {
        clearTo(0xFFFF00FF);
    }
    else if (std::strcmp(name, "quad2d") == 0)
    {
        clearTo(0xFF000000);
        drawQuad(0, 0, w, h, {0xFF000080, 0xFF000080, 0xFF000080, 0xFF000080});
        const float m = 0.25f;
        drawQuad(w * m, h * m, w * (1 - m), h * (1 - m), {0xFFCC0000, 0xFFCC0000, 0xFFCC0000, 0xFFCC0000});
    }
}

void EngineMTL::SetDebugFlatColor(bool enable)
{
    if (_debugFlatColor == enable)
        return;
    _debugFlatColor = enable;
    _tlFrameValid = false;
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
                          render::AlphaMode alphaMode, std::uint8_t alphaRef, const PackedColor* specular,
                          float detailMode)
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
                               static_cast<int>(clip.x), static_cast<int>(clip.y), static_cast<int>(clip.w),
                               static_cast<int>(clip.h), z != nullptr, depthMode, blendMode, sampler, surface, shader,
                               alphaMode, alphaRef, fogColor);
}

void EngineMTL::Draw2D(const Draw2DPars& pars, const Rect2DAbs& rect, const Rect2DAbs& clip)
{
    if (!pars.mip.IsOK())
        return;

    const float xy[8] = {
        rect.x, rect.y, rect.x + rect.w, rect.y, rect.x + rect.w, rect.y + rect.h, rect.x, rect.y + rect.h,
    };
    const float uv[8] = {
        pars.uTL, pars.vTL, pars.uTR, pars.vTR, pars.uBR, pars.vBR, pars.uBL, pars.vBL,
    };
    const PackedColor colors[4] = {pars.colorTL, pars.colorTR, pars.colorBR, pars.colorBL};

    const render::RenderPassDescriptor d = render::BuildRenderPassDescriptor(render::SplitLegacy(pars.spec));
    DrawFan2D(xy, nullptr, nullptr, uv, nullptr, colors, 4, GpuHandleOf(pars.mip._texture), 0, clip, d.depth, d.blend,
              d.sampler, d.surface, d.shader, d.alpha, d.alphaRef);
}

void EngineMTL::DrawPoly(const MipInfo& mip, const Vertex2DAbs* vertices, int n, const Rect2DAbs& clip, int specFlags)
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
              d.depth, d.blend, d.sampler, d.surface, d.shader, d.alpha, d.alphaRef);
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
    DrawFan2D(xy, nullptr, nullptr, uv, nullptr, colors, n, mip.IsOK() ? GpuHandleOf(mip._texture) : 0, 0, clipAbs,
              d.depth, d.blend, d.sampler, d.surface, d.shader, d.alpha, d.alphaRef);
}

void EngineMTL::DrawDecal(Vector3Par screen, float rhw, float sizeX, float sizeY, PackedColor color, const MipInfo& mip,
                          int specFlags)
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
    const float z[4] = {screen.Z(), screen.Z(), screen.Z(), screen.Z()};
    const float rhwValues[4] = {rhw, rhw, rhw, rhw};
    const float uv[8] = {uBeg, vBeg, uEnd, vBeg, uEnd, vEnd, uBeg, vEnd};
    const PackedColor colors[4] = {color, color, color, color};
    const Rect2DAbs clip(0, 0, static_cast<float>(_w), static_cast<float>(_h));

    const render::RenderPassDescriptor d = render::BuildRenderPassDescriptor(render::SplitLegacy(specFlags));
    DrawFan2D(xy, z, rhwValues, uv, nullptr, colors, 4, GpuHandleOf(mip._texture), 0, clip, d.depth, d.blend, d.sampler,
              d.surface, d.shader, d.alpha, d.alphaRef);
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
    if (_instCount > 1)
        _instImpure = true; // vertex-soup geometry can't be instanced -- run must fall back
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
              _currentTriDepthMode, _currentTriBlendMode, _currentTriSampler, _currentTriSurfaceMode, _currentTriShader,
              _currentTriAlphaMode, _currentTriAlphaRef, specular, _currentTriDetailMode);
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
    _currentTriSpec = spec;
    render::BuildContext ctx;
    ctx.isIn3DPass = false;
    ctx.isMultitexturing = IsMultitexturing();
    const render::RenderPassDescriptor d = render::BuildRenderPassDescriptor(spec, ctx);
    _currentTriDepthMode = d.depth;
    _currentTriBlendMode = d.blend;
    _currentTriSampler = d.sampler;
    _currentTriSurfaceMode = d.surface;
    _currentTriShader = d.shader;
    _currentTriAlphaMode = d.alpha;
    _currentTriAlphaRef = d.alphaRef;
    // Control3D pictures share this legacy fan path with software-transformed
    // world models, but their source art commonly contains graded alpha used
    // for row tinting/fades.  Reclassifying those pictures as the wheel's
    // near-opaque cutout makes unselected thumbnails black until highlighted.
    // 3D UI overlays (cutObj/RscObject title effects, ControlObject) and
    // optics models are drawn magnified across the screen; the near-opaque
    // threshold below would discard everything but the core of their
    // bilinear-filtered reticle lines and glass vignettes, so they keep
    // GL33's blend path.
    const bool screenSpaceOverlay = GetPassKindHint() == render::PassKindHint::ScreenSpace3D ||
                                    render::Has(spec.material, render::Material::BestMipmap);
    const bool measuredCutout =
        !_legacyMeshUiOverlay && !screenSpaceOverlay && mip.IsOK() && mip._texture && mip._texture->IsTransparent();
    if (measuredCutout)
    {
        // Legacy model flags only say "has alpha". The decoded texture class
        // supplies the missing distinction: cutout holes discard, while the
        // surviving wheel/foliage pixels write depth and occlude rear faces.
        _currentTriDepthMode = d.depth;
        _currentTriAlphaMode = render::AlphaMode::Test;
        _currentTriAlphaRef = 254;
    }
    else if (d.blend == render::BlendMode::AlphaBlend)
    {
        // Genuine translucent legacy geometry tests opaque depth but does not
        // create ordering-dependent depth between its own blended layers.
        _currentTriDepthMode = render::DepthMode::ReadOnly;
    }
    if (_legacyMeshUiOverlay && d.blend == render::BlendMode::AlphaBlend && d.fog == render::FogMode::AlphaFog)
        _currentTriDepthMode = render::DepthMode::ReadOnly;

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

    // Local point/spot lights (street lamps, vehicle headlights): selected
    // from the frame table by index (GL33's SetLocalLightIndices). The
    // night gate lives in the raw material colours: they are zero by day
    // except for DisableSun materials (cockpit interiors), which the legacy
    // SetupLights forces to full night regardless of the time of day.
    float night = sun->NightEffect();
    if (render::Has(spec.material, render::Material::DisableSun))
        night = 1.0f;
    const Color matDif = mat.diffuse * night;
    const Color matAmb = mat.ambient * night;
    _tlObject.matDiffuseRaw[0] = matDif.R();
    _tlObject.matDiffuseRaw[1] = matDif.G();
    _tlObject.matDiffuseRaw[2] = matDif.B();
    _tlObject.matDiffuseRaw[3] = 0.0f;
    _tlObject.matAmbientRaw[0] = matAmb.R();
    _tlObject.matAmbientRaw[1] = matAmb.G();
    _tlObject.matAmbientRaw[2] = matAmb.B();
    _tlObject.matAmbientRaw[3] = 0.0f;
    const auto packed = PackLightIndices(lights);
    std::memcpy(_tlObject.lightIdx, packed.data(), sizeof(_tlObject.lightIdx));

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

void EngineMTL::SetGrassParams(float a1, float a2, float a3, float a4)
{
    _grassParams[0] = a1;
    _grassParams[1] = a2;
    _grassParams[2] = a3;
    _grassParams[3] = a4;
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
    _tlObject.flags[2] = _grassParams[0];
    _tlObject.flags[3] = _grassParams[1];
    _tlSecondaryTexture = 0;

    // Same mapping BuildRenderPassDescriptor.hpp uses for d.sampler -- no
    // competing signal to special-case around here (unlike blend mode's
    // texture-stat override above), so this is a direct, unconditional
    // mirror of the spec-bit -> SamplerMode translation.
    _tlSectionSampler.filter = render::Has(spec.backend, render::Backend::PointSampling)
                                   ? render::SamplerFilter::Point
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
    if (d.shader == render::ShaderFamily::Water)
    {
        if (_textBank)
        {
            TextureMTL* water = _textBank->GetWaterBumpMap();
            if (water)
            {
                _textBank->UseMipmap(water, 0, 0);
                _tlSecondaryTexture = water->GpuHandle();
                // Do not enable the bump/specular equation unless the bump
                // map really made it to the GPU. The slot's fallback texture
                // is opaque white, which decodes as a constant (-1,-1,-1)
                // normal and can saturate the entire water surface to white.
                if (_tlSecondaryTexture != 0)
                    _tlObject.flags[1] = 3.0f;
            }
        }
    }
    else if (d.shader == render::ShaderFamily::Detail)
    {
        // d.shader==Detail covers both DetailTexture- and SpecularTexture-
        // tagged sections (BuildRenderPassDescriptor.hpp's generic mtMask
        // branch collapses both to Detail when GrassTexture isn't set).
        // Water tiles currently carry SpecularTexture rather than IsWater
        // (LandscapeRender.cpp's WaterFlags), so they arrive through this
        // generic Detail family. For Metal, route that legacy marker to the
        // dedicated water bump map and water equation.
        const bool isDetailTagged = render::Has(spec.backend, render::Backend::DetailTexture);
        if (isDetailTagged)
            _tlObject.flags[1] = 1.0f;
        if (_textBank)
        {
            TextureMTL* tex = isDetailTagged ? _textBank->GetDetailTexture() : _textBank->GetWaterBumpMap();
            if (tex)
            {
                _textBank->UseMipmap(tex, 0, 0);
                _tlSecondaryTexture = tex->GpuHandle();
                if (!isDetailTagged && _tlSecondaryTexture != 0)
                    _tlObject.flags[1] = 3.0f;
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
        const bool forceBlend =
            d.blend == render::BlendMode::AlphaBlend &&
            (d.fog == render::FogMode::AlphaFog || render::Has(spec.backend, render::Backend::IsAlpha));
        const bool isBlend = !isCutout && (isAlpha || forceBlend);
        // Measured cutouts stay in ShapeDraw's opaque/depth-resolved pass and
        // use a real discard rather than alpha blending. Blending while also
        // writing depth makes a rope look plausible against its own tent but
        // leaves its partially transparent edge depth-occluding soldiers and
        // weapons behind it. Genuine translucent surfaces remain blended.
        _tlSectionBlendMode = isBlend ? render::BlendMode::AlphaBlend : render::BlendMode::Opaque;
        // Match GL33/BuildRenderPassDescriptor: alpha/blend does not by
        // itself disable depth writes. Only NoZWrite/NoZBuf/shadow change
        // depth mode. Measured cutouts were separated above; genuinely
        // blended features still use their descriptor-selected depth mode.
        _tlSectionDepthMode = d.depth;
    }
}

// Ported from GL33's PrepareMeshTL/PrepareMeshTLImpl (EngineGL33_Mesh.cpp).
// Caches the per-frame constants (view/sun-direction/fog) across a frame's
// whole run of draws, like GL33's _frameState/BeginPass -- see _tlFrameValid's
// comment for why projection and the sun-enabled flag are excluded from the
// cache and still rebuilt below on every call.
void EngineMTL::PrepareMeshTL(const LightList& /*lights*/, const Matrix4& modelToWorld, const render::LegacySpec& spec)
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

    if (!_tlFrameValid)
    {
        // Per-frame: view (rotation only -- camera-relative rendering zeroes
        // the translation).
        GfxMatrix view;
        ConvertMatrix(view, camera->InverseScaled());
        view._41 = view._42 = view._43 = 0;
        std::memcpy(_tlFrame.view.m, &view, sizeof(view));

        const Vector3 sunDir = sun->Direction();
        _tlFrame.sunDirAndEnabled[0] = static_cast<float>(sunDir.X());
        _tlFrame.sunDirAndEnabled[1] = static_cast<float>(sunDir.Y());
        _tlFrame.sunDirAndEnabled[2] = static_cast<float>(sunDir.Z());

        const float fogStart = GScene->GetFogMinRange();
        const float fogEnd = GScene->GetFogMaxRange();
        _tlFrame.fogParams[0] = fogStart;
        _tlFrame.fogParams[1] = (fogEnd > fogStart) ? 1.0f / (fogEnd - fogStart) : 0.0f;
        _tlFrame.fogParams[2] = 1.0f;
        _tlFrame.fogParams[3] = _debugFlatColor ? 1.0f : 0.0f;
        _tlFrame.fogColor[0] = _fogColor.R();
        _tlFrame.fogColor[1] = _fogColor.G();
        _tlFrame.fogColor[2] = _fogColor.B();
        _tlFrame.fogColor[3] = 1.0f;
        const Vector3 waterSunDir = sun->SunDirection();
        _tlFrame.waterSunDirAndTime[0] = static_cast<float>(waterSunDir.X());
        _tlFrame.waterSunDirAndTime[1] = static_cast<float>(waterSunDir.Y());
        _tlFrame.waterSunDirAndTime[2] = static_cast<float>(waterSunDir.Z());
        _tlFrame.waterSunDirAndTime[3] = Glob.time.toFloat();

        _tlFrameValid = true;
    }

    GfxMatrix projection;
    ConvertProjectionMatrix(projection, camera->ProjectionNormal(), CanZBias() ? 0 : _bias);
    std::memcpy(_tlFrame.projection.m, &projection, sizeof(projection));
    _tlFrame.sunDirAndEnabled[3] = _sunEnabled ? 1.0f : 0.0f;
    std::memcpy(_tlFrame.nightEyeCoef, _nightEyeCoef, sizeof(_nightEyeCoef));

    // Per-object: camera-relative world matrix (translation has the camera
    // position subtracted), same as GL33's PrepareMeshTLImpl.
    GfxMatrix world;
    ConvertMatrix(world, modelToWorld);
    const Vector3 camPos = camera->Position();
    _tlFrame.hmParams[0] = _heightmapValid ? _hmInvGrid : 0.0f;
    _tlFrame.hmParams[1] = static_cast<float>(camPos.X());
    _tlFrame.hmParams[2] = static_cast<float>(camPos.Z());
    _tlFrame.hmParams[3] = static_cast<float>(camPos.Y());
    _tlFrame.landGrid[0] = _hmInvLandGrid;
    _tlFrame.landGrid[1] = (_heightmapValid && _hmInvLandGrid > 0) ? _hmInvGrid / _hmInvLandGrid : 0.0f;
    world._41 -= static_cast<float>(camPos.X());
    world._42 -= static_cast<float>(camPos.Y());
    world._43 -= static_cast<float>(camPos.Z());
    std::memcpy(_tlObject.world.m, &world, sizeof(world));

    // IsColored objects carry their opacity + fade in the scene constant
    // colour (GL33's PrepareMeshTL does the same upload).
    Color constColor = HWhite;
    if (render::Has(spec.routing, render::Routing::IsColored))
        constColor = GScene->GetConstantColor();
    _tlObject.constColor[0] = constColor.R();
    _tlObject.constColor[1] = constColor.G();
    _tlObject.constColor[2] = constColor.B();
    _tlObject.constColor[3] = constColor.A();

    _currentDrawItem.worldMatrix = world;
    _currentDrawItem.specFlags = spec;
    _currentDrawItem.bias = _bias;
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
                             _tlCurrentTexture, _tlSecondaryTexture, _tlObject, _tlFrame, _tlSectionDepthMode,
                             _tlSectionBlendMode, _tlSectionSampler, _tlSectionSurfaceMode, _tlSectionShader);

    DrawItem item = _currentDrawItem;
    item.isTLDraw = true;
    item.sectionBegin = beg;
    item.sectionEnd = end;
    item.firstIndex = firstIndex;
    item.indexCount = indexCount;
    item.vertexBuffer = buf;
    item.backendMeshHandle = static_cast<std::uint32_t>(buf->VertexBufferHandle());
    item.backendTextureHandle = static_cast<std::uint32_t>(_tlCurrentTexture);
    item.backendTexture1Handle = static_cast<std::uint32_t>(_tlSecondaryTexture);
    item.passId = SpecToPassId(item.specFlags);
    _drawItems.push_back(item);
}

void EngineMTL::FlushQueues()
{
    _bootstrap.FlushTriangles2D();
}

// Ported from EngineGL33::UploadLocalLights: raw light colours, positions
// camera-relative to match the world matrices. SetMaterial and
// InstancedRunAdd select from it by index.
void EngineMTL::UploadLocalLights(const LightList& aLights)
{
    _localLightIndices.clear();
    LocalLightTableMTL table = {};
    Vector3 camPos = VZero;
    if (GScene != nullptr && GScene->GetCamera() != nullptr)
        camPos = GScene->GetCamera()->Position();

    int n = 0;
    for (int i = 0; i < aLights.Size() && n < kMaxLightTableMTL; i++)
    {
        Light* light = aLights[i];
        if (!light)
            continue;
        LightDescription desc;
        light->GetDescription(desc);
        const bool isSpot = desc.type == LTSpotLight;
        if (desc.type != LTPoint && !isSpot)
            continue;

        _localLightIndices[light] = n;
        LightMTL& l = table.lights[n];
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
        l.diffuse[0] = desc.diffuse.R();
        l.diffuse[1] = desc.diffuse.G();
        l.diffuse[2] = desc.diffuse.B();
        l.ambient[0] = desc.ambient.R();
        l.ambient[1] = desc.ambient.G();
        l.ambient[2] = desc.ambient.B();
        n++;
    }
    table.count[0] = static_cast<float>(n);
    _bootstrap.UploadLocalLightTable(table);
}

void EngineMTL::UpdateShadowMapLitState()
{
    float ctl[4] = {0.0f, 0.0f, 1.0f, 0.0f};
    float splits[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float cascadeCtl[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float camFwd[4] = {0.0f, 0.0f, 1.0f, 0.0f};
    if (_shadowTuning.enabled && _shadowMapActive && _shadowCascades > 0)
    {
        ctl[0] = 1.0f;
        // Full darkness by day, none at night (the scene drives the sun factor).
        ctl[2] = 1.0f - _shadowSunFactor * (1.0f - _shadowTuning.darkness);
        ctl[3] = (_shadowMapRes > 0) ? (1.0f / static_cast<float>(_shadowMapRes)) : 0.0f;
        cascadeCtl[0] = static_cast<float>(_shadowCascades);
        cascadeCtl[1] = _shadowTuning.fadeRange;
        cascadeCtl[2] = _shadowTuning.biasBase;
        cascadeCtl[3] = static_cast<float>(_shadowOmniCount);
        for (int i = 0; i < _shadowCascades; i++)
            splits[i] = _shadowSplits[i];
        camFwd[0] = _shadowCamFwd[0];
        camFwd[1] = _shadowCamFwd[1];
        camFwd[2] = _shadowCamFwd[2];
        std::memcpy(_tlFrame.cascadeVP, _shadowMapVP, sizeof(float) * 16 * static_cast<size_t>(_shadowCascades));
    }
    std::memcpy(_tlFrame.shadowCtl, ctl, sizeof(ctl));
    std::memcpy(_tlFrame.cascadeSplits, splits, sizeof(splits));
    std::memcpy(_tlFrame.cascadeCtl, cascadeCtl, sizeof(cascadeCtl));
    std::memcpy(_tlFrame.camFwd, camFwd, sizeof(camFwd));
}

void EngineMTL::RenderShadowDepthScene(const float* lightVPs, const float* splitViewDist, const float* camFwd3,
                                       int numCascades, int omniCount, int res, const ShadowCasterSet& casters)
{
    if (numCascades > kShadowCascadesMTL)
        numCascades = kShadowCascadesMTL;

    // Resolve each alpha batch's caster texture (loading its base mip if the
    // depth pass beat the lit draw to it), as SetTexture does.
    std::vector<ShadowAlphaBatchMTL> batches;
    batches.reserve(static_cast<size_t>(casters.alphaBatchCount));
    for (int b = 0; b < casters.alphaBatchCount; b++)
    {
        const ShadowCasterBatch& src = casters.alphaBatches[b];
        TextureMTL* tex = dynamic_cast<TextureMTL*>(src.texture);
        if (_textBank && tex)
            _textBank->UseMipmap(tex, 0, 0);
        batches.push_back({tex ? tex->GpuHandle() : 0, src.firstVertex, src.vertexCount});
    }

    const bool queued = numCascades >= 1 && _bootstrap.QueueShadowCascades(lightVPs, numCascades, res, casters.solidXYZ,
                                                                           casters.solidVertexCount, casters.alphaXYZUV,
                                                                           casters.alphaVertexCount, batches.data(),
                                                                           static_cast<int>(batches.size()));
    if (!queued)
    {
        _shadowMapActive = false;
        return;
    }
    _shadowMapRes = res;
    _shadowCascades = numCascades;
    _shadowOmniCount = (omniCount < 0) ? 0 : (omniCount > numCascades ? numCascades : omniCount);
    std::memcpy(_shadowMapVP, lightVPs, sizeof(float) * 16 * static_cast<size_t>(numCascades));
    for (int i = 0; i < numCascades; i++)
        _shadowSplits[i] = splitViewDist[i];
    _shadowCamFwd[0] = camFwd3[0];
    _shadowCamFwd[1] = camFwd3[1];
    _shadowCamFwd[2] = camFwd3[2];
    _shadowMapActive = true;
}

bool EngineMTL::DumpShadowMap(const char* path)
{
    if (!path || !_shadowMapActive)
        return false;
    std::vector<float> depth;
    int res = 0;
    if (!_bootstrap.ReadShadowCascade0(depth, res) || res <= 0)
        return false;
    // Same grey mapping as GL33's dump; Metal's rows are already top-down.
    std::vector<uint8_t> gray(static_cast<size_t>(res) * res);
    for (size_t i = 0; i < gray.size(); i++)
    {
        const float d = depth[i];
        gray[i] =
            (d >= 0.999f) ? static_cast<uint8_t>(35) : static_cast<uint8_t>((0.15f + (1.0f - d) * 0.85f) * 255.0f);
    }
    return PNGWriter::WritePNG(path, res, res, 1, gray.data());
}

void EngineMTL::SetTerrainHeightmap(const float* heights, int width, int height, float invGrid, float invLandGrid)
{
    if (!_bootstrap.SetTerrainHeightmap(heights, width, height))
        return;
    _heightmapValid = true;
    _hmInvGrid = invGrid;
    _hmInvLandGrid = invLandGrid;
}

void EngineMTL::SetLandClipParams(float mode, Vector3Par boundingCenter)
{
    _tlObject.landClip[3] = mode;
    const bool active = mode > 0.5f;
    _tlObject.landClip[0] = active ? static_cast<float>(boundingCenter.X()) : 0.0f;
    _tlObject.landClip[1] = active ? static_cast<float>(boundingCenter.Y()) : 0.0f;
    _tlObject.landClip[2] = active ? static_cast<float>(boundingCenter.Z()) : 0.0f;
}

std::array<std::uint32_t, 4> EngineMTL::PackLightIndices(const LightList& lights) const
{
    int idx[GL33LightIndices::Capacity];
    int n = 0;
    for (int i = 0; i < lights.Size() && n < GL33LightIndices::Capacity; i++)
    {
        auto it = _localLightIndices.find(lights[i]);
        if (it != _localLightIndices.end())
            idx[n++] = it->second;
    }
    return GL33LightIndices::Pack(idx, n);
}

bool EngineMTL::InstancedRunAdd(const Matrix4& modelToWorld, const LightList& lights)
{
    if (_instPending >= kMaxInstancesMTL || GScene == nullptr || GScene->GetCamera() == nullptr)
        return false;
    InstanceMTL& inst = _instArray[_instPending];
    GfxMatrix world;
    ConvertMatrix(world, modelToWorld);
    const Vector3 camPos = GScene->GetCamera()->Position();
    world._41 -= static_cast<float>(camPos.X());
    world._42 -= static_cast<float>(camPos.Y());
    world._43 -= static_cast<float>(camPos.Z());
    std::memcpy(inst.world.m, &world, sizeof(world));

    const auto packed = PackLightIndices(lights);
    std::memcpy(inst.lightIdx, packed.data(), sizeof(inst.lightIdx));
    ++_instPending;
    return true;
}

void EngineMTL::BeginInstancedRunUpload()
{
    _bootstrap.UploadInstances(_instArray, _instPending);
    _instCount = _bootstrap.InstanceCount();
    // A failed upload draws the head alone; impure makes the scene redraw the tail.
    _instImpure = _instCount < _instPending;
}

void EngineMTL::DrawPolygon(const VertexIndex* i, int n)
{
    DrawIndexedFan3D(i, n);

    DrawItem item = {};
    item.isTLDraw = false;
    item.specFlags = _currentTriSpec;
    item.passId = SpecToPassId(_currentTriSpec);
    _drawItems.push_back(item);
}

// Stars and laser-target dots: each screen-space point becomes a 2x2 quad
// whose corner alphas carry the sub-pixel position, same as GL33's
// DrawPoints. Points are never fogged (specular 0xff000000 = full vFogTC).
void EngineMTL::DrawPoints(int beg, int end)
{
    if (_mesh == nullptr)
        return;

    const Rect2DAbs fullScreen(0, 0, static_cast<float>(_w), static_cast<float>(_h));
    for (int i = beg; i < end; i++)
    {
        if (_mesh->Clip(i) & ClipAll)
            continue;
        const TLVertex& v = _mesh->GetVertex(i);
        const PackedColor color = v.color;
        if (color.A8() < 8)
            continue;

        const int xI = toIntFloor(v.pos[0]);
        const int yI = toIntFloor(v.pos[1]);
        if (xI < 0 || xI + 2 > _w || yI < 0 || yI + 2 > _h)
            continue;
        const float xFrac = v.pos[0] - xI;
        const float yFrac = v.pos[1] - yI;
        const float a = color.A8();
        auto fracAlpha = [](float alpha)
        {
            int ia = toInt(alpha);
            saturate(ia, 0, 255);
            return ia;
        };

        const float x0 = xI + 0.5f, x1 = xI + 2.5f;
        const float y0 = yI + 0.5f, y1 = yI + 2.5f;
        const float xy[8] = {x0, y0, x1, y0, x1, y1, x0, y1};
        const float z[4] = {v.pos.Z(), v.pos.Z(), v.pos.Z(), v.pos.Z()};
        const float rhw[4] = {v.rhw, v.rhw, v.rhw, v.rhw};
        const float uv[8] = {v.t0.u, v.t0.v, v.t0.u, v.t0.v, v.t0.u, v.t0.v, v.t0.u, v.t0.v};
        const PackedColor colors[4] = {
            PackedColorRGB(color, fracAlpha((1 - xFrac) * (1 - yFrac) * a)),
            PackedColorRGB(color, fracAlpha(xFrac * (1 - yFrac) * a)),
            PackedColorRGB(color, fracAlpha(xFrac * yFrac * a)),
            PackedColorRGB(color, fracAlpha((1 - xFrac) * yFrac * a)),
        };
        const PackedColor noFog(0xff000000);
        const PackedColor specular[4] = {noFog, noFog, noFog, noFog};

        DrawFan2D(xy, z, rhw, uv, nullptr, colors, 4, _currentTriTexture, _currentTriSecondaryTexture, fullScreen,
                  _currentTriDepthMode, _currentTriBlendMode, _currentTriSampler, _currentTriSurfaceMode,
                  _currentTriShader, _currentTriAlphaMode, _currentTriAlphaRef, specular, _currentTriDetailMode);
    }
}

void EngineMTL::EnableNightEye(float night)
{
    if (std::fabs(_nightEye - night) < 0.01f)
        return;
    FlushQueues();
    _nightEye = night;
    if (_nightEye > 0.01f)
    {
        _nightEyeCoef[0] = 0.2f;
        _nightEyeCoef[1] = 0.9f;
        _nightEyeCoef[2] = 0.4f;
        _nightEyeCoef[3] = 1.0f - _nightEye;
    }
    else
    {
        _nightEyeCoef[0] = 0.0f;
        _nightEyeCoef[1] = 0.0f;
        _nightEyeCoef[2] = 0.0f;
        _nightEyeCoef[3] = 1.0f;
    }
    _bootstrap.SetNightEyeCoef(_nightEyeCoef);
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
    const render::SamplerMode sampler = {render::SamplerFilter::Linear, true, true};
    DrawFan2D(xy, z, rhw, uv, nullptr, colors, 4, 0, 0, fullScreen, render::DepthMode::ReadOnly,
              render::BlendMode::AlphaBlend, sampler);
}

bool EngineMTL::SwitchRes(int w, int h, int bpp)
{
    _pixelSize = bpp;
    if (_sdlWindow && _windowed)
        SDL_SetWindowSize(_sdlWindow, w, h);

    int cw = 0, ch = 0;
    if (_sdlWindow)
        SDL_GetWindowSizeInPixels(_sdlWindow, &cw, &ch);
    const int rawW = cw > 0 ? cw : w;
    const int rawH = ch > 0 ? ch : h;
    _bootstrap.OnWindowResized(rawW, rawH);
    _w = _bootstrap.DrawableWidth() > 0 ? _bootstrap.DrawableWidth() : rawW;
    _h = _bootstrap.DrawableHeight() > 0 ? _bootstrap.DrawableHeight() : rawH;
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

    if (mode == WindowMode::Windowed)
    {
        // A window created fullscreen/borderless never had SDL_WINDOW_RESIZABLE,
        // and SDL_SetWindowBordered does not add it.
        SDL_SetWindowResizable(_sdlWindow, true);
        if (_windowedRestoreW > 0)
            SDL_SetWindowSize(_sdlWindow, _windowedRestoreW, _windowedRestoreH);
        SDL_SetWindowPosition(_sdlWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }

    int cw = 0, ch = 0;
    SDL_GetWindowSizeInPixels(_sdlWindow, &cw, &ch);
    _bootstrap.OnWindowResized(cw, ch);
    _w = _bootstrap.DrawableWidth() > 0 ? _bootstrap.DrawableWidth() : cw;
    _h = _bootstrap.DrawableHeight() > 0 ? _bootstrap.DrawableHeight() : ch;

    OnFullscreenChanged(_windowed);
    return true;
}

void EngineMTL::OnWindowResized(int w, int h)
{
    if (w <= 0 || h <= 0)
        return;

    _bootstrap.OnWindowResized(w, h);
    const int drawableW = _bootstrap.DrawableWidth();
    const int drawableH = _bootstrap.DrawableHeight();
    _w = drawableW > 0 ? drawableW : w;
    _h = drawableH > 0 ? drawableH : h;
    if (_windowed)
    {
        _windowedRestoreW = w;
        _windowedRestoreH = h;
    }

    LOG_DEBUG(Graphics, "MTL: OnWindowResized raw={}x{} drawable={}x{}", w, h, _w, _h);
    FireResizePostHook(_w, _h);
}

void EngineMTL::OnWindowSafeAreaChanged()
{
    if (!_sdlWindow)
        return;

    int cw = 0;
    int ch = 0;
    SDL_GetWindowSizeInPixels(_sdlWindow, &cw, &ch);
    OnWindowResized(cw, ch);
}

void EngineMTL::OnFullscreenChanged(bool windowed)
{
    _windowed = windowed;
    OnWindowSafeAreaChanged();
}

RString EngineMTL::GetDebugName() const
{
    std::string name = _bootstrap.GetRendererName();
    return name.empty() ? "Metal" : RString("Metal — ") + name.c_str();
}

RString EngineMTL::GetRendererName() const
{
    return "Metal";
}

WindowMode EngineMTL::GetCurrentWindowMode() const
{
    if (!_sdlWindow)
        return WindowMode::Windowed;
    return _windowMode;
}

void EngineMTL::ListMonitors(FindArray<MonitorInfo>& ret)
{
    ret.Clear();
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    if (!displays)
        return;
    for (int i = 0; i < count; ++i)
    {
        const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(displays[i]);
        const char* name = SDL_GetDisplayName(displays[i]);
        MonitorInfo info;
        info.index = i;
        info.name = name ? name : "Unknown";
        info.w = mode ? mode->w : 0;
        info.h = mode ? mode->h : 0;
        info.refresh = mode ? (int)(mode->refresh_rate + 0.5f) : 0;
        ret.Add(info);
    }
    SDL_free(displays);
}

int EngineMTL::GetCurrentMonitor() const
{
    if (!_sdlWindow)
        return 0;
    SDL_DisplayID id = SDL_GetDisplayForWindow(_sdlWindow);
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    int idx = 0;
    if (displays)
    {
        for (int i = 0; i < count; ++i)
            if (displays[i] == id)
            {
                idx = i;
                break;
            }
        SDL_free(displays);
    }
    return idx;
}

bool EngineMTL::SwitchMonitor(int idx)
{
    if (!_sdlWindow)
        return false;
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    if (!displays || idx < 0 || idx >= count)
    {
        SDL_free(displays);
        return false;
    }
    SDL_DisplayID target = displays[idx];
    SDL_free(displays);
    SDL_Rect bounds;
    if (!SDL_GetDisplayBounds(target, &bounds))
        return false;
    int windowW = _w;
    int windowH = _h;
    SDL_GetWindowSize(_sdlWindow, &windowW, &windowH);
    SDL_SetWindowPosition(_sdlWindow, bounds.x + (bounds.w - windowW) / 2, bounds.y + (bounds.h - windowH) / 2);
    return true;
}

bool EngineMTL::GetDesktopDisplayMode(int& w, int& h, int& refresh) const
{
    if (!_sdlWindow)
        return false;
    SDL_DisplayID display = SDL_GetDisplayForWindow(_sdlWindow);
    if (!display)
        display = SDL_GetPrimaryDisplay();
    return ReadDisplayMode(SDL_GetDesktopDisplayMode(display), w, h, refresh);
}

bool EngineMTL::GetCurrentDisplayMode(int& w, int& h, int& refresh) const
{
    if (!_sdlWindow)
        return false;
    SDL_DisplayID display = SDL_GetDisplayForWindow(_sdlWindow);
    if (!display)
        display = SDL_GetPrimaryDisplay();
    return ReadDisplayMode(SDL_GetCurrentDisplayMode(display), w, h, refresh);
}

bool EngineMTL::GetRequestedFullscreenMode(int& w, int& h, int& refresh) const
{
    if (!_sdlWindow)
        return false;
    return ReadDisplayMode(SDL_GetWindowFullscreenMode(_sdlWindow), w, h, refresh);
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

void EngineMTL::ResetForRemount()
{
    FlushQueues();
    if (_textBank)
    {
        _textBank->ReleaseAllTextures();
        _textBank->ReleaseDetailTextures();
    }
}

int EngineMTL::GpuHandleOf(Texture* tex)
{
    TextureMTL* mtlTex = dynamic_cast<TextureMTL*>(tex);
    if (mtlTex == nullptr)
        return 0;
    if (_textBank)
        _textBank->EnsureResident(mtlTex);
    return mtlTex->GpuHandle();
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
