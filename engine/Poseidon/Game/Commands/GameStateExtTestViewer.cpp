#include <Evaluator/express.hpp>
using namespace Poseidon;
#include <Poseidon/World/World.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/UI/UITestEngine.hpp>
#include <Poseidon/UI/UIActiveDisplay.hpp>
#include <Poseidon/UI/Controls/UIControls.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/IO/ParamFileExt.hpp>                         // GetFontID (triAssertTextFits)
#include <Poseidon/Asset/Formats/Common/CsvReader.hpp>          // CsvReadRow (triAssertCsvTextFits)
#include <Poseidon/UI/Locale/Stringtable/CodepageTranscode.hpp> // text transcoding helpers
#include <Poseidon/IO/Streams/QBStream.hpp>                     // QIFStreamB (triAssertCsvTextFits)
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
#include <Poseidon/Core/resincl.hpp>                      // IDC_* used by DisplayUI.hpp (not self-contained)
#include <Poseidon/UI/DisplayUI.hpp>                      // DisplayMultiplayer / DisplayMods for the seed verbs
#include <Poseidon/Core/ModSystem.hpp>                    // GetModList for triAssertActiveMod
#include <Poseidon/Network/MasterServerServiceClient.hpp> // catalog entry for triSeedWorkshopMods
#include <Poseidon/Graphics/Rendering/Draw/FontMapping.hpp>
#include <Poseidon/Dev/Debug/DebugOverlay.hpp>
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

// Viewer / cursor / object / camera / pixel Trident test commands, split out of
// GameStateExtTest.cpp to keep that TU under the size threshold. These Tri* commands
// are registered in GameStateExtTestAudio.cpp and use only self-contained state.

/// triMouseLeft <down> — set the synthetic left-mouse-button state
/// (0=up, 1=down).  In --window mode (where the harness runs) the
/// SDL pipeline's ProcessMouse_SDL → MouseState::Update path doesn't
/// execute, so a direct write to GInput.mouse.left persists across
/// frames.  ControlsContainer::OnSimulate's mouse poll reads that
/// flag directly via InputSubsystem::IsMouseLeftDown.  Pair with
/// triCursorMove to stage a real-style click + drag.  Returns "OK".
GameValue TriMouseLeft(const GameState* /*state*/, GameValuePar arg)
{
    int down = static_cast<int>(static_cast<GameScalarType>(arg));
    // TestSetButton — writes both buttons[0] and the convenience .left
    // flag so the state survives the next per-frame MouseState::Update.
    GInput.mouse.TestSetButton(0, down != 0);
    LOG_INFO(Core, "[tri] triMouseLeft down={}", down);
    return GameValue("OK");
}

/// triMouseRight / triMouseMid <0|1> — synthesise right / middle mouse
/// button state.  Mirrors triMouseLeft.  Pair with triMouseDelta to
/// drive viewer-mode RMB-rotate / MMB-zoom in tri tests.
GameValue TriMouseRight(const GameState* /*state*/, GameValuePar arg)
{
    int down = static_cast<int>(static_cast<GameScalarType>(arg));
    GInput.mouse.TestSetButton(1, down != 0);
    LOG_INFO(Core, "[tri] triMouseRight down={}", down);
    return GameValue("OK");
}

GameValue TriMouseMid(const GameState* /*state*/, GameValuePar arg)
{
    int down = static_cast<int>(static_cast<GameScalarType>(arg));
    GInput.mouse.TestSetButton(2, down != 0);
    LOG_INFO(Core, "[tri] triMouseMid down={}", down);
    return GameValue("OK");
}

/// triMouseDelta [dx, dy] — inject a one-frame mouse delta.  Mouse
/// drag in production comes from SDL relative-motion events; tests
/// don't have an SDL window pumping events, so they write the deltas
/// directly into GInput.mouse.deltaX / deltaY.  Cleared by the input
/// pump on the next frame, so this gives exactly one frame of drag.
GameValue TriMouseDelta(const GameState* /*state*/, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected_array");
    const GameArrayType& a = arg;
    if (a.Size() < 2)
        return GameValue("FAIL:need_dx_dy");
    float dx = (float)(GameScalarType)a[0];
    float dy = (float)(GameScalarType)a[1];
    // Inject into the buffer so the next per-frame Update() picks it
    // up — writing deltaX/deltaY directly would be clobbered by the
    // pump's reset (deltaX = bufDeltaX_ * sensitivity).  Sensitivity
    // is divided out so dx=200 yields ~200 px of effective drag,
    // matching what production code sees from a real SDL motion event.
    constexpr float kSens = 1.5f; // matches the default MouseTuning::baseScale
    GInput.mouse.TestInjectMotion(dx / kSens, dy / kSens);
    LOG_INFO(Core, "[tri] triMouseDelta dx={} dy={}", dx, dy);
    return GameValue("OK");
}

/// triCursorScroll <delta> — push a synthetic mouse-wheel delta into the
/// input subsystem.  Positive delta = wheel up (scroll content upward to
/// reveal earlier items), negative = wheel down (scroll downward to
/// reveal later items), matching SDL's convention.  The value is added
/// to GInput.cursor.aimDeltaZ which whatever UI path needs the wheel
/// will consume via InputSubsystem::ConsumeCursorScroll().  Returns "OK".
GameValue TriCursorScroll(const GameState* /*state*/, GameValuePar arg)
{
    float dz = static_cast<float>(static_cast<GameScalarType>(arg));
    GInput.cursor.aimDeltaZ += dz;
    LOG_INFO(Core, "[tri] triCursorScroll dz={} (aimDeltaZ now {})", dz, GInput.cursor.aimDeltaZ);
    return GameValue("OK");
}

/// triViewerObjectPos / triViewerCamPos — read live position of the active
/// ObjectViewer / CameraViewer as "x,y,z".  Returns "FAIL:not_viewer_mode"
/// outside viewer mode, "FAIL:no_target" if the viewer object isn't bound.
/// Used by viewer-control tri tests to assert that LMB drag / wheel zoom
/// actually moved the right entity.
GameValue TriObjectPos(const GameState* /*state*/)
{
    if (!AppConfig::Instance().IsViewerMode())
        return GameValue("FAIL:not_viewer_mode");
    if (!GWorld)
        return GameValue("FAIL:no_world");
    // GetViewerObject unwraps CameraViewer → its ObjectViewer target,
    // so this reads the *model* the viewer is showing rather than the
    // camera that wraps it.
    Object* tgt = GWorld->GetViewerObject();
    if (!tgt)
        return GameValue("FAIL:no_target");
    Vector3 p = tgt->Position();
    char buf[96];
    snprintf(buf, sizeof(buf), "%.4f,%.4f,%.4f", (float)p.X(), (float)p.Y(), (float)p.Z());
    return GameValue(buf);
}

GameValue TriCamPos(const GameState* /*state*/)
{
    if (!AppConfig::Instance().IsViewerMode())
        return GameValue("FAIL:not_viewer_mode");
    if (!GScene)
        return GameValue("FAIL:no_scene");
    Vector3 p = GScene->GetCamera()->Position();
    char buf[96];
    snprintf(buf, sizeof(buf), "%.4f,%.4f,%.4f", (float)p.X(), (float)p.Y(), (float)p.Z());
    return GameValue(buf);
}

// Latched-cam-pos slot for triViewerLatchCam / triAssertCamMoved.  Keeps
// the test SQF a flat sequence (no variables / conditionals) which is
// the convention this project's other tri tests follow.
namespace
{
bool g_camLatched = false;
Vector3 g_camLatchedPos;
} // namespace

/// triCursorLocked — report state of the active cursor overlay's
/// lock toggle.  Returns "locked", "unlocked", or "none" when no
/// cursor overlay is installed (e.g. dedicated server).
GameValue TriCursorLocked(const GameState* /*state*/)
{
    if (!GWorld)
        return GameValue("none");
    const ICursorOverlay* cur = GWorld->GetCursorOverlay();
    if (!cur)
        return GameValue("none");
    return GameValue(cur->IsLocked() ? "locked" : "unlocked");
}

/// triViewerAnimPhase — current animation phase of the active viewer
/// object as a scalar in [0, 1).  Outside viewer mode returns -1.
GameValue TriAnimPhase(const GameState* /*state*/)
{
    if (!AppConfig::Instance().IsViewerMode() || !GWorld)
        return GameValue(-1.0f);
    return GameValue(GWorld->GetViewerPhase());
}

// Latched-object-pos slot for triViewerLatchObject / triAssertObjectMoved.
// Independent from the camera latch above — both can be live at once
// (e.g. RMB rotates the object, MMB zooms the camera; one test asserts
// both moved).
namespace
{
bool g_objLatched = false;
Vector3 g_objLatchedPos;
Matrix3 g_objLatchedOrient;
} // namespace

GameValue TriLatchObject(const GameState* /*state*/)
{
    if (!AppConfig::Instance().IsViewerMode() || !GWorld)
        return GameValue("FAIL:not_viewer_mode");
    // GetViewerObject unwraps CameraViewer → its ObjectViewer target,
    // so this reads the *model* the viewer is showing rather than the
    // camera that wraps it.
    Object* tgt = GWorld->GetViewerObject();
    if (!tgt)
        return GameValue("FAIL:no_target");
    g_objLatchedPos = tgt->Position();
    g_objLatchedOrient = tgt->Orientation();
    g_objLatched = true;
    char buf[96];
    snprintf(buf, sizeof(buf), "%.4f,%.4f,%.4f", (float)g_objLatchedPos.X(), (float)g_objLatchedPos.Y(),
             (float)g_objLatchedPos.Z());
    return GameValue(buf);
}

/// triAssertObjectMoved <minDistance> — assert the active viewer object
/// has moved at least minDistance world-units since triViewerLatchObject.
GameValue TriAssertObjectMoved(const GameState* /*state*/, GameValuePar arg)
{
    if (!AppConfig::Instance().IsViewerMode() || !GWorld)
        return GameValue("FAIL:not_viewer_mode");
    // GetViewerObject unwraps CameraViewer → its ObjectViewer target,
    // so this reads the *model* the viewer is showing rather than the
    // camera that wraps it.
    Object* tgt = GWorld->GetViewerObject();
    if (!tgt)
        return GameValue("FAIL:no_target");
    if (!g_objLatched)
        return GameValue("FAIL:no_latch");
    float minDist = static_cast<float>(static_cast<GameScalarType>(arg));
    Vector3 d = tgt->Position() - g_objLatchedPos;
    float dist = d.Size();
    if (dist >= minDist)
    {
        LOG_INFO(Core, "[tri] triAssertObjectMoved OK dist={:.4f} min={:.4f}", dist, minDist);
        return GameValue("OK");
    }
    char buf[160];
    snprintf(buf, sizeof(buf), "FAIL:not_moved dist=%.4f min=%.4f", dist, minDist);
    LOG_ERROR(Core, "[tri] triAssertObjectMoved {}", buf);
    return GameValue(buf);
}

/// triAssertObjectRotated <minRadians> — assert orientation differs from
/// the latched value by at least minRadians on Direction().  Catches
/// "rotation input never reached the orientation matrix" regressions.
GameValue TriAssertObjectRotated(const GameState* /*state*/, GameValuePar arg)
{
    if (!AppConfig::Instance().IsViewerMode() || !GWorld)
        return GameValue("FAIL:not_viewer_mode");
    // GetViewerObject unwraps CameraViewer → its ObjectViewer target,
    // so this reads the *model* the viewer is showing rather than the
    // camera that wraps it.
    Object* tgt = GWorld->GetViewerObject();
    if (!tgt)
        return GameValue("FAIL:no_target");
    if (!g_objLatched)
        return GameValue("FAIL:no_latch");
    float minRad = static_cast<float>(static_cast<GameScalarType>(arg));
    Vector3 dirOld = g_objLatchedOrient.Direction();
    Vector3 dirNew = tgt->Orientation().Direction();
    // Angle between Direction() vectors via dot.  Both unit-length so
    // dot is the cosine; clamp for numerical safety.
    float dot = dirOld.X() * dirNew.X() + dirOld.Y() * dirNew.Y() + dirOld.Z() * dirNew.Z();
    if (dot > 1.0f)
        dot = 1.0f;
    if (dot < -1.0f)
        dot = -1.0f;
    float angle = acosf(dot);
    if (angle >= minRad)
    {
        LOG_INFO(Core, "[tri] triAssertObjectRotated OK angle={:.4f} min={:.4f}", angle, minRad);
        return GameValue("OK");
    }
    char buf[160];
    snprintf(buf, sizeof(buf), "FAIL:not_rotated angle=%.4f min=%.4f", angle, minRad);
    LOG_ERROR(Core, "[tri] triAssertObjectRotated {}", buf);
    return GameValue(buf);
}

GameValue TriLatchCam(const GameState* /*state*/)
{
    if (!AppConfig::Instance().IsViewerMode())
        return GameValue("FAIL:not_viewer_mode");
    if (!GScene)
        return GameValue("FAIL:no_scene");
    g_camLatchedPos = GScene->GetCamera()->Position();
    g_camLatched = true;
    char buf[96];
    snprintf(buf, sizeof(buf), "%.4f,%.4f,%.4f", (float)g_camLatchedPos.X(), (float)g_camLatchedPos.Y(),
             (float)g_camLatchedPos.Z());
    return GameValue(buf);
}

/// triAssertCamMoved <minDistance> — assert camera position differs from
/// the latched value by at least minDistance world-units.  Catches the
/// regression where wheel scroll / drag input never reaches the camera.
GameValue TriAssertCamMoved(const GameState* /*state*/, GameValuePar arg)
{
    if (!AppConfig::Instance().IsViewerMode())
        return GameValue("FAIL:not_viewer_mode");
    if (!GScene)
        return GameValue("FAIL:no_scene");
    if (!g_camLatched)
        return GameValue("FAIL:no_latch");
    float minDist = static_cast<float>(static_cast<GameScalarType>(arg));
    Vector3 now = GScene->GetCamera()->Position();
    Vector3 d = now - g_camLatchedPos;
    float dist = d.Size();
    if (dist >= minDist)
    {
        LOG_INFO(Core, "[tri] triAssertCamMoved OK dist={:.4f} min={:.4f}", dist, minDist);
        return GameValue("OK");
    }
    char buf[160];
    snprintf(buf, sizeof(buf), "FAIL:not_moved dist=%.4f min=%.4f latched=(%.3f,%.3f,%.3f) now=(%.3f,%.3f,%.3f)", dist,
             minDist, (float)g_camLatchedPos.X(), (float)g_camLatchedPos.Y(), (float)g_camLatchedPos.Z(),
             (float)now.X(), (float)now.Y(), (float)now.Z());
    LOG_ERROR(Core, "[tri] triAssertCamMoved {}", buf);
    return GameValue(buf);
}

/// triEnableShadows — force GScene->SetObjectShadows(true) +
/// SetVehicleShadows(true) at runtime.  The default Scene state has
/// _objectShadows=false; only ApplyGraphicsConfigToEngine flips it to
/// true and that's gated on the user visiting the Options UI.  Tests
/// run in an ephemeral user dir that never sees the Options page, so
/// shadow draws never fire — this verb makes a regression test that
/// pins shadow pixel values actually testable.
GameValue TriEnableShadows(const GameState* /*state*/)
{
    if (!GScene)
        return GameValue("FAIL:no_scene");
    GScene->SetObjectShadows(true);
    GScene->SetVehicleShadows(true);
    LOG_INFO(Core, "[tri] triEnableShadows OK (object+vehicle shadows on)");
    return GameValue("OK");
}

/// triAssertRegionLit [u0, v0, u1, v1, minBrightness, minLitPixels] — count the
/// pixels in the normalized rectangle whose brightest channel is >= minBrightness
/// and assert at least minLitPixels qualify.  Robust where exact glyph columns
/// shift across platforms (font metrics differ Win/Linux): a rendered line of
/// text lights hundreds of pixels across its band, while a missing line leaves
/// the band on the dark backdrop.  Returns "OK" or "FAIL:lit=N,need=M,min=B".
GameValue TriAssertRegionLit(const GameState* /*state*/, GameValuePar arg)
{
    if (!GEngine)
        return GameValue("FAIL:no_engine");
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected_array");
    const GameArrayType& a = arg;
    if (a.Size() < 6)
        return GameValue("FAIL:need_u0_v0_u1_v1_min_count");
    float u0 = (float)(GameScalarType)a[0];
    float v0 = (float)(GameScalarType)a[1];
    float u1 = (float)(GameScalarType)a[2];
    float v1 = (float)(GameScalarType)a[3];
    int minBright = (int)(GameScalarType)a[4];
    int minCount = (int)(GameScalarType)a[5];

    int w = GEngine->Width();
    int h = GEngine->Height();
    int x0 = (int)(u0 * (w - 1)), x1 = (int)(u1 * (w - 1));
    int y0 = (int)(v0 * (h - 1)), y1 = (int)(v1 * (h - 1));
    if (x1 < x0)
    {
        int t = x0;
        x0 = x1;
        x1 = t;
    }
    if (y1 < y0)
    {
        int t = y0;
        y0 = y1;
        y1 = t;
    }

    int lit = 0;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
        {
            uint8_t rgb[3] = {0, 0, 0};
            if (!GEngine->SamplePixel(x, y, rgb))
                return GameValue("FAIL:not_supported");
            int mc = (int)rgb[0];
            if ((int)rgb[1] > mc)
                mc = (int)rgb[1];
            if ((int)rgb[2] > mc)
                mc = (int)rgb[2];
            if (mc >= minBright)
                ++lit;
        }
    if (lit >= minCount)
    {
        LOG_INFO(Core, "[tri] triAssertRegionLit OK rect=({},{})-({},{}) lit={} need={}", u0, v0, u1, v1, lit,
                 minCount);
        return GameValue("OK");
    }
    char buf[96];
    snprintf(buf, sizeof(buf), "FAIL:lit=%d,need=%d,min=%d", lit, minCount, minBright);
    LOG_ERROR(Core, "[tri] triAssertRegionLit rect=({},{})-({},{}) {}", u0, v0, u1, v1, buf);
    return GameValue(buf);
}

/// triAssertPixelLit [u, v, minBrightness] — "OK" if the sampled pixel's
/// brightest channel is at least minBrightness.
GameValue TriAssertPixelLit(const GameState* /*state*/, GameValuePar arg)
{
    if (!GEngine)
        return GameValue("FAIL:no_engine");
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected_array");
    const GameArrayType& a = arg;
    if (a.Size() < 3)
        return GameValue("FAIL:need_u_v_min");

    const float u = static_cast<float>(static_cast<GameScalarType>(a[0]));
    const float v = static_cast<float>(static_cast<GameScalarType>(a[1]));
    const int minBrightness = static_cast<int>(static_cast<GameScalarType>(a[2]));
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
        return GameValue("FAIL:out_of_range");

    const int w = GEngine->Width();
    const int h = GEngine->Height();
    if (w <= 0 || h <= 0)
        return GameValue("FAIL:no_surface");

    uint8_t rgb[3] = {0, 0, 0};
    const int px = static_cast<int>(u * static_cast<float>(w - 1));
    const int py = static_cast<int>(v * static_cast<float>(h - 1));
    if (!GEngine->SamplePixel(px, py, rgb))
        return GameValue("FAIL:not_supported");

    int maxChannel = rgb[0];
    if (rgb[1] > maxChannel)
        maxChannel = rgb[1];
    if (rgb[2] > maxChannel)
        maxChannel = rgb[2];
    if (maxChannel >= minBrightness)
        return GameValue("OK");

    char buf[96];
    snprintf(buf, sizeof(buf), "FAIL:actual=%d,%d,%d max=%d min=%d", rgb[0], rgb[1], rgb[2], maxChannel, minBrightness);
    LOG_ERROR(Core, "[tri] triAssertPixelLit {}", buf);
    return GameValue(buf);
}

/// triAssertTextFits [text, sizeEx, font, maxWidth] — "OK" if the rendered width of `text` in
/// `font` at `sizeEx` is <= `maxWidth` (screen-relative, 0..1), else "FAIL:width=..". An automated
/// guard that a string fits its box / the screen instead of clipping — uses the same GetTextWidth
/// the engine uses to lay text out, so it tracks the live font metrics.
GameValue TriAssertTextFits(const GameState* /*state*/, GameValuePar arg)
{
    if (!GEngine)
        return GameValue("FAIL:no_engine");
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected_array");
    const GameArrayType& a = arg;
    if (a.Size() < 4)
        return GameValue("FAIL:need_text_sizeEx_font_maxWidth");
    RString text = (GameStringType)a[0];
    float sizeEx = (float)(GameScalarType)a[1];
    RString fontName = (GameStringType)a[2];
    float maxW = (float)(GameScalarType)a[3];
    Font* font = GEngine->LoadFont(GetFontID(fontName));
    if (!font)
        return GameValue("FAIL:font_not_loaded");
    float w = GEngine->GetTextWidth(sizeEx, font, text);
    if (w <= maxW)
    {
        LOG_INFO(Core, "[tri] triAssertTextFits OK width={} max={} font={}", w, maxW, (const char*)fontName);
        return GameValue("OK");
    }
    char buf[96];
    snprintf(buf, sizeof(buf), "FAIL:width=%.4f>max=%.4f", w, maxW);
    return GameValue(buf);
}

static bool CaseEq(const char* a, const char* b)
{
#ifdef _WIN32
    return _stricmp(a, b) == 0;
#else
    return strcasecmp(a, b) == 0;
#endif
}

/// triAssertCsvTextFits [csvPath, language, sizeEx, font, maxWidth] — scan a stringtable CSV and
/// assert EVERY line of EVERY entry (in the given language column) fits within maxWidth (screen-
/// relative) at font/size. Multi-line entries (names stacked with the literal "\n" marker, as in
/// the credits) are split first, so each rendered line is checked individually. Comprehensive,
/// deterministic guard that credit/subtitle text is fully rendered and not cropped.
GameValue TriAssertCsvTextFits(const GameState* /*state*/, GameValuePar arg)
{
    if (!GEngine)
        return GameValue("FAIL:no_engine");
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected_array");
    const GameArrayType& a = arg;
    if (a.Size() < 5)
        return GameValue("FAIL:need_csvPath_language_sizeEx_font_maxWidth");
    RString csvPath = (GameStringType)a[0];
    RString language = (GameStringType)a[1];
    float sizeEx = (float)(GameScalarType)a[2];
    RString fontName = (GameStringType)a[3];
    float maxW = (float)(GameScalarType)a[4];

    if (!QIFStreamB::FileExist(csvPath))
        return GameValue(RString("FAIL:csv_not_found:") + csvPath);
    Font* font = GEngine->LoadFont(GetFontID(fontName));
    if (!font)
        return GameValue("FAIL:font_not_loaded");

    const int plen = csvPath.GetLength();
    const bool isUtf8 = plen >= 9 && CaseEq(((const char*)csvPath) + plen - 9, ".utf8.csv");

    QIFStreamB f;
    f.AutoOpen(csvPath);

    int column = -1;
    Poseidon::Codepage cp = Poseidon::Codepage::CP1252;
    while (!f.eof())
    {
        std::vector<std::string> row;
        if (!Poseidon::Asset::Formats::CsvReadRow(f, row))
            return GameValue("FAIL:csv_parse");
        if (!row.empty() && CaseEq(row[0].c_str(), "LANGUAGE"))
        {
            for (int c = 1; c < (int)row.size(); c++)
                if (CaseEq(row[c].c_str(), (const char*)language))
                {
                    column = c - 1;
                    cp = isUtf8 ? Poseidon::Codepage::Utf8 : Poseidon::CodepageForLanguage(row[c].c_str());
                    break;
                }
            break;
        }
    }
    if (column < 0)
        return GameValue(RString("FAIL:language_column_not_found:") + language);

    int checked = 0;
    while (!f.eof())
    {
        std::vector<std::string> row;
        if (!Poseidon::Asset::Formats::CsvReadRow(f, row))
            break;
        if (row.empty() || column + 1 >= (int)row.size())
            continue;
        const std::string value = Poseidon::DecodeLegacyTextToUtf8(row[column + 1], cp);
        size_t start = 0;
        while (start <= value.size())
        {
            const size_t nl = value.find("\\n", start);
            const std::string line = value.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
            if (!line.empty())
            {
                const float w = GEngine->GetTextWidth(sizeEx, font, line.c_str());
                checked++;
                if (w > maxW)
                {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "FAIL:%.40s line='%.48s' width=%.4f>max=%.4f", row[0].c_str(),
                             line.c_str(), w, maxW);
                    return GameValue(buf);
                }
            }
            if (nl == std::string::npos)
                break;
            start = nl + 2;
        }
    }
    LOG_INFO(Core, "[tri] triAssertCsvTextFits OK csv={} lang={} lines={} maxW={}", (const char*)csvPath,
             (const char*)language, checked, maxW);
    if (checked == 0)
        return GameValue("FAIL:no_lines_checked");
    return GameValue("OK");
}

GameValue TriAssertPixelDarkerThan(const GameState* /*state*/, GameValuePar arg)
{
    if (!GEngine)
        return GameValue("FAIL:no_engine");
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected_array");
    const GameArrayType& a = arg;
    if (a.Size() < 5)
        return GameValue("FAIL:need_us_vs_ul_vl_min");
    float us = (float)(GameScalarType)a[0];
    float vs = (float)(GameScalarType)a[1];
    float ul = (float)(GameScalarType)a[2];
    float vl = (float)(GameScalarType)a[3];
    int minDelta = (int)(GameScalarType)a[4];
    int w = GEngine->Width();
    int h = GEngine->Height();
    uint8_t ps[3] = {0, 0, 0}, pl[3] = {0, 0, 0};
    if (!GEngine->SamplePixel((int)(us * (w - 1)), (int)(vs * (h - 1)), ps))
        return GameValue("FAIL:not_supported");
    if (!GEngine->SamplePixel((int)(ul * (w - 1)), (int)(vl * (h - 1)), pl))
        return GameValue("FAIL:not_supported");
    int maxShadow = ps[0];
    if (ps[1] > maxShadow)
        maxShadow = ps[1];
    if (ps[2] > maxShadow)
        maxShadow = ps[2];
    int maxLit = pl[0];
    if (pl[1] > maxLit)
        maxLit = pl[1];
    if (pl[2] > maxLit)
        maxLit = pl[2];
    int delta = maxLit - maxShadow;
    if (delta >= minDelta)
        return GameValue("OK");
    char buf[128];
    snprintf(buf, sizeof(buf), "FAIL:shadow=%d,%d,%d lit=%d,%d,%d delta=%d min=%d", ps[0], ps[1], ps[2], pl[0], pl[1],
             pl[2], delta, minDelta);
    LOG_ERROR(Core, "[tri] triAssertPixelDarkerThan {}", buf);
    return GameValue(buf);
}

GameValue TriAssertPixelEquals(const GameState* /*state*/, GameValuePar arg)
{
    if (!GEngine)
        return GameValue("FAIL:no_engine");
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected_array");
    const GameArrayType& a = arg;
    if (a.Size() < 6)
        return GameValue("FAIL:need_u_v_r_g_b_tol");

    const float u = static_cast<float>(static_cast<GameScalarType>(a[0]));
    const float v = static_cast<float>(static_cast<GameScalarType>(a[1]));
    const int expected[3] = {static_cast<int>(static_cast<GameScalarType>(a[2])),
                             static_cast<int>(static_cast<GameScalarType>(a[3])),
                             static_cast<int>(static_cast<GameScalarType>(a[4]))};
    const int tolerance = static_cast<int>(static_cast<GameScalarType>(a[5]));
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
        return GameValue("FAIL:out_of_range");

    const int w = GEngine->Width();
    const int h = GEngine->Height();
    if (w <= 0 || h <= 0)
        return GameValue("FAIL:no_surface");

    uint8_t rgb[3] = {0, 0, 0};
    const int px = static_cast<int>(u * static_cast<float>(w - 1));
    const int py = static_cast<int>(v * static_cast<float>(h - 1));
    if (!GEngine->SamplePixel(px, py, rgb))
        return GameValue("FAIL:not_supported");

    int maxDelta = 0;
    for (int i = 0; i < 3; ++i)
    {
        int delta = static_cast<int>(rgb[i]) - expected[i];
        if (delta < 0)
            delta = -delta;
        if (delta > maxDelta)
            maxDelta = delta;
    }
    if (maxDelta <= tolerance)
        return GameValue("OK");

    char buf[160];
    snprintf(buf, sizeof(buf), "FAIL:actual=%d,%d,%d expected=%d,%d,%d max_delta=%d tol=%d", rgb[0], rgb[1], rgb[2],
             expected[0], expected[1], expected[2], maxDelta, tolerance);
    LOG_ERROR(Core, "[tri] triAssertPixelEquals {}", buf);
    return GameValue(buf);
}
