#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_scancode.h>
#include <stdint.h>
#include <algorithm>
#include <map>
#include <string_view>
#include <system_error>
#include <utility>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/platform.hpp>

// The PCH pulls in Logging.hpp which #defines DebugLog() as a logging macro.
// That collides with the method ImGui::DebugLog().  Undef before including
// ImGui headers — none of our code in this TU uses the DebugLog macro.
#ifdef DebugLog
#undef DebugLog
#endif

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <SDL3/SDL.h>
// This overlay is only ever wired up from the GL33 engine (see the GL33
// engine's calls into DebugOverlay::Init/NewFrame/Render), which isn't
// built for iOS (no desktop OpenGL there) -- skip the GL-backend headers
// and the few call sites below that need them.
#ifndef POSEIDON_TARGET_IOS
#include <imgui_impl_opengl3.h>
#include <glad/gl.h>
#endif

#include <Poseidon/Dev/Debug/DebugOverlay.hpp>
#include <Poseidon/Dev/Debug/DebugCheats.hpp>
#include <Poseidon/Dev/Debug/DebugCommands.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Input/ControlsCategory.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Input/UserAction.hpp>
#include <Poseidon/Foundation/Platform/AppConfig.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/AI/LicensePlateTextTuning.hpp>
#include <Poseidon/Graphics/Rendering/Draw/FontMapping.hpp>
#include <Poseidon/UI/Locale/MissionLanguageDetector.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <Poseidon/Dev/Diag/FrameProfiler.hpp>
#include <Poseidon/UI/Settings/GameSettingsConfig.hpp>
#include <Poseidon/UI/Settings/AspectRatio.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Foundation/Memory/CheckMem.hpp>
#include <Poseidon/Foundation/Memory/MemFreeReq.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/WorldInputContext.hpp>
#include <Poseidon/World/Entities/Infantry/Person.hpp>
#include <Poseidon/World/Entities/Vehicles/Transport.hpp>
#include <Poseidon/World/Scene/Object.hpp>
#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <Evaluator/express.hpp>
#include <filesystem>

// Voice-language + visibility helpers — extern decls so we don't
// have to pull the locale/audio/world internals into this TU.  All
// three are linked in from Poseidon.lib at the unified-build stage.
extern const std::string& GetSelectedVoiceLanguage();
extern void SetSelectedVoiceLanguage(const std::string&);
extern void SetVisibility(float distance);

#include <string>
#include <cstring>
#include <cstdio>
#include <functional>
#include <vector>

namespace Poseidon::Dev
{
namespace DebugOverlay
{

namespace
{
bool s_initialized = false;
bool s_visible = false;
bool s_selectShadowsTab = false; // one-shot: force-select the Shadows tab next draw
bool s_selectMemoryTab = false;  // one-shot: force-select the Memory tab next draw
SDL_Window* s_window = nullptr;
enum class RendererBackend
{
    None,
    OpenGL,
    Metal
};
RendererBackend s_rendererBackend = RendererBackend::None;
// Saved mouse-grab state while the dev panel holds the cursor released.
bool s_mouseReleasedByPanel = false;
bool s_savedMouseGrab = false;

// Deferred actions — populated by UI button click handlers, drained
// AFTER ImGui::Render() returns each frame.  Why deferred:
//
// Some cheat invocations call deep into the engine and run code that
// mutates state ImGui still needs in the current frame.  The worst
// offender is Cmd_SaveGame, whose World::SaveBin path ends with
// `MemoryCleanUp()` (engine/poseidon/Memory/JimboAllocator.cpp:252)
// — that shrinks the engine's memory pool back to the OS and can
// invalidate buffers some ImGui widget still references later in
// the same DrawCheatsTab pass.  The result was a confirmed crash
// stack:
//   ImGui::ButtonEx+0x38
//   DrawCheatsTab+0x275
//   DrawMainWindow+0x6d
//   EngineGL33::BackToFront+0xd
//
// Deferring keeps the click handlers tiny (just enqueue a closure)
// and runs the actual cheat after ImGui::Render() — by which point
// no ImGui internal data is still in flight, so any engine
// reallocation is safe.
std::vector<std::function<void()>> s_pendingActions;

void Defer(std::function<void()> action)
{
    s_pendingActions.push_back(std::move(action));
}

void CreateSharedContext(SDL_Window* window)
{
    s_window = window;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // no imgui.ini side-effects
    ImGui::StyleColorsDark();
}

// One mutable copy per (slot, role) shown by the tuner.  Pulled from the
// active mapping on first Render() and pushed back to font.cpp via
// SetFontMappingTuning on every slider change.
struct RoleEditState
{
    const char* prefix;
    const char* alias; // legacy alias prefix kept in sync (or nullptr)
    int renderPx;
    float widthScale;
    float baselineOffset;
    float syntheticBold;
    float letterSpacing;
};

struct TuningState
{
    bool loaded = false;
    RoleEditState roles[5]; // title, body, mono, serif, hand
};

TuningState s_tuning;
int s_currentRole = 0; // which face's tuning the panel is editing

static const char* const kRoleNames[5] = {"Title", "Body", "Mono", "Serif", "Hand"};
static const char* const kRolePrefixes[5] = {"cwrtitle", "cwrbody", "cwrmono", "cwrserif", "cwrhand"};
static const char* const kRoleAliases[5] = {"steelfishb", "tahomab", "couriernewb", "garamond", "audreyshand"};

void LoadTuningIfNeeded()
{
    if (s_tuning.loaded)
        return;
    // Parse the dump to fill renderPx / widthScale for each role.
    // Format per line: '  {"prefix", "ttfPath", maxH, renderPx, widthScalef, oblique},'
    const char* dump = DumpFontTable();
    for (int r = 0; r < 5; r++)
    {
        s_tuning.roles[r].prefix = kRolePrefixes[r];
        s_tuning.roles[r].alias = kRoleAliases[r];
        s_tuning.roles[r].renderPx = 24;
        s_tuning.roles[r].widthScale = 1.0f;
        s_tuning.roles[r].baselineOffset = 0.0f;
        s_tuning.roles[r].syntheticBold = 0.0f;
        s_tuning.roles[r].letterSpacing = 0.0f;
        char needle[64];
        snprintf(needle, sizeof(needle), "{\"%s\",", kRolePrefixes[r]);
        const char* p = strstr(dump, needle);
        if (!p)
            continue;
        // Skip past prefix + ttfPath + maxH by counting commas.  DumpFontTable
        // emits:  prefix, ttfPath, maxH, renderPx, widthScalef, obliqueBool,
        //         baselineOffsetf, syntheticBoldf, letterSpacingf
        int commas = 0;
        const char* q = p;
        while (*q && commas < 3)
        {
            if (*q == ',')
                commas++;
            q++;
        }
        // q now points just after the 3rd comma — next is space + renderPx
        int rpx = 0;
        float ws = 1.0f;
        char oblique[16] = "false";
        float baseline = 0.0f, bold = 0.0f, spacing = 0.0f;
        if (sscanf(q, " %d, %ff, %15[^,], %ff, %ff, %ff", &rpx, &ws, oblique, &baseline, &bold, &spacing) >= 2)
        {
            s_tuning.roles[r].renderPx = rpx;
            s_tuning.roles[r].widthScale = ws;
            s_tuning.roles[r].baselineOffset = baseline;
            s_tuning.roles[r].syntheticBold = bold;
            s_tuning.roles[r].letterSpacing = spacing;
        }
    }
    s_tuning.loaded = true;
}

void DrawFontTab()
{
    // ── Face picker ────────────────────────────────────────────────
    // A single shipping font set; this picks which face the
    // size/stretch/spacing sliders below tune.
    ImGui::Text("Face:");
    for (int r = 0; r < 5; r++)
    {
        if (r > 0)
            ImGui::SameLine();
        bool active = (s_currentRole == r);
        if (active)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.6f, 0.9f, 1.0f));
        if (ImGui::Button(kRoleNames[r]))
            s_currentRole = r;
        if (active)
            ImGui::PopStyleColor();
    }
    ImGui::Separator();

    // ── Sliders for the selected face ─────────────────────────────
    LoadTuningIfNeeded();
    auto& role = s_tuning.roles[s_currentRole];

    ImGui::Text("%s - %s (alias %s)", kRoleNames[s_currentRole], role.prefix, role.alias ? role.alias : "(none)");

    bool changed = false;
    changed |= ImGui::SliderInt("renderPx", &role.renderPx, 8, 128);
    changed |= ImGui::SliderFloat("widthScale", &role.widthScale, 0.3f, 1.6f, "%.3f");
    changed |= ImGui::SliderFloat("baseline", &role.baselineOffset, -16.0f, 16.0f, "%.1f px");
    changed |= ImGui::SliderFloat("bold", &role.syntheticBold, -2.0f, 4.0f, "%.1f px");
    changed |= ImGui::SliderFloat("spacing", &role.letterSpacing, -4.0f, 8.0f, "%.1f px");
    if (changed)
    {
        SetFontMappingTuning(role.prefix, role.renderPx, role.widthScale, role.baselineOffset, role.syntheticBold,
                             role.letterSpacing, nullptr);
        if (role.alias)
            SetFontMappingTuning(role.alias, role.renderPx, role.widthScale, role.baselineOffset, role.syntheticBold,
                                 role.letterSpacing, nullptr);
    }
    ImGui::Separator();

    if (ImGui::Button("Dump font table to log"))
    {
        LOG_INFO(Graphics, "\n{}", DumpFontTable());
    }

    ImGui::Separator();
    ImGui::TextUnformatted("License plates");
    LicensePlateTextTuning plate = GetLicensePlateTextTuning();
    bool plateChanged = false;
    plateChanged |= ImGui::SliderFloat("plate width", &plate.widthScale, 0.30f, 1.20f, "%.3f");
    plateChanged |= ImGui::SliderFloat("plate x offset", &plate.horizontalOffset, -5.00f, 1.00f, "%.2f em");
    plateChanged |= ImGui::SliderFloat("plate y offset", &plate.verticalOffset, -1.00f, 2.00f, "%.2f em");
    plateChanged |= ImGui::SliderFloat("plate surface offset", &plate.surfaceOffset, 0.000f, 0.050f, "%.3f m");
    plateChanged |= ImGui::SliderFloat("plate softness", &plate.softness, 0.000f, 0.050f, "%.3f em");
    if (plateChanged)
        SetLicensePlateTextTuning(plate);
    ImGui::SameLine();
    if (ImGui::Button("Reset plate"))
        ResetLicensePlateTextTuning();
    ImGui::TextDisabled("  session-only override; defaults come from CfgLicensePlateText");
}

// Last command output for the Cheats tab — shown under the buttons so
// the user gets a visible confirmation that a click did something.
std::string s_cheatsStatus;

template <typename ClickFn>
void CheatButton(const char* label, bool enabled, const char* tooltip, ClickFn&& click)
{
    ImGui::BeginDisabled(!enabled);
    if (ImGui::Button(label))
        click();
    ImGui::EndDisabled();
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", tooltip);
}

void DrawCheatsTab()
{
    ImGui::TextUnformatted("Mission");
    ImGui::Separator();

    // One-button End Mission, matching the original OFP ENDMISSION word
    // cheat shape — no outcome picker; the cheat just wins the mission.
    // Power users who want a specific outcome use the `triEndMission
    // "lose"` / `endmission end3` / etc. paths through tri or the
    // console; Cmd_EndMission::Invoke keeps all the outcome strings.
    //
    // Re-queried every frame so the button greys instantly when there's
    // no mission running or the mission has already entered teardown.
    const bool canEnd = DebugCheats::Cmd_EndMission::Available();
    CheatButton("End Mission (win)", canEnd,
                canEnd ? "Force the active mission to win (sets EndMode=EMEnd1).\n"
                         "Matches the 1999 ENDMISSION word-cheat semantics.\n"
                         "Closes the dev panel afterwards — the engine starts\n"
                         "tearing down the world over the next several frames\n"
                         "and keeping any in-mission UI alive during that\n"
                         "transition risks crashes (textures get evicted while\n"
                         "we'd still be querying them).\n"
                         "Other outcomes still available via `triEndMission` /\n"
                         "the dev console (lose, killed, end1..end6)."
                       : "Requires an active mission that has not already ended.",
                []
                {
                    // Hide first (cheap, just sets a bool) — the deferred
                    // Invoke can then run after ImGui::Render with no
                    // panel-render side effects to worry about.
                    SetVisible(false);
                    Defer([] { DebugCheats::Cmd_EndMission::Invoke("win", s_cheatsStatus); });
                });

    ImGui::Spacing();
    ImGui::TextUnformatted("System");
    ImGui::Separator();

    // Full in-process reload — re-mounts all banks/addons/config and rebuilds
    // the world on the same window (the mod "Apply" path).  Gated to outside a
    // mission: re-mounting mid-mission would evict assets the simulation still
    // references.  Hide the panel first, then run after ImGui::Render — the
    // reload tears down the very world/UI we'd otherwise be drawing this frame.
    const bool canReload = Poseidon::GApp != nullptr && Poseidon::GApp->m_canRender && GWorld != nullptr &&
                           GWorld->GetMode() == GModeIntro;
    CheatButton("Reload game", canReload,
                canReload ? "Reload all game content (mods + config) in place.\n"
                            "Keeps the window, shows the loading screen, and lands\n"
                            "back on a fresh main menu."
                          : "Available from the main menu (not during a mission).",
                []
                {
                    SetVisible(false);
                    // Queue the reload for the next AppIdle (before simulate/draw) rather than
                    // running it inside the swap — see RequestRemount / RequestDeferredReload.
                    if (Poseidon::GApp != nullptr)
                        Poseidon::GApp->RequestRemount();
                });

    CheatButton("Exit game", true, "Exits game.",
                []
                {
                    SetVisible(false);
                    // Queue the close
                    if (Poseidon::GApp != nullptr)
                        Poseidon::GApp->m_closeRequest = 1;
                });

    ImGui::Spacing();
    ImGui::TextUnformatted("Player");
    ImGui::Separator();

    // God mode — sticky toggle.  Disabled state mirrors EndMission's
    // gating: needs a mission with a real player.  The toggle itself
    // is persisted by DebugCheats; we just round-trip the bool here.
    const bool canGod = DebugCheats::Cmd_God::Available();
    bool god = DebugCheats::Cmd_God::IsActive();
    ImGui::BeginDisabled(!canGod);
    if (ImGui::Checkbox("God mode", &god))
        DebugCheats::Cmd_God::SetActive(god);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", canGod ? "Silently drop all damage applied to the real player.\n"
                                         "Hooked in Object::SetDammage so every weapon / explosion /\n"
                                         "fall is covered, not just script-driven damage."
                                       : "Requires an active mission.");

    // Infinite ammo — same gating shape as god mode.
    const bool canAmmo = DebugCheats::Cmd_InfiniteAmmo::Available();
    bool infammo = DebugCheats::Cmd_InfiniteAmmo::IsActive();
    ImGui::BeginDisabled(!canAmmo);
    if (ImGui::Checkbox("Infinite ammo", &infammo))
        DebugCheats::Cmd_InfiniteAmmo::SetActive(infammo);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", canAmmo ? "Refund the burst the real player just fired so the current\n"
                                          "magazine never depletes.  Hooked in EntityAI::FireWeapon.\n"
                                          "AI weapons fire normally; magazine swaps are unaffected."
                                        : "Requires an active mission.");

    ImGui::Spacing();
    ImGui::TextUnformatted("Vehicle (player's current)");
    ImGui::Separator();

    // Infinite fuel — hooked in Transport::ConsumeFuel.  Only meaningful
    // when the player is inside a vehicle; the gating logic itself
    // still allows toggling on foot (the cheat just has no effect
    // until the player mounts something).
    const bool canFuel = DebugCheats::Cmd_InfiniteFuel::Available();
    bool inffuel = DebugCheats::Cmd_InfiniteFuel::IsActive();
    ImGui::BeginDisabled(!canFuel);
    if (ImGui::Checkbox("Infinite fuel", &inffuel))
        DebugCheats::Cmd_InfiniteFuel::SetActive(inffuel);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", canFuel ? "Refund fuel consumption on the vehicle the player is in.\n"
                                          "Hook lives in Transport::ConsumeFuel.  Refuel still works.\n"
                                          "No effect when on foot or in an AI-driven vehicle."
                                        : "Requires an active mission.");

    // Infinite armor — second SetDammage gate, targeting the player's
    // vehicle rather than the player's own body.
    const bool canArmor = DebugCheats::Cmd_InfiniteArmor::Available();
    bool infarmor = DebugCheats::Cmd_InfiniteArmor::IsActive();
    ImGui::BeginDisabled(!canArmor);
    if (ImGui::Checkbox("Infinite armor", &infarmor))
        DebugCheats::Cmd_InfiniteArmor::SetActive(infarmor);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", canArmor ? "Drop damage to the vehicle the player is currently in.\n"
                                           "Hooked alongside god mode in Object::SetDammage.\n"
                                           "Covers tanks, jeeps, planes, helicopters."
                                         : "Requires an active mission.");

    ImGui::Spacing();
    ImGui::TextUnformatted("Actions");
    ImGui::Separator();

    // Store position — log + clipboard.  The original OFP INSERT dev
    // hotkey.  Always available when a scene is up (works in the menu
    // demo loop too — camera dump is useful even without a mission
    // player).
    const bool canStore = DebugCheats::Cmd_StorePosition::Available();
    CheatButton("Store position (log + clipboard)", canStore,
                canStore ? "Dump camera + player position to the log AND copy a\n"
                           "ready-to-paste block (triSetView for the exact render\n"
                           "view + this setPos for the player) to the clipboard.\n"
                           "Replaces the old INSERT hotkey."
                         : "Requires a scene with an active camera.",
                [] { Defer([] { DebugCheats::Cmd_StorePosition::Invoke("", s_cheatsStatus); }); });

    // Save game — one-shot action.  Always usable when a mission is
    // running, including missions that normally disallow save.
    const bool canSave = DebugCheats::Cmd_SaveGame::Available();
    CheatButton("Save game now", canSave,
                canSave ? "Force-save the current world state to <SaveDir>/save.fps.\n"
                          "No 'save allowed' gating — bypasses mission-script restrictions."
                        : "Requires an active mission.",
                [] { Defer([] { DebugCheats::Cmd_SaveGame::Invoke("", s_cheatsStatus); }); });

    // Load game — inverse.  Grey out when the save file isn't on disk.
    const bool canLoad = DebugCheats::Cmd_LoadGame::Available();
    ImGui::SameLine();
    CheatButton("Load game now", canLoad,
                canLoad ? "Restore from <SaveDir>/save.fps via World::LoadBin.\n"
                          "Same engine path the normal 'Load Game' menu uses;\n"
                          "rehydrates the world in place (player, vehicles,\n"
                          "ammo, damage, time).  Deferred to run after ImGui\n"
                          "finishes the frame, same reason as Save."
                        : "Requires an active mission.  If <SaveDir>/save.fps doesn't\n"
                          "exist the click reports it in the status line; no crash.",
                [] { Defer([] { DebugCheats::Cmd_LoadGame::Invoke("", s_cheatsStatus); }); });

    // Skip time — four buttons.  The original OFP SCANCODE_T/Y/G/H
    // cheats are +1h / -1h continuous and +24h / -24h one-shot;
    // discrete buttons match the dev panel's click-driven UI better.
    const bool canTime = DebugCheats::Cmd_SkipTime::Available();
    ImGui::BeginDisabled(!canTime);
    if (ImGui::Button("Time -1h"))
        DebugCheats::Cmd_SkipTime::InvokeHours(-1.0f, s_cheatsStatus);
    ImGui::SameLine();
    if (ImGui::Button("Time +1h"))
        DebugCheats::Cmd_SkipTime::InvokeHours(+1.0f, s_cheatsStatus);
    ImGui::SameLine();
    if (ImGui::Button("Time -24h"))
        DebugCheats::Cmd_SkipTime::InvokeHours(-24.0f, s_cheatsStatus);
    ImGui::SameLine();
    if (ImGui::Button("Time +24h"))
        DebugCheats::Cmd_SkipTime::InvokeHours(+24.0f, s_cheatsStatus);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !canTime)
        ImGui::SetTooltip("Requires an active mission.");

    // Weather presets — instant overcast change.  No active-value
    // highlight: there's no public World::GetOvercast() to read back,
    // so we can't reliably show which preset is in effect.
    const bool canWeather = DebugCheats::Cmd_SetWeather::Available();
    ImGui::TextUnformatted("Weather:");
    ImGui::SameLine();
    ImGui::BeginDisabled(!canWeather);
    struct WeatherPreset
    {
        const char* label;
        float overcast;
    };
    static const WeatherPreset kWeather[] = {
        {"Clear", 0.0f},
        {"Cloudy", 0.3f},
        {"Overcast", 0.7f},
        {"Storm", 1.0f},
    };
    for (int i = 0; i < (int)(sizeof(kWeather) / sizeof(kWeather[0])); i++)
    {
        if (i > 0)
            ImGui::SameLine();
        if (ImGui::Button(kWeather[i].label))
            DebugCheats::Cmd_SetWeather::InvokeOvercast(kWeather[i].overcast, s_cheatsStatus);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !canWeather)
        ImGui::SetTooltip("Requires an active mission.");

    // Time multiplier — preset list.  Highlights the active value so
    // the user sees what's currently selected.  Engine saturates to
    // [kTimeAccMin, kTimeAccMax]; reading back via Get() reports the
    // clamped value.
    const bool canMult = DebugCheats::Cmd_TimeMultiplier::Available();
    const float currentMult = canMult ? DebugCheats::Cmd_TimeMultiplier::Get() : 1.0f;
    ImGui::TextUnformatted("Time multiplier:");
    ImGui::SameLine();
    ImGui::BeginDisabled(!canMult);
    static const float kPresets[] = {0.5f, 1.0f, 2.0f, 4.0f};
    for (int i = 0; i < (int)(sizeof(kPresets) / sizeof(kPresets[0])); i++)
    {
        const float v = kPresets[i];
        // Highlight the active preset (within 0.01 — float compare).
        const bool isActive = canMult && (currentMult > v - 0.01f) && (currentMult < v + 0.01f);
        if (isActive)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.6f, 0.9f, 1.0f));
        char label[16];
        snprintf(label, sizeof(label), "%.1fx", v);
        if (i > 0)
            ImGui::SameLine();
        if (ImGui::Button(label))
            DebugCheats::Cmd_TimeMultiplier::SetValue(v, s_cheatsStatus);
        if (isActive)
            ImGui::PopStyleColor();
    }
    ImGui::EndDisabled();

    // Unlock campaign — writes <TmpSaveDir>/<campaign>.sqc files
    // directly so the unlock survives reopening the campaign load
    // screen.  Works from any display.
    if (ImGui::Button("Unlock all campaigns"))
        Defer([] { DebugCheats::Cmd_UnlockCampaign::Invoke("", s_cheatsStatus); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", "Mark every mission of every installed campaign as available.\n"
                                "Writes the unlock to <TmpSaveDir>/<campaign>.sqc so the\n"
                                "next Campaign Load open sees it.  Refreshes the live list\n"
                                "if the Campaign Load screen happens to be open right now.");

    ImGui::Spacing();
    ImGui::TextUnformatted("Map");
    ImGui::Separator();

    // Show all units — independent of the _showUnits flag (which is
    // _ENABLE_CHEATS-gated).  Adds an unconditional DrawUnits pass in
    // CStaticMapMain::DrawExt across every AICenter.
    const bool canShowAll = DebugCheats::Cmd_ShowAllUnits::Available();
    bool showAll = DebugCheats::Cmd_ShowAllUnits::IsActive();
    ImGui::BeginDisabled(!canShowAll);
    if (ImGui::Checkbox("Show all units on map", &showAll))
        DebugCheats::Cmd_ShowAllUnits::SetActive(showAll);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", canShowAll ? "Draw every unit of every side on the in-mission map,\n"
                                             "bypassing fog-of-war and side filtering.  Hooked in\n"
                                             "CStaticMapMain::DrawExt."
                                           : "Requires an active mission.");

    // Click-to-teleport — left-click on the in-mission map teleports
    // the player's vehicle to the clicked spot instead of issuing the
    // normal move/watch order.
    const bool canTeleport = DebugCheats::Cmd_MapTeleport::Available();
    bool teleport = DebugCheats::Cmd_MapTeleport::IsActive();
    ImGui::BeginDisabled(!canTeleport);
    if (ImGui::Checkbox("Click-on-map to teleport", &teleport))
        DebugCheats::Cmd_MapTeleport::SetActive(teleport);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", canTeleport ? "Open the in-mission map (M) and left-click anywhere to\n"
                                              "teleport the player's vehicle.  Snaps to the ground\n"
                                              "surface height; if you're in a tank/chopper, the whole\n"
                                              "vehicle goes along.  Hooked in CStaticMapMain::OnLButtonClick."
                                            : "Requires an active mission.");

    if (!s_cheatsStatus.empty())
    {
        ImGui::Separator();
        ImGui::TextWrapped("Last: %s", s_cheatsStatus.c_str());
    }
}

// Game tab — text / voice language pickers + view distance slider.
// Reuses the same kLangRotation list the F12 / F11 dev hotkeys use
// (engine/poseidon/UI/optionsUI.cpp:2333) so picker + hotkey stay
// consistent.
namespace
{
static const char* const kGameLangs[] = {
    "English", "Czech", "French", "German", "Italian", "Spanish", "Russian",
};
constexpr int kGameLangsCount = (int)(sizeof(kGameLangs) / sizeof(kGameLangs[0]));

int FindLangIndex(const char* current)
{
    if (!current)
        return 0;
    for (int i = 0; i < kGameLangsCount; i++)
        if (stricmp(current, kGameLangs[i]) == 0)
            return i;
    return 0;
}

const char* DebugBool(bool value)
{
    return value ? "true" : "false";
}

RString DebugObjectName(Object* object)
{
    if (!object)
        return "<null>";
    return object->GetDebugName();
}

ControlsCategory DebugSettingsCategoryForContext(InputContext context)
{
    switch (context)
    {
        case InputContext::Infantry:
            return ControlsCategoryOnFoot;
        case InputContext::CarDriver:
        case InputContext::TankDriver:
        case InputContext::ShipDriver:
            return ControlsCategoryVehicles;
        case InputContext::HeliPilot:
        case InputContext::PlanePilot:
            return ControlsCategoryPilot;
        case InputContext::TankGunner:
        case InputContext::Gunner:
            return ControlsCategoryGunner;
        default:
            return ControlsCategoryCount;
    }
}

void DrawInputContextDiagnostics()
{
    ImGui::TextUnformatted("Input context");
    if (!GWorld)
    {
        ImGui::TextDisabled("world not loaded");
        return;
    }

    const auto& input = InputSubsystem::Instance();
    const InputContextResolution resolution = GWorld->ResolveInputContextResolution();
    const InputContext liveWorld = resolution.context;
    const InputContext cached = input.GetContext();

    Person* player = GWorld->PlayerOn();

    AIUnit* focus = GWorld->FocusOn();

    ImGui::Text("World: live=%s cached=%s manual=%s map=%s options=%s", InputContextName(liveWorld),
                InputContextName(cached), DebugBool(GWorld->PlayerManual()), DebugBool(GWorld->HasMap()),
                DebugBool(GWorld->HasOptions()));
    const ControlsCategory settingsCategory = DebugSettingsCategoryForContext(liveWorld);
    ImGui::Text("Settings: %s", settingsCategory == ControlsCategoryCount ? "<none>" : GetControlsCategoryName(settingsCategory));
    ImGui::Text("Resolved: %s | %s", static_cast<const char*>(DebugObjectName(resolution.transport)),
                InputSeatContextName(resolution.seat));
    ImGui::Text("Player: %s", static_cast<const char*>(DebugObjectName(player)));
    ImGui::Text("Focus:  %s", focus ? static_cast<const char*>(focus->GetDebugName()) : "<null>");
    ImGui::Text("Camera: %s", static_cast<const char*>(DebugObjectName(GWorld->CameraOn())));

    const InputContext ctx = resolution.context;
    ImGui::Text("Actions [%s]: F %.2f B %.2f L %.2f R %.2f Up %.2f Dn %.2f TL %.2f TR %.2f",
                InputContextName(ctx), input.GetAction(ctx, UAMoveForward, true), input.GetAction(ctx, UAMoveBack, true),
                input.GetAction(ctx, UAMoveLeft, true), input.GetAction(ctx, UAMoveRight, true),
                input.GetAction(ctx, UAMoveUp, true), input.GetAction(ctx, UAMoveDown, true),
                input.GetAction(ctx, UATurnLeft, true), input.GetAction(ctx, UATurnRight, true));
}
} // namespace

void DrawGameTab()
{
    ImGui::TextUnformatted("Language");
    ImGui::Separator();

    // Text language — drives stringtables, mission briefings, UI.
    const char* currentText = GLanguage;
    int textIdx = FindLangIndex(currentText);
    if (ImGui::Combo("Text", &textIdx, kGameLangs, kGameLangsCount))
        Defer([picked = std::string(kGameLangs[textIdx])] { SetLanguage(RString(picked.c_str())); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Stringtables, mission briefings, UI labels.\nSame as the F12 dev-only hotkey.");

    // Voice language — drives <base>.<voiceLang>.<ext> sound lookups.
    const std::string voiceLang = GetSelectedVoiceLanguage();
    int voiceIdx = FindLangIndex(voiceLang.c_str());
    if (ImGui::Combo("Voice", &voiceIdx, kGameLangs, kGameLangsCount))
        Defer([picked = std::string(kGameLangs[voiceIdx])] { SetSelectedVoiceLanguage(picked); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Voice-over track for say / playSound / radio.\nSame as the F11 dev-only hotkey.\nIn-flight "
                          "audio is unaffected; lookup applies on the next play.");

    ImGui::Spacing();
    ImGui::TextUnformatted("View distance");
    ImGui::Separator();

    // VD slider — clamped to the same range as the Options UI.  Engine
    // saturates internally too; we mirror so the slider can't request
    // a value that just gets clipped silently.
    static float s_vd = ENGINE_CONFIG.tacticalZ;
    s_vd = ENGINE_CONFIG.tacticalZ; // sync with whatever else set it
    if (ImGui::SliderFloat("VD (m)", &s_vd, GameSettingsConfig::kMinViewDistance, GameSettingsConfig::kMaxViewDistance,
                           "%.0f m"))
    {
        const float v = s_vd;
        Defer([v] { SetVisibility(v); });
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Terrain / horizon distance (the master).\nRange %.0f..%.0f m.  Bypasses the "
                          "per-tier graphics preset.",
                          GameSettingsConfig::kMinViewDistance, GameSettingsConfig::kMaxViewDistance);
    // Object and shadow distances are derived from VD (ViewDistanceResolver), so
    // there are no separate sliders — moving VD moves all three.

    ImGui::Spacing();
    ImGui::TextUnformatted("Diagnostics");
    ImGui::Separator();

    DrawInputContextDiagnostics();
    ImGui::Spacing();

    // The TXT / VO / VD localization-status block in the mission preview is a
    // diagnostic overlay, hidden from players by default.  Off shows the plain
    // mission overview; on prepends the per-language text/voice/view-distance table.
    bool showLoc = MissionLanguageDetector::ShowLocalizationDebugInfo();
    if (ImGui::Checkbox("Mission localization info (TXT/VO/VD)", &showLoc))
        MissionLanguageDetector::SetShowLocalizationDebugInfo(showLoc);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Prepend the per-language text/voice availability and view-distance\n"
                          "table to the mission preview.  Re-select a mission to refresh.");
}

// Console tab — SQF / DebugCommand runner.  Bare lines without a `:`
// prefix dispatch through DebugCommands::Run first (so "save",
// "endmission win", etc. work), then fall back to SQF evaluation.
// Lines starting with `:` are forced to SQF (use this if a SQF name
// happens to collide with a DebugCommand name).
namespace
{
struct ConsoleState
{
    char input[512] = "";
    std::vector<std::string> scrollback;
    bool autoScroll = true;
    bool focusOnShow = true;
};
ConsoleState s_console;

void ConsoleAppend(const std::string& line)
{
    s_console.scrollback.push_back(line);
    if (s_console.scrollback.size() > 200)
        s_console.scrollback.erase(s_console.scrollback.begin(),
                                   s_console.scrollback.begin() + (s_console.scrollback.size() - 200));
}

void ConsoleRun(std::string_view line)
{
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
        line.remove_prefix(1);
    if (line.empty())
        return;
    ConsoleAppend(std::string("> ") + std::string(line));

    const bool forceSqf = !line.empty() && line.front() == ':';
    if (forceSqf)
        line.remove_prefix(1);

    if (!forceSqf)
    {
        std::string out;
        if (DebugCommands::Run(line, out))
        {
            if (!out.empty())
                ConsoleAppend(out);
            return;
        }
    }

    // SQF path.  Mirrors the Evaluator/express.hpp idiom — runs in the
    // current game state's evaluation context.  No-op + error log when
    // no world is up (e.g. main-menu, before mission load).
    if (!GWorld || !GWorld->GetGameState())
    {
        ConsoleAppend("(no game state — SQF unavailable here)");
        return;
    }
    GameValue result = GWorld->GetGameState()->EvaluateMultiple(std::string(line).c_str());
    if (result.GetType() != GameNothing)
        ConsoleAppend(std::string("= ") + (const char*)result.GetText());
}
} // namespace

void DrawConsoleTab()
{
    ImGui::TextUnformatted("SQF / DebugCommands console");
    ImGui::Separator();

    // Scrollback region.  Reserve room for the input row at the bottom.
    const float inputRowH = ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginChild("ConsoleScroll", ImVec2(0, -inputRowH), true, ImGuiWindowFlags_HorizontalScrollbar))
    {
        for (const auto& line : s_console.scrollback)
            ImGui::TextUnformatted(line.c_str());
        if (s_console.autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();

    // Input row: text box + Run button.  Enter inside the text box
    // also runs the line.  EnterReturnsTrue makes the InputText
    // produce true when Enter is hit, so we don't need a separate
    // key check.
    if (s_console.focusOnShow)
    {
        ImGui::SetKeyboardFocusHere();
        s_console.focusOnShow = false;
    }
    bool entered = ImGui::InputText("##ConsoleInput", s_console.input, sizeof(s_console.input),
                                    ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    bool clicked = ImGui::Button("Run");
    if (entered || clicked)
    {
        std::string line(s_console.input);
        s_console.input[0] = 0;
        if (!line.empty())
            Defer([line] { ConsoleRun(line); });
        ImGui::SetKeyboardFocusHere(-1); // refocus the text box
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
        s_console.scrollback.clear();
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &s_console.autoScroll);

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Bare lines dispatch through DebugCommands first\n"
                          "(save, endmission win, weather 0.5, …).\n"
                          "Prefix `:` to force SQF (e.g. `:hint \"hi\"`)");
}

// Profile tab — FPS gauge + frame-time history graph.  Reads the
// engine's _lastFrameDuration each draw, keeps a 240-frame ring
// buffer (~4 seconds at 60 fps) for the plot.
namespace
{
constexpr int kProfHistory = 240;
float s_frameMs[kProfHistory] = {};
int s_frameMsHead = 0;

void ProfileSample()
{
    if (!GEngine)
        return;
    const uint32_t lastMs = GEngine->GetLastFrameDuration();
    const float ms = static_cast<float>(lastMs);
    s_frameMs[s_frameMsHead] = ms;
    s_frameMsHead = (s_frameMsHead + 1) % kProfHistory;
}

float ProfileFps()
{
    if (!GEngine)
        return 0.0f;
    const uint32_t lastMs = GEngine->GetLastFrameDuration();
    return lastMs > 0 ? 1000.0f / static_cast<float>(lastMs) : 0.0f;
}

float ProfileFrameMs()
{
    if (!GEngine)
        return 0.0f;
    return static_cast<float>(GEngine->GetLastFrameDuration());
}
} // namespace

void DrawProfileTab()
{
    ProfileSample(); // pump every draw

    ImGui::TextUnformatted("Frame stats");
    ImGui::Separator();

    const float fps = ProfileFps();
    const float ms = ProfileFrameMs();
    ImGui::Text("FPS:   %.1f", fps);
    ImGui::Text("Frame: %.2f ms", ms);

    // Frame-time plot.  PlotLines is fine for ring-buffered floats;
    // ImGui handles the visual stride.  Y-axis fixed 0..50 ms (~20fps
    // floor) so spikes are visible without auto-rescaling jitter.
    ImGui::Separator();
    ImGui::PlotLines("##frame_ms", s_frameMs, kProfHistory, s_frameMsHead, "frame ms (last 240)", 0.0f, 50.0f,
                     ImVec2(0, 80));

    if (ImGui::Button("Reset history"))
    {
        for (int i = 0; i < kProfHistory; i++)
            s_frameMs[i] = 0.0f;
        s_frameMsHead = 0;
    }
}

// Memory tab — live MemoryUsed() value + peak tracker + history plot.
namespace
{
constexpr int kMemHistory = 240;
float s_memMb[kMemHistory] = {};
int s_memHead = 0;
size_t s_memPeak = 0;

void MemorySample()
{
    const size_t used = Foundation::MemoryUsed();
    if (used > s_memPeak)
        s_memPeak = used;
    s_memMb[s_memHead] = static_cast<float>(used) / (1024.0f * 1024.0f);
    s_memHead = (s_memHead + 1) % kMemHistory;
}
} // namespace

inline float ToMB(size_t bytes)
{
    return static_cast<float>(bytes) / (1024.0f * 1024.0f);
}

void DrawMemoryTab()
{
    MemorySample();

    const Foundation::ProcessMemoryStats stats = Foundation::MemoryProcessStats();
    const float mb = ToMB(stats.used);
    const float peakMb = ToMB(s_memPeak);

    ImGui::TextUnformatted("Process heap");
    ImGui::Separator();
    ImGui::Text("Current: %.1f MB", mb);
    ImGui::Text("Peak:    %.1f MB", peakMb);
    if (stats.softLimit || stats.hardLimit)
    {
        ImGui::Text("Soft (trim):  %.0f MB%s", ToMB(stats.softLimit),
                    stats.softLimit && stats.used > stats.softLimit ? "  (OVER — trimming to budgets)" : "");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Pressure watermark. Over it, each cache is trimmed back to its own\n"
                              "declared budget once per frame (FrameMaintenance). Never refuses.");
        ImGui::Text("Hard (evict): %.0f MB%s", ToMB(stats.hardLimit),
                    stats.hardLimit && stats.used > stats.hardLimit ? "  (OVER — evicting caches)" : "");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Eviction target, not a wall. Over it the allocator additionally claws\n"
                              "memory back with cost-ordered cache eviction — but never refuses an\n"
                              "allocation: refusing would crash the engine's many unchecked `new` sites.");
    }
    else
    {
        ImGui::TextDisabled("No process limit set (unlimited).");
    }

    // Plot — same shape as the profile tab.  Y-axis floats around the
    // peak; ImPlot would give a nicer presentation but PlotHistogram
    // is sufficient and zero-dependency.
    char overlay[64];
    snprintf(overlay, sizeof(overlay), "MB used (last %d frames)", kMemHistory);
    ImGui::PlotHistogram("##mem_mb", s_memMb, kMemHistory, s_memHead, overlay, 0.0f, peakMb * 1.1f + 1.0f,
                         ImVec2(0, 80));

    if (ImGui::Button("Reset peak / history"))
    {
        for (int i = 0; i < kMemHistory; i++)
            s_memMb[i] = 0.0f;
        s_memHead = 0;
        s_memPeak = stats.used;
    }

    // ── Per-subsystem residency (the FreeOnDemand registry) ────────────────
    // One snapshot drives both the count and the table so they can't disagree.
    Foundation::MemoryDomainStat domains[32];
    const int n = Foundation::MemorySnapshotDomains(domains, 32);

    ImGui::Spacing();
    ImGui::Text("Subsystems (%d registered)", n);
    ImGui::SameLine();
    if (ImGui::Button("Trim caches now"))
        Defer([] { Foundation::MemoryEnforceBudgets(); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Evict every registered cache back to its declared budget\n"
                          "(FreeOnDemand). Domains with no budget are untouched.");
    ImGui::Separator();

    if (n == 0)
    {
        ImGui::TextDisabled("(no subsystems registered yet)");
    }
    else if (ImGui::BeginTable("mem_domains", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("domain");
        ImGui::TableSetupColumn("held");
        ImGui::TableSetupColumn("budget / usage");
        ImGui::TableHeadersRow();
        for (int i = 0; i < n; i++)
        {
            const Foundation::MemoryDomainStat& d = domains[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(d.name);
            ImGui::TableNextColumn();
            // Byte-accounted caches show MB; count-only registries (shapes,
            // materials) show their item count instead of a misleading 0 MB.
            if (d.heldBytes > 0 || d.heldItems == 0)
                ImGui::Text("%.1f MB", ToMB(d.heldBytes));
            else
                ImGui::Text("%zu items", d.heldItems);
            ImGui::TableNextColumn();
            if (d.budgetBytes > 0)
            {
                const float frac = static_cast<float>(d.heldBytes) / static_cast<float>(d.budgetBytes);
                char label[48];
                snprintf(label, sizeof(label), "%.0f / %.0f MB", ToMB(d.heldBytes), ToMB(d.budgetBytes));
                ImGui::ProgressBar(frac > 1.0f ? 1.0f : frac, ImVec2(-1, 0), label);
            }
            else
            {
                ImGui::TextDisabled("— (no budget)");
            }
        }
        ImGui::EndTable();
    }

    // ── Live limit controls — set a watermark and watch the tick trim/evict ──
    ImGui::Spacing();
    ImGui::TextDisabled("Set process limits (MB; soft=trim, hard=evict; 0 = unlimited):");
    static int s_softMB = -1, s_hardMB = -1;
    if (s_softMB < 0) // seed once from the live values
    {
        s_softMB = static_cast<int>(ToMB(stats.softLimit));
        s_hardMB = static_cast<int>(ToMB(stats.hardLimit));
    }
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("soft (trim) MB", &s_softMB, 32, 256);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("hard (evict) MB", &s_hardMB, 32, 256);
    if (s_softMB < 0)
        s_softMB = 0;
    if (s_hardMB < 0)
        s_hardMB = 0;
    if (ImGui::Button("Apply limits"))
    {
        const size_t soft = static_cast<size_t>(s_softMB) * (1024 * 1024);
        const size_t hard = static_cast<size_t>(s_hardMB) * (1024 * 1024);
        Defer([soft, hard] { Foundation::SetProcessMemoryLimits(soft, hard); });
    }
    ImGui::SameLine();
    ImGui::TextDisabled("session-only; persist via EngineConfig");
}

// Live mod picker + in-process reload. Lists the @<mod> folders actually present
// in the user's mods folder, lets you tick a set, and re-mounts the game with
// exactly that set — only from the main menu (re-mounting mid-mission would evict
// assets the simulation still references).
void DrawModsTab()
{
    namespace fs = std::filesystem;
    const std::string modsRoot = GamePaths::Instance().ModsDir();

    ImGui::TextUnformatted("Mods folder (scanned live):");
    ImGui::TextWrapped("%s", modsRoot.c_str());
    ImGui::Separator();

    // Checkbox state persists across frames: modId ("@foo") -> checked.
    static std::map<std::string, bool> s_modChecked;

    // Live scan for @<mod> folders each frame.
    std::vector<std::string> mods;
    std::error_code ec;
    if (fs::is_directory(modsRoot, ec))
    {
        for (const auto& entry : fs::directory_iterator(modsRoot, ec))
        {
            std::error_code dirEc;
            if (!entry.is_directory(dirEc))
                continue;
            const std::string name = entry.path().filename().string();
            if (!name.empty() && name[0] == '@')
                mods.push_back(name);
        }
    }
    std::sort(mods.begin(), mods.end());

    if (mods.empty())
        ImGui::TextDisabled("(no @<mod> folders here yet — drop one in and it shows up)");

    for (const std::string& modId : mods)
    {
        auto it = s_modChecked.find(modId);
        bool checked = (it != s_modChecked.end()) ? it->second : false;
        if (ImGui::Checkbox(modId.c_str(), &checked) || it == s_modChecked.end())
            s_modChecked[modId] = checked;
    }

    // Build the mod path from the checked set — semicolon-separated absolute
    // mod-folder paths (empty string = base game only). modsRoot ends with a sep.
    std::string modPath;
    for (const std::string& modId : mods)
    {
        auto it = s_modChecked.find(modId);
        if (it != s_modChecked.end() && it->second)
        {
            if (!modPath.empty())
                modPath += ';';
            modPath += modsRoot + modId;
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextWrapped("Apply set: %s", modPath.empty() ? "(none — base game only)" : modPath.c_str());

    const bool canReload = Poseidon::GApp != nullptr && Poseidon::GApp->m_canRender && GWorld != nullptr &&
                           GWorld->GetMode() == GModeIntro;
    CheatButton("Reload with selected mods", canReload,
                canReload ? "Re-mount the game with exactly the checked mods.\n"
                            "Keeps the window, shows the loading screen, and lands\n"
                            "back on a fresh main menu with the new mod set.\n"
                            "Uncheck everything to reload the base game."
                          : "Available from the main menu only (not during a mission).",
                [modPath]
                {
                    SetVisible(false);
                    // Queue for the next AppIdle (before simulate/draw); running the reload
                    // inside the swap crashed the rebuilt world's first Simulate.
                    RequestDeferredReload(modPath.c_str());
                });
}

void AspectReapply()
{
    // Re-resolve + apply the aspect settings for the current viewport.
    // Deferred so the engine mutation runs after ImGui::Render returns.
    Defer(
        []
        {
            if (GEngine)
                GEngine->FireResizePostHook(GEngine->Width(), GEngine->Height());
        });
}

// Release the game's mouse grab while the panel is open so the cursor can
// leave the window (to drag-resize it); restore on close.  The game keeps
// simulating — this only frees the cursor.
void ApplyDevPanelMouseState()
{
    if (!GEngine)
        return;
    if (s_visible && !s_mouseReleasedByPanel)
    {
        s_savedMouseGrab = GEngine->IsMouseGrabbed();
        GEngine->SetMouseGrab(false);
        s_mouseReleasedByPanel = true;
    }
    else if (!s_visible && s_mouseReleasedByPanel)
    {
        GEngine->SetMouseGrab(s_savedMouseGrab);
        s_mouseReleasedByPanel = false;
    }
}

// Resize the window to the largest box of the given aspect ratio that fits
// the current monitor's usable area, then center it (so it stays fully
// visible).  Drives the normal SDL resize path → aspect re-resolves.
void ResizeWindowToRatio(float ratio)
{
    if (!s_window || ratio <= 0.0f)
        return;
    int availW = 1920, availH = 1080;
    const SDL_DisplayID disp = SDL_GetDisplayForWindow(s_window);
    SDL_Rect ub{};
    if (SDL_GetDisplayUsableBounds(disp, &ub) && ub.w > 0 && ub.h > 0)
    {
        availW = ub.w;
        availH = ub.h;
    }
    const float margin = 0.90f;
    float w = static_cast<float>(availW) * margin;
    float h = w / ratio;
    if (h > static_cast<float>(availH) * margin)
    {
        h = static_cast<float>(availH) * margin;
        w = h * ratio;
    }
    int iw = static_cast<int>(w + 0.5f);
    int ih = static_cast<int>(h + 0.5f);
    if (iw < 320)
        iw = 320;
    if (ih < 240)
        ih = 240;
    SDL_SetWindowSize(s_window, iw, ih);
    SDL_SetWindowPosition(s_window, SDL_WINDOWPOS_CENTERED_DISPLAY(disp), SDL_WINDOWPOS_CENTERED_DISPLAY(disp));
}

void DrawAspectTab()
{
    AspectRatio::LiveControls& live = AspectRatio::Live();
    bool changed = false;

    // --- Window size + monitor info + resize-to-ratio presets ---
    if (s_window)
    {
        int ww = 0, wh = 0;
        SDL_GetWindowSize(s_window, &ww, &wh);
        ImGui::Text("window : %d x %d  (%.3f)", ww, wh,
                    wh > 0 ? static_cast<float>(ww) / static_cast<float>(wh) : 0.0f);
        const SDL_DisplayID disp = SDL_GetDisplayForWindow(s_window);
        SDL_Rect ub{};
        if (SDL_GetDisplayUsableBounds(disp, &ub) && ub.h > 0)
            ImGui::Text("monitor: %d x %d  (%.3f)", ub.w, ub.h, static_cast<float>(ub.w) / static_cast<float>(ub.h));
        ImGui::TextDisabled("resize window (fits monitor, centered):");
        struct RatioPreset
        {
            const char* label;
            float ratio;
        };
        static const RatioPreset presets[] = {
            {"32:9", 32.0f / 9.0f}, {"21:9", 21.0f / 9.0f}, {"16:9", 16.0f / 9.0f}, {"16:10", 16.0f / 10.0f},
            {"3:2", 3.0f / 2.0f},   {"4:3", 4.0f / 3.0f},   {"5:4", 5.0f / 4.0f},
        };
        for (int i = 0; i < static_cast<int>(sizeof(presets) / sizeof(presets[0])); ++i)
        {
            if (i > 0 && i != 4) // row break before "3:2"
                ImGui::SameLine();
            if (ImGui::Button(presets[i].label))
            {
                const float r = presets[i].ratio;
                Defer([r] { ResizeWindowToRatio(r); });
            }
        }
        ImGui::Separator();
    }

    changed |= ImGui::Checkbox("Override enabled", &live.overrideEnabled);
    ImGui::SameLine();
    ImGui::TextDisabled("(off = display.cfg policy)");
    ImGui::Separator();

    int style = static_cast<int>(live.style);
    if (ImGui::Combo("Display style", &style, "Modern\0Legacy\0"))
    {
        live.style = (style == 1) ? AspectRatio::Legacy : AspectRatio::Modern;
        changed = true;
    }
    int clamp = static_cast<int>(live.clamp);
    if (ImGui::Combo("Ultrawide clamp", &clamp,
                     "Off\0"
                     "21:9\0"
                     "16:9\0"))
    {
        live.clamp = static_cast<AspectRatio::UltrawideClamp>(clamp);
        changed = true;
    }

    ImGui::Separator();
    changed |= ImGui::Checkbox("Pillarbox  (crop world to band + black bars)", &live.pillarbox);
    changed |= ImGui::Checkbox("HUD clamp  (center UI in band, world full)", &live.hudClamp);

    ImGui::Separator();
    changed |= ImGui::Checkbox("Manual viewport (noodle)", &live.manualRect);
    changed |= ImGui::SliderFloat("rect Left", &live.rectL, 0.0f, 1.0f, "%.3f");
    changed |= ImGui::SliderFloat("rect Top", &live.rectT, 0.0f, 1.0f, "%.3f");
    changed |= ImGui::SliderFloat("rect Right", &live.rectR, 0.0f, 1.0f, "%.3f");
    changed |= ImGui::SliderFloat("rect Bottom", &live.rectB, 0.0f, 1.0f, "%.3f");
    if (ImGui::Button("Reset rect to full"))
    {
        live.rectL = 0.0f;
        live.rectT = 0.0f;
        live.rectR = 1.0f;
        live.rectB = 1.0f;
        live.manualRect = false;
        changed = true;
    }

    if (changed)
        AspectReapply();

    ImGui::Separator();
    if (GEngine)
    {
        Poseidon::AspectSettings a;
        GEngine->GetAspectSettings(a);
        ImGui::Text("viewport   %d x %d", GEngine->Width(), GEngine->Height());
        ImGui::Text("FOV        L=%.3f  T=%.3f", a.leftFOV, a.topFOV);
        ImGui::Text("UI rect    x[%.3f..%.3f] y[%.3f..%.3f]", a.uiTopLeftX, a.uiBottomRightX, a.uiTopLeftY,
                    a.uiBottomRightY);
        ImGui::Text("world rect x[%.3f..%.3f] y[%.3f..%.3f]", a.worldLeft, a.worldRight, a.worldTop, a.worldBottom);
    }
}

void DrawShadowsTab()
{
    if (!GEngine)
    {
        ImGui::TextDisabled("No engine.");
        return;
    }

    Engine::ShadowMapTuning t = GEngine->GetShadowMapTuning();
    bool changed = false;

    ImGui::TextDisabled("Depth-buffer shadow maps (durable fix).");
    ImGui::TextDisabled("OFF = legacy projected shadows. ON = light-space shadow map (no flicker).");
    ImGui::Separator();

    changed |= ImGui::Checkbox("Enabled (shadow maps)", &t.enabled);
    ImGui::SameLine();
    ImGui::TextDisabled(t.enabled ? "(projected path skipped)" : "(projected path active)");

    ImGui::Separator();

    changed |= ImGui::SliderFloat("Darkness", &t.darkness, 0.0f, 1.0f, "%.3f");
    ImGui::TextDisabled("  lit-colour multiplier where shadowed; lower = darker (1.0 = no shadow)");

    changed |= ImGui::SliderInt("Cascades", &t.cascadeCount, 1, 4);
    ImGui::TextDisabled("  total tiers (omni + frustum); more = crisper across distance");

    changed |= ImGui::SliderFloat("Distance coef", &t.distanceCoef, 0.05f, 1.0f, "%.3f");
    ImGui::TextDisabled("  frustum-tier far distance as a fraction of view distance (1.0 = full VD)");

    changed |= ImGui::SliderFloat("Split coef", &t.splitCoef, 0.0f, 1.0f, "%.2f");
    ImGui::TextDisabled("  PSSM blend: 0 = uniform splits, 1 = logarithmic (FP 0.95)");

    ImGui::Separator();
    ImGui::TextDisabled("Omni near tiers — camera-centred spheres, all-direction coverage");
    ImGui::TextDisabled("(so a caster behind/beside you still casts a shadow into view)");
    changed |= ImGui::SliderInt("Omni tiers", &t.omniCount, 0, t.cascadeCount);
    ImGui::TextDisabled("  leading tiers fit a sphere around the camera (0 = pure frustum)");
    changed |= ImGui::SliderFloat("Omni radius 0", &t.omniCoef0, 0.02f, 0.5f, "%.3f");
    changed |= ImGui::SliderFloat("Omni radius 1", &t.omniCoef1, 0.02f, 0.8f, "%.3f");
    ImGui::TextDisabled("  sphere radii as a fraction of the shadow range (ascending)");

    changed |= ImGui::SliderFloat("Bias base", &t.biasBase, 0.0f, 0.0005f, "%.6f");
    ImGui::TextDisabled("  per-cascade depth bias base*(i+1)^2; raise to kill acne");

    changed |= ImGui::SliderFloat("Far fade (m)", &t.fadeRange, 1.0f, 120.0f, "%.1f");
    ImGui::TextDisabled("  distant shadows dissolve over this band instead of a hard cut-off");

    static const int resOptions[] = {512, 1024, 2048, 4096};
    int resIdx = 2;
    for (int i = 0; i < 4; ++i)
        if (resOptions[i] == t.resolution)
            resIdx = i;
    if (ImGui::Combo("Resolution", &resIdx,
                     "512\0"
                     "1024\0"
                     "2048\0"
                     "4096\0"))
    {
        t.resolution = resOptions[resIdx];
        changed = true;
    }
    ImGui::TextDisabled("  per-cascade depth-map size; higher = sharper, more VRAM");

    ImGui::Separator();
    if (ImGui::Button("Reset knobs to defaults"))
    {
        const bool keepEnabled = t.enabled;
        t = Engine::ShadowMapTuning{};
        t.enabled = keepEnabled;
        changed = true;
    }

    if (changed)
        GEngine->SetShadowMapTuning(t);

    // Read-back: a one-line summary the user can copy and paste back so the
    // values they tuned by eye can be baked into the engine defaults.
    ImGui::Separator();
    ImGui::TextDisabled("Current tuning (copy back to share):");
    char summary[320];
    snprintf(summary, sizeof(summary),
             "shadows: enabled=%s darkness=%.3f cascades=%d omni=%d/%.3f/%.3f dist=%.3f split=%.2f bias=%.6f "
             "fade=%.1f res=%d",
             t.enabled ? "true" : "false", t.darkness, t.cascadeCount, t.omniCount, t.omniCoef0, t.omniCoef1,
             t.distanceCoef, t.splitCoef, t.biasBase, t.fadeRange, t.resolution);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##shadowSummary", summary, sizeof(summary), ImGuiInputTextFlags_ReadOnly);
    if (ImGui::Button("Copy summary to clipboard"))
        ImGui::SetClipboardText(summary);
}

// Live anti-aliasing knobs — MSAA sample count, SSAA render scale and
// alpha-to-coverage apply at the next frame boundary, so the effect is
// visible immediately while hunting for the shipped default.
// Live frame-phase breakdown from the always-on FrameProfiler ring
// (World::Simulate marks setup/draw/hud/ai+veh/sound/swap each frame).
void DrawPerfTab()
{
    Dev::FrameProfiler& perf = Dev::GFrameProfiler();
    const int frames = perf.FrameCount();
    if (frames == 0)
    {
        ImGui::TextDisabled("no frames recorded yet");
        return;
    }

    const Dev::FrameProfiler::PhaseStats total = perf.TotalStats();
    ImGui::Text("FPS %.1f", perf.AvgFps());
    ImGui::SameLine();
    ImGui::TextDisabled("frame %.2f ms avg / %.2f p95 / %.2f max (last %d frames)", total.avgMs, total.p95Ms,
                        total.maxMs, frames);

    static float history[Dev::FrameProfiler::kRingSize];
    const int n = frames;
    for (int i = 0; i < n; i++)
        history[i] = perf.Frame(n - 1 - i).totalMs; // oldest → newest
    ImGui::PlotLines("##frametimes", history, n, 0, "frame ms", 0.f, total.maxMs * 1.2f, ImVec2(-1, 64));

    if (ImGui::BeginTable("phases", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("phase");
        ImGui::TableSetupColumn("avg ms");
        ImGui::TableSetupColumn("p95 ms");
        ImGui::TableSetupColumn("max ms");
        ImGui::TableSetupColumn("% frame");
        ImGui::TableHeadersRow();
        for (int p = 0; p < Dev::FrameProfiler::PhaseCount; p++)
        {
            const auto s = perf.Stats(static_cast<Dev::FrameProfiler::Phase>(p));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(Dev::FrameProfiler::PhaseName(p));
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", s.avgMs);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", s.p95Ms);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", s.maxMs);
            ImGui::TableNextColumn();
            ImGui::Text("%.0f%%", total.avgMs > 0.001f ? 100.f * s.avgMs / total.avgMs : 0.f);
        }
        ImGui::EndTable();
    }
    ImGui::Text("draw calls %.0f avg", perf.AvgDrawCalls());
    ImGui::SameLine();
    if (ImGui::Button("Reset window"))
        perf.Reset();
}
void DrawRenderTab()
{
    if (!GEngine)
    {
        ImGui::TextDisabled("engine not up");
        return;
    }

    int samples = GEngine->GetMsaaSamples();
    int sampleIdx = samples >= 8 ? 3 : samples >= 4 ? 2 : samples >= 2 ? 1 : 0;
    if (ImGui::Combo("MSAA", &sampleIdx,
                     "Off\0"
                     "2x\0"
                     "4x\0"
                     "8x\0"))
    {
        static const int kSamples[] = {0, 2, 4, 8};
        GEngine->SetMsaaSamples(kSamples[sampleIdx]);
    }

    float scale = GEngine->GetRenderScale();
    if (ImGui::SliderFloat("Render scale (SSAA)", &scale, 1.0f, 2.0f, "%.2f"))
        GEngine->SetRenderScale(scale);
    ImGui::SameLine();
    if (ImGui::Button("1x"))
        GEngine->SetRenderScale(1.0f);
    ImGui::SameLine();
    if (ImGui::Button("1.5x"))
        GEngine->SetRenderScale(1.5f);
    ImGui::SameLine();
    if (ImGui::Button("2x"))
        GEngine->SetRenderScale(2.0f);

    bool a2c = GEngine->GetAlphaToCoverage();
    if (ImGui::Checkbox("Alpha-to-coverage (cutout AA; needs MSAA)", &a2c))
        GEngine->SetAlphaToCoverage(a2c);

    bool flat = GEngine->GetDebugFlatColor();
    if (ImGui::Checkbox("Flat shading (objects -> solid red; shading-vs-geometry probe)", &flat))
        GEngine->SetDebugFlatColor(flat);

    ImGui::Separator();
    ImGui::Text("window  %d x %d", GEngine->Width(), GEngine->Height());
    ImGui::Text("target  scale %.2fx, %dx MSAA", GEngine->GetRenderScale(), GEngine->GetMsaaSamples());
    ImGui::TextDisabled("settings are session-only; persist via graphics.cfg");
}
void DrawMainWindow()
{
    ImGui::SetNextWindowSize(ImVec2(560, 480), ImGuiCond_FirstUseEver);
    ImGui::Begin("Poseidon Dev Panel");

    if (ImGui::BeginTabBar("DevPanelTabs"))
    {
        if (ImGui::BeginTabItem("Cheats"))
        {
            DrawCheatsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Mods"))
        {
            DrawModsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Game"))
        {
            DrawGameTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Console"))
        {
            DrawConsoleTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Profile"))
        {
            DrawProfileTab();
            ImGui::EndTabItem();
        }
        ImGuiTabItemFlags memoryFlags = 0;
        if (s_selectMemoryTab)
        {
            memoryFlags = ImGuiTabItemFlags_SetSelected;
            s_selectMemoryTab = false;
        }
        if (ImGui::BeginTabItem("Memory", nullptr, memoryFlags))
        {
            DrawMemoryTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Perf"))
        {
            DrawPerfTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Font"))
        {
            DrawFontTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Aspect"))
        {
            DrawAspectTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Render"))
        {
            DrawRenderTab();
            ImGui::EndTabItem();
        }
        ImGuiTabItemFlags shadowFlags = 0;
        if (s_selectShadowsTab)
        {
            shadowFlags = ImGuiTabItemFlags_SetSelected;
            s_selectShadowsTab = false;
        }
        if (ImGui::BeginTabItem("Shadows", nullptr, shadowFlags))
        {
            DrawShadowsTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::Separator();
    ImGui::TextDisabled("Ctrl+` / Ctrl+; to hide");
    ImGui::End();
}
} // namespace

void Init(SDL_Window* window, void* glContext)
{
    if (s_initialized)
        return;
#ifdef POSEIDON_TARGET_IOS
    LOG_ERROR(Graphics, "DebugOverlay: OpenGL backend requested on iOS");
    (void)window;
    (void)glContext;
    return;
#else
    CreateSharedContext(window);

    if (!ImGui_ImplSDL3_InitForOpenGL(window, glContext))
    {
        LOG_ERROR(Graphics, "DebugOverlay: ImGui_ImplSDL3_InitForOpenGL failed");
        ImGui::DestroyContext();
        s_window = nullptr;
        return;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 330"))
    {
        LOG_ERROR(Graphics, "DebugOverlay: ImGui_ImplOpenGL3_Init failed");
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        s_window = nullptr;
        return;
    }

    s_rendererBackend = RendererBackend::OpenGL;
    s_initialized = true;
    LOG_INFO(Graphics, "DebugOverlay: ImGui initialized (press Ctrl+` / Ctrl+; to toggle)");
#endif
}

void InitForMetal(SDL_Window* window)
{
    if (s_initialized)
        return;
    CreateSharedContext(window);

    if (!ImGui_ImplSDL3_InitForMetal(window))
    {
        LOG_ERROR(Graphics, "DebugOverlay: ImGui_ImplSDL3_InitForMetal failed");
        ImGui::DestroyContext();
        s_window = nullptr;
        return;
    }

    s_rendererBackend = RendererBackend::Metal;
    s_initialized = true;
    LOG_INFO(Graphics, "DebugOverlay: ImGui initialized for Metal (press Ctrl+` / Ctrl+; to toggle)");
}

void Shutdown()
{
    if (!s_initialized)
        return;
#ifndef POSEIDON_TARGET_IOS
    if (s_rendererBackend == RendererBackend::OpenGL)
        ImGui_ImplOpenGL3_Shutdown();
#endif
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    s_rendererBackend = RendererBackend::None;
    s_initialized = false;
    s_window = nullptr;
}

void ProcessEvent(const SDL_Event& event)
{
    if (!s_initialized)
        return;
    ImGui_ImplSDL3_ProcessEvent(&event);

    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
    {
        // Ctrl+Grave + F5 are dev-only hotkeys (toggle dev panel +
        // role-slot flicker) gated by --dev.
        if (!AppConfig::Instance().DevMode())
            return;
        // Ctrl+` (US) / Ctrl+; (CZ) — toggle the dev panel.  Ctrl is required
        // so the unmodified key stays available to the game (it's used in
        // radio/chat commands).
        const bool ctrlDown = (event.key.mod & SDL_KMOD_CTRL) != 0;
        if ((event.key.scancode == SDL_SCANCODE_GRAVE || event.key.scancode == SDL_SCANCODE_SEMICOLON) && ctrlDown)
        {
            ToggleVisible();
            return;
        }
    }
}

void NewFrame()
{
    if (!s_initialized)
        return;
    if (!AppConfig::Instance().DevMode() && s_visible)
        SetVisible(false);
    // Toggle the software cursor with panel visibility.  When the panel is
    // shown, the engine's UI cursor renders BEHIND ImGui (we composite ImGui
    // after the game render), so we draw our own cursor as part of ImGui's
    // drawlist to stay on top.  When hidden, fall back to the engine cursor.
    ImGui::GetIO().MouseDrawCursor = s_visible;
#ifndef POSEIDON_TARGET_IOS
    if (s_rendererBackend == RendererBackend::OpenGL)
        ImGui_ImplOpenGL3_NewFrame();
#endif
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    if (s_visible)
        DrawMainWindow();
}

void Render()
{
    if (!s_initialized)
        return;
    ImGui::Render();
#ifndef POSEIDON_TARGET_IOS
    // Make sure we draw to the default framebuffer in case the engine left
    // an FBO bound — happens with post-FX in GL33.  Other state (blend,
    // scissor, vao, depth) is saved/restored inside RenderDrawData.
    if (s_rendererBackend == RendererBackend::OpenGL)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
#endif

    // Drain deferred actions queued by UI click handlers.  See the
    // s_pendingActions comment for the why — running cheats here
    // (after ImGui::Render returns) means engine code in the cheat
    // can freely realloc / clean up without trashing ImGui state.
    if (!s_pendingActions.empty())
    {
        auto local = std::move(s_pendingActions);
        s_pendingActions.clear();
        for (auto& fn : local)
            fn();
    }
}

bool IsVisible()
{
    return s_visible;
}
void SetVisible(bool v)
{
    if (v && !AppConfig::Instance().DevMode())
        v = false;
    s_visible = v;
    ApplyDevPanelMouseState();
}
void ToggleVisible()
{
    SetVisible(!s_visible);
}
void SelectShadowsTab()
{
    s_selectShadowsTab = true;
}
void SelectMemoryTab()
{
    s_selectMemoryTab = true;
}

void RequestDeferredReload(const char* modPath)
{
    // Route through the Application's between-frames re-mount request (serviced at the top
    // of AppIdle, before any simulate/draw). Running the reload inside Render()/BackToFront
    // instead — mid-frame, after Simulate — left the rebuilt world's first Simulate touching
    // a torn-down sensor list (null SensorList, SensorList::CheckPos crash).
    Poseidon::GApp->RequestRemountWithMods(modPath);
}

bool WantsKeyboard()
{
    if (!s_initialized || !s_visible)
        return false;
    return ImGui::GetIO().WantCaptureKeyboard;
}

bool WantsMouse()
{
    if (!s_initialized || !s_visible)
        return false;
    return ImGui::GetIO().WantCaptureMouse;
}

} // namespace DebugOverlay
} // namespace Poseidon::Dev
