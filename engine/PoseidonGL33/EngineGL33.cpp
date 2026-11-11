#include <PoseidonGL33/EngineGL33.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Graphics/Shared/WindowPlacement.hpp>

#include <SDL3/SDL.h>
#include <glad/gl.h>
#include <Poseidon/Dev/Debug/DebugOverlay.hpp>

using namespace Poseidon::Dev;

#include <mutex>
#include <unordered_map>


namespace
{
// Lifetime-of-process counter of HIGH-severity GL errors (driver
// validation failures).  the frame layer's ValidateFrame consumes this via the
// Engine::GetDebugErrorCount() virtual to enforce I-20 (no
// GL_INVALID_* during a frame).  Static-not-atomic — the callback
// fires on the GL thread only.
unsigned int s_glHighSeverityErrorCount = 0;

// Most recent HIGH-severity GL_DEBUG message.  Captured by the
// callback below and surfaced via `EngineGL33::GetLastDebugMessage`
// so the frame layer's I-20 violation detail says *what* the error was, not just
// that one fired.  Static-not-atomic for the same single-thread
// reason as the counter.
std::string s_glLastHighSeverityMessage;
} // namespace

unsigned int EngineGL33::GetDebugErrorCount() const
{
    return s_glHighSeverityErrorCount;
}

std::string EngineGL33::GetLastDebugMessage() const
{
    return s_glLastHighSeverityMessage;
}

namespace
{

// Per-error GL debug callback (KHR_debug / GL 4.3+ — universally
// available on modern desktop GL).  Wired in all builds: NDEBUG
// (Release) covers production, but the project's day-to-day build is
// RelWithDebInfo where NDEBUG is also defined; gating on _DEBUG would
// hide the callback from dev builds where it's most valuable.  Synchronous
// callback overhead is negligible vs the diagnostic value of seeing the
// exact call site that produced each GL error.  Severity maps to log
// level so the runtime channel filter decides what reaches the console.
//
// Non-HIGH messages are deduplicated per `id`: every unique id logs
// once at full severity, repeats only bump a counter.  GL drivers
// emit the same MEDIUM/LOW notification thousands of times per frame
// (NVIDIA's "buffer detailed info" stream alone hit ~1.9 M lines in a
// 2-min capture), which otherwise drowns out the PERF / Audio stream
// we run alongside it.  HIGH-severity errors are always logged in
// full — the frame layer's I-20 gate counts them and the message text is needed
// for the violation report.
std::unordered_map<GLuint, std::uint64_t> s_glDedupCounts;
std::mutex s_glDedupMtx;

void GLAPIENTRY GlDebugCallback(GLenum /*source*/, GLenum type, GLuint id, GLenum severity, GLsizei /*length*/,
                                const GLchar* message, const void* /*userParam*/)
{
    // Suppress NVIDIA buffer-detail messages (id 131185 = "Buffer object
    // <X> will use VIDEO memory as the source for buffer object operations")
    // — chatty, not actionable.
    if (id == 131185)
    {
        return;
    }

    const char* sev = severity == GL_DEBUG_SEVERITY_HIGH     ? "HIGH"
                      : severity == GL_DEBUG_SEVERITY_MEDIUM ? "MEDIUM"
                      : severity == GL_DEBUG_SEVERITY_LOW    ? "LOW"
                                                             : "INFO";

    if (severity == GL_DEBUG_SEVERITY_HIGH)
    {
        ++s_glHighSeverityErrorCount;
        s_glLastHighSeverityMessage =
            "type=0x" + std::to_string(type) + " id=" + std::to_string(id) + ": " + (message ? message : "(null)");
        LOG_ERROR(Graphics, "GL[{} type=0x{:04X} id={}]: {}", sev, type, id, message);
        return;
    }

    // Non-HIGH: dedup by id.  First sighting logs at full severity;
    // repeats bump a counter that EngineGL33's dtor flushes as a
    // summary line ("GL[MEDIUM id=N]: <msg> (suppressed K repeats)").
    bool firstSighting = false;
    {
        std::lock_guard<std::mutex> lock(s_glDedupMtx);
        auto& count = s_glDedupCounts[id];
        firstSighting = (count == 0);
        ++count;
    }
    if (!firstSighting)
    {
        return;
    }

    if (severity == GL_DEBUG_SEVERITY_MEDIUM)
    {
        LOG_WARN(Graphics, "GL[{} type=0x{:04X} id={}]: {}", sev, type, id, message);
    }
    else if (severity == GL_DEBUG_SEVERITY_LOW)
    {
        LOG_INFO(Graphics, "GL[{} type=0x{:04X} id={}]: {}", sev, type, id, message);
    }
    else
    {
        LOG_DEBUG(Graphics, "GL[{} type=0x{:04X} id={}]: {}", sev, type, id, message);
    }

    // Fail-fast in debug builds when a real GL API error fires.
    // GL_DEBUG_TYPE_ERROR at HIGH severity = the driver caught a misuse
    // (invalid enum, unbound program at draw, etc.).  Synchronous
    // callback is already enabled, so the stack trace points at the
    // exact GL call.  This catches state-machine bugs at the source
    // instead of letting them produce silent rendering corruption.
    //
    // Release builds (incl. day-to-day RelWithDebInfo) log only — we
    // want diagnostic visibility there, not crashes.
#ifdef _DEBUG
    if (severity == GL_DEBUG_SEVERITY_HIGH && type == GL_DEBUG_TYPE_ERROR)
    {
#ifdef _MSC_VER
        __debugbreak();
#else
        __builtin_trap();
#endif
    }
#endif
}
} // namespace

QueueGL33::QueueGL33()
{
    for (int i = 0; i < MaxTriQueues; i++)
    {
        _triUsed[i] = false;
    }
    _usedCounter = 0;
    _vertexBufferUsed = 0;
    _indexBufferUsed = 0;
    _meshBase = 0;
    _meshSize = 0;
    _actTri = -1;
    _firstVertex = true;
    _firstIndex = true;
}

int QueueGL33::Allocate(TextureGL33* tex, int level, int spec, int minI, int maxI, int tip)
{
    int index = -1;
    if (tip >= minI && tip < maxI && _triUsed[tip])
    {
        TriQueue& triq = _tri[tip];
        if (tex == triq._texture && spec == triq._special)
            index = tip;
    }

    int free = -1;
    if (index < 0)
    {
        for (int i = minI; i < maxI; i++)
        {
            if (_triUsed[i])
            {
                TriQueue& triq = _tri[i];
                if (tex != triq._texture || spec != triq._special)
                    continue;
                index = i;
            }
            else if (free < 0)
            {
                free = i;
            }
        }
    }
    _usedCounter++;
    if (index >= 0)
    {
        TriQueue& triq = _tri[index];
        saturateMin(triq._level, level);
        triq._lastUsed = _usedCounter;
        return index;
    }
    if (free >= 0)
    {
        TriQueue& triq = _tri[free];
        triq._special = spec;
        triq._texture = tex;
        triq._level = level;
        triq._passId = SpecToPassId(spec);
        triq._lastUsed = _usedCounter;
        PoseidonAssert(triq._triangleQueue.Size() == 0);
        triq._triangleQueue.Resize(0);
        _triUsed[free] = true;
    }
    return free;
}

void QueueGL33::Free(int i)
{
    PoseidonAssert(_tri[i]._triangleQueue.Size() == 0);
    PoseidonAssert(_triUsed[i]);
    _triUsed[i] = false;
}

EngineGL33::EngineGL33(int width, int height, bool windowed, int bpp)
{
    _w = width;
    _h = height;
    _windowed = windowed;
    _windowedRestoreW = width;
    _windowedRestoreH = height;
    _pixelSize = bpp;
    _depthBpp = 24;
    _refreshRate = 60;
    _bias = 0;
    _gamma = 1.0f;
    _frameOpen = false;
    _nightEye = 0;

    _grassParam[0] = 0;
    _grassParam[1] = 0;
    _grassParam[2] = 0;
    _grassParam[3] = 0;

    _clipANearEnabled = false;
    _clipAFarEnabled = false;

    _minGuardX = 0;
    _maxGuardX = _w;
    _minGuardY = 0;
    _maxGuardY = _h;

    _prepSpec = -1;
    _stencilExclusionEnabled = false;
    _texGenMode = TGFixed;
    _iOffset = 0;
    _lastQueueSource = nullptr;

    _lastClampU = false;
    _lastClampV = false;
    _pointSampling = false;
    _enableReorder = false;

    _texLoc = TexLocalVidMem;

    _pixelShaderSel = PSNone;
    _pixelShaderModeSel = PSMDay;
    _pixelShaderSpecularSel = PSSNormal;
    memset(_shaderProgram, 0, sizeof(_shaderProgram));

    _vertexShaderSel = VSNone;
    _formatSet = SingleTex;

    _textColor = Color(1, 1, 1, 1);

    LOG_INFO(Graphics, "GL33: Initializing engine — bootstrap {}x{} {}bpp {} before display.cfg/user overrides", _w,
             _h, _pixelSize, _windowed ? "windowed" : "fullscreen");

    // SDL3 initialization
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        LOG_ERROR(Graphics, "GL33: SDL_Init failed: {}", SDL_GetError());
        return;
    }

    // Request OpenGL 3.3 Core Profile
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    // Forward-compatible flag is required by Apple's GL implementation
    // for any 3.3 Core context; harmless on Win/Linux.  Debug flag opens
    // GL_CONTEXT_FLAG_DEBUG_BIT so KHR_debug callbacks can fire — kept
    // on in all builds because the day-to-day RelWithDebInfo build is
    // what we develop against, and gating on _DEBUG would leave us without
    // the per-error GL log there.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG | SDL_GL_CONTEXT_DEBUG_FLAG);

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    // The default framebuffer is single-sampled on purpose: every frame
    // renders into the offscreen frame target (BindFrameRenderTarget), which
    // carries the MSAA samples — 8x at render scale 1, 4x at SSAA scales —
    // and resolves into the window with a blit.  A multisampled window
    // framebuffer would make the scaled-resolve blit illegal
    // (GL_INVALID_OPERATION: scaling blits need single-sampled targets) and
    // glReadPixels on it is out-of-spec anyway.

    // Resolve final placement (mode + size + position) from the engine
    // config, the --window override, and the target display's desktop
    // mode.  Borderless is forced to native desktop resolution at (0,0)
    // so DWM (Win10 1903+) detects it and engages independent flip —
    // without this, "borderless" produces a chromeless window the size
    // of the configured w/h, defeating the whole point of the mode.
    auto& engineCfg = GApp->GetConfig().GetEngineConfig();
    DisplayPlacementInput displayCfg;
    displayCfg.displayMode = engineCfg.displayMode;
    // --window forces Windowed regardless of saved displayMode (dev /
    // testing ergonomics).  appConfig already collapses --window into
    // displayMode = "windowed" during arg parsing, but be defensive.
    if (windowed && displayCfg.displayMode != "windowed")
        displayCfg.displayMode = "windowed";
    if (!windowed && displayCfg.displayMode == "windowed")
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

    // On Windows, Borderless avoids SDL's fullscreen state machine
    // because Win11 + OpenGL can promote that path to exclusive on the
    // first SwapWindow (libsdl-org/SDL#12791).  On Linux/macOS we *do*
    // want the compositor's real desktop-fullscreen state so shell
    // work-area reservations (GNOME top bar, etc.) don't treat the game
    // as a regular borderless window.
    //
    // Windowed mode keeps the standard resizable bordered window.
    // SDL_WINDOW_HIGH_PIXEL_DENSITY: opt into native-pixel rendering on
    // HighDPI displays (Retina, Windows 200% scaling).  Without this
    // flag SDL renders at *logical* pixels and blits up — content looks
    // blurry.  The SDL_GetWindowSizeInPixels readback below already
    // handles the size correctly; this flag makes the readback
    // actually return higher-than-logical numbers when the display has
    // a pixel density > 1.
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY;
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

    _sdlWindow = SDL_CreateWindow("Poseidon [GL33]", placement.width, placement.height, flags);
    if (!_sdlWindow)
    {
        LOG_ERROR(Graphics, "GL33: SDL_CreateWindow failed: {}", SDL_GetError());
        return;
    }

    if (placement.mode == WindowMode::Borderless)
    {
#ifndef _WIN32
        SDL_SetWindowFullscreenMode(_sdlWindow, nullptr);
        if (!SDL_SetWindowFullscreen(_sdlWindow, true))
            LOG_WARN(Graphics, "GL33: SDL_SetWindowFullscreen(true) failed for borderless startup: {}", SDL_GetError());
#else
        if (placement.posX != WindowPlacement::kCentered)
            SDL_SetWindowPosition(_sdlWindow, placement.posX, placement.posY);
#endif
    }
    else if (placement.posX != WindowPlacement::kCentered)
    {
        SDL_SetWindowPosition(_sdlWindow, placement.posX, placement.posY);
    }

    _w = placement.width;
    _h = placement.height;
    if (placement.refreshHz > 0)
        _refreshRate = placement.refreshHz;

    // Create OpenGL context
    _glContext = SDL_GL_CreateContext(_sdlWindow);
    if (!_glContext)
    {
        LOG_ERROR(Graphics, "GL33: SDL_GL_CreateContext failed: {}", SDL_GetError());
        return;
    }

    // Load OpenGL function pointers via GLAD
    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress))
    {
        LOG_ERROR(Graphics, "GL33: gladLoadGL failed");
        _glContext = nullptr;
        return;
    }

    // VSync ON by default.  The options menu's GraphicsConfig::vsync
    // setting can override this at runtime via SetSwapInterval /
    // GetSwapInterval.
    SDL_GL_SetSwapInterval(1);

    // Initialize the ImGui debug overlay (font tuner + future panels).  Hidden
    // by default — F8 toggles.  Must be called after GL context exists.
    DebugOverlay::Init(_sdlWindow, _glContext);

    // MSAA lives on the offscreen frame target; _msaaActive (the
    // alpha-to-coverage gate) is set when that target is created with
    // multisample storage — see ApplyPendingRenderScale.

    // KHR_debug callback: turn raw GL errors into actionable per-call log
    // lines instead of "stale GL error 0x0502" sweeps a function later.
    // Synchronous output ensures the callback fires inside the call site
    // that produced the error (LearnOpenGL recommendation).
    {
        GLint ctxFlags = 0;
        glGetIntegerv(GL_CONTEXT_FLAGS, &ctxFlags);
        if ((ctxFlags & GL_CONTEXT_FLAG_DEBUG_BIT) && glDebugMessageCallback)
        {
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(GlDebugCallback, nullptr);
            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
            LOG_INFO(Graphics, "GL33: KHR_debug callback wired (synchronous)");
        }
        else
        {
            LOG_INFO(Graphics, "GL33: KHR_debug unavailable — debug context: {}, callback fn: {}",
                     (ctxFlags & GL_CONTEXT_FLAG_DEBUG_BIT) ? "yes" : "no",
                     glDebugMessageCallback ? "available" : "missing");
        }
    }

    // Query actual pixel dimensions
    int cw = 0, ch = 0;
    SDL_GetWindowSizeInPixels(_sdlWindow, &cw, &ch);
    _w = cw;
    _h = ch;

    // Vendor + renderer make hybrid-GPU support reports self-diagnosing — they
    // show which GPU Windows/Optimus actually handed the process.
    LOG_INFO(Graphics, "GL33: OpenGL {} — {} — {}", (const char*)glGetString(GL_VERSION),
             (const char*)glGetString(GL_VENDOR), (const char*)glGetString(GL_RENDERER));
    LOG_INFO(Graphics, "GL33: surface resolved to {}x{} {}", _w, _h, _windowed ? "windowed" : "fullscreen");

    // Hook SDL events to the engine.
    _eventWindow.Attach(_sdlWindow, _w, _h);

    // Initialize shaders, vertex buffers, and 3D state.  InitGL also
    // sets the GL viewport; the Splash / progress UI handles showing
    // something during startup.
    LoadConfig();
    InitGL();
}

EngineGL33::ShutdownGuard::~ShutdownGuard()
{
    // I-05 / B-019: clear the base class's FontCache while
    // `engine->_textBank` is still alive.  This destructor runs
    // FIRST in the EngineGL33 teardown chain (last-declared member,
    // first-destroyed), so `_textBank` and every other EngineGL33
    // member still have valid storage at this point.  By the time
    // the base `~Engine()` destroys `_fonts`, it is empty and the
    // dangling-Ref<Texture> path of B-019 cannot fire.
    if (engine)
        engine->ClearFontCache();
}

EngineGL33::~EngineGL33()
{
    LOG_INFO(Graphics, "GL33: Destroying engine");

    SaveConfig();

    DebugOverlay::Shutdown();

    FreeTerrainInstanced();

    ShutdownGL();

    _eventWindow.Detach();

    if (_glContext)
    {
        SDL_GL_DestroyContext((SDL_GLContext)_glContext);
        _glContext = nullptr;
    }

    if (_sdlWindow)
    {
        SDL_DestroyWindow(_sdlWindow);
        _sdlWindow = nullptr;
    }
}
