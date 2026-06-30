#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Input/CheatCode.hpp>
#include <Poseidon/Input/ControllerUiLayout.hpp>
#include <SDL3/SDL_scancode.h>

#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Foundation/Platform/AppConfig.hpp>
#include <Poseidon/IO/ParamFileExt.hpp>
#include <Poseidon/UI/UIActiveDisplay.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>
#include <Poseidon/Audio/Speaker.hpp>
#include <stdio.h>
#include <string.h>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/RStringArray.hpp>
#include <Poseidon/Foundation/Enums/EnumNames.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Memory/CheckMem.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Memtype.h>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>
#ifdef _WIN32
#include <windows.h>
#endif
#include <Poseidon/Graphics/Rendering/Primitives/ClipVert.hpp>

namespace
{
Poseidon::DisplayArcadeMap* FindEditorOwner(ControlsContainer* display)
{
    for (ControlsContainer* cur = display; cur; cur = cur->Parent())
    {
        if (auto* editor = dynamic_cast<Poseidon::DisplayArcadeMap*>(cur))
            return editor;
    }
    return nullptr;
}

Poseidon::DisplayMission* FindMissionOwner(ControlsContainer* display)
{
    for (ControlsContainer* cur = display; cur; cur = cur->Parent())
    {
        if (auto* mission = dynamic_cast<Poseidon::DisplayMission*>(cur))
            return mission;
    }
    return nullptr;
}

bool DoEditorChildControllerUiAction(ControlsContainer* display, Poseidon::ControllerUiAction action)
{
    Poseidon::ControllerEditorUiLayout layout;
    const Poseidon::ControllerUiDispatch dispatch = layout.Map(action);
    if (dispatch.kind == Poseidon::ControllerUiDispatchKind::KeyTap)
    {
        display->OnKeyDown(dispatch.key, 1, 0);
        display->OnKeyUp(dispatch.key, 1, 0);
        return true;
    }
    return false;
}
}

#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/World/Terrain/Visibility.hpp>
#include <Random/randomGen.hpp>
#include <Poseidon/World/Scene/Camera/CamEffects.hpp>
#include <Poseidon/Game/TitEffects.hpp>
#include <Poseidon/Game/Scripting/Scripts.hpp>
#include <Poseidon/UI/GameModule.hpp>
#include <Poseidon/Dev/Debug/DebugTrap.hpp>

#include <Poseidon/Dev/Diag/DiagModes.hpp>

void MemoryCleanUp();
extern bool showCinemaBorder;
extern SoundPars EnvSoundPars[];
extern SoundPars EnvSoundParsNight[];
extern char LoadFile[];
extern RString GMapOnSingleClick;
ControlsContainer* CreateWarningMessageBox(RString text);
void SetVisibility(float distance);
LSError SerializeMapInfo(ParamArchive& ar, RString name, int minVersion);

extern bool AutoTest;

namespace Poseidon
{
using namespace Dev;
} // namespace Poseidon

#include <Poseidon/Core/Progress.hpp>

#include <Poseidon/World/Entities/Vehicles/SeaGull.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>

#include <Poseidon/Game/Commands/GameStateExt.hpp>

#include <Poseidon/World/Entities/Weapons/Shots.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/Input/KeyLights.hpp>

#include <Poseidon/IO/Serialization/ThreadSync.hpp>

#include <Poseidon/Game/Chat.hpp>

#include <Poseidon/Network/Network.hpp>

#include <Poseidon/AI/ArcadeTemplate.hpp>
#include <Poseidon/UI/Controls/UIControls.hpp>
#include <Poseidon/UI/Map/UIMap.hpp>

#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/IO/Serialization/ParamArchive.hpp>
#include <Poseidon/World/Scene/ObjectClasses.hpp>

#include <Poseidon/World/Detection/Detector.hpp>

#include <Poseidon/UI/Locale/StringtableExt.hpp>

#include <Poseidon/Core/resincl.hpp>

namespace Poseidon
{
bool StartAutoTest();
bool ProcessFullName(RString);
} // namespace Poseidon

namespace Poseidon
{
void SetBaseSubdirectory(RString);
void SetCampaign(RString);
void StartRandomCutscene(RString);
} // namespace Poseidon

namespace Poseidon
{

#define LOG_ADD_REMOVE_VEHICLE 0
#define PERF_SIM 0

static const int NEnvSoundPars = 6;
struct SoundPars;
} // namespace Poseidon
namespace Poseidon
{

const float PreferredGridSizeMP = 25;

bool World::IsDisplayEnabled() const
{
    if (ENGINE_CONFIG.landEditor)
    {
        return true;
    }
    if (_noDisplay)
    {
        return false;
    }
    if (_showMap && _map && !_map->IsDisplayEnabled())
    {
        return false;
    }
    if (GetGProgress().Active())
    {
        return false;
    }
    // No menu attached → display is always on (viewer / dedicated /
    // editor paths skip CreateMainOptions, so `_options == nullptr`
    // is the natural signal for "no menu in this mode").
    if (!_options)
    {
        return true;
    }
    return _options->IsDisplayEnabled();
}

bool World::IsSimulationEnabled() const
{
    if (ENGINE_CONFIG.landEditor)
    {
        return true;
    }
    if (_mode == GModeNetware)
    {
        return GetNetworkManager().GetGameState() >= NGSPlay;
    }
    else
    {
        if (_warningMessage)
        {
            return false;
        }
        if (!_enableSimulation)
        {
            return false;
        }
        if (!_simulationFocus)
        {
            return false;
        }
        // No menu (viewer / dedicated / editor) → simulation runs
        // without a pause overlay gating it.  Lets ObjectViewer's
        // animation phase keep advancing in viewer mode without
        // a special-case IsViewerMode() check here.
        if (!_options)
        {
            return true;
        }
        return _options->IsSimulationEnabled();
    }
}

bool World::HasOptions() const
{
    if (!_options)
    {
        return false;
    }
    return !_options->IsSimulationEnabled();
}

bool World::IsUIEnabled() const
{
    if (!IsSimulationEnabled())
    {
        return false;
    }
    if (!_options)
    {
        return false;
    }
    return _options->IsUIEnabled();
}

bool World::HasCompass() const
{
    return _showCompass && !_showMap;
}

bool World::HasWatch() const
{
    return _showWatch && !_showMap;
}

void World::OnChannelChanged()
{
    _channelChanged = Glob.uiTime;
}

void World::CreateMainOptions()
{
    if (!_options)
    {
        _options = CreateMainOptionsUI();
        if (GEngine)
        {
            GEngine->ReinitCounters();
        }
    }
}

void World::CreateEndOptions(int mode)
{
    if (!_options)
    {
        _options = CreateEndOptionsUI(mode);
    }
}
void World::CreateChat()
{
    if (!_chat)
    {
        InputSubsystem::Instance().ChangeGameFocus(+1);
        _chat = CreateChatUI();
    }
}
void World::CreateVoiceChat()
{
    if (!_voiceChat)
    {
        _voiceChat = CreateVoiceChatUI();
    }
}
void World::CreateMainMap()
{
    if (!_map)
    {
        _map = CreateMainMapUI();
    }
}

void World::CreateWarningMessage(RString text)
{
    if (!_warningMessage)
    {
        _warningMessage = CreateWarningMessageBox(text);
    }
}

void World::DestroyOptions(int exitCode)
{
    if (!_options)
    {
        return;
    }
    _options.Free();
    /*
        switch( exitCode )
        {
            case IDC_CANCEL:
            case IDC_MAIN_GAME:
            break;
            case IDC_MAIN_QUIT:
                Glob.exit=true;
            break;
            case IDC_OK:
                CreateMainOptions();
            break;
        }
    */
    if (exitCode == IDC_MAIN_QUIT)
    {
        Glob.exit = true;
    }

    GEngine->ReinitCounters();
}

void World::DestroyMap(int exitCode)
{
    _map.Free();
}

void World::DestroyChat(int exitCode)
{
    if (_chat)
    {
        _chat.Free();
        InputSubsystem::Instance().ChangeGameFocus(-1);
    }
}

void World::DestroyVoiceChat(int exitCode)
{
    _voiceChat.Free();
}

Transport* World::FindFreeVehicle(Person* driver) const
{
    const float maxDist = 20;
    float minDist2 = Square(maxDist);
    Transport* free = nullptr;
    int xMin, xMax, zMin, zMax;
    Vector3Val pos = driver->Position();
    ObjRadiusRectangle(xMin, xMax, zMin, zMax, pos, pos, maxDist);
    for (int x = xMin; x <= xMax; x++)
    {
        for (int z = zMin; z <= zMax; z++)
        {
            const ObjectList& list = _scene.GetLandscape()->GetObjects(z, x);
            for (int i = 0; i < list.Size(); i++)
            {
                Object* obj = list[i];
                Transport* vehicle = dyn_cast<Transport>(obj);
                if (!vehicle)
                {
                    continue;
                }
                float dist2 = vehicle->Position().Distance2(driver->Position());
                if (minDist2 > dist2)
                {
                    if (vehicle->QCanIGetInAny(driver))
                    {
                        Vector3Val relPos = driver->PositionWorldToModel(vehicle->Position());
                        //					if( relPos.Z()>0 && fabs(relPos.X())<relPos.Z() )
                        if (relPos.Z() > 0)
                        {
                            minDist2 = dist2;
                            free = vehicle;
                        }
                    }
                }
            }
        }
    }
    return free;
}

#if _ENABLE_CHEATS
bool disableAI = false;
bool disableUnitAI = false;
bool disableSimpleSim = false;
#endif

void World::PerformAI(float deltaT, float noAccDeltaT)
{
    auto& input = InputSubsystem::Instance();
#if _ENABLE_CHEATS
    if (input.GetCheat2ToDo(SDL_SCANCODE_T))
    {
        disableAI = !disableAI;
        GlobalShowMessage(500, "Group AI %s", disableAI ? "Off" : "On");
    }
    if (input.GetCheat2ToDo(SDL_SCANCODE_U))
    {
        disableUnitAI = !disableUnitAI;
        GlobalShowMessage(500, "Unit AI %s", disableUnitAI ? "Off" : "On");
    }
    if (input.GetCheat2ToDo(SDL_SCANCODE_I))
    {
        disableSimpleSim = !disableSimpleSim;
        GlobalShowMessage(500, "Simple sim %s", disableSimpleSim ? "Off" : "On");
    }
#endif
    if (deltaT > 0
#if _ENABLE_CHEATS
        && !disableAI
#endif
    )
    {
        GetRadio().Simulate(deltaT);
        if (_eastCenter)
        {
            _eastCenter->GetRadio().Simulate(deltaT);
            _eastCenter->Think();
            int i;
            for (i = 0; i < _eastCenter->NGroups(); i++)
            {
                AIGroup* grp = _eastCenter->GetGroup(i);
                if (grp)
                {
                    grp->GetRadio().Simulate(deltaT);
                }
            }
        }
        if (_westCenter)
        {
            _westCenter->GetRadio().Simulate(deltaT);
            _westCenter->Think();
            int i;
            for (i = 0; i < _westCenter->NGroups(); i++)
            {
                AIGroup* grp = _westCenter->GetGroup(i);
                if (grp)
                {
                    grp->GetRadio().Simulate(deltaT);
                }
            }
        }
        if (_guerrilaCenter)
        {
            _guerrilaCenter->GetRadio().Simulate(deltaT);
            _guerrilaCenter->Think();
            int i;
            for (i = 0; i < _guerrilaCenter->NGroups(); i++)
            {
                AIGroup* grp = _guerrilaCenter->GetGroup(i);
                if (grp)
                {
                    grp->GetRadio().Simulate(deltaT);
                }
            }
        }
        if (_civilianCenter)
        {
            _civilianCenter->GetRadio().Simulate(deltaT);
            _civilianCenter->Think();
            int i;
            for (i = 0; i < _civilianCenter->NGroups(); i++)
            {
                AIGroup* grp = _civilianCenter->GetGroup(i);
                if (grp)
                {
                    grp->GetRadio().Simulate(deltaT);
                }
            }
        }
        if (_logicCenter)
        {
            _logicCenter->Think();
        }

        if (_endMission == EMContinue)
        {
            if (input.CheatActivated() == CheatWinMission)
            {
                if (_mode != GModeNetware)
                    _endMission = EMEnd1;
                input.CheatServed();
            }
            else
#if _ENABLE_CHEATS
                if (input.GetCheat2ToDo(SDL_SCANCODE_1))
            {
                _endMission = EMEnd1;
            }
            else if (input.GetCheat2ToDo(SDL_SCANCODE_2))
            {
                _endMission = EMEnd2;
            }
            else if (input.GetCheat2ToDo(SDL_SCANCODE_3))
            {
                _endMission = EMEnd3;
            }
            else if (input.GetCheat2ToDo(SDL_SCANCODE_4))
            {
                _endMission = EMEnd4;
            }
            else if (input.GetCheat2ToDo(SDL_SCANCODE_5))
            {
                _endMission = EMEnd5;
            }
            else if (input.GetCheat2ToDo(SDL_SCANCODE_6))
            {
                _endMission = EMEnd6;
            }
            else if (input.GetCheat2ToDo(SDL_SCANCODE_0))
            {
                _endMission = EMLoser;
            }
            else
#endif
            {
                switch (_mode)
                {
                    case GModeArcade:
                    {
                        Person* veh = GetRealPlayer();
                        AIUnit* unit = veh ? veh->Brain() : nullptr;
                        if (!unit || unit->GetLifeState() == AIUnit::LSDead)
                        {
                            _endMission = EMKilled;
                            return;
                        }
                    }
                    case GModeIntro:
                    case GModeNetware:
                    {
                        int nEnd1 = 0, cEnd1 = 0;
                        int nEnd2 = 0, cEnd2 = 0;
                        int nEnd3 = 0, cEnd3 = 0;
                        int nEnd4 = 0, cEnd4 = 0;
                        int nEnd5 = 0, cEnd5 = 0;
                        int nEnd6 = 0, cEnd6 = 0;

                        for (int i = 0; i < sensorsMap.Size(); i++)
                        {
                            Entity* veh = sensorsMap[i];
                            if (!veh)
                            {
                                continue;
                            }
                            Detector* sensor = dyn_cast<Detector>(veh);
                            PoseidonAssert(sensor);
                            if (!sensor)
                            {
                                continue;
                            }
                            switch (sensor->GetAction())
                            {
                                case ASTLoose:
                                    if (sensor->IsActive())
                                    {
                                        _endMission = EMLoser;
                                        return;
                                    }
                                    break;
                                case ASTEnd1:
                                    nEnd1++;
                                    if (sensor->IsActive())
                                    {
                                        cEnd1++;
                                    }
                                    break;
                                case ASTEnd2:
                                    nEnd2++;
                                    if (sensor->IsActive())
                                    {
                                        cEnd2++;
                                    }
                                    break;
                                case ASTEnd3:
                                    nEnd3++;
                                    if (sensor->IsActive())
                                    {
                                        cEnd3++;
                                    }
                                    break;
                                case ASTEnd4:
                                    nEnd4++;
                                    if (sensor->IsActive())
                                    {
                                        cEnd4++;
                                    }
                                    break;
                                case ASTEnd5:
                                    nEnd5++;
                                    if (sensor->IsActive())
                                    {
                                        cEnd5++;
                                    }
                                    break;
                                case ASTEnd6:
                                    nEnd6++;
                                    if (sensor->IsActive())
                                    {
                                        cEnd6++;
                                    }
                                    break;
                            }
                        }
                        if (nEnd1 > 0 && cEnd1 == nEnd1)
                        {
                            _endMission = EMEnd1;
                        }
                        else if (nEnd2 > 0 && cEnd2 == nEnd2)
                        {
                            _endMission = EMEnd2;
                        }
                        else if (nEnd3 > 0 && cEnd3 == nEnd3)
                        {
                            _endMission = EMEnd3;
                        }
                        else if (nEnd4 > 0 && cEnd4 == nEnd4)
                        {
                            _endMission = EMEnd4;
                        }
                        else if (nEnd5 > 0 && cEnd5 == nEnd5)
                        {
                            _endMission = EMEnd5;
                        }
                        else if (nEnd6 > 0 && cEnd6 == nEnd6)
                        {
                            _endMission = EMEnd6;
                        }
                        else
                        {
                            _endMission = EMContinue;
                        }
                    }
                    break;
                    default:
                        Fail("Unknown mode");
                        _endMission = EMContinue;
                        break;
                }
            }
        }
    }
}

void World::SimulateVehicles(float deltaT, VehicleSimulation simul, Entity* insideVehcile)
{
    MoveOutAndDelete(_vehicles, deltaT, false);
    MoveOutAndDelete(_animals, deltaT, false);
    SimulateOnly(_vehicles, deltaT, simul, insideVehcile, SimulateVisibleNear);
    SimulateOnly(_animals, deltaT, simul, insideVehcile, SimulateVisibleNear);

    MoveOutAndDelete(_vehicles, deltaT, true);
    MoveOutAndDelete(_animals, deltaT, true);

#if PERF_SIM
#endif
}

void World::SimulateBuildings(float deltaT, VehicleSimulation simul)
{
    MoveOutAndDelete(_buildings, deltaT, false);
    SimulateOnly(_buildings, deltaT, simul, nullptr, SimulateVisibleNear);
#if PERF_SIM
#endif
    MoveOutAndDelete(_buildings, deltaT, true);
}

void World::SimulateFastVehicles(float deltaT, VehicleSimulation simul)
{
    MoveOutAndDelete(_fastVehicles, deltaT, false);
    SimulateOnly(_fastVehicles, deltaT, simul, nullptr, SimulateVisibleNear);
#if PERF_SIM
#endif
    MoveOutAndDelete(_fastVehicles, deltaT, true);
}

void World::SimulateCloudlets(float deltaT)
{
    SimulationImportance prec = SimulateVisibleFar;

    for (int i = 0; i < _cloudlets.Size();)
    {
        Entity* vehicle = _cloudlets[i];
        vehicle->Simulate(deltaT, prec);
        if (!vehicle->ToDelete())
        {
            i++;
        }
        else
        {
            _cloudlets.Delete(i);
        }
    }
}

void World::SimulateAllVehicles(float deltaT, float noAccDeltaT, Entity* cameraVehicle)
{
    float farValidFor = 1.5;
    if (Glob.time > _farImportanceDistributionTime + farValidFor)
    {
        DistributeFarImportances();
    }
    if (Glob.time > _nearImportanceDistributionTime + 1.0)
    {
        DistributeNearImportances();
    }
    SetActiveChannels();
#define MAX_SIM_STEP_VEHICLES (1.0 / 15)
#define MAX_SIM_STEP_FAST (0.001)

    for (int i = 0; i < _fastVehicles.Size(); i++)
    {
        Entity* vehicle = _fastVehicles[i];
        vehicle->StartFrame();
    }

    SimulateCloudlets(deltaT);
    float toSimVehicles = deltaT;
    float toSimFast = deltaT;
    while (toSimVehicles > MAX_SIM_STEP_VEHICLES)
    {
        SimulateVehicles(MAX_SIM_STEP_VEHICLES, &Entity::SimulateOptimized, cameraVehicle);
        toSimVehicles -= MAX_SIM_STEP_VEHICLES;
        while (toSimFast > toSimVehicles && toSimFast > MAX_SIM_STEP_FAST)
        {
            SimulateFastVehicles(MAX_SIM_STEP_FAST, &Entity::SimulateOptimized);
            toSimFast -= MAX_SIM_STEP_FAST;
        }
    }
    SimulateVehicles(toSimVehicles, &Entity::SimulateRest, cameraVehicle);
    while (toSimFast > MAX_SIM_STEP_FAST)
    {
        SimulateFastVehicles(MAX_SIM_STEP_FAST, &Entity::SimulateOptimized);
        toSimFast -= MAX_SIM_STEP_FAST;
    }
    SimulateFastVehicles(toSimFast, &Entity::SimulateRest);
    SimulateBuildings(deltaT, &Entity::SimulateOptimized);

    for (int i = 0; i < _attached.Size(); i++)
    {
        _attached[i]->UpdatePosition();
    }
}

float World::Visibility(AIUnit* from, Object* to) const
{
    EntityAI* ai = dyn_cast<EntityAI>(to);
    if (!ai)
    {
        return 1;
    }
    Person* me = from->GetPerson();
    AIUnit* aiUnit = ai->CommanderUnit();
    if (aiUnit || ai->GetType()->IsKindOf(GLOB_WORLD->Preloaded(VTypeStrategic)) ||
        ai->GetType()->IsKindOf(GLOB_WORLD->Preloaded(VTypeAllVehicles)))
    {
        if (!ai->IsInLandscape())
        {
            LOG_DEBUG(World, "Patch: vanished vehicle visibility queried ({} to {})", (const char*)me->GetDebugName(),
                      (const char*)ai->GetDebugName());
            return 1;
        }
        if (aiUnit)
        {
            AIGroup* grp = from->GetGroup();
            if (aiUnit && aiUnit->GetGroup() == grp && aiUnit->GetLifeState() == AIUnit::LSAlive)
            {
                return 1;
            }
        }

        return _sensorList->GetVisibility(me, ai);
    }
    return 1;
}

Foundation::Time World::VisibilityTime(AIUnit* from, Object* to) const
{
    EntityAI* ai = dyn_cast<EntityAI>(to);
    if (!ai)
    {
        return Glob.time;
    }
    Person* me = from->GetPerson();
    AIUnit* aiUnit = ai->CommanderUnit();
    if (aiUnit || ai->GetType()->IsKindOf(GLOB_WORLD->Preloaded(VTypeStrategic)) ||
        ai->GetType()->IsKindOf(GLOB_WORLD->Preloaded(VTypeAllVehicles)))
    {
        if (!ai->IsInLandscape())
        {
            return Glob.time;
        }
        if (aiUnit)
        {
            AIGroup* grp = from->GetGroup();
            if (aiUnit && aiUnit->GetGroup() == grp && aiUnit->GetLifeState() == AIUnit::LSAlive)
            {
                return Glob.time;
            }
        }

        return _sensorList->GetVisibilityTime(me, ai);
    }
    return Glob.time;
}

static const char* MapWaveSound(bool night, int index, float& v)
{
    if (index < 0)
    {
        return nullptr;
    }
    if (index > NEnvSoundPars - 1)
    {
        return nullptr;
    }
    if (v <= 0)
    {
        return nullptr;
    }
    // note: return string must be lowercase
    const SoundPars& pars = night ? EnvSoundParsNight[index] : EnvSoundPars[index];
    v *= pars.vol;
    return pars.name;
}

void World::PerformSound(VehicleList& list, Entity* inside, float deltaT)
{
    if (inside)
    {
        for (int i = 0; i < list.Size(); i++)
        {
            Entity* vehicle = list[i];
            vehicle->Sound(inside == vehicle, deltaT);
        }
    }
    else
    {
        for (int i = 0; i < list.Size(); i++)
        {
            Entity* vehicle = list[i];
            vehicle->Sound(false, deltaT);
        }
    }
}

void World::PerformSound(VehiclesDistributed& list, Entity* inside, float deltaT)
{
    PerformSound(list._visibleNear, inside, deltaT);
    PerformSound(list._visibleFar, inside, deltaT);
    PerformSound(list._invisibleNear, inside, deltaT);
    PerformSound(list._invisibleFar, inside, deltaT);
}

void World::PerformSound(Entity* inside, float deltaT)
{
    int i;
    if (!GSoundsys)
    {
        return;
    }
    const Camera& cam = *_scene.GetCamera();
    Vector3Val pos = cam.Position();
    int x = toIntFloor(pos.X() * InvLandGrid);
    int z = toIntFloor(pos.Z() * InvLandGrid);
    GeographyInfo geogr = GLandscape->GetGeography(x, z);
    float sy = GLandscape->SurfaceYAboveWater(pos.X(), pos.Z());
#define SHOW_ENV 0
    if (geogr.u.forestInner || geogr.u.forestOuter)
    {
        SoundEnvironment env;
        env.type = SEForest;
        env.size = 38;
        env.density = 0.5;
        GSoundsys->SetEnvironment(env);
#if SHOW_ENV
        GlobalShowMessage(100, "Forest %.1f %.1f", env.size, env.density);
#endif
    }
    else if (geogr.u.howManyHardObjects > 0)
    {
        SoundEnvironment env;
        env.type = SECity;
        env.size = (4 - geogr.u.howManyHardObjects) * 15;
        env.density = geogr.u.howManyHardObjects * (1.0f / 3);
        GSoundsys->SetEnvironment(env);
#if SHOW_ENV
        GlobalShowMessage(100, "City %.1f %.1f", env.size, env.density);
#endif
    }
    else if (sy > 170)
    {
        SoundEnvironment env;
        env.type = SEMountains;
        env.size = sy - 120;
        saturate(env.size, 50, 100);
        env.density = 0.5;
        GSoundsys->SetEnvironment(env);
#if SHOW_ENV
        GlobalShowMessage(100, "Mountains %.1f %.1f", env.size, env.density);
#endif
    }
    else
    {
        SoundEnvironment env;
        env.type = SEPlain;
        env.size = 75 - geogr.u.howManyObjects * 15;
        env.density = 0.5;
        GSoundsys->SetEnvironment(env);
#if SHOW_ENV
        GlobalShowMessage(100, "Plain %.1f %.1f", env.size, env.density);
#endif
    }

    GSoundsys->SetListener(cam.Position(), cam.Speed(), cam.Direction(), cam.DirectionUp());
    PerformSound(_vehicles, inside, deltaT);
    PerformSound(_animals, inside, deltaT);
    PerformSound(_buildings, inside, deltaT);
    PerformSound(_fastVehicles, inside, deltaT);
#if 1
    int s[5];
    float v[5];
    const Vector3& camPos = cam.Position();
    float camHeight = camPos[1] - _scene.GetLandscape()->SurfaceY(camPos[0], camPos[2]);
    float canHear = 1 - camHeight * 1.0 / 200;
    saturate(canHear, 0, 1);
    float vCoef = 0.5 * canHear;

    if (stricmp(EnvSoundPars[5].name, "SOUND\\$DEFAULT$.WSS") != 0)
    {
        s[0] = 5;
        v[0] = 2.0 * vCoef;
        s[1] = -1;
        v[1] = 0;
        s[2] = -1;
        v[2] = 0;
        s[3] = -1;
        v[3] = 0;
    }
    else
    {
        float xGrid = camPos.X() * InvLandGrid - 0.5;
        float zGrid = camPos.Z() * InvLandGrid - 0.5;
        int x = toIntFloor(xGrid);
        int z = toIntFloor(zGrid);
        float xFrac = xGrid - x;
        float zFrac = zGrid - z;
        s[0] = GLOB_LAND->GetSound(x, z);
        s[1] = GLOB_LAND->GetSound(x + 1, z);
        s[2] = GLOB_LAND->GetSound(x, z + 1);
        s[3] = GLOB_LAND->GetSound(x + 1, z + 1);
        // volume: bilinear by xFrac,zFrac
        v[0] = (1 - xFrac + 1 - zFrac) * vCoef;
        v[1] = (xFrac + 1 - zFrac) * vCoef;
        v[2] = (1 - xFrac + zFrac) * vCoef;
        v[3] = (xFrac + zFrac) * vCoef;
    }
    s[4] = -1;
    v[4] = 0;
    const float thold = 0.1;
    float rain = _scene.GetLandscape()->GetRainDensity() - thold;
    if (rain >= 0)
    {
        float coef = rain * (1 / (1 - thold));
        v[0] *= 1 - coef;
        v[1] *= 1 - coef;
        v[2] *= 1 - coef;
        v[3] *= 1 - coef;
        s[4] = 4, v[4] = coef;
    }
    // sum of all v[x] is 1
    // merge idenl sXX
    {
        for (int i = 0; i < 5; i++)
        {
            for (int j = 0; j < i; j++)
            {
                if (s[j] == s[i])
                {
                    s[j] = -1;
                    v[i] += v[j];
                }
            }
        }
    }
    const char* ss[5];
    bool night = _scene.MainLight()->NightEffect() > 0.5;
    for (i = 0; i < 5; i++)
    {
        ss[i] = MapWaveSound(night, s[i], v[i]);
    }
    GSoundScene->SetEnvSound(ss[0], v[0]);
    GSoundScene->SetEnvSound(ss[1], v[1]);
    GSoundScene->SetEnvSound(ss[2], v[2]);
    GSoundScene->SetEnvSound(ss[3], v[3]);
    GSoundScene->SetEnvSound(ss[4], v[4]);
    GSoundScene->AdvanceEnvSounds();
#endif
}

void World::UnloadSounds(VehicleList& list)
{
    for (int i = 0; i < list.Size(); i++)
    {
        list[i]->UnloadSound();
    }
}

void World::UnloadSounds(VehiclesDistributed& list)
{
    UnloadSounds(list._visibleNear);
    UnloadSounds(list._visibleFar);
    UnloadSounds(list._invisibleNear);
    UnloadSounds(list._invisibleFar);
}

void World::UnloadSounds()
{
    UnloadSounds(_vehicles);
    UnloadSounds(_buildings);
    UnloadSounds(_animals);
    UnloadSounds(_fastVehicles);
    if (GSoundScene)
        GSoundScene->Reset();
}

void World::AdjustSubdivisionGrid(float gridSize)
{
    const float invLog2 = 1 / log(2);
    float coefLog = log(LandGrid / gridSize) * invLog2;
    int coefLogInt = toInt(coefLog);
    saturate(coefLogInt, 0, 8);
    LOG_DEBUG(World, "Terrain subdivision wanted: {} ({:.3f})", coefLogInt, coefLog);
    int currentLog = TerrainRangeLog - LandRangeLog;
    int terrainChange = coefLogInt - currentLog;

    if (terrainChange > 0)
    {
        if (!GLandscape->LoadSubdivCache(coefLogInt))
        {
            GLandscape->SubdivideTerrain(terrainChange);
            GLandscape->SaveSubdivCache(coefLogInt);
        }
    }
    else if (terrainChange < 0)
    {
        GLandscape->ResampleTerrain(-terrainChange);
    }
}

void World::AdjustSubdivision(GameMode mode)
{
    DWORD tSub = GetTickCount();
    float gridSize = GScene->GetPreferredTerrainGrid();
    float viewDist = GScene->GetPreferredViewDistance();
    // MP requires all clients use the same grid size.
    if (mode == GModeNetware)
    {
        gridSize = PreferredGridSizeMP;
        viewDist = 900;
    }
    AdjustSubdivisionGrid(gridSize);
    SetVisibility(viewDist);
    LOG_DEBUG(Core, "LOAD: AdjustSubdivision {}ms (grid={} vd={})", GetTickCount() - tSub, gridSize, viewDist);
}

void World::ActivateAddons(const FindArrayRStringCI& addons)
{
    _activeAddons.Clear();
    const ParamEntry& def = Pars >> "CfgAddons" >> "PreloadAddons";
    for (int c = 0; c < def.GetEntryCount(); c++)
    {
        const ParamEntry& cc = def.GetEntry(c);
        if (!cc.IsClass())
        {
            continue;
        }
        if (!cc.FindEntry("list"))
        {
            continue;
        }
        const ParamEntry& cl = cc >> "list";
        for (int i = 0; i < cl.GetSize(); i++)
        {
            RString addon = cl[i];
            _activeAddons.Add(addon);
        }
    }
    for (int i = 0; i < addons.Size(); i++)
    {
        RString addon = addons[i];
        LOG_DEBUG(World, "Activating addon {}", (const char*)addon);
        _activeAddons.Add(addon);
    }
}

bool World::CheckAddon(const ParamEntry& entry)
{
    bool visible = entry.CheckVisible(_activeAddons);
    if (visible)
    {
        return true;
    }
    if (!_options)
    {
        return false;
    }
    RString addon = entry.GetOwner();
    bool registered = _options->DoUnregisteredAddonUsed(addon);
    if (registered)
    {
        _activeAddons.Add(addon);
    }
    return registered;
}

void World::SwitchLandscape(const char* name)
{
    DWORD t0 = GetTickCount();
    GSoundScene->Reset();
    GDebugger.NextAliveExpected(15 * 60 * 1000);
    VehicleTypes.LockAllTypes();
    GEngine->TextBank()->LockAllTextures();
    CleanUp();
    Landscape* land = _scene.GetLandscape();
    if (!land)
    {
        Fail("No landscape");
        return;
    }

    const char* lName = land->GetName();
    if (!lName || strcmpi(name, lName))
    {
        char islandName[256];
        const char* fname = strrchr(name, '\\');
        if (!fname)
        {
            fname = name;
        }
        else
        {
            fname++;
        }
        snprintf(islandName, sizeof(islandName), "%s", (const char*)fname);
        char* ext = strchr(islandName, '.');
        if (ext)
        {
            *ext = 0;
        }
        strcpy(Glob.header.worldname, islandName);

        const ParamEntry& cls = Pars >> "CfgWorlds" >> Glob.header.worldname;
        float grid = cls >> "LandGrid";
        DWORD tLoad = GetTickCount();
        land->LoadData(name, grid);
        LOG_DEBUG(Core, "LOAD: LoadData {}ms", GetTickCount() - tLoad);

        DWORD tCfg = GetTickCount();
        ParseCfgWorld();
        LOG_DEBUG(Core, "LOAD: ParseCfgWorld {}ms", GetTickCount() - tCfg);

        DWORD tInit = GetTickCount();
        InitLandscape(land);
        land->RebuildIDCache();
        LOG_DEBUG(Core, "LOAD: InitLandscape+RebuildIDCache {}ms", GetTickCount() - tInit);
    }
    else
    {
        land->ResetState();
        Reset();
        land->FlushCache();
        if (!_map)
        {
            _showMap = false;
        }
    }

    ENGINE_CONFIG.tacticalZ = 900;
    ENGINE_CONFIG.horizontZ = 900;
    ENGINE_CONFIG.objectsZ = 600;
    ENGINE_CONFIG.shadowsZ = 250;

    InputSubsystem::Instance().ResetLookAroundToggle();

    GLandscape->Simulate(0);
    GScene->ResetFog();

    DWORD tFlush = GetTickCount();
    _engine->TextBank()->FlushTextures();
    GetNetworkManager().CleanUpMemory();
    MemoryCleanUp();
    LOG_DEBUG(Core, "LOAD: SwitchLandscape total {}ms (flush {}ms)", GetTickCount() - t0, GetTickCount() - tFlush);
}

void World::AddVehicle(Entity* vehicle)
{
#if LOG_ADD_REMOVE_VEHICLE
    LOG_DEBUG(World, "World::AddVehicle {}", (const char*)vehicle->GetDebugName());
#endif
    DoAssert(vehicle->RefCounter() == 0 || _vehicles.Find(vehicle) < 0);
    _vehicles.Add(vehicle);
}
void World::RemoveVehicle(Entity* vehicle)
{
#if LOG_ADD_REMOVE_VEHICLE
    LOG_DEBUG(World, "World::RemoveVehicle {}", (const char*)vehicle->GetDebugName());
#endif
    DoAssert(_vehicles.Find(vehicle) >= 0);
    _vehicles.Remove(vehicle);
}
void World::InsertVehicle(Entity* vehicle)
{
#if LOG_ADD_REMOVE_VEHICLE
    LOG_DEBUG(World, "World::InsertVehicle {}", (const char*)vehicle->GetDebugName());
#endif
    DoAssert(vehicle->RefCounter() == 0 || _vehicles.Find(vehicle) < 0);
    _vehicles.Insert(vehicle);
}
void World::DeleteVehicle(Entity* vehicle)
{
#if LOG_ADD_REMOVE_VEHICLE
    LOG_DEBUG(World, "World::DeleteVehicle {}", (const char*)vehicle->GetDebugName());
#endif
    DoAssert(_vehicles.Find(vehicle) >= 0);
    _vehicles.Delete(vehicle);
}

void World::AddOutVehicle(Entity* vehicle)
{
#if LOG_ADD_REMOVE_VEHICLE
    LOG_DEBUG(World, "World::AddOutVehicle {}", (const char*)vehicle->GetDebugName());
#endif
    DoAssert(vehicle->RefCounter() == 0 || _outVehicles.Find(vehicle) < 0);
    _outVehicles.Add(vehicle);
}

void World::RemoveOutVehicle(Entity* vehicle)
{
#if LOG_ADD_REMOVE_VEHICLE
    LOG_DEBUG(World, "World::RemoveOutVehicle {}", (const char*)vehicle->GetDebugName());
#endif
    DoAssert(_outVehicles.Find(vehicle) >= 0);
    _outVehicles.Delete(vehicle);
}

bool World::ValidateOutVehicle(Entity* veh, bool complex) const
{
    bool ok = true;
    for (int i = 0; i < NVehicles(); i++)
    {
        Entity* v = GetVehicle(i);
        if (v == veh)
        {
            ok = false;
            RptF("Out Vehicle %s in normal list", (const char*)veh->GetDebugName());
        }
    }

    for (int zz = 0; zz < LandRange; zz++)
    {
        for (int xx = 0; xx < LandRange; xx++)
        {
            const ObjectList& list = GLandscape->GetObjects(zz, xx);
            for (int i = 0; i < list.Size(); i++)
            {
                if (list[i] == veh)
                {
                    RptF("Out Vehicle %s in landscape (%d,%d)", (const char*)veh->GetDebugName(), xx, zz);
                }
            }
        }
    }

    return ok;
}

bool World::ValidateOutVehicles(bool complex) const
{
    bool ok = true;
    for (int i = 0; i < NOutVehicles(); i++)
    {
        Entity* veh = GetOutVehicle(i);
        if (!ValidateOutVehicle(veh, complex))
        {
            ok = false;
        }
    }
    return ok;
}

bool World::CheckVehicleStructure() const
{
    bool ok = true;
    for (int i = 0; i < NVehicles(); i++)
    {
        Entity* vehicle = GetVehicle(i);
        EntityAI* veh = dyn_cast<EntityAI>(vehicle);
        AIUnit* unit = veh->CommanderUnit();
        if (unit)
        {
            if (!unit->AssertValid())
            {
                ok = false;
            }
        }
        unit = veh->PilotUnit();
        if (unit)
        {
            if (!unit->AssertValid())
            {
                ok = false;
            }
        }
        unit = veh->GunnerUnit();
        if (unit)
        {
            if (!unit->AssertValid())
            {
                ok = false;
            }
        }
    }
    for (int i = 0; i < NOutVehicles(); i++)
    {
        Entity* vehicle = GetOutVehicle(i);
        EntityAI* veh = dyn_cast<EntityAI>(vehicle);
        AIUnit* unit = veh->CommanderUnit();
        if (unit)
        {
            if (!unit->AssertValid())
            {
                ok = false;
            }
        }
        unit = veh->PilotUnit();
        if (unit)
        {
            if (!unit->AssertValid())
            {
                ok = false;
            }
        }
        unit = veh->GunnerUnit();
        if (unit)
        {
            if (!unit->AssertValid())
            {
                ok = false;
            }
        }
    }
    return ok;
}

inline bool IsPrimary(Object* vehicle)
{
    return (vehicle->GetType() == Primary || vehicle->GetType() == Network);
}

void World::ResetIDs() const
{
    Log("World::ResetIDs");
    _scene.GetLandscape()->ResetObjectIDs();
    int i;
    for (i = 0; i < NVehicles(); i++)
    {
        Entity* vehicle = GetVehicle(i);
        if (IsPrimary(vehicle))
        {
            PoseidonAssert(vehicle->ID() >= 0);
            continue;
        }
        int id = _scene.GetLandscape()->NewObjectID();
        vehicle->SetID(id);
    }
    for (i = 0; i < NAnimals(); i++)
    {
        Entity* vehicle = GetAnimal(i);
        if (IsPrimary(vehicle))
        {
            PoseidonAssert(vehicle->ID() >= 0);
            continue;
        }
        int id = _scene.GetLandscape()->NewObjectID();
        vehicle->SetID(id);
    }
    for (i = 0; i < NBuildings(); i++)
    {
        Entity* vehicle = GetBuilding(i);
        if (IsPrimary(vehicle))
        {
            PoseidonAssert(vehicle->ID() >= 0);
            continue;
        }
        int id = _scene.GetLandscape()->NewObjectID();
        vehicle->SetID(id);
    }
    for (i = 0; i < NFastVehicles(); i++)
    {
        Entity* vehicle = GetFastVehicle(i);
        if (IsPrimary(vehicle))
        {
            Fail("Fast primary vehicle");
            PoseidonAssert(vehicle->ID() >= 0);
            continue;
        }
        int id = _scene.GetLandscape()->NewObjectID();
        vehicle->SetID(id);
    }
    for (i = 0; i < NOutVehicles(); i++)
    {
        Entity* vehicle = GetOutVehicle(i);
        if (IsPrimary(vehicle))
        {
            Fail("Out primary vehicle");
            PoseidonAssert(vehicle->ID() >= 0);
            continue;
        }
        int id = _scene.GetLandscape()->NewObjectID();
        vehicle->SetID(id);
    }
}
void World::RemoveIDs() const
{
    Fail("Obsolete - do not use");
    Log("World::RemoveIDs");
    _scene.GetLandscape()->ResetObjectIDs();
    int i;
    for (i = 0; i < NVehicles(); i++)
    {
        Entity* vehicle = GetVehicle(i);
        if (IsPrimary(vehicle))
        {
            continue;
        }
        vehicle->SetID(-1);
    }
    for (i = 0; i < NAnimals(); i++)
    {
        Entity* vehicle = GetAnimal(i);
        if (IsPrimary(vehicle))
        {
            continue;
        }
        vehicle->SetID(-1);
    }
    for (i = 0; i < NBuildings(); i++)
    {
        Entity* vehicle = GetBuilding(i);
        if (IsPrimary(vehicle))
        {
            continue;
        }
        vehicle->SetID(-1);
    }
    for (i = 0; i < NOutVehicles(); i++)
    {
        Entity* vehicle = GetOutVehicle(i);
        if (IsPrimary(vehicle))
        {
            continue;
        }
        vehicle->SetID(-1);
    }
}

} // namespace Poseidon
#include <Poseidon/Core/SaveVersion.hpp>

namespace Poseidon
{
RString GetBaseDirectory();
RString GetBaseSubdirectory();
RString GetCampaignSaveDirectory(RString campaign);
bool ParseMission(bool);
void StartCampaign(RString, Display*);
void OpenEditor();
} // namespace Poseidon

LSError SerializeWorldSimulationTime(ParamArchive& ar)
{
    using namespace Poseidon;
    int simulationTimeMs = Glob.time.toInt();
    PARAM_CHECK(ar.Serialize("SimulationTimeMs", simulationTimeMs, 13, 0))
    Glob.time = Foundation::Time(simulationTimeMs);
    return LSOK;
}
namespace Poseidon
{

LSError World::Load(const char* name, int message)
{
    Fail("Text load obsolete");
    GDebugger.NextAliveExpected(15 * 60 * 1000);
    ParamArchiveLoad ar(name);
    ar.FirstPass();
    LSError err = Serialize(ar, message);
    if (err == LSOK)
    {
        ar.SecondPass();
        err = Serialize(ar, message);
    }
    if (err == LSOK)
    {
    }
    else
    {
        ErrorMessage("Cannot load '%s'. Error '%s' at '%s'.", name, ar.GetErrorName(err),
                     (const char*)ar.GetErrorContext());
    }
    return err;
}

LSError World::Save(const char* name, int message) const
{
    Fail("Text save obsolete");
    GDebugger.NextAliveExpected(15 * 60 * 1000);
    ParamArchiveSave ar(WorldSerializeVersion);
    World* w = const_cast<World*>(this);
    PARAM_CHECK(w->Serialize(ar, message))
    return ar.Save(name);
}

bool World::LoadBin(const char* name, int message)
{
    LOG_DEBUG(World, "LoadBin: Start - Total allocated: {} MB", Foundation::MemoryUsed() / (1024 * 1024));
    LSError err;
    {
        GDebugger.NextAliveExpected(15 * 60 * 1000);
        ParamArchiveLoad ar;
        bool result = ar.LoadBin(name);
        if (!result)
        {
            return false;
        }

        LOG_DEBUG(World, "Load: Total allocated after ar.LoadBin: {} MB", Foundation::MemoryUsed() / (1024 * 1024));
        ar.FirstPass();
        err = Serialize(ar, message);
        if (err == LSOK)
        {
            ar.SecondPass();
            err = Serialize(ar, message);
        }
        if (err == LSOK)
        {
        }
        else
        {
            ErrorMessage("Cannot load '%s'. Error '%s' at '%s'.", name, ar.GetErrorName(err),
                         (const char*)ar.GetErrorContext());
        }
        LOG_DEBUG(World, "Load: Total allocated after World::Serialize: {} MB",
                  Foundation::MemoryUsed() / (1024 * 1024));
    }
    LOG_DEBUG(World, "Total allocated after ~ParamArchive: {} MB", Foundation::MemoryUsed() / (1024 * 1024));
    MemoryCleanUp();
    LOG_DEBUG(World, "Total allocated after MemoryCleanUp: {} MB", Foundation::MemoryUsed() / (1024 * 1024));
    return err == LSOK;
}

bool World::SaveBin(const char* name, int message) const
{
    bool ret;
    {
        GDebugger.NextAliveExpected(15 * 60 * 1000);
        ParamArchiveSave ar(WorldSerializeVersion);
        World* w = const_cast<World*>(this);
        LOG_DEBUG(World, "SaveBin: Start - Total allocated: {} MB", Foundation::MemoryUsed() / (1024 * 1024));
        if (w->Serialize(ar, message) != LSOK)
        {
            return false;
        }

        LOG_DEBUG(World, "Total allocated after World::Serialize: {} MB", Foundation::MemoryUsed() / (1024 * 1024));
        ret = ar.SaveBin(name);
        LOG_DEBUG(World, "Total allocated after ar.SaveBin: {} MB", Foundation::MemoryUsed() / (1024 * 1024));
        if (!ret)
        {
            LOG_WARN(World, "SaveBin failed for '{}'", name);
        }
    }

    LOG_DEBUG(World, "Total allocated after ~ParamArchive: {} MB", Foundation::MemoryUsed() / (1024 * 1024));
    MemoryCleanUp();
    LOG_DEBUG(World, "Total allocated after MemoryCleanUp: {} MB", Foundation::MemoryUsed() / (1024 * 1024));
    return ret;
}

LSError World::SerializeVehicles(ParamArchive& ar)
{
    // Note: PARAM_CHECK(ar.Serialize("Cloudlets", _cloudlets, 1))
    PARAM_CHECK(ar.Serialize("FastVehicles", _fastVehicles, 1))
    PARAM_CHECK(ar.Serialize("Vehicles", _vehicles, 1))
    PARAM_CHECK(ar.Serialize("Animals", _animals, 1))
    PARAM_CHECK(ar.Serialize("Buildings", _buildings, 1))
    PARAM_CHECK(ar.Serialize("OutVehicles", _outVehicles, 1))
    if (ar.IsLoading())
    {
        for (int i = 0; i < _outVehicles.Size(); i++)
        {
            Entity* veh = _outVehicles[i];
            veh->SetMoveOutFlag();
        }
    }
    PARAM_CHECK(ar.Serialize("NearImportance", _nearImportanceDistributionTime, 1))
    PARAM_CHECK(ar.Serialize("FarImportance", _farImportanceDistributionTime, 1))
    return LSOK;
}

template <>
const ::Poseidon::Foundation::EnumName* ::Poseidon::Foundation::GetEnumNames(GameMode dummy)
{
    static const ::Poseidon::Foundation::EnumName GameModeNames[] = {
        ::Poseidon::Foundation::EnumName(GModeNetware, "NETWARE"),
        ::Poseidon::Foundation::EnumName(GModeArcade, "ARCADE"),
        ::Poseidon::Foundation::EnumName(GModeIntro, "INTRO"),
        ::Poseidon::Foundation::EnumName(GModeArcade, "NORMAL"),
        ::Poseidon::Foundation::EnumName(GModeArcade, "TRAINING"),
        ::Poseidon::Foundation::EnumName()};
    return GameModeNames;
}

bool ProcessTemplateName(RString name);
bool ProcessFullName(RString name);
void StartIntro();
void StartMission();
} // namespace Poseidon
namespace Poseidon
{
LSError World::Serialize(ParamArchive& ar, int message)
{
    if (ar.IsSaving())
    {
        ProgressReset();
        ProgressClear(false);
        ProgressStart(LocalizeString(message));
        PARAM_CHECK(ar.Serialize("CurrentCampaign", CurrentCampaign, 1, ""))
    }
    else if (ar.GetPass() == ParamArchive::PassFirst)
    {
        RString campaign;
        PARAM_CHECK(ar.Serialize("CurrentCampaign", campaign, 1, ""))
        SetCampaign(campaign);
    }

    PARAM_CHECK(ar.Serialize("CurrentBattle", CurrentBattle, 1, ""))
    PARAM_CHECK(ar.Serialize("CurrentMission", CurrentMission, 1, ""))

    PARAM_CHECK(ar.SerializeEnum("mode", _mode, 1))
    PARAM_CHECK(ar.SerializeEnum("endMission", _endMission, 1, (EndMode)EMContinue))

    PARAM_CHECK(ar.Serialize("cadetMode", USER_CONFIG.easyMode, 1, false))

    PARAM_CHECK(ar.Serialize("nextMagazineID", _nextMagazineID, 1, 0))

    PARAM_CHECK(ar.Serialize("enableRadio", _enableRadio, 1, true))
    PARAM_CHECK(ar.Serialize("Radio", *_radio, 1))
    //	PARAM_CHECK(ar.Serialize("Map", *_map, 11))
    PARAM_CHECK(SerializeMapInfo(ar, "Map", 11))

    Landscape* land = _scene.GetLandscape();
    if (ar.IsSaving())
    {
        ResetIDs();
        land->RebuildIDCache();

        RString worldName = land->GetName();
        PARAM_CHECK(ar.Serialize("worldName", worldName, 1))

        AutoArray<RString> addons;
        for (int i = 0; i < _activeAddons.GetSize(); i++)
        {
            addons.Add(_activeAddons.Get(i));
        }
        PARAM_CHECK(ar.SerializeArray("addons", addons, 1))
    }
    else if (ar.GetPass() == ParamArchive::PassFirst)
    {
        VehicleTypes.LockAllTypes();
        GEngine->TextBank()->LockAllTextures();

        Clear();
        CurrentTemplate.Clear();

        RString worldName;
        PARAM_CHECK(ar.Serialize("worldName", worldName, 1))
        SwitchLandscape(worldName);
        AdjustSubdivision(GModeArcade);

        ProgressReset();
        ProgressStart(LocalizeString(message));
        FindArrayRStringCI addons;
        PARAM_CHECK(ar.SerializeArray("addons", addons, 1))
        ActivateAddons(addons);
    }
    DWORD tLand = GetTickCount();
    PARAM_CHECK(ar.Serialize("Landscape", *land, 1))
    LOG_DEBUG(Core, "LOAD: Serialize Landscape {}ms", GetTickCount() - tLand);

    DWORD tVeh = GetTickCount();
    PARAM_CHECK(SerializeVehicles(ar))
    LOG_DEBUG(Core, "LOAD: SerializeVehicles {}ms", GetTickCount() - tVeh);
    PARAM_CHECK(ar.Serialize("SensorList", _sensorList, 1))
    // A save written without a SensorList subclass deserializes the SRef as null
    // (ParamArchive::Serialize sets value = nullptr when the named entry is absent). The
    // world invariant is "always has a (possibly empty) sensor list" — World::Simulate
    // dereferences GetSensorList() unconditionally, so a null here is a latent crash
    // (SensorList::CheckPos, this = null). Restore an empty list on the final load pass:
    // doing it earlier would leave _sensorList non-null while the entry is still absent,
    // which trips the second-pass serialize's OpenSubclass into LSNoEntry.
    if (!ar.IsSaving() && ar.GetPass() == ParamArchive::PassSecond && !_sensorList)
    {
        _sensorList = new SensorList;
    }
    PARAM_CHECK(ar.Serialize("Clock", Glob.clock, 1))
    PARAM_CHECK(SerializeWorldSimulationTime(ar))
    PARAM_CHECK(ar.Serialize("GameState", GGameState, 1))

    PARAM_CHECK(ar.Serialize("actualOvercast", _actualOvercast, 1))
    PARAM_CHECK(ar.Serialize("wantedOvercast", _wantedOvercast, 1))
    PARAM_CHECK(ar.Serialize("actualFog", _actualFog, 1))
    PARAM_CHECK(ar.Serialize("wantedFog", _wantedFog, 1))
    PARAM_CHECK(ar.Serialize("speedOvercast", _speedOvercast, 1))
    PARAM_CHECK(ar.Serialize("weatherTime", _weatherTime, 1))
    PARAM_CHECK(ar.Serialize("nextWeatherChange", _nextWeatherChange, 1))

    PARAM_CHECK(ar.Serialize("horizontZ", ENGINE_CONFIG.horizontZ, 1, 900))
    PARAM_CHECK(ar.Serialize("tacticalZ", ENGINE_CONFIG.tacticalZ, 1, 900))
    PARAM_CHECK(ar.Serialize("objectsZ", ENGINE_CONFIG.objectsZ, 1, 600))
    PARAM_CHECK(ar.Serialize("shadowsZ", ENGINE_CONFIG.shadowsZ, 1, 250))

    PARAM_CHECK(ar.SerializeRef("playerOn", _playerOn, 1))
    PARAM_CHECK(ar.SerializeRef("cameraOn", _cameraOn, 1))
    PARAM_CHECK(ar.SerializeRef("realPlayer", _realPlayer, 1))
    PARAM_CHECK(ar.Serialize("playerManual", _playerManual, 1, true))
    PARAM_CHECK(ar.Serialize("playerSuspended", _playerSuspended, 1, false))

    PARAM_CHECK(ar.Serialize("EastCenter", _eastCenter, 1))
    PARAM_CHECK(ar.Serialize("WestCenter", _westCenter, 1))
    PARAM_CHECK(ar.Serialize("GuerrilaCenter", _guerrilaCenter, 1))
    PARAM_CHECK(ar.Serialize("CivilianCenter", _civilianCenter, 1))
    PARAM_CHECK(ar.Serialize("LogicCenter", _logicCenter, 1))
    PARAM_CHECK(AIGlobalSerialize(ar))

    PARAM_CHECK(ar.Serialize("Scripts", _scripts, 3))

    PARAM_CHECK(ar.Serialize("OnMapSingleClick", GMapOnSingleClick, 1, RString()))

    PARAM_CHECK(ar.Serialize("CameraEffect", _cameraEffect, 1))

    if (ar.IsSaving())
    {
        RString dir = GetBaseDirectory();
        PARAM_CHECK(ar.Serialize("directory", dir, 1, ""))
        dir = GetBaseSubdirectory();
        PARAM_CHECK(ar.Serialize("subdirectory", dir, 1, ""))
        RString mission = Glob.header.filename;
        PARAM_CHECK(ar.Serialize("mission", mission, 1, ""))
        PARAM_CHECK(ar.Serialize("filenameReal", Glob.header.filenameReal, 1, ""))
        ProgressFinish();
    }
    else if (ar.GetPass() == ParamArchive::PassFirst)
    {
        char wname[256];
        snprintf(wname, sizeof(wname), "%s", (const char*)land->GetName());
        char* ext = strrchr(wname, '.');
        if (ext)
        {
            *ext = 0;
        }
        char* world = strrchr(wname, '\\');
        if (world)
        {
            world++;
        }
        else
        {
            world = wname;
        }

        RString dir;
        PARAM_CHECK(ar.Serialize("directory", dir, 1, ""))
        SetBaseDirectory(dir);
        RString mission;
        PARAM_CHECK(ar.Serialize("mission", mission, 1, ""))
        SetMission(world, mission);
        PARAM_CHECK(ar.Serialize("subdirectory", dir, 1, ""))
        SetBaseSubdirectory(dir);
        PARAM_CHECK(ar.Serialize("filenameReal", Glob.header.filenameReal, 1, ""))

        ParseMission(false);
    }
    else
    {
        DWORD tFinal = GetTickCount();
        _camType = _camTypeMain = CamInternal;
        InitCameraPars();

        _scene.MainLight()->Recalculate(this);
        _scene.MainLightChanged();

        AIUnit* player = _playerOn ? _playerOn->Brain() : nullptr;
        AIGroup* grp = player ? player->GetGroup() : nullptr;
        AICenter* center = grp ? grp->GetCenter() : nullptr;
        Glob.header.playerSide = center ? center->GetSide() : TSideUnknown;

        VehicleTypes.UnlockAllTypes();
        GEngine->TextBank()->UnlockAllTextures();
        DWORD tPreload = GetTickCount();
        GEngine->TextBank()->Preload();
        LOG_DEBUG(Core, "LOAD: TextBank Preload {}ms", GetTickCount() - tPreload);

        DWORD tOpt = GetTickCount();
        Shapes.OptimizeAll();
        LOG_DEBUG(Core, "LOAD: Shapes.OptimizeAll {}ms", GetTickCount() - tOpt);

        DisplayMap* map = dynamic_cast<DisplayMap*>((AbstractOptionsUI*)_map);
        if (map)
        {
            map->UpdatePlan();
        }

        LOG_DEBUG(Core, "LOAD: Final pass total {}ms", GetTickCount() - tFinal);
        ProgressFinish();
    }

    return LSOK;
}

void World::DoKeyDown(unsigned wParam, unsigned nRepCnt, unsigned nFlags)
{
    if (!IsUserInputEnabled())
    {
        return;
    }

    if (_warningMessage)
    {
        _warningMessage->OnKeyDown(wParam, nRepCnt, nFlags);
        if (_warningMessage->GetExitCode() >= 0)
        {
            _warningMessage = nullptr;
        }
        return;
    }
    if (_voiceChat)
    {
        if (_voiceChat->DoKeyDown(wParam, nRepCnt, nFlags))
        {
            return;
        }
    }
    if (_chat)
    {
        if (_chat->DoKeyDown(wParam, nRepCnt, nFlags))
        {
            return;
        }
    }
    if (!HasOptions())
    {
        if (_userDlg)
        {
            if (_userDlg->DoKeyDown(wParam, nRepCnt, nFlags))
            {
                return;
            }
        }
        else if (_map && _showMap)
        {
            if (_map->DoKeyDown(wParam, nRepCnt, nFlags))
            {
                return;
            }
        }
    }
    if (_options)
    {
        if (_options->DoKeyDown(wParam, nRepCnt, nFlags))
        {
            return;
        }
    }
}

bool World::DoControllerUiAction(ControllerUiAction action)
{
    if (!IsUserInputEnabled())
        return false;

    ControlsContainer* topmost = UIActiveDisplay::FindTopmost(this);
    DisplayArcadeMap* editor = dynamic_cast<DisplayArcadeMap*>(topmost);
    if (editor)
        return editor->DoControllerUiAction(action);
    DisplayMission* mission = dynamic_cast<DisplayMission*>(topmost);
    if (mission)
        return mission->DoControllerUiAction(action);
    if (topmost && FindMissionOwner(topmost))
    {
        if (auto* display = dynamic_cast<Display*>(topmost))
            return display->DoControllerUiAction(action);
    }
    if (topmost && FindEditorOwner(topmost))
        return DoEditorChildControllerUiAction(topmost, action);
    if (auto* display = dynamic_cast<Display*>(topmost))
        return display->DoControllerUiAction(action);
    return false;
}

ControllerUiScene World::GetControllerUiScene() const
{
    ControlsContainer* topmost = UIActiveDisplay::FindTopmost(const_cast<World*>(this));
    DisplayArcadeMap* editor = dynamic_cast<DisplayArcadeMap*>(topmost);
    if (editor)
        return editor->GetControllerUiScene();

    DisplayMission* mission = dynamic_cast<DisplayMission*>(topmost);
    if (mission)
        return mission->GetControllerUiScene();

    if (topmost && FindMissionOwner(topmost))
    {
        if (auto* display = dynamic_cast<Display*>(topmost))
            return display->GetControllerUiScene();
    }

    if (topmost && FindEditorOwner(topmost))
        return EditorDialogControllerScene();

    if (auto* display = dynamic_cast<Display*>(topmost))
        return display->GetControllerUiScene();

    return GameplayControllerScene();
}

bool World::IsEditorControllerUiActive()
{
    const ControllerUiScene scene = GetControllerUiScene();
    return scene.kind == ControllerSceneKind::EditorMap || scene.kind == ControllerSceneKind::EditorDialog;
}

void World::DoKeyUp(unsigned wParam, unsigned nRepCnt, unsigned nFlags)
{
    if (!IsUserInputEnabled())
    {
        return;
    }

    if (_warningMessage)
    {
        _warningMessage->OnKeyUp(wParam, nRepCnt, nFlags);
        if (_warningMessage->GetExitCode() >= 0)
        {
            _warningMessage = nullptr;
        }
        return;
    }
    if (_voiceChat)
    {
        if (_voiceChat->DoKeyUp(wParam, nRepCnt, nFlags))
        {
            return;
        }
    }
    if (_chat)
    {
        if (_chat->DoKeyUp(wParam, nRepCnt, nFlags))
        {
            return;
        }
    }
    if (!HasOptions())
    {
        if (_userDlg)
        {
            if (_userDlg->DoKeyUp(wParam, nRepCnt, nFlags))
            {
                return;
            }
        }
        else if (_map && _showMap)
        {
            if (_map->DoKeyUp(wParam, nRepCnt, nFlags))
            {
                return;
            }
        }
    }
    if (_options)
    {
        if (_options->DoKeyUp(wParam, nRepCnt, nFlags))
        {
            return;
        }
    }
}

void World::DoChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    if (!IsUserInputEnabled())
    {
        return;
    }

    if (_warningMessage)
    {
        _warningMessage->OnChar(nChar, nRepCnt, nFlags);
        if (_warningMessage->GetExitCode() >= 0)
        {
            _warningMessage = nullptr;
        }
        return;
    }
    if (_voiceChat)
    {
        if (_voiceChat->DoChar(nChar, nRepCnt, nFlags))
        {
            return;
        }
    }
    if (_chat)
    {
        if (_chat->DoChar(nChar, nRepCnt, nFlags))
        {
            return;
        }
    }
    if (!HasOptions())
    {
        if (_userDlg)
        {
            if (_userDlg->DoChar(nChar, nRepCnt, nFlags))
            {
                return;
            }
        }
        else if (_map && _showMap)
        {
            if (_map->DoChar(nChar, nRepCnt, nFlags))
            {
                return;
            }
        }
    }
    if (_options)
    {
        if (_options->DoChar(nChar, nRepCnt, nFlags))
        {
            return;
        }
    }
}

void World::DoIMEChar(unsigned nChar, unsigned nRepCnt, unsigned nFlags)
{
    if (!IsUserInputEnabled())
    {
        return;
    }

    if (_warningMessage)
    {
        _warningMessage->OnIMEChar(nChar, nRepCnt, nFlags);
        if (_warningMessage->GetExitCode() >= 0)
        {
            _warningMessage = nullptr;
        }
        return;
    }
    if (_voiceChat)
    {
        if (_voiceChat->DoIMEChar(nChar, nRepCnt, nFlags))
        {
            return;
        }
    }
    if (_chat)
    {
        if (_chat->DoIMEChar(nChar, nRepCnt, nFlags))
        {
            return;
        }
    }
    if (!HasOptions())
    {
        if (_userDlg)
        {
            if (_userDlg->DoIMEChar(nChar, nRepCnt, nFlags))
            {
                return;
            }
        }
        else if (_map && _showMap)
        {
            if (_map->DoIMEChar(nChar, nRepCnt, nFlags))
            {
                return;
            }
        }
    }
    if (_options)
    {
        if (_options->DoIMEChar(nChar, nRepCnt, nFlags))
        {
            return;
        }
    }
}

void World::DoIMEComposition(unsigned nChar, unsigned nFlags)
{
    if (!IsUserInputEnabled())
    {
        return;
    }

    if (_warningMessage)
    {
        _warningMessage->OnIMEComposition(nChar, nFlags);
        if (_warningMessage->GetExitCode() >= 0)
        {
            _warningMessage = nullptr;
        }
        return;
    }
    if (_voiceChat)
    {
        if (_voiceChat->DoIMEComposition(nChar, nFlags))
        {
            return;
        }
    }
    if (_chat)
    {
        if (_chat->DoIMEComposition(nChar, nFlags))
        {
            return;
        }
    }
    if (!HasOptions())
    {
        if (_userDlg)
        {
            if (_userDlg->DoIMEComposition(nChar, nFlags))
            {
                return;
            }
        }
        else if (_map && _showMap)
        {
            if (_map->DoIMEComposition(nChar, nFlags))
            {
                return;
            }
        }
    }
    if (_options)
    {
        if (_options->DoIMEComposition(nChar, nFlags))
        {
            return;
        }
    }
}

void World::SaveCrash() const
{
    SaveBin("$_crash_$.fps", IDS_SAVE_GAME);
}

void SaveCrash()
{
    GWorld->SaveCrash();
}

CameraEffect::CameraEffect(Object* object) : _object(object) {}
CameraEffect::~CameraEffect() = default;

void CameraEffect::Draw() const
{
    if (showCinemaBorder)
    {
        Object cinema(GScene->Preloaded(CinemaBorder), -1);
        cinema.Draw2D(0);
        // The CinemaBorder model is 4:3-designed; on wider viewports
        // its bars don't reach the screen edges.  See Object::
        // DrawWidescreenPillarbox for the why.
        Object::DrawWidescreenPillarbox();
    }
}

void World::StartIntro()
{
    CurrentBattle = "";
    CurrentMission = "";

    const char* ext = strrchr(LoadFile, '.');
    if (ext && QIFStreamB::FileExist(LoadFile))
    {
        if (!strcmpi(ext, ".fps"))
        {
            LoadBin(LoadFile, IDS_LOAD_GAME);
            StartMission();
        }
        else if (!strcmpi(ext, ".sqg"))
        {
            Load(LoadFile, IDS_LOAD_GAME);
            StartMission();
        }
        else if (GameModuleRegistry::IsRegistered(GameModuleId::Editor) && !strcmpi(ext, ".sqm"))
        {
            if (ProcessFullName(LoadFile))
            {
                if (AutoTest)
                {
                    if (!StartAutoTest())
                    {
                        LOG_ERROR(Core, "StartAutoTest could not boot '{}'", LoadFile);
                        if (AppConfig::Instance().IsMissionSmokeCheck())
                        {
                            LOG_ERROR(Core, "Mission smoke check failed: StartAutoTest could not boot '{}'", LoadFile);
                            GApp->m_exitCode = 1;
                            GApp->m_closeRequest = true;
                        }
                    }
                }
                else
                {
                    OpenEditor();
                }
            }
        }
    }
    else
    {
        RString campaign = Pars >> "CfgIntro" >> "firstCampaign";
        if (campaign.GetLength() > 0 &&
            QIFStream::FileExists(GetCampaignSaveDirectory(campaign) + RString("continue.fps")))
        {
            StartCampaign(campaign, nullptr);
        }
        else
        {
            RString world = GetMenuInitWorld();
            StartRandomCutscene(world);
        }
    }
}

void World::StopIntro() {}

void World::StartLogo() {}

void World::StopLogo() {}
} // namespace Poseidon
