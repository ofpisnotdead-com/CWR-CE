#include <Evaluator/express.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/Graphics/Shared/RenderDocCapture.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/UI/UITestEngine.hpp>
#include <Poseidon/UI/Controls/UIControls.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Graphics/Rendering/Shape/Shape.hpp> // Shapes (triLoadedShapeCount)
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Network/NetworkCustomAssets.hpp>
#include <Poseidon/Network/NetworkImpl.hpp>
#include <Poseidon/Core/PlayerMuteIgnore.hpp>
#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/Input/KeyInput.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Audio/IAudioSystem.hpp>
#include <Poseidon/Audio/SoundScene.hpp>
#include <Poseidon/Audio/Voice/VonCapture.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <Poseidon/UI/Settings/GameSettingsConfig.hpp>
#include <Poseidon/Foundation/Platform/AppConfig.hpp>
#include <Poseidon/IO/Streams/QStream.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/Graphics/Cursor/ICursorOverlay.hpp>
#include <Poseidon/UI/Map/UIMap.hpp>
#include <Poseidon/Graphics/Rendering/Draw/FontMapping.hpp>
#include <Poseidon/Dev/Debug/DebugOverlay.hpp>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Strings/StrFormat.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

using namespace Poseidon;
namespace Poseidon
{
} // namespace Poseidon

using namespace Poseidon::Dev;
#include <Poseidon/Dev/Debug/DebugCheats.hpp>
#include <Poseidon/Dev/Debug/DebugCommands.hpp>
#include <Poseidon/UI/Settings/AspectRatio.hpp>
#include <Poseidon/UI/Settings/PresentationRects.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Profile/ProfileManager.hpp>
#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <Poseidon/Foundation/Common/PlayerPrefs.hpp>
#include <Poseidon/Foundation/Memory/CheckMem.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/AI/VehicleAI.hpp>
#include <Poseidon/AI/Path/ArcadeWaypoint.hpp>
#include <Poseidon/World/Entities/Vehicles/Transport.hpp>
#include <Poseidon/AI/EntityAI.hpp>
#include <Poseidon/AI/AIUnit.hpp>
#include <Poseidon/Game/Commands/GameStateExtCommon.hpp>
#include <Poseidon/Foundation/Strings/Mbcs.hpp>

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
#include <SDL3/SDL.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>

using Poseidon::Foundation::LoggingSystem;
using Poseidon::Foundation::MemoryUsed;
using Poseidon::Foundation::Time;

GameValue TriVersion(const GameState*);
GameValue TriGameMode(const GameState*);
GameValue TriDisplay(const GameState*);
GameValue TriEditorMode(const GameState*);
GameValue TriRemount(const GameState*);
GameValue TriLoadedShapeCount(const GameState*);
GameValue TriClick(const GameState*, GameValuePar);
GameValue TriClickAt(const GameState*, GameValuePar);
GameValue TriInvokeButton(const GameState*, GameValuePar);
GameValue TriSeedSessions(const GameState*, GameValuePar);
GameValue TriSeedMods(const GameState*, GameValuePar);
GameValue TriSeedWorkshopMods(const GameState*, GameValuePar);
GameValue TriSortMods(const GameState*, GameValuePar);
GameValue TriModsVisibleCount(const GameState*);
GameValue TriModsSetFilter(const GameState*, GameValuePar);
GameValue TriModsRowClick(const GameState*, GameValuePar);
GameValue TriOpenModDownload(const GameState*, GameValuePar);
GameValue TriOpenJoinRequirements(const GameState*, GameValuePar);
GameValue TriAssertControlLineStarts(const GameState*, GameValuePar);
GameValue TriAssertActiveIDD(const GameState*, GameValuePar);
GameValue TriAssertActiveMod(const GameState*, GameValuePar);
GameValue TriAssertConfigClass(const GameState*, GameValuePar);
GameValue TriClickText(const GameState*, GameValuePar);
GameValue TriMouseDown(const GameState*, GameValuePar);
GameValue TriMouseDownArr(const GameState*, GameValuePar);
GameValue TriMouseDragV(const GameState*, GameValuePar);
GameValue TriMouseUp(const GameState*);
GameValue TriDblClick(const GameState*, GameValuePar);
GameValue TriSelectList(const GameState*, GameValuePar);
GameValue TriSelectListByData(const GameState*, GameValuePar);
GameValue TriListSel(const GameState*, GameValuePar);
GameValue TriAssertListSelAtLeast(const GameState*, GameValuePar);
GameValue TriSendKey(const GameState*, GameValuePar);
GameValue TriSendKeyArr(const GameState*, GameValuePar);
GameValue TriSendText(const GameState*, GameValuePar);
GameValue TriTypeText(const GameState*, GameValuePar);
GameValue TriSendTextArr(const GameState*, GameValuePar);
GameValue TriKeyDown(const GameState*, GameValuePar);
GameValue TriKeyRepeat(const GameState*, GameValuePar);
GameValue TriGpadButton(const GameState*, GameValuePar);
GameValue TriGpadPov(const GameState*, GameValuePar);
GameValue TriGpadLeft(const GameState*, GameValuePar);
GameValue TriKeyUp(const GameState*, GameValuePar);
GameValue TriScreenshot(const GameState*, GameValuePar);
GameValue TriShadowDepthProbe(const GameState*, GameValuePar);
GameValue TriEnableShadowMaps(const GameState*);
GameValue TriShadowSceneDump(const GameState*, GameValuePar);
GameValue TriSetAlphaToCoverage(const GameState*, GameValuePar);
GameValue TriSetFlatShading(const GameState*, GameValuePar);
GameValue TriSetRenderScale(const GameState*, GameValuePar);
GameValue TriSetMsaa(const GameState*, GameValuePar);
GameValue TriPerfStats(const GameState*, GameValuePar);
GameValue TriSetVsync(const GameState*, GameValuePar);
GameValue TriPerfDumpShapes(const GameState*, GameValuePar);
GameValue TriPerfReset(const GameState*, GameValuePar);
GameValue TriShadowSetDarkness(const GameState*, GameValuePar);
GameValue TriShadowSetBias(const GameState*, GameValuePar);
GameValue TriShadowSetCascades(const GameState*, GameValuePar);
GameValue TriShadowSetDistance(const GameState*, GameValuePar);
GameValue TriShadowSetSplit(const GameState*, GameValuePar);
GameValue TriShadowSetOmni(const GameState*, GameValuePar);
GameValue TriShadowSetOmniR0(const GameState*, GameValuePar);
GameValue TriShadowSetOmniR1(const GameState*, GameValuePar);
GameValue TriShadowSetFade(const GameState*, GameValuePar);
GameValue TriShadowSetRes(const GameState*, GameValuePar);
GameValue TriShadowTuning(const GameState*);
GameValue TriSetViewDistance(const GameState*, GameValuePar);
GameValue TriAssertVisibility(const GameState*, GameValuePar);
GameValue TriDevPanelSelectShadows(const GameState*);
GameValue TriDevPanelSelectMemory(const GameState*);
GameValue TriShadowSunFactor(const GameState*);
GameValue TriShadowProxyVerts(const GameState*);
GameValue TriStartRandomCutscene(const GameState*);
GameValue TriHornPlayerVehicle(const GameState*);
GameValue TriControls(const GameState*);
GameValue TriAssertDisplay(const GameState*, GameValuePar);
GameValue TriGetEndMode(const GameState*);
GameValue TriAssertCommandMenuOpen(const GameState*);
GameValue TriAssertCommandMenuClosed(const GameState*);
GameValue TriActionMenuText(const GameState*);
GameValue TriCommandMenuText(const GameState*);
GameValue TriInGameplay(const GameState*);
GameValue TriOpenMap(const GameState*);
GameValue TriShowMap(const GameState*, GameValuePar);
GameValue TriMapSetScale(const GameState*, GameValuePar);
GameValue TriShowVoiceOverlay(const GameState*, GameValuePar);
GameValue TriClickBriefingLink(const GameState*, GameValuePar);
GameValue TriProbeClickBriefingLink(const GameState*, GameValuePar);
GameValue TriBriefingSection(const GameState*);
GameValue TriBriefingSwitch(const GameState*, GameValuePar);
GameValue TriBriefingLinkRoute(const GameState*, GameValuePar);
GameValue TriBriefingClickAt(const GameState*, GameValuePar);
GameValue TriMissionPlayerReady(const GameState*);
GameValue TriControlText(const GameState*, GameValuePar);
GameValue TriAssertControlLeftOf(const GameState*, GameValuePar);
GameValue TriVisibleTexts(const GameState*);
GameValue TriActiveMods(const GameState*);
GameValue TriAssertTreeText(const GameState*, GameValuePar);
GameValue TriAssertListText(const GameState*, GameValuePar);
GameValue TriVoiceLanguage(const GameState*);
GameValue TriSetVoiceLanguage(const GameState*, GameValuePar);
GameValue TriSetLanguage(const GameState*, GameValuePar);
GameValue TriEndTest(const GameState*);
GameValue TriFrameCount(const GameState*);
GameValue TriErrorCount(const GameState*);
GameValue TriResetErrorCount(const GameState*);
GameValue TriWaitFrames(const GameState*, GameValuePar);
GameValue TriSimFrames(const GameState*, GameValuePar);
GameValue TriSetSimTime(const GameState*, GameValuePar);
GameValue TriSetBrightness(const GameState*, GameValuePar);
GameValue TriSceneReady(const GameState*);
GameValue TriNetCommand(const GameState*, GameValuePar);
GameValue TriDrawColorbar(const GameState*);
GameValue TriAssertColorbarBytes(const GameState*);
GameValue TriDrawGradient3d(const GameState*);
GameValue TriDrawClearBlue(const GameState*);
GameValue TriDrawClearMagenta(const GameState*);
GameValue TriDrawQuad2d(const GameState*);
GameValue TriDrawLines2d(const GameState*);
GameValue TriDrawAlphaBlend(const GameState*);
GameValue TriDrawDepthTest(const GameState*);
GameValue TriSendAltEnter(const GameState*);
GameValue TriIsWindowed(const GameState*);
GameValue TriGetWindowSize(const GameState*);
GameValue TriAssertWindowedSmallerThanDesktop(const GameState*);
GameValue TriGetFrameShape(const GameState*);
GameValue TriAssertFrameShape(const GameState*, GameValuePar);
GameValue TriResetGLErrorBaseline(const GameState*);
GameValue TriGetGLErrorCount(const GameState*);
GameValue TriSamplePixel(const GameState*, GameValuePar);
GameValue TriCursorMove(const GameState*, GameValuePar);
GameValue TriCursorMoveControl(const GameState*, GameValuePar);
GameValue TriCursorPos(const GameState*);
GameValue TriCursorDrawRect(const GameState*);
GameValue TriAssertCursorDrawRect(const GameState*, GameValuePar);
GameValue TriOpenDisabledChildDisplay(const GameState*, GameValuePar);
GameValue TriMouseLeft(const GameState*, GameValuePar);
GameValue TriMouseRight(const GameState*, GameValuePar);
GameValue TriMouseMid(const GameState*, GameValuePar);
GameValue TriMouseDelta(const GameState*, GameValuePar);
GameValue TriCursorScroll(const GameState*, GameValuePar);
GameValue TriObjectPos(const GameState*);
GameValue TriCamPos(const GameState*);
GameValue TriCursorLocked(const GameState*);
GameValue TriAnimPhase(const GameState*);
GameValue TriLatchObject(const GameState*);
GameValue TriAssertObjectMoved(const GameState*, GameValuePar);
GameValue TriAssertObjectRotated(const GameState*, GameValuePar);
GameValue TriLatchCam(const GameState*);
GameValue TriAssertCamMoved(const GameState*, GameValuePar);
GameValue TriEnableShadows(const GameState*);
GameValue TriAssertRegionLit(const GameState*, GameValuePar);
GameValue TriAssertPixelLit(const GameState*, GameValuePar);
GameValue TriAssertTextFits(const GameState*, GameValuePar);
GameValue TriAssertCsvTextFits(const GameState*, GameValuePar);
GameValue TriAssertPixelEquals(const GameState*, GameValuePar);
GameValue TriAssertPixelDarkerThan(const GameState*, GameValuePar);

// Linker hooks — force GameStateExtTestGeneric.cpp and GameStateExtTestGetters.cpp
// into the link graph via PoseidonGame, where no game code references their
// TriAssert*/TriGet* families directly.  Their own INIT_MODULEs register the verbs.
void EnsureGameStateExtTestGenericLinked();
void EnsureGameStateExtTestGettersLinked();

// Audio test surface — direct GSoundsys read/write so tests can verify
// AudioPage's UI plumbing without scraping rendered text.

namespace
{
int VolumeRowFromName(const char* name)
{
    // 0 = Music (CD), 1 = Effects (Wave), 2 = Speech.  Anything else
    // returns -1 so the caller can FAIL with a recognisable code.
    if (!name)
        return -1;
    if (!strcmp(name, "music") || !strcmp(name, "cd"))
        return 0;
    if (!strcmp(name, "effects") || !strcmp(name, "wave"))
        return 1;
    if (!strcmp(name, "speech") || !strcmp(name, "voices"))
        return 2;
    return -1;
}
} // namespace

/// triGetVolume "music"|"effects"|"speech" -> "0".."100" (UI scale).
GameValue TriGetVolume(const GameState* /*state*/, GameValuePar arg)
{
    if (!GSoundsys)
        return GameValue("FAIL:no_sound");
    int row = VolumeRowFromName(((RString)(GameStringType)arg).Data());
    if (row < 0)
        return GameValue("FAIL:bad_row");
    float v = (row == 0)   ? GSoundsys->GetCDVolume()
              : (row == 1) ? GSoundsys->GetWaveVolume()
                           : GSoundsys->GetSpeechVolume();
    int pct = (int)(v * 10.0f + 0.5f);
    if (pct < 0)
        pct = 0;
    if (pct > 100)
        pct = 100;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", pct);
    return GameValue(buf);
}

/// triSetVolume ["music"|"effects"|"speech", 0..100] -> "OK" or "FAIL:..."
GameValue TriSetVolume(const GameState* /*state*/, GameValuePar arg)
{
    if (!GSoundsys)
        return GameValue("FAIL:no_sound");
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected_array");
    const GameArrayType& a = arg;
    if (a.Size() < 2)
        return GameValue("FAIL:need_name_and_pct");
    int row = VolumeRowFromName(((RString)(GameStringType)a[0]).Data());
    if (row < 0)
        return GameValue("FAIL:bad_row");
    int pct = (int)(GameScalarType)a[1];
    if (pct < 0)
        pct = 0;
    if (pct > 100)
        pct = 100;
    float vf = pct / 10.0f;
    switch (row)
    {
        case 0:
            GSoundsys->SetCDVolume(vf);
            break;
        case 1:
            GSoundsys->SetWaveVolume(vf);
            break;
        case 2:
            GSoundsys->SetSpeechVolume(vf);
            break;
    }
    return GameValue("OK");
}

/// triGetEAX -> "0" (off) / "1" (on)
GameValue TriGetEAX(const GameState* /*state*/)
{
    if (!GSoundsys)
        return GameValue("FAIL:no_sound");
    return GameValue(GSoundsys->GetEAX() ? "1" : "0");
}

/// triSetEAX <0|1> -> "OK"
GameValue TriSetEAX(const GameState* /*state*/, GameValuePar arg)
{
    if (!GSoundsys)
        return GameValue("FAIL:no_sound");
    bool on = ((int)(GameScalarType)arg) != 0;
    GSoundsys->EnableEAX(on);
    return GameValue("OK");
}

/// triCountSuppressedMusicWaves -> integer count of music waves muted
/// by SuppressMusicForPreview.  AudioPage::Mount calls Suppress, Unmount
/// calls Resume, so this should be non-zero while the Audio screen is
/// up and the menu is playing background music.
GameValue TriCountSuppressedMusicWaves(const GameState* /*state*/)
{
    if (!GSoundsys)
        return GameValue((float)0);
    return GameValue((float)GSoundsys->CountSuppressedMusicWaves());
}

// Audio per-frame counter probes — read IAudioSystem::GetCounters()
// for integration / Pester tests (audio-invariants A-28, A-30).
// Each verb returns one field of AudioCounters as a scalar so SQF
// tests can assert deterministic budget state without screen-scraping
// logs.

/// triAudioActive3D -> int (currently-playing 3D waves)
GameValue TriAudioActive3D(const GameState* /*state*/)
{
    return GameValue((float)(GSoundsys ? GSoundsys->GetCounters().active3D : 0));
}

/// triAudioActive2D -> int (currently-playing 2D waves)
GameValue TriAudioActive2D(const GameState* /*state*/)
{
    return GameValue((float)(GSoundsys ? GSoundsys->GetCounters().active2D : 0));
}

/// triAudioPausedNow -> int (waves currently in Paused state)
GameValue TriAudioPausedNow(const GameState* /*state*/)
{
    return GameValue((float)(GSoundsys ? GSoundsys->GetCounters().paused : 0));
}

/// triAudioEvictedThisFrame -> int (3D waves stopped by budget in the
/// last AdvanceAll tick — A-07 / A-09 / A-30)
GameValue TriAudioEvictedThisFrame(const GameState* /*state*/)
{
    return GameValue((float)(GSoundsys ? GSoundsys->GetCounters().evictedThisFrame : 0));
}

/// triAudioPausedThisFrame -> int (waves transitioned to Paused in the
/// last AdvanceAll tick — A-10)
GameValue TriAudioPausedThisFrame(const GameState* /*state*/)
{
    return GameValue((float)(GSoundsys ? GSoundsys->GetCounters().pausedThisFrame : 0));
}

/// triAudioAllocFailures -> int (session-cumulative AL allocation
/// failures — A-23)
GameValue TriAudioAllocFailures(const GameState* /*state*/)
{
    return GameValue((float)(GSoundsys ? GSoundsys->GetCounters().allocFailures : 0));
}

/// triCategoryPreviewRestarts -> int (session-cumulative
/// StartCategoryPreview restarts — A-18)
GameValue TriCategoryPreviewRestarts(const GameState* /*state*/)
{
    return GameValue((float)(GSoundsys ? GSoundsys->CountCategoryPreviewRestarts() : 0));
}

// Graphics screen probes — read live engine state for integration +
// Pester tests of the GraphicsPage row → engine pipeline.  The cfg
// file is ParamFile-readable so most assertions read it directly; the
// tri verbs cover the engine-side state where reading the file isn't
// enough (e.g. "did SetSwapInterval actually take effect").

/// triGetSwapInterval -> int (0 off / 1 on / -1 adaptive).
GameValue TriGetSwapInterval(const GameState* /*state*/)
{
    if (!GEngine)
        return GameValue((float)1);
    return GameValue((float)GEngine->GetSwapInterval());
}

/// triGetObjectLodBias -> float (Scene's current LOD bias).
GameValue TriGetObjectLodBias(const GameState* /*state*/)
{
    if (!GScene)
        return GameValue((float)1.0f);
    return GameValue(GScene->GetObjectLODBias());
}

/// triGetTerrainGrid -> float (current preferred grid in metres).
GameValue TriGetTerrainGrid(const GameState* /*state*/)
{
    if (!GScene)
        return GameValue((float)0);
    return GameValue(GScene->GetPreferredTerrainGrid());
}

/// triGetViewDistance -> float (current preferred visibility distance in metres).
GameValue TriGetViewDistance(const GameState* /*state*/)
{
    if (!GScene)
        return GameValue((float)0);
    return GameValue(GScene->GetPreferredViewDistance());
}

// Display screen probes — used by integration + Pester tests to
// inspect the live engine state (monitor / window mode / resolution /
// refresh rate) so a test can assert the Apply path actually moved
// the engine, not just the cfg file.

/// triGetMonitor -> int (current monitor index)
GameValue TriGetMonitor(const GameState* /*state*/)
{
    if (!GEngine)
        return GameValue((float)0);
    return GameValue((float)GEngine->GetCurrentMonitor());
}

/// triGetWindowMode -> "fullscreen" / "borderless" / "windowed"
GameValue TriGetWindowMode(const GameState* /*state*/)
{
    if (!GEngine)
        return GameValue("windowed");
    switch (GEngine->GetCurrentWindowMode())
    {
        case WindowMode::Fullscreen:
            return GameValue("fullscreen");
        case WindowMode::Borderless:
            return GameValue("borderless");
        case WindowMode::Windowed:
            return GameValue("windowed");
    }
    return GameValue("windowed");
}

static GameValue FormatDisplayModeValue(bool ok, int w, int h, int refresh,
                                        const char* unavailable = "FAIL:not_supported")
{
    if (!ok)
        return GameValue(unavailable);

    char buf[64];
    snprintf(buf, sizeof(buf), "%dx%d@%d", w, h, refresh);
    return GameValue(buf);
}

/// triGetDesktopDisplayMode -> "WxH@R" or "FAIL:not_supported"
GameValue TriGetDesktopDisplayMode(const GameState* /*state*/)
{
    if (!GEngine)
        return GameValue("FAIL:no_engine");

    int w = 0;
    int h = 0;
    int refresh = 0;
    // Sequence the query before reading w/h/refresh: passing them as later
    // arguments to FormatDisplayModeValue alongside the call that fills them is
    // unsequenced, and clang reads the zero-initialised values first (-> "0x0@0").
    const bool ok = GEngine->GetDesktopDisplayMode(w, h, refresh);
    return FormatDisplayModeValue(ok, w, h, refresh);
}

/// triGetCurrentDisplayMode -> "WxH@R" or "FAIL:not_supported"
GameValue TriGetCurrentDisplayMode(const GameState* /*state*/)
{
    if (!GEngine)
        return GameValue("FAIL:no_engine");

    int w = 0;
    int h = 0;
    int refresh = 0;
    const bool ok = GEngine->GetCurrentDisplayMode(w, h, refresh);
    return FormatDisplayModeValue(ok, w, h, refresh);
}

/// triGetRequestedFullscreenMode -> "WxH@R" or "none"
GameValue TriGetRequestedFullscreenMode(const GameState* /*state*/)
{
    if (!GEngine)
        return GameValue("FAIL:no_engine");

    int w = 0;
    int h = 0;
    int refresh = 0;
    const bool ok = GEngine->GetRequestedFullscreenMode(w, h, refresh);
    return FormatDisplayModeValue(ok, w, h, refresh, "none");
}

/// triGetAspectSettings -> "left,top,uiLeft,uiTop,uiRight,uiBottom"
GameValue TriGetAspectSettings(const GameState* /*state*/)
{
    if (!GEngine)
        return GameValue("FAIL:no_engine");

    AspectSettings aspect{};
    GEngine->GetAspectSettings(aspect);

    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%.4f,%.4f,%.4f,%.4f,%.4f,%.4f", aspect.leftFOV, aspect.topFOV, aspect.uiTopLeftX,
             aspect.uiTopLeftY, aspect.uiBottomRightX, aspect.uiBottomRightY);
    return GameValue(buffer);
}

/// triGetPresentationRect "RectKind" -> "x,y,w,h" in normalized physical viewport coordinates
GameValue TriGetPresentationRect(const GameState* /*state*/, GameValuePar arg)
{
    if (!GEngine)
        return GameValue("FAIL:no_engine");

    Presentation::RectKind kind;
    const RString name = arg;
    if (!Presentation::ParseRectKind(name.Data(), kind))
        return GameValue("FAIL:unknown_rect");

    AspectSettings aspect{};
    GEngine->GetAspectSettings(aspect);
    const Presentation::Rect rect = Presentation::ResolveRect(kind, GEngine->Width(), GEngine->Height(), aspect);

    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%.6f,%.6f,%.6f,%.6f", rect.x, rect.y, rect.w, rect.h);
    return GameValue(buffer);
}

/// triGetPresentationRectPx "RectKind" -> "x,y,w,h" in active render pixels
GameValue TriGetPresentationRectPx(const GameState* /*state*/, GameValuePar arg)
{
    if (!GEngine)
        return GameValue("FAIL:no_engine");

    Presentation::RectKind kind;
    const RString name = arg;
    if (!Presentation::ParseRectKind(name.Data(), kind))
        return GameValue("FAIL:unknown_rect");

    AspectSettings aspect{};
    GEngine->GetAspectSettings(aspect);
    const int width = GEngine->Width();
    const int height = GEngine->Height();
    const Presentation::Rect rect = Presentation::ResolveRect(kind, width, height, aspect);

    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%.1f,%.1f,%.1f,%.1f", rect.x * width, rect.y * height, rect.w * width,
             rect.h * height);
    return GameValue(buffer);
}

/// triListMonitors -> pipe-joined list "[idx] Name|[idx] Name|..."
GameValue TriListMonitors(const GameState* /*state*/)
{
    if (!GEngine)
        return GameValue("");
    FindArray<MonitorInfo> mons;
    GEngine->ListMonitors(mons);
    RString out;
    for (int i = 0; i < mons.Size(); ++i)
    {
        char buf[160];
        snprintf(buf, sizeof(buf), "[%d] %s", mons[i].index,
                 mons[i].name.GetLength() ? mons[i].name.Data() : "Display");
        if (out.GetLength())
            out = out + RString("|");
        out = out + RString(buf);
    }
    return GameValue(out);
}

/// triSwitchOutputDevice "name" -> "OK" / "FAIL:..." (empty / "" -> default)
GameValue TriSwitchOutputDevice(const GameState* /*state*/, GameValuePar arg)
{
    if (!GSoundsys)
        return GameValue("FAIL:no_sound");
    RString name = arg;
    bool ok = GSoundsys->SwitchOutputDevice(name.GetLength() ? name.Data() : nullptr);
    return GameValue(ok ? "OK" : "FAIL:switch_failed");
}

/// triGetOutputDevice -> "" (system default) or device name
GameValue TriGetOutputDevice(const GameState* /*state*/)
{
    if (!GSoundsys)
        return GameValue("FAIL:no_sound");
    return GameValue(GSoundsys->GetCurrentOutputDevice().c_str());
}

/// triListOutputDevices -> "name1|name2|..." pipe-joined
GameValue TriListOutputDevices(const GameState* /*state*/)
{
    if (!GSoundsys)
        return GameValue("FAIL:no_sound");
    std::string out;
    for (auto& d : GSoundsys->ListOutputDevices())
    {
        if (!out.empty())
            out += "|";
        out += d;
    }
    LOG_INFO(Core, "[tri] triListOutputDevices -> '{}'", out);
    return GameValue(out.c_str());
}

/// triAudioPageOutputCount -> total device entries the AudioPage picker
/// would expose (1 for "System default" + the OAL enumeration count).
/// Mirrors what AudioPage::AudioProvider::RebuildOutputDeviceList does.
GameValue TriAudioPageOutputCount(const GameState* /*state*/)
{
    if (!GSoundsys)
        return GameValue("FAIL:no_sound");
    int count = 1 + (int)GSoundsys->ListOutputDevices().size();
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", count);
    return GameValue(buf);
}

/// triAudioPageInputCount -> total mic entries the picker exposes
/// ("System default" + VoNCapture::listDevices()).
GameValue TriAudioPageInputCount(const GameState* /*state*/)
{
    int count = 1 + (int)VoNCapture::listDevices().size();
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", count);
    return GameValue(buf);
}

/// triStartPreview — kick off the GSoundsys audio preview chain.  Same
/// path AudioProvider::SetRowValue uses on volume/device changes.
GameValue TriStartPreview(const GameState* /*state*/)
{
    if (!GSoundsys)
        return GameValue("FAIL:no_sound");
    GSoundsys->StartPreview();
    return GameValue("OK");
}

/// triStartCategoryPreview "music|effects|speech" — kick off the same
/// focused-row category preview AudioPage uses after its dwell timer.
GameValue TriStartCategoryPreview(const GameState* /*state*/, GameValuePar arg)
{
    if (!GSoundsys)
        return GameValue("FAIL:no_sound");

    RString kindName = arg;
    if (strcmpi(kindName, "music") == 0)
    {
        GSoundsys->StartCategoryPreview(WaveMusic);
        return GameValue("OK");
    }
    if (strcmpi(kindName, "effects") == 0 || strcmpi(kindName, "effect") == 0)
    {
        GSoundsys->StartCategoryPreview(WaveEffect);
        return GameValue("OK");
    }
    if (strcmpi(kindName, "speech") == 0)
    {
        GSoundsys->StartCategoryPreview(WaveSpeech);
        return GameValue("OK");
    }
    return GameValue("FAIL:unknown_category");
}

/// triListInputDevices -> "name1|name2|..." pipe-joined (uses VoNCapture)
GameValue TriListInputDevices(const GameState* /*state*/)
{
    std::string out;
    for (auto& d : VoNCapture::listDevices())
    {
        if (!out.empty())
            out += "|";
        out += d;
    }
    LOG_INFO(Core, "[tri] triListInputDevices -> '{}'", out);
    return GameValue(out.c_str());
}

// Latched pixel for cross-step comparison (triPixelLatch / triAssertPixelLatch).
// Single slot — tests want to sample-then-act-then-resample, that's it.
static bool g_latchValid = false;
static uint8_t g_latchRGB[3] = {0, 0, 0};

/// triPixelLatch [u, v] — sample a pixel and store it for later comparison
/// via triAssertPixelLatch.  Returns "R,G,B" or "FAIL:...".  Only one slot
/// is kept; subsequent latches overwrite.
GameValue TriPixelLatch(const GameState* /*state*/, GameValuePar arg)
{
    if (!GEngine)
        return GameValue("FAIL:no_engine");
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected_array");
    const GameArrayType& a = arg;
    if (a.Size() < 2)
        return GameValue("FAIL:expected_uv");
    float u = (float)(GameScalarType)a[0];
    float v = (float)(GameScalarType)a[1];

    int w = GEngine->Width();
    int h = GEngine->Height();
    uint8_t rgb[3] = {0, 0, 0};
    if (!GEngine->SamplePixel((int)(u * (w - 1)), (int)(v * (h - 1)), rgb))
        return GameValue("FAIL:not_supported");

    g_latchRGB[0] = rgb[0];
    g_latchRGB[1] = rgb[1];
    g_latchRGB[2] = rgb[2];
    g_latchValid = true;

    char buf[64];
    snprintf(buf, sizeof(buf), "%d,%d,%d", rgb[0], rgb[1], rgb[2]);
    LOG_INFO(Core, "[tri] triPixelLatch uv=({},{}) -> ({},{},{})", u, v, rgb[0], rgb[1], rgb[2]);
    return GameValue(buf);
}

/// triAssertPixelChanged [u, v, tol] — inverse of triAssertPixelLatch.
/// Sample the pixel, assert it differs from the latched value by AT
/// LEAST `tol` on at least one channel.  Returns "OK" or
/// "FAIL:unchanged p_now=R,G,B p_latched=R,G,B".  Used to prove that
/// some action between latch and assert actually altered the frame
/// (e.g. wheel scroll moved the camera).
GameValue TriAssertPixelChanged(const GameState* /*state*/, GameValuePar arg)
{
    if (!GEngine)
        return GameValue("FAIL:no_engine");
    if (!g_latchValid)
        return GameValue("FAIL:no_latch");
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected_array");
    const GameArrayType& a = arg;
    if (a.Size() < 3)
        return GameValue("FAIL:need_u_v_tol");
    float u = (float)(GameScalarType)a[0];
    float v = (float)(GameScalarType)a[1];
    int tol = (int)(GameScalarType)a[2];

    int w = GEngine->Width();
    int h = GEngine->Height();
    uint8_t rgb[3] = {0, 0, 0};
    if (!GEngine->SamplePixel((int)(u * (w - 1)), (int)(v * (h - 1)), rgb))
        return GameValue("FAIL:not_supported");

    int dr = std::abs((int)rgb[0] - (int)g_latchRGB[0]);
    int dg = std::abs((int)rgb[1] - (int)g_latchRGB[1]);
    int db = std::abs((int)rgb[2] - (int)g_latchRGB[2]);
    if (dr >= tol || dg >= tol || db >= tol)
    {
        LOG_INFO(Core, "[tri] triAssertPixelChanged OK now=({},{},{}) latched=({},{},{}) max(d)={} tol>={}", rgb[0],
                 rgb[1], rgb[2], g_latchRGB[0], g_latchRGB[1], g_latchRGB[2], std::max(dr, std::max(dg, db)), tol);
        return GameValue("OK");
    }

    char buf[160];
    snprintf(buf, sizeof(buf), "FAIL:unchanged p_now=%d,%d,%d p_latched=%d,%d,%d (max delta < tol=%d)", rgb[0], rgb[1],
             rgb[2], g_latchRGB[0], g_latchRGB[1], g_latchRGB[2], tol);
    LOG_ERROR(Core, "[tri] triAssertPixelChanged {}", buf);
    return GameValue(buf);
}

#include <Poseidon/Foundation/Modules/Modules.hpp>

namespace Poseidon
{
RString GetTmpSaveDirectory();
}

/// triCheatEndMissionOutcomes — returns "|"-joined list of outcomes
/// Cmd_EndMission::AvailableOutcomes() reports for the current mission.
/// Empty string means the cheat is unavailable (no active mission).
GameValue TriCheatEndMissionOutcomes(const GameState* /*state*/)
{
    const auto outcomes = DebugCheats::Cmd_EndMission::AvailableOutcomes();
    std::string joined;
    for (size_t i = 0; i < outcomes.size(); i++)
    {
        if (i > 0)
            joined += '|';
        joined += outcomes[i];
    }
    return GameValue(joined.c_str());
}

/// triAssertCheatAvailable ["<name>", <expected 0|1>] — query the
/// DebugCommands registry's Available() predicate for a cheat and assert
/// it matches the expected value.  Used to regression-test gating logic
/// (e.g. End Mission must be unavailable on the main menu).
GameValue TriAssertCheatAvailable(const GameState* /*state*/, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected_array");
    const GameArrayType& a = arg;
    if (a.Size() < 2)
        return GameValue("FAIL:need_name_expected");
    if (a[0].GetType() != GameString)
        return GameValue("FAIL:name_must_be_string");
    GameStringType s = static_cast<GameStringType>(a[0]);
    const char* name = (const char*)s;
    int expected = static_cast<int>(static_cast<GameScalarType>(a[1])) ? 1 : 0;

    const auto* cmd = DebugCommands::Find(name ? name : "");
    if (!cmd)
    {
        char buf[96];
        snprintf(buf, sizeof(buf), "FAIL:unknown_cheat=%s", name ? name : "(null)");
        return GameValue(buf);
    }
    int actual = (cmd->available && cmd->available()) ? 1 : 0;
    if (actual == expected)
        return GameValue("OK");
    char buf[96];
    snprintf(buf, sizeof(buf), "FAIL:expected=%d,actual=%d,cheat=%s", expected, actual, name);
    return GameValue(buf);
}

/// triClockHour — returns current time-of-day in hours (0..24).
/// Glob.clock.GetTimeOfDay() returns a [0..1] fraction; we multiply.
GameValue TriClockHour(const GameState* /*state*/)
{
    return GameValue(Glob.clock.GetTimeOfDay() * 24.0f);
}

/// triAssertClockHourEqualLatch <tol> — sample current hour, assert
/// |current - latched| <= tol modulo 24 (so the assertion still works
/// when SkipTime crosses midnight).  Latches with triLatchClockHour.
static float g_clockLatch = -1.0f;
GameValue TriLatchClockHour(const GameState* /*state*/)
{
    g_clockLatch = Glob.clock.GetTimeOfDay() * 24.0f;
    return GameValue(g_clockLatch);
}

/// triAssertClockHourDeltaSinceLatch <expected_hours> <tol> — assert
/// (current - latched) mod 24 is within tol of expected_hours.  Array
/// args: [expected, tol].
GameValue TriAssertClockHourDeltaSinceLatch(const GameState* /*state*/, GameValuePar arg)
{
    if (g_clockLatch < 0)
        return GameValue("FAIL:no_latch");
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected_array");
    const GameArrayType& a = arg;
    if (a.Size() < 2)
        return GameValue("FAIL:need_expected_tol");
    float expected = static_cast<float>(static_cast<GameScalarType>(a[0]));
    float tol = static_cast<float>(static_cast<GameScalarType>(a[1]));
    float now = Glob.clock.GetTimeOfDay() * 24.0f;
    float delta = now - g_clockLatch;
    // Normalize to (-12, 12] so wrap-around (e.g. 23 -> 1 with +2h) is captured.
    while (delta > 12.0f)
        delta -= 24.0f;
    while (delta <= -12.0f)
        delta += 24.0f;
    float diff = delta - expected;
    if (diff < 0)
        diff = -diff;
    if (diff <= tol)
        return GameValue("OK");
    char buf[96];
    snprintf(buf, sizeof(buf), "FAIL:delta=%.3f,expected=%.3f,tol=%.3f", delta, expected, tol);
    return GameValue(buf);
}

/// triTeleportPlayerTo [x, y, z] — direct teleport via the same path
/// the map click handler uses (Move(transform) on player's vehicle).
/// Used by the map-teleport regression to exercise the hook without
/// needing to fake a map click.  Returns "OK" or "FAIL:<reason>".
GameValue TriTeleportPlayerTo(const GameState* /*state*/, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected_array");
    const GameArrayType& a = arg;
    if (a.Size() < 3)
        return GameValue("FAIL:need_xyz");
    if (!GWorld || !GWorld->GetRealPlayer() || !GWorld->GetRealPlayer()->Brain())
        return GameValue("FAIL:no_player");
    EntityAI* veh = GWorld->GetRealPlayer()->Brain()->GetVehicle();
    if (!veh)
        return GameValue("FAIL:no_vehicle");
    Vector3 pos(static_cast<float>(static_cast<GameScalarType>(a[0])),
                static_cast<float>(static_cast<GameScalarType>(a[1])),
                static_cast<float>(static_cast<GameScalarType>(a[2])));
    Matrix4 trans = veh->Transform();
    trans.SetPosition(pos);
    veh->Move(trans);
    return GameValue("OK");
}

// Defined in world.cpp — force / release the render-view override.
extern void World_SetTriViewOverride(Vector3Par pos, Vector3Par dir, Vector3Par up);
extern void World_ClearTriViewOverride();

// triSetView [px,py,pz, dx,dy,dz, (ux,uy,uz)] — pin the render camera to an exact
// world-space position + look direction (raw engine [X,up,Z] order, the same
// values a dev-cheats position dump prints).  Optional up triple controls roll;
// defaults to world up.  Reproduces a captured view without a mission camera.
GameValue TriSetView(const GameState* /*state*/, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected_array");
    const GameArrayType& a = arg;
    if (a.Size() != 6 && a.Size() != 9)
        return GameValue("FAIL:need [px,py,pz,dx,dy,dz] or with up [..,ux,uy,uz]");
    if (!GWorld)
        return GameValue("FAIL:no_world");
    Vector3 pos(static_cast<float>(static_cast<GameScalarType>(a[0])),
                static_cast<float>(static_cast<GameScalarType>(a[1])),
                static_cast<float>(static_cast<GameScalarType>(a[2])));
    Vector3 dir(static_cast<float>(static_cast<GameScalarType>(a[3])),
                static_cast<float>(static_cast<GameScalarType>(a[4])),
                static_cast<float>(static_cast<GameScalarType>(a[5])));
    Vector3 up = a.Size() == 9 ? Vector3(static_cast<float>(static_cast<GameScalarType>(a[6])),
                                         static_cast<float>(static_cast<GameScalarType>(a[7])),
                                         static_cast<float>(static_cast<GameScalarType>(a[8])))
                               : Vector3(0, 1, 0);
    if (dir.SquareSize() < 1e-6f)
        return GameValue("FAIL:zero_dir");
    World_SetTriViewOverride(pos, dir.Normalized(), up.Normalized());
    return GameValue("OK");
}

// triClearView — release the override, returning to the player view.
GameValue TriClearView(const GameState* /*state*/)
{
    World_ClearTriViewOverride();
    return GameValue("OK");
}

// triRdcCapture "path" — trigger a RenderDoc capture of the next swap (no-op
// unless the game was launched under RenderDoc, i.e. librenderdoc loaded).  The
// optional string sets the capture-file path template.  Used to diagnose render
// bugs at the draw-call level (e.g. the two-vehicle transparency bug, B-038).
GameValue TriRdcCapture(const GameState* /*state*/, GameValuePar arg)
{
    if (!RdcCapture::Available())
        RdcCapture::Init();
    if (!RdcCapture::Available())
        return GameValue("FAIL:renderdoc_not_loaded");
    if (arg.GetType() == GameString)
    {
        RString tmpl = arg;
        if (tmpl.GetLength() > 0)
            RdcCapture::SetPathTemplate(tmpl);
    }
    RdcCapture::Trigger();
    return GameValue("OK");
}

/// triPlayerPosX / triPlayerPosY / triPlayerPosZ — single-axis getters
/// for the player's vehicle position (or -1 if no player).  Used by
/// the teleport regression to confirm the position changed.
GameValue TriPlayerPosX(const GameState* /*state*/)
{
    if (!GWorld || !GWorld->GetRealPlayer() || !GWorld->GetRealPlayer()->Brain())
        return GameValue(-1.0f);
    EntityAI* veh = GWorld->GetRealPlayer()->Brain()->GetVehicle();
    return GameValue(veh ? veh->Position()[0] : -1.0f);
}
GameValue TriPlayerPosY(const GameState* /*state*/)
{
    if (!GWorld || !GWorld->GetRealPlayer() || !GWorld->GetRealPlayer()->Brain())
        return GameValue(-1.0f);
    EntityAI* veh = GWorld->GetRealPlayer()->Brain()->GetVehicle();
    return GameValue(veh ? veh->Position()[1] : -1.0f);
}
GameValue TriPlayerPosZ(const GameState* /*state*/)
{
    if (!GWorld || !GWorld->GetRealPlayer() || !GWorld->GetRealPlayer()->Brain())
        return GameValue(-1.0f);
    EntityAI* veh = GWorld->GetRealPlayer()->Brain()->GetVehicle();
    return GameValue(veh ? veh->Position()[2] : -1.0f);
}

/// triCheatShowAllUnits <bool> — set show-all-units toggle.
GameValue TriCheatShowAllUnits(const GameState* /*state*/, GameValuePar arg)
{
    if (!DebugCheats::Cmd_ShowAllUnits::Available())
        return GameValue("FAIL:unavailable");
    DebugCheats::Cmd_ShowAllUnits::SetActive(static_cast<bool>(static_cast<GameBoolType>(arg)));
    return GameValue("OK");
}

/// triCheatMapTeleport <bool> — set click-to-teleport toggle.
GameValue TriCheatMapTeleport(const GameState* /*state*/, GameValuePar arg)
{
    if (!DebugCheats::Cmd_MapTeleport::Available())
        return GameValue("FAIL:unavailable");
    DebugCheats::Cmd_MapTeleport::SetActive(static_cast<bool>(static_cast<GameBoolType>(arg)));
    return GameValue("OK");
}

/// triViewDistance — return current visibility (ENGINE_CONFIG.tacticalZ).
GameValue TriViewDistance(const GameState* /*state*/)
{
    return GameValue(ENGINE_CONFIG.tacticalZ);
}

/// triFps — return current engine FPS (1000 / GetLastFrameDuration) or 0.
GameValue TriFps(const GameState* /*state*/)
{
    if (!GEngine)
        return GameValue(0.0f);
    auto ms = GEngine->GetLastFrameDuration();
    return GameValue(ms > 0 ? 1000.0f / static_cast<float>(ms) : 0.0f);
}

/// triMemoryMB — return MemoryUsed() in MB.
GameValue TriMemoryMB(const GameState* /*state*/)
{
    return GameValue(static_cast<float>(MemoryUsed()) / (1024.0f * 1024.0f));
}

/// triConsoleRun "<line>" — mirror DebugOverlay's Console tab dispatch:
/// bare lines try DebugCommands::Run first, then fall back to SQF
/// EvaluateMultiple.  `:` prefix forces SQF.  Returns "OK" if dispatch
/// reached something (command found or SQF parsed); "FAIL:<why>"
/// otherwise.  Used to test the console without ImGui input
/// simulation — exact same dispatcher the UI uses.
GameValue TriConsoleRun(const GameState* /*state*/, GameValuePar arg)
{
    if (arg.GetType() != GameString)
        return GameValue("FAIL:expected_string");
    GameStringType s = static_cast<GameStringType>(arg);
    std::string_view line = (const char*)s ? (const char*)s : "";
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
        line.remove_prefix(1);
    if (line.empty())
        return GameValue("FAIL:empty");

    const bool forceSqf = line.front() == ':';
    if (forceSqf)
        line.remove_prefix(1);

    if (!forceSqf)
    {
        std::string out;
        if (DebugCommands::Run(line, out))
            return GameValue("OK");
    }
    if (!GWorld || !GWorld->GetGameState())
        return GameValue("FAIL:no_game_state");
    GWorld->GetGameState()->EvaluateMultiple(std::string(line).c_str());
    return GameValue("OK");
}

/// triSetPillarboxBarsEnabled <bool> — flip the global pillarbox
/// painting toggle (AspectRatio::SetPillarboxBarsEnabled).  When
/// false, Object::DrawWidescreenPillarbox is a no-op, so 4:3 overlays
/// no longer get the lateral black-bar treatment.  Used to drive the
/// pillarbox pixel regression — flip off, sample a corner pixel,
/// confirm it's NOT black; flip on, sample again, confirm it's black.
GameValue TriSetPillarboxBarsEnabled(const GameState* /*state*/, GameValuePar arg)
{
    AspectRatio::SetPillarboxBarsEnabled(static_cast<bool>(static_cast<GameBoolType>(arg)));
    return GameValue("OK");
}

/// triPillarboxBarsEnabled — read the current toggle (0 or 1).
GameValue TriPillarboxBarsEnabled(const GameState* /*state*/)
{
    return GameValue(AspectRatio::ArePillarboxBarsEnabled() ? 1.0f : 0.0f);
}

// Re-resolve + apply the aspect settings for the current viewport after a
// live control changed (mirrors the resize-hook path in GameApplication).
static void ReapplyAspect()
{
    if (GEngine)
        GEngine->FireResizePostHook(GEngine->Width(), GEngine->Height());
}

/// triSetAspectOverride <bool> — enable/disable the live aspect override.
/// When off, the display.cfg-driven policy applies and the other live
/// controls are ignored.
GameValue TriSetAspectOverride(const GameState* /*state*/, GameValuePar arg)
{
    AspectRatio::Live().overrideEnabled = static_cast<bool>(static_cast<GameBoolType>(arg));
    ReapplyAspect();
    return GameValue("OK");
}

/// triSetDisplayStyle <0|1> — 0=Modern, 1=Legacy.  Enables the override.
GameValue TriSetDisplayStyle(const GameState* /*state*/, GameValuePar arg)
{
    const int v = static_cast<int>(static_cast<GameScalarType>(arg));
    AspectRatio::Live().style = (v == 1) ? AspectRatio::Legacy : AspectRatio::Modern;
    AspectRatio::Live().overrideEnabled = true;
    ReapplyAspect();
    return GameValue("OK");
}

/// triSetUltrawideClamp <0|1|2> — 0=Off, 1=21:9, 2=16:9.  Enables override.
GameValue TriSetUltrawideClamp(const GameState* /*state*/, GameValuePar arg)
{
    const int v = static_cast<int>(static_cast<GameScalarType>(arg));
    AspectRatio::UltrawideClamp clamp = AspectRatio::ClampOff;
    if (v == 1)
        clamp = AspectRatio::Clamp21x9;
    else if (v == 2)
        clamp = AspectRatio::Clamp16x9;
    AspectRatio::Live().clamp = clamp;
    AspectRatio::Live().overrideEnabled = true;
    ReapplyAspect();
    return GameValue("OK");
}

/// triSetAspectPillarbox <bool> — crop the 3D world to the clamp band with
/// black bars (whole frame locked to the band).  Enables override.
GameValue TriSetAspectPillarbox(const GameState* /*state*/, GameValuePar arg)
{
    AspectRatio::Live().pillarbox = static_cast<bool>(static_cast<GameBoolType>(arg));
    AspectRatio::Live().overrideEnabled = true;
    ReapplyAspect();
    return GameValue("OK");
}

/// triSetHudClamp <bool> — keep the HUD centered in the clamp band even
/// when the world is full-width.  Enables override.
GameValue TriSetHudClamp(const GameState* /*state*/, GameValuePar arg)
{
    AspectRatio::Live().hudClamp = static_cast<bool>(static_cast<GameBoolType>(arg));
    AspectRatio::Live().overrideEnabled = true;
    ReapplyAspect();
    return GameValue("OK");
}

/// triSetManualViewport [l,t,r,b] — crop the world to an explicit rect
/// (fractions of the window).  Empty array [] clears manual mode.  Enables
/// override.
GameValue TriSetManualViewport(const GameState* /*state*/, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected_array");
    const GameArrayType& a = arg;
    AspectRatio::LiveControls& live = AspectRatio::Live();
    live.overrideEnabled = true;
    if (a.Size() == 0)
    {
        live.manualRect = false;
    }
    else if (a.Size() == 4)
    {
        live.manualRect = true;
        live.rectL = static_cast<float>(static_cast<GameScalarType>(a[0]));
        live.rectT = static_cast<float>(static_cast<GameScalarType>(a[1]));
        live.rectR = static_cast<float>(static_cast<GameScalarType>(a[2]));
        live.rectB = static_cast<float>(static_cast<GameScalarType>(a[3]));
    }
    else
    {
        return GameValue("FAIL:need_[l,t,r,b]_or_[]");
    }
    ReapplyAspect();
    return GameValue("OK");
}

/// triApplyPresentationViewport "RectKind" — crop the world viewport to a named
/// presentation rect. Enables the live aspect override.
GameValue TriApplyPresentationViewport(const GameState* /*state*/, GameValuePar arg)
{
    if (!GEngine)
        return GameValue("FAIL:no_engine");

    Presentation::RectKind kind;
    const RString name = arg;
    if (!Presentation::ParseRectKind(name.Data(), kind))
        return GameValue("FAIL:unknown_rect");

    AspectSettings aspect{};
    GEngine->GetAspectSettings(aspect);
    const Presentation::Rect rect = Presentation::ResolveRect(kind, GEngine->Width(), GEngine->Height(), aspect);

    AspectRatio::LiveControls& live = AspectRatio::Live();
    live.overrideEnabled = true;
    live.manualRect = true;
    live.rectL = rect.x;
    live.rectT = rect.y;
    live.rectR = rect.x + rect.w;
    live.rectB = rect.y + rect.h;
    ReapplyAspect();
    return GameValue("OK");
}

/// triGetWorldViewport -> "left,top,right,bottom" (fractions of window).
GameValue TriGetWorldViewport(const GameState* /*state*/)
{
    if (!GEngine)
        return GameValue("FAIL:no_engine");
    AspectSettings aspect{};
    GEngine->GetAspectSettings(aspect);
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%.4f,%.4f,%.4f,%.4f", aspect.worldLeft, aspect.worldTop, aspect.worldRight,
             aspect.worldBottom);
    return GameValue(buffer);
}

/// triCheatWeather <overcast> — set overcast 0..1 instantly.  Returns
/// "OK" / "FAIL:unavailable".
GameValue TriCheatWeather(const GameState* /*state*/, GameValuePar arg)
{
    if (!DebugCheats::Cmd_SetWeather::Available())
        return GameValue("FAIL:unavailable");
    float overcast = static_cast<float>(static_cast<GameScalarType>(arg));
    std::string out;
    DebugCheats::Cmd_SetWeather::InvokeOvercast(overcast, out);
    return GameValue("OK");
}

/// triCheatTimeMultiplier <mult> — set GWorld->_acceleratedTime.
/// Returns "OK" / "FAIL:unavailable".  Engine saturates to its own
/// [kTimeAccMin, kTimeAccMax] range; query via triTimeMultiplier.
GameValue TriCheatTimeMultiplier(const GameState* /*state*/, GameValuePar arg)
{
    if (!DebugCheats::Cmd_TimeMultiplier::Available())
        return GameValue("FAIL:unavailable");
    float mult = static_cast<float>(static_cast<GameScalarType>(arg));
    std::string out;
    DebugCheats::Cmd_TimeMultiplier::SetValue(mult, out);
    return GameValue("OK");
}

/// triTimeMultiplier — returns current engine-saturated multiplier as scalar.
GameValue TriTimeMultiplier(const GameState* /*state*/)
{
    return GameValue(DebugCheats::Cmd_TimeMultiplier::Get());
}

/// triCheatSkipTime <hours> — skip in-game time by the given signed
/// number of hours.  Returns "OK" / "FAIL:unavailable".
GameValue TriCheatSkipTime(const GameState* /*state*/, GameValuePar arg)
{
    if (!DebugCheats::Cmd_SkipTime::Available())
        return GameValue("FAIL:unavailable");
    float hours = static_cast<float>(static_cast<GameScalarType>(arg));
    std::string out;
    DebugCheats::Cmd_SkipTime::InvokeHours(hours, out);
    return GameValue("OK");
}

/// triCheatUnlockCampaign — fires the campaign-unlock cheat.  Writes
/// <TmpSaveDir>/<campaign>.sqc for every installed campaign.  Returns
/// "OK" on success, "FAIL:no_campaigns" if the campaign scan found
/// nothing on disk.
GameValue TriCheatUnlockCampaign(const GameState* /*state*/)
{
    std::string out;
    DebugCheats::Cmd_UnlockCampaign::Invoke("", out);
    if (out.rfind("unlockcampaign: every", 0) == 0)
        return GameValue("OK");
    return GameValue("FAIL:no_campaigns");
}

/// triCheatStorePosition — fire the storepos cheat (log + clipboard).
/// Returns "OK" on success.  Mirrors the Cheats-tab "Store position"
/// button + the `storepos` console command.
GameValue TriCheatStorePosition(const GameState* /*state*/)
{
    if (!DebugCheats::Cmd_StorePosition::Available())
        return GameValue("FAIL:unavailable");
    std::string out;
    DebugCheats::Cmd_StorePosition::Invoke("", out);
    if (out.rfind("storepos: written to log + copied", 0) == 0 ||
        out.rfind("storepos: written to log; clipboard FAILED", 0) == 0)
        return GameValue("OK");
    return GameValue(("FAIL:" + out).c_str());
}

/// triCheatSaveGame — force-save the current world.  Returns the
/// status string the cheat's Invoke produced ("save: written to ..."
/// on success, "save: failed ..." or "save: no active world" on
/// failure).  Mirrors the Cheats-tab "Save game now" button.
GameValue TriCheatSaveGame(const GameState* /*state*/)
{
    if (!DebugCheats::Cmd_SaveGame::Available())
        return GameValue("FAIL:unavailable");
    std::string out;
    DebugCheats::Cmd_SaveGame::Invoke("", out);
    if (out.rfind("save: written", 0) == 0)
        return GameValue("OK");
    return GameValue(("FAIL:" + out).c_str());
}

/// triCheatLoadGame — restore the save.fps written by triCheatSaveGame.
/// Returns "OK" / "FAIL:<reason>".  Mirrors the "Load game now" button.
GameValue TriCheatLoadGame(const GameState* /*state*/)
{
    if (!DebugCheats::Cmd_LoadGame::Available())
        return GameValue("FAIL:unavailable");
    std::string out;
    DebugCheats::Cmd_LoadGame::Invoke("", out);
    if (out.rfind("load: restored", 0) == 0)
        return GameValue("OK");
    return GameValue(("FAIL:" + out).c_str());
}

/// triCheatGod <bool> — set the god-mode toggle.  Returns "OK" or
/// "FAIL:unavailable".  Mirrors the Cheats-tab god checkbox.
GameValue TriCheatGod(const GameState* /*state*/, GameValuePar arg)
{
    if (!DebugCheats::Cmd_God::Available())
        return GameValue("FAIL:unavailable");
    bool active = static_cast<bool>(static_cast<GameBoolType>(arg));
    DebugCheats::Cmd_God::SetActive(active);
    return GameValue("OK");
}

/// triCheatGodActive — returns current god-mode toggle as 0/1 scalar.
GameValue TriCheatGodActive(const GameState* /*state*/)
{
    return GameValue(DebugCheats::Cmd_God::IsActive() ? 1.0f : 0.0f);
}

/// triCheatInfiniteAmmo <bool> — set the infinite-ammo toggle.
GameValue TriCheatInfiniteAmmo(const GameState* /*state*/, GameValuePar arg)
{
    if (!DebugCheats::Cmd_InfiniteAmmo::Available())
        return GameValue("FAIL:unavailable");
    bool active = static_cast<bool>(static_cast<GameBoolType>(arg));
    DebugCheats::Cmd_InfiniteAmmo::SetActive(active);
    return GameValue("OK");
}

/// triCheatInfiniteAmmoActive — returns current toggle as 0/1 scalar.
GameValue TriCheatInfiniteAmmoActive(const GameState* /*state*/)
{
    return GameValue(DebugCheats::Cmd_InfiniteAmmo::IsActive() ? 1.0f : 0.0f);
}

/// triCheatInfiniteFuel <bool> — set fuel toggle.
GameValue TriCheatInfiniteFuel(const GameState* /*state*/, GameValuePar arg)
{
    if (!DebugCheats::Cmd_InfiniteFuel::Available())
        return GameValue("FAIL:unavailable");
    DebugCheats::Cmd_InfiniteFuel::SetActive(static_cast<bool>(static_cast<GameBoolType>(arg)));
    return GameValue("OK");
}

/// triCheatInfiniteArmor <bool> — set armor toggle.
GameValue TriCheatInfiniteArmor(const GameState* /*state*/, GameValuePar arg)
{
    if (!DebugCheats::Cmd_InfiniteArmor::Available())
        return GameValue("FAIL:unavailable");
    DebugCheats::Cmd_InfiniteArmor::SetActive(static_cast<bool>(static_cast<GameBoolType>(arg)));
    return GameValue("OK");
}

/// triPlayerVehicle — returns the player's current vehicle as a string
/// identifier ("none" if on foot, "type@addr" otherwise).  Used to
/// confirm the player actually mounted a vehicle before testing armor.
GameValue TriPlayerVehicle(const GameState* /*state*/)
{
    if (!GWorld || !GWorld->GetRealPlayer() || !GWorld->GetRealPlayer()->Brain())
        return GameValue("none");
    Transport* veh = GWorld->GetRealPlayer()->Brain()->GetVehicleIn();
    if (!veh)
        return GameValue("none");
    char buf[64];
    snprintf(buf, sizeof(buf), "%s@%p", (const char*)veh->GetDebugName(), (void*)veh);
    return GameValue(buf);
}

/// triPlayerName — returns the selected player profile name
/// (Glob.header.playerName), set by startup profile selection.
GameValue TriPlayerName(const GameState* /*state*/)
{
    return GameValue((const char*)Glob.header.playerName);
}

/// triSetPlayerPref <name> — write the persisted last-used profile name (the
/// PlayerName pref). A sequence phase uses this to leave a stale pref for the
/// next boot. Returns "OK".
GameValue TriSetPlayerPref(const GameState* /*state*/, GameValuePar arg)
{
    std::string name = ((RString)(GameStringType)arg).Data();
    Foundation::prefsSetString(AppName, "PlayerName", name.c_str());
    return GameValue("OK");
}

/// triAssertProfileMissing <name> -> "OK" if no profile directory exists for
/// <name> under the user dir, else "FAIL:profile_exists". A later boot must not
/// recreate a profile that a stale pref points at.
GameValue TriAssertProfileMissing(const GameState* /*state*/, GameValuePar arg)
{
    std::string name = ((RString)(GameStringType)arg).Data();
    std::string dir =
        Poseidon::ProfileManager::GetProfileDirPath(Poseidon::Foundation::GamePaths::Instance().UserDir(), name);
    std::error_code ec;
    if (std::filesystem::is_directory(dir, ec))
        return GameValue("FAIL:profile_exists");
    return GameValue("OK");
}

/// triDamagePlayerVehicle <amount> — apply SetDammage to the vehicle
/// the player is currently in.  Returns "OK" / "FAIL:not_in_vehicle".
GameValue TriDamagePlayerVehicle(const GameState* /*state*/, GameValuePar arg)
{
    if (!GWorld || !GWorld->GetRealPlayer() || !GWorld->GetRealPlayer()->Brain())
        return GameValue("FAIL:no_player");
    Transport* veh = GWorld->GetRealPlayer()->Brain()->GetVehicleIn();
    if (!veh)
        return GameValue("FAIL:not_in_vehicle");
    veh->SetDammage(static_cast<float>(static_cast<GameScalarType>(arg)));
    return GameValue("OK");
}

/// triPlayerVehicleDammage — current dammage of the player's vehicle,
/// or -1 if on foot.
GameValue TriPlayerVehicleDammage(const GameState* /*state*/)
{
    if (!GWorld || !GWorld->GetRealPlayer() || !GWorld->GetRealPlayer()->Brain())
        return GameValue(-1.0f);
    Transport* veh = GWorld->GetRealPlayer()->Brain()->GetVehicleIn();
    if (!veh)
        return GameValue(-1.0f);
    return GameValue(veh->GetTotalDammage());
}

/// triConsumePlayerVehicleFuel <amount> — call Transport::ConsumeFuel
/// directly on the player's vehicle.  Returns "OK" / "FAIL:not_in_vehicle".
GameValue TriConsumePlayerVehicleFuel(const GameState* /*state*/, GameValuePar arg)
{
    if (!GWorld || !GWorld->GetRealPlayer() || !GWorld->GetRealPlayer()->Brain())
        return GameValue("FAIL:no_player");
    Transport* veh = GWorld->GetRealPlayer()->Brain()->GetVehicleIn();
    if (!veh)
        return GameValue("FAIL:not_in_vehicle");
    // Refuel(-x) routes through ConsumeFuel(+x) and is the public path.
    veh->Refuel(-static_cast<float>(static_cast<GameScalarType>(arg)));
    return GameValue("OK");
}

/// triPlayerVehicleFuel — fuel of player's vehicle, or -1 if on foot.
GameValue TriPlayerVehicleFuel(const GameState* /*state*/)
{
    if (!GWorld || !GWorld->GetRealPlayer() || !GWorld->GetRealPlayer()->Brain())
        return GameValue(-1.0f);
    Transport* veh = GWorld->GetRealPlayer()->Brain()->GetVehicleIn();
    if (!veh)
        return GameValue(-1.0f);
    return GameValue(veh->GetFuel());
}

/// triLatchPlayerVehicleFuel — snapshot the player's vehicle fuel.
/// Returns the current fuel (or -1 if not in vehicle).
GameValue TriLatchPlayerVehicleFuel(const GameState* /*state*/)
{
    if (!GWorld || !GWorld->GetRealPlayer() || !GWorld->GetRealPlayer()->Brain())
        return GameValue(-1.0f);
    Transport* veh = GWorld->GetRealPlayer()->Brain()->GetVehicleIn();
    if (!veh)
        return GameValue(-1.0f);
    return GameValue(veh->GetFuel());
}

/// triPlayerCurrentMagazineAmmo — returns the ammo count of the real
/// player's currently-selected magazine, or -1 if no player / no
/// magazine.  Used by the infinite-ammo regression to verify the
/// FireWeapon refund kicked in.
GameValue TriPlayerCurrentMagazineAmmo(const GameState* /*state*/)
{
    if (!GWorld)
        return GameValue(-1.0f);
    EntityAI* p = dyn_cast<EntityAI>(GWorld->GetRealPlayer());
    if (!p)
        return GameValue(-1.0f);
    int weapon = p->SelectedWeapon();
    if (weapon < 0 || weapon >= p->NMagazineSlots())
        return GameValue(-1.0f);
    const MagazineSlot& slot = p->GetMagazineSlot(weapon);
    if (!slot._magazine)
        return GameValue(-1.0f);
    return GameValue((float)slot._magazine->_ammo);
}

/// triLatchPlayerAmmo — snapshot the real player's current magazine ammo count.
/// Returns the count (or -1 if no player / no magazine).
GameValue TriLatchPlayerAmmo(const GameState* /*state*/)
{
    if (!GWorld)
        return GameValue(-1.0f);
    EntityAI* p = dyn_cast<EntityAI>(GWorld->GetRealPlayer());
    if (!p)
        return GameValue(-1.0f);
    int weapon = p->SelectedWeapon();
    if (weapon < 0 || weapon >= p->NMagazineSlots())
        return GameValue(-1.0f);
    const MagazineSlot& slot = p->GetMagazineSlot(weapon);
    if (!slot._magazine)
        return GameValue(-1.0f);
    return GameValue((float)slot._magazine->_ammo);
}

/// triFirePlayerWeapon — invoke EntityAI::FireWeapon for the player's
/// currently-selected weapon at a dummy target (self position).  Used
/// to drive the infinite-ammo regression test.  Returns "OK" or
/// "FAIL:<reason>".
GameValue TriFirePlayerWeapon(const GameState* /*state*/)
{
    if (!GWorld)
        return GameValue("FAIL:no_world");
    EntityAI* p = dyn_cast<EntityAI>(GWorld->GetRealPlayer());
    if (!p)
        return GameValue("FAIL:no_player");
    int weapon = p->SelectedWeapon();
    if (weapon < 0 || weapon >= p->NMagazineSlots())
        return GameValue("FAIL:no_weapon");
    if (!p->FireWeapon(weapon, nullptr))
        return GameValue("FAIL:fire_rejected");
    return GameValue("OK");
}

/// triDoDammageToPlayer <amount> — invoke Object::DoDammage on the
/// player's vehicle.  This drives the same code path bullets and
/// shells take (LocalDammage → DoDammage → SetTotalDammage), so it
/// exercises the right hook for projectile damage.
/// Distinct from triDamagePlayer (which calls SetDammage directly,
/// the script-driven path).  Used to regress god + armor cheats
/// against bullet fire.  valRange must be negative for "direct hit"
/// per the engine convention; we pass -2 so the avgDammage calc at
/// line ~346 multiplies by 1.0 (i.e. full hit on the player's body).
GameValue TriDoDammageToPlayer(const GameState* /*state*/, GameValuePar arg)
{
    if (!GWorld)
        return GameValue("FAIL:no_world");
    Person* p = GWorld->GetRealPlayer();
    if (!p || !p->Brain())
        return GameValue("FAIL:no_player");
    EntityAI* veh = p->Brain()->GetVehicle();
    if (!veh)
        return GameValue("FAIL:no_vehicle");
    float amount = static_cast<float>(static_cast<GameScalarType>(arg));
    veh->DoDammage(nullptr, VZero, amount, -2.0f * amount, RString("test"));
    return GameValue("OK");
}

/// triDamagePlayer <amount> — apply Object::SetDammage(amount) to the
/// real player.  Used to drive god-mode regression: without the cheat,
/// damage 1.0 destroys the player; with god mode on, the dammage stays
/// at the previous value.  Returns "OK" or "FAIL:no_player".
GameValue TriDamagePlayer(const GameState* /*state*/, GameValuePar arg)
{
    if (!GWorld)
        return GameValue("FAIL:no_world");
    Person* p = GWorld->GetRealPlayer();
    if (!p)
        return GameValue("FAIL:no_player");
    float amount = static_cast<float>(static_cast<GameScalarType>(arg));
    p->SetDammage(amount);
    return GameValue("OK");
}

/// triPlayerDammage — returns current GetTotalDammage() of the real
/// player (0..1 scalar), or -1 if no player.
GameValue TriPlayerDammage(const GameState* /*state*/)
{
    if (!GWorld)
        return GameValue(-1.0f);
    Person* p = GWorld->GetRealPlayer();
    if (!p)
        return GameValue(-1.0f);
    return GameValue(p->GetTotalDammage());
}

/// triAssertCheatActive ["<name>", <expected 0|1>] — query cheat
/// toggle state.  Currently only "god" has a toggle exposed; other
/// names return FAIL.
GameValue TriAssertCheatActive(const GameState* /*state*/, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected_array");
    const GameArrayType& a = arg;
    if (a.Size() < 2)
        return GameValue("FAIL:need_name_expected");
    if (a[0].GetType() != GameString)
        return GameValue("FAIL:name_must_be_string");
    GameStringType s = static_cast<GameStringType>(a[0]);
    const char* name = (const char*)s;
    int expected = static_cast<int>(static_cast<GameScalarType>(a[1])) ? 1 : 0;

    int actual = -1;
    if (name && strcmp(name, "god") == 0)
        actual = DebugCheats::Cmd_God::IsActive() ? 1 : 0;
    else if (name && strcmp(name, "infammo") == 0)
        actual = DebugCheats::Cmd_InfiniteAmmo::IsActive() ? 1 : 0;
    else if (name && strcmp(name, "inffuel") == 0)
        actual = DebugCheats::Cmd_InfiniteFuel::IsActive() ? 1 : 0;
    else if (name && strcmp(name, "infarmor") == 0)
        actual = DebugCheats::Cmd_InfiniteArmor::IsActive() ? 1 : 0;
    else if (name && strcmp(name, "showall") == 0)
        actual = DebugCheats::Cmd_ShowAllUnits::IsActive() ? 1 : 0;
    else if (name && strcmp(name, "mapteleport") == 0)
        actual = DebugCheats::Cmd_MapTeleport::IsActive() ? 1 : 0;
    if (actual < 0)
    {
        char buf[96];
        snprintf(buf, sizeof(buf), "FAIL:unknown_toggle=%s", name ? name : "(null)");
        return GameValue(buf);
    }
    if (actual == expected)
        return GameValue("OK");
    char buf[96];
    snprintf(buf, sizeof(buf), "FAIL:cheat=%s,expected=%d,actual=%d", name, expected, actual);
    return GameValue(buf);
}

/// triIsEndForced — returns World::IsEndForced() as 0/1 scalar.
GameValue TriIsEndForced(const GameState* /*state*/)
{
    if (!GWorld)
        return GameValue(-1.0f);
    return GameValue(GWorld->IsEndForced() ? 1.0f : 0.0f);
}

/// triEndMission "<outcome>" — force-end the active mission.  Outcome is
/// one of: win | lose | killed | end1..end6.  Returns "OK" or
/// "FAIL:<reason>".  Mirrors the Cheats-tab End Mission button.
GameValue TriEndMission(const GameState* /*state*/, GameValuePar arg)
{
    if (arg.GetType() != GameString)
        return GameValue("FAIL:expected_string");
    if (!DebugCheats::Cmd_EndMission::Available())
        return GameValue("FAIL:unavailable (no active mission)");
    GameStringType s = static_cast<GameStringType>(arg);
    const char* outcome = (const char*)s;
    std::string out;
    DebugCheats::Cmd_EndMission::Invoke(outcome ? outcome : "", out);
    if (out.rfind("unknown", 0) == 0)
        return GameValue(("FAIL:" + out).c_str());
    LOG_INFO(Core, "[tri] triEndMission {} -> {}", outcome ? outcome : "", out);
    return GameValue("OK");
}

/// triRemount — request an in-process re-mount (reload all game content in place).
/// Only valid from the main menu (GModeIntro); a reload mid-mission would evict assets
/// the simulation still references. The reload is deferred to a safe between-frames
/// point (AppIdle), so follow with triSimFrames/triSimUntil to let it complete.
/// Returns "OK" or "FAIL:<reason>".
GameValue TriRemount(const GameState* /*state*/)
{
    if (Poseidon::GApp == nullptr)
        return GameValue("FAIL:no_app");
    if (GWorld == nullptr || GWorld->GetMode() != GModeIntro)
        return GameValue("FAIL:not_at_menu");
    Poseidon::GApp->RequestRemount(); // current mod set, serviced at the next AppIdle
    LOG_INFO(Core, "[tri] triRemount requested");
    return GameValue("OK");
}

/// triLoadedShapeCount — number of shapes (meshes) currently resident in the global
/// ShapeBank. Loading a mission warms this cache; it must drop back to the menu
/// baseline after returning to the menu and re-mounting.
GameValue TriLoadedShapeCount(const GameState* /*state*/)
{
    int n = 0;
    Shapes.ForEach([&](Poseidon::LODShapeWithShadow&) { n++; });
    return GameValue(static_cast<float>(n));
}

/// triFontTune ["prefix", renderPx, widthScale]
/// triFontTune ["prefix", renderPx, widthScale, baselineOffset]
/// triFontTune ["prefix", renderPx, widthScale, baselineOffset, syntheticBold]
/// triFontTune ["prefix", renderPx, widthScale, baselineOffset, syntheticBold, letterSpacing]
/// triFontTune ["prefix", renderPx, widthScale, baselineOffset, syntheticBold, letterSpacing, "ttfPath"]
/// Live-mutate one mapping row and refresh fonts.  No rebuild needed.
/// baselineOffset / syntheticBold / letterSpacing are atlas px (0 = none).
GameValue TriFontTune(const GameState* state, GameValuePar arg)
{
    auto& arr = static_cast<const GameArrayType&>(arg);
    if (arr.Size() < 3)
        return GameValue("FAIL:need [prefix, renderPx, widthScale] "
                         "(+ optional baseline, bold, spacing, ttfPath)");
    RString prefix = static_cast<RString>(arr[0]);
    int rpx = static_cast<int>(static_cast<GameScalarType>(arr[1]));
    float ws = static_cast<float>(static_cast<GameScalarType>(arr[2]));
    float baseline = 0.0f, bold = 0.0f, spacing = 0.0f;
    if (arr.Size() >= 4)
        baseline = static_cast<float>(static_cast<GameScalarType>(arr[3]));
    if (arr.Size() >= 5)
        bold = static_cast<float>(static_cast<GameScalarType>(arr[4]));
    if (arr.Size() >= 6)
        spacing = static_cast<float>(static_cast<GameScalarType>(arr[5]));
    const char* ttfPath = nullptr;
    if (arr.Size() >= 7)
    {
        static thread_local RString tmp;
        tmp = static_cast<RString>(arr[6]);
        ttfPath = (const char*)tmp;
    }
    bool ok = SetFontMappingTuning((const char*)prefix, rpx, ws, baseline, bold, spacing, ttfPath);
    LOG_INFO(Core,
             "[tri] triFontTune prefix={} rpx={} ws={:.3f} baseline={:.2f} bold={:.2f} spacing={:.2f} ttf={} -> {}",
             (const char*)prefix, rpx, ws, baseline, bold, spacing, ttfPath ? ttfPath : "(unchanged)",
             ok ? "OK" : "FAIL:no-match");
    return GameValue(ok ? "OK" : "FAIL:no matching prefix");
}

/// triFontDump — return the current font mapping as a copy-pasteable C++ table.
GameValue TriFontDump(const GameState* state)
{
    return GameValue(DumpFontTable());
}

// Issue #11 — pause / radio diagnostics
// triPauseGame / triUnpauseGame drive GWorld->_enableSimulation, the
// same cheat-pause flag the in-game options overlay sets.  This bypasses
// the menu-render path but exercises the same `paused` argument that
// World::PreSimulate routes into SoundScene::AdvanceAll, which is the
// surface 92d6ecdf changed.

/// triPauseGame -> "OK" or "FAIL:<reason>"
GameValue TriPauseGame(const GameState* /*state*/)
{
    if (!GWorld)
        return GameValue("FAIL:no_world");
    GWorld->SetSimulationEnabled(false);
    LOG_INFO(Core, "[tri] triPauseGame — _enableSimulation=false");
    return GameValue("OK");
}

/// triUnpauseGame -> "OK" or "FAIL:<reason>"
GameValue TriUnpauseGame(const GameState* /*state*/)
{
    if (!GWorld)
        return GameValue("FAIL:no_world");
    GWorld->SetSimulationEnabled(true);
    LOG_INFO(Core, "[tri] triUnpauseGame — _enableSimulation=true");
    return GameValue("OK");
}

/// triRadioWaveCount -> scalar count of live 2D speech waves in
/// SoundScene::_all2DSounds (i.e. radio messages currently active).
GameValue TriRadioWaveCount(const GameState* /*state*/)
{
    if (!GSoundScene)
        return GameValue((float)0);
    return GameValue((float)GSoundScene->CountActive2DSpeechWaves());
}

// triMuteVoN <dpnid> / triIgnoreChat <dpnid> — client-local mute/ignore
// (fizzy #159).  <dpnid> == -1 mutes/ignores all remote players, including any
// that join after the call (identity list may not be fully populated at call time).
static void ApplyMuteIgnore(int id, bool voice)
{
    if (id == -1)
    {
        voice ? SetVoiceMutedAll(true) : SetChatIgnoredAll(true);
        return;
    }
    voice ? SetVoiceMuted(id, true) : SetChatIgnored(id, true);
}

GameValue TriMuteVoN(const GameState* /*state*/, GameValuePar arg)
{
    ApplyMuteIgnore(static_cast<int>(static_cast<GameScalarType>(arg)), true);
    return GameValue("OK");
}

GameValue TriIgnoreChat(const GameState* /*state*/, GameValuePar arg)
{
    ApplyMuteIgnore(static_cast<int>(static_cast<GameScalarType>(arg)), false);
    return GameValue("OK");
}

GameValue TriSendVonTestTone(const GameState* /*state*/, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected_array");

    const GameArrayType& a = arg;
    const int frames = a.Size() > 0 ? static_cast<int>(static_cast<GameScalarType>(a[0])) : 20;
    const int amplitude = a.Size() > 1 ? static_cast<int>(static_cast<GameScalarType>(a[1])) : 10000;
    if (frames <= 0)
        return GameValue("FAIL:bad_frames");

    NetworkClient* client = GNetworkManager.GetClient();
    if (!client)
        return GameValue("FAIL:no_client");

    const int sent = client->SendVoiceTestTone(frames, amplitude);
    if (sent <= 0)
        return GameValue("FAIL:not_sent");

    char buf[32];
    snprintf(buf, sizeof(buf), "OK:%d", sent);
    return GameValue(buf);
}

GameValue TriMpAssignSelf(const GameState* /*state*/, GameValuePar arg)
{
    const int role = static_cast<int>(static_cast<GameScalarType>(arg));
    if (role < 0)
        return GameValue("FAIL:bad_role");
    const int player = GNetworkManager.GetPlayer();
    if (player == NO_PLAYER)
        return GameValue("FAIL:no_player");
    GNetworkManager.AssignPlayer(role, player);
    GNetworkManager.ClientReady(NGSPrepareOK);
    return GameValue("OK");
}

static bool ParseTriMpAssign(const char* assign, TargetSide& outSide, int& outSlot)
{
    if (!assign)
        return false;
    const char* colon = std::strchr(assign, ':');
    if (!colon || colon == assign || !colon[1])
        return false;
    std::string side(assign, colon);
    if (side == "WEST")
        outSide = TWest;
    else if (side == "EAST")
        outSide = TEast;
    else if (side == "RES")
        outSide = TGuerrila;
    else if (side == "CIV")
        outSide = TCivilian;
    else
        return false;
    outSlot = std::atoi(colon + 1);
    return outSlot >= 1;
}

static int FindTriMpAssignRole(TargetSide side, int slot, int player)
{
    int sideCount = 0;
    for (int i = 0; i < GNetworkManager.NPlayerRoles(); ++i)
    {
        const PlayerRole* role = GNetworkManager.GetPlayerRole(i);
        if (!role || role->side != side)
            continue;
        ++sideCount;
        if (sideCount == slot)
        {
            if (role->player != NO_PLAYER && role->player != AI_PLAYER && role->player != player)
                return -2;
            return i;
        }
    }
    return -1;
}

GameValue TriMpAssignSelfSlot(const GameState* /*state*/, GameValuePar arg)
{
    GameStringType assign = static_cast<GameStringType>(arg);
    TargetSide side;
    int slot = 0;
    if (!ParseTriMpAssign((const char*)assign, side, slot))
        return GameValue("FAIL:bad_slot");
    const int player = GNetworkManager.GetPlayer();
    if (player == NO_PLAYER)
        return GameValue("FAIL:no_player");
    const int role = FindTriMpAssignRole(side, slot, player);
    if (role == -1)
        return GameValue("FAIL:no_slot");
    if (role == -2)
        return GameValue("FAIL:occupied");
    GNetworkManager.AssignPlayer(role, player);
    GNetworkManager.ClientReady(NGSPrepareOK);
    return GameValue("OK");
}

GameValue TriMpClientReady(const GameState* /*state*/, GameValuePar arg)
{
    const int state = static_cast<int>(static_cast<GameScalarType>(arg));
    if (state < NGSNone || state > NGSPlay)
        return GameValue("FAIL:bad_state");
    const NetworkGameState readyState = static_cast<NetworkGameState>(state);
    GNetworkManager.ClientReady(readyState);
    if (readyState == NGSPlay && GNetworkManager.IsServer() && GNetworkManager.GetServerState() == NGSBriefing)
    {
        GNetworkManager.SetGameState(NGSPlay);
    }
    return GameValue("OK");
}

/// triRadioWaveStates -> "name:state|name:state|..." for every 2D speech
/// wave still resident in SoundScene._all2DSounds.  state ∈
/// {playing, stopped, terminated}.  Empty string when no speech is in
/// flight.  Note: WaveOAL::Pause clears _playing, so a paused wave
/// reports "stopped" here — that's still observable via the count not
/// dropping after a pause.
GameValue TriRadioWaveStates(const GameState* /*state*/)
{
    if (!GSoundScene)
        return GameValue("");
    return GameValue(GSoundScene->Describe2DSpeechWaves());
}

/// triRadioWaveOffset "<name_substring>" -> scalar seconds.  Finds the first
/// 2D wave whose Name() contains <name_substring> (case sensitive — names
/// in OAL are stored lowercased) and returns its live AL_SEC_OFFSET.
/// Returns -1 when no matching wave is in _all2DSounds.  Used by the
/// Issue #11 regression test to assert pause→resume preserves position.
GameValue TriRadioWaveOffset(const GameState* /*state*/, GameValuePar arg)
{
    if (!GSoundScene)
        return GameValue((float)-1);
    GameStringType name = static_cast<GameStringType>(arg);
    const char* needle = (const char*)name;
    if (!needle || !*needle)
        return GameValue((float)-1);
    return GameValue(GSoundScene->Find2DWaveOffsetSeconds(needle));
}

GameValue TriAssertNetworkAssetExists(const GameState* /*state*/, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected_array");
    const GameArrayType& a = arg;
    if (a.Size() < 3)
        return GameValue("FAIL:need_kind_owner_file");
    if (a[0].GetType() != GameString)
        return GameValue("FAIL:kind_must_be_string");
    if (a[1].GetType() != GameString)
        return GameValue("FAIL:owner_must_be_string");
    if (a[2].GetType() != GameString)
        return GameValue("FAIL:file_must_be_string");

    const GameStringType kindArg = static_cast<GameStringType>(a[0]);
    const GameStringType ownerArg = static_cast<GameStringType>(a[1]);
    const GameStringType fileArg = static_cast<GameStringType>(a[2]);
    const RString path = Poseidon::BuildNetworkTransferredAssetProbeTmpPath(
        RString((const char*)kindArg), RString((const char*)ownerArg), RString((const char*)fileArg));
    if (path.GetLength() == 0)
        return GameValue("FAIL:invalid_asset_path");

    if (QIFStream::FileExists(path))
        return GameValue("OK");

    char buf[512];
    snprintf(buf, sizeof(buf), "FAIL:missing:%s", (const char*)path);
    return GameValue(buf);
}

// Issue #9 — save / load + vehicle lock state diagnostics

/// triSaveGame "label" — write a binary save to the test tmp directory
/// (UserDir/Saved/Tmp/<label>.fps). Returns "OK" or "FAIL:<reason>".
///
/// Uses Poseidon::GetTmpSaveDirectory() rather than GetSaveDirectory() because the
/// regular save path embeds GetBaseSubdirectory() which in simulate-server
/// mode is an absolute TempDir path — concatenating it into the user dir
/// produces a broken double-absolute path that CreatePath cannot mkdir.
GameValue TriSaveGame(const GameState* /*state*/, GameValuePar arg)
{
    GameStringType label = static_cast<GameStringType>(arg);
    const char* labelStr = (const char*)label;
    if (!labelStr || !*labelStr)
        return GameValue("FAIL:empty_label");
    if (!GWorld)
        return GameValue("FAIL:no_world");

    RString dir = Poseidon::GetTmpSaveDirectory();
    RString name = dir + RString(labelStr) + RString(".fps");
    LOG_INFO(Core, "[tri] triSaveGame \"{}\" -> {}", labelStr, (const char*)name);
    bool ok = GWorld->SaveBin((const char*)name, 0);
    return GameValue(ok ? "OK" : "FAIL:save_failed");
}

/// triLoadGame "label" — reload a binary save written by triSaveGame.
/// Returns "OK" or "FAIL:<reason>". Loads from the test tmp directory
/// (UserDir/Saved/Tmp/<label>.fps), not the regular save directory.
GameValue TriLoadGame(const GameState* /*state*/, GameValuePar arg)
{
    GameStringType label = static_cast<GameStringType>(arg);
    const char* labelStr = (const char*)label;
    if (!labelStr || !*labelStr)
        return GameValue("FAIL:empty_label");
    if (!GWorld)
        return GameValue("FAIL:no_world");

    RString dir = Poseidon::GetTmpSaveDirectory();
    RString name = dir + RString(labelStr) + RString(".fps");
    LOG_INFO(Core, "[tri] triLoadGame \"{}\" <- {}", labelStr, (const char*)name);
    bool ok = GWorld->LoadBin((const char*)name, 0);
    return GameValue(ok ? "OK" : "FAIL:load_failed");
}

/// triVehicleLockState "<name>" — return the current lock state of a named
/// Transport (vehicle). Returns "LOCKED", "UNLOCKED", "DEFAULT", or
/// "FAIL:<reason>".
GameValue TriVehicleLockState(const GameState* state, GameValuePar arg)
{
    GameStringType name = static_cast<GameStringType>(arg);
    const char* nameStr = (const char*)name;
    if (!nameStr || !*nameStr)
        return GameValue("FAIL:empty_name");
    if (!state)
        return GameValue("FAIL:no_state");

    GameValue v = state->VarGet(nameStr);
    if (v.GetType() != GameObject)
        return GameValue("FAIL:not_an_object");
    Object* obj = GetObject(v);
    if (!obj)
        return GameValue("FAIL:null_object");
    Transport* veh = dyn_cast<Transport>(obj);
    if (!veh)
        return GameValue("FAIL:not_transport");

    switch (veh->GetLock())
    {
        case LSLocked:
            return GameValue("LOCKED");
        case LSUnlocked:
            return GameValue("UNLOCKED");
        case LSDefault:
            return GameValue("DEFAULT");
        default:
            return GameValue("FAIL:unknown_state");
    }
}

/// triRadioEnabled -> bool. World::IsRadioEnabled() — the `enableRadio` scripting
/// flag (drives radio-sentence audibility via World::SetActiveChannels). Used to
/// prove enableRadio survives save/load.
GameValue TriRadioEnabled(const GameState* /*state*/)
{
    if (!GWorld)
        return GameValue(false);
    return GameValue(GWorld->IsRadioEnabled());
}

/// triUnitAIDisabled "<name>" -> scalar bitmask of the named unit's disabled AI
/// subsystems (AIUnit::DisabledAI: TARGET=1, MOVE=2, AUTOTARGET=4, ANIM=8). 0 =
/// fully enabled, -1 = name did not resolve to a unit. Resolves the unit the same
/// way `disableAI` does. Used to prove disableAI survives save/load.
GameValue TriUnitAIDisabled(const GameState* state, GameValuePar arg)
{
    GameStringType name = static_cast<GameStringType>(arg);
    const char* nameStr = (const char*)name;
    if (!nameStr || !*nameStr || !state)
        return GameValue((float)-1);

    GameValue v = state->VarGet(nameStr);
    if (v.GetType() != GameObject)
        return GameValue((float)-1);
    Object* obj = GetObject(v);
    EntityAI* ai = obj ? dyn_cast<EntityAI>(obj) : nullptr;
    AIUnit* unit = ai ? ai->CommanderUnit() : nullptr;
    if (!unit)
        return GameValue((float)-1);
    return GameValue((float)unit->GetAIDisabled());
}

/// triAssertSubgroupLeader "<name>" - reselect the named unit's subgroup leader
/// and assert the unit is still selected. Cargo passengers are not IsUnit()
/// vehicle commanders, but authored GETOUT chains still rely on them remaining
/// subgroup leaders.
GameValue TriAssertSubgroupLeader(const GameState* state, GameValuePar arg)
{
    GameStringType name = static_cast<GameStringType>(arg);
    const char* nameStr = (const char*)name;
    if (!nameStr || !*nameStr || !state)
        return GameValue("FAIL:empty_name");

    GameValue v = state->VarGet(nameStr);
    if (v.GetType() != GameObject)
        return GameValue("FAIL:not_an_object");
    Object* obj = GetObject(v);
    EntityAI* ai = obj ? dyn_cast<EntityAI>(obj) : nullptr;
    AIUnit* unit = ai ? ai->CommanderUnit() : nullptr;
    if (!unit)
        return GameValue("FAIL:not_a_unit");
    AISubgroup* subgroup = unit->GetSubgroup();
    if (!subgroup)
        return GameValue("FAIL:no_subgroup");
    subgroup->SelectLeader();
    AIUnit* leader = subgroup->Leader();
    if (leader != unit)
        return GameValue("FAIL:not_subgroup_leader");
    return GameValue("OK");
}

INIT_MODULE(GameStateExtTest, 3)
{
    const AppConfig& appConfig = AppConfig::Instance();
    if (!appConfig.DevMode() && appConfig.GetHarnessPort() < 0 && appConfig.GetTestMissionPath().empty())
        return;

    GGameState.NewNularOp(GameNular(GameString, "triVersion", TriVersion));
    GGameState.NewNularOp(GameNular(GameScalar, "triGameMode", TriGameMode));
    GGameState.NewNularOp(GameNular(GameScalar, "triDisplay", TriDisplay));
    GGameState.NewNularOp(GameNular(GameScalar, "triEditorMode", TriEditorMode));
    GGameState.NewNularOp(GameNular(GameArray, "triControls", TriControls));
    GGameState.NewNularOp(GameNular(GameNothing, "triEndTest", TriEndTest));
    GGameState.NewFunction(GameFunction(GameString, "triSetBrightness", TriSetBrightness, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triSaveGame", TriSaveGame, GameString));
    GGameState.NewFunction(GameFunction(GameString, "triLoadGame", TriLoadGame, GameString));
    GGameState.NewFunction(GameFunction(GameString, "triVehicleLockState", TriVehicleLockState, GameString));
    GGameState.NewNularOp(GameNular(GameBool, "triRadioEnabled", TriRadioEnabled));
    GGameState.NewFunction(GameFunction(GameScalar, "triUnitAIDisabled", TriUnitAIDisabled, GameString));
    GGameState.NewFunction(GameFunction(GameString, "triAssertSubgroupLeader", TriAssertSubgroupLeader, GameString));
    GGameState.NewNularOp(GameNular(GameScalar, "triFrameCount", TriFrameCount));
    GGameState.NewNularOp(GameNular(GameScalar, "triErrorCount", TriErrorCount));
    GGameState.NewNularOp(GameNular(GameString, "triResetErrorCount", TriResetErrorCount));
    GGameState.NewNularOp(GameNular(GameString, "triSceneReady", TriSceneReady));
    GGameState.NewNularOp(GameNular(GameBool, "triSendAltEnter", TriSendAltEnter));
    GGameState.NewNularOp(GameNular(GameString, "triIsWindowed", TriIsWindowed));
    GGameState.NewNularOp(GameNular(GameString, "triGetWindowSize", TriGetWindowSize));
    GGameState.NewFunction(GameFunction(GameString, "triGetPresentationRect", TriGetPresentationRect, GameString));
    GGameState.NewFunction(GameFunction(GameString, "triGetPresentationRectPx", TriGetPresentationRectPx, GameString));
    GGameState.NewNularOp(
        GameNular(GameString, "triAssertWindowedSmallerThanDesktop", TriAssertWindowedSmallerThanDesktop));
    GGameState.NewNularOp(GameNular(GameString, "triGetFrameShape", TriGetFrameShape));
    GGameState.NewFunction(GameFunction(GameString, "triAssertFrameShape", TriAssertFrameShape, GameArray));
    GGameState.NewNularOp(GameNular(GameString, "triResetGLErrorBaseline", TriResetGLErrorBaseline));
    GGameState.NewNularOp(GameNular(GameString, "triDrawColorbar", TriDrawColorbar));
    GGameState.NewNularOp(GameNular(GameString, "triAssertColorbarBytes", TriAssertColorbarBytes));
    GGameState.NewNularOp(GameNular(GameString, "triDrawGradient3d", TriDrawGradient3d));
    GGameState.NewNularOp(GameNular(GameString, "triDrawClearBlue", TriDrawClearBlue));
    GGameState.NewNularOp(GameNular(GameString, "triDrawClearMagenta", TriDrawClearMagenta));
    GGameState.NewNularOp(GameNular(GameString, "triDrawQuad2d", TriDrawQuad2d));
    GGameState.NewNularOp(GameNular(GameString, "triDrawLines2d", TriDrawLines2d));
    GGameState.NewNularOp(GameNular(GameString, "triDrawAlphaBlend", TriDrawAlphaBlend));
    GGameState.NewNularOp(GameNular(GameString, "triDrawDepthTest", TriDrawDepthTest));
    GGameState.NewFunction(GameFunction(GameBool, "triClick", TriClick, GameScalar));
    GGameState.NewFunction(GameFunction(GameBool, "triClickAt", TriClickAt, GameArray));
    GGameState.NewFunction(GameFunction(GameBool, "triInvokeButton", TriInvokeButton, GameScalar));
    GGameState.NewFunction(GameFunction(GameBool, "triSeedSessions", TriSeedSessions, GameScalar));
    GGameState.NewFunction(GameFunction(GameBool, "triSeedMods", TriSeedMods, GameScalar));
    GGameState.NewFunction(GameFunction(GameBool, "triSeedWorkshopMods", TriSeedWorkshopMods, GameScalar));
    GGameState.NewFunction(GameFunction(GameBool, "triSortMods", TriSortMods, GameScalar));
    GGameState.NewNularOp(GameNular(GameScalar, "triModsVisibleCount", TriModsVisibleCount));
    GGameState.NewFunction(GameFunction(GameBool, "triModsSetFilter", TriModsSetFilter, GameString));
    GGameState.NewFunction(GameFunction(GameBool, "triModsRowClick", TriModsRowClick, GameArray));
    GGameState.NewFunction(GameFunction(GameBool, "triOpenModDownload", TriOpenModDownload, GameScalar));
    GGameState.NewFunction(GameFunction(GameBool, "triOpenJoinRequirements", TriOpenJoinRequirements, GameScalar));
    GGameState.NewFunction(
        GameFunction(GameString, "triAssertControlLineStarts", TriAssertControlLineStarts, GameArray));
    GGameState.NewFunction(GameFunction(GameString, "triAssertConfigClass", TriAssertConfigClass, GameString));
    GGameState.NewFunction(GameFunction(GameString, "triClickText", TriClickText, GameString));
    GGameState.NewFunction(GameFunction(GameBool, "triDblClick", TriDblClick, GameScalar));
    GGameState.NewFunction(GameFunction(GameBool, "triMouseDown", TriMouseDown, GameScalar));
    GGameState.NewFunction(GameFunction(GameBool, "triMouseDown", TriMouseDownArr, GameArray));
    GGameState.NewFunction(GameFunction(GameBool, "triMouseDragV", TriMouseDragV, GameScalar));
    GGameState.NewFunction(GameFunction(GameBool, "triMouseDragV", TriMouseDragV, GameArray));
    GGameState.NewNularOp(GameNular(GameBool, "triMouseUp", TriMouseUp));
    GGameState.NewFunction(GameFunction(GameBool, "triSelectList", TriSelectList, GameArray));
    GGameState.NewFunction(GameFunction(GameBool, "triSelectListByData", TriSelectListByData, GameArray));
    GGameState.NewFunction(GameFunction(GameScalar, "triListSel", TriListSel, GameScalar));
    GGameState.NewFunction(GameFunction(GameBool, "triSendKey", TriSendKey, GameScalar));
    GGameState.NewFunction(GameFunction(GameBool, "triSendKey", TriSendKeyArr, GameArray));
    GGameState.NewFunction(GameFunction(GameString, "triEndMission", TriEndMission, GameString));
    GGameState.NewNularOp(GameNular(GameString, "triStartRandomCutscene", TriStartRandomCutscene));
    GGameState.NewNularOp(GameNular(GameString, "triHornPlayerVehicle", TriHornPlayerVehicle));
    GGameState.NewNularOp(GameNular(GameString, "triRemount", TriRemount));
    GGameState.NewNularOp(GameNular(GameScalar, "triLoadedShapeCount", TriLoadedShapeCount));
    GGameState.NewNularOp(GameNular(GameScalar, "triIsEndForced", TriIsEndForced));
    GGameState.NewNularOp(GameNular(GameString, "triCheatStorePosition", TriCheatStorePosition));
    GGameState.NewNularOp(GameNular(GameString, "triCheatSaveGame", TriCheatSaveGame));
    GGameState.NewNularOp(GameNular(GameString, "triCheatLoadGame", TriCheatLoadGame));
    GGameState.NewNularOp(GameNular(GameString, "triCheatUnlockCampaign", TriCheatUnlockCampaign));
    GGameState.NewFunction(GameFunction(GameString, "triCheatSkipTime", TriCheatSkipTime, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triTeleportPlayerTo", TriTeleportPlayerTo, GameArray));
    GGameState.NewFunction(GameFunction(GameString, "triSetView", TriSetView, GameArray));
    GGameState.NewNularOp(GameNular(GameString, "triClearView", TriClearView));
    GGameState.NewFunction(GameFunction(GameString, "triRdcCapture", TriRdcCapture, GameString));
    GGameState.NewNularOp(GameNular(GameScalar, "triPlayerPosX", TriPlayerPosX));
    GGameState.NewNularOp(GameNular(GameScalar, "triPlayerPosY", TriPlayerPosY));
    GGameState.NewNularOp(GameNular(GameScalar, "triPlayerPosZ", TriPlayerPosZ));
    GGameState.NewFunction(GameFunction(GameString, "triCheatShowAllUnits", TriCheatShowAllUnits, GameBool));
    GGameState.NewFunction(GameFunction(GameString, "triCheatMapTeleport", TriCheatMapTeleport, GameBool));
    GGameState.NewNularOp(GameNular(GameScalar, "triViewDistance", TriViewDistance));
    GGameState.NewNularOp(GameNular(GameScalar, "triFps", TriFps));
    GGameState.NewNularOp(GameNular(GameScalar, "triMemoryMB", TriMemoryMB));
    GGameState.NewFunction(GameFunction(GameString, "triConsoleRun", TriConsoleRun, GameString));
    GGameState.NewFunction(
        GameFunction(GameString, "triSetPillarboxBarsEnabled", TriSetPillarboxBarsEnabled, GameBool));
    GGameState.NewNularOp(GameNular(GameScalar, "triPillarboxBarsEnabled", TriPillarboxBarsEnabled));
    GGameState.NewFunction(GameFunction(GameString, "triSetAspectOverride", TriSetAspectOverride, GameBool));
    GGameState.NewFunction(GameFunction(GameString, "triSetDisplayStyle", TriSetDisplayStyle, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triSetUltrawideClamp", TriSetUltrawideClamp, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triSetAspectPillarbox", TriSetAspectPillarbox, GameBool));
    GGameState.NewFunction(GameFunction(GameString, "triSetHudClamp", TriSetHudClamp, GameBool));
    GGameState.NewFunction(GameFunction(GameString, "triSetManualViewport", TriSetManualViewport, GameArray));
    GGameState.NewFunction(
        GameFunction(GameString, "triApplyPresentationViewport", TriApplyPresentationViewport, GameString));
    GGameState.NewNularOp(GameNular(GameString, "triGetWorldViewport", TriGetWorldViewport));
    GGameState.NewFunction(GameFunction(GameString, "triCheatWeather", TriCheatWeather, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triCheatTimeMultiplier", TriCheatTimeMultiplier, GameScalar));
    GGameState.NewNularOp(GameNular(GameScalar, "triTimeMultiplier", TriTimeMultiplier));
    GGameState.NewNularOp(GameNular(GameScalar, "triClockHour", TriClockHour));
    GGameState.NewNularOp(GameNular(GameScalar, "triLatchClockHour", TriLatchClockHour));
    GGameState.NewFunction(
        GameFunction(GameString, "triAssertClockHourDeltaSinceLatch", TriAssertClockHourDeltaSinceLatch, GameArray));
    GGameState.NewFunction(GameFunction(GameString, "triCheatGod", TriCheatGod, GameBool));
    GGameState.NewNularOp(GameNular(GameScalar, "triCheatGodActive", TriCheatGodActive));
    GGameState.NewFunction(GameFunction(GameString, "triCheatInfiniteAmmo", TriCheatInfiniteAmmo, GameBool));
    GGameState.NewNularOp(GameNular(GameScalar, "triCheatInfiniteAmmoActive", TriCheatInfiniteAmmoActive));
    GGameState.NewFunction(GameFunction(GameString, "triCheatInfiniteFuel", TriCheatInfiniteFuel, GameBool));
    GGameState.NewFunction(GameFunction(GameString, "triCheatInfiniteArmor", TriCheatInfiniteArmor, GameBool));
    GGameState.NewNularOp(GameNular(GameString, "triPlayerVehicle", TriPlayerVehicle));
    GGameState.NewNularOp(GameNular(GameString, "triPlayerName", TriPlayerName));
    GGameState.NewFunction(GameFunction(GameString, "triSetPlayerPref", TriSetPlayerPref, GameString));
    GGameState.NewFunction(GameFunction(GameString, "triAssertProfileMissing", TriAssertProfileMissing, GameString));
    GGameState.NewFunction(GameFunction(GameString, "triDamagePlayerVehicle", TriDamagePlayerVehicle, GameScalar));
    GGameState.NewNularOp(GameNular(GameScalar, "triPlayerVehicleDammage", TriPlayerVehicleDammage));
    GGameState.NewFunction(
        GameFunction(GameString, "triConsumePlayerVehicleFuel", TriConsumePlayerVehicleFuel, GameScalar));
    GGameState.NewNularOp(GameNular(GameScalar, "triPlayerVehicleFuel", TriPlayerVehicleFuel));
    GGameState.NewNularOp(GameNular(GameScalar, "triLatchPlayerVehicleFuel", TriLatchPlayerVehicleFuel));
    GGameState.NewNularOp(GameNular(GameScalar, "triPlayerCurrentMagazineAmmo", TriPlayerCurrentMagazineAmmo));
    GGameState.NewNularOp(GameNular(GameString, "triFirePlayerWeapon", TriFirePlayerWeapon));
    GGameState.NewNularOp(GameNular(GameScalar, "triLatchPlayerAmmo", TriLatchPlayerAmmo));
    GGameState.NewFunction(GameFunction(GameString, "triDoDammageToPlayer", TriDoDammageToPlayer, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triDamagePlayer", TriDamagePlayer, GameScalar));
    GGameState.NewNularOp(GameNular(GameScalar, "triPlayerDammage", TriPlayerDammage));
    GGameState.NewFunction(GameFunction(GameString, "triAssertCheatActive", TriAssertCheatActive, GameArray));
    GGameState.NewFunction(GameFunction(GameString, "triAssertCheatAvailable", TriAssertCheatAvailable, GameArray));
    GGameState.NewNularOp(GameNular(GameString, "triCheatEndMissionOutcomes", TriCheatEndMissionOutcomes));
    GGameState.NewFunction(GameFunction(GameBool, "triSendText", TriSendText, GameString));
    GGameState.NewFunction(GameFunction(GameBool, "triSendText", TriSendTextArr, GameArray));
    GGameState.NewFunction(GameFunction(GameBool, "triTypeText", TriTypeText, GameString));
    GGameState.NewFunction(GameFunction(GameBool, "triKeyDown", TriKeyDown, GameScalar));
    GGameState.NewFunction(GameFunction(GameBool, "triKeyRepeat", TriKeyRepeat, GameScalar));
    GGameState.NewFunction(GameFunction(GameBool, "triKeyUp", TriKeyUp, GameScalar));
    GGameState.NewFunction(GameFunction(GameBool, "triGpadButton", TriGpadButton, GameScalar));
    GGameState.NewFunction(GameFunction(GameBool, "triGpadPov", TriGpadPov, GameScalar));
    GGameState.NewFunction(GameFunction(GameBool, "triGpadLeft", TriGpadLeft, GameArray));
    GGameState.NewFunction(GameFunction(GameString, "triScreenshot", TriScreenshot, GameString));
    GGameState.NewFunction(GameFunction(GameString, "triShadowDepthProbe", TriShadowDepthProbe, GameScalar));
    GGameState.NewNularOp(GameNular(GameString, "triEnableShadowMaps", TriEnableShadowMaps));
    GGameState.NewFunction(GameFunction(GameString, "triShadowSceneDump", TriShadowSceneDump, GameString));
    GGameState.NewFunction(GameFunction(GameString, "triSetAlphaToCoverage", TriSetAlphaToCoverage, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triSetFlatShading", TriSetFlatShading, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triSetRenderScale", TriSetRenderScale, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triSetMsaa", TriSetMsaa, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triPerfStats", TriPerfStats, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triSetVsync", TriSetVsync, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triPerfDumpShapes", TriPerfDumpShapes, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triPerfReset", TriPerfReset, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triShadowSetDarkness", TriShadowSetDarkness, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triShadowSetBias", TriShadowSetBias, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triShadowSetCascades", TriShadowSetCascades, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triShadowSetDistance", TriShadowSetDistance, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triShadowSetSplit", TriShadowSetSplit, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triShadowSetOmni", TriShadowSetOmni, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triShadowSetOmniR0", TriShadowSetOmniR0, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triShadowSetOmniR1", TriShadowSetOmniR1, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triShadowSetFade", TriShadowSetFade, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triShadowSetRes", TriShadowSetRes, GameScalar));
    GGameState.NewNularOp(GameNular(GameString, "triShadowTuning", TriShadowTuning));
    GGameState.NewFunction(GameFunction(GameString, "triSetViewDistance", TriSetViewDistance, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triAssertVisibility", TriAssertVisibility, GameString));
    GGameState.NewNularOp(GameNular(GameString, "triDevPanelSelectShadows", TriDevPanelSelectShadows));
    GGameState.NewNularOp(GameNular(GameString, "triDevPanelSelectMemory", TriDevPanelSelectMemory));
    GGameState.NewNularOp(GameNular(GameString, "triShadowSunFactor", TriShadowSunFactor));
    GGameState.NewNularOp(GameNular(GameString, "triShadowProxyVerts", TriShadowProxyVerts));
    GGameState.NewFunction(GameFunction(GameString, "triWaitFrames", TriWaitFrames, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triSimFrames", TriSimFrames, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triSetSimTime", TriSetSimTime, GameScalar));
    GGameState.NewNularOp(GameNular(GameString, "triMissionPlayerReady", TriMissionPlayerReady));
    GGameState.NewNularOp(GameNular(GameString, "triActionMenuText", TriActionMenuText));
    GGameState.NewNularOp(GameNular(GameString, "triCommandMenuText", TriCommandMenuText));
    GGameState.NewNularOp(GameNular(GameString, "triInGameplay", TriInGameplay));
    GGameState.NewFunction(GameFunction(GameString, "triAssertDisplay", TriAssertDisplay, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triControlText", TriControlText, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triAssertControlLeftOf", TriAssertControlLeftOf, GameArray));
    GGameState.NewNularOp(GameNular(GameString, "triVisibleTexts", TriVisibleTexts));
    GGameState.NewNularOp(GameNular(GameString, "triActiveMods", TriActiveMods));
    GGameState.NewFunction(GameFunction(GameString, "triAssertTreeText", TriAssertTreeText, GameArray));
    GGameState.NewFunction(GameFunction(GameString, "triAssertListText", TriAssertListText, GameArray));
    GGameState.NewNularOp(GameNular(GameString, "triGetEndMode", TriGetEndMode));
    GGameState.NewFunction(GameFunction(GameBool, "triSetLanguage", TriSetLanguage, GameString));
    GGameState.NewNularOp(GameNular(GameString, "triVoiceLanguage", TriVoiceLanguage));
    GGameState.NewFunction(GameFunction(GameBool, "triSetVoiceLanguage", TriSetVoiceLanguage, GameString));
    GGameState.NewFunction(GameFunction(GameString, "triNetCommand", TriNetCommand, GameString));
    GGameState.NewFunction(GameFunction(GameString, "triCursorScroll", TriCursorScroll, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triCursorMove", TriCursorMove, GameArray));
    GGameState.NewFunction(GameFunction(GameString, "triCursorMoveControl", TriCursorMoveControl, GameScalar));
    GGameState.NewNularOp(GameNular(GameString, "triCursorPos", TriCursorPos));
    GGameState.NewNularOp(GameNular(GameString, "triCursorDrawRect", TriCursorDrawRect));
    GGameState.NewFunction(GameFunction(GameString, "triAssertCursorDrawRect", TriAssertCursorDrawRect, GameArray));
    GGameState.NewFunction(
        GameFunction(GameString, "triOpenDisabledChildDisplay", TriOpenDisabledChildDisplay, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triMouseLeft", TriMouseLeft, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triSamplePixel", TriSamplePixel, GameArray));
    GGameState.NewFunction(GameFunction(GameString, "triAssertRegionLit", TriAssertRegionLit, GameArray));
    GGameState.NewFunction(GameFunction(GameString, "triAssertPixelLit", TriAssertPixelLit, GameArray));
    GGameState.NewFunction(GameFunction(GameString, "triAssertTextFits", TriAssertTextFits, GameArray));
    GGameState.NewFunction(GameFunction(GameString, "triAssertCsvTextFits", TriAssertCsvTextFits, GameArray));
    GGameState.NewFunction(GameFunction(GameString, "triAssertPixelEquals", TriAssertPixelEquals, GameArray));
    GGameState.NewFunction(GameFunction(GameString, "triAssertPixelDarkerThan", TriAssertPixelDarkerThan, GameArray));
    GGameState.NewNularOp(GameNular(GameString, "triEnableShadows", TriEnableShadows));

    // Audio surface (Iteration 4 wiring tests)
    GGameState.NewFunction(GameFunction(GameString, "triGetVolume", TriGetVolume, GameString));
    GGameState.NewFunction(GameFunction(GameString, "triSetVolume", TriSetVolume, GameArray));
    GGameState.NewNularOp(GameNular(GameString, "triGetEAX", TriGetEAX));
    GGameState.NewNularOp(GameNular(GameScalar, "triCountSuppressedMusicWaves", TriCountSuppressedMusicWaves));
    GGameState.NewNularOp(GameNular(GameScalar, "triAudioActive3D", TriAudioActive3D));
    GGameState.NewNularOp(GameNular(GameScalar, "triAudioActive2D", TriAudioActive2D));
    GGameState.NewNularOp(GameNular(GameScalar, "triAudioPausedNow", TriAudioPausedNow));
    GGameState.NewNularOp(GameNular(GameScalar, "triAudioEvictedThisFrame", TriAudioEvictedThisFrame));
    GGameState.NewNularOp(GameNular(GameScalar, "triAudioPausedThisFrame", TriAudioPausedThisFrame));
    GGameState.NewNularOp(GameNular(GameScalar, "triAudioAllocFailures", TriAudioAllocFailures));
    GGameState.NewNularOp(GameNular(GameScalar, "triCategoryPreviewRestarts", TriCategoryPreviewRestarts));

    // Display screen probes
    GGameState.NewNularOp(GameNular(GameScalar, "triGetMonitor", TriGetMonitor));
    GGameState.NewNularOp(GameNular(GameString, "triGetWindowMode", TriGetWindowMode));
    GGameState.NewNularOp(GameNular(GameString, "triGetDesktopDisplayMode", TriGetDesktopDisplayMode));
    GGameState.NewNularOp(GameNular(GameString, "triGetCurrentDisplayMode", TriGetCurrentDisplayMode));
    GGameState.NewNularOp(GameNular(GameString, "triGetRequestedFullscreenMode", TriGetRequestedFullscreenMode));
    GGameState.NewNularOp(GameNular(GameString, "triGetAspectSettings", TriGetAspectSettings));
    GGameState.NewNularOp(GameNular(GameString, "triListMonitors", TriListMonitors));
    // Graphics screen probes
    GGameState.NewNularOp(GameNular(GameScalar, "triGetSwapInterval", TriGetSwapInterval));
    GGameState.NewNularOp(GameNular(GameScalar, "triGetObjectLodBias", TriGetObjectLodBias));
    GGameState.NewNularOp(GameNular(GameScalar, "triGetTerrainGrid", TriGetTerrainGrid));
    GGameState.NewNularOp(GameNular(GameScalar, "triGetViewDistance", TriGetViewDistance));

    GGameState.NewFunction(GameFunction(GameString, "triSetEAX", TriSetEAX, GameScalar));
    GGameState.NewNularOp(GameNular(GameString, "triGetOutputDevice", TriGetOutputDevice));
    GGameState.NewFunction(GameFunction(GameString, "triSwitchOutputDevice", TriSwitchOutputDevice, GameString));
    GGameState.NewNularOp(GameNular(GameString, "triListOutputDevices", TriListOutputDevices));
    GGameState.NewNularOp(GameNular(GameString, "triListInputDevices", TriListInputDevices));
    GGameState.NewNularOp(GameNular(GameString, "triStartPreview", TriStartPreview));
    GGameState.NewFunction(GameFunction(GameString, "triStartCategoryPreview", TriStartCategoryPreview, GameString));
    GGameState.NewNularOp(GameNular(GameString, "triAudioPageOutputCount", TriAudioPageOutputCount));
    GGameState.NewNularOp(GameNular(GameString, "triAudioPageInputCount", TriAudioPageInputCount));
    GGameState.NewFunction(GameFunction(GameString, "triPixelLatch", TriPixelLatch, GameArray));
    GGameState.NewFunction(GameFunction(GameString, "triAssertPixelChanged", TriAssertPixelChanged, GameArray));

    // Viewer-mode probes — "x,y,z" position readback for tri tests that
    // exercise the viewer's input adapter (see ViewerController unit
    // tests for the input mapping itself).
    GGameState.NewNularOp(GameNular(GameString, "triObjectPos", TriObjectPos));
    GGameState.NewNularOp(GameNular(GameString, "triCamPos", TriCamPos));
    GGameState.NewNularOp(GameNular(GameString, "triLatchCam", TriLatchCam));
    GGameState.NewNularOp(GameNular(GameString, "triCursorLocked", TriCursorLocked));
    GGameState.NewNularOp(GameNular(GameString, "triLatchObject", TriLatchObject));
    GGameState.NewNularOp(GameNular(GameScalar, "triAnimPhase", TriAnimPhase));
    GGameState.NewFunction(GameFunction(GameString, "triAssertCamMoved", TriAssertCamMoved, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triAssertObjectMoved", TriAssertObjectMoved, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triAssertObjectRotated", TriAssertObjectRotated, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triMouseRight", TriMouseRight, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triMouseMid", TriMouseMid, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triMouseDelta", TriMouseDelta, GameArray));
    GGameState.NewFunction(GameFunction(GameString, "triFontTune", TriFontTune, GameArray));
    GGameState.NewNularOp(GameNular(GameString, "triFontDump", TriFontDump));

    // Issue #11 — pause/unpause + radio-wave diagnostics
    GGameState.NewNularOp(GameNular(GameString, "triPauseGame", TriPauseGame));
    GGameState.NewNularOp(GameNular(GameString, "triUnpauseGame", TriUnpauseGame));
    GGameState.NewNularOp(GameNular(GameString, "triOpenMap", TriOpenMap));
    GGameState.NewFunction(GameFunction(GameString, "triShowMap", TriShowMap, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triMapSetScale", TriMapSetScale, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triShowVoiceOverlay", TriShowVoiceOverlay, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triClickBriefingLink", TriClickBriefingLink, GameString));
    GGameState.NewFunction(
        GameFunction(GameString, "triProbeClickBriefingLink", TriProbeClickBriefingLink, GameString));
    GGameState.NewNularOp(GameNular(GameString, "triBriefingSection", TriBriefingSection));
    GGameState.NewFunction(GameFunction(GameString, "triBriefingSwitch", TriBriefingSwitch, GameString));
    GGameState.NewFunction(GameFunction(GameString, "triBriefingLinkRoute", TriBriefingLinkRoute, GameString));
    GGameState.NewFunction(GameFunction(GameString, "triBriefingClickAt", TriBriefingClickAt, GameArray));
    GGameState.NewNularOp(GameNular(GameScalar, "triRadioWaveCount", TriRadioWaveCount));
    GGameState.NewNularOp(GameNular(GameString, "triRadioWaveStates", TriRadioWaveStates));
    GameValue TriSideChat(const GameState*, GameValuePar);
    GGameState.NewFunction(GameFunction(GameString, "triSideChat", TriSideChat, GameString));
    GGameState.NewFunction(GameFunction(GameString, "triMuteVoN", TriMuteVoN, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triIgnoreChat", TriIgnoreChat, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triSendVonTestTone", TriSendVonTestTone, GameArray));
    GGameState.NewFunction(
        GameFunction(GameString, "triAssertNetworkAssetExists", TriAssertNetworkAssetExists, GameArray));
    GGameState.NewFunction(GameFunction(GameString, "triMpAssignSelf", TriMpAssignSelf, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triMpAssignSelfSlot", TriMpAssignSelfSlot, GameString));
    GGameState.NewFunction(GameFunction(GameString, "triMpClientReady", TriMpClientReady, GameScalar));
    GGameState.NewFunction(GameFunction(GameScalar, "triRadioWaveOffset", TriRadioWaveOffset, GameString));

    // Pull GameStateExtTestGeneric.cpp and GameStateExtTestGetters.cpp into the
    // link graph; their own INIT_MODULEs register the actual verbs.
    EnsureGameStateExtTestGenericLinked();
    EnsureGameStateExtTestGettersLinked();
};
