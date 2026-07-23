#include <Evaluator/express.hpp>
using namespace Poseidon;
#include <Poseidon/World/World.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Game/GameLoop.hpp>
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
namespace Poseidon
{
void StartRandomCutscene(RString world);
} // namespace Poseidon
namespace Poseidon
{
extern unsigned GTriNetSoundsReceived;
} // namespace Poseidon
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

// Defined in GameStateExtTestRender.cpp (moved there with the screenshot/render
// commands); the pattern-draw commands below share the same output dir + counter.
std::string GetTriOutputDir();
int& TriSeqCounter();

// Helper: get the active display via a temporary UITestEngine
static ControlsContainer* GetActiveDisplayForSQF()
{
    UITestEngine tmp;
    return tmp.GetActiveDisplay();
}

namespace
{
ControlObjectContainer* FindHostForIdc(int idc);
}

static const char* EndModeName(EndMode mode)
{
    switch (mode)
    {
        case EMContinue:
            return "CONTINUE";
        case EMKilled:
            return "KILLED";
        case EMLoser:
            return "LOSER";
        case EMEnd1:
            return "END1";
        case EMEnd2:
            return "END2";
        case EMEnd3:
            return "END3";
        case EMEnd4:
            return "END4";
        case EMEnd5:
            return "END5";
        case EMEnd6:
            return "END6";
        default:
            return "UNKNOWN";
    }
}

// Helper: check if a visible control with given text exists.  IDC
// upper bound is wide enough to cover the OptionsShell modal range
// (9000+ namespace) without exhaustively scanning the full int space.
// Also walks listbox/combobox row text so mission lists, key tables,
// etc. are searchable.
static bool HasVisibleText(const char* text)
{
    auto* display = GetActiveDisplayForSQF();
    if (!display)
        return false;
    for (int idc = 0; idc < 10000; idc++)
    {
        IControl* ctrl = display->GetCtrl(idc);
        if (!ctrl || !ctrl->IsVisible())
            continue;
        std::string ctrlText = UITestEngine::GetControlText(ctrl);
        if (ctrlText == text)
            return true;
        if (auto* lbox = dynamic_cast<CListBoxContainer*>(ctrl))
        {
            const int n = lbox->GetSize();
            for (int i = 0; i < n; i++)
            {
                if (strcmp((const char*)lbox->GetText(i), text) == 0)
                    return true;
            }
        }
        if (auto* tbox = dynamic_cast<CToolBox*>(ctrl))
        {
            const int n = tbox->GetSize();
            for (int i = 0; i < n; i++)
            {
                if (strcmp((const char*)tbox->GetText(i), text) == 0)
                    return true;
            }
        }
    }
    return false;
}

static bool TreeContainsText(const CTreeItem* item, const char* text)
{
    if (!item)
        return false;
    if (!strcmp(item->text, text))
        return true;
    for (int i = 0; i < item->children.Size(); ++i)
    {
        if (TreeContainsText(item->children[i], text))
            return true;
    }
    return false;
}

/// triVersion — returns "tri/1" to confirm test commands are available.
GameValue TriVersion(const GameState* state)
{
    return GameValue("tri/1");
}

/// triGameMode — returns the current game mode as a number.
GameValue TriGameMode(const GameState* state)
{
    if (!GWorld)
        return GameValue(float(-1));
    return GameValue(float(static_cast<int>(GWorld->GetMode())));
}

/// triStartRandomCutscene — from a running mission, trigger the mission->menu
/// teardown transition (StartRandomCutscene on the current world): the same path
/// the death debrief's Abort runs (OptionsUIApp.cpp:963), tearing down the mission
/// world (SwitchLandscape) and rebuilding the menu cutscene. Lets the harness
/// drive the death->menu teardown without the intro/briefing UI choreography.
/// Returns "OK" or "FAIL:<reason>".
GameValue TriStartRandomCutscene(const GameState* /*state*/)
{
    if (!GWorld)
        return GameValue("FAIL:no_world");
    if (GWorld->GetMode() != GModeArcade)
        return GameValue("FAIL:not_in_mission");
    LOG_INFO(Core, "[tri] triStartRandomCutscene: world='{}'", Glob.header.worldname);
    StartRandomCutscene(Glob.header.worldname);
    return GameValue("OK");
}

/// triHornPlayerVehicle — honk the horn of the vehicle the player occupies (Car /
/// Motorcycle): fires its selected weapon, i.e. the horn. Used to drive the MP horn
/// replication test. Returns "OK" or "FAIL:<reason>".
GameValue TriHornPlayerVehicle(const GameState* /*state*/)
{
    if (!GWorld || !GWorld->GetRealPlayer() || !GWorld->GetRealPlayer()->Brain())
        return GameValue("FAIL:no_player");
    EntityAI* veh = GWorld->GetRealPlayer()->Brain()->GetVehicle();
    if (!veh)
        return GameValue("FAIL:no_vehicle");
    const char* vtype = veh->GetType() ? (const char*)veh->GetType()->GetName() : "?";
    if (veh == static_cast<EntityAI*>(GWorld->GetRealPlayer()))
    {
        LOG_INFO(Core, "[tri] triHornPlayerVehicle: on foot (vehicle='{}')", vtype);
        return GameValue("FAIL:on_foot");
    }
    int weapon = veh->SelectedWeapon();
    if (weapon < 0)
        weapon = 0;
    bool ok = veh->FireWeapon(weapon, nullptr);
    LOG_INFO(Core, "[tri] triHornPlayerVehicle: vehicle='{}' weapon={} fired={}", vtype, weapon, ok);
    if (!ok)
        return GameValue("FAIL:fire_rejected");
    return GameValue("OK");
}

/// triDisplay — returns IDD of the active display, or -1.
GameValue TriDisplay(const GameState* state)
{
    auto* display = GetActiveDisplayForSQF();
    return GameValue(float(display ? display->IDD() : -1));
}

/// triEditorMode — returns DisplayArcadeMap's current InsertMode, or -1 outside the editor.
GameValue TriEditorMode(const GameState* state)
{
    DisplayArcadeMap* editor = dynamic_cast<DisplayArcadeMap*>(GetActiveDisplayForSQF());
    return GameValue(float(editor ? editor->_mode : -1));
}

/// triAssertDisplay <idd> — assert the active display IDD.
GameValue TriAssertDisplay(const GameState* state, GameValuePar arg)
{
    const int expected = static_cast<int>(static_cast<GameScalarType>(arg));
    auto* display = GetActiveDisplayForSQF();
    const int actual = display ? display->IDD() : -1;
    if (actual == expected)
        return GameValue("OK");

    char buf[64];
    snprintf(buf, sizeof(buf), "FAIL:expected=%d,actual=%d", expected, actual);
    LOG_ERROR(Core, "[tri] triAssertDisplay {}", buf);
    return GameValue(buf);
}

/// triClick <idc> — click a control by IDC. Returns true on success.
GameValue TriClick(const GameState* state, GameValuePar arg)
{
    int idc = static_cast<int>(static_cast<GameScalarType>(arg));
    auto* display = GetActiveDisplayForSQF();
    if (!display)
    {
        LOG_ERROR(Core, "[tri] triClick: no active display");
        return GameValue(false);
    }
    // A transient channel/chat HUD can shadow a modal map child (e.g. the
    // server get-ready dialog).  If the topmost container doesn't own the
    // control, resolve the stack entry that does.
    if (!display->GetCtrl(idc))
    {
        if (auto* owner = UIActiveDisplay::FindContainerWithIdc(GWorld, idc))
            display = owner;
    }
    IControl* ctrl = display->GetCtrl(idc);
    if (!ctrl)
    {
        LOG_ERROR(Core, "[tri] triClick: IDC {} not found", idc);
        return GameValue(false);
    }
    auto* control = dynamic_cast<Control*>(ctrl);
    if (!control)
    {
        LOG_ERROR(Core, "[tri] triClick: IDC {} not a Control", idc);
        return GameValue(false);
    }
    float cx = control->X() + control->W() * 0.5f;
    float cy = control->Y() + control->H() * 0.5f;
    LOG_INFO(Core, "[tri] triClick IDC={} at ({},{})", idc, cx, cy);
    if (auto* host = FindHostForIdc(idc))
    {
        host->OnLButtonDown(cx, cy);
        host->OnLButtonUp(cx, cy);
    }
    else
    {
        control->OnLButtonDown(cx, cy);
        control->OnLButtonUp(cx, cy);
    }
    return GameValue(true);
}

// triClickAt [idc, u, v] — like triClick but presses at the local (u, v) point
// inside the control instead of its centre.  Lets a test click PAST an edit
// field's text (clicking beyond a short line drove CEdit::XToPos off the end).
GameValue TriClickAt(const GameState* /*state*/, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
        return GameValue(false);
    const GameArrayType& a = arg;
    if (a.Size() < 3)
        return GameValue(false);
    int idc = static_cast<int>(static_cast<GameScalarType>(a[0]));
    float u = static_cast<float>(static_cast<GameScalarType>(a[1]));
    float v = static_cast<float>(static_cast<GameScalarType>(a[2]));
    auto* display = GetActiveDisplayForSQF();
    if (!display)
        return GameValue(false);
    if (!display->GetCtrl(idc))
    {
        if (auto* owner = UIActiveDisplay::FindContainerWithIdc(GWorld, idc))
            display = owner;
    }
    auto* control = dynamic_cast<Control*>(display->GetCtrl(idc));
    if (!control)
        return GameValue(false);
    float x = control->X() + control->W() * u;
    float y = control->Y() + control->H() * v;
    LOG_INFO(Core, "[tri] triClickAt IDC={} at ({},{})", idc, x, y);
    control->OnLButtonDown(x, y);
    control->OnLButtonUp(x, y);
    return GameValue(true);
}

/// triInvokeButton <idc> — call the active display's OnButtonClicked(idc)
/// directly. Useful for hidden startup buttons that are wired in code but not
/// reachable via pointer clicks in a given harness state.
GameValue TriInvokeButton(const GameState* state, GameValuePar arg)
{
    int idc = static_cast<int>(static_cast<GameScalarType>(arg));
    auto* display = GetActiveDisplayForSQF();
    if (!display)
    {
        LOG_ERROR(Core, "[tri] triInvokeButton: no active display");
        return GameValue(false);
    }
    // The topmost container can be a transient channel/chat HUD that shadows a
    // modal map child (e.g. the server get-ready dialog).  If it doesn't own the
    // button, fall back to the stack entry that does.
    if (!display->GetCtrl(idc))
    {
        if (auto* owner = UIActiveDisplay::FindContainerWithIdc(GWorld, idc))
            display = owner;
    }
    LOG_INFO(Core, "[tri] triInvokeButton IDC={}", idc);
    display->OnButtonClicked(idc);
    return GameValue(true);
}

/// triSeedSessions <n> — inject n fake sessions into the active MP server
/// browser so its table (rows + sort) can be characterized in the harness,
/// which otherwise shows an empty list (sessions come only from the network).
GameValue TriSeedSessions(const GameState* state, GameValuePar arg)
{
    int n = static_cast<int>(static_cast<GameScalarType>(arg));
    DisplayMultiplayer* mp = dynamic_cast<DisplayMultiplayer*>(GetActiveDisplayForSQF());
    if (mp == nullptr)
    {
        LOG_ERROR(Core, "[tri] triSeedSessions: MP server browser is not the active display");
        return GameValue(false);
    }
    mp->SeedTestSessions(n);
    LOG_INFO(Core, "[tri] triSeedSessions: seeded {} sessions", n);
    return GameValue(true);
}

/// triSessionPing <row> - return the currently visible MP browser row ping.
GameValue TriSessionPing(const GameState* state, GameValuePar arg)
{
    int row = static_cast<int>(static_cast<GameScalarType>(arg));
    DisplayMultiplayer* mp = dynamic_cast<DisplayMultiplayer*>(GetActiveDisplayForSQF());
    if (mp == nullptr)
    {
        LOG_ERROR(Core, "[tri] triSessionPing: MP server browser is not the active display");
        return GameValue(static_cast<GameScalarType>(-1));
    }
    return GameValue(static_cast<GameScalarType>(mp->GetVisibleSessionPingForTest(row)));
}

/// triSeedMods <n> — inject n fake catalog rows into the active MODS table.
GameValue TriSeedMods(const GameState* state, GameValuePar arg)
{
    int n = static_cast<int>(static_cast<GameScalarType>(arg));
    DisplayMods* mods = dynamic_cast<DisplayMods*>(GetActiveDisplayForSQF());
    if (mods == nullptr)
    {
        LOG_ERROR(Core, "[tri] triSeedMods: MODS screen is not the active display");
        return GameValue(false);
    }
    mods->SeedTestMods(n);
    LOG_INFO(Core, "[tri] triSeedMods: seeded {} mods", n);
    return GameValue(true);
}

/// triSortMods <col> — sort the active MODS catalog table by column
/// (0=Name, 1=Version, 2=Size, 3=State), ascending. Drives CModsList::Sort
/// until the table grows clickable column headers.
GameValue TriSortMods(const GameState* state, GameValuePar arg)
{
    int col = static_cast<int>(static_cast<GameScalarType>(arg));
    DisplayMods* mods = dynamic_cast<DisplayMods*>(GetActiveDisplayForSQF());
    if (mods == nullptr)
    {
        LOG_ERROR(Core, "[tri] triSortMods: MODS screen is not the active display");
        return GameValue(false);
    }
    CModsList* list = dynamic_cast<CModsList*>(mods->GetCtrl(IDC_MODS_LIST));
    if (list == nullptr)
    {
        LOG_ERROR(Core, "[tri] triSortMods: catalog list not found");
        return GameValue(false);
    }
    list->Sort(static_cast<ModsSortColumn>(col), true);
    LOG_INFO(Core, "[tri] triSortMods: sorted by column {}", col);
    return GameValue(true);
}

/// triModsVisibleCount — number of rows currently shown in the MODS catalog list
/// (after the Source / name filters), or -1 if the MODS screen isn't active. Reads
/// the list's GetSize() (the filtered visible count), so it proves a filter narrows
/// the view without depending on which rows render.
GameValue TriModsVisibleCount(const GameState* /*state*/)
{
    DisplayMods* mods = dynamic_cast<DisplayMods*>(GetActiveDisplayForSQF());
    if (mods == nullptr)
        return GameValue(static_cast<float>(-1));
    CModsList* list = dynamic_cast<CModsList*>(mods->GetCtrl(IDC_MODS_LIST));
    if (list == nullptr)
        return GameValue(static_cast<float>(-1));
    return GameValue(static_cast<float>(list->GetSize()));
}

/// triModsSetFilter "text" — set the MODS name filter directly (bypassing the
/// Filter dialog) and refresh the Filter button label. Lets a test pin the name
/// filtering deterministically without driving the 3D edit field. Returns true.
GameValue TriModsSetFilter(const GameState* /*state*/, GameValuePar arg)
{
    GameStringType text = static_cast<GameStringType>(arg);
    DisplayMods* mods = dynamic_cast<DisplayMods*>(GetActiveDisplayForSQF());
    if (mods == nullptr)
        return GameValue(false);
    CModsList* list = dynamic_cast<CModsList*>(mods->GetCtrl(IDC_MODS_LIST));
    if (list == nullptr)
        return GameValue(false);
    list->SetNameFilter((const char*)text);
    mods->UpdateFilterButton();
    return GameValue(true);
}

/// triModsRowClick [row, u] — simulate a click on display row `row` at horizontal
/// fraction `u` (0 = left edge .. 1 = right edge). Drives CModsList::HandleRowClick,
/// which toggles the tick ONLY when `u` lands in the checkbox column. Regression
/// guard for "a click anywhere on the row toggled the checkbox". Returns true.
GameValue TriModsRowClick(const GameState* /*state*/, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
        return GameValue(false);
    const GameArrayType& a = arg;
    if (a.Size() < 2)
        return GameValue(false);
    int row = static_cast<int>(static_cast<GameScalarType>(a[0]));
    float u = static_cast<float>(static_cast<GameScalarType>(a[1]));
    DisplayMods* mods = dynamic_cast<DisplayMods*>(GetActiveDisplayForSQF());
    if (mods == nullptr)
        return GameValue(false);
    CModsList* list = dynamic_cast<CModsList*>(mods->GetCtrl(IDC_MODS_LIST));
    if (list == nullptr)
        return GameValue(false);
    // Mirror the real mouse order: button-down toggles the checkbox of the row
    // UNDER THE CURSOR (using the selection as it was BEFORE this click), then
    // button-up moves the selection. We do NOT pre-select `row`, so a regression
    // where the toggle follows the old selection (clicking between lines unticks
    // another line) is caught.
    list->ClickRowCheckbox(row, u);
    list->SetCurSel(row, false);
    return GameValue(true);
}

/// triAssertConfigClass "Parent/Child/..." — assert a (possibly nested) class
/// exists in the merged global config (Pars). Proves a mod's bin/config actually
/// merged into the game config — e.g. a CfgVehicles unit that survived the
/// deferred-merge. "OK" or "FAIL:missing config class '<path>'".
GameValue TriAssertConfigClass(const GameState* /*state*/, GameValuePar arg)
{
    std::string path((const char*)static_cast<GameStringType>(arg));
    auto lower = [](std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    };
    const Poseidon::ParamEntry* cur = nullptr;
    std::stringstream ss(path);
    std::string seg;
    bool first = true;
    bool ok = true;
    while (std::getline(ss, seg, '/'))
    {
        if (seg.empty())
            continue;
        const Poseidon::ParamEntry* next =
            first ? Pars.FindEntry(seg.c_str()) : (cur != nullptr ? cur->FindEntry(seg.c_str()) : nullptr);
        first = false;
        // FindEntry hands back a non-null sentinel for a missing entry, so verify the
        // hit is a class AND is actually named the segment (the sentinel is not).
        if (next == nullptr || !next->IsClass() || lower((const char*)next->GetName()) != lower(seg))
        {
            ok = false;
            break;
        }
        cur = next;
    }
    if (ok && cur != nullptr)
        return GameValue("OK");
    return GameValue(RString(("FAIL:missing config class '" + path + "'").c_str()));
}

/// triSeedWorkshopMods <n> — inject n fake REMOTE catalog entries into the active
/// MODS table via the same MergeWorkshopMods path the worker-thread fetch uses, so
/// the workshop UI (rows, Workshop source, merge with local) is testable without a
/// live master server. Returns true.
GameValue TriSeedWorkshopMods(const GameState* /*state*/, GameValuePar arg)
{
    int n = static_cast<int>(static_cast<GameScalarType>(arg));
    DisplayMods* mods = dynamic_cast<DisplayMods*>(GetActiveDisplayForSQF());
    if (mods == nullptr)
        return GameValue(false);
    std::vector<MasterServerServiceModCatalogEntry> catalog;
    for (int i = 0; i < n; i++)
    {
        MasterServerServiceModCatalogEntry e;
        e.modId = "wsmod" + std::to_string(i + 1);
        e.name = "Workshop Mod " + std::to_string(i + 1);
        e.version = "2.0";
        e.sizeBytes = static_cast<int64_t>(i + 1) * 5 * 1024 * 1024;
        e.downloadUrl = "test://" + e.modId + ".pbo"; // so Apply's download-gate has a URL to use
        catalog.push_back(e);
    }
    mods->MergeWorkshopMods(catalog);
    return GameValue(true);
}

/// triOpenModDownload <n> — open the download dialog (RscDisplayModDownload) as a
/// child of the current display with n synthetic tasks and a FAKE in-process
/// transport (no network/disk). Exercises the live dialog — two bars, speed/ETA,
/// completion — offline. Click idc 125 (Download) to start, then again (relabeled
/// "Continue") to dismiss on success. Returns true.
GameValue TriOpenModDownload(const GameState* /*state*/, GameValuePar arg)
{
    int n = static_cast<int>(static_cast<GameScalarType>(arg));
    if (n < 1)
        n = 1;
    ControlsContainer* display = GetActiveDisplayForSQF();
    if (display == nullptr)
        return GameValue(false);

    std::vector<DownloadTask> tasks;
    for (int i = 0; i < n; i++)
    {
        DownloadTask t;
        t.label = "@wsmod" + std::to_string(i + 1);
        t.url = "test://" + t.label;
        t.expectedBytes = static_cast<int64_t>(i + 1) * 4 * 1024 * 1024;
        tasks.push_back(std::move(t));
    }
    // Fake transport: stream the file in two halves in-process, no I/O.
    DownloadFileFn fake = [](const DownloadTask& task, const std::function<void(int64_t, int64_t)>& onBytes,
                             const std::function<bool()>& /*cancelled*/, std::string& /*error*/) -> bool
    {
        onBytes(task.expectedBytes / 2, task.expectedBytes);
        onBytes(task.expectedBytes, task.expectedBytes);
        return true;
    };
    display->CreateChild(new DisplayModDownload(display, std::move(tasks), std::move(fake)));
    return GameValue(true);
}

/// triOpenJoinRequirements <0|1> — open the join-requirements dialog
/// (RscDisplayJoinRequirements, idd 75) with a synthetic mod diff + password field as
/// a child of the current display. Exercises the live dialog offline; mode 0 shows
/// Download & Join, mode 1 shows Set up & Join. idc 1 = OK, idc 2 = Cancel.
/// Returns true.
GameValue TriOpenJoinRequirements(const GameState* /*state*/, GameValuePar arg)
{
    ControlsContainer* display = GetActiveDisplayForSQF();
    if (display == nullptr)
        return GameValue(false);
    const bool setupOnly = static_cast<int>(static_cast<GameScalarType>(arg)) != 0;
    const RString title = Format(LocalizeString("STR_DISP_MODS_JOIN_TITLE"), "Pristar's CSLA Server");
    std::string diff = (const char*)LocalizeString("STR_DISP_MODS_JOIN_REQUIRES");
    diff += "\n  [ok] @csla   ";
    diff += (const char*)LocalizeString("STR_DISP_MODS_JOIN_INSTALLED");
    if (!setupOnly)
    {
        diff += "\n  [dl] CSLA Sounds   ";
        diff += (const char*)LocalizeString("STR_DISP_MODS_JOIN_DOWNLOAD");
        diff += " 12 MB\n";
    }
    diff += (const char*)LocalizeString("STR_DISP_MODS_JOIN_DISABLED");
    diff += "\n  [x] @ffur1985\n";
    const RString okText = LocalizeString(setupOnly ? "STR_DISP_MODS_SETUP_JOIN" : "STR_DISP_MODS_DOWNLOAD_JOIN");
    display->CreateChild(new DisplayJoinRequirements(display, title, RString(diff.c_str()), "", okText));
    return GameValue(true);
}

/// triAssertControlLineStarts [idc, "prefix"] — assert some rendered line of the active
/// display's C3DStatic (idc) begins with prefix (leading spaces ignored). Proves a
/// multi-line static broke at the right place: an explicit '\n' makes the following
/// text START a line, whereas width-only wrapping leaves it mid-line. Returns "OK" or
/// "FAIL:..." (bare-call assert — the harness surfaces the FAIL).
GameValue TriAssertControlLineStarts(const GameState* /*state*/, GameValuePar arg)
{
    const GameArrayType& a = arg;
    if (a.Size() < 2)
        return GameValue(RString("FAIL:triAssertControlLineStarts needs [idc, prefix]"));
    const int idc = static_cast<int>(static_cast<GameScalarType>(a[0]));
    const std::string prefix = (const char*)static_cast<RString>(a[1]);
    ControlsContainer* display = GetActiveDisplayForSQF();
    if (display == nullptr)
        return GameValue(RString("FAIL:no active display"));
    C3DStatic* s = dynamic_cast<C3DStatic*>(display->GetCtrl(idc));
    if (s == nullptr)
        return GameValue(Format("FAIL:no C3DStatic with idc %d", idc));
    for (int i = 0; i < s->GetLineCount(); i++)
    {
        std::string line = (const char*)s->GetLine(i);
        std::size_t start = line.find_first_not_of(" \t\r\n");
        if (start != std::string::npos && line.compare(start, prefix.size(), prefix) == 0)
            return GameValue(RString("OK"));
    }
    return GameValue(Format("FAIL:no line starts with '%s' (%d lines)", prefix.c_str(), s->GetLineCount()));
}

/// triAssertControlLinesExclude [idc, "needle"] - assert no rendered line of the
/// active display's C3DStatic (idc) contains needle. Useful for catching explicit
/// line-break delimiters that split lines but still leak into the drawn substring.
GameValue TriAssertControlLinesExclude(const GameState* /*state*/, GameValuePar arg)
{
    const GameArrayType& a = arg;
    if (a.Size() < 2)
        return GameValue(RString("FAIL:triAssertControlLinesExclude needs [idc, needle]"));
    const int idc = static_cast<int>(static_cast<GameScalarType>(a[0]));
    std::string needle = (const char*)static_cast<RString>(a[1]);
    if (needle == "\\n")
        needle = "\n";
    else if (needle == "\\r")
        needle = "\r";
    ControlsContainer* display = GetActiveDisplayForSQF();
    if (display == nullptr)
        return GameValue(RString("FAIL:no active display"));
    C3DStatic* s = dynamic_cast<C3DStatic*>(display->GetCtrl(idc));
    if (s == nullptr)
        return GameValue(Format("FAIL:no C3DStatic with idc %d", idc));
    for (int i = 0; i < s->GetLineCount(); i++)
    {
        std::string line = (const char*)s->GetLine(i);
        if (line.find(needle) != std::string::npos)
            return GameValue(Format("FAIL:line %d contains excluded text", i));
    }
    return GameValue(RString("OK"));
}

/// triClickText "text" — find control by text content and click it. Returns true on success.
/// triClickText "needle" -> "OK" or "FAIL:not_found" / "FAIL:no_display".
///
/// Returns a string (rather than a bool) so the tri runner can apply
/// Capybara-style automatic synchronisation: if the text isn't on
/// screen yet, the runner re-tries every poll_interval until the
/// assertion timeout.  Tests that previously had to pad with
/// `triSimFrames N` after a navigation event can drop the wait —
/// the next click implicitly waits for its target.
GameValue TriClickText(const GameState* state, GameValuePar arg)
{
    GameStringType needle = static_cast<GameStringType>(arg);
    const char* needleStr = (const char*)needle;
    auto* display = GetActiveDisplayForSQF();
    if (!display)
        return GameValue("FAIL:no_display");
    // Scan up to 10000 so OptionsShell modal IDCs (9000+ namespace)
    // are reachable — same range HasVisibleText uses.  The previous
    // 2000 cap meant `triClickText "Cancel"` on a confirm modal
    // silently failed (no IDC < 2000 had that text), which used to
    // be invisible because the runner didn't retry on `false`; now
    // that failures auto-retry, the cap surfaces as a real fail.
    for (int idc = 0; idc < 10000; idc++)
    {
        IControl* ctrl = display->GetCtrl(idc);
        if (!ctrl || !ctrl->IsVisible())
            continue;
        std::string text = UITestEngine::GetControlText(ctrl);
        if (text.empty())
            continue;
        if (text == needleStr)
        {
            auto* control = dynamic_cast<Control*>(ctrl);
            if (!control)
                continue;
            float cx = control->X() + control->W() * 0.5f;
            float cy = control->Y() + control->H() * 0.5f;
            LOG_INFO(Core, "[tri] triClickText \"{}\" → IDC={} at ({},{})", needleStr, idc, cx, cy);
            control->OnLButtonDown(cx, cy);
            control->OnLButtonUp(cx, cy);
            return GameValue("OK");
        }
    }
    return GameValue("FAIL:not_found");
}

// ---- Mouse drag trio ------------------------------------------------------
//
// triMouseDown <idc>  — synthesise a left-button press over the given
// sub-control.  Sets InputSubsystem::IsMouseLeftDown() = true, marks
// the host ControlObjectContainer's `_indexL` so subsequent moves land
// in the right place, and forwards OnLButtonDown to the sub-control.
//
// triMouseDragV [v]   — array form: [u, v]  (or just a scalar v).
// Updates the held control's local click point to (u, v) in 0..1.
// Mirrors what `C3DActiveText::OnMouseHold` does for a real mouse
// during a drag — refreshes `_u` / `_v` so polled drag handlers see
// the new position.  Default u = 0.5 if only v is supplied.
//
// triMouseUp          — release.  Forwards OnLButtonUp, clears
// `_indexL`, and clears IsMouseLeftDown.
//
// Together these let SQF tests exercise drag-and-drop interactions
// (scrollbar thumbs, slider thumbs, list reordering) without touching
// the SDL pipeline.

namespace
{
// Find the first ControlObjectContainer in the active display that
// contains the given idc (recursively via GetCtrl).  Returns nullptr
// if no host carries that idc.  In the harness this is always the
// notebook at idc 105, but a generic search keeps the verbs portable.
ControlObjectContainer* FindHostForIdc(int idc)
{
    auto* display = GetActiveDisplayForSQF();
    auto* asCC = dynamic_cast<ControlsContainer*>(display);
    // If the topmost container doesn't own the IDC (a channel/chat HUD shadowing
    // a modal map child), resolve the stack entry that does.
    if (!asCC || !asCC->GetCtrl(idc))
    {
        if (auto* owner = UIActiveDisplay::FindContainerWithIdc(GWorld, idc))
            asCC = owner;
    }
    if (!asCC)
        return nullptr;
    // Try common harness host first.
    if (auto* host = dynamic_cast<ControlObjectContainer*>(asCC->GetCtrl(105)))
    {
        if (host->GetCtrl(idc))
            return host;
    }
    // Fallback: walk the display's direct controls and return the
    // first ControlObjectContainer that holds the idc.
    for (int probe = 100; probe < 200; ++probe)
    {
        if (auto* host = dynamic_cast<ControlObjectContainer*>(asCC->GetCtrl(probe)))
        {
            if (host->GetCtrl(idc))
                return host;
        }
    }
    return nullptr;
}
} // namespace

// Scalar form: triMouseDown <idc> — press at the control's centre.
// Array form:  triMouseDown [idc, u, v] — press at the local-UV point
// (u, v) inside the control.  Use the array form for widgets whose
// click-point matters: scrollbar carets vs thumb vs track, slider
// fills clicked at a specific x to set value, etc.  The (u, v) is
// pushed via DebugSetUV before forwarding OnLButtonDown so the
// control's GetU() / GetV() see it during the press handler.
static GameValue TriMouseDownImpl(int idc, bool haveUV, float u, float v)
{
    auto* host = FindHostForIdc(idc);
    if (!host)
    {
        LOG_ERROR(Core, "[tri] triMouseDown: no host for IDC {}", idc);
        return GameValue(false);
    }
    auto* ctrl = dynamic_cast<Control*>(host->GetCtrl(idc));
    if (!ctrl)
    {
        LOG_ERROR(Core, "[tri] triMouseDown: IDC {} not a Control", idc);
        return GameValue(false);
    }
    host->DebugSetLeftPressedIdc(idc);
    GInput.mouse.left = true;
    if (haveUV)
    {
        // Synthetic press at (u, v): record the desired click-point on
        // the 3D control so its OnLButtonDown / OnMouseHold see it via
        // GetU / GetV instead of the post-IsInside _u / _v.
        if (auto* c3d = dynamic_cast<Control3D*>(ctrl))
            c3d->DebugSetUV(u, v);
    }
    float cx = ctrl->X() + ctrl->W() * 0.5f;
    float cy = ctrl->Y() + ctrl->H() * 0.5f;
    ctrl->OnLButtonDown(cx, cy);
    if (haveUV)
        LOG_INFO(Core, "[tri] triMouseDown IDC={} at ({},{})", idc, u, v);
    else
        LOG_INFO(Core, "[tri] triMouseDown IDC={}", idc);
    return GameValue(true);
}

GameValue TriMouseDown(const GameState* /*state*/, GameValuePar arg)
{
    int idc = static_cast<int>(static_cast<GameScalarType>(arg));
    return TriMouseDownImpl(idc, false, 0.5f, 0.5f);
}

GameValue TriMouseDownArr(const GameState* /*state*/, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
        return GameValue(false);
    const GameArrayType& a = arg;
    if (a.Size() < 3)
        return GameValue(false);
    int idc = (int)(GameScalarType)a[0];
    float u = (float)(GameScalarType)a[1];
    float v = (float)(GameScalarType)a[2];
    if (u < 0.0f)
        u = 0.0f;
    if (u > 1.0f)
        u = 1.0f;
    if (v < 0.0f)
        v = 0.0f;
    if (v > 1.0f)
        v = 1.0f;
    return TriMouseDownImpl(idc, true, u, v);
}

GameValue TriMouseDragV(const GameState* state, GameValuePar arg)
{
    float u = 0.5f;
    float v = 0.5f;
    // Accept either a scalar (v only) or an array [u, v].
    if (arg.GetType() == GameArray)
    {
        const GameArrayType& a = arg;
        if (a.Size() >= 1)
            u = (float)(GameScalarType)a[0];
        if (a.Size() >= 2)
            v = (float)(GameScalarType)a[1];
    }
    else
    {
        v = (float)(GameScalarType)arg;
    }
    if (u < 0.0f)
        u = 0.0f;
    if (u > 1.0f)
        u = 1.0f;
    if (v < 0.0f)
        v = 0.0f;
    if (v > 1.0f)
        v = 1.0f;

    // The held control is whichever ControlObjectContainer reports a
    // pressed idc.  Walk the active display's known hosts, find the
    // one with a non-(-1) `GetLeftPressedIdc`, and update its held
    // control's _u / _v.
    auto* display = GetActiveDisplayForSQF();
    auto* asCC = dynamic_cast<ControlsContainer*>(display);
    if (!asCC)
    {
        LOG_ERROR(Core, "[tri] triMouseDragV: no display");
        return GameValue(false);
    }
    for (int probe = 100; probe < 200; ++probe)
    {
        auto* host = dynamic_cast<ControlObjectContainer*>(asCC->GetCtrl(probe));
        if (!host)
            continue;
        int pressed = host->GetLeftPressedIdc();
        if (pressed < 0)
            continue;
        auto* ctrl = dynamic_cast<Control3D*>(host->GetCtrl(pressed));
        if (!ctrl)
            continue;
        ctrl->DebugSetUV(u, v);
        // Forward an OnMouseHold so controls that act on hold (rather
        // than per-frame polling of _u/_v) update immediately.  Pixel
        // (x, y) doesn't matter here — the control reads GetU/GetV which
        // returns the debug values we just set.  Use the control's
        // centre as a stable placeholder so IsInside has a defined
        // (x, y) to project from.
        float cx = ctrl->X() + ctrl->W() * 0.5f;
        float cy = ctrl->Y() + ctrl->H() * 0.5f;
        ctrl->OnMouseHold(cx, cy);
        LOG_INFO(Core, "[tri] triMouseDragV pressed IDC={} -> u={} v={}", pressed, u, v);
        return GameValue(true);
    }
    LOG_ERROR(Core, "[tri] triMouseDragV: nothing held");
    return GameValue(false);
}

GameValue TriMouseUp(const GameState* state)
{
    auto* display = GetActiveDisplayForSQF();
    auto* asCC = dynamic_cast<ControlsContainer*>(display);
    if (!asCC)
    {
        LOG_ERROR(Core, "[tri] triMouseUp: no display");
        return GameValue(false);
    }
    for (int probe = 100; probe < 200; ++probe)
    {
        auto* host = dynamic_cast<ControlObjectContainer*>(asCC->GetCtrl(probe));
        if (!host)
            continue;
        int pressed = host->GetLeftPressedIdc();
        if (pressed < 0)
            continue;
        if (auto* ctrl = dynamic_cast<Control*>(host->GetCtrl(pressed)))
        {
            float cx = ctrl->X() + ctrl->W() * 0.5f;
            float cy = ctrl->Y() + ctrl->H() * 0.5f;
            ctrl->OnLButtonUp(cx, cy);
        }
        // Clear the synthetic _u/_v so a subsequent real-mouse drag
        // on the same control isn't haunted by stale debug values.
        if (auto* c3d = dynamic_cast<Control3D*>(host->GetCtrl(pressed)))
            c3d->DebugClearUV();
        host->DebugSetLeftPressedIdc(-1);
        GInput.mouse.left = false;
        LOG_INFO(Core, "[tri] triMouseUp released IDC={}", pressed);
        return GameValue(true);
    }
    GInput.mouse.left = false;
    return GameValue(true);
}

/// triDblClick <idc> — double-click a control by IDC. Returns true on success.
GameValue TriDblClick(const GameState* state, GameValuePar arg)
{
    int idc = static_cast<int>(static_cast<GameScalarType>(arg));
    auto* display = GetActiveDisplayForSQF();
    if (!display)
    {
        LOG_ERROR(Core, "[tri] triDblClick: no active display");
        return GameValue(false);
    }
    IControl* ctrl = display->GetCtrl(idc);
    if (!ctrl)
    {
        LOG_ERROR(Core, "[tri] triDblClick: IDC {} not found", idc);
        return GameValue(false);
    }
    auto* control = dynamic_cast<Control*>(ctrl);
    if (!control)
    {
        LOG_ERROR(Core, "[tri] triDblClick: IDC {} not a Control", idc);
        return GameValue(false);
    }
    float cx = control->X() + control->W() * 0.5f;
    float cy = control->Y() + control->H() * 0.5f;
    LOG_INFO(Core, "[tri] triDblClick IDC={} at ({},{})", idc, cx, cy);
    control->OnLButtonDblClick(cx, cy);
    return GameValue(true);
}

/// triSelectList [idc, index] — select an item in a listbox. Returns true on success.
GameValue TriSelectList(const GameState* state, GameValuePar arg)
{
    const GameArrayType& arr = arg;
    if (arr.Size() < 2)
    {
        LOG_ERROR(Core, "[tri] triSelectList: expected [idc, index]");
        return GameValue(false);
    }
    int idc = static_cast<int>(static_cast<GameScalarType>(arr[0]));
    int index = static_cast<int>(static_cast<GameScalarType>(arr[1]));
    auto* display = GetActiveDisplayForSQF();
    if (!display)
    {
        LOG_ERROR(Core, "[tri] triSelectList: no active display");
        return GameValue(false);
    }
    IControl* ctrl = display->GetCtrl(idc);
    if (!ctrl)
    {
        LOG_ERROR(Core, "[tri] triSelectList: IDC {} not found", idc);
        return GameValue(false);
    }
    if (auto* lb = dynamic_cast<CListBox*>(ctrl))
    {
        LOG_INFO(Core, "[tri] triSelectList IDC={} index={} (size={})", idc, index, lb->GetSize());
        if (index < 0 || index >= lb->GetSize())
        {
            return GameValue(false);
        }
        lb->SetCurSel(index);
        return GameValue(true);
    }
    if (auto* lb3d = dynamic_cast<C3DListBox*>(ctrl))
    {
        LOG_INFO(Core, "[tri] triSelectList IDC={} index={} (size={})", idc, index, lb3d->GetSize());
        if (index < 0 || index >= lb3d->GetSize())
        {
            return GameValue(false);
        }
        lb3d->SetCurSel(index);
        return GameValue(true);
    }
    LOG_ERROR(Core, "[tri] triSelectList: IDC {} not a listbox", idc);
    return GameValue(false);
}

static int ListBoxCurSel(IControl* ctrl)
{
    if (auto* lb = dynamic_cast<CListBox*>(ctrl))
        return lb->GetCurSel();
    if (auto* lb3d = dynamic_cast<C3DListBox*>(ctrl))
        return lb3d->GetCurSel();
    return -2; // not a listbox
}

/// triListSel <idc> — current selected row index of a listbox (-1 = none,
/// -2 = not found / not a listbox). Returns a scalar.
GameValue TriListSel(const GameState* /*state*/, GameValuePar arg)
{
    int idc = static_cast<int>(static_cast<GameScalarType>(arg));
    auto* display = GetActiveDisplayForSQF();
    if (!display)
        return GameValue(static_cast<float>(-2));
    return GameValue(static_cast<float>(ListBoxCurSel(display->GetCtrl(idc))));
}

/// triSelectListByData [idc, "substr"] — select the first C3DListBox row whose data
/// (for the session list, the guid "addr:port") contains substr. Returns true if found.
/// Lets a test pick a specific enumerated server (e.g. the 127.0.0.1 one) when LAN
/// broadcast surfaces the same host on several interfaces.
GameValue TriSelectListByData(const GameState* /*state*/, GameValuePar arg)
{
    const GameArrayType& arr = arg;
    if (arr.Size() < 2)
        return GameValue(false);
    const int idc = static_cast<int>(static_cast<GameScalarType>(arr[0]));
    const std::string needle = (const char*)static_cast<RString>(arr[1]);
    auto* display = GetActiveDisplayForSQF();
    if (!display)
        return GameValue(false);
    auto* lb = dynamic_cast<C3DListBox*>(display->GetCtrl(idc));
    if (!lb)
        return GameValue(false);
    for (int i = 0; i < lb->GetSize(); i++)
    {
        const std::string data = (const char*)lb->GetData(i);
        if (data.find(needle) != std::string::npos)
        {
            lb->SetCurSel(i);
            return GameValue(true);
        }
    }
    return GameValue(false);
}

static GameValue TriSendKeyImpl(int sc, int mods)
{
    LOG_INFO(Core, "[tri] triSendKey scancode=0x{:x} mods=0x{:x}", sc, mods);
    SDL_Event ev = {};
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.scancode = static_cast<SDL_Scancode>(sc);
    ev.key.key = SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(sc), static_cast<SDL_Keymod>(mods), false);
    ev.key.mod = static_cast<SDL_Keymod>(mods);
    ev.key.down = true;
    SDL_PushEvent(&ev);
    ev.type = SDL_EVENT_KEY_UP;
    ev.key.down = false;
    SDL_PushEvent(&ev);
    return GameValue(true);
}

/// triSendKey <scancode> — push an SDL key event (press+release). Returns true.
GameValue TriSendKey(const GameState* state, GameValuePar arg)
{
    int sc = static_cast<int>(static_cast<GameScalarType>(arg));
    return TriSendKeyImpl(sc, SDL_KMOD_NONE);
}

/// triSendKey [scancode, mods] — push an SDL key event with modifier mask.
/// Use SDL_KMOD_LCTRL (0x40), SDL_KMOD_LSHIFT (0x01), SDL_KMOD_LALT (0x100) etc.
GameValue TriSendKeyArr(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
        return GameValue(false);
    const GameArrayType& arr = arg;
    if (arr.Size() < 2)
        return GameValue(false);
    int sc = static_cast<int>(static_cast<GameScalarType>(arr[0]));
    int mods = static_cast<int>(static_cast<GameScalarType>(arr[1]));
    return TriSendKeyImpl(sc, mods);
}

static GameValue TriSendTextImpl(IControl* ctrl, const char* utf8)
{
    if (!ctrl)
        return GameValue(false);

    for (const char* p = utf8; p && *p;)
    {
        unsigned codepoint = 0xFFFD;
        int consumed = DecodeUtf8Codepoint(p, &codepoint);
        if (consumed <= 0)
            break;
        ctrl->OnChar(codepoint, 1, 0);
        p += consumed;
    }
    return GameValue(true);
}

/// triSendText "utf8" — send UTF-8 text to the focused control. Returns true.
GameValue TriSendText(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameString)
        return GameValue(false);

    auto* display = GetActiveDisplayForSQF();
    if (!display)
        return GameValue(false);

    GameStringType text = static_cast<GameStringType>(arg);
    const char* utf8 = (const char*)text;
    LOG_INFO(Core, "[tri] triSendText focused \"{}\"", utf8 ? utf8 : "");
    return TriSendTextImpl(display->GetFocused(), utf8);
}

/// triSendText [idc, "utf8"] — send UTF-8 text directly to a specific control.
GameValue TriSendTextArr(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
        return GameValue(false);
    const GameArrayType& arr = arg;
    if (arr.Size() < 2)
        return GameValue(false);

    auto* display = GetActiveDisplayForSQF();
    if (!display)
        return GameValue(false);

    int idc = static_cast<int>(static_cast<GameScalarType>(arr[0]));
    GameStringType text = static_cast<GameStringType>(arr[1]);
    const char* utf8 = (const char*)text;
    LOG_INFO(Core, "[tri] triSendText IDC={} \"{}\"", idc, utf8 ? utf8 : "");
    return TriSendTextImpl(display->GetCtrl(idc), utf8);
}

/// triTypeText "utf8" — type UTF-8 text through the real input dispatch
/// (World::DoChar), the same entry SDL's TEXT_INPUT events feed.  Unlike
/// triSendText (which calls OnChar directly on a control), this routes through
/// the focus + display char path a real keystroke takes — warning box, chat,
/// user dialog, the editor map and its child dialogs — so it reproduces
/// "field is focused but typed text never lands" routing bugs.
GameValue TriTypeText(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameString || !GWorld)
        return GameValue(false);

    GameStringType text = static_cast<GameStringType>(arg);
    const char* utf8 = (const char*)text;
    LOG_INFO(Core, "[tri] triTypeText \"{}\"", utf8 ? utf8 : "");
    for (const char* p = utf8; p && *p;)
    {
        unsigned codepoint = 0xFFFD;
        int consumed = DecodeUtf8Codepoint(p, &codepoint);
        if (consumed <= 0)
            break;
        GWorld->DoChar(codepoint, 1, 0);
        p += consumed;
    }
    return GameValue(true);
}

/// triKeyDown <scancode> — push an SDL KEY_DOWN event only (key stays held).
/// Use with triKeyUp to simulate holding a key across multiple frames.
GameValue TriKeyDown(const GameState* state, GameValuePar arg)
{
    int sc = static_cast<int>(static_cast<GameScalarType>(arg));
    LOG_INFO(Core, "[tri] triKeyDown scancode=0x{:x}", sc);
    SDL_Event ev = {};
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.scancode = static_cast<SDL_Scancode>(sc);
    ev.key.key = SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(sc), SDL_KMOD_NONE, false);
    ev.key.mod = SDL_KMOD_NONE;
    ev.key.down = true;
    SDL_PushEvent(&ev);
    return GameValue(true);
}

/// triKeyRepeat <scancode> — push an SDL KEY_DOWN repeat event.
GameValue TriKeyRepeat(const GameState* state, GameValuePar arg)
{
    int sc = static_cast<int>(static_cast<GameScalarType>(arg));
    LOG_INFO(Core, "[tri] triKeyRepeat scancode=0x{:x}", sc);
    SDL_Event ev = {};
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.scancode = static_cast<SDL_Scancode>(sc);
    ev.key.key = SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(sc), SDL_KMOD_NONE, false);
    ev.key.mod = SDL_KMOD_NONE;
    ev.key.down = true;
    ev.key.repeat = true;
    SDL_PushEvent(&ev);
    return GameValue(true);
}

/// triControls — returns array of [idc, "type", "text", visible] for all controls.
GameValue TriControls(const GameState* state)
{
    auto* display = GetActiveDisplayForSQF();
    if (!display)
        return GameValue();

    AutoArray<GameValue> result;
    for (int idc = -1; idc < 2000; idc++)
    {
        IControl* ctrl = display->GetCtrl(idc);
        if (!ctrl)
            continue;
        AutoArray<GameValue> entry;
        entry.Add(GameValue(float(ctrl->IDC())));
        entry.Add(GameValue(UITestEngine::GetControlTypeName(ctrl)));
        std::string text = UITestEngine::GetControlText(ctrl);
        entry.Add(GameValue(text.c_str()));
        entry.Add(GameValue(ctrl->IsVisible()));
        result.Add(GameValue(entry));
    }
    return GameValue(result);
}

/// triGetEndMode — returns the current mission end mode name.
GameValue TriGetEndMode(const GameState* /*state*/)
{
    if (!GWorld)
        return GameValue("NO_WORLD");
    return GameValue(EndModeName(GWorld->GetEndMode()));
}

static AbstractUI* GetInGameUIForTests()
{
    if (!GWorld)
        return nullptr;
    return GWorld->GetUI();
}

GameValue TriActionMenuText(const GameState* /*state*/)
{
    AbstractUI* ui = GetInGameUIForTests();
    if (!ui)
        return GameValue("FAIL:no_ingame_ui");
    return GameValue(ui->GetActionMenuTexts());
}

GameValue TriCommandMenuText(const GameState* /*state*/)
{
    AbstractUI* ui = GetInGameUIForTests();
    if (!ui)
        return GameValue("FAIL:no_ingame_ui");
    return GameValue(ui->GetCommandMenuTexts());
}

// "1" while the player is actively controlling a unit in a running mission, "0" in
// menus/briefing/Esc — the gate the Alt+F4 window-close honours.
GameValue TriInGameplay(const GameState* /*state*/)
{
    return GameValue(RString(GApp && GApp->IsInGameplay() ? "1" : "0"));
}

static Person* FindFirstBindablePlayerForTests()
{
    if (!GWorld)
        return nullptr;

    auto getPersonForEntity = [](Object* entity) -> Person*
    {
        if (!entity)
            return nullptr;
        if (Person* person = dyn_cast<Person>(entity))
        {
            if (person->Brain() && !person->IsDammageDestroyed())
                return person;
            return nullptr;
        }

        EntityAI* vehicle = dyn_cast<EntityAI>(entity);
        if (!vehicle)
            return nullptr;

        AIUnit* unit = vehicle->CommanderUnit();
        if (!unit)
            return nullptr;

        Person* person = unit->GetPerson();
        if (!person || !person->Brain() || person->IsDammageDestroyed())
            return nullptr;
        return person;
    };

    StaticArrayAuto<OLink<Person>> players;
    GWorld->ScanPlayers(players);
    for (int i = 0; i < players.Size(); ++i)
    {
        Person* person = players[i];
        if (!person || !person->Brain() || person->IsDammageDestroyed())
            continue;
        return person;
    }

    if (AIUnit* focus = GWorld->FocusOn())
    {
        Person* person = focus->GetPerson();
        if (person && person->Brain() && !person->IsDammageDestroyed())
            return person;
    }

    if (Person* person = getPersonForEntity(GWorld->CameraOn()))
        return person;

    auto tryVehicleList = [&](int count, auto getEntity) -> Person*
    {
        for (int i = 0; i < count; ++i)
        {
            if (Person* person = getPersonForEntity(getEntity(i)))
                return person;
        }
        return nullptr;
    };

    if (Person* person = tryVehicleList(GWorld->NVehicles(), [](int i) { return GWorld->GetVehicle(i); }))
        return person;
    return tryVehicleList(GWorld->NOutVehicles(), [](int i) { return GWorld->GetOutVehicle(i); });
}

GameValue TriMissionPlayerReady(const GameState* /*state*/)
{
    if (!GWorld)
        return GameValue("FAIL:no_world");

    auto* display = GetActiveDisplayForSQF();
    int idd = display ? display->IDD() : -1;

    // Intro-only missions (cutscenes) boot to DisplayIntro (IDD 47) and have no
    // player. Readiness for them means the intro display is up and a couple of
    // frames have rendered, so the harness can drive / screenshot the cutscene.
    if (idd == 47)
    {
        uint32_t introFrames = GEngine ? GEngine->GetFrameCounter() : 0;
        if (introFrames < 2)
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "FAIL:frames=%u,need>=2", introFrames);
            return GameValue(buf);
        }
        return GameValue("OK");
    }

    if (idd != 46)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "FAIL:display=%d,expected=46_or_47", idd);
        return GameValue(buf);
    }

    uint32_t frames = GEngine ? GEngine->GetFrameCounter() : 0;
    if (frames < 2)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "FAIL:frames=%u,need>=2", frames);
        return GameValue(buf);
    }

    Person* player = GWorld->GetRealPlayer();
    if (!player || !player->Brain())
    {
        Person* candidate = FindFirstBindablePlayerForTests();
        if (!candidate || !candidate->Brain())
        {
            return GameValue("FAIL:no_player_assigned:hint=add PLAYER COMMANDER to mission");
        }

        char reason[256];
        snprintf(reason, sizeof(reason),
                 "FAIL:player_not_active:candidate=%s:hint=mission boot should activate authored player",
                 (const char*)candidate->GetDebugName());
        return GameValue(reason);
    }

    EntityAI* vehicle = player->Brain()->GetVehicle();
    if (!vehicle)
        return GameValue("FAIL:no_vehicle");

    if (GWorld->PlayerOn() != player)
    {
        char reason[256];
        snprintf(reason, sizeof(reason),
                 "FAIL:player_not_switched:real=%s:active=%s:hint=mission boot should switch to authored player",
                 (const char*)player->GetDebugName(),
                 GWorld->PlayerOn() ? (const char*)GWorld->PlayerOn()->GetDebugName() : "<null>");
        return GameValue(reason);
    }

    return GameValue("OK");
}

/// triAssertMissionPlayable — assert a client is past MP briefing/map states and
/// actually controls a live unit in an advancing mission.
GameValue TriAssertMissionPlayable(const GameState* /*state*/)
{
    if (!GWorld)
        return GameValue("FAIL:no_world");

    auto* display = GetActiveDisplayForSQF();
    const int idd = display ? display->IDD() : -1;
    if (idd != 46)
    {
        char buf[96];
        snprintf(buf, sizeof(buf), "FAIL:display=%d,expected=46", idd);
        return GameValue(buf);
    }

    if (!GApp || !GApp->IsInGameplay())
    {
        char buf[96];
        snprintf(buf, sizeof(buf), "FAIL:not_in_gameplay,display=%d", idd);
        return GameValue(buf);
    }

    Person* player = GWorld->GetRealPlayer();
    if (!player || !player->Brain())
        return GameValue("FAIL:no_player");
    if (player->IsDammageDestroyed())
        return GameValue("FAIL:player_destroyed");
    if (GWorld->PlayerOn() != player)
        return GameValue("FAIL:player_not_active");
    if (!player->Brain()->GetVehicle())
        return GameValue("FAIL:no_vehicle");

    const int beforeTime = Glob.time.toInt();
    const uint32_t beforeFrame = GEngine ? GEngine->GetFrameCounter() : 0;
    for (int i = 0; i < 5; ++i)
        Poseidon::AppIdle();
    const int afterTime = Glob.time.toInt();
    const uint32_t afterFrame = GEngine ? GEngine->GetFrameCounter() : 0;

    if (afterFrame <= beforeFrame)
    {
        char buf[96];
        snprintf(buf, sizeof(buf), "FAIL:frames_not_advancing,before=%u,after=%u", beforeFrame, afterFrame);
        return GameValue(buf);
    }
    if (afterTime <= beforeTime)
    {
        char buf[96];
        snprintf(buf, sizeof(buf), "FAIL:sim_time_not_advancing,before=%d,after=%d", beforeTime, afterTime);
        return GameValue(buf);
    }

    return GameValue("OK");
}

/// triControlText <idc> — return the current text carried by a control, or ""
/// if the control does not exist / has no text-bearing type.
GameValue TriControlText(const GameState* state, GameValuePar arg)
{
    int idc = static_cast<int>(static_cast<GameScalarType>(arg));
    auto* display = GetActiveDisplayForSQF();
    if (!display)
        return GameValue("");
    IControl* ctrl = display->GetCtrl(idc);
    if (!ctrl)
        return GameValue("");
    std::string text = UITestEngine::GetControlText(ctrl);
    if (text.empty())
        text = UITestEngine::GetHtmlText(ctrl); // mission/overview HTML preview
    return GameValue(text.c_str());
}

/// triAssertControlLeftOf [innerIdc, anchorIdc, maxGap] — assert the inner control
/// sits immediately to the left of the anchor on the same row (|dY| within half the
/// anchor height) and no further than maxGap from its left edge. Used to verify the
/// injected Mods button is placed just left of Quit in a custom menu, not floating
/// into the centre or pushed off screen. Returns "OK" or "FAIL:..." with geometry.
GameValue TriAssertControlLeftOf(const GameState* /*state*/, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected [inner,anchor,maxGap]");
    const GameArrayType& a = arg;
    if (a.Size() < 3)
        return GameValue("FAIL:expected [inner,anchor,maxGap]");
    int innerIdc = static_cast<int>(static_cast<GameScalarType>(a[0]));
    int anchorIdc = static_cast<int>(static_cast<GameScalarType>(a[1]));
    float maxGap = static_cast<float>(static_cast<GameScalarType>(a[2]));

    auto* display = GetActiveDisplayForSQF();
    if (!display)
        return GameValue("FAIL:no active display");
    auto* inner = dynamic_cast<Control*>(display->GetCtrl(innerIdc));
    auto* anchor = dynamic_cast<Control*>(display->GetCtrl(anchorIdc));
    if (!inner)
        return GameValue("FAIL:inner not found");
    if (!anchor)
        return GameValue("FAIL:anchor not found");

    const float dy = inner->Y() - anchor->Y();
    const float rowTol = anchor->H() * 0.5f;
    const float gap = anchor->X() - (inner->X() + inner->W());
    char buf[192];
    if ((dy < 0 ? -dy : dy) > rowTol)
    {
        snprintf(buf, sizeof(buf), "FAIL:not on row innerY=%.4f anchorY=%.4f", inner->Y(), anchor->Y());
        return GameValue(buf);
    }
    if (inner->X() >= anchor->X())
    {
        snprintf(buf, sizeof(buf), "FAIL:not left innerX=%.4f anchorX=%.4f", inner->X(), anchor->X());
        return GameValue(buf);
    }
    if (gap > maxGap)
    {
        snprintf(buf, sizeof(buf), "FAIL:gap %.4f > max %.4f", gap, maxGap);
        return GameValue(buf);
    }
    return GameValue("OK");
}

/// triVisibleTexts — return all visible non-empty control texts on the active
/// display joined by '|'. Useful for probing live UI surfaces in tests.
GameValue TriVisibleTexts(const GameState* state)
{
    auto* display = GetActiveDisplayForSQF();
    if (!display)
        return GameValue("");

    std::string out;
    for (int idc = 0; idc < 10000; ++idc)
    {
        IControl* ctrl = display->GetCtrl(idc);
        if (!ctrl || !ctrl->IsVisible())
            continue;
        std::string text = UITestEngine::GetControlText(ctrl);
        if (text.empty())
            continue;
        if (!out.empty())
            out += "|";
        out += text;
    }
    return GameValue(out.c_str());
}

/// triMpSetupMessage - return DisplayMultiplayerSetup's immediate-mode wait text.
GameValue TriMpSetupMessage(const GameState* state)
{
    auto* display = GetActiveDisplayForSQF();
    auto* setup = dynamic_cast<DisplayMultiplayerSetup*>(display);
    if (!setup)
    {
        return GameValue("");
    }
    return GameValue((const char*)setup->GetMessageForTest());
}

/// triMpTransferOverlayShows - return how many times the current multiplayer
/// setup entered the mission-transfer overlay.
GameValue TriMpTransferOverlayShows(const GameState* /*state*/)
{
    for (ControlsContainer* display = GetActiveDisplayForSQF(); display; display = display->Parent())
    {
        auto* setup = dynamic_cast<DisplayMultiplayerSetup*>(display);
        if (setup)
        {
            return GameValue(static_cast<float>(setup->GetTransferOverlayShowsForTest()));
        }
    }
    return GameValue(-1.0f);
}

/// triMpTransferStats - return [receivedBytes,totalBytes] from the MP transfer model.
GameValue TriMpTransferStats(const GameState* state)
{
    int curBytes = 0;
    int totBytes = 0;
    GetNetworkManager().GetTransferStats(curBytes, totBytes);

    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;
    array.Add(GameValue(static_cast<float>(curBytes)));
    array.Add(GameValue(static_cast<float>(totBytes)));
    return value;
}

/// triActiveMods - return the active mod mount path list.
GameValue TriActiveMods(const GameState* /*state*/)
{
    return GameValue((const char*)Poseidon::ModSystem::GetModList());
}

/// triAssertTreeText [idc, "text"] — assert a tree control contains an item
/// with the given text anywhere in its hierarchy.
GameValue TriAssertTreeText(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected [idc,text] array");
    const GameArrayType& arr = arg;
    if (arr.Size() < 2)
        return GameValue("FAIL:expected [idc,text] array");

    int idc = static_cast<int>(static_cast<GameScalarType>(arr[0]));
    GameStringType expected = static_cast<GameStringType>(arr[1]);
    auto* display = GetActiveDisplayForSQF();
    if (!display)
        return GameValue("FAIL:no active display");
    IControl* ctrl = display->GetCtrl(idc);
    if (!ctrl)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "FAIL:IDC %d not found", idc);
        return GameValue(buf);
    }
    auto* tree = dynamic_cast<CTree*>(ctrl);
    if (!tree)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "FAIL:IDC %d not tree", idc);
        return GameValue(buf);
    }
    return GameValue(TreeContainsText(tree->GetRoot(), (const char*)expected) ? "OK" : "FAIL:text not found");
}

/// triAssertListText [idc, "text"] - assert a listbox contains an item with the
/// given text.
GameValue TriAssertListText(const GameState* /*state*/, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
    {
        return GameValue("FAIL:expected [idc,text] array");
    }
    const GameArrayType& arr = arg;
    if (arr.Size() < 2)
    {
        return GameValue("FAIL:expected [idc,text] array");
    }

    int idc = static_cast<int>(static_cast<GameScalarType>(arr[0]));
    GameStringType expected = static_cast<GameStringType>(arr[1]);
    auto* display = GetActiveDisplayForSQF();
    if (!display)
    {
        return GameValue("FAIL:no active display");
    }
    IControl* ctrl = display->GetCtrl(idc);
    if (!ctrl)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "FAIL:IDC %d not found", idc);
        return GameValue(buf);
    }

    auto failWithRows = [](const std::vector<std::string>& rows) -> GameValue
    {
        std::string out = "FAIL:text not found";
        if (!rows.empty())
        {
            out += ": ";
            for (size_t i = 0; i < rows.size(); ++i)
            {
                if (i > 0)
                {
                    out += "|";
                }
                out += rows[i];
            }
        }
        return GameValue(out.c_str());
    };

    if (auto* lb = dynamic_cast<CListBox*>(ctrl))
    {
        std::vector<std::string> rows;
        for (int i = 0; i < lb->GetSize(); ++i)
        {
            rows.emplace_back(lb->GetText(i));
            if (rows.back() == (const char*)expected)
            {
                return GameValue("OK");
            }
        }
        return failWithRows(rows);
    }
    if (auto* lb3d = dynamic_cast<C3DListBox*>(ctrl))
    {
        std::vector<std::string> rows;
        for (int i = 0; i < lb3d->GetSize(); ++i)
        {
            rows.emplace_back(lb3d->GetText(i));
            if (rows.back() == (const char*)expected)
            {
                return GameValue("OK");
            }
        }
        return failWithRows(rows);
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "FAIL:IDC %d not listbox", idc);
    return GameValue(buf);
}

/// triVoiceLanguage — return the currently selected voice language as a string
/// ("English", "Czech", …). Independent of GLanguage (text/UI language).
GameValue TriVoiceLanguage(const GameState* /*state*/)
{
    return GameValue(GetSelectedVoiceLanguage().c_str());
}

/// triSetVoiceLanguage "lang" — set the preferred voiceover language. Affects
/// the next say/playSound resolved through FindSound's "<base>.<voiceLang>.<ext>"
/// lookup; currently playing audio is unaffected. Always returns true.
GameValue TriSetVoiceLanguage(const GameState* /*state*/, GameValuePar arg)
{
    GameStringType lang = static_cast<GameStringType>(arg);
    const char* langStr = (const char*)lang;
    SetSelectedVoiceLanguage(langStr);
    LOG_INFO(Core, "[tri] triSetVoiceLanguage \"{}\"", langStr);
    return GameValue(true);
}

/// triSetLanguage "lang" — switch stringtable column and fire language-changed
/// callbacks. Returns true if the call succeeded (or was a no-op on same
/// language), false on empty/invalid input.
GameValue TriSetLanguage(const GameState* /*state*/, GameValuePar arg)
{
    GameStringType lang = static_cast<GameStringType>(arg);
    const char* langStr = (const char*)lang;
    bool ok = SetLanguage(RString(langStr));
    LOG_INFO(Core, "[tri] triSetLanguage \"{}\" -> {}", langStr, ok ? "OK" : "FAIL");
    return GameValue(ok);
}

/// triFrameCount — return total frames rendered by the engine.
GameValue TriFrameCount(const GameState* /*state*/)
{
    if (!GEngine)
        return GameValue(0.0f);
    return GameValue(static_cast<float>(GEngine->GetFrameCounter()));
}

/// triErrorCount — return the running count of LOG_ERROR-level messages
/// emitted since process start.  Tests typically call this before and
/// after the action under test and assert the delta is zero.  Pairs with
/// triResetErrorCount when an earlier-than-test boot warning would
/// otherwise inflate the baseline.
GameValue TriErrorCount(const GameState* /*state*/)
{
    return GameValue(static_cast<float>(LoggingSystem::GetErrorCount()));
}

/// triResetErrorCount — clear the running LOG_ERROR counter.  Use right
/// before the action under test so subsequent triErrorCount reflects
/// only that action's emissions.
GameValue TriResetErrorCount(const GameState* /*state*/)
{
    LoggingSystem::ResetErrorCount();
    return GameValue("OK");
}

/// triWaitFrames <n> — pump N render frames (clear + present).
/// Keeps the window alive and advances the GPU pipeline without
/// re-entering the script system. Returns "OK:<n>".
GameValue TriWaitFrames(const GameState* /*state*/, GameValuePar arg)
{
    int n = static_cast<int>(static_cast<GameScalarType>(arg));
    if (n < 1)
        n = 1;
    if (n > 600)
        n = 600;

    // The render-only pump bypasses World::Simulate where Glob.uiTime normally
    // ticks, so UI animations would freeze. Advance it manually at a synthetic
    // 60 fps cadence.
    const float kSyntheticFrameDeltaSec = 1.0f / 60.0f;

    for (int i = 0; i < n; i++)
    {
        Poseidon::ProcessMessagesNoWait();
        Glob.uiTime += kSyntheticFrameDeltaSec;
        if (GEngine)
        {
            GEngine->InitDraw(true, PackedColor(0));
            GEngine->FinishDraw();
            GEngine->NextFrame();
        }
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "OK:%d", n);
    LOG_INFO(Core, "[tri] triWaitFrames: pumped {} frames", n);
    return GameValue(buf);
}

/// triSimFrames <n> — actually run N AppIdle ticks (simulate + render).
/// Unlike triWaitFrames (render-only), this drives Display::OnSimulate
/// so per-frame poll handlers (mouse hover focus, scrollbar drag,
/// VU meter, marquee) actually advance.  Use whenever a test expects
/// engine state to change between steps.
GameValue TriSimFrames(const GameState* /*state*/, GameValuePar arg)
{
    int n = static_cast<int>(static_cast<GameScalarType>(arg));
    if (n < 1)
        n = 1;
    if (n > 600)
        n = 600;
    for (int i = 0; i < n; ++i)
        Poseidon::AppIdle();
    char buf[32];
    snprintf(buf, sizeof(buf), "OK:%d", n);
    LOG_INFO(Core, "[tri] triSimFrames: simulated {} frames", n);
    return GameValue(buf);
}

/// triSetSimTime <seconds> — set Glob.time to an exact value (in seconds).
/// Makes time-dependent visuals (water waves, sky dome) deterministic
/// across runs. Use after setAccTime 0 to normalize simulation state.
/// Returns "OK:<ms>" with the millisecond value set.
GameValue TriSetSimTime(const GameState* /*state*/, GameValuePar arg)
{
    float seconds = static_cast<float>(static_cast<GameScalarType>(arg));
    int ms = static_cast<int>(seconds * 1000.0f);
    Glob.time = Time(ms);
    char buf[64];
    snprintf(buf, sizeof(buf), "OK:%d", ms);
    LOG_INFO(Core, "[tri] triSetSimTime: Glob.time set to {} ms ({:.3f} s)", ms, seconds);
    return GameValue(buf);
}

/// triSetBrightness <value> — set the engine user-brightness (eye
/// accommodation). 1.0 = neutral. Returns "OK:<value>".
GameValue TriSetBrightness(const GameState* /*state*/, GameValuePar arg)
{
    float value = static_cast<float>(static_cast<GameScalarType>(arg));
    if (GEngine)
        GEngine->SetBrightness(value);
    char buf[64];
    snprintf(buf, sizeof(buf), "OK:%.3f", value);
    LOG_INFO(Core, "[tri] triSetBrightness: user brightness set to {:.3f}", value);
    return GameValue(buf);
}

/// triSceneReady — check if the 3D scene is rendering.
/// Returns "OK" if the mission display (IDD 46) is active and at least
/// one frame has been rendered. Returns "FAIL:reason" otherwise.
/// The Trident runner can retry this command until it returns "OK".
GameValue TriSceneReady(const GameState* /*state*/)
{
    auto* display = GetActiveDisplayForSQF();
    int idd = display ? display->IDD() : -1;
    // IDD 46 = DisplayMission, IDD 47 = DisplayIntro (cutscene) — both render the
    // 3D scene, so either counts as the scene being ready.
    if (idd != 46 && idd != 47)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "FAIL:display=%d,expected=46_or_47", idd);
        return GameValue(buf);
    }

    uint32_t frames = GEngine ? GEngine->GetFrameCounter() : 0;
    if (frames < 2)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "FAIL:frames=%u,need>=2", frames);
        return GameValue(buf);
    }

    return GameValue("OK");
}

/// triDrawColorbar — draw 5 colored vertical bars and capture screenshot. Returns "OK".
/// Performs a complete frame cycle: queue screenshot → InitDraw → draw → FinishDraw → NextFrame.
/// Screenshot capture happens in NextFrame (before Present), guaranteeing the test pattern is captured.
GameValue TriDrawColorbar(const GameState* /*state*/)
{
    if (!GEngine)
        return GameValue("FAIL:no engine");

    auto dir = GetTriOutputDir();
    std::filesystem::create_directories(dir);
    int n = TriSeqCounter()++;
    char filename[512];
    snprintf(filename, sizeof(filename), "%s/%03d_colorbar", dir.c_str(), n);
    GEngine->Screenshot(filename);

    GEngine->InitDraw(true);
    GEngine->DrawTestPattern("colorbar");
    GEngine->FinishDraw();
    GEngine->NextFrame();

    LOG_INFO(Core, "[tri] triDrawColorbar: drawn + captured → {}", filename);
    return GameValue("OK");
}

/// triAssertColorbarBytes — I-31 / B-027 in-process pixel readback.
///
/// Draws the colorbar test pattern (5 vertical bars with known
/// PackedColor values) and samples the centre of each bar via
/// `glReadPixels` BEFORE the buffer swap.  This avoids the
/// double-buffer race the screenshot-style triDrawColorbar verb
/// has (where the just-drawn frame may get overwritten by the
/// next menu frame before tri can sample it).
///
/// Expected RGB per bar (matches DrawTestPattern's `colors[]`):
///   bar 0 — 255,   0,   0   (red)
///   bar 1 —   0, 255,   0   (green)
///   bar 2 —   0,   0, 255   (blue)
///   bar 3 — 255, 255,   0   (yellow)
///   bar 4 — 255,   0, 255   (magenta)
///
/// A GL_RGBA-vs-GL_BGRA regression in the vertex attribute pointer
/// produces a 255-channel R/B swap on bars 0, 2, 3 (bar 4 = R+B
/// is self-symmetric so it would alone be silent).  Returns "OK"
/// or "FAIL:bar=N got=R,G,B exp=R,G,B".
GameValue TriAssertColorbarBytes(const GameState* /*state*/)
{
    if (!GEngine)
        return GameValue("FAIL:no_engine");

    GEngine->InitDraw(true);
    GEngine->DrawTestPattern("colorbar");
    GEngine->FinishDraw();

    const int w = GEngine->Width();
    const int h = GEngine->Height();
    const uint8_t expected[5][3] = {
        {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 0}, {255, 0, 255},
    };
    const int tol = 8;

    int failingBar = -1;
    uint8_t gotRGB[3] = {0, 0, 0};
    for (int i = 0; i < 5; ++i)
    {
        // Bar i centre in pixel space; y = window mid-height.
        const int u_px = static_cast<int>((i + 0.5f) * (w / 5.0f));
        const int v_px = h / 2;
        uint8_t rgb[3] = {0, 0, 0};
        if (!GEngine->SamplePixel(u_px, v_px, rgb))
        {
            failingBar = i;
            break;
        }
        const int dr = std::abs(static_cast<int>(rgb[0]) - expected[i][0]);
        const int dg = std::abs(static_cast<int>(rgb[1]) - expected[i][1]);
        const int db = std::abs(static_cast<int>(rgb[2]) - expected[i][2]);
        if (dr > tol || dg > tol || db > tol)
        {
            failingBar = i;
            gotRGB[0] = rgb[0];
            gotRGB[1] = rgb[1];
            gotRGB[2] = rgb[2];
            break;
        }
        LOG_INFO(Core, "[tri] triAssertColorbarBytes bar {} OK rgb=({},{},{}) exp=({},{},{})", i, rgb[0], rgb[1],
                 rgb[2], expected[i][0], expected[i][1], expected[i][2]);
    }

    GEngine->NextFrame();

    if (failingBar < 0)
        return GameValue("OK");

    char buf[160];
    snprintf(buf, sizeof(buf), "FAIL:bar=%d got=%d,%d,%d exp=%d,%d,%d tol=%d", failingBar, gotRGB[0], gotRGB[1],
             gotRGB[2], expected[failingBar][0], expected[failingBar][1], expected[failingBar][2], tol);
    LOG_ERROR(Core, "[tri] triAssertColorbarBytes: {}", buf);
    return GameValue(buf);
}

/// triDrawGradient3d — draw 4-corner color gradient and capture screenshot. Returns "OK".
/// Same frame cycle as triDrawColorbar.
GameValue TriDrawGradient3d(const GameState* /*state*/)
{
    if (!GEngine)
        return GameValue("FAIL:no engine");

    auto dir = GetTriOutputDir();
    std::filesystem::create_directories(dir);
    int n = TriSeqCounter()++;
    char filename[512];
    snprintf(filename, sizeof(filename), "%s/%03d_gradient3d", dir.c_str(), n);
    GEngine->Screenshot(filename);

    GEngine->InitDraw(true);
    GEngine->DrawTestPattern("gradient3d");
    GEngine->FinishDraw();
    GEngine->NextFrame();

    LOG_INFO(Core, "[tri] triDrawGradient3d: drawn + captured → {}", filename);
    return GameValue("OK");
}

/// Helper: draw named test pattern with frame cycle + screenshot.
static GameValue DrawPatternHelper(const char* pattern, const char* label)
{
    if (!GEngine)
        return GameValue("FAIL:no engine");

    auto dir = GetTriOutputDir();
    std::filesystem::create_directories(dir);
    int n = TriSeqCounter()++;
    char filename[512];
    snprintf(filename, sizeof(filename), "%s/%03d_%s", dir.c_str(), n, label);
    GEngine->Screenshot(filename);

    GEngine->InitDraw(true);
    GEngine->DrawTestPattern(pattern);
    GEngine->FinishDraw();
    GEngine->NextFrame();

    LOG_INFO(Core, "[tri] triDraw{}: drawn + captured → {}", label, filename);
    return GameValue("OK");
}

GameValue TriDrawClearBlue(const GameState* /*state*/)
{
    return DrawPatternHelper("clear_blue", "clear_blue");
}
GameValue TriDrawClearMagenta(const GameState* /*state*/)
{
    return DrawPatternHelper("clear_magenta", "clear_magenta");
}
GameValue TriDrawQuad2d(const GameState* /*state*/)
{
    return DrawPatternHelper("quad2d", "quad2d");
}
GameValue TriDrawLines2d(const GameState* /*state*/)
{
    return DrawPatternHelper("lines2d", "lines2d");
}
GameValue TriDrawAlphaBlend(const GameState* /*state*/)
{
    return DrawPatternHelper("alpha_blend", "alpha_blend");
}
GameValue TriDrawDepthTest(const GameState* /*state*/)
{
    return DrawPatternHelper("depth_test", "depth_test");
}

/// triSendAltEnter — push SDL Alt+Enter event to trigger fullscreen toggle. Returns true.
GameValue TriSendAltEnter(const GameState* /*state*/)
{
    LOG_INFO(Core, "[tri] triSendAltEnter");
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_RETURN;
    event.key.mod = SDL_KMOD_LALT;
    event.key.repeat = false;
    SDL_PushEvent(&event);
    return GameValue(true);
}

/// triIsWindowed — return "OK:windowed" or "OK:fullscreen".
GameValue TriIsWindowed(const GameState* /*state*/)
{
    if (!GEngine)
        return GameValue("FAIL:no engine");
    return GameValue(GEngine->IsWindowed() ? "OK:windowed" : "OK:fullscreen");
}

/// triGetWindowSize — return the engine's current render size as "WxH".
GameValue TriGetWindowSize(const GameState* /*state*/)
{
    if (!GEngine)
        return GameValue("FAIL:no engine");
    char buf[64];
    snprintf(buf, sizeof(buf), "%dx%d", GEngine->Width(), GEngine->Height());
    return GameValue(buf);
}

/// triAssertWindowedSmallerThanDesktop — assert the engine is windowed AND the
/// window is strictly smaller than the desktop on both axes (a real window with the
/// desktop around it, not a screen-filling one).  "OK" / "FAIL:...".
GameValue TriAssertWindowedSmallerThanDesktop(const GameState* /*state*/)
{
    if (!GEngine)
        return GameValue("FAIL:no engine");
    if (!GEngine->IsWindowed())
        return GameValue("FAIL:not_windowed");
    int dw = 0, dh = 0, dr = 0;
    if (!GEngine->GetDesktopDisplayMode(dw, dh, dr) || dw <= 0 || dh <= 0)
        return GameValue("FAIL:no_desktop_mode");
    const int w = GEngine->Width();
    const int h = GEngine->Height();
    if (w < dw && h < dh)
        return GameValue("OK");
    char buf[128];
    snprintf(buf, sizeof(buf), "FAIL:%dx%d not smaller than desktop %dx%d", w, h, dw, dh);
    return GameValue(buf);
}

namespace
{
// "Sky:3,WorldOpaque:55,ScreenSpace:8" for the last observed frame.
std::string FormatFrameShape()
{
    std::string out;
    for (const auto& p : Poseidon::render::frame::LastObservedFrameShape())
    {
        if (!out.empty())
            out += ',';
        out += Poseidon::render::frame::FramePassKindName(p.kind);
        out += ':';
        out += std::to_string(p.draws);
    }
    return out;
}
} // namespace

/// triGetFrameShape -> "Sky:3,WorldOpaque:55,ScreenSpace:8" — pass kinds
/// and draw counts of the most recently observed frame, in emission order.
GameValue TriGetFrameShape(const GameState* /*state*/)
{
    const std::string shape = FormatFrameShape();
    if (shape.empty())
        return GameValue("FAIL:no_frame_observed");
    return GameValue(shape.c_str());
}

/// triAssertFrameShape ["Sky","WorldOpaque","ScreenSpace"] — assert the
/// named passes are all present in the last observed frame, each with at
/// least one draw, in the given relative order; and that the whole
/// observed sequence is in canonical pass order.  Returns "OK" or
/// "FAIL:<reason>" with the actual shape in the log.
GameValue TriAssertFrameShape(const GameState* /*state*/, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected_array");
    const GameArrayType& a = arg;

    const auto& shape = Poseidon::render::frame::LastObservedFrameShape();
    if (shape.empty())
    {
        LOG_ERROR(Core, "[tri] triAssertFrameShape: FAIL — no frame observed yet");
        return GameValue("FAIL:no_frame_observed");
    }

    // Canonical order: BuildFrame emits each pass kind at most once, in
    // enum order — observed kinds must be strictly increasing.
    for (size_t i = 1; i < shape.size(); ++i)
    {
        if (static_cast<int>(shape[i].kind) <= static_cast<int>(shape[i - 1].kind))
        {
            LOG_ERROR(Core, "[tri] triAssertFrameShape: FAIL — pass order not canonical, shape={}",
                      FormatFrameShape().c_str());
            return GameValue("FAIL:order_not_canonical");
        }
    }

    // Expected passes present, with draws, in the given relative order.
    size_t cursor = 0;
    for (int i = 0; i < a.Size(); ++i)
    {
        const RString want = (GameStringType)a[i];
        bool found = false;
        while (cursor < shape.size())
        {
            const char* name = Poseidon::render::frame::FramePassKindName(shape[cursor].kind);
            const int draws = shape[cursor].draws;
            ++cursor;
            if (strcmp(name, (const char*)want) == 0)
            {
                if (draws <= 0)
                {
                    LOG_ERROR(Core, "[tri] triAssertFrameShape: FAIL — pass {} has no draws, shape={}",
                              (const char*)want, FormatFrameShape().c_str());
                    return GameValue("FAIL:pass_empty");
                }
                found = true;
                break;
            }
        }
        if (!found)
        {
            LOG_ERROR(Core, "[tri] triAssertFrameShape: FAIL — pass {} missing (or out of order), shape={}",
                      (const char*)want, FormatFrameShape().c_str());
            return GameValue("FAIL:pass_missing");
        }
    }

    LOG_INFO(Core, "[tri] triAssertFrameShape OK shape={}", FormatFrameShape().c_str());
    return GameValue("OK");
}

/// triResetGLErrorBaseline — capture the current HIGH-severity GL error
/// counter as the baseline for subsequent `triAssertNoGLErrors` checks.
/// Call once at the start of a test that wants to assert "no NEW errors
/// during this section" — driver-level noise from earlier frames is
/// excluded from the assertion window.  Returns "OK"; the snapshot
/// count is logged.
namespace
{
unsigned int& TriGlErrorBaseline()
{
    static unsigned int s = 0;
    return s;
}
} // namespace

GameValue TriResetGLErrorBaseline(const GameState* /*state*/)
{
    if (!GEngine)
        return GameValue("FAIL:no_engine");
    const unsigned int now = GEngine->GetDebugErrorCount();
    TriGlErrorBaseline() = now;
    LOG_INFO(Core, "[tri] triResetGLErrorBaseline: snapshot={}", now);
    return GameValue("OK");
}

/// triGetGLErrorCount — number of new GL errors since the last triResetGLErrorBaseline
/// (or process start). Defined here to access the TU-local TriGlErrorBaseline().
GameValue TriGetGLErrorCount(const GameState* /*state*/)
{
    if (!GEngine)
        return GameValue(static_cast<float>(-1));
    const unsigned int now = GEngine->GetDebugErrorCount();
    const unsigned int baseline = TriGlErrorBaseline();
    return GameValue(static_cast<float>(now > baseline ? now - baseline : 0u));
}

/// triSamplePixel [u, v] — read a single pixel from the back buffer at the
/// given normalized coords (0..1, top-left origin).  Returns "R,G,B" with
/// 8-bit values, e.g. "120,200,140", or "FAIL:..." on error.
GameValue TriSamplePixel(const GameState* /*state*/, GameValuePar arg)
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
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
        return GameValue("FAIL:out_of_range");

    int w = GEngine->Width();
    int h = GEngine->Height();
    int px = (int)(u * (w - 1));
    int py = (int)(v * (h - 1));
    uint8_t rgb[3] = {0, 0, 0};
    if (!GEngine->SamplePixel(px, py, rgb))
        return GameValue("FAIL:not_supported");

    char buf[64];
    snprintf(buf, sizeof(buf), "%d,%d,%d", rgb[0], rgb[1], rgb[2]);
    LOG_INFO(Core, "[tri] triSamplePixel uv=({},{}) -> ({},{},{})", u, v, rgb[0], rgb[1], rgb[2]);
    return GameValue(buf);
}

/// triCursorMove [x, y] — write the cursor position into GInput.cursor.
/// Lets a test stage hover / drag scenarios that depend on FindControl's
/// hit-test results — synthetic triMouseDown / triMouseDragV bypass the
/// dispatch chain and inject directly on a known idc, which doesn't
/// catch dispatch-side bugs (a control routing wrong, a hit-test gap).
/// Returns "OK".
GameValue TriCursorMove(const GameState* /*state*/, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected_array");
    const GameArrayType& a = arg;
    if (a.Size() < 2)
        return GameValue("FAIL:expected_xy");
    float x = (float)(GameScalarType)a[0];
    float y = (float)(GameScalarType)a[1];
    GInput.cursor.cursorX = x;
    GInput.cursor.cursorY = y;
    LOG_INFO(Core, "[tri] triCursorMove ({},{})", x, y);
    return GameValue("OK");
}

/// triCursorMoveControl <idc> — move the synthetic cursor to the centre of the
/// given control.  Uses the control's live projected geometry instead of a
/// hardcoded screen coordinate so hover-driven tests can target notebook rows
/// without hand-calibrated cursor values.
GameValue TriCursorMoveControl(const GameState* /*state*/, GameValuePar arg)
{
    int idc = static_cast<int>(static_cast<GameScalarType>(arg));
    auto* display = GetActiveDisplayForSQF();
    if (!display)
        return GameValue("FAIL:no_display");

    IControl* ctrl = display->GetCtrl(idc);
    auto* control = dynamic_cast<Control*>(ctrl);
    if (!control)
        return GameValue("FAIL:not_found");

    // Default to the screen-space AABB centre.
    float screenX = control->X() + control->W() * 0.5f;
    float screenY = control->Y() + control->H() * 0.5f;
    if (auto* c3d = dynamic_cast<Control3D*>(ctrl))
    {
        // Prefer the projected centre of the actual 3D quad (u=0.5, v=0.5):
        // IsInside ray-casts the cursor back against that quad, so this point
        // round-trips to the same control.  The AABB centre (X()+W()/2) does
        // NOT under perspective — for notebook rows it can fall inside the
        // neighbouring row's quad, so hover/click tests landed on the wrong
        // row.  SceneToScreen divides by the projected Z, so before the quad
        // has been projected (UpdateInfo) GetCenter() is the origin and the
        // result is non-finite — keep the AABB centre in that case.
        DrawCoord c = SceneToScreen(c3d->GetCenter());
        if (std::isfinite(c.x) && std::isfinite(c.y))
        {
            screenX = c.x;
            screenY = c.y;
        }
    }
    float cursorX = screenX * 2.0f - 1.0f;
    float cursorY = screenY * 2.0f - 1.0f;
    GInput.cursor.cursorX = cursorX;
    GInput.cursor.cursorY = cursorY;
    LOG_INFO(Core, "[tri] triCursorMoveControl IDC={} cursor=({},{})", idc, cursorX, cursorY);
    return GameValue("OK");
}

/// triCursorPos — report the synthetic cursor position as "x,y" in the
/// same normalized [-1..1] coordinates used by triCursorMove.
GameValue TriCursorPos(const GameState* /*state*/)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%.4f,%.4f", GInput.cursor.cursorX, GInput.cursor.cursorY);
    return GameValue(buf);
}

/// triCursorDrawRect — report the last software cursor draw as "idd,x,y,w,h",
/// or "none" if no cursor draw has happened yet.
GameValue TriCursorDrawRect(const GameState* /*state*/)
{
    int idd = -1, x = 0, y = 0, w = 0, h = 0;
    if (!CursorDrawDebug::Read(idd, x, y, w, h))
        return GameValue("none");

    char buf[96];
    snprintf(buf, sizeof(buf), "%d,%d,%d,%d,%d", idd, x, y, w, h);
    return GameValue(buf);
}

/// triAssertCursorDrawRect [idd, x, y, w, h, tol] — assert the last software
/// cursor draw targeted the expected display and screen rect within tolerance.
GameValue TriAssertCursorDrawRect(const GameState* /*state*/, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
        return GameValue("FAIL:expected_array");
    const GameArrayType& a = arg;
    if (a.Size() < 6)
        return GameValue("FAIL:expected_idd_xywh_tol");

    int expectedIDD = (int)(GameScalarType)a[0];
    int expectedX = (int)(GameScalarType)a[1];
    int expectedY = (int)(GameScalarType)a[2];
    int expectedW = (int)(GameScalarType)a[3];
    int expectedH = (int)(GameScalarType)a[4];
    int tol = (int)(GameScalarType)a[5];

    int idd = -1, x = 0, y = 0, w = 0, h = 0;
    if (!CursorDrawDebug::Read(idd, x, y, w, h))
        return GameValue("FAIL:none");

    if (idd == expectedIDD && std::abs(x - expectedX) <= tol && std::abs(y - expectedY) <= tol &&
        std::abs(w - expectedW) <= tol && std::abs(h - expectedH) <= tol)
    {
        LOG_INFO(Core, "[tri] triAssertCursorDrawRect OK got=({},{},{},{},{}) exp=({},{},{},{},{}) tol={}", idd, x, y,
                 w, h, expectedIDD, expectedX, expectedY, expectedW, expectedH, tol);
        return GameValue("OK");
    }

    char buf[160];
    snprintf(buf, sizeof(buf), "FAIL:got=%d,%d,%d,%d,%d exp=%d,%d,%d,%d,%d tol=%d", idd, x, y, w, h, expectedIDD,
             expectedX, expectedY, expectedW, expectedH, tol);
    LOG_ERROR(Core, "[tri] triAssertCursorDrawRect {}", buf);
    return GameValue(buf);
}

namespace
{
class TriDisabledChildDisplay : public Display
{
  public:
    TriDisabledChildDisplay(ControlsContainer* parent, int idd) : Display(parent)
    {
        _idd = idd;
        _enableSimulation = false;
        _enableDisplay = false;
    }
};
} // namespace

/// triOpenDisabledChildDisplay <idd> — attach a disabled child display to the
/// current active display. Used to regression-test cursor ownership for modal
/// editor/dialog children that participate in the stack while reporting
/// IsDisplayEnabled() == false.
GameValue TriOpenDisabledChildDisplay(const GameState* /*state*/, GameValuePar arg)
{
    int idd = static_cast<int>(static_cast<GameScalarType>(arg));
    auto* display = GetActiveDisplayForSQF();
    if (!display)
        return GameValue("FAIL:no_display");

    display->CreateChild(new TriDisabledChildDisplay(display, idd));
    LOG_INFO(Core, "[tri] triOpenDisabledChildDisplay idd={}", idd);
    return GameValue("OK");
}
