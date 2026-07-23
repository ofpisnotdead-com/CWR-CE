#include <PoseidonGL33/EngineGL33.hpp>
#include <PoseidonGL33/GL33BindCache.hpp>
#include <Poseidon/Graphics/Core/GLClear.hpp>
#include <Poseidon/Graphics/Core/GLCullState.hpp>
#include <Poseidon/Graphics/Core/GLPipelineState.hpp>
#include <PoseidonGL33/TextureGL33.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/Core/Config/Config.hpp>

#include <Poseidon/Graphics/Shared/WindowMetrics.hpp>
#include <Poseidon/Graphics/Shared/WindowPlacement.hpp>

#include <glad/gl.h>
#include <SDL3/SDL.h>

using Poseidon::Foundation::MStorage;

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

bool FindExclusiveDisplayMode(SDL_DisplayID display, int requestedW, int requestedH, int requestedRefresh,
                              SDL_DisplayMode& out)
{
    int count = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &count);
    if (!modes)
        return false;

    const SDL_DisplayMode* best = nullptr;
    int bestRefreshDelta = INT_MAX;
    for (int i = 0; i < count; ++i)
    {
        const SDL_DisplayMode* mode = modes[i];
        if (!mode || mode->w != requestedW || mode->h != requestedH)
            continue;

        const int modeRefresh = (int)(mode->refresh_rate + 0.5f);
        const int refreshDelta = requestedRefresh > 0 ? abs(modeRefresh - requestedRefresh) : 0;
        if (!best || refreshDelta < bestRefreshDelta)
        {
            best = mode;
            bestRefreshDelta = refreshDelta;
            if (refreshDelta == 0)
                break;
        }
    }

    const bool found = best != nullptr;
    if (found)
        out = *best;
    SDL_free(modes);
    return found;
}

void LogExclusiveModeStatus(SDL_Window* window, int requestedW, int requestedH, int requestedRefresh)
{
    if (!window)
        return;

    const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
    const SDL_DisplayMode* requested = SDL_GetWindowFullscreenMode(window);
    const SDL_DisplayMode* current = SDL_GetCurrentDisplayMode(display ? display : SDL_GetPrimaryDisplay());
    const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(display ? display : SDL_GetPrimaryDisplay());

    int actualW = 0;
    int actualH = 0;
    int actualRefresh = 0;
    int desktopW = 0;
    int desktopH = 0;
    int desktopRefresh = 0;
    ReadDisplayMode(current, actualW, actualH, actualRefresh);
    ReadDisplayMode(desktop, desktopW, desktopH, desktopRefresh);

    LOG_INFO(Graphics,
             "GL33: Exclusive request={}x{}@{} SDL-requested={}x{}@{} current-display={}x{}@{} desktop={}x{}@{}",
             requestedW, requestedH, requestedRefresh, requested ? requested->w : 0, requested ? requested->h : 0,
             requested ? (int)(requested->refresh_rate + 0.5f) : 0, actualW, actualH, actualRefresh, desktopW, desktopH,
             desktopRefresh);

    if (requestedW > 0 && requestedH > 0 && (actualW != requestedW || actualH != requestedH))
    {
        LOG_WARN(Graphics,
                 "GL33: Exclusive request did not change the active display mode; the OS/compositor is likely scaling "
                 "a {}x{} render inside {}x{} fullscreen",
                 requestedW, requestedH, actualW, actualH);
    }
}

bool ApplyExclusiveDisplayMode(SDL_Window* window, SDL_DisplayID display, int requestedW, int requestedH,
                               int requestedRefresh)
{
    SDL_DisplayMode target{};
    if (FindExclusiveDisplayMode(display, requestedW, requestedH, requestedRefresh, target))
    {
        return SDL_SetWindowFullscreenMode(window, &target);
    }

    if (const SDL_DisplayMode* dm = SDL_GetDesktopDisplayMode(display))
        target = *dm;
    target.w = requestedW;
    target.h = requestedH;
    if (requestedRefresh > 0)
        target.refresh_rate = (float)requestedRefresh;
    return SDL_SetWindowFullscreenMode(window, &target);
}
} // namespace

bool EngineGL33::InitDrawDone()
{
    return _frameOpen;
}

bool EngineGL33::IsAbleToDraw()
{
    return _sdlWindow != nullptr && _glContext != nullptr;
}
bool EngineGL33::IsAbleToDrawCheckOnly()
{
    return _sdlWindow != nullptr && _glContext != nullptr;
}

void EngineGL33::InitDraw(bool clear, PackedColor color)
{
    if (_frameOpen)
    {
        LOG_DEBUG(Graphics, "InitDraw done twice");
        return;
    }

    if (!_glContext)
        return;

    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = (color & 0xFF) / 255.0f;

    if (clear)
    {
        Poseidon::render::pipeline::SetClearColor(r, g, b, 1.0f);
    }

    GLbitfield clearMask = GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
    if (clear)
        clearMask |= GL_COLOR_BUFFER_BIT;
    Poseidon::render::clear::WithMask(clearMask);

    if (_textBank)
        _textBank->StartFrame();

    // Invalidate the material/light cache at frame start so it never lives across
    // frames. Within a frame the cache key (material + DisableSun + light-list
    // signature) covers every per-draw-varying input; the frame-constant globals
    // the upload also folds in — MainLight NightEffect and sun diffuse/ambient —
    // are not in the key, so a cache that survived into the next frame could
    // freeze a dusk/dawn transition on a static same-material draw. Resetting per
    // frame keeps those fresh without widening the per-draw key.
    _materialSetSpec = -1;

    base::InitDraw();

    _frameOpen = true;
    _drawItems.clear();

    TLMaterial invalidMat;
    invalidMat.diffuse = Color(-1, -1, -1, -1);
    invalidMat.ambient = Color(-1, -1, -1, -1);
    invalidMat.forcedDiffuse = Color(-1, -1, -1, -1);
    invalidMat.emmisive = Color(-1, -1, -1, -1);
    invalidMat.specFlags = 0;

    LightList lights;
    DoSetMaterialAndLights(invalidMat, lights, Poseidon::render::LegacySpec{});
}

void EngineGL33::FinishDraw()
{
    if (_frameOpen)
    {
        // Safety: if the frame ended while the world viewport was still
        // cropped (no 2D draw followed the 3D pass), restore it + bars.
        EndWorldViewport();
        // The active pass debug group must not span the swap.
        ClosePassDebugGroup();
        base::FinishDraw();
        base::DrawFinishTexts();

        CloseAllQueues(_queueNo);

        LOG_DEBUG(
            Graphics,
            "GL33: frame meshDraws={} totalIndices={} addVertCalls={} totalVerts={} queueFanCalls={} totalFanTris={}",
            _dbgMeshDrawCalls, _dbgMeshTotalIndices, _dbgAddVerticesCalls, _dbgTotalVertices, _dbgQueueFanCalls,
            _dbgTotalFanTris);
        _dbgMeshDrawCalls = 0;
        _dbgMeshTotalIndices = 0;
        _dbgAddVerticesCalls = 0;
        _dbgTotalVertices = 0;
        _dbgQueueFanCalls = 0;
        _dbgTotalFanTris = 0;

        _frameOpen = false;
        DiscardVB();

        if (_textBank)
            _textBank->FinishFrame();
    }
}

void EngineGL33::NextFrame()
{
    BackToFront();
    base::NextFrame();
}

void EngineGL33::DrawTestPattern(const char* name)
{
    if (!_glContext || !_frameOpen)
        return;

    auto makeTL = [](float x, float y, float z, DWORD col) -> TLVertex
    {
        TLVertex v;
        v.pos = Vector3P(x, y, z);
        v.rhw = 1.0f;
        v.color = PackedColor(col);
        v.specular = PackedColor(DWORD(0xFF000000));
        v.t0 = {0.0f, 0.0f};
        v.t1 = {0.0f, 0.0f};
        return v;
    };

    auto setupFlatDraw = [&]()
    {
        SelectVertexShader(VSScreen);
        UploadVSScreenConstants();
        SelectPixelShader(PSFlat);
        Poseidon::render::cull::None();
    };

    auto drawQuad = [&](TLVertex v[4])
    {
        TLVertex triList[6] = {v[0], v[1], v[2], v[0], v[2], v[3]};
        GL33Bind::Vao(_vaoScreen);
        glBindBuffer(GL_ARRAY_BUFFER, _vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(triList), triList);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    };

    auto clearGL = [&](DWORD color)
    {
        float c[4] = {((color >> 16) & 0xFF) / 255.0f, ((color >> 8) & 0xFF) / 255.0f, (color & 0xFF) / 255.0f,
                      ((color >> 24) & 0xFF) / 255.0f};
        Poseidon::render::pipeline::SetClearColor(c[0], c[1], c[2], c[3]);
        Poseidon::render::clear::ColorDepthStencil();
    };

    if (strcmp(name, "gradient3d") == 0)
    {
        setupFlatDraw();
        InvalidatePipelineCache();
        ApplyBlendMode(BlendMode::Opaque);
        ApplyDepthMode(DepthMode::Normal);

        float w = (float)_w, h = (float)_h;
        TLVertex v[4] = {
            makeTL(0, 0, 0.5f, 0xFFFF0000),
            makeTL(w, 0, 0.5f, 0xFF00FF00),
            makeTL(w, h, 0.5f, 0xFFFFFFFF),
            makeTL(0, h, 0.5f, 0xFF0000FF),
        };
        drawQuad(v);
        return;
    }

    if (strcmp(name, "colorbar") == 0)
    {
        setupFlatDraw();
        InvalidatePipelineCache();
        ApplyBlendMode(BlendMode::Opaque);
        ApplyDepthMode(DepthMode::Disabled);

        float barW = _w / 5.0f;
        DWORD colors[5] = {0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFF00, 0xFFFF00FF};
        for (int i = 0; i < 5; i++)
        {
            float x0 = barW * i, x1 = barW * (i + 1);
            TLVertex v[4] = {
                makeTL(x0, 0, 0.5f, colors[i]),
                makeTL(x1, 0, 0.5f, colors[i]),
                makeTL(x1, (float)_h, 0.5f, colors[i]),
                makeTL(x0, (float)_h, 0.5f, colors[i]),
            };
            drawQuad(v);
        }
        return;
    }

    if (strcmp(name, "clear_blue") == 0)
    {
        clearGL(0xFF0040FF);
        return;
    }
    if (strcmp(name, "clear_magenta") == 0)
    {
        clearGL(0xFFFF00FF);
        return;
    }

    if (strcmp(name, "quad2d") == 0)
    {
        clearGL(0xFF000000);
        setupFlatDraw();
        InvalidatePipelineCache();
        ApplyBlendMode(BlendMode::Opaque);
        ApplyDepthMode(DepthMode::Disabled);

        float w = (float)_w, h = (float)_h;
        {
            TLVertex v[4] = {makeTL(0, 0, 0.5f, 0xFF000080), makeTL(w, 0, 0.5f, 0xFF000080),
                             makeTL(w, h, 0.5f, 0xFF000080), makeTL(0, h, 0.5f, 0xFF000080)};
            drawQuad(v);
        }
        {
            float m = 0.25f;
            TLVertex v[4] = {makeTL(w * m, h * m, 0.5f, 0xFFCC0000), makeTL(w * (1 - m), h * m, 0.5f, 0xFFCC0000),
                             makeTL(w * (1 - m), h * (1 - m), 0.5f, 0xFFCC0000),
                             makeTL(w * m, h * (1 - m), 0.5f, 0xFFCC0000)};
            drawQuad(v);
        }
        {
            float m = 0.40f;
            TLVertex v[4] = {makeTL(w * m, h * m, 0.5f, 0xFFFFFFFF), makeTL(w * (1 - m), h * m, 0.5f, 0xFFFFFFFF),
                             makeTL(w * (1 - m), h * (1 - m), 0.5f, 0xFFFFFFFF),
                             makeTL(w * m, h * (1 - m), 0.5f, 0xFFFFFFFF)};
            drawQuad(v);
        }
        return;
    }

    if (strcmp(name, "lines2d") == 0)
    {
        clearGL(0xFF202020);
        setupFlatDraw();
        InvalidatePipelineCache();
        ApplyBlendMode(BlendMode::Opaque);
        ApplyDepthMode(DepthMode::Disabled);

        float w = (float)_w, h = (float)_h;
        float lw = 3.0f;

        auto drawLine = [&](float x0, float y0, float x1, float y1, DWORD col)
        {
            float dx = x1 - x0, dy = y1 - y0;
            float len = sqrtf(dx * dx + dy * dy);
            if (len < 0.001f)
                return;
            float nx = -dy / len * lw * 0.5f, ny = dx / len * lw * 0.5f;
            TLVertex v[4] = {makeTL(x0 + nx, y0 + ny, 0.5f, col), makeTL(x1 + nx, y1 + ny, 0.5f, col),
                             makeTL(x1 - nx, y1 - ny, 0.5f, col), makeTL(x0 - nx, y0 - ny, 0.5f, col)};
            drawQuad(v);
        };

        drawLine(2, 2, w - 2, 2, 0xFFFF0000);
        drawLine(w - 2, 2, w - 2, h - 2, 0xFFFF0000);
        drawLine(w - 2, h - 2, 2, h - 2, 0xFFFF0000);
        drawLine(2, h - 2, 2, 2, 0xFFFF0000);
        drawLine(0, 0, w, h, 0xFF00FF00);
        drawLine(w, 0, 0, h, 0xFF00FF00);
        drawLine(w / 2, 0, w / 2, h, 0xFFFFFFFF);
        drawLine(0, h / 2, w, h / 2, 0xFFFFFFFF);
        return;
    }

    if (strcmp(name, "alpha_blend") == 0)
    {
        clearGL(0xFFFFFFFF);
        setupFlatDraw();
        InvalidatePipelineCache();
        ApplyBlendMode(BlendMode::AlphaBlend);
        ApplyDepthMode(DepthMode::Disabled);

        float w = (float)_w, h = (float)_h;

        DWORD colors[3] = {0x80FF0000, 0x8000FF00, 0x800000FF};
        float cx[3] = {w * 0.35f, w * 0.65f, w * 0.50f};
        float cy[3] = {h * 0.35f, h * 0.35f, h * 0.65f};
        float sz = w * 0.25f;
        for (int i = 0; i < 3; i++)
        {
            TLVertex v[4] = {
                makeTL(cx[i] - sz, cy[i] - sz, 0.5f, colors[i]), makeTL(cx[i] + sz, cy[i] - sz, 0.5f, colors[i]),
                makeTL(cx[i] + sz, cy[i] + sz, 0.5f, colors[i]), makeTL(cx[i] - sz, cy[i] + sz, 0.5f, colors[i])};
            drawQuad(v);
        }
        return;
    }

    if (strcmp(name, "depth_test") == 0)
    {
        clearGL(0xFF000000);
        setupFlatDraw();
        InvalidatePipelineCache();
        ApplyBlendMode(BlendMode::Opaque);
        ApplyDepthMode(DepthMode::Normal);

        float w = (float)_w, h = (float)_h;

        {
            TLVertex v[4] = {makeTL(w * 0.1f, h * 0.1f, 0.8f, 0xFF0000FF), makeTL(w * 0.9f, h * 0.1f, 0.8f, 0xFF0000FF),
                             makeTL(w * 0.9f, h * 0.9f, 0.8f, 0xFF0000FF),
                             makeTL(w * 0.1f, h * 0.9f, 0.8f, 0xFF0000FF)};
            drawQuad(v);
        }
        {
            TLVertex v[4] = {makeTL(w * 0.3f, h * 0.3f, 0.2f, 0xFFFF0000), makeTL(w * 0.7f, h * 0.3f, 0.2f, 0xFFFF0000),
                             makeTL(w * 0.7f, h * 0.7f, 0.2f, 0xFFFF0000),
                             makeTL(w * 0.3f, h * 0.7f, 0.2f, 0xFFFF0000)};
            drawQuad(v);
        }
        return;
    }
}

void EngineGL33::Pause() {}
void EngineGL33::Restore() {}

void EngineGL33::PreReset(bool hard)
{
    FreeAllQueues(_queueNo);

    // Detach engine textures from units 0/1 by re-binding the 1x1 white
    // sentinel.  Binding GL name 0 here would leave the units pointing at
    // a texture object with no defined base level, which the driver flags
    // as a KHR_debug LOW warning the moment any draw happens between the
    // soft Reset and the next SetTexture (id 131204).  The sentinel
    // survives a soft reset and is recreated by InitGL on a hard reset
    // (PreReset(true) is followed by DestroySurfaces + InitGL).
    GLuint placeholder = _fallbackWhiteTex ? _fallbackWhiteTex : 0;
    for (int i = 0; i < 2; i++)
    {
        GL33Bind::Tex2D(i, placeholder);
    }
    glFlush();

    if (hard)
    {
        _textBank->ReleaseAllTextures();
        DeinitPixelShaders();
        DeinitVertexShaders();
    }

    DestroyVB();
    DestroyVBTL();
    _lastQueueSource = nullptr;
}

void EngineGL33::PostReset()
{
    CreateVB();
    CreateVBTL();

    Init3DState();
}

bool EngineGL33::Reset()
{
    GL33Bind::Invalidate();
    InvalidatePipelineCache();
    if (!_glContext)
        return false;

    bool inScene = _frameOpen;
    if (inScene)
        FinishDraw();

    PreReset(false);

    // Window size may have changed — rebuild the frame target at the new
    // dimensions before deriving the viewport from it.
    DestroySSAATarget();
    ApplyPendingRenderScale();
    BindFrameRenderTarget();

    int viewportW, viewportH;
    RenderTargetSize(viewportW, viewportH);

    LOG_DEBUG(Graphics, "GL33: Reset logical={}x{} viewport={}x{}", _w, _h, viewportW, viewportH);

    glViewport(0, 0, viewportW, viewportH);

    PostReset();
    if (inScene)
        InitDraw();
    return true;
}

void EngineGL33::ResetForRemount()
{
    GL33Bind::Invalidate();
    InvalidatePipelineCache();
    // A mod re-mount only changes *content*, not GL infrastructure. Flush any
    // queued draws and release the GPU textures tied to the old banks — the new
    // mod set's textures reload on demand. Shaders, vertex buffers and sampler
    // state are engine-level (not mod content) and MUST stay intact: a full
    // ResetHard tears them down and leaves the VS material-constant binding null
    // for the first post-reload draw (UploadVSMaterialConstants then faults).
    FreeAllQueues(_queueNo);
    if (_textBank)
    {
        _textBank->ReleaseAllTextures();
    }
}

bool EngineGL33::ResetHard()
{
    if (!_glContext)
        return false;

    bool inScene = _frameOpen;
    if (inScene)
        FinishDraw();

    PreReset(true);
    DestroySurfaces();

    DoSetGamma();
    InitGL();

    PostReset();
    if (inScene)
        InitDraw();
    return true;
}

bool EngineGL33::SwitchRes(int w, int h, int bpp)
{
    if (_pendingExclusiveEnter && _sdlWindow)
    {
        SDL_DisplayID display = SDL_GetDisplayForWindow(_sdlWindow);
        if (!display)
            display = SDL_GetPrimaryDisplay();

        if (!ApplyExclusiveDisplayMode(_sdlWindow, display, w, h, _refreshRate))
            LOG_WARN(Graphics, "GL33: SDL_SetWindowFullscreenMode failed for {}x{}@{}: {}", w, h, _refreshRate,
                     SDL_GetError());
        _pixelSize = bpp;
        _w = w;
        _h = h;
        SDL_SetWindowFullscreen(_sdlWindow, true);
        LogExclusiveModeStatus(_sdlWindow, w, h, _refreshRate);
        return true;
    }

    if (_windowed && _sdlWindow)
    {
        WindowMetrics metrics;
        SDL_GetWindowSize(_sdlWindow, &metrics.logicalWidth, &metrics.logicalHeight);
        if (WindowedResolutionAlreadyApplied(metrics, w, h) && _pixelSize == bpp)
            return true;
    }
    else if (w == _w && h == _h && _pixelSize == bpp)
    {
        return true;
    }

    _pixelSize = bpp;

    if (_windowed)
    {
        if (_sdlWindow)
        {
            int logicalW = w;
            int logicalH = h;
            SDL_SetWindowSize(_sdlWindow, w, h);
            SDL_GetWindowSize(_sdlWindow, &logicalW, &logicalH);
            SDL_GetWindowSizeInPixels(_sdlWindow, &_w, &_h);
            _windowedRestoreW = logicalW;
            _windowedRestoreH = logicalH;
            LOG_DEBUG(Graphics, "GL33: SwitchRes windowed request={}x{} logical={}x{} drawable={}x{}", w, h, logicalW,
                      logicalH, _w, _h);
        }
        else
        {
            _w = w;
            _h = h;
            _windowedRestoreW = w;
            _windowedRestoreH = h;
        }
        Reset();
    }
    else
    {
        if (_sdlWindow)
        {
            SDL_DisplayID display = SDL_GetDisplayForWindow(_sdlWindow);
            if (!display)
                display = SDL_GetPrimaryDisplay();

            if (!ApplyExclusiveDisplayMode(_sdlWindow, display, w, h, _refreshRate))
            {
                LOG_WARN(Graphics, "GL33: SDL_SetWindowFullscreenMode failed for {}x{}@{}: {}", w, h, _refreshRate,
                         SDL_GetError());
            }
            LogExclusiveModeStatus(_sdlWindow, w, h, _refreshRate);
        }
        _w = w;
        _h = h;
        ResetHard();
    }

    return true;
}

bool EngineGL33::SwitchRefreshRate(int refresh)
{
    if (refresh == 0)
        return false;
    if (_refreshRate == refresh)
        return true;
    _refreshRate = refresh;
    if (_windowed)
        return true;
    ResetHard();
    return true;
}

bool EngineGL33::SetSwapInterval(int interval)
{
    // SDL3 SDL_GL_SetSwapInterval handles all three modes natively:
    // 0 = off, 1 = on, -1 = adaptive (where supported; falls back to 1
    // automatically when adaptive isn't available on the current driver).
    return SDL_GL_SetSwapInterval(interval);
}

int EngineGL33::GetSwapInterval() const
{
    int v = 1;
    SDL_GL_GetSwapInterval(&v);
    return v;
}

bool EngineGL33::SetWindowMode(WindowMode mode)
{
    if (!_sdlWindow)
        return false;
    const WindowMode previousMode = _windowMode;
    LOG_DEBUG(Graphics, "GL33: SetWindowMode requesting {}",
              mode == WindowMode::Fullscreen   ? "fullscreen"
              : mode == WindowMode::Borderless ? "borderless"
                                               : "windowed");
    if (_sdlWindow && _windowed && mode != WindowMode::Windowed)
        SDL_GetWindowSize(_sdlWindow, &_windowedRestoreW, &_windowedRestoreH);
    _windowMode = mode;
    _pendingExclusiveEnter = (mode == WindowMode::Fullscreen && _windowed);

    // Build a synthetic DisplayConfig so we can route through the
    // shared resolver — keeps the "borderless covers monitor" rule in
    // exactly one place (WindowPlacement.cpp) and makes runtime mode
    // toggles match the initial-create behaviour bit-for-bit.
    DisplayPlacementInput synth;
    synth.displayMode = (mode == WindowMode::Fullscreen)   ? "exclusive"
                        : (mode == WindowMode::Borderless) ? "borderless"
                                                           : "windowed";
    synth.width = _w;
    synth.height = _h;
    if (_sdlWindow && mode == WindowMode::Windowed)
    {
        if (previousMode != WindowMode::Windowed)
        {
            synth.width = _windowedRestoreW > 0 ? _windowedRestoreW : synth.width;
            synth.height = _windowedRestoreH > 0 ? _windowedRestoreH : synth.height;
        }
        else if (_windowed)
        {
            WindowMetrics metrics;
            SDL_GetWindowSize(_sdlWindow, &metrics.logicalWidth, &metrics.logicalHeight);
            metrics.drawableWidth = _w;
            metrics.drawableHeight = _h;
            std::tie(synth.width, synth.height) = GetWindowedModeSize(metrics);
        }
        else
        {
            synth.width = _windowedRestoreW > 0 ? _windowedRestoreW : synth.width;
            synth.height = _windowedRestoreH > 0 ? _windowedRestoreH : synth.height;
        }
    }
    synth.refresh = _refreshRate;

    int desktopW = 0, desktopH = 0, desktopRefresh = 0;
    const SDL_DisplayID disp = SDL_GetDisplayForWindow(_sdlWindow);
    if (const SDL_DisplayMode* dm = SDL_GetDesktopDisplayMode(disp ? disp : SDL_GetPrimaryDisplay()))
    {
        desktopW = dm->w;
        desktopH = dm->h;
        desktopRefresh = (int)(dm->refresh_rate + 0.5f);
    }
    const WindowPlacement p = ResolveWindowPlacement(synth, desktopW, desktopH, desktopRefresh);

    // Windowed: leave fullscreen first so size/border/pos edits land on
    // a regular window. Borderless uses SDL's "desktop fullscreen"
    // path (nullptr mode). Exclusive fullscreen installs a concrete
    // display mode first so the monitor can switch away from desktop.
    if (p.mode == WindowMode::Windowed)
    {
        SDL_SetWindowFullscreenMode(_sdlWindow, nullptr);
        SDL_SetWindowFullscreen(_sdlWindow, false);
        SDL_SetWindowBordered(_sdlWindow, true);
        // Restore resizability: the window may have been created without
        // SDL_WINDOW_RESIZABLE (fullscreen/borderless startup) or lost the
        // flag after a fullscreen transition.  SDL_SetWindowBordered does
        // not restore it.
        SDL_SetWindowResizable(_sdlWindow, true);
        SDL_SetWindowSize(_sdlWindow, p.width, p.height);
        // The resolver returns `kCentered` for windowed; translate to
        // SDL_WINDOWPOS_CENTERED so the window actually re-centers.
        // Without this, the window keeps whatever position it had
        // before (the prior borderless geometry at (0,0)), so toggling
        // Borderless -> Windowed shrinks the window in place at the
        // top-left corner instead of returning to the centre.
        const int wantX = (p.posX == WindowPlacement::kCentered) ? SDL_WINDOWPOS_CENTERED : p.posX;
        const int wantY = (p.posY == WindowPlacement::kCentered) ? SDL_WINDOWPOS_CENTERED : p.posY;
        SDL_SetWindowPosition(_sdlWindow, wantX, wantY);
        // SDL's `OnFullscreenChanged` callback updates `_windowed` for
        // transitions that go through the SDL fullscreen state machine,
        // but the Borderless path below no longer enters that state
        // (see SDL #12791 comment).  Set the flag explicitly here so
        // `IsWindowed()` and every downstream check stay coherent
        // regardless of which transition we came from.
        _windowed = true;
    }
    else if (p.mode == WindowMode::Fullscreen)
    {
        if (_pendingExclusiveEnter)
            return true;
        if (!ApplyExclusiveDisplayMode(_sdlWindow, disp ? disp : SDL_GetPrimaryDisplay(), p.width, p.height,
                                       p.refreshHz))
        {
            LOG_WARN(Graphics, "GL33: SDL_SetWindowFullscreenMode failed for {}x{}@{}: {}", p.width, p.height,
                     p.refreshHz, SDL_GetError());
        }
        SDL_SetWindowFullscreen(_sdlWindow, true);
        LogExclusiveModeStatus(_sdlWindow, p.width, p.height, p.refreshHz);
        _windowed = false;
    }
    else
    {
        // Borderless: keep the Windows-only SDL #12791 workaround, but
        // on Linux/macOS use SDL's real desktop-fullscreen state so the
        // compositor treats the window as fullscreen instead of a
        // regular work-area-clamped borderless window.
#ifdef _WIN32
        SDL_SetWindowFullscreenMode(_sdlWindow, nullptr);
        SDL_SetWindowFullscreen(_sdlWindow, false);
        SDL_SetWindowBordered(_sdlWindow, false);
        SDL_SetWindowSize(_sdlWindow, p.width, p.height);
        SDL_SetWindowPosition(_sdlWindow, p.posX, p.posY);
#else
        SDL_SetWindowFullscreenMode(_sdlWindow, nullptr);
        SDL_SetWindowFullscreen(_sdlWindow, true);
#endif
        _windowed = false;
    }
    return true;
}

WindowMode EngineGL33::GetCurrentWindowMode() const
{
    if (!_sdlWindow)
        return WindowMode::Windowed;
    return _windowMode;
}

void EngineGL33::ListMonitors(FindArray<MonitorInfo>& ret)
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

int EngineGL33::GetCurrentMonitor() const
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

bool EngineGL33::SwitchMonitor(int idx)
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

bool EngineGL33::GetDesktopDisplayMode(int& w, int& h, int& refresh) const
{
    if (!_sdlWindow)
        return false;

    SDL_DisplayID display = SDL_GetDisplayForWindow(_sdlWindow);
    if (!display)
        display = SDL_GetPrimaryDisplay();
    return ReadDisplayMode(SDL_GetDesktopDisplayMode(display), w, h, refresh);
}

bool EngineGL33::GetCurrentDisplayMode(int& w, int& h, int& refresh) const
{
    if (!_sdlWindow)
        return false;

    SDL_DisplayID display = SDL_GetDisplayForWindow(_sdlWindow);
    if (!display)
        display = SDL_GetPrimaryDisplay();
    return ReadDisplayMode(SDL_GetCurrentDisplayMode(display), w, h, refresh);
}

bool EngineGL33::GetRequestedFullscreenMode(int& w, int& h, int& refresh) const
{
    if (!_sdlWindow)
        return false;

    return ReadDisplayMode(SDL_GetWindowFullscreenMode(_sdlWindow), w, h, refresh);
}

void EngineGL33::OnFullscreenChanged(bool windowed)
{
    LOG_DEBUG(Graphics, "GL33: OnFullscreenChanged {} (was {})", windowed ? "windowed" : "fullscreen",
              _windowed ? "windowed" : "fullscreen");
    _windowed = windowed;
    if (!_sdlWindow)
        return;

    if (!_windowed)
    {
        _pendingExclusiveEnter = false;
    }
    if (_windowed && _windowMode == WindowMode::Windowed && _windowedRestoreW > 0 && _windowedRestoreH > 0)
    {
        SDL_SetWindowSize(_sdlWindow, _windowedRestoreW, _windowedRestoreH);
    }
}

void EngineGL33::OnWindowResized(int w, int h)
{
    if (w <= 0 || h <= 0)
        return;

    if (!_windowed && _windowMode == WindowMode::Fullscreen && _w > 0 && _h > 0)
    {
        w = _w;
        h = _h;
    }

    LOG_DEBUG(Graphics, "GL33: OnWindowResized {}x{}", w, h);
    _w = w;
    _h = h;
    if (_windowed)
    {
        _windowedRestoreW = w;
        _windowedRestoreH = h;
    }
    Reset();

    // Fire the aspect-policy post-hook so apps re-resolve the
    // aspect rectangle for the new viewport.  Without this the
    // UI rect stays at whatever was computed at boot — pillarboxed
    // even on a 16:9 viewport when boot picked a different default.
    FireResizePostHook(w, h);
}

void EngineGL33::ListResolutions(FindArray<ResolutionInfo>& ret)
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

void EngineGL33::ListRefreshRates(FindArray<int>& ret)
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
        {
            int hz = static_cast<int>(modes[i]->refresh_rate);
            ret.AddUnique(hz);
        }
    }
    SDL_free(modes);
}

void EngineGL33::DestroySurfaces()
{
    DestroySamplerStates();
    DeinitPixelShaders();
    DeinitVertexShaders();
    DestroyVB();
    DestroyVBTL();
}

RString EngineGL33::GetDebugName() const
{
    if (_glContext)
    {
        const char* renderer = (const char*)glGetString(GL_RENDERER);
        if (renderer)
            return RString("OpenGL 3.3 — ") + RString(renderer);
    }
    return "OpenGL 3.3";
}

RString EngineGL33::GetRendererName() const
{
    return "OpenGL 3.3";
}

int EngineGL33::FrameTime() const
{
    return 0;
}

int EngineGL33::AddLight(Light*)
{
    return 0;
}
void EngineGL33::ClearLights() {}

void EngineGL33::InitGL()
{
    LOG_INFO(Graphics, "GL33: InitGL — initializing shaders");
    if (!_glContext)
        return;

    _frameOpen = false;

    // Initialize queue storage
    static StaticStorage<WORD> TriangleQueueStorageNo[MaxTriQueues];
    for (int i = 0; i < MaxTriQueues; i++)
    {
        TriQueue& triqNo = _queueNo._tri[i];
        triqNo._triangleQueue.SetStorage(TriangleQueueStorageNo[i].Init(TriQueueSize));
    }

    // 1x1 opaque-white sentinel for untextured P3D faces — see EngineGL33.hpp
    // for the full rationale.  Created FIRST (before any other GL setup)
    // and pre-bound to unit 0, so any draw triggered before the first
    // SetTexture (early splash/progress paths) samples a defined texture
    // instead of GL name 0.
    glGenTextures(1, &_fallbackWhiteTex);
    GL33Bind::Tex2D(kUploadUnit - GL_TEXTURE0, _fallbackWhiteTex);
    const uint32_t whitePixel = 0xFFFFFFFFu;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &whitePixel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    GL33Bind::Tex2D(0, _fallbackWhiteTex);
    GL33Bind::Tex2D(1, _fallbackWhiteTex);
    GL33Bind::ActiveUnit(0);

    CreateVB();
    CreateVBTL();
    CreateTextBank();
    Init3DState();
}

void EngineGL33::ShutdownGL()
{
    LOG_INFO(Graphics, "GL33: ShutdownGL");
    _fonts.Clear();
    if (_textBank)
    {
        delete _textBank;
        _textBank = nullptr;
    }

    DeinitVertexShaders();
    DeinitPixelShaders();
    DestroySamplerStates();
    DestroyVBTL();
    DestroyVB();

    if (_fallbackWhiteTex)
    {
        GL33Bind::OnTexDeleted(_fallbackWhiteTex);
        glDeleteTextures(1, &_fallbackWhiteTex);
        _fallbackWhiteTex = 0;
    }
}

// TextureDestroyed is mandated by the Engine interface (callers in the
// texture-destruction path notify the engine).  GL33 has no per-texture
// teardown work to do — the GL handle is released by TextureGL33 itself.
void EngineGL33::TextureDestroyed(Texture*) {}

namespace Poseidon
{
Engine* CreateEngineGL33(int w, int h, bool windowed, int bpp)
{
    // Construct first so SDL video / window are initialised, then hide
    // the OS cursor unconditionally — the game draws its own cursor
    // sprite (menu Arrow, in-game crosshair, viewer crosshair).
    // SDL_HideCursor before the window exists is a no-op on some SDL3
    // backends, so the order matters.
    Engine* engine = new EngineGL33(w, h, windowed, bpp);
    SDL_HideCursor();
    return engine;
}
} // namespace Poseidon
