#include <Evaluator/express.hpp>
using namespace Poseidon;
#include <Poseidon/World/World.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/UI/UITestEngine.hpp>
#include <Poseidon/UI/UIActiveDisplay.hpp>
#include <Poseidon/UI/Controls/UIControls.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Graphics/Rendering/Frame/WorldFrameObserver.hpp>
#include <Poseidon/Graphics/Shadow/ShadowMath.hpp>
#include <Poseidon/Graphics/Shared/PNGWriter.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Input/KeyInput.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Audio/IAudioSystem.hpp>
#include <Poseidon/Audio/SoundScene.hpp>
#include <Poseidon/Audio/Voice/VonCapture.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <Poseidon/UI/Settings/GameSettingsConfig.hpp>
#include <Poseidon/Foundation/Platform/AppConfig.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/Graphics/Cursor/ICursorOverlay.hpp>
#include <Poseidon/UI/Map/UIMap.hpp>
#include <Poseidon/Core/resincl.hpp>    // IDC_* used by DisplayUI.hpp (not self-contained)
#include <Poseidon/UI/DisplayUI.hpp>    // DisplayMultiplayer / DisplayMods for the seed verbs
#include <Poseidon/Core/ModSystem.hpp>  // GetModList for triAssertActiveMod
#include <Poseidon/IO/ParamFileExt.hpp> // global Pars for triAssertConfigClass
#include <sstream>
#include <Poseidon/Network/MasterServerServiceClient.hpp> // catalog entry for triSeedWorkshopMods
#include <Poseidon/Graphics/Rendering/Draw/FontMapping.hpp>
#include <Poseidon/Dev/Debug/DebugOverlay.hpp>
#include <Poseidon/Dev/Diag/FrameProfiler.hpp>
namespace Poseidon
{
extern bool gPerfDumpShapesOnce;
}
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

namespace Poseidon
{
} // namespace Poseidon

using namespace Poseidon::Dev;
#include <Poseidon/Dev/Debug/DebugCheats.hpp>
#include <Poseidon/Dev/Debug/DebugCommands.hpp>
#include <Poseidon/UI/Settings/AspectRatio.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Foundation/Memory/CheckMem.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/AI/VehicleAI.hpp>
#include <Poseidon/AI/Path/ArcadeWaypoint.hpp>
#include <Poseidon/World/Entities/Vehicles/Transport.hpp>
#include <Poseidon/Game/Commands/GameStateExtCommon.hpp>
#include <Poseidon/Foundation/Strings/Mbcs.hpp>

// Visibility apply (global free fn, defined in GameStateExtUi.cpp) — externed like
// DebugOverlay does, so the tri visibility verbs re-derive objectsZ / shadowsZ
// after changing a distance, exactly as the dev-panel Game tab sliders do.
extern void SetVisibility(float distance);

// Save-directory helpers from optionsUICommon.hpp — forward-declared so we
// don't drag in the full options UI graph (which transitively requires
// rendering / UI types we don't link from this TU).
// Poseidon::GetTmpSaveDirectory() returns UserDir/Saved/Tmp/ and internally calls
// CreatePath — safe in both client and simulate-server modes.
// GInput is defined in InputSubsystem.cpp; used by triGpad* verbs to
// simulate gamepad input for harness tests.
namespace Poseidon
{
extern Input GInput;
}
// SceneToScreen projects a 3D scene point to screen UV [0,1]; defined in
// UIMapDisplayBriefing.cpp at global scope.  Used by triCursorMoveControl to
// aim the synthetic cursor at a 3D control's true projected quad centre.
extern DrawCoord SceneToScreen(Vector3Par pos);
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>

using Poseidon::Foundation::LoggingSystem;
using Poseidon::Foundation::MemoryUsed;
using Poseidon::Foundation::Time;

extern void SDLInput_BufferControllerUiAction(Poseidon::ControllerUiAction action, bool menuFallback);

/// triGpadButton <index> — synthesize a gamepad button-press event
/// for harness tests. Also injects the matching controller UI action for
/// menu/editor navigation tests without a physical SDL pad.
/// Mapping:
/// 0=A, 1=B, 2=X, 3=Y, 4=LB, 5=RB, 8=Back, 9=Start, 10=LStick, 11=RStick.
GameValue TriGpadButton(const GameState* state, GameValuePar arg)
{
    int i = static_cast<int>(static_cast<GameScalarType>(arg));
    if (i < 0 || i >= N_JOYSTICK_BUTTONS)
    {
        LOG_ERROR(Core, "[tri] triGpadButton: index {} out of range", i);
        return GameValue(false);
    }
    // Write to the synthetic buffer — ProcessJoystick's per-frame clear
    // would otherwise wipe a direct GInput write before the consumer reads it.
    InputSubsystem::Instance().SetSyntheticStickButton(i, true);
    if (i == 0)
        SDLInput_BufferControllerUiAction(GWorld && GWorld->IsEditorControllerUiActive()
                                              ? ControllerUiAction::PrimaryClick
                                              : ControllerUiAction::Confirm,
                                          true); // A -> confirm in menus, primary click in editor
    else if (i == 1)
        SDLInput_BufferControllerUiAction(ControllerUiAction::Cancel, true); // B -> cancel/back in menus
    else if (i == 2)
        SDLInput_BufferControllerUiAction(ControllerUiAction::Preview, false);
    else if (i == 3)
        SDLInput_BufferControllerUiAction(ControllerUiAction::Delete, false);
    else if (i == 4)
        SDLInput_BufferControllerUiAction(ControllerUiAction::PreviousTab, false);
    else if (i == 5)
        SDLInput_BufferControllerUiAction(ControllerUiAction::NextTab, false);
    else if (i == 6)
        SDLInput_BufferControllerUiAction(ControllerUiAction::PagePrevious, true);
    else if (i == 7)
        SDLInput_BufferControllerUiAction(ControllerUiAction::PageNext, true);
    else if (i == 9)
        SDLInput_BufferControllerUiAction(ControllerUiAction::Pause, false);
    return GameValue(true);
}

/// triGpadPov <index> — synthesize a D-pad press.  Index is 8-way
/// (0=N, 2=E, 4=S, 6=W; diagonals 1/3/5/7).  Cardinal directions also
/// inject the matching controller UI action for menu-navigation parity tests.
GameValue TriGpadPov(const GameState* state, GameValuePar arg)
{
    int i = static_cast<int>(static_cast<GameScalarType>(arg));
    if (i < 0 || i >= 8)
    {
        LOG_ERROR(Core, "[tri] triGpadPov: index {} out of range", i);
        return GameValue(false);
    }
    InputSubsystem::Instance().SetSyntheticStickPov(i, true);
    switch (i)
    {
        case 0:
            SDLInput_BufferControllerUiAction(ControllerUiAction::NavigateUp, true);
            break;
        case 2:
            SDLInput_BufferControllerUiAction(ControllerUiAction::NavigateRight, true);
            break;
        case 4:
            SDLInput_BufferControllerUiAction(ControllerUiAction::NavigateDown, true);
            break;
        case 6:
            SDLInput_BufferControllerUiAction(ControllerUiAction::NavigateLeft, true);
            break;
        default:
            break;
    }
    return GameValue(true);
}

/// triGpadLeft [x,y] — synthesize a held left-stick vector for controller UI tests.
GameValue TriGpadLeft(const GameState* state, GameValuePar arg)
{
    const GameArrayType& arr = arg;
    if (arr.Size() < 2)
    {
        LOG_ERROR(Core, "[tri] triGpadLeft: expected [x,y]");
        return GameValue(false);
    }
    const float x = static_cast<float>(static_cast<GameScalarType>(arr[0]));
    const float y = static_cast<float>(static_cast<GameScalarType>(arr[1]));
    InputSubsystem::Instance().SetSyntheticLeftStick(x, y);
    return GameValue(true);
}

/// triKeyUp <scancode> — push an SDL KEY_UP event (release a held key).
GameValue TriKeyUp(const GameState* state, GameValuePar arg)
{
    int sc = static_cast<int>(static_cast<GameScalarType>(arg));
    LOG_INFO(Core, "[tri] triKeyUp scancode=0x{:x}", sc);
    SDL_Event ev = {};
    ev.type = SDL_EVENT_KEY_UP;
    ev.key.scancode = static_cast<SDL_Scancode>(sc);
    ev.key.key = SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(sc), SDL_KMOD_NONE, false);
    ev.key.mod = SDL_KMOD_NONE;
    ev.key.down = false;
    SDL_PushEvent(&ev);
    return GameValue(true);
}

// Shared output dir and sequence counter for test capture commands.
std::string GetTriOutputDir()
{
    const char* envDir = getenv("TRI_OUTPUT_DIR");
    if (envDir && envDir[0] != '\0')
        return envDir;
    return "/tmp/ofpr/tri-screenshots";
}

int& TriSeqCounter()
{
    static int seq = 0;
    return seq;
}

/// triScreenshot "label" — capture screenshot to output dir (PNG + BMP). Returns "OK".
/// Uses no-extension path so ScreenshotWriter produces both formats.
GameValue TriScreenshot(const GameState* state, GameValuePar arg)
{
    GameStringType label = static_cast<GameStringType>(arg);
    const char* labelStr = (const char*)label;

    auto dir = GetTriOutputDir();
    std::filesystem::create_directories(dir);

    int n = TriSeqCounter()++;
    char filename[512];
    snprintf(filename, sizeof(filename), "%s/%03d_%s", dir.c_str(), n, labelStr);
    LOG_INFO(Core, "[tri] triScreenshot: {}", filename);
    if (GEngine)
    {
        GEngine->Screenshot(filename);
        GEngine->FlushPendingScreenshot();
    }

    return GameValue("OK");
}

/// triShadowDepthProbe <res> — render a known cube caster from the sun into a GL
/// depth FBO and cross-check it against the CPU shadow-map oracle (ShadowMath).
/// Dumps the GL depth map to the tri output dir. Returns "OK iou=.. diff=.." or
/// "FAIL ..". Exercises the GL shadow-depth FBO in isolation, off the live path.
GameValue TriShadowDepthProbe(const GameState* /*state*/, GameValuePar arg)
{
    namespace sm = Poseidon::shadow;

    int res = static_cast<int>(static_cast<GameScalarType>(arg));
    if (res < 16)
        res = 256;
    if (res > 2048)
        res = 2048;

    if (!GEngine)
        return GameValue("FAIL:no engine");

    // A box caster floating above the origin (y in [1,5]).
    const sm::Vec3 c[8] = {{-2.0f, 1.0f, -2.0f}, {2.0f, 1.0f, -2.0f}, {2.0f, 1.0f, 2.0f}, {-2.0f, 1.0f, 2.0f},
                           {-2.0f, 5.0f, -2.0f}, {2.0f, 5.0f, -2.0f}, {2.0f, 5.0f, 2.0f}, {-2.0f, 5.0f, 2.0f}};
    const int faces[6][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}};
    std::vector<sm::Tri> tris;
    for (const auto& f : faces)
    {
        tris.push_back({c[f[0]], c[f[1]], c[f[2]]});
        tris.push_back({c[f[0]], c[f[2]], c[f[3]]});
    }

    // Light fit over the box corners (same matrix feeds both GL and the oracle).
    sm::Vec3 sun = sm::Normalize({0.4f, -1.0f, 0.3f});
    sm::Vec3 up{0.0f, 1.0f, 0.0f};
    sm::Mat4 lightView = sm::LightView(sun, up);
    sm::OrthoFit fit = sm::FitOrtho(lightView, c, 8);
    fit = sm::SnapToTexelGrid(fit, res);
    sm::Mat4 lightVP = sm::Mul(fit.proj, lightView);

    std::vector<float> verts;
    verts.reserve(tris.size() * 9);
    for (const auto& t : tris)
    {
        const sm::Vec3 vv[3] = {t.a, t.b, t.c};
        for (const auto& v : vv)
        {
            verts.push_back(v.x);
            verts.push_back(v.y);
            verts.push_back(v.z);
        }
    }
    int vertCount = static_cast<int>(verts.size() / 3);

    std::vector<float> glDepth(static_cast<size_t>(res) * res, 1.0f);
    if (!GEngine->ShadowDepthProbe(lightVP.m.data(), verts.data(), vertCount, res, glDepth.data()))
        return GameValue("FAIL:ShadowDepthProbe unsupported");

    sm::DepthMap cpu = sm::CpuRasterDepth(tris.data(), static_cast<int>(tris.size()), lightVP, res);

    // Silhouette IoU + mean depth diff on the overlap.
    long inter = 0, uni = 0, both = 0;
    double sumDiff = 0.0;
    for (size_t i = 0; i < glDepth.size(); ++i)
    {
        bool og = glDepth[i] < 0.999f;
        bool oc = cpu.depth[i] < 0.999f;
        if (og || oc)
            uni++;
        if (og && oc)
        {
            inter++;
            both++;
            sumDiff += std::fabs(glDepth[i] - cpu.depth[i]);
        }
    }
    double iou = uni > 0 ? static_cast<double>(inter) / static_cast<double>(uni) : 0.0;
    double meanDiff = both > 0 ? sumDiff / static_cast<double>(both) : 1.0;
    double occ = static_cast<double>(uni) / static_cast<double>(glDepth.size());

    // Dump the GL depth map for eyeballing (top-down: readback is bottom-origin).
    auto dir = GetTriOutputDir();
    std::filesystem::create_directories(dir);
    std::vector<uint8_t> gray(static_cast<size_t>(res) * res);
    for (int y = 0; y < res; ++y)
    {
        const float* srcRow = glDepth.data() + static_cast<size_t>(res - 1 - y) * res;
        uint8_t* dstRow = gray.data() + static_cast<size_t>(y) * res;
        for (int x = 0; x < res; ++x)
        {
            float d = srcRow[x];
            dstRow[x] =
                (d >= 0.999f) ? static_cast<uint8_t>(35) : static_cast<uint8_t>((0.15f + (1.0f - d) * 0.85f) * 255.0f);
        }
    }
    std::string png = dir + "/shadow_depth_probe.png";
    ::PNGWriter::WritePNG(png.c_str(), res, res, 1, gray.data());

    bool ok = (iou > 0.90) && (meanDiff < 0.02) && (inter > 0);
    char buf[256];
    snprintf(buf, sizeof(buf), "%s:iou=%.3f diff=%.4f occ=%.3f res=%d", ok ? "OK" : "FAIL", iou, meanDiff, occ, res);
    LOG_INFO(Core, "[tri] triShadowDepthProbe: {} -> {}", buf, png.c_str());
    return GameValue(buf);
}

/// triSetAlphaToCoverage <0|1> — toggle MSAA alpha-to-coverage on cutout draws
/// (fence wire, foliage sparkle). On by default via GraphicsConfig; tests
/// toggle it to capture broken-vs-fixed pairs. Returns "OK".
GameValue TriSetAlphaToCoverage(const GameState* /*state*/, GameValuePar arg)
{
    const bool enable = static_cast<float>(arg) != 0.0f;
    if (GEngine)
        GEngine->SetAlphaToCoverage(enable);
    LOG_INFO(Core, "[tri] triSetAlphaToCoverage {}", enable ? 1 : 0);
    return GameValue("OK");
}

/// triSetFlatShading <0|1> — replace all object shading with solid red, keeping
/// the alpha-test silhouette + cutout holes.  Diagnostic for highlight bugs: a
/// bright pixel that vanishes under flat colour is a shading/texture artifact;
/// one that persists (background through a sub-pixel crack) is a geometry /
/// vertex-position artifact.  Returns "OK".
GameValue TriSetFlatShading(const GameState* /*state*/, GameValuePar arg)
{
    const bool enable = static_cast<float>(arg) != 0.0f;
    if (GEngine)
        GEngine->SetDebugFlatColor(enable);
    LOG_INFO(Core, "[tri] triSetFlatShading {}", enable ? 1 : 0);
    return GameValue("OK");
}

/// triPerfDumpShapes — log the shape histogram of the next Pass1 (top 20
/// repeated shapes = instancing candidates).
GameValue TriPerfDumpShapes(const GameState* /*state*/, GameValuePar /*arg*/)
{
    Poseidon::gPerfDumpShapesOnce = true;
    return GameValue("OK");
}

/// triSetVsync <0|1> — swap interval, for benchmarks (vsync off frees the
/// swap phase from the refresh-rate wait). Session-only.
GameValue TriSetVsync(const GameState* /*state*/, GameValuePar arg)
{
    const int v = toInt(static_cast<float>(arg));
    if (GEngine)
        GEngine->SetSwapInterval(v);
    LOG_INFO(Core, "[tri] triSetVsync {}", v);
    return GameValue("OK");
}
/// triPerfStats — frame-phase profiler rolling stats as a machine-readable
/// string: "fps=F frame=avg/p95/max setup=avg/p95 draw=... calls=N frames=N".
GameValue TriPerfStats(const GameState* /*state*/, GameValuePar /*arg*/)
{
    Dev::FrameProfiler& perf = Dev::GFrameProfiler();
    if (perf.FrameCount() == 0)
        return GameValue("FAIL:no_frames");
    const auto total = perf.TotalStats();
    const auto draw = perf.DrawStats();
    char buf[768];
    int off = snprintf(buf, sizeof(buf), "fps=%.1f frame=%.2f/%.2f/%.2f setup=%.2f/%.2f draw=%.2f/%.2f", perf.AvgFps(),
                       total.avgMs, total.p95Ms, total.maxMs, perf.Stats(Dev::FrameProfiler::PhaseSetup).avgMs,
                       perf.Stats(Dev::FrameProfiler::PhaseSetup).p95Ms, draw.avgMs, draw.p95Ms);
    for (int p = Dev::FrameProfiler::PhaseHud; p < Dev::FrameProfiler::PhaseCount; p++)
    {
        const auto s = perf.Stats(static_cast<Dev::FrameProfiler::Phase>(p));
        off +=
            snprintf(buf + off, sizeof(buf) - off, " %s=%.2f/%.2f", Dev::FrameProfiler::PhaseName(p), s.avgMs, s.p95Ms);
    }
    off += snprintf(buf + off, sizeof(buf) - off, " calls=%.0f frames=%d", perf.AvgDrawCalls(), perf.FrameCount());
    for (int p = Dev::FrameProfiler::PhaseDrawFirst; p <= Dev::FrameProfiler::PhaseDrawLast; p++)
    {
        const auto s = perf.Stats(static_cast<Dev::FrameProfiler::Phase>(p));
        off +=
            snprintf(buf + off, sizeof(buf) - off, " %s=%.2f/%.2f", Dev::FrameProfiler::PhaseName(p), s.avgMs, s.p95Ms);
    }
    LOG_INFO(Core, "[tri] triPerfStats {}", buf);
    return GameValue(buf);
}

/// triPerfReset — clear the frame-profiler ring (start a fresh measurement window).
GameValue TriPerfReset(const GameState* /*state*/, GameValuePar /*arg*/)
{
    Dev::GFrameProfiler().Reset();
    return GameValue("OK");
}
/// triSetMsaa <0|2|4|8> — MSAA sample count on the frame target (0 = off).
/// Applied at the next frame boundary. Returns "OK".
GameValue TriSetMsaa(const GameState* /*state*/, GameValuePar arg)
{
    const int samples = toInt(static_cast<float>(arg));
    if (GEngine)
        GEngine->SetMsaaSamples(samples);
    LOG_INFO(Core, "[tri] triSetMsaa {}", samples);
    return GameValue("OK");
}

/// triSetRenderScale <1.0..2.0> — SSAA render scale (1 = off). Applied at the
/// next frame boundary. Returns "OK".
GameValue TriSetRenderScale(const GameState* /*state*/, GameValuePar arg)
{
    const float scale = static_cast<float>(arg);
    if (GEngine)
        GEngine->SetRenderScale(scale);
    LOG_INFO(Core, "[tri] triSetRenderScale {}", scale);
    return GameValue("OK");
}

/// triEnableShadowMaps — turn on the durable shadow-map (depth-buffer) path. Off
/// by default; tests / the player enable it so the new shadows supersede the
/// projected ones. Returns "OK".
GameValue TriEnableShadowMaps(const GameState* /*state*/)
{
    if (GEngine)
        GEngine->SetShadowMapsEnabled(true);
    LOG_INFO(Core, "[tri] triEnableShadowMaps");
    return GameValue("OK");
}

/// triShadowSceneDump "path" — write the current frame's shadow depth map (the
/// live scene rendered from the sun) to a PNG for eyeballing. "OK:path"/"FAIL:..".
GameValue TriShadowSceneDump(const GameState* /*state*/, GameValuePar arg)
{
    GameStringType label = static_cast<GameStringType>(arg);
    const char* lbl = (const char*)label;
    if (!GEngine || !lbl || !lbl[0])
        return GameValue("FAIL:bad args");
    auto dir = GetTriOutputDir();
    std::filesystem::create_directories(dir);
    std::string path = dir + "/" + lbl + ".png";
    if (!GEngine->DumpShadowMap(path.c_str()))
        return GameValue("FAIL:no shadow map (enabled? casters in view?)");
    LOG_INFO(Core, "[tri] triShadowSceneDump: {}", path.c_str());
    return GameValue((std::string("OK:") + path).c_str());
}
