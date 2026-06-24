#include <SDL3/SDL_scancode.h>

#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/WorldInputContext.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Input/CheatCode.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>
#include <Poseidon/Graphics/Rendering/Frame/WorldFrameObserver.hpp>
#include <Poseidon/Audio/Speaker.hpp>
#include <stdint.h>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Memory/CheckMem.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Memtype.h>

using namespace Poseidon;
extern void SDLGamepad_SetEngine(float mag);
extern void SDLGamepad_PlayRamp(float beg, float end, float dur);
#include <Poseidon/Graphics/Rendering/Primitives/ClipVert.hpp>

#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/World/Terrain/Visibility.hpp>
#include <Random/randomGen.hpp>
#include <Poseidon/World/Scene/Camera/CamEffects.hpp>
#include <Poseidon/Game/TitEffects.hpp>
#include <Poseidon/Game/Scripting/Scripts.hpp>
#include <Poseidon/Dev/Debug/DebugCheats.hpp>
#include <Poseidon/Dev/Debug/DebugTrap.hpp>
#include <Poseidon/Dev/Diag/FrameProfiler.hpp>

#include <Poseidon/Dev/Diag/DiagModes.hpp>
#include <chrono>

#include <Poseidon/Core/Progress.hpp>

#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/World/Scene/Camera/CameraHold.hpp>
#include <Poseidon/World/Entities/Vehicles/Air/Airplane.hpp>
#include <Poseidon/World/Entities/Vehicles/Air/Helicopter.hpp>
#include <Poseidon/World/Entities/Vehicles/Ground/Car.hpp>
#include <Poseidon/World/Entities/Vehicles/Ground/Tank.hpp>
#include <Poseidon/World/Entities/Vehicles/Misc/Ship.hpp>

#include <Poseidon/Game/Commands/GameStateExt.hpp>

#include <Poseidon/AI/AI.hpp>

#include <Poseidon/Game/Chat.hpp>

#include <Poseidon/Network/Network.hpp>

#include <Poseidon/Foundation/Platform/AppConfig.hpp>
#include <Poseidon/Graphics/Cursor/ICursorOverlay.hpp>
#include <Poseidon/UI/Controls/UIControls.hpp>
#include <Poseidon/UI/Map/UIMap.hpp>
#include <Poseidon/UI/Map/UIMapCommon.hpp>

#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/IO/Serialization/ParamArchive.hpp>

#include <Poseidon/UI/Locale/StringtableExt.hpp>

#include <Poseidon/World/WorldShared.hpp>
AbstractOptionsUI* CreateChannelUI();

namespace Poseidon
{
#undef GetObject
#undef DrawText
// #undef LoadString

} // namespace Poseidon
#include <Poseidon/Core/resincl.hpp>
#include <Poseidon/Graphics/Cursor/ICursorOverlay.hpp>
using namespace Poseidon::Dev;
using Poseidon::Foundation::IsOutOfMemory;
namespace Poseidon
{
extern int gPerfDrawCalls;
}

// Time acceleration limits (PageUp/PageDown in-game)
constexpr double kTimeAccMin = 1.0;
constexpr double kTimeAccMax = 4.0;

#include <Poseidon/World/WorldSimHelpers.inc>

// Test/debug render-view override.  When active, World::Simulate forces the
// scene camera to an exact world-space transform instead of the player-derived
// view.  Driven by the `triSetView` test verb; lets a dev-cheats position dump
// reproduce a captured view without a mission camera object.
static bool s_triViewActive = false;
static Matrix4 s_triViewTransform(MIdentity);

void World_SetTriViewOverride(Vector3Par pos, Vector3Par dir, Vector3Par up)
{
    s_triViewTransform.SetDirectionAndUp(dir, up);
    s_triViewTransform.SetPosition(pos);
    s_triViewActive = true;
}

void World_ClearTriViewOverride()
{
    s_triViewActive = false;
}

void World::UpdateInputContext()
{
    InputSubsystem::Instance().SetContext(ResolveInputContext());
}

void World::Simulate(float deltaT, bool& enableDraw)
{
    // Frame-phase profiler — feeds the dev panel Perf tab and triPerfStats.
    Dev::FrameProfiler& perf = Dev::GFrameProfiler();
    perf.BeginFrame();
    float noAccDeltaT = deltaT;
    UpdateInputContext();
    auto& input = InputSubsystem::Instance();

    // Viewer-mode controls run BEFORE the rest of the simulation step
    // so the viewer can consume scancodes (Esc, F5, Space, R, O, ?)
    // before any game-side handler sees them.  No-op outside viewer mode.
    TickViewerControls(deltaT);

    OLink<Object> cameraVehicle = _cameraOn;
    OLink<Entity> camVehicle = dyn_cast<Entity, Object>(cameraVehicle);
    OLink<EntityAI> camAI = dyn_cast<EntityAI, Entity>(camVehicle);
    OLink<Person> person = FocusOn() ? FocusOn()->GetPerson() : nullptr;

    if (input.CheatActivated() == CheatCrash)
    {
        input.CheatServed();
        volatile int a = *(int*)nullptr;
        (void)a;
    }

    if (_editor)
    {
#if _ENABLE_CHEATS
        if (input.GetCheat1ToDo(SDL_SCANCODE_C))
        {
            AbstractOptionsUI* CreateDebugConsole();
            if (!_options)
                _options = CreateDebugConsole();
        }
#endif
    }
    else
    {
        ProcessNetwork();

#if _ENABLE_CHEATS
        if (input.GetCheat1ToDo(SDL_SCANCODE_R))
        {
            USER_CONFIG.easyMode = !USER_CONFIG.easyMode;
            Foundation::GlobalShowMessage(500, "%s", USER_CONFIG.easyMode ? "Cadet" : "Veteran");
        }
        if (input.GetCheat2ToDo(SDL_SCANCODE_Z))
        {
            ENGINE_CONFIG.super = !ENGINE_CONFIG.super;
            Foundation::GlobalShowMessage(500, "Immortality %s", ENGINE_CONFIG.super ? "On" : "Off");
        }
        if (input.GetCheat1ToDo(SDL_SCANCODE_M))
        {
            showCinemaBorder = !showCinemaBorder;
        }
        if (input.GetCheat2ToDo(SDL_SCANCODE_SLASH))
        {
            void ExportOperMaps(RString prefix);
            ExportOperMaps(Glob.header.worldname);
        }
#endif

        if (_mode == GModeNetware)
        {
            if (AppConfig::Instance().IsSimulateMode())
                _acceleratedTime = (float)AppConfig::Instance().GetTimeScale();
            else
                _acceleratedTime = 1;
        }
        else
#if !_ENABLE_CHEATS
            if (!_cameraEffect)
#endif
        {
            if (input.GetActionToDo(UATimeInc))
            {
                _acceleratedTime *= 2;
                saturate(_acceleratedTime, kTimeAccMin, kTimeAccMax);
                Foundation::GlobalShowMessage(1000, LocalizeString(IDS_TIME_ACC_FORMAT), _acceleratedTime);
                // SetActiveChannels();
            }
            if (input.GetActionToDo(UATimeDec))
            {
                _acceleratedTime *= 0.5;
                input.ConsumeKeyPress(SDL_SCANCODE_PAGEDOWN);
                saturate(_acceleratedTime, kTimeAccMin, kTimeAccMax);
                Foundation::GlobalShowMessage(1000, LocalizeString(IDS_TIME_ACC_FORMAT), _acceleratedTime);
                // SetActiveChannels();
            }
        }
        deltaT *= _acceleratedTime;
    }

    if (input.CheatActivated() == CheatGodMode)
    {
        if (_mode != GModeNetware)
            Dev::DebugCheats::Cmd_God::SetActive(true);
        input.CheatServed();
    }

    if (input.CheatActivated() == CheatSaveGame)
    {
        if (_mode != GModeNetware)
        {
            RString name = GetSaveDirectory() + RString("save.fps");
            GWorld->SaveBin(name, IDS_SAVE_GAME);
        }
        input.CheatServed();
    }
#if _ENABLE_CHEATS
    if (input.GetCheat1ToDo(SDL_SCANCODE_P))
    {
        _enableSimulation = !_enableSimulation;
    }
    if (input.GetCheat1ToDo(SDL_SCANCODE_S))
    {
        RString dir = GetTmpSaveDirectory();
        char filename[256];
        for (int i = 1; i <= 99999; i++)
        {
            snprintf(filename, sizeof(filename), "%sTMP%05d.fps", (const char*)dir, i);
            if (!QIFStream::FileExists(filename))
            {
                SaveBin(filename, IDS_SAVE_GAME);
                break;
            }
        }
    }
    if (input.GetCheat1ToDo(SDL_SCANCODE_L))
    {
        RString dir = GetTmpSaveDirectory();
        char filename[256];
        snprintf(filename, sizeof(filename), "%sTMP%05d.fps", (const char*)dir, 1);
        if (QIFStream::FileExists(filename))
        {
            // some binary save exist
            for (int i = 1; i <= 99999; i++)
            {
                snprintf(filename, sizeof(filename), "%sTMP%05d.fps", (const char*)dir, i);
                if (!QIFStream::FileExists(filename))
                {
                    if (i > 1)
                    {
                        snprintf(filename, sizeof(filename), "%sTMP%05d.fps", (const char*)dir, i - 1);
                        LoadBin(filename, IDS_LOAD_GAME);
                    }
                    break;
                }
            }
        }
        else
        {
            for (int i = 1; i <= 99999; i++)
            {
                snprintf(filename, sizeof(filename), "%sTMP%05d.sqg", (const char*)dir, i);
                if (!QIFStream::FileExists(filename))
                {
                    if (i > 1)
                    {
                        snprintf(filename, sizeof(filename), "%sTMP%05d.sqg", (const char*)dir, i - 1);
                        Load(filename, IDS_LOAD_GAME);
                    }
                    break;
                }
            }
        }
    }
    if (input.GetCheat2ToDo(SDL_SCANCODE_B))
    {
        DebugOperMapTrouble();
    }
    if (input.GetCheat2ToDo(SDL_SCANCODE_M))
    {
        DebugOperMap();
    }
    if (input.GetCheat2ToDo(SDL_SCANCODE_X))
    {
        EntityAI* veh = dyn_cast<EntityAI>(CameraOn());
        AIUnit* unit = veh ? veh->PilotUnit() : nullptr;
        DebugOperMap(unit);
    }
    if (input.GetCheat2ToDo(SDL_SCANCODE_PERIOD))
    {
        Pars.SaveBin("bin\\config.bin");
        Res.SaveBin("bin\\resource.bin");
    }
    if (input.GetCheat2ToDo(SDL_SCANCODE_BACKSLASH))
    {
        forceControlsPaused = !forceControlsPaused;
        if (forceControlsPaused)
            GEngine->ShowMessage(500, "controls paused on");
        else
            GEngine->ShowMessage(500, "controls paused off");
    }
    const static int cheatVar[] = {SDL_SCANCODE_0, SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4,
                                   SDL_SCANCODE_5, SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9};
    for (int i = 0; i < sizeof(cheatVar) / sizeof(*cheatVar); i++)
    {
        if (input.GetCheat1ToDo(cheatVar[i]))
        {
            char varName[64];
            snprintf(varName, sizeof(varName), "cheat%d", i);
            GetGameState()->VarSet(varName, true, false);
            break;
        }
    }
#endif

    Glob.uiTime += noAccDeltaT;
    if (IsSimulationEnabled())
    {
        Glob.time += deltaT;
    }

    // multiplayer chat control
    if (GetNetworkManager().GetGameState() >= NGSCreate)
    {
        if (IsPlayerDead())
        {
            if (ActualChatChannel() != CCGlobal)
            {
                SetChatChannel(CCGlobal);
                if (_channel)
                {
                    _channel->ResetHUD();
                }
                if (_voiceChat)
                {
                    _voiceChat->ResetHUD();
                }
                OnChannelChanged();
            }
        }
        else
        {
            if (input.GetActionToDo(UAPrevChannel, true, false))
            {
                PrevChatChannel();
                if (_channel)
                {
                    _channel->ResetHUD();
                }
                if (_voiceChat)
                {
                    _voiceChat->ResetHUD();
                }
                OnChannelChanged();
            }

            if (input.GetActionToDo(UANextChannel, true, false))
            {
                NextChatChannel();
                if (_channel)
                {
                    _channel->ResetHUD();
                }
                if (_voiceChat)
                {
                    _voiceChat->ResetHUD();
                }
                OnChannelChanged();
            }
        }

        if (input.GetActionToDo(UAChat, true, false))
        {
            CreateChat();
        }

        if (!_voiceChat && input.GetActionToDo(UAVoiceOverNet, true, false))
        {
            CreateVoiceChat();
        }
    }

    if (_chat || _voiceChat || _channelChanged >= Glob.uiTime - 3.0f)
    {
        if (!_channel)
        {
            _channel = CreateChannelUI();
        }
    }
    else
    {
        if (_channel)
        {
            _channel = nullptr;
        }
    }

    DisplayMap* map = static_cast<DisplayMap*>((AbstractOptionsUI*)_map);
    _showCompass = map && map->IsShownCompass() && input.GetAction(UACompass) > 0;
    _showWatch = map && map->IsShownWatch() && input.GetAction(UAWatch) > 0;

    bool enableOptics = false;
    bool forceOptics = false;

    if (camAI && !_cameraEffect)
    {
        if (!camAI->DisableWeapons())
        {
            if (person)
            {
                enableOptics = camAI->GetOpticsModel(person) != nullptr;
                forceOptics = camAI->GetForceOptics(person);
            }
        }
    }

    bool isNV = GEngine->GetNightVision();
    bool enableNV = false;
    bool wantNV = false;
    if (person)
    {
        enableNV = person->IsNVEnabled();
        wantNV = person->IsNVWanted();
    }
    if (!_cameraEffect && IsSimulationEnabled())
    {
        if (!HasMap() || _map->IsTopmost())
        {
            if (input.GetActionToDo(UAPersonView))
            {
                if (_cameraExternal)
                {
                    if (_camTypeMain == CamExternal && !_showMap)
                    {
                        _cameraExternal = false;
                        _camTypeMain = CamInternal;
                    }
                    else
                    {
                        _camTypeMain = CamExternal;
                    }
                }
                else
                {
                    if (_camTypeMain == CamInternal && !_showMap)
                    {
                        _cameraExternal = true;
                        _camTypeMain = CamExternal;
                    }
                    else
                    {
                        _camTypeMain = CamInternal;
                    }
                }
                _showMap = false;
            }
            if (input.GetActionToDo(UAOptics))
            {
                if (_camTypeMain == CamGunner && !_showMap)
                {
                    if (_cameraExternal)
                    {
                        _camTypeMain = CamExternal;
                    }
                    else
                    {
                        _camTypeMain = CamInternal;
                    }
                }
                else
                {
                    _camTypeMain = CamGunner;
                }
                _showMap = false;
            }
            if (input.GetActionToDo(UATacticalView))
            {
                if (_camTypeMain == CamGroup && !_showMap)
                {
                    if (_cameraExternal)
                    {
                        _camTypeMain = CamExternal;
                    }
                    else
                    {
                        _camTypeMain = CamInternal;
                    }
                }
                else if (FocusOn() && FocusOn()->IsGroupLeader() && FocusOn()->GetGroup()->NUnits() > 1)
                {
                    _camTypeMain = CamGroup;
                    if (UI())
                    {
                        UI()->ShowMe();
                    }
                }
                _showMap = false;
            }
            if (input.GetActionToDo(UAMap))
            {
                if (_showMap)
                {
                    _showMap = false;
                }
                else
                {
                    if (_map)
                    {
                        _map->ResetHUD();
                    }
                    _showMap = true;
                }
            }
        }
    }
    if (_cameraEffect)
    {
        _showMap = false;
    }

    bool enableExternal = USER_CONFIG.IsEnabled(DT3rdPersonView);
    if (!enableExternal)
    {
        _cameraExternal = false;
        if (_camTypeMain == CamExternal)
        {
            _camTypeMain = CamInternal;
        }
    }

    bool forcedZoom = false;
    if (FocusOn())
    {
#if _ENABLE_CHEATS
        static bool enableAnyCamera = false;
        if (input.GetCheat2ToDo(SDL_SCANCODE_P))
        {
            enableAnyCamera = !enableAnyCamera;
        }
        if (enableAnyCamera)
        {
            _camTypeMain = CamGroup;
            goto CameraOK;
        }
#endif
        if (_camTypeMain == CamGroup)
        {
            if (FocusOn()->IsGroupLeader() && FocusOn()->GetGroup()->NUnits() > 1 && enableExternal)
            {
                goto CameraOK;
            }
            else if (_cameraExternal)
            {
                _camTypeMain = CamExternal;
            }
            else
            {
                _camTypeMain = CamInternal;
            }
        }
        // _camTypeMain != CamGroup

    CameraOK:

        _camType = _camTypeMain;
        if (_camType == CamGunner)
        {
            if (enableOptics)
            {
            }
            else if (_cameraExternal)
            {
                _camType = CamExternal;
            }
            else
            {
                _camType = CamInternal;
            }
        }
        // _camType != CamGunner
        if ((_camType == CamInternal || _camType == CamExternal) && forceOptics)
        {
            _camType = CamGunner;
        }
        if (input.GetAction(UALockTarget) && (_camType == CamInternal || _camType == CamExternal))
        {
            EntityAI* veh = FocusOn()->GetVehicle();
            PoseidonAssert(veh);
            int curWeapon = veh->SelectedWeapon();
            bool allowZoom = true;
            if (curWeapon >= 0 && curWeapon < veh->NMagazineSlots())
            {
                const MagazineSlot& slot = veh->GetMagazineSlot(curWeapon);
                const MuzzleType* muzzle = slot._muzzle;
                bool canLock =
                    muzzle->_canBeLocked == 2 || muzzle->_canBeLocked == 1 && USER_CONFIG.IsEnabled(DTAutoGuideAT);
                if (canLock)
                {
                    allowZoom = false;
                }
            }
            if (allowZoom)
            {
                // zoom in
                forcedZoom = true;
            }
        }
    }
    else
    {
        if (_cameraExternal)
        {
            _camTypeMain = CamExternal;
        }
        else
        {
            _camTypeMain = CamInternal;
        }
        _camType = _camTypeMain;
        if (input.GetAction(UALockTarget) && (_camType == CamInternal || _camType == CamExternal))
        {
            // zoom in
            forcedZoom = true;
        }
    }

    if (!IsSimulationEnabled() || _showMap || !enableNV)
    {
        wantNV = false;
    }

    {
        if (isNV != wantNV)
        {
            GEngine->SetNightVision(wantNV);
            _scene.MainLightChanged();
        }
    }

    {
        SimulateLandscape(deltaT);

#if _ENABLE_CHEATS
        {
            if (input.GetCheat1(SDL_SCANCODE_U))
            {
                _actualOvercast += noAccDeltaT * 0.1 * input.GetCheat1(SDL_SCANCODE_U);
                saturate(_actualOvercast, 0, 1);
                _wantedOvercast = _actualOvercast;
                GLOB_LAND->SetOvercast(_actualOvercast);
            }
            if (input.GetCheat1(SDL_SCANCODE_I))
            {
                _actualOvercast -= noAccDeltaT * 0.1 * input.GetCheat1(SDL_SCANCODE_I);
                saturate(_actualOvercast, 0, 1);
                _wantedOvercast = _actualOvercast;
                GLOB_LAND->SetOvercast(_actualOvercast);
            }
            if (input.GetCheat1(SDL_SCANCODE_COMMA))
            {
                _actualFog += noAccDeltaT * 0.1 * input.GetCheat1(SDL_SCANCODE_COMMA);
                saturate(_actualFog, 0, 1);
                _wantedFog = _actualFog;
                GLOB_LAND->SetFog(_actualFog);
            }
            if (input.GetCheat1(SDL_SCANCODE_PERIOD))
            {
                _actualFog -= noAccDeltaT * 0.1 * input.GetCheat1(SDL_SCANCODE_PERIOD);
                saturate(_actualFog, 0, 1);
                _wantedFog = _actualFog;
                GLOB_LAND->SetFog(_actualFog);
            }

#if _ENABLE_CHEATS
            if (!_showMap && input.GetCheat1ToDo(SDL_SCANCODE_V))
            {
                // cycle through diagnostic modes
                if (DiagMode != 0)
                {
                    DiagMode = 0;
                    Foundation::GlobalShowMessage(100, "Diag off");
                }
                else
                {
                    DiagMode = (1 << DECombat) | (1 << DEPath);
                    Foundation::GlobalShowMessage(100, "Diag: Combat + Path");
                }
            }
#endif

            if (!_showMap && input.GetCheat1ToDo(SDL_SCANCODE_X))
            {
                DisableTextures = !DisableTextures;
            }
        }

        if (input.GetCheat1ToDo(SDL_SCANCODE_J))
        {
            BrowseCamera(-1);
            InitCameraPars();
        }
        if (input.GetCheat1ToDo(SDL_SCANCODE_K))
        {
            BrowseCamera(+1);
            InitCameraPars();
        }
        if (input.GetCheat2ToDo(SDL_SCANCODE_L))
        {
            for (int i = 0; i < NAnimals(); i++)
            {
                Entity* vehicle = GetAnimal(i);
                if (!vehicle->GetName())
                    continue;
                if (!strcmpi(vehicle->GetName(), "SeaGull"))
                {
                    BrowseCamera(vehicle);
                    break;
                }
            }
        }
        if (input.GetCheat2ToDo(SDL_SCANCODE_K))
        {
            QOFStream out;
            Object* camOn = _cameraOn;
            if (camOn)
            {
                char buf[1024];
                Vector3Val pos = camOn->Position();
                Vector3Val dir = camOn->Direction();
                float posSY = GLandscape->SurfaceYAboveWater(pos.X(), pos.Z());
                snprintf(buf, sizeof(buf), "'%s' setPos [%.3f,%.3f,%.3f]\r\n", (const char*)camOn->GetDebugName(),
                         pos.X(), pos.Z(), pos.Y() - posSY);
                out.write(buf, strlen(buf));
                float head = atan2(dir.X(), dir.Z());
                snprintf(buf, sizeof(buf), "'%s' setDir %.0f\r\n", (const char*)camOn->GetDebugName(),
                         head * (180 / H_PI));
                out.write(buf, strlen(buf));
                out.export_clip("clipboard.txt");
            }
        }

        if (input.GetCheat1ToDo(SDL_SCANCODE_F))
        {
            int fps = GLOB_ENGINE->ShowFps() + 1;
            if (fps > 3)
                fps = 0;
            GLOB_ENGINE->ToggleFps(fps);
        }
#endif
    }

    Entity* camInsideVehicle = nullptr;

    if (_cameraOn != nullptr)
    {
#if _ENABLE_CHEATS
        bool manToggle = input.GetCheat1ToDo(SDL_SCANCODE_SCROLLLOCK);
#endif
        bool manual = false;
        {
            Object* camOn = _cameraOn;
            EntityAI* ai = dyn_cast<EntityAI>(camOn);
            if (ai)
            {
                manual = ai->QIsManual();
#if _ENABLE_CHEATS
                if (manToggle)
                {
                    manual = !manual;
                    if (PlayerOn())
                    {
                        Log("SetManual(%d) %s", manual, (const char*)PlayerOn()->GetDebugName());
                    }
                    if (manual)
                    {
                        Transport* transp = dyn_cast<Transport>(ai);
                        if (transp && transp->Driver())
                        {
                            SwitchPlayerTo(transp->Driver());
                        }
                        else
                        {
                            Person* soldier = dyn_cast<Person>(ai);
                            PoseidonAssert(soldier);
                            SwitchPlayerTo(soldier);
                        }
                    }
                    else
                    {
                        AIUnit* unit = ai->CommanderUnit();
                        if (unit)
                            DoVerify(unit->SetState(AIUnit::Wait));
                    }
                    SetPlayerManual(manual);
                }
                KeyState.SetScrollLock(!manual);
#endif
            }
            else
            {
                CameraHolder* camHolder = dyn_cast<CameraHolder, Object>(_cameraOn);
                if (camHolder)
                {
                    manual = camHolder->GetManual();
#if _ENABLE_CHEATS
                    if (manToggle)
                    {
                        manual = !manual;
                        camHolder->SetManual(manual);
                    }
                    KeyState.SetScrollLock(!manual);
#endif
                }
            }
        }
    }

#if _ENABLE_CHEATS
    if (input.GetCheat2ToDo(SDL_SCANCODE_R))
    {
        _noDisplay = !_noDisplay;
    }
#endif

    if (IsSimulationEnabled())
    {
        if (_titleEffect && _titleEffect->IsTerminated())
        {
            _titleEffect.Free();
            Log("_titleEffect.Free()");
        }

        if (_cutEffect && _cutEffect->IsTerminated())
        {
            _cutEffect.Free();
        }

        if (_titleEffect)
        {
            _titleEffect->Simulate(deltaT);
        }
        if (_cutEffect)
        {
            _cutEffect->Simulate(deltaT);
        }
    }

    if (camVehicle)
    {
        if (IsSimulationEnabled())
        {
            FFEffects eff;
            camVehicle->PerformFF(eff);
            SDLGamepad_SetEngine(eff.engineMag);
        }
        else
        {
            SDLGamepad_SetEngine(0);
        }
    }

    {
        SimulateScripts();

        if (_cameraEffect && _cameraEffect->IsTerminated())
        {
            _cameraEffect.Free();
            _playerSuspended = false;
        }

        Camera& camera = *_scene.GetCamera();
        float fov = 0.7;
        Matrix4 transform = camera.Transform();
        float cameraRotate = 0;

        if (_cameraEffect)
        {
            _cameraEffect->Simulate(deltaT);
            fov = _cameraEffect->GetFOV();
            if (fov < 0)
            { // default fov
                Object* object = _cameraEffect->GetObject();
                fov = object ? object->CamEffectFOV() : 0.7f;
            }
            transform = _cameraEffect->GetTransform();

            if (_cameraEffect->IsInside())
            {
                camInsideVehicle = dyn_cast<Entity, Object>(_cameraEffect->GetObject());
            }
        }
        else if (cameraVehicle)
        {
            fov = _camFOV[_camType];
            transform = cameraVehicle->Transform();
            // if any vehicle control key is pressed, set to manual control
            // check vehicle control keys

            if (!HasOptions())
            {
                cameraVehicle->SimulateHUD(_camType, deltaT);
                bool isVirtual = cameraVehicle->IsVirtual(_camType);
                //// isGunner was: cameraVehicle->IsGunner(_camType);
                bool isGunner = cameraVehicle->IsGunner(_camType);
                if (isGunner || isVirtual)
                { // gunner camera
                    // if( !isGunner || camAI->GetType()->IsKindOf(GWorld->Preloaded(VTypeMan)) )
                    if (_ui && !_showMap)
                    {
                        _ui->SetCursorMode(cameraVehicle->GetCursorRelMode(_camType) == CMouseAbs);
                        Vector3 cursorDir = _ui->GetCursorDirection();
                        float scale = _camFOV[_camType];
                        float moveX = input.ConsumeCursorDeltaX() * scale;
                        float moveY = -input.ConsumeCursorDeltaY() * scale;
                        const float maxRotX = H_PI / 2;
                        const float maxRotY = H_PI / 2;
                        saturate(moveX, -maxRotX, +maxRotX);
                        saturate(moveY, -maxRotY, +maxRotY);
                        Vector3 rot(moveX, moveY, 0);
                        // user moves cursor in camera space
                        // we need to convert it into world space
                        Vector3 curCam = GScene->GetCamera()->Transform().Rotate(rot);

                        // Matrix3 cursorOrient(MDirection,cursorDir,VUp);
                        // cursorDir=cursorOrient*rot;
                        cursorDir += curCam;
                        cursorDir.Normalize();

                        cameraVehicle->LimitCursorHard(_camType, cursorDir);
                        cursorDir.Normalize();

                        _ui->SetCursorDirection(cursorDir);
                    }
                }

                if (cameraVehicle->IsVirtual(_camType))
                {
                    const float diag = 0.5 * H_SQRT2;
                    float headSpeed = (input.GetAction(UALookLeft) + input.GetAction(UALookLeftUp) * diag +
                                       input.GetAction(UALookLeftDown) * diag - input.GetAction(UALookRight) -
                                       input.GetAction(UALookRightUp) * diag - input.GetAction(UALookRightDown) * diag);
                    float diveSpeed = (input.GetAction(UALookDown) + input.GetAction(UALookLeftDown) * diag +
                                       input.GetAction(UALookRightDown) * diag - input.GetAction(UALookUp) -
                                       input.GetAction(UALookRightUp) * diag - input.GetAction(UALookLeftUp) * diag);
                    float headChange = noAccDeltaT * headSpeed;
                    float diveChange = noAccDeltaT * diveSpeed;
                    if (!cameraVehicle->IsContinuous(_camType) && !cameraVehicle->IsExternal(_camType) &&
                        (cameraVehicle->GetCursorRelMode(_camType) == CKeyboard || input.IsJoystickActive()))
                    {
                        headChange = 0;
                        // discrete movement
                        // float dummy=0;
                        float initDive, initHead, initFOV;
                        cameraVehicle->InitVirtual(_camType, initHead, initDive, initFOV);
                        if (input.GetAction(UALookLeftUp))
                        {
                            _camHeadingWanted[_camType] = initHead + H_PI / 4;
                            diveChange = 0;
                        }
                        else if (input.GetAction(UALookLeft))
                        {
                            _camHeadingWanted[_camType] = initHead + H_PI / 2;
                            diveChange = 0;
                        }
                        else if (input.GetAction(UALookLeftDown))
                        {
                            _camHeadingWanted[_camType] = initHead + H_PI * 0.99;
                            diveChange = 0;
                        }
                        else if (input.GetAction(UALookRightUp))
                        {
                            _camHeadingWanted[_camType] = initHead - H_PI / 4;
                            diveChange = 0;
                        }
                        else if (input.GetAction(UALookRight))
                        {
                            _camHeadingWanted[_camType] = initHead - H_PI / 2;
                            diveChange = 0;
                        }
                        else if (input.GetAction(UALookRightDown))
                        {
                            _camHeadingWanted[_camType] = initHead - H_PI * 0.99;
                            diveChange = 0;
                        }
                        else
                        {
                            _camHeadingWanted[_camType] = initHead;
                        }
                    }
                    if (input.GetActionToDo(UALookCenter))
                    {
                        cameraVehicle->InitVirtual(_camType, _camHeadingWanted[_camType], _camDiveWanted[_camType],
                                                   _camFOVWanted[_camType]);
                        _camNear[_camType] = 1.0;
                        _camMaxDist[_camType] = 1e10;
                        headChange = 0;
                        diveChange = 0;

                        if (_ui)
                        {
                            // cursor must be set to the screen center
                            _ui->SetCursorDirection(camera.Direction());
                            _ui->SetCursorMode(false);
                        }
                    }
                    // if mouse cursor is on screen edge, rotate view
                    if (headChange || diveChange)
                    {
                        // keep ui cursor in screen range
                        if (_ui)
                        {
                            cameraRotate = fabs(headSpeed) + fabs(diveSpeed);
                            _camHeading[_camType] += headChange;
                            _camDive[_camType] += diveChange;
                        }
                        _camHeadingWanted[_camType] = _camHeading[_camType];
                        _camDiveWanted[_camType] = _camDive[_camType];
                    }
                    if (_ui && !_showMap)
                    {
                        bool cursorMode = _ui->GetCursorMode();
                        _ui->SetCursorMode(false);
                        if (cursorMode && !ENGINE_CONFIG.landEditor)
                        {
                            // rotate camera so that cursor stays in the neutral zone
                            Vector3 curDir = _ui->GetCursorDirection();
                            cameraVehicle->LimitCursor(GetCameraType(), curDir);

                            Matrix4Val camInvTransform = camera.GetInvTransform();

                            Vector3 pos = camInvTransform.Rotate(curDir);
                            float cursorX = 0, cursorY = 0;
                            if (pos.Z() > 0)
                            {
                                float invZ = 1.0 / pos.Z();

                                cursorX = pos.X() * invZ * camera.InvLeft();
                                cursorY = -pos.Y() * invZ * camera.InvTop();

                                saturate(cursorX, -0.95, +0.95);
                                saturate(cursorY, -0.95, +0.95);
                            }

                            if (cameraVehicle->IsVirtualX(_camType))
                            {
                                KeepNZone(_camHeading[_camType], cursorX, 0, 0.8, camera.Left());
                                _camHeadingWanted[_camType] = _camHeading[_camType];
                            }
                            else
                            {
                                float initDive, initHead, initFOV;
                                cameraVehicle->InitVirtual(_camType, initHead, initDive, initFOV);
                                _camHeadingWanted[_camType] = initHead;
                            }
                            KeepNZone(_camDive[_camType], -cursorY, 0, 0.5, camera.Top());
                            _camDiveWanted[_camType] = _camDive[_camType];
                        }
                        Vector3 cursor = _ui->GetModelCursor();

                        saturateMax(cursor[2], 0.01);
                        cursor *= 1 / cursor[2];
                        float xLimit = camera.Left();
                        float yLimit = camera.Top();
                        if (!cursorMode)
                        {
                            xLimit *= 0.8, yLimit *= 0.8;
                        }
                        saturate(cursor[0], -xLimit, +xLimit);
                        saturate(cursor[1], -yLimit, +yLimit);
                        cursor.Normalize();

                        _ui->SetModelCursor(cursor);
                        // return cursor mode
                        _ui->SetCursorMode(cursorMode);
                    }
                    cameraVehicle->LimitVirtual(_camType, _camHeading[_camType], _camDive[_camType], _camFOV[_camType]);
                    cameraVehicle->LimitVirtual(_camType, _camHeadingWanted[_camType], _camDiveWanted[_camType],
                                                _camFOVWanted[_camType]);
                }
            }
            if (cameraVehicle->IsExternal(_camType))
            {
                float expChange = input.GetAction(UAZoomOut) - input.GetAction(UAZoomIn);
                if (expChange)
                {
                    float change = pow(ZoomSpeed, expChange * noAccDeltaT);
                    _camNear[_camType] *= change;
                    if (!ENGINE_CONFIG.landEditor)
                    {
                        saturate(_camNear[_camType], 0.25, 4);
                    }
                    else
                    {
                        saturate(_camNear[_camType], 0.01, 100);
                    }
                }
            }
            else
            {
                if (cameraVehicle->IsContinuous(_camType))
                {
                    float expChange = input.GetAction(UAZoomOut) - input.GetAction(UAZoomIn);
                    if (expChange)
                    {
                        float change = pow(ZoomSpeed, expChange * noAccDeltaT);
                        _camFOV[_camType] *= change;
                        _camFOVWanted[_camType] = _camFOV[_camType];
                        cameraVehicle->LimitVirtual(_camType, _camHeading[_camType], _camDive[_camType],
                                                    _camFOV[_camType]);
                        cameraVehicle->LimitVirtual(_camType, _camHeadingWanted[_camType], _camDiveWanted[_camType],
                                                    _camFOVWanted[_camType]);
                    }
                }
                else
                {
                    float initHead, initDive, initFOV;
                    cameraVehicle->InitVirtual(_camType, initHead, initDive, initFOV);
                    if (input.GetAction(UAZoomIn) || forcedZoom)
                    {
                        initFOV *= 0.25;
                    }
                    else if (input.GetAction(UAZoomOut))
                    {
                        initFOV *= 4;
                    }
                    cameraVehicle->LimitVirtual(_camType, initHead, initDive, initFOV);
                    _camFOVWanted[_camType] = initFOV;
                }
            }

            // if it is, you can use AngleDifference instead of operator -
            float delta;
            delta = _camHeadingWanted[_camType] - _camHeading[_camType];
            saturate(delta, -4 * deltaT, +4 * deltaT);
            _camHeading[_camType] += delta;

            delta = _camDiveWanted[_camType] - _camDive[_camType];
            saturate(delta, -2 * deltaT, +2 * deltaT);
            _camDive[_camType] += delta;

            delta = _camFOVWanted[_camType] / _camFOV[_camType];

            float changeMax = pow(ZoomSpeed, noAccDeltaT);
            saturate(delta, 1 / changeMax, changeMax);
            _camFOV[_camType] *= delta;

            _camMaxDist[_camType] += deltaT;
            saturateMin(_camMaxDist[_camType], 1e10);

            {
                Matrix3 camChange = CameraChange(_camHeading[_camType], _camDive[_camType]);
                switch (_camType)
                {
                    case CamGunner:
                    {
                        transform = cameraVehicle->Transform() * cameraVehicle->InsideCamera(_camType);
                        camInsideVehicle = dyn_cast<Entity, Object>(cameraVehicle);
                    }
                    break;
                    default: // case CamInternal:
                    {
                        transform = cameraVehicle->Transform() * cameraVehicle->InsideCamera(_camType);
                        camInsideVehicle = dyn_cast<Entity, Object>(cameraVehicle);
                        transform.SetOrientation(transform.Orientation() * camChange);
                    }
                    break;
                    case CamExternal:
                    {
                        Matrix3 vehOrient;
                        Vector3Val dist = cameraVehicle->ExternalCameraPosition(_camType);
                        Vector3 dir = cameraVehicle->GetCameraDirection(_camType);
                        vehOrient.SetUpAndDirection(VUp, dir);
                        vehOrient = vehOrient * camChange;
                        transform.SetOrientation(vehOrient);
                        Vector3 focPos = cameraVehicle->CameraPosition();
                        Vector3 camPos = vehOrient * dist + focPos;
                        ClipCamera(camPos, cameraVehicle, focPos, _camMaxDist[_camType]);
                        transform.SetPosition(camPos);
                        camChange = M3Identity;
                        camInsideVehicle = nullptr;
                    }
                    break;
                    case CamGroup:
                    {
                        Matrix3 vehOrient;
                        vehOrient.SetUpAndDirection(VUp, _cameraOn->Direction());
                        float dist = cameraVehicle->OutsideCameraDistance(_camType) * _camNear[_camType];
                        Matrix3Val orient = ENGINE_CONFIG.landEditor ? camChange : vehOrient * camChange;
                        transform.SetOrientation(orient);
                        Vector3 focPos = cameraVehicle->CameraPosition();
                        Vector3 camPos = focPos - orient.Direction() * dist;
                        transform.SetPosition(camPos);
                        camChange = M3Identity;
                        camInsideVehicle = nullptr;
                    }
                    break;
                }
            }
        }
        {
            Vector3 camPos = transform.Position();
            float minCamY = _scene.GetLandscape()->SurfaceYAboveWater(camPos.X(), camPos.Z());
            minCamY += 0.1;
            if (camPos.Y() < minCamY)
            {
                camPos[1] = minCamY;
            }
            transform.SetPosition(camPos);
        }
        if (s_triViewActive)
        {
            transform = s_triViewTransform;
        }
        camera.SetTransform(transform);

        if (cameraVehicle)
        {
            camera.SetSpeed(cameraVehicle->ObjectSpeed());
        }
        else
        {
            camera.SetSpeed(VZero);
        }
        if (camVehicle)
        {
            float visualSpeed = camVehicle->Speed().SquareSize() * (1.0 / 900.0);
            float visualRotate = camVehicle->AngVelocity().SquareSize() * (2.5);
            saturateMax(visualSpeed, visualRotate + cameraRotate);
            Glob.dropDown = visualSpeed;
            saturate(Glob.dropDown, 0, 1);
        }
        Glob.fullDropDown += Glob.fullDropDownChange;
        Glob.fullDropDown -= noAccDeltaT * 0.33;
        saturate(Glob.fullDropDown, 0, 1);
        Glob.fullDropDownChange = 0;

        // normal soldier fov is about 0.85
        float cNear = 0.067f / fov;
        saturate(cNear, 0.07f, 0.2f);
        camera.SetPerspectiveForView(GEngine, cNear, _scene.GetFogMaxRange(), fov);
        camera.Adjust(GEngine);
    }

#if BACKGROUND_AI
    SecondaryContext context;
    context.deltaT = deltaT;
    context.noAccDeltaT = noAccDeltaT;
    context.cameraVehicle = camInsideVehicle;
    context.insideVehicle = camInsideVehicle != nullptr;
    context.world = this;
#endif

    bool clear = true;

    if (_warningMessage)
    {
        _warningMessage->OnSimulate(nullptr);
        if (_warningMessage->GetExitCode() >= 0)
        {
            _warningMessage = nullptr;
        }
    }
    else
    {
        if (_voiceChat)
        {
            _voiceChat->SimulateHUD(nullptr);
        }
        if (!HasOptions() && _cameraOn != nullptr)
        {
            if (_userDlg)
            {
                _userDlg->SimulateHUD(nullptr);
            }
            else if (_map && _showMap)
            {
                _map->SimulateHUD(camAI);
            }
        }
        if (_options)
        {
            _options->SimulateHUD(nullptr);
        }
        if (!HasOptions() && _cameraOn != nullptr && !_userDlg)
        {
            if (_ui && IsUIEnabled())
            {
                if (camAI)
                {
                    _ui->SimulateHUD(*_scene.GetCamera(), camAI, _camType, deltaT);
                }
                else if (camVehicle)
                {
                    _ui->SimulateHUDNonAI(*_scene.GetCamera(), camVehicle, _camType, deltaT);
                }
            }
        }
    }
    input.ConsumeCursorScroll();

    bool quiet = false;
    // bool isSimEnabled = IsSimulationEnabled();
    // bool simEnabledEdge = isSimEnabled && !wasSimEnabled;

    if (_firstFrame /*|| simEnabledEdge*/)
    {
        // LOG_DEBUG(World, "_firstFrame {}, simEnabledEdge {}",_firstFrame,simEnabledEdge);
        enableDraw = false, _firstFrame = false, quiet = true;

        // Static landscape objects (street lamps) evaluated their on/off state
        // against the world's default daytime clock during InitLandscape, before
        // the mission start time was applied.  Re-broadcast a time-skip on the
        // first simulated frame so they sync to the actual mission clock — auto
        // lamps would otherwise stay dark until they happened to re-simulate
        // (which is why they only appeared after a mission retry).
        if (GLandscape)
        {
            GLandscape->OnTimeSkipped();
        }
    }
    if (enableDraw)
    {
        enableDraw = GEngine->IsAbleToDraw();
    }
    perf.Mark(Dev::FrameProfiler::PhaseSetup);
    if (enableDraw)
    {
        PackedColor color(GEngine->FogColor());
        // Viewer mode: solid charcoal background — no fog-color cast on the model
        // (same reasoning as Blender/Maya default mid-grey backgrounds).
        if (AppConfig::Instance().IsViewerMode())
            color = PackedColor(Color(0.18f, 0.18f, 0.20f, 1.0f));
        GEngine->InitDraw(clear, color);
    }

    bool doSim = IsSimulationEnabled();

#if BACKGROUND_AI
    if (doSim)
    {
        _secThread->StartSecondary(DoBackgroundSimulate, &context);
    }
#endif

    if (enableDraw)
    {
        _scene.BeginObjects();
        DrawViewerSceneAddons();
    }

    if (camInsideVehicle)
    {
        _scene.SelectActiveLights(camInsideVehicle);
    }
    else
    {
        _scene.SelectActiveLights(nullptr);
    }
    perf.Mark(Dev::FrameProfiler::PhaseDrawInit);

    if (_scene.GetLandscape())
    {
        // remove all vehicles that should be removed
        MoveOutAndDelete(_vehicles, 0);
        MoveOutAndDelete(_animals, 0);
        MoveOutAndDelete(_fastVehicles, 0);
        MoveOutAndDelete(_buildings, 0);

        if (enableDraw)
        {
            if (!_showMap && IsDisplayEnabled())
            {
                {
                    LandBegEnd objBegEnd;
                    Landscape::CalculBoundingRect(objBegEnd, *_scene.GetCamera(), _scene.GetFogMaxRange(), ObjGrid);

                    int x, z;
                    int xMin = objBegEnd.xBeg, xMax = objBegEnd.xEnd;
                    int zMin = objBegEnd.zBeg, zMax = objBegEnd.zEnd;
                    if (GScene->GetObjectShadows() || GScene->GetVehicleShadows())
                    {
#define SHADOW_BORDER 2
                        xMin -= SHADOW_BORDER, xMax += SHADOW_BORDER;
                        zMin -= SHADOW_BORDER, zMax += SHADOW_BORDER;
                    }
                    if (xMin < 0)
                    {
                        xMin = 0;
                    }
                    if (xMin > ObjRange - 1)
                    {
                        xMin = ObjRange - 1;
                    }
                    if (xMax < 0)
                    {
                        xMax = 0;
                    }
                    if (xMax > ObjRange - 1)
                    {
                        xMax = ObjRange - 1;
                    }
                    if (zMin < 0)
                    {
                        zMin = 0;
                    }
                    if (zMin > ObjRange - 1)
                    {
                        zMin = ObjRange - 1;
                    }
                    if (zMax < 0)
                    {
                        zMax = 0;
                    }
                    if (zMax > ObjRange - 1)
                    {
                        zMax = ObjRange - 1;
                    }
#define RECT_CLIPPERS 1
                    for (z = zMin; z <= zMax; z++)
                    {
                        for (x = xMin; x <= xMax; x++)
                        {
                            // build if necessary
                            const ObjectList& list = GLandscape->GetObjects(z, x);
                            if (list.Null())
                            {
                                continue;
                            }
                            Vector3Val bCenter = list->GetBSphereCenter();
                            float bRadius = list->GetBSphereRadius();
                            // Point3 cPos(VFastTransform,GScene->ScaledInvTransform(),bCenter);
                            const Camera& cam = *GScene->GetCamera();
#if RECT_CLIPPERS
                            ClipFlags andClip = cam.IsClipped(bCenter, bRadius, 1);
                            if (andClip && list->GetNonStaticCount() <= 0)
                            {
                                continue;
                            }
#endif
                            ClipFlags orClip = cam.MayBeClipped(bCenter, bRadius, 1);
                            int n = list.Size();
                            for (int i = 0; i < n; i++)
                            {
                                Object* obj = list[i];
                                PoseidonAssert(obj);
                                if (obj->Invisible())
                                {
                                    continue;
                                }
                                ClipFlags clip = orClip;
#if RECT_CLIPPERS
                                if (obj->Static())
                                {
                                    if (andClip)
                                    {
                                        continue;
                                    }
                                }
                                else
                                {
                                    clip = ClipAll;
                                }
#endif
                                if (obj == camInsideVehicle)
                                {
                                    _scene.ObjectForDrawing(obj, obj->InsideLOD(_camType), clip);
#if _ENABLE_CHEATS
                                    if (DiagMode)
                                    {
                                        obj->DrawDiags();
                                    }
#endif
                                }
                                else
                                {
                                    _scene.ObjectForDrawing(obj, -1, clip);
#if _ENABLE_CHEATS
                                    if (DiagMode)
                                    {
                                        obj->DrawDiags();
                                    }
#endif
                                }
                            }
                        }
                    }
                    for (int i = 0; i < NCloudlets(); i++)
                    {
                        _scene.CloudletForDrawing(GetCloudlet(i));
                    }
                    _scene.EndObjects(); // prepare objects for drawing

                    GEngine->EnableReorderQueues(true);
                }

                perf.Mark(Dev::FrameProfiler::PhaseDrawObjPrep);
                _scene.GetLandscape()->Draw(_scene);
                perf.Mark(Dev::FrameProfiler::PhaseDrawLandscape);

                GEngine->EnableReorderQueues(false);
                GEngine->FlushQueues();

                _scene.ObjectsDrawn();
                GEngine->FlushQueues();

                // Frame validation — ExtractSceneInputs → BuildFrame →
                // ValidateFrame + runtime checks, after the world's
                // pixels have landed.  See the matching call below.
                if (GEngine)
                    render::frame::ObserveRenderedFrame(*GEngine, _scene);
                perf.Mark(Dev::FrameProfiler::PhaseDrawObjects);
                GEngine->EnableNightEye(0);

                if (_cameraEffect)
                {
                    _cameraEffect->Draw();
                }
            }
            if (!IsDisplayEnabled())
            {
                ProgressDraw();
            }
        } // if (enableDraw)
        else
        {
            _scene.CleanUp();
        }
    }
    else
    {
        if (enableDraw)
        {
            _scene.EndObjects(); // prepare objects for drawing
            _scene.DrawObjectsAndShadowsPass1();
            _scene.DrawObjectsAndShadowsPass2();
            _scene.ObjectsDrawn();

            // Frame validation — ExtractSceneInputs → BuildFrame →
            // ValidateFrame + runtime checks.  The wrapper lives in
            // WorldFrameObserver.cpp so this translation unit doesn't
            // need EngineGL33.hpp (which collides with appGlobalsShim's
            // GApp->m_keepFocus macro).  Per-frame stats are surfaced
            // via --render-frame-log (AppConfig flag), default off.
            if (GEngine)
                render::frame::ObserveRenderedFrame(*GEngine, _scene);
        }
        else
        {
            _scene.CleanUp();
        }
    }

    perf.Mark(Dev::FrameProfiler::PhaseDrawPost);

    if (enableDraw)
    {
        if (_map && (_showMap || _forceMap))
        {
            _map->DrawHUD(camAI, 1);
        }

        if (_ui && IsUIEnabled() && !_cameraEffect)
        {
            if (camAI)
            {
                if (_camType == CamInternal)
                {
                    camAI->DrawCameraCockpit();
                }

                if (person && person->IsNVWanted())
                {
                    person->DrawNVOptics();
                }

                _ui->DrawHUD(*_scene.GetCamera(), camAI, _camType);
            }
            else if (camVehicle)
            {
                camVehicle->DrawCameraCockpit();
            }
        }

        NetworkGameState state = GetNetworkManager().GetGameState();
        // MP score table: auto-shown at debriefing, and held-to-show during play while the
        // NetworkStats action (I) is down — the original behaviour. Drawn here inside the
        // HUD/UI 2D pass; drawing it after the cursor overlay (which ends the UI passes)
        // renders nothing.
        if (state == NGSDebriefing || (state == NGSPlay && input.GetAction(UANetworkStats, false)))
        {
            GStats.DrawMPTable(1.0f);
        }
#if _ENABLE_CHEATS
        void DrawNetworkStatistics();
        DrawNetworkStatistics();
#endif

        if (_options)
        {
            _options->DrawHUD(nullptr, 1);
        }
        if (_userDlg)
        {
            _userDlg->DrawHUD(nullptr, 1);
        }
        if (_channel)
        {
            _channel->DrawHUD(nullptr, 1);
        }
        if (_chat)
        {
            _chat->DrawHUD(nullptr, 1);
        }
        if (_voiceChat)
        {
            _voiceChat->DrawHUD(nullptr, 1);
        }
        GChatList.OnDraw();
        if (_warningMessage)
        {
            _warningMessage->OnDraw(nullptr, 1);
        }

        // Polymorphic cursor overlay — drawn last so the cursor
        // sits on top of all UI passes.  ViewerCursorOverlay paints
        // a ring; GameCursorOverlay walks the dialog stack and
        // calls the topmost ControlsContainer's DrawCursor.
        if (_cursorOverlay)
            _cursorOverlay->Draw(_engine);

        if (_mode == GModeNetware)
        {
            DrawConnectionQuality(GetNetworkManager().GetConnectionQuality());
        }

        if (IsSimulationEnabled())
        {
            if (_cutEffect)
            {
                _cutEffect->Draw();
            }
            if (_titleEffect)
            {
                _titleEffect->Draw();
            }
        }
    } // if (enableDraw)

    if (enableDraw)
    {
        GEngine->FinishDraw();
    }

    perf.Mark(Dev::FrameProfiler::PhaseHud);

    if (doSim)
    {
#if BACKGROUND_AI
        _secThread->FinishSecondary();
#else
        PerformAI(deltaT, noAccDeltaT);
#endif
        SimulateAllVehicles(deltaT, noAccDeltaT, camVehicle);
    }

    perf.Mark(Dev::FrameProfiler::PhaseAiVehicles);

    if (IsSimulationEnabled())
    {
        if (camInsideVehicle)
        {
            PerformSound(camInsideVehicle, deltaT);
        }
        else
        {
            PerformSound(nullptr, deltaT);
        }
    }

    GSoundScene->AdvanceAll(deltaT, !IsSimulationEnabled() || quiet); // sort and activate sounds
    if (GSoundsys)
    {
        GSoundsys->Commit(); // commit deferred settings
    }

    perf.Mark(Dev::FrameProfiler::PhaseSound);

    if (enableDraw)
    {
        GEngine->NextFrame();
    }

    perf.Mark(Dev::FrameProfiler::PhaseSwap);
    perf.EndFrame(Poseidon::gPerfDrawCalls);
    Poseidon::gPerfDrawCalls = 0;

#if _ENABLE_CHEATS
    static bool disableVis = false;
    if (input.GetCheat2ToDo(SDL_SCANCODE_Y))
    {
        disableVis = !disableVis;
        Foundation::GlobalShowMessage(500, "VisTests %s", disableVis ? "Off" : "On");
    }

#ifndef _DEBUG
#define REGULAR_FOOTPRINT 1
#endif

#if REGULAR_FOOTPRINT
    static DWORD lastSample = 0;
    if (GlobalTickCount() > lastSample + 60000)
    {
        void MemoryFootprint();
        MemoryFootprint();
        lastSample = GlobalTickCount();
    }
    if (input.GetCheat1ToDo(SDL_SCANCODE_O))
    {
        void MemoryFootprint();
        MemoryFootprint();
        lastSample = UINT_MAX;
    }
#else
    if (input.GetCheat1ToDo(SDL_SCANCODE_O))
    {
        void MemoryFootprint();
        MemoryFootprint();
    }
#endif

#endif

    if (doSim
#if _ENABLE_CHEATS
        && !disableVis
#endif
    )
    {
        GetSensorList()->SmartUpdateAll();
    }
}
