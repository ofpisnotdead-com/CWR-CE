#include <Poseidon/Core/Application.hpp>

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

#include <ctype.h>
#include <time.h>
#include <limits.h>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/BoolArray.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Foundation/Enums/EnumNames.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

using Poseidon::Foundation::EnumName;
using Poseidon::Foundation::GetEnumNames;
using Poseidon::Foundation::GetEnumValue;

using namespace Poseidon;
namespace Poseidon
{
} // namespace Poseidon

#ifdef _WIN32
#include <io.h>
#endif

#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/IO/PreprocC/Preproc.h>

#include <Poseidon/Foundation/Platform/VersionNo.h>

#include <Poseidon/Game/Commands/GameStateExtCommon.hpp>

GameValue ObjBehaviour(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return "ERROR";
    }
    EntityAI* veh = dyn_cast<EntityAI>(obj);
    if (!veh)
    {
        return "ERROR";
    }
    AIUnit* unit = veh->CommanderUnit();
    if (!unit)
    {
        return "ERROR";
    }
    CombatMode mode = unit->GetCombatMode();
    const EnumName* names = GetEnumNames(mode);
    for (int i = 0; names[i].IsValid(); i++)
    {
        if (names[i].value == mode)
        {
            return names[i].name;
        }
    }
    return "ERROR";
}

GameValue GrpCombatMode(const GameState* state, GameValuePar oper1)
{
    AIGroup* grp1 = GetGroup(oper1);
    if (!grp1)
    {
        return "ERROR";
    }
    AI::Semaphore sem = grp1->GetSemaphore();
    const EnumName* names = GetEnumNames(sem);
    for (int i = 0; names[i].IsValid(); i++)
    {
        if (names[i].value == sem)
        {
            return names[i].name;
        }
    }
    return "ERROR";
}

GameValue GrpFormation(const GameState* state, GameValuePar oper1)
{
    AIGroup* grp = GetGroup(oper1);
    if (!grp)
    {
        return "ERROR";
    }
    AISubgroup* subgrp = grp->MainSubgroup();
    if (!subgrp)
    {
        return "ERROR";
    }
    AI::Formation form = subgrp->GetFormation();
    const EnumName* names = GetEnumNames(form);
    for (int i = 0; names[i].IsValid(); i++)
    {
        if (names[i].value == form)
        {
            return names[i].name;
        }
    }
    return "ERROR";
}

GameValue GrpSpeedMode(const GameState* state, GameValuePar oper1)
{
    AIGroup* grp = GetGroup(oper1);
    if (!grp)
    {
        return "ERROR";
    }
    AISubgroup* subgrp = grp->MainSubgroup();
    if (!subgrp)
    {
        return "ERROR";
    }
    SpeedMode mode = subgrp->GetSpeedMode();
    const EnumName* names = GetEnumNames(mode);
    for (int i = 0; names[i].IsValid(); i++)
    {
        if (names[i].value == mode)
        {
            return names[i].name;
        }
    }
    return "ERROR";
}

GameValue GrpSetBehaviour(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    AIGroup* grp1 = GetGroup(oper1);
    if (!grp1)
    {
        return NOTHING;
    }
    GameStringType str = oper2;
    CombatMode mode = CMUnchanged; // avoid warning
    const EnumName* names = GetEnumNames(mode);
    for (int i = 0; names[i].IsValid(); i++)
    {
        if (stricmp(names[i].name, str) == 0)
        {
            mode = (CombatMode)names[i].value;
            grp1->SetCombatModeMajor(mode);
            return NOTHING;
        }
    }
    return NOTHING;
}

GameValue ObjSetUnitPos(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }

    Person* veh = dyn_cast<Person>(obj);
    if (!veh)
    {
        return NOTHING;
    }

    AIUnit* unit = veh->Brain();
    if (!unit)
    {
        return NOTHING;
    }

    GameStringType str = oper2;
    UnitPosition pos = UPAuto; // avoid warning
    const EnumName* names = GetEnumNames(pos);
    for (int i = 0; names[i].IsValid(); i++)
    {
        if (stricmp(names[i].name, str) == 0)
        {
            pos = (UnitPosition)names[i].value;
            unit->SetUnitPosition(pos);
            return NOTHING;
        }
    }
    return NOTHING;
}

GameValue GrpSetCombatMode(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    AIGroup* grp1 = GetGroup(oper1);
    if (!grp1)
    {
        return NOTHING;
    }
    GameStringType str = oper2;
    AI::Semaphore sem = AI::SemaphoreBlue; // avoid warning
    const EnumName* names = GetEnumNames(sem);
    for (int i = 0; names[i].IsValid(); i++)
    {
        if (stricmp(names[i].name, str) == 0)
        {
            sem = (AI::Semaphore)names[i].value;
            grp1->SetSemaphore(sem);

            PackedBoolArray all;
            for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
            {
                AIUnit* unit = grp1->UnitWithID(i + 1);
                if (!unit)
                {
                    continue;
                }
                all.Set(i, true);
            }
            grp1->SendSemaphore(sem, all);

            return NOTHING;
        }
    }
    return NOTHING;
}

GameValue GrpSetFormation(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    AIGroup* grp = GetGroup(oper1);
    if (!grp)
    {
        return NOTHING;
    }
    AISubgroup* subgrp = grp->MainSubgroup();
    if (!subgrp)
    {
        return NOTHING;
    }
    GameStringType str = oper2;
    AI::Formation form = AI::FormColumn; // avoid warning
    const EnumName* names = GetEnumNames(form);
    for (int i = 0; names[i].IsValid(); i++)
    {
        if (stricmp(names[i].name, str) == 0)
        {
            form = (AI::Formation)names[i].value;
            grp->SendFormation(form, subgrp);
            return NOTHING;
        }
    }
    return NOTHING;
}

GameValue GrpSetSpeedMode(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    AIGroup* grp = GetGroup(oper1);
    if (!grp)
    {
        return NOTHING;
    }
    AISubgroup* subgrp = grp->MainSubgroup();
    if (!subgrp)
    {
        return NOTHING;
    }
    GameStringType str = oper2;
    SpeedMode mode = SpeedNormal; // avoid warning
    const EnumName* names = GetEnumNames(mode);
    for (int i = 0; names[i].IsValid(); i++)
    {
        if (stricmp(names[i].name, str) == 0)
        {
            mode = (SpeedMode)names[i].value;
            subgrp->SetSpeedMode(mode);
            return NOTHING;
        }
    }
    return NOTHING;
}

GameValue GrpLockWP(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    AIGroup* grp1 = GetGroup(oper1);
    if (!grp1)
    {
        return NOTHING;
    }
    grp1->LockWP(oper2);
    return NOTHING;
}

GameValue GrpSetFormDir(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Ref<AIGroup> grp = GetGroup(oper1);
    if (!grp)
    {
        return NOTHING;
    }
    AISubgroup* sub = grp->MainSubgroup();
    float angle = oper2;
    Matrix3 rotY(MRotationY, -HDegree(angle));
    if (sub)
    {
        sub->SetDirection(rotY.Direction());
    }
    return NOTHING;
}

GameValue Player(const GameState* state)
{
    Person* soldier = GWorld->PlayerOn();
    OLink<Object> player = (Object*)soldier;
    return GameValueExt(player.GetLink());
}

GameValue PlayerClearInjured(const GameState* state)
{
    Person* soldier = GWorld->PlayerOn();
    if (EntityAI* ai = dyn_cast<EntityAI>(soldier))
    {
        if (AIUnit* unit = ai->CommanderUnit())
        {
            if (AIGroup* grp = unit->GetGroup())
            {
                grp->SetHealthStateReported(unit, AIUnit::RSNormal);
            }
        }
    }
    return GameValue();
}

GameValue UsedVersion(const GameState* state)
{
    return float(APP_VERSION_MAJOR) * 1000.0f + float(APP_VERSION_MINOR) * 10.0f;
}

GameValue ObjGetMass(const GameState* state, GameValuePar oper1)
{
    Object* obj1 = GetObject(oper1);
    if (obj1)
    {
        return obj1->GetMass();
    }
    else
    {
        return 0.0f;
    }
}

GameValue CameraOn(const GameState* state)
{
    Object* object = GWorld->CameraOn();
    return GameValueExt(object);
}

GameValue GameTime(const GameState* state)
{
    return Glob.time - Poseidon::Foundation::Time(0);
}

GameValue DayTime(const GameState* state)
{
    return Glob.clock.GetTimeOfDay() * 24.0f;
}

GameValue Benchmark(const GameState* state)
{
    return ENGINE_CONFIG.benchmark;
}

GameValue CadetMode(const GameState* state)
{
    return USER_CONFIG.easyMode;
}

GameValue SaveVar(const GameState* state, GameValuePar oper1)
{
    RString name = oper1;
    name.Lower();
    GameVariable& var = const_cast<GameVariable&>(state->GetVariables()[name]);
    if (state->IsNull(var))
    {
        return NOTHING;
    }

    GStats._campaign.AddVariable(var);

    return NOTHING;
}

GameValue GetAcceleratedTime(const GameState* state)
{
    return GWorld->GetAcceleratedTime();
}

GameValue SetAcceleratedTime(const GameState* state, GameValuePar oper1)
{
    GWorld->SetAcceleratedTime(oper1);
    return NOTHING;
}

int ListCountSideH(const GameArrayType& array, TargetSide side)
{
    int count = 0;
    for (int i = 0; i < array.Size(); i++)
    {
        const GameValue& val = array[i];
        Object* obj = GetObject(val);
        if (!obj)
        {
            continue;
        }
        EntityAI* ai = dyn_cast<EntityAI>(obj);
        if (!ai)
        {
            continue;
        }
        if (ai->GetTargetSide() == side)
        {
            count++;
        }
    }
    return count;
}

int ListCountSideH(const GameArrayType& array, Object* obj, IsSide func)
{
    EntityAI* ai = dyn_cast<EntityAI>(obj);
    if (!ai)
    {
        return 0;
    }
    AIGroup* grp = ai->GetGroup();
    if (!grp)
    {
        return 0;
    }

    int count = 0;
    AICenter* center = grp->GetCenter();
    for (int i = 0; i < array.Size(); i++)
    {
        Object* obj = GetObject(array[i]);
        if (!obj)
        {
            continue;
        }
        EntityAI* ai = dyn_cast<EntityAI>(obj);
        if (!ai)
        {
            continue;
        }

        const AITargetInfo* ctgt = center->FindTargetInfo(ai);
        TargetSide side = TSideUnknown;
        if (ctgt)
        {
            side = ctgt->_side;
        }
        if ((center->*func)(side))
        {
            count++;
        }
    }
    return count;
}

int ListCountTypeH(const GameArrayType& array, const EntityType* type)
{
    int count = 0;
    for (int i = 0; i < array.Size(); i++)
    {
        Object* obj = GetObject(array[i]);
        if (!obj)
        {
            continue;
        }
        EntityAI* ai = dyn_cast<EntityAI>(obj);
        if (!ai)
        {
            continue;
        }

        if (ai->GetType()->IsKindOf(type))
        {
            count++;
        }
    }
    return count;
}

GameValue ListCountSide(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    return (float)ListCountSideH(oper2, GetSide(oper1));
}

GameValue ListCountEnemy(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    return (float)ListCountSideH(oper2, GetObject(oper1), &AICenter::IsEnemy);
}

GameValue ListCountFriendly(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    return (float)ListCountSideH(oper2, GetObject(oper1), &AICenter::IsFriendly);
}
GameValue ListCountUnknown(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    return (float)ListCountSideH(oper2, GetObject(oper1), &AICenter::IsUnknown);
}

GameValue ObjGetType(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    Entity* ent = dyn_cast<Entity>(obj);
    RString type = "";
    if (ent)
    {
        const EntityType* etype = ent->GetNonAIType();
        if (etype)
        {
            type = etype->GetName();
        }
    }
    return type;
}

GameValue ListCountType(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    RString name = oper1;
    const EntityType* type = VehicleTypes.New(name);
    return (float)ListCountTypeH((const GameArrayType&)oper2, type);
}

GameValue ListAllowGetIn(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    const GameArrayType& array = oper1;
    bool allow = oper2;
    for (int i = 0; i < array.Size(); i++)
    {
        Object* obj = GetObject(array[i]);
        if (!obj)
        {
            continue;
        }
        EntityAI* ai = dyn_cast<EntityAI>(obj);
        if (!ai)
        {
            continue;
        }
        AIUnit* unit = ai->CommanderUnit();
        if (!unit)
        {
            continue;
        }
        unit->AllowGetIn(allow);
    }

    return NOTHING;
}

GameValue ListOrderGetIn(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    const GameArrayType& array = oper1;
    bool allow = oper2;
    for (int i = 0; i < array.Size(); i++)
    {
        Object* obj = GetObject(array[i]);
        if (!obj)
        {
            continue;
        }
        EntityAI* ai = dyn_cast<EntityAI>(obj);
        if (!ai)
        {
            continue;
        }
        AIUnit* unit = ai->CommanderUnit();
        if (!unit)
        {
            continue;
        }
        unit->OrderGetIn(allow);
    }

    return NOTHING;
}

void ProcessJoinGroups(OLinkArray<AIUnit>& units, AIGroup* group)
{
    Ref<AIGroup> grp = group;

    Person* player = GWorld->GetRealPlayer();
    AIUnit* playerUnit = player ? player->Brain() : nullptr;

    AUTO_STATIC_ARRAY(AIUnit*, joined, 32)
    if (grp)
    {
        for (int i = 0; i < units.Size(); i++)
        {
            if (grp->NUnits() >= MAX_UNITS_PER_GROUP)
            {
                break;
            }

            Ref<AIUnit> unit = (AIUnit*)units[i];
            if (!unit)
            {
                continue;
            }
            AIGroup* g = unit->GetGroup();
            if (!g)
            {
                continue;
            }
            // send only when not player or near units
            bool sendJoin = grp->IsAnyPlayerGroup() || !grp->Leader() ||
                            grp->Leader()->Position().Distance2(unit->Position()) < Square(200);

            int id = -1;
            if (GWorld->GetMode() != GModeNetware && unit == playerUnit)
            {
                id = unit->ID();
            }
            unit->ForceRemoveFromGroup();
            grp->AddUnit(unit, id);
            joined.Add(unit);

            if (!sendJoin)
            {
                AISubgroup* subgrp = new AISubgroup();
                grp->AddSubgroup(subgrp);
                subgrp->AddUnit(unit);
                subgrp->SelectLeader(unit);
                if (GWorld->GetMode() == GModeNetware)
                {
                    GetNetworkManager().CreateObject(subgrp);
                    GetNetworkManager().UpdateObject(subgrp);
                }
            }
            if (g->NUnits() == 0)
            {
                g->RemoveFromCenter();
            }
        }
    }
    else
    {
        AICenter* center = nullptr;
        for (int i = 0; i < units.Size(); i++)
        {
            AIUnit* unit = units[i];
            if (!unit)
            {
                continue;
            }
            AIGroup* g = unit->GetGroup();
            if (!g)
            {
                continue;
            }
            AICenter* c = g->GetCenter();
            if (!c)
            {
                continue;
            }
            center = c;
            break;
        }
        // no units to add
        if (!center)
        {
            return;
        }
        // cannot add group
        if (center->NGroups() >= MaxGroups)
        {
            return;
        }

        grp = new AIGroup();
        center->AddGroup(grp);

        Mission mis;
        mis._action = Mission::Arcade;
        center->SendMission(grp, mis);

        for (int i = 0; i < units.Size(); i++)
        {
            if (grp->NUnits() >= MAX_UNITS_PER_GROUP)
            {
                break;
            }

            Ref<AIUnit> unit = (AIUnit*)units[i];
            if (!unit)
            {
                continue;
            }
            AIGroup* g = unit->GetGroup();
            if (!g)
            {
                continue;
            }
            int id = -1;
            if (GWorld->GetMode() != GModeNetware && unit == playerUnit)
            {
                id = unit->ID();
            }
            unit->ForceRemoveFromGroup();
            grp->AddUnit(unit, id);
            joined.Add(unit);
            if (g->NUnits() == 0)
            {
                g->RemoveFromCenter();
            }
        }
        if (GWorld->GetMode() == GModeNetware)
        {
            GetNetworkManager().CreateObject(grp);
            for (int i = 0; i < grp->NSubgroups(); i++)
            {
                if (grp->GetSubgroup(i))
                {
                    GetNetworkManager().CreateObject(grp->GetSubgroup(i));
                }
            }
        }
    }

    // update values for flee
    grp->CalculateMaximalStrength();

    if (grp->NUnits() == 0)
    {
        return;
    }

    AICenter* center = grp->GetCenter();
    if (!grp->Leader())
    {
        center->SelectLeader(grp);
    }
    PoseidonAssert(grp->Leader());

    if (grp->NWaypoints() == 0)
    {
        grp->AddFirstWaypoint(grp->Leader()->Position());
    }

    // special case - formation leader inside vehicle
    if (!grp->MainSubgroup()->Leader() || !grp->MainSubgroup()->Leader()->IsUnit())
    {
        grp->MainSubgroup()->SelectLeader();
    }

    GWorld->SetActiveChannels();
    PackedBoolArray list;
    Command cmd;
    cmd._message = Command::Join;
    cmd._context = Command::CtxMission;
    for (int i = 0; i < joined.Size(); i++)
    {
        list.Set(joined[i]->ID() - 1, true);
    }
    grp->GetRadio().Transmit(new RadioMessageJoin(grp, list), grp->GetCenter()->GetLanguage());
    for (int i = 0; i < joined.Size(); i++)
    {
        if (joined[i] != playerUnit)
        {
            grp->GetRadio().Transmit(new RadioMessageJoinDone(joined[i], grp), grp->GetCenter()->GetLanguage());
        }
    }

    // allow get in by leader
    bool allow = grp->Leader()->IsGetInAllowed();
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* unit = grp->UnitWithID(i + 1);
        if (unit)
        {
            unit->AllowGetIn(allow);
        }
    }

    if (GWorld->GetMode() == GModeNetware)
    {
        GetNetworkManager().UpdateObject(grp->MainSubgroup());
        GetNetworkManager().UpdateObject(grp);
    }
}

GameValue ListJoin(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    const GameArrayType& array = oper1;
    AIGroup* grp = GetGroup(oper2);

    OLinkArray<AIUnit> units;
    for (int i = 0; i < array.Size(); i++)
    {
        Object* obj = GetObject(array[i]);
        if (!obj)
        {
            continue;
        }
        EntityAI* ai = dyn_cast<EntityAI>(obj);
        if (!ai)
        {
            continue;
        }
        AIUnit* unit = ai->CommanderUnit();
        if (!unit)
        {
            continue;
        }
        units.Add(unit);
    }

    if (!grp || grp->IsLocal())
    {
        ProcessJoinGroups(units, grp);
    }
    else
    {
        GetNetworkManager().AskForJoin(grp, units);
    }

    return NOTHING;
}

bool GetVector(Vector3& ret, GameValuePar oper)
{
    const GameArrayType& array = oper;
    if (array.Size() != 3)
    {
        return false;
    }
    if (array[0].GetType() != GameScalar || array[1].GetType() != GameScalar || array[2].GetType() != GameScalar)
    {
        return false;
    }

    float x = array[0];
    float z = array[1];
    float y = array[2];
    ret = Vector3(x, y, z);
    return true;
}

bool GetRelPos(const GameState* state, Vector3& ret, GameValuePar oper)
{
    Object* obj = GetObject(oper);
    if (obj)
    {
        ret = obj->Position();
        // convert to relative position?
        ret[1] -= GLandscape->SurfaceYAboveWater(ret.X(), ret.Z());
        return true;
    }
    const GameArrayType& array = oper;
    if (array.Size() < 2 || array.Size() > 3)
    {
        if (state)
        {
            state->SetError(EvalDim, array.Size(), 3);
        }
        return false;
    }
    if (array[0].GetType() != GameScalar)
    {
        if (state)
        {
            state->TypeError(GameScalar, array[0].GetType());
        }
        return false;
    }
    if (array[1].GetType() != GameScalar)
    {
        if (state)
        {
            state->TypeError(GameScalar, array[1].GetType());
        }
        return false;
    }

    float x = array[0];
    float z = array[1];
    float y = 0;
    if (array.Size() == 3)
    {
        if (array[2].GetType() != GameScalar)
        {
            if (state)
            {
                state->TypeError(GameScalar, array[2].GetType());
            }
            return false;
        }
        y = array[2];
    }
    ret = Vector3(x, y, z);
    return true;
}

bool GetPos(const GameState* state, Vector3& ret, GameValuePar oper)
{
    Vector3 rel;
    bool ok = GetRelPos(state, rel, oper);
    if (!ok)
    {
        return ok;
    }
    rel[1] += GLandscape->SurfaceYAboveWater(rel.X(), rel.Z());
    ret = rel;
    return true;
}

bool GetPos(Vector3& ret, GameValuePar oper)
{
    return GetPos(nullptr, ret, oper);
}

GameValue GrpMove(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    AIGroup* grp = GetGroup(oper1);
    if (!grp)
    {
        return NOTHING;
    }
    Vector3 pos;
    if (!GetPos(state, pos, oper2))
    {
        return NOTHING;
    }

    grp->Move(grp->MainSubgroup(), // who
              pos,                 // where
              Command::Undefined   // how
    );

    return NOTHING;
}

GameValue GrpSetIdentity(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    AIGroup* grp = GetGroup(oper1);
    if (!grp)
    {
        return NOTHING;
    }

    RString name;
    RString color;
    RString picture;

    const GameArrayType& array = oper2;
    switch (array.Size())
    {
        case 3:
            if (array[2].GetType() != GameString)
            {
                state->TypeError(GameString, array[2].GetType());
                return NOTHING;
            }
            picture = array[2];
        case 2:
            if (array[0].GetType() != GameString)
            {
                state->TypeError(GameString, array[0].GetType());
                return NOTHING;
            }
            if (array[1].GetType() != GameString)
            {
                state->TypeError(GameString, array[1].GetType());
                return NOTHING;
            }
            name = array[0];
            color = array[1];
            break;
        default:
            state->SetError(EvalDim, array.Size(), 3);
            return NOTHING;
    }

    grp->SetIdentity(name, color, picture);
    return NOTHING;
}

GameValue GrpKnowsAbout(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    AIGroup* grp = GetGroup(oper1);
    if (!grp)
    {
        return 0.0f;
    }

    Object* obj = GetObject(oper2);
    EntityAI* veh = dyn_cast<EntityAI>(obj);
    if (!veh)
    {
        return 0.0f;
    }

    Target* target = grp->FindTarget(veh);
    if (!target)
    {
        return 0.0f;
    }

    return target->FadingSideAccuracy();
}

AIUnit* GetUnit(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    EntityAI* veh = dyn_cast<EntityAI>(obj);
    if (!veh)
    {
        return nullptr;
    }
    AIUnit* unit = veh->CommanderUnit();
    return unit;
}

static AIGroup* GetUnits(const GameState* state, GameValuePar oper1, PackedBoolArray& list)
{
    if (oper1.GetType() == GameObject)
    {
        AIUnit* unit = GetUnit(state, oper1);
        if (!unit)
        {
            return nullptr;
        }
        AIGroup* grp = unit->GetGroup();
        if (!grp)
        {
            return nullptr;
        }
        list.Set(unit->ID() - 1, true);
        return grp;
    }
    else
    {
        const GameArrayType& array = oper1;
        AIGroup* grp = nullptr;
        for (int i = 0; i < array.Size(); i++)
        {
            AIUnit* unit = GetUnit(state, array[i]);
            if (!unit)
            {
                continue;
            }
            AIGroup* g = unit->GetGroup();
            if (!g)
            {
                continue;
            }
            if (grp && g != grp)
            {
                continue; // different group
            }
            grp = g;
            list.Set(unit->ID() - 1, true);
        }
        return grp;
    }
}

GameValue VehDone(const GameState* state, GameValuePar oper1)
{
    PackedBoolArray list;
    AIGroup* grp = GetUnits(state, oper1, list);
    if (!grp)
    {
        return true;
    }
    for (int u = 0; u < MAX_UNITS_PER_GROUP; u++)
    {
        AIUnit* unit = grp->UnitWithID(u + 1);
        if (!unit)
        {
            continue;
        }
        if (!list[u])
        {
            continue;
        }
        AISubgroup* subgrp = unit->GetSubgroup();
        if (!subgrp)
        {
            continue;
        }
        if (!unit->IsSubgroupLeader())
        {
            continue;
        }
        if (!subgrp->HasCommand())
        {
            continue;
        }
        // some busy unit found
        return false;
    }
    return true;
}

GameValue GrpReveal(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    AIGroup* grp = GetGroup(oper1);
    if (!grp)
    {
        return NOTHING;
    }
    EntityAI* veh = dyn_cast<EntityAI>(GetObject(oper2));
    if (!veh)
    {
        return NOTHING;
    }
    // full immediate reveal
    grp->AddTarget(veh, 1, 1, 0);
    return NOTHING;
}

GameValue VehMove(const GameState* state, GameValuePar oper1, GameValuePar oper2, bool silent)
{
    PackedBoolArray list;
    AIGroup* grp = GetUnits(state, oper1, list);
    if (!grp)
    {
        return NOTHING;
    }
    Vector3 pos;
    if (!GetPos(state, pos, oper2))
    {
        return NOTHING;
    }
    Command cmd;
    cmd._message = Command::Move;
    cmd._destination = pos;
    cmd._discretion = Command::Undefined;
    cmd._context = Command::CtxMission;
    if (silent)
    {
        cmd._id = grp->GetNextCmdId();
        grp->IssueCommand(cmd, list);
    }
    else
    {
        grp->SendCommand(cmd, list);
    }

    return NOTHING;
}

DEFINE_COMMAND(Move)

GameValue VehFire(const GameState* state, GameValuePar oper1, GameValuePar oper2, bool silent)
{
    PackedBoolArray list;
    AIGroup* grp = GetUnits(state, oper1, list);
    if (!grp)
    {
        return NOTHING;
    }

    Object* obj = GetObject(oper2);
    EntityAI* veh = dyn_cast<EntityAI>(obj);

    Target* tgt = veh ? FindTargetReveal(grp, veh) : nullptr;
    grp->SendState(new RadioMessageTarget(grp, list, tgt, false, true), silent);
    return NOTHING;
}

DEFINE_COMMAND(Fire)

GameValue VehStop(const GameState* state, GameValuePar oper1, bool silent)
{
    PackedBoolArray list;
    AIGroup* grp = GetUnits(state, oper1, list);
    if (!grp)
    {
        return NOTHING;
    }
    Command cmd;
    cmd._message = Command::Stop;
    cmd._destination = VZero;
    cmd._discretion = Command::Undefined;
    cmd._context = Command::CtxMission;
    if (silent)
    {
        cmd._id = grp->GetNextCmdId();
        grp->IssueCommand(cmd, list);
    }
    else
    {
        grp->SendCommand(cmd, list);
    }

    return NOTHING;
}

DEFINE_COMMAND_S(Stop)

GameValue VehFollow(const GameState* state, GameValuePar oper1, GameValuePar oper2, bool silent)
{
    PackedBoolArray list;
    AIGroup* grp = GetUnits(state, oper1, list);
    if (!grp)
    {
        return NOTHING;
    }
    Vector3 pos;
    Object* obj = GetObject(oper2);
    EntityAI* veh = dyn_cast<EntityAI>(obj);
    if (!veh)
    {
        return NOTHING;
    }
    AIUnit* follow = veh->CommanderUnit();
    if (!follow)
    {
        LOG_DEBUG(Script, "Cannot follow empty vehicle");
        return NOTHING;
    }
    if (follow->GetGroup() != grp)
    {
        LOG_DEBUG(Script, "Cannot follow unit from other group");
        return NOTHING;
    }
    Command cmd;
    cmd._message = Command::Join;
    cmd._destination = VZero;
    cmd._discretion = Command::Undefined;
    cmd._context = Command::CtxMission;
    cmd._joinToSubgroup = follow->GetSubgroup();
    if (silent)
    {
        cmd._id = grp->GetNextCmdId();
        grp->IssueCommand(cmd, list);
    }
    else
    {
        grp->SendCommand(cmd, list);
    }

    return NOTHING;
}

DEFINE_COMMAND(Follow)

GameValue VehWatch(const GameState* state, GameValuePar oper1, GameValuePar oper2, bool silent)
{
    PackedBoolArray list;
    AIGroup* grp = GetUnits(state, oper1, list);
    if (!grp)
    {
        return NOTHING;
    }
    Vector3 pos;
    Target* tgt = nullptr;
    Object* obj = nullptr;
    bool noTgt = false;
    if (oper2.GetType() == GameObject)
    {
        obj = GetObject(oper2);
        if (!obj)
        {
            noTgt = true;
        }
        else
        {
            EntityAI* veh = dyn_cast<EntityAI>(obj);
            if (!veh)
            {
                pos = obj->Position();
            }
            else
            {
                tgt = FindTargetReveal(grp, veh);
                if (!tgt)
                {
                    pos = obj->Position();
                }
            }
        }
    }
    else if (!GetPos(state, pos, oper2))
    {
        return NOTHING;
    }
    if (noTgt)
    {
        grp->SendState(new RadioMessageWatchAuto(grp, list), silent);
    }
    else if (tgt)
    {
        grp->SendState(new RadioMessageWatchTgt(grp, list, tgt), silent);
    }
    else
    {
        grp->SendState(new RadioMessageWatchPos(grp, list, pos), silent);
    }
    return NOTHING;
}

DEFINE_COMMAND(Watch)

GameValue VehTarget(const GameState* state, GameValuePar oper1, GameValuePar oper2, bool silent)
{
    PackedBoolArray list;
    AIGroup* grp = GetUnits(state, oper1, list);
    if (!grp)
    {
        return NOTHING;
    }
    Vector3 pos;
    Target* tgt = nullptr;
    Object* obj = GetObject(oper2);
    EntityAI* veh = dyn_cast<EntityAI>(obj);
    if (veh)
    {
        tgt = FindTargetReveal(grp, veh);
    }

    grp->SendTarget(tgt, false, false, list, silent);
    return NOTHING;
}

DEFINE_COMMAND(Target)

GameValue VehProcessAction(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    EntityAI* veh = dyn_cast<EntityAI>(GetObject(oper1));
    if (!veh)
    {
        return NOTHING;
    }

    UIAction action;
    action.type = ATNone;
    action.target = veh;
    action.param = 0;
    action.param2 = 0;

    action.priority = 0;
    action.hideOnUse = true;
    action.showWindow = false;

    const GameArrayType& array = oper2;
    int n = array.Size();
    switch (n)
    {
        case 5:
            action.param3 = array[4];
        case 4:
            action.param2 = array[3];
        case 3:
            action.param = array[2];
        case 2:
            action.target = dyn_cast<EntityAI>(GetObject(array[1]));
        case 1:
        {
            GameStringType type = array[0];
            action.type = GetEnumValue<UIActionType>((const char*)type);
            if (action.type == INT_MIN)
            {
                action.type = ATNone;
            }
        }
            veh->StartActionProcessing(action, veh->CommanderUnit());
            return NOTHING;
        default:
            state->SetError(EvalDim, array.Size(), 5);
            return NOTHING;
    }
}

GameValue VehAddUserAction(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    EntityAI* veh = dyn_cast<EntityAI>(GetObject(oper1));
    if (!veh)
    {
        return -1.0f;
    }

    const GameArrayType& array = oper2;
    if (array.Size() != 2)
    {
        state->SetError(EvalDim, array.Size(), 2);
        return -1.0f;
    }
    if (array[0].GetType() != GameString)
    {
        state->TypeError(GameString, array[0].GetType());
        return -1.0f;
    }
    if (array[1].GetType() != GameString)
    {
        state->TypeError(GameString, array[1].GetType());
        return -1.0f;
    }
    return (float)veh->AddUserAction(array[0], array[1]);
}

GameValue VehRemoveUserAction(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    EntityAI* veh = dyn_cast<EntityAI>(GetObject(oper1));
    if (!veh)
    {
        return NOTHING;
    }

    veh->RemoveUserAction(toInt((float)oper2));
    return NOTHING;
}

static EntityEvent GetEvent(const GameValue& oper)
{
    RString name = oper;
    int event = GetEnumValue<EntityEvent>((const char*)name);
    if (event < 0 || event >= NEntityEvent)
    {
        return NEntityEvent;
    }
    return (EntityEvent)event;
}

GameValue VehAddEventHandler(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    const GameArrayType& array = oper2;
    if (array.Size() != 2)
    {
        state->SetError(EvalDim, array.Size(), 2);
        return -1.0f;
    }
    if (array[0].GetType() != GameString)
    {
        state->TypeError(GameString, array[0].GetType());
        return -1.0f;
    }
    if (array[1].GetType() != GameString)
    {
        state->TypeError(GameString, array[1].GetType());
        return -1.0f;
    }
    Object* obj = GetObject(oper1);
    EntityAI* ai = dyn_cast<EntityAI>(obj);
    EntityEvent event = GetEvent(array[0]);
    RString handler = array[1];
    if (ai && event < NEntityEvent)
    {
        return (float)ai->AddEventHandler(event, handler);
    }
    return -1.0f;
}
GameValue VehRemoveEventHandler(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
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
    Object* obj = GetObject(oper1);
    EntityAI* ai = dyn_cast<EntityAI>(obj);
    EntityEvent event = GetEvent(array[0]);
    int handle = toInt((float)array[1]);
    if (ai && event < NEntityEvent)
    {
        ai->RemoveEventHandler(event, handle);
    }
    return NOTHING;
}

GameValue VehRemoveAllEventHandlers(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    EntityAI* ai = dyn_cast<EntityAI>(obj);
    EntityEvent event = GetEvent(oper2);
    if (ai && event < NEntityEvent)
    {
        ai->ClearEventHandlers(event);
    }
    return NOTHING;
}

GameValue ObjGetPos(const GameState* state, GameValuePar oper1)
{
    Object* obj1 = GetObject(oper1);
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;
    array.Resize(3);
    if (obj1)
    {
        Vector3Val pos = obj1->WorldPosition();
        array[0] = pos.X();
        array[1] = pos.Z();
        // check position corresponding to (0,0,0)
        Matrix4 trans = obj1->WorldTransform();
        EntityAI* veh = dyn_cast<EntityAI>(obj1);
        if (veh)
        {
            veh->PlaceOnSurface(trans);
            array[2] = pos.Y() - trans.Position().Y();
        }
        else
        {
            float yOffset = 0;
            LODShape* shape = obj1->GetShape();
            if (!shape)
            {
                array[0] = 0.0f;
                array[1] = 0.0f;
                array[2] = 0.0f;
                return array;
            }
            Shape* geom = shape->LandContactLevel();
            if (!geom)
            {
                geom = shape->GeometryLevel();
            }
            if (!geom)
            {
                geom = shape->Level(0);
            }
            if (geom)
            {
                yOffset = geom->Min().Y();
            }
            else
            {
                yOffset = shape->Min().Y();
            }

            array[2] = pos.Y() - GLandscape->SurfaceYAboveWater(pos.X(), pos.Z()) - yOffset;
        }
    }
    else
    {
        array[0] = 0.0f;
        array[1] = 0.0f;
        array[2] = 0.0f;
    }
    return value;
}

GameValue ObjGetPosASL(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;
    array.Resize(3);
    if (obj)
    {
        Vector3Val pos = obj->WorldPosition();
        array[0] = pos.X();
        array[1] = pos.Z();
        array[2] = pos.Y();
    }
    else
    {
        array[0] = 0.0f;
        array[1] = 0.0f;
        array[2] = 0.0f;
    }
    return value;
}

GameValue ObjSetPosASL(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }

    Vector3 pos;
    if (!GetVector(pos, oper2))
    {
        return NOTHING;
    }
    if (obj->GetType() == Primary || obj->GetType() == Network)
    {
        return NOTHING;
    }

    // let vehicle adjust position

    Matrix4 trans = obj->Transform();
    trans.SetPosition(pos);

    EntityAI* veh = dyn_cast<EntityAI>(obj);
    if (veh)
    {
        veh->IsMoved();
    }

    Person* soldier = dyn_cast<Person>(veh);
    if (soldier)
    {
        AIUnit* unit = soldier->Brain();
        if (unit)
        {
            Transport* transport = unit->GetVehicleIn();
            if (transport)
            {
                bool isFocused = unit == GWorld->FocusOn();
                Matrix4 transform = soldier->Transform();
                transform.SetPosition(pos);
                transport->GetOutFinished(unit);
                if (soldier == transport->Driver())
                {
                    transport->GetOutDriver(transform);
                    if (unit->GetGroup())
                    {
                        unit->GetGroup()->CalculateMaximalStrength();
                    }
                }
                else if (soldier == transport->Commander())
                {
                    transport->GetOutCommander(transform);
                }
                else if (soldier == transport->Gunner())
                {
                    transport->GetOutGunner(transform);
                }
                else
                {
                    transport->GetOutCargo(soldier, transform);
                }
                soldier->SetSpeed(VZero);
                if (unit->GetState() == AIUnit::InCargo)
                {
                    unit->SetState(AIUnit::Wait);
                }
                if (isFocused)
                {
                    GWorld->SwitchCameraTo(soldier, GWorld->GetCameraType());
                }
                return NOTHING;
            }
        }
    }

    obj->MoveNetAware(trans);
    obj->OnPositionChanged();
    GScene->GetShadowCache().ShadowChanged(obj);
    return NOTHING;
}

GameValue ObjSetPos(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj1 = GetObject(oper1);
    if (!obj1)
    {
        return NOTHING;
    }

    Vector3 pos;
    if (!GetRelPos(state, pos, oper2))
    {
        return NOTHING;
    }
    if (obj1->GetType() == Primary || obj1->GetType() == Network)
    {
        return NOTHING;
    }

    // let vehicle adjust position

    Matrix4 trans = obj1->Transform();

    EntityAI* veh = dyn_cast<EntityAI>(obj1);
    if (veh)
    {
        trans.SetPosition(pos);
        veh->PlaceOnSurface(trans);
        Vector3 surfPos = trans.Position();
        surfPos[1] += pos[1];
        trans.SetPosition(surfPos);
        if (veh->GetType()->HasDriver() && pos[1] > 0.1f)
        {
            veh->IsMoved();
        }
    }
    else
    {
        LODShape* shape = obj1->GetShape();
        if (!shape)
        {
            return NOTHING;
        }
        Shape* geom = shape->LandContactLevel();
        if (!geom)
        {
            geom = shape->GeometryLevel();
        }
        if (!geom)
        {
            geom = shape->Level(0);
        }
        if (geom)
        {
            pos[1] += geom->Min().Y();
        }
        else
        {
            pos[1] += shape->Min().Y();
        }
        trans.SetPosition(pos);
    }

    Person* soldier = dyn_cast<Person>(veh);
    if (soldier)
    {
        AIUnit* unit = soldier->Brain();
        if (unit)
        {
            Transport* transport = unit->GetVehicleIn();
            if (transport)
            {
                bool isFocused = unit == GWorld->FocusOn();
                Matrix4 transform = soldier->Transform();
                transform.SetPosition(pos);
                transport->GetOutFinished(unit);
                if (soldier == transport->Driver())
                {
                    transport->GetOutDriver(transform);
                    if (unit->GetGroup())
                    {
                        unit->GetGroup()->CalculateMaximalStrength();
                    }
                }
                else if (soldier == transport->Commander())
                {
                    transport->GetOutCommander(transform);
                }
                else if (soldier == transport->Gunner())
                {
                    transport->GetOutGunner(transform);
                }
                else
                {
                    transport->GetOutCargo(soldier, transform);
                }
                soldier->SetSpeed(VZero);
                if (unit->GetState() == AIUnit::InCargo)
                {
                    unit->SetState(AIUnit::Wait);
                }
                if (isFocused)
                {
                    GWorld->SwitchCameraTo(soldier, GWorld->GetCameraType());
                }
                return NOTHING;
            }
        }
    }

    obj1->MoveNetAware(trans);
    obj1->OnPositionChanged();
    GScene->GetShadowCache().ShadowChanged(obj1);
    return NOTHING;
}

GameValue ObjGetDir(const GameState* state, GameValuePar oper1)
{
    Object* obj1 = GetObject(oper1);
    if (obj1)
    {
        Vector3Val dir = obj1->Direction();
        float azimut = atan2(dir.X(), dir.Z()) * (180.0f / H_PI);
        if (azimut < 0)
        {
            azimut += 360.0f;
        }
        return azimut;
    }
    else
    {
        return 0.0f;
    }
}

GameValue ObjGetTurretDirAndElev(const GameState* state, GameValuePar oper1)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;

    Object* obj1 = GetObject(oper1);
    Tank* pTank = dyn_cast<Tank>(obj1);
    if (!pTank)
    {
        return array;
    }

    Vector3P dirTurret = pTank->GetTurretAbsDirection();
    array.Resize(2);
    array[0] = (float)atan2(dirTurret.X(), dirTurret.Z()) * (180 / H_PI);
    array[1] = (float)atan2(dirTurret.Y(), abs(dirTurret.X())) * (180 / H_PI);
    return array;
}

GameValue ObjSetDir(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj1 = GetObject(oper1);
    if (!obj1)
    {
        return NOTHING;
    }

    float azimut = oper2;
    Matrix3 rotY(MRotationY, -HDegree(azimut));
    obj1->SetOrient(rotY);
    return NOTHING;
}

GameValue ObjGetVelocity(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    Entity* veh = dyn_cast<Entity>(obj);
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;
    array.Resize(3);
    if (veh)
    {
        Vector3Val speed = veh->Speed();
        array[0] = speed.X();
        array[1] = speed.Z();
        array[2] = speed.Y();
    }
    else
    {
        array[0] = 0.0f;
        array[1] = 0.0f;
        array[2] = 0.0f;
    }
    return value;
}

GameValue ObjSetVelocity(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    Entity* veh = dyn_cast<Entity>(obj);
    if (!veh)
    {
        return NOTHING;
    }

    const GameArrayType& array = oper2;
    if (array.Size() != 3)
    {
        state->SetError(EvalDim, array.Size(), 3);
        return NOTHING;
    }
    if (array[0].GetType() != GameScalar)
    {
        state->TypeError(GameScalar, array[0].GetType());
        return NOTHING;
    }
    if (array[1].GetType() != GameScalar)
    {
        state->TypeError(GameScalar, array[1].GetType());
        return NOTHING;
    }
    if (array[2].GetType() != GameScalar)
    {
        state->TypeError(GameScalar, array[2].GetType());
        return NOTHING;
    }

    Vector3 speed(array[0], array[2], array[1]);
    veh->SetSpeed(speed);
    return NOTHING;
}

GameValue ObjSetFlyingHeight(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    EntityAI* veh = dyn_cast<EntityAI>(obj);
    if (!veh)
    {
        return NOTHING;
    }
    veh->SetFlyingHeight(oper2);
    return NOTHING;
}

GameValue ObjGetSpeed(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    Vehicle* veh = dyn_cast<Vehicle>(obj);
    if (!veh)
    {
        return 0.0f;
    }

    return 3.6f * veh->ModelSpeed().Z();
}

GameValue SelectPlayer(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    EntityAI* ai = dyn_cast<EntityAI>(obj);
    if (!ai)
    {
        return NOTHING;
    }

    AIUnit* unit = ai->CommanderUnit();
    if (!unit)
    {
        return NOTHING;
    }

    GWorld->SwitchCameraTo(unit->GetVehicle(), CamInternal);
    GWorld->SetPlayerManual(true);
    GWorld->SwitchPlayerTo(unit->GetPerson());
    GWorld->SetRealPlayer(unit->GetPerson());

    return NOTHING;
}
