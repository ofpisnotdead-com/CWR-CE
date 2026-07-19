
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Core/Version.hpp>
#include <Poseidon/World/Scene/Object.hpp>
#include <Poseidon/World/Detection/Detector.hpp>
#include <Poseidon/World/Scene/ObjectClasses.hpp>
#include <Poseidon/World/Scene/Fireplace.hpp>

#include <Poseidon/Game/Commands/GameStateExt.hpp>

#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/AI/VehicleAI.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/AI/AIRadio.hpp>
#include <Poseidon/UI/Map/UIMap.hpp>
#include <Poseidon/World/Entities/Infantry/SoldierOld.hpp>
#include <Poseidon/World/Entities/Vehicles/Ground/Car.hpp>
#include <Poseidon/World/Entities/Vehicles/Ground/Tank.hpp>
#include <Poseidon/World/Entities/Vehicles/Air/Airplane.hpp>
#include <Poseidon/World/Entities/Vehicles/House.hpp>
#include <Poseidon/Audio/DynSound.hpp>
#include <Poseidon/World/Scene/Camera/CameraHold.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Core/resincl.hpp>

#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>

#include <Poseidon/Game/UiActions.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <Poseidon/Foundation/Platform/AppConfig.hpp>

#include <ctype.h>
#include <time.h>
#include <limits.h>
#include <string.h>
#include <string>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/BankArray.hpp>
#include <Poseidon/Foundation/Enums/EnumNames.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Strings/StrFormat.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

using Poseidon::Foundation::GetEnumValue;

using namespace Poseidon;
namespace Poseidon
{
} // namespace Poseidon

#ifdef _WIN32
#include <io.h>
#else
#include <dirent.h>
#endif

#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/IO/PreprocC/Preproc.h>

#include <Poseidon/Foundation/Platform/VersionNo.h>

#include <Poseidon/Game/Commands/GameStateExtCommon.hpp>

namespace Poseidon
{
void CreatePath(RString);
void AddDeadIdentity(RString);
} // namespace Poseidon

GameValue VehCreate(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    GameStringType type = oper1;

    Vector3 pos, normal = VUp;
    if (!GetPos(state, pos, oper2))
    {
        return OBJECT_NULL;
    }

    Ref<Entity> veh = NewNonAIVehicle(type, nullptr);
    if (!veh)
    {
        return OBJECT_NULL;
    }
    EntityAI* vehAI = dyn_cast<EntityAI>(veh.GetRef());
    if (vehAI)
    {
        if (AIUnit::FindFreePosition(pos, normal, false, vehAI))
        {
            float dx, dz;
            pos[1] = GLOB_LAND->SurfaceYAboveWater(pos[0], pos[2], &dx, &dz);
            normal = Vector3(-dx, 1, -dz);
        }
    }

    Matrix3 dir;
    Matrix4 transform;

    transform.SetPosition(pos);
    dir.SetUpAndDirection(normal, VForward);
    transform.SetOrientation(dir);

    if (vehAI)
    {
        veh->PlaceOnSurface(transform);
    }

    veh->SetTransform(transform);
    veh->Init(transform);

    if (vehAI)
    {
        if (veh->GetNonAIType()->IsKindOf(GWorld->Preloaded(VTypeStatic)))
        {
            GWorld->AddBuilding(veh);
            if (GWorld->GetMode() == GModeNetware)
            {
                GetNetworkManager().CreateVehicle(veh, VLTBuilding, "", -1);
            }
        }
        else
        {
            GWorld->AddVehicle(veh);
            if (GWorld->GetMode() == GModeNetware)
            {
                GetNetworkManager().CreateVehicle(veh, VLTVehicle, "", -1);
            }
        }
    }
    else
    {
        GWorld->AddAnimal(veh);
        if (GWorld->GetMode() == GModeNetware)
        {
            GetNetworkManager().CreateVehicle(veh, VLTAnimal, "", -1);
        }
    }

    return GameValueExt(veh.GetRef());
}

void CreateUnit(AIGroup* group, RString type, Vector3Par position, RString init, float skill, Rank rank)
{
    if (group->NUnits() >= MAX_UNITS_PER_GROUP)
    {
        return;
    }

    Ref<EntityAI> veh = NewVehicle(type);
    Person* soldier = dyn_cast<Person>(veh.GetRef());
    if (!soldier)
    {
        return;
    }

    Vector3 normal, pos = position;
    if (AIUnit::FindFreePosition(pos, normal, true, veh))
    {
        float dx, dz;
        pos[1] = GLOB_LAND->RoadSurfaceYAboveWater(pos[0], pos[2], &dx, &dz);
    }

    Matrix3 dir;
    Matrix4 transform;

    transform.SetPosition(pos);
    dir.SetUpAndDirection(VUp, VForward);
    transform.SetOrientation(dir);

    veh->PlaceOnSurface(transform);

    veh->SetTransform(transform);
    veh->Init(transform);

    AICenter* center = group->GetCenter();
    PoseidonAssert(center);

    veh->SetTargetSide(center->GetSide());

    GameValue varValue = GameValueExt(veh);
    GameVarSpace local;
    GWorld->GetGameState()->BeginContext(&local);
    GWorld->GetGameState()->VarSet("this", varValue);
    GWorld->GetGameState()->Execute(init);
    GWorld->GetGameState()->EndContext();

    GWorld->AddVehicle(veh);
    if (GWorld->GetMode() == GModeNetware)
    {
        GetNetworkManager().CreateVehicle(veh, VLTVehicle, "", -1);

        VehicleInitCmd cmd;
        cmd.vehicle = veh;
        cmd.init = init;
        GetNetworkManager().VehicleInit(cmd);
    }

    // free soldier needs its own sensor
    GWorld->AddSensor(soldier);

    AIUnit* unit = soldier->Brain();

    unit->Load(center->NextSoldierIdentity(soldier->IsWoman()));

    AIUnitInfo& aiInfo = soldier->GetInfo();
    aiInfo._rank = rank;
    aiInfo._initExperience = aiInfo._experience = AI::ExpForRank(rank);
    unit->SetAbility(skill);

    AISubgroup* subgroup = group->MainSubgroup();
    group->AddUnit(unit);
    if (!subgroup)
    {
        PoseidonAssert(group->MainSubgroup());
        GetNetworkManager().CreateObject(group->MainSubgroup());
    }
    GetNetworkManager().CreateObject(unit);
    if (!group->Leader())
    {
        center->SelectLeader(group);
    }
}

GameValue UnitCreate(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    GameStringType type = oper1;
    const GameArrayType& array = oper2;

    Vector3 pos;
    AIGroup* grp;
    float skill = 0.5;
    Rank rank = RankPrivate;
    RString init;
    switch (array.Size())
    {
        case 5:
        {
            if (array[4].GetType() != GameString)
            {
                state->TypeError(GameString, array[4].GetType());
                return NOTHING;
            }
            GameStringType rankName = array[4];
            rank = GetEnumValue<Rank>((const char*)rankName);
            if (rank == INT_MIN)
            {
                rank = RankPrivate;
            }
        }
        case 4:
        {
            if (array[3].GetType() != GameScalar)
            {
                state->TypeError(GameScalar, array[3].GetType());
                return NOTHING;
            }
            skill = array[3];
        }
        case 3:
        {
            if (array[2].GetType() != GameString)
            {
                state->TypeError(GameString, array[2].GetType());
                return NOTHING;
            }
            init = array[2];
        }
        case 2:
        {
            if (!GetRelPos(state, pos, array[0]))
            {
                // state->SetError(EvalGen);
                return NOTHING;
            }
            grp = GetGroup(array[1]);
            if (!grp)
            {
                return NOTHING;
            }
        }
        break;
        default:
            state->SetError(EvalDim, array.Size(), 5);
            return NOTHING;
    }

    if (grp->IsLocal())
    {
        CreateUnit(grp, type, pos, init, skill, rank);
    }
    else
    {
        GetNetworkManager().AskForCreateUnit(grp, type, pos, init, skill, rank);
    }

    return NOTHING;
}

static void Eject(Person* man, Transport* trans)
{
    if (!man)
    {
        return;
    }

    AIUnit* unit = man->Brain();
    if (!unit)
    {
        man->SetDelete();
        return;
    }
    unit->DoGetOut(trans, false);

    AIGroup* group = unit->GetGroup();
    if (group)
    {
        group->UnassignVehicle(trans);
    }
}

void DeleteVehicle(Entity* veh)
{
    // only regular entities may be deleted
    if (veh->Object::GetType() != TypeVehicle)
    {
        return;
    }

    Person* man = dyn_cast<Person>(veh);
    if (man)
    {
        AIUnit* unit = man->Brain();
        if (unit)
        {
            if (unit->IsAnyPlayer())
            {
                return;
            }
            Transport* transport = unit->VehicleAssigned();
            if (transport)
            {
                transport->UpdateStop();
            }
            GWorld->RemoveSensor(man);

            Ref<AISubgroup> subgrp = unit->GetSubgroup();
            Ref<AIGroup> grp = subgrp ? subgrp->GetGroup() : nullptr;
            if (grp)
            {
                bool groupLeader = unit == grp->Leader();
                bool subgroupLeader = unit == subgrp->Leader();
                unit->RemoveFromGroup();
                if (grp->NUnits() > 0 && groupLeader)
                {
                    grp->GetCenter()->SelectLeader(grp);
                }
                if (subgrp->NUnits() == 0)
                {
                    if (subgrp != grp->MainSubgroup())
                    {
                        subgrp->RemoveFromGroup();
                    }
                }
                else if (subgroupLeader)
                {
                    subgrp->SelectLeader();
                }
            }
            man->SetBrain(nullptr);
        }
    }
    else
    {
        Transport* transport = dyn_cast<Transport>(veh);
        if (transport)
        {
            for (int i = transport->GetManCargo().Size(); --i >= 0;)
            {
                Eject(transport->GetManCargo()[i], transport);
            }
            Eject(transport->Driver(), transport);
            Eject(transport->Gunner(), transport);
            Eject(transport->Commander(), transport);
            GetNetworkManager().UpdateObject(transport);
        }
    }
    veh->SetDelete();
}

GameValue VehDelete(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }

    Entity* veh = dyn_cast<Entity>(obj);
    if (!veh)
    {
        return NOTHING;
    }

    if (veh->IsLocal())
    {
        DeleteVehicle(veh);
    }
    else
    {
        GetNetworkManager().AskForDeleteVehicle(veh);
    }

    return NOTHING;
}

GameValue MarkerGetPos(const GameState* state, GameValuePar oper1)
{
    GameStringType name = oper1;

    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;
    array.Resize(3);
    array[0] = 0.0f;
    array[1] = 0.0f;
    array[2] = 0.0f;

    for (int i = 0; i < markersMap.Size(); i++)
    {
        ArcadeMarkerInfo& mInfo = markersMap[i];
        if (stricmp(mInfo.name, name) == 0)
        {
            array[0] = mInfo.position.X();
            array[1] = mInfo.position.Z();
            break;
        }
    }
    return value;
}

GameValue MarkerSetPos(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    GameStringType name = oper1;

    Vector3 pos;
    if (!GetPos(state, pos, oper2))
    {
        return NOTHING;
    }

    for (int i = 0; i < markersMap.Size(); i++)
    {
        ArcadeMarkerInfo& mInfo = markersMap[i];
        if (stricmp(mInfo.name, name) == 0)
        {
            mInfo.position = pos;
            break;
        }
    }
    return NOTHING;
}

GameValue MarkerGetType(const GameState* state, GameValuePar oper1)
{
    GameStringType name = oper1;

    for (int i = 0; i < markersMap.Size(); i++)
    {
        ArcadeMarkerInfo& mInfo = markersMap[i];
        if (stricmp(mInfo.name, name) == 0)
        {
            return mInfo.type;
        }
    }
    return GameStringType("");
}

GameValue MarkerSetType(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    GameStringType name = oper1;
    GameStringType type = oper2;

    for (int i = 0; i < markersMap.Size(); i++)
    {
        ArcadeMarkerInfo& mInfo = markersMap[i];
        if (stricmp(mInfo.name, name) == 0)
        {
            mInfo.type = type;
            mInfo.OnTypeChanged();
            break;
        }
    }
    return NOTHING;
}

GameValue MarkerGetSize(const GameState* state, GameValuePar oper1)
{
    GameStringType name = oper1;

    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;
    array.Resize(2);
    array[0] = 0.0f;
    array[1] = 0.0f;

    for (int i = 0; i < markersMap.Size(); i++)
    {
        ArcadeMarkerInfo& mInfo = markersMap[i];
        if (stricmp(mInfo.name, name) == 0)
        {
            array[0] = mInfo.a;
            array[1] = mInfo.b;
        }
    }
    return value;
}

GameValue MarkerSetSize(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    GameStringType name = oper1;

    const GameArrayType& size = oper2;
    if (size.Size() != 2 || size[0].GetType() != GameScalar || size[1].GetType() != GameScalar)
    {
        state->SetError(EvalGen);
        return NOTHING;
    }

    for (int i = 0; i < markersMap.Size(); i++)
    {
        ArcadeMarkerInfo& mInfo = markersMap[i];
        if (stricmp(mInfo.name, name) == 0)
        {
            mInfo.a = size[0];
            mInfo.b = size[1];
            break;
        }
    }
    return NOTHING;
}

GameValue MarkerGetColor(const GameState* state, GameValuePar oper1)
{
    GameStringType name = oper1;

    for (int i = 0; i < markersMap.Size(); i++)
    {
        ArcadeMarkerInfo& mInfo = markersMap[i];
        if (stricmp(mInfo.name, name) == 0)
        {
            return mInfo.colorName;
        }
    }
    return GameStringType("");
}

GameValue MarkerSetColor(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    GameStringType name = oper1;
    GameStringType color = oper2;

    for (int i = 0; i < markersMap.Size(); i++)
    {
        ArcadeMarkerInfo& mInfo = markersMap[i];
        if (stricmp(mInfo.name, name) == 0)
        {
            mInfo.colorName = color;
            mInfo.OnColorChanged();
            break;
        }
    }
    return NOTHING;
}

GameValue WpGetPos(const GameState* state, GameValuePar oper1)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;
    array.Resize(3);
    array[0] = 0.0f;
    array[1] = 0.0f;
    array[2] = 0.0f;

    const GameArrayType& wp = oper1;
    if (wp.Size() != 2)
    {
        state->SetError(EvalGen);
        return value;
    }

    AIGroup* grp = GetGroup(wp[0]);
    if (!grp)
    {
        return value;
    }

    if (wp[1].GetType() != GameScalar)
    {
        state->SetError(EvalGen);
        return value;
    }
    int index = toInt((float)wp[1]);

    if (index < 0 || index >= grp->NWaypoints())
    {
        return value;
    }
    ArcadeWaypointInfo& wInfo = grp->GetWaypoint(index);
    array[0] = wInfo.position.X();
    array[1] = wInfo.position.Z();

    return value;
}

GameValue WpSetPos(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    const GameArrayType& wp = oper1;
    if (wp.Size() != 2)
    {
        state->SetError(EvalGen);
        return NOTHING;
    }

    AIGroup* grp = GetGroup(wp[0]);
    if (!grp)
    {
        return NOTHING;
    }

    if (wp[1].GetType() != GameScalar)
    {
        state->SetError(EvalGen);
        return NOTHING;
    }
    int index = toInt((float)wp[1]);

    Vector3 pos;
    if (!GetPos(state, pos, oper2))
    {
        return NOTHING;
    }

    if (index < 0 || index >= grp->NWaypoints())
    {
        return NOTHING;
    }
    ArcadeWaypointInfo& wInfo = grp->GetWaypoint(index);
    wInfo.position = pos;

    int& cur = grp->GetCurrent()->_fsm->Var(0);
    if (cur == index)
    {
        AIGroupContext ctx(grp);
        ctx._fsm = grp->GetCurrent()->_fsm;
        ctx._task = const_cast<Mission*>(grp->GetMission());
        ctx._fsm->SetState(1, &ctx);
    }

    return NOTHING;
}

GameValue RequiredVersion(const GameState* state, GameValuePar oper1)
{
    RString strVersion = Poseidon::GetAppVersion();
    int version = VersionToInt(strVersion);

    RString strRequired = oper1;
    int required = VersionToInt(strRequired);
    if (required > version)
    {
        WarningMessage(LocalizeString(IDS_MSG_MISSION_VERSION), (const char*)strRequired);
        return false;
    }
    return true;
}

GameValue EstimatedTimeLeft(const GameState* state, GameValuePar oper1)
{
    float t = oper1;
    GetNetworkManager().SetEstimatedEndTime((t <= 7200) ? (Glob.time + t) : TIME_MIN);
    return NOTHING;
}

GameValue StrFormat(const GameState* state, GameValuePar oper1)
{
    const GameArrayType& array = oper1;
    if (array.Size() < 1 || array[0].GetType() != GameString)
    {
        return "";
    }
    RString format = array[0];

    int nParams = array.Size() - 1;
    // The result is unbounded (params can be arbitrarily long), so accumulate into a
    // growable buffer. A fixed stack buffer here overflows on long output and crashes.
    std::string result;
    const char* src = format;

    while (char c = *src)
    {
        if (c == '%')
        {
            src++;
            int index = 0;
            while (isdigit(static_cast<unsigned char>(*src)))
            {
                index = index * 10 + (*src - '0');
                src++;
            }
            if (index < 1 || index > nParams)
            {
                continue;
            }
            const GameValue& value = array[index];
            RString text = (value.GetType() == GameString) ? value.GetData()->GetString() : value.GetText();
            result.append(static_cast<const char*>(text), text.GetLength());
        }
        else
        {
            result.push_back(c);
            src++;
        }
    }
    return RString(result.c_str());
}

GameValue StrLocalize(const GameState* state, GameValuePar oper1)
{
    GameStringType name = oper1;
    return LocalizeString(name);
}

#if _ENABLE_CHEATS
extern AutoArray<RString> DebugConsoleLog;
#endif

GameValue TextLog(const GameState* state, GameValuePar oper1)
{
#if _ENABLE_CHEATS
    DebugConsoleLog.Add(oper1.GetText());
#endif
    if (AppConfig::Instance().IsSimulateMode())
        LOG_INFO(Mission, "[diagLog] {}", (const char*)oper1.GetText());
    return NOTHING;
}

GameValue TextDebugLog(const GameState* state, GameValuePar oper1)
{
#if _ENABLE_CHEATS
    DebugConsoleLog.Add(oper1.GetDebugText());
#endif
    if (AppConfig::Instance().IsSimulateMode())
        LOG_INFO(Mission, "[debugLog] {}", (const char*)oper1.GetDebugText());
    return NOTHING;
}

GameValue LogInfo(const GameState* state, GameValuePar oper1)
{
    LOG_INFO(Script, "{}", (const char*)oper1.GetText());
    return NOTHING;
}

GameValue EnableEndDialog(const GameState* state)
{
    GWorld->EnableEndDialog(true);
    return NOTHING;
}

GameValue ForceEnd(const GameState* state)
{
    GWorld->ForceEnd(true);
    return NOTHING;
}

GameValue EndGame(const GameState* state)
{
    if (AppConfig::Instance().IsSimulateMode())
        LOG_INFO(Mission, "[endGame]");

    const bool automationMissionExit = AppConfig::Instance().IsSimulateMode() ||
                                       !AppConfig::Instance().GetTestMissionPath().empty() ||
                                       !AppConfig::Instance().GetMPAssign().empty();
    if (automationMissionExit)
        GStats.OnMPMissionEnd();

    // In mp-assign mode, endGame means the mission completed successfully
    if (!AppConfig::Instance().GetMPAssign().empty())
        GApp->m_exitCode = ResolveMultiplayerAutomationExitCode(GApp->m_exitCode, GApp->m_closeRequest, true);
    GApp->m_closeRequest = true;
    return NOTHING;
}

#include <Poseidon/Core/SaveVersion.hpp>

namespace Poseidon
{
RString FindScript(RString name);
RString GetCampaignSaveDirectory(RString campaign);
RString GetUserDirectory();
} // namespace Poseidon

GameValue ObjSaveStatus(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return false;
    }

    Person* person = dyn_cast<Person>(obj);
    if (person)
    {
        RString identity = person->GetInfo()._identityContext;
        if (identity.GetLength() > 0)
        {
            Poseidon::AddDeadIdentity(identity);
        }
    }

    RString name = oper2;
    if (name.GetLength() == 0)
    {
        return false;
    }
    RString filename = Poseidon::GetCampaignSaveDirectory(CurrentCampaign) + RString("objects.sav");

    ParamArchiveSave ar(UnitStatusVersion);
    if (!ar.ParseBin(filename) && ar.Parse(filename) != LSOK)
    {
        return false;
    }

    ParamArchive arObjects;
    if (!ar.OpenSubclass("Objects", arObjects))
    {
        return false;
    }

    ParamArchive arCls;
    if (!arObjects.OpenSubclass(name, arCls))
    {
        return false;
    }

    if (obj->Serialize(arCls) != LSOK)
    {
        return false;
    }

    arObjects.CloseSubclass(arCls);
    ar.CloseSubclass(arObjects);
#if _ENABLE_CHEATS
    return ar.Save(filename) == LSOK;
#else
    return ar.SaveBin(filename);
#endif
}

GameValue DeleteStatus(const GameState* state, GameValuePar oper1)
{
    RString name = oper1;
    if (name.GetLength() == 0)
    {
        return false;
    }
    RString filename = Poseidon::GetCampaignSaveDirectory(CurrentCampaign) + RString("objects.sav");

    ParamFile file;
    if (!file.ParseBin(filename) && file.Parse(filename) != LSOK)
    {
        return false;
    }

    ParamEntry* cls = file.FindEntry("Objects");
    if (cls)
    {
        cls->Delete(name);
    }
#if _ENABLE_CHEATS
    return file.Save(filename) == LSOK;
#else
    return file.SaveBin(filename);
#endif
}

GameValue ObjLoadStatus(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return false;
    }

    RString name = oper2;
    if (name.GetLength() == 0)
    {
        return false;
    }
    RString filename = Poseidon::GetCampaignSaveDirectory(CurrentCampaign) + RString("objects.sav");

    ParamArchiveLoad ar;
    if (!ar.LoadBin(filename) && ar.Load(filename) != LSOK)
    {
        return false;
    }

    ParamArchive arObjects;
    if (!ar.OpenSubclass("Objects", arObjects))
    {
        return false;
    }

    ParamArchive arCls;
    if (!arObjects.OpenSubclass(name, arCls))
    {
        return false;
    }

    arCls.FirstPass();
    if (obj->Serialize(arCls) != LSOK)
    {
        return false;
    }
    arCls.SecondPass();
    if (obj->Serialize(arCls) != LSOK)
    {
        return false;
    }

    arObjects.CloseSubclass(arCls);
    ar.CloseSubclass(arObjects);
    return true;
}

GameValue ObjSaveIdentity(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    Person* person = dyn_cast<Person>(obj);
    if (!person)
    {
        return false;
    }

    RString identity = person->GetInfo()._identityContext;
    if (identity.GetLength() > 0)
    {
        Poseidon::AddDeadIdentity(identity);
    }

    RString name = oper2;
    if (name.GetLength() == 0)
    {
        return false;
    }
    RString filename = Poseidon::GetCampaignSaveDirectory(CurrentCampaign) + RString("objects.sav");

    ParamArchiveSave ar(UnitStatusVersion);
    if (!ar.ParseBin(filename) && ar.Parse(filename) != LSOK)
    {
        return false;
    }

    ParamArchive arIdentities;
    if (!ar.OpenSubclass("Identities", arIdentities))
    {
        return false;
    }

    ParamArchive arCls;
    if (!arIdentities.OpenSubclass(name, arCls))
    {
        return false;
    }

    if (person->SerializeIdentity(arCls) != LSOK)
    {
        return false;
    }

    arIdentities.CloseSubclass(arCls);
    ar.CloseSubclass(arIdentities);

#if _ENABLE_CHEATS
    return ar.Save(filename) == LSOK;
#else
    return ar.SaveBin(filename);
#endif
}

GameValue DeleteIdentity(const GameState* state, GameValuePar oper1)
{
    RString name = oper1;
    if (name.GetLength() == 0)
    {
        return false;
    }
    RString filename = Poseidon::GetCampaignSaveDirectory(CurrentCampaign) + RString("objects.sav");

    ParamFile file;
    if (!file.ParseBin(filename) && file.Parse(filename) != LSOK)
    {
        return false;
    }

    ParamEntry* cls = file.FindEntry("Identities");
    if (cls)
    {
        cls->Delete(name);
    }
#if _ENABLE_CHEATS
    return file.Save(filename) == LSOK;
#else
    return file.SaveBin(filename);
#endif
}

GameValue ObjLoadIdentity(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    Person* person = dyn_cast<Person>(obj);
    if (!person)
    {
        return false;
    }

    RString name = oper2;
    if (name.GetLength() == 0)
    {
        return false;
    }
    RString filename = Poseidon::GetCampaignSaveDirectory(CurrentCampaign) + RString("objects.sav");

    ParamArchiveLoad ar;
    if (!ar.LoadBin(filename) && ar.Load(filename) != LSOK)
    {
        return false;
    }

    ParamArchive arIdentities;
    if (!ar.OpenSubclass("Identities", arIdentities))
    {
        return false;
    }

    ParamArchive arCls;
    if (!arIdentities.OpenSubclass(name, arCls))
    {
        return false;
    }

    arCls.FirstPass();
    if (person->SerializeIdentity(arCls) != LSOK)
    {
        return false;
    }
    arCls.SecondPass();
    if (person->SerializeIdentity(arCls) != LSOK)
    {
        return false;
    }

    arIdentities.CloseSubclass(arCls);
    ar.CloseSubclass(arIdentities);
    return true;
}

RString GBriefingOnPlan;
RString GBriefingOnNotes;
RString GBriefingOnGear;
RString GBriefingOnGroup;
// fired on single map click only, not on double-click
RString GMapOnSingleClick;

GameValue BriefingOnPlan(const GameState* state, GameValuePar oper1)
{
    GBriefingOnPlan = oper1;
    return NOTHING;
}

GameValue BriefingOnNotes(const GameState* state, GameValuePar oper1)
{
    GBriefingOnNotes = oper1;
    return NOTHING;
}

GameValue BriefingOnGear(const GameState* state, GameValuePar oper1)
{
    GBriefingOnGear = oper1;
    return NOTHING;
}

GameValue BriefingOnGroup(const GameState* state, GameValuePar oper1)
{
    GBriefingOnGroup = oper1;
    return NOTHING;
}

GameValue MapOnSingleClick(const GameState* state, GameValuePar oper1)
{
    GMapOnSingleClick = oper1;
    return NOTHING;
}

GameValue ObjAnimate(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    Entity* veh = dyn_cast<Entity>(obj);
    if (!veh)
    {
        return NOTHING;
    }

    const GameArrayType& array = oper2;
    if (array.Size() != 2)
    {
        state->SetError(EvalDim, array.Size(), 2);
        return NOTHING;
    }
    if (array[0].GetType() != GameString)
    {
        state->TypeError(GameString, array[0].GetType());
        return NOTHING;
    }
    if (array[1].GetType() != GameScalar)
    {
        state->TypeError(GameScalar, array[1].GetType());
        return NOTHING;
    }

    RString animation = array[0];
    float phase = array[1];
    if (veh->IsLocal())
    {
        veh->SetAnimationPhase(animation, phase);
        NetworkId id = veh->GetNetworkId();
        if (id.creator == STATIC_OBJECT)
        {
            // no automatic update is performed - update manually
            GetNetworkManager().AskForAnimationPhase(veh, animation, phase);
        }
    }
    else
    {
        GetNetworkManager().AskForAnimationPhase(veh, animation, phase);
    }
    return NOTHING;
}

GameValue ObjAnimationPhase(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    Entity* veh = dyn_cast<Entity>(obj);
    if (!veh)
    {
        return 0.0f;
    }
    RString animation = oper2;
    return veh->GetAnimationPhase(animation);
}

GameValue ObjChangeModel(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }

    RString name = oper2;
    if (name.GetLength() == 0)
    {
        return NOTHING;
    }

    if (!QIFStreamB::FileExist(name))
    {
        return NOTHING;
    }

    LODShape* shape = obj->GetShape();
    if (!shape)
    {
        return NOTHING;
    }
    bool reversed = (shape->Remarks() & REM_REVERSED) != 0;
    *shape = LODShape(name, reversed);

    shape->InternalTransform(M4Identity);

    obj->GetShape()->ShadowChanged();
    return NOTHING;
}

bool EnabledWeaponPool();
namespace Poseidon
{
void CampaignLoadWeaponPool(WeaponsInfo& pool);
void CampaignSaveWeaponPool(WeaponsInfo& pool);
} // namespace Poseidon

GameValue ObjWeaponsFromPool(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    Person* person = dyn_cast<Person>(obj);
    if (!person)
    {
        return NOTHING;
    }

    if (!EnabledWeaponPool())
    {
        return NOTHING;
    }

    WeaponsInfo pool;
    Poseidon::CampaignLoadWeaponPool(pool);

    for (int i = 0; i < person->NMagazines(); i++)
    {
        pool._magazinesPool.Add(person->GetMagazine(i));
    }
    person->RemoveAllMagazines();

    const WeaponType* primary = nullptr;
    const WeaponType* secondary = nullptr;
    int index = person->FindWeaponType(MaskSlotPrimary);
    if (index >= 0)
    {
        primary = person->GetWeaponSystem(index);
    }
    index = person->FindWeaponType(MaskSlotSecondary);
    if (index >= 0)
    {
        secondary = person->GetWeaponSystem(index);
    }

    int free = GetItemSlotsCount(person->GetType()->_weaponSlots);
    if (primary && primary->_muzzles.Size() > 0 && primary->_muzzles[0]->_magazines.Size() > 0)
    {
        const MagazineType* def = primary->_muzzles[0]->_magazines[0];
        int size = GetItemSlotsCount(def->_magazineType);
        int count = (free < 4 ? free : 4) / size;
        for (int i = 0; i < count; i++)
        {
            Ref<Magazine> magazine = pool.RemovePoolMagazine(def);
            if (!magazine)
            {
                break;
            }
            person->AddMagazine(magazine);
            free -= size;
        }
    }
    if (secondary && secondary->_muzzles.Size() > 0 && secondary->_muzzles[0]->_magazines.Size() > 0)
    {
        const MagazineType* def = secondary->_muzzles[0]->_magazines[0];
        int size = GetItemSlotsCount(def->_magazineType);
        int count = free / size;
        for (int i = 0; i < count; i++)
        {
            Ref<Magazine> magazine = pool.RemovePoolMagazine(def);
            if (!magazine)
            {
                break;
            }
            person->AddMagazine(magazine);
            free -= size;
        }
    }
    if (free > 0)
    {
        const MagazineType* def = MagazineTypes.New("HandGrenade");
        int size = GetItemSlotsCount(def->_magazineType);
        int count = free / size;
        for (int i = 0; i < count; i++)
        {
            Ref<Magazine> magazine = pool.RemovePoolMagazine(def);
            if (!magazine)
            {
                break;
            }
            person->AddMagazine(magazine);
            free -= size;
        }
    }

    const WeaponType* handGun = nullptr;
    index = person->FindWeaponType(MaskSlotHandGun);
    if (index >= 0)
    {
        handGun = person->GetWeaponSystem(index);
    }
    if (handGun && handGun->_muzzles.Size() > 0 && handGun->_muzzles[0]->_magazines.Size() > 0)
    {
        int free = GetHandGunItemSlotsCount(person->GetType()->_weaponSlots);

        const MagazineType* def = handGun->_muzzles[0]->_magazines[0];
        int size = GetHandGunItemSlotsCount(def->_magazineType);
        int count = free / size;
        for (int i = 0; i < count; i++)
        {
            Ref<Magazine> magazine = pool.RemovePoolMagazine(def);
            if (!magazine)
            {
                break;
            }
            person->AddMagazine(magazine);
            free -= size;
        }
    }
    person->AutoReloadAll();

    Poseidon::CampaignSaveWeaponPool(pool);
    return NOTHING;
}

GameValue IsCheatsEnabled(const GameState* state)
{
#if _ENABLE_CHEATS
    return true;
#else
    return false;
#endif
}

GameValue VehIsEngineOn(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    EntityAI* veh = dyn_cast<EntityAI>(obj);
    if (!veh)
    {
        return false;
    }
    return veh->EngineIsOn();
}

GameValue VehEngineOn(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    EntityAI* veh = dyn_cast<EntityAI>(obj);
    if (!veh)
    {
        return NOTHING;
    }
    bool on = oper2;
    if (on)
    {
        veh->EngineOn();
    }
    else
    {
        veh->EngineOff();
    }
    return NOTHING;
}

GameValue GetNetIdObj(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }

    return NetObjToNetId(obj);
}

GameValue GetObjFromNetId(const GameState* state, GameValuePar oper)
{
    RString netId = oper;
    NetworkObject* obj = NetIdToNetObj(netId);
    Object* ret = dynamic_cast<Object*>(obj);
    return GameValueExt(ret);
}

GameValue GetNetworkId(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }

    if (GWorld->GetMode() != GModeNetware)
    {
        return GameArrayType();
    }

    NetworkId id = obj->GetNetworkId();

    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;
    array.Resize(2);
    array[0] = Format("%d", id.creator);
    array[1] = float(id.id);

    return array;
}

GameValue GetUnitById(const GameState* state, GameValuePar oper1)
{
    const GameArrayType& array = oper1;
    if (!CheckSize(state, array, 2))
    {
        return OBJECT_NULL;
    }
    if (!CheckType(state, array[0], GameString))
    {
        return OBJECT_NULL;
    }
    if (!CheckType(state, array[1], GameScalar))
    {
        return OBJECT_NULL;
    }

    GameStringType name = array[0];

    RString netId = Format("%s:%d", name.Data(), toInt((float)array[1]));
    ;
    NetworkObject* obj = NetIdToNetObj(netId);
    Object* ret = dynamic_cast<Object*>(obj);
    return GameValueExt(ret);
}

static RString ConfigFullName(RString filename)
{
    if (filename.GetLength() == 0)
    {
        return RString();
    }

    // avoid path in filename
    if (strchr(filename, '\\'))
    {
        return RString();
    }
    if (strchr(filename, '/'))
    {
        return RString();
    }
    return Poseidon::GetUserDirectory() + RString("Config/") + filename;
}

static GameFileType GetFile(GameValuePar oper)
{
    PoseidonAssert(oper.GetType() == GameFile);
    return static_cast<GameDataFile*>(oper.GetData())->GetFile();
}
