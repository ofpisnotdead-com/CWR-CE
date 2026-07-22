#include <Evaluator/express.hpp>
using namespace Poseidon;
#include <Poseidon/Foundation/Modules/Modules.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>
#include <Poseidon/Audio/IAudioSystem.hpp>
#include <Poseidon/UI/UITestEngine.hpp>
#include <Poseidon/UI/UIActiveDisplay.hpp>
#include <Poseidon/UI/Controls/UIControls.hpp>
#include <Poseidon/Dev/Debug/DebugOverlay.hpp>
#include <Poseidon/Core/ModSystem.hpp>
#include <Poseidon/Core/resincl.hpp>
#include <Poseidon/UI/DisplayUI.hpp>
#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Network/NetworkServerCommon.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <Poseidon/Input/ControllerUiScene.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/World/World.hpp>

#include <cstdint>
#include <string>
#include <algorithm>

using namespace Poseidon::Dev;

namespace Poseidon
{
extern int gSmDepthCachedCasters;
extern int gShadowFrozenCasters;
extern unsigned GTriNetSoundsReceived;
} // namespace Poseidon

extern int LastShadowProxyVertCount();

// TriGetGLErrorCount is defined in GameStateExtTest.cpp where it can access the
// anonymous-namespace TriGlErrorBaseline() helper.
GameValue TriGetGLErrorCount(const GameState*);

// ============================================================================
// Window getters
// ============================================================================

GameValue TriGetResizable(const GameState* /*state*/)
{
    if (!GEngine)
        return GameValue("0");
    return GameValue(GEngine->IsResizable() ? "1" : "0");
}

GameValue TriGetBackBufferNonBlackCount(const GameState* /*state*/)
{
    if (!GEngine)
        return GameValue(static_cast<float>(-1));
    return GameValue(static_cast<float>(GEngine->SampleBackBufferNonBlack()));
}

GameValue TriGetWindowWidth(const GameState* /*state*/)
{
    if (!GEngine)
        return GameValue(static_cast<float>(-1));
    return GameValue(static_cast<float>(GEngine->Width()));
}

GameValue TriGetWindowHeight(const GameState* /*state*/)
{
    if (!GEngine)
        return GameValue(static_cast<float>(-1));
    return GameValue(static_cast<float>(GEngine->Height()));
}

// ============================================================================
// Shadow getters
// ============================================================================

GameValue TriGetShadowSunFactor(const GameState* /*state*/)
{
    if (!GScene || !GScene->MainLight())
        return GameValue(static_cast<float>(-1));
    float f = 1.0f - GScene->MainLight()->NightEffect();
    f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
    return GameValue(f);
}

GameValue TriGetShadowDepthCachedCount(const GameState* /*state*/)
{
    return GameValue(static_cast<float>(gSmDepthCachedCasters));
}

GameValue TriGetShadowFrozenCasters(const GameState* /*state*/)
{
    return GameValue(static_cast<float>(gShadowFrozenCasters));
}

GameValue TriGetShadowMapCacheTest(const GameState* /*state*/)
{
    if (!GEngine)
        return GameValue("0");
    return GameValue(GEngine->ShadowMapCacheSelfTest() ? "1" : "0");
}

GameValue TriGetProxyVertCount(const GameState* /*state*/)
{
    return GameValue(static_cast<float>(LastShadowProxyVertCount()));
}

// ============================================================================
// Audio getters
// ============================================================================

GameValue TriGetAudioCacheEntries(const GameState* /*state*/)
{
    if (!GSoundsys)
        return GameValue(static_cast<float>(-1));
    return GameValue(static_cast<float>(GSoundsys->WaveCacheEntries()));
}

GameValue TriGetAudioCacheHits(const GameState* /*state*/)
{
    if (!GSoundsys)
        return GameValue(static_cast<float>(-1));
    return GameValue(static_cast<float>(GSoundsys->WaveCacheHits()));
}

// ============================================================================
// Pixel getters
// ============================================================================

/// triGetPixelMaxChannel [u, v] — scalar max(R,G,B) of the back-buffer pixel at
/// normalized coords (0..1, top-left origin). -1 on error.
GameValue TriGetPixelMaxChannel(const GameState* /*state*/, GameValuePar arg)
{
    if (!GEngine)
        return GameValue(static_cast<float>(-1));
    const GameArrayType& a = arg;
    if (a.Size() < 2)
        return GameValue(static_cast<float>(-1));
    const float u = static_cast<float>(static_cast<GameScalarType>(a[0]));
    const float v = static_cast<float>(static_cast<GameScalarType>(a[1]));
    const int w = GEngine->Width();
    const int h = GEngine->Height();
    if (w <= 0 || h <= 0)
        return GameValue(static_cast<float>(-1));
    const int px = static_cast<int>(u * static_cast<float>(w - 1));
    const int py = static_cast<int>(v * static_cast<float>(h - 1));
    uint8_t rgb[3] = {0, 0, 0};
    if (!GEngine->SamplePixel(px, py, rgb))
        return GameValue(static_cast<float>(-1));
    const int mx = std::max({static_cast<int>(rgb[0]), static_cast<int>(rgb[1]), static_cast<int>(rgb[2])});
    return GameValue(static_cast<float>(mx));
}

/// triGetPixelMaxDiff [u1, v1, u2, v2] — scalar max per-channel absolute diff
/// between two back-buffer pixels. -1 on error.
GameValue TriGetPixelMaxDiff(const GameState* /*state*/, GameValuePar arg)
{
    if (!GEngine)
        return GameValue(static_cast<float>(-1));
    const GameArrayType& a = arg;
    if (a.Size() < 4)
        return GameValue(static_cast<float>(-1));
    const float u1 = static_cast<float>(static_cast<GameScalarType>(a[0]));
    const float v1 = static_cast<float>(static_cast<GameScalarType>(a[1]));
    const float u2 = static_cast<float>(static_cast<GameScalarType>(a[2]));
    const float v2 = static_cast<float>(static_cast<GameScalarType>(a[3]));
    const int w = GEngine->Width();
    const int h = GEngine->Height();
    if (w <= 0 || h <= 0)
        return GameValue(static_cast<float>(-1));
    const int px1 = static_cast<int>(u1 * static_cast<float>(w - 1));
    const int py1 = static_cast<int>(v1 * static_cast<float>(h - 1));
    const int px2 = static_cast<int>(u2 * static_cast<float>(w - 1));
    const int py2 = static_cast<int>(v2 * static_cast<float>(h - 1));
    uint8_t rgb1[3] = {0, 0, 0};
    uint8_t rgb2[3] = {0, 0, 0};
    if (!GEngine->SamplePixel(px1, py1, rgb1) || !GEngine->SamplePixel(px2, py2, rgb2))
        return GameValue(static_cast<float>(-1));
    int mx = 0;
    for (int i = 0; i < 3; i++)
    {
        const int d = std::abs(static_cast<int>(rgb1[i]) - static_cast<int>(rgb2[i]));
        if (d > mx)
            mx = d;
    }
    return GameValue(static_cast<float>(mx));
}

/// triAssertPixelNotWhite [u, v, threshold] — passes when at least one RGB
/// channel is below threshold. Useful for catching missing white placeholder
/// textures without depending on the exact face artwork.
GameValue TriAssertPixelNotWhite(const GameState* /*state*/, GameValuePar arg)
{
    if (!GEngine)
        return GameValue("FAIL:no_engine");
    const GameArrayType& a = arg;
    if (a.Size() < 2)
        return GameValue("FAIL:need [u,v,(threshold)]");
    const float u = static_cast<float>(static_cast<GameScalarType>(a[0]));
    const float v = static_cast<float>(static_cast<GameScalarType>(a[1]));
    const int threshold = a.Size() >= 3 ? static_cast<int>(static_cast<GameScalarType>(a[2])) : 245;
    const int w = GEngine->Width();
    const int h = GEngine->Height();
    if (w <= 0 || h <= 0)
        return GameValue("FAIL:no_backbuffer");
    const int px = static_cast<int>(u * static_cast<float>(w - 1));
    const int py = static_cast<int>(v * static_cast<float>(h - 1));
    uint8_t rgb[3] = {0, 0, 0};
    if (!GEngine->SamplePixel(px, py, rgb))
        return GameValue("FAIL:sample");
    const int mn = std::min({static_cast<int>(rgb[0]), static_cast<int>(rgb[1]), static_cast<int>(rgb[2])});
    if (mn < threshold)
        return GameValue("OK");
    char buf[64];
    snprintf(buf, sizeof(buf), "FAIL:white:%d,%d,%d", rgb[0], rgb[1], rgb[2]);
    return GameValue(buf);
}

// ============================================================================
// UI / controls getters
// ============================================================================

GameValue TriGetControlVisible(const GameState* /*state*/, GameValuePar arg)
{
    const int idc = static_cast<int>(static_cast<GameScalarType>(arg));
    UITestEngine tmp;
    ControlsContainer* display = tmp.GetActiveDisplay();
    if (!display)
        return GameValue("0");
    IControl* ctrl = display->GetCtrl(idc);
    if (!ctrl)
        return GameValue("0");
    return GameValue(ctrl->IsVisible() ? "1" : "0");
}

GameValue TriGetControlFocused(const GameState* /*state*/, GameValuePar arg)
{
    const int idc = static_cast<int>(static_cast<GameScalarType>(arg));
    UITestEngine tmp;
    ControlsContainer* display = tmp.GetActiveDisplay();
    if (!display)
        return GameValue("0");
    IControl* ctrl = display->GetCtrl(idc);
    if (!ctrl)
        return GameValue("0");
    return GameValue(ctrl->IsFocused() ? "1" : "0");
}

GameValue TriGetControlEnabled(const GameState* /*state*/, GameValuePar arg)
{
    const int idc = static_cast<int>(static_cast<GameScalarType>(arg));
    UITestEngine tmp;
    ControlsContainer* display = tmp.GetActiveDisplay();
    if (!display)
        return GameValue("0");
    IControl* ctrl = display->GetCtrl(idc);
    if (!ctrl)
        return GameValue("0");
    return GameValue(ctrl->IsEnabled() ? "1" : "0");
}

GameValue TriGetCommandMenuOpen(const GameState* /*state*/)
{
    AbstractUI* ui = GWorld ? GWorld->GetUI() : nullptr;
    if (!ui)
        return GameValue("0");
    return GameValue(ui->IsCommandMenuOpen() ? "1" : "0");
}

GameValue TriGetDevPanelVisible(const GameState* /*state*/)
{
    return GameValue(DebugOverlay::IsVisible() ? "1" : "0");
}

// ============================================================================
// Options / locale getters
// ============================================================================

GameValue TriGetLanguage(const GameState* /*state*/)
{
    return GameValue(RString((const char*)GLanguage));
}

// ============================================================================
// MP / network / server getters
// ============================================================================

GameValue TriGetNgsState(const GameState* /*state*/)
{
    int val = -1;
    try
    {
        val = static_cast<int>(GetNetworkManager().GetServerState());
    }
    catch (...)
    {
    }
    return GameValue(static_cast<float>(val));
}

GameValue TriGetNgsClientState(const GameState* /*state*/)
{
    int val = -1;
    try
    {
        val = static_cast<int>(GetNetworkManager().GetGameState());
    }
    catch (...)
    {
    }
    return GameValue(static_cast<float>(val));
}

GameValue TriGetAdminLoggedIn(const GameState* /*state*/)
{
    bool granted = false;
    try
    {
        granted = GetNetworkManager().HasAdminLoggedIn();
    }
    catch (...)
    {
    }
    return GameValue(granted ? "1" : "0");
}

GameValue TriGetServerBanCount(const GameState* /*state*/)
{
    int count = -1;
    try
    {
        count = GetServerBanCount();
    }
    catch (...)
    {
    }
    return GameValue(static_cast<float>(count));
}

GameValue TriGetServerLocked(const GameState* /*state*/)
{
    bool locked = false;
    try
    {
        locked = GetServerLocked();
    }
    catch (...)
    {
    }
    return GameValue(locked ? "1" : "0");
}

GameValue TriGetNetSoundsReceived(const GameState* /*state*/)
{
    return GameValue(static_cast<float>(GTriNetSoundsReceived));
}

/// triGetModsActiveSet — comma-joined @modId of all ticked rows (CModsList::CheckedModIds).
GameValue TriGetModsActiveSet(const GameState* /*state*/)
{
    UITestEngine tmp;
    DisplayMods* mods = dynamic_cast<DisplayMods*>(tmp.GetActiveDisplay());
    if (!mods)
        return GameValue(RString(""));
    CModsList* list = dynamic_cast<CModsList*>(mods->GetCtrl(IDC_MODS_LIST));
    if (!list)
        return GameValue(RString(""));
    return GameValue(RString((const char*)list->CheckedModIds()));
}

/// triGetModsMountSet — comma-joined @modId of ticked rows that are not Missing.
GameValue TriGetModsMountSet(const GameState* /*state*/)
{
    UITestEngine tmp;
    DisplayMods* mods = dynamic_cast<DisplayMods*>(tmp.GetActiveDisplay());
    if (!mods)
        return GameValue(RString(""));
    CModsList* list = dynamic_cast<CModsList*>(mods->GetCtrl(IDC_MODS_LIST));
    if (!list)
        return GameValue(RString(""));
    std::string got;
    const AutoArray<ModRow>& rows = list->GetRows();
    for (int i = 0; i < rows.Size(); i++)
    {
        if (!rows[i].checked || rows[i].state == ModRowState::Missing)
            continue;
        if (!got.empty())
            got += ',';
        got += (const char*)rows[i].modId;
    }
    return GameValue(RString(got.c_str()));
}

/// triGetModsSortColumn — index 0-4 of the visible sort-caret icon, or -1 if none.
GameValue TriGetModsSortColumn(const GameState* /*state*/)
{
    UITestEngine tmp;
    DisplayMods* mods = dynamic_cast<DisplayMods*>(tmp.GetActiveDisplay());
    if (!mods)
        return GameValue(static_cast<float>(-1));
    const int idcs[5] = {IDC_MODS_ICON_NAME, IDC_MODS_ICON_VERSION, IDC_MODS_ICON_SIZE, IDC_MODS_ICON_STATE,
                         IDC_MODS_ICON_SOURCE};
    for (int c = 0; c < 5; c++)
    {
        IControl* icon = mods->GetCtrl(idcs[c]);
        if (icon && icon->IsVisible())
            return GameValue(static_cast<float>(c));
    }
    return GameValue(static_cast<float>(-1));
}

/// triGetActiveMods — pipe-joined active mod folder string (ModSystem::GetModList).
GameValue TriGetActiveMods(const GameState* /*state*/)
{
    return GameValue(RString((const char*)Poseidon::ModSystem::GetModList()));
}

/// triGetChatLines — pipe-joined chat lines from GChatList.
GameValue TriGetChatLines(const GameState* /*state*/)
{
    std::string result;
    for (int i = 0; i < GChatList.Size(); i++)
    {
        if (!result.empty())
            result += '|';
        result += (const char*)GChatList.Get(i).text;
    }
    return GameValue(RString(result.c_str()));
}

GameValue TriGetControllerScene(const GameState* /*state*/)
{
    if (!GWorld)
        return GameValue("None");
    const ControllerUiScene scene = GWorld->GetControllerUiScene();
    return GameValue(ControllerSceneKindName(scene.kind));
}

GameValue TriGetControllerSection(const GameState* /*state*/)
{
    if (!GWorld)
        return GameValue("None");
    const ControllerUiScene scene = GWorld->GetControllerUiScene();
    return GameValue(ControllerSectionKindName(scene.activeSection.kind));
}

GameValue TriGetControllerPrompts(const GameState* /*state*/)
{
    if (!GWorld)
        return GameValue("");
    const ControllerUiScene scene = GWorld->GetControllerUiScene();
    return GameValue(RString(BuildControllerPromptString(scene).c_str()));
}

GameValue TriGetInputContext(const GameState* /*state*/)
{
    switch (InputSubsystem::Instance().GetContext())
    {
        case InputContext::Menu:
            return GameValue("Menu");
        case InputContext::Infantry:
            return GameValue("Infantry");
        case InputContext::CarDriver:
            return GameValue("CarDriver");
        case InputContext::TankDriver:
            return GameValue("TankDriver");
        case InputContext::TankGunner:
            return GameValue("TankGunner");
        case InputContext::HeliPilot:
            return GameValue("HeliPilot");
        case InputContext::PlanePilot:
            return GameValue("PlanePilot");
        case InputContext::ShipDriver:
            return GameValue("ShipDriver");
        case InputContext::Gunner:
            return GameValue("Gunner");
        case InputContext::Spectator:
            return GameValue("Spectator");
        case InputContext::Map:
            return GameValue("Map");
        case InputContext::Chat:
            return GameValue("Chat");
        case InputContext::Editor:
            return GameValue("Editor");
        default:
            return GameValue("Unknown");
    }
}

GameValue TriGetCameraEffectActive(const GameState* /*state*/)
{
    return GameValue(static_cast<float>(GWorld && GWorld->GetCameraEffect() ? 1 : 0));
}

// Called from GameStateExtTestAudio.cpp's INIT_MODULE to force this TU into
// the link when building PoseidonGame (where no other game code references
// the TriGet* family directly).
void EnsureGameStateExtTestGettersLinked() {}

// ============================================================================
// Module registration
// ============================================================================

INIT_MODULE(GameStateExtTestGetters, 3)
{
    // Window
    GGameState.NewNularOp(GameNular(GameString, "triGetResizable", TriGetResizable));
    GGameState.NewNularOp(GameNular(GameScalar, "triGetBackBufferNonBlackCount", TriGetBackBufferNonBlackCount));
    GGameState.NewNularOp(GameNular(GameScalar, "triGetGLErrorCount", TriGetGLErrorCount));
    GGameState.NewNularOp(GameNular(GameScalar, "triGetWindowWidth", TriGetWindowWidth));
    GGameState.NewNularOp(GameNular(GameScalar, "triGetWindowHeight", TriGetWindowHeight));

    // Shadow
    GGameState.NewNularOp(GameNular(GameScalar, "triGetShadowSunFactor", TriGetShadowSunFactor));
    GGameState.NewNularOp(GameNular(GameScalar, "triGetShadowDepthCachedCount", TriGetShadowDepthCachedCount));
    GGameState.NewNularOp(GameNular(GameScalar, "triGetShadowFrozenCasters", TriGetShadowFrozenCasters));
    GGameState.NewNularOp(GameNular(GameString, "triGetShadowMapCacheTest", TriGetShadowMapCacheTest));
    GGameState.NewNularOp(GameNular(GameScalar, "triGetProxyVertCount", TriGetProxyVertCount));

    // Audio
    GGameState.NewNularOp(GameNular(GameScalar, "triGetAudioCacheEntries", TriGetAudioCacheEntries));
    GGameState.NewNularOp(GameNular(GameScalar, "triGetAudioCacheHits", TriGetAudioCacheHits));

    // Pixel
    GGameState.NewFunction(GameFunction(GameScalar, "triGetPixelMaxChannel", TriGetPixelMaxChannel, GameArray));
    GGameState.NewFunction(GameFunction(GameScalar, "triGetPixelMaxDiff", TriGetPixelMaxDiff, GameArray));
    GGameState.NewFunction(GameFunction(GameString, "triAssertPixelNotWhite", TriAssertPixelNotWhite, GameArray));

    // UI / controls
    GGameState.NewFunction(GameFunction(GameString, "triGetControlVisible", TriGetControlVisible, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triGetControlFocused", TriGetControlFocused, GameScalar));
    GGameState.NewFunction(GameFunction(GameString, "triGetControlEnabled", TriGetControlEnabled, GameScalar));
    GGameState.NewNularOp(GameNular(GameString, "triGetControllerScene", TriGetControllerScene));
    GGameState.NewNularOp(GameNular(GameString, "triGetControllerSection", TriGetControllerSection));
    GGameState.NewNularOp(GameNular(GameString, "triGetControllerPrompts", TriGetControllerPrompts));
    GGameState.NewNularOp(GameNular(GameString, "triGetInputContext", TriGetInputContext));
    GGameState.NewNularOp(GameNular(GameString, "triGetCommandMenuOpen", TriGetCommandMenuOpen));
    GGameState.NewNularOp(GameNular(GameString, "triGetDevPanelVisible", TriGetDevPanelVisible));
    GGameState.NewNularOp(GameNular(GameScalar, "triGetCameraEffectActive", TriGetCameraEffectActive));

    // Options
    GGameState.NewNularOp(GameNular(GameString, "triGetLanguage", TriGetLanguage));

    // MP / network / server
    GGameState.NewNularOp(GameNular(GameScalar, "triGetNgsState", TriGetNgsState));
    GGameState.NewNularOp(GameNular(GameScalar, "triGetNgsClientState", TriGetNgsClientState));
    GGameState.NewNularOp(GameNular(GameString, "triGetAdminLoggedIn", TriGetAdminLoggedIn));
    GGameState.NewNularOp(GameNular(GameScalar, "triGetServerBanCount", TriGetServerBanCount));
    GGameState.NewNularOp(GameNular(GameString, "triGetServerLocked", TriGetServerLocked));
    GGameState.NewNularOp(GameNular(GameScalar, "triGetNetSoundsReceived", TriGetNetSoundsReceived));
    GGameState.NewNularOp(GameNular(GameString, "triGetModsActiveSet", TriGetModsActiveSet));
    GGameState.NewNularOp(GameNular(GameString, "triGetModsMountSet", TriGetModsMountSet));
    GGameState.NewNularOp(GameNular(GameScalar, "triGetModsSortColumn", TriGetModsSortColumn));
    GGameState.NewNularOp(GameNular(GameString, "triGetActiveMods", TriGetActiveMods));
    GGameState.NewNularOp(GameNular(GameString, "triGetChatLines", TriGetChatLines));
}
