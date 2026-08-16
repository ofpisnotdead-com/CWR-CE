#include <Poseidon/Game/Scripting/Scripts.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/AI/AIRadio.hpp>
#include <Poseidon/Core/FSM/Fsm.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/World/Detection/Detector.hpp>
#include <Poseidon/World/Entities/Vehicles/House.hpp>
#include <Poseidon/World/Entities/Infantry/Person.hpp>
#include <Poseidon/Audio/DynSound.hpp>
#include <Poseidon/AI/ArcadeTemplate.hpp>

#include <Poseidon/World/Scene/Camera/CamEffects.hpp>
#include <Poseidon/Game/TitEffects.hpp>

#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/UI/OptionsUI.hpp>

#include <Random/randomGen.hpp>

#include <Poseidon/Game/Commands/GameStateExt.hpp>
#include <Poseidon/Network/Network.hpp>
#include <float.h>
#include <string.h>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/BoolArray.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Memtype.h>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

using namespace Poseidon;
extern SoundPars EnvSoundPars[];
extern SoundPars EnvSoundParsNight[];

namespace Poseidon
{
using namespace Foundation;

inline void Trace(AIGroup* grp, const char* waypoint, const char* state)
{
#if 1
    if (grp)
    {
        LOG_DEBUG(AI, "FSM: {} - waypoint {} - state {}, time {:.1f}", (const char*)grp->GetDebugName(), waypoint,
                  state, Glob.time.toFloat());
    }
#endif
}

static void AllowGetIn(AIGroup* group, bool allow = true)
{
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* unit = group->UnitWithID(i + 1);
        if (unit)
        {
            unit->AllowGetIn(allow);
        }
    }
}

static bool IsTargetValid(const Target* info, AIUnit* leader)
{
    return info && !info->destroyed && !info->vanished && info->IsKnownBy(leader);
}

#define STATE_PREFIX                   \
    AIGroup* group = context->_group;  \
    AI_ERROR(group);                   \
    Mission* mission = context->_task; \
    AI_ERROR(mission);                 \
    (void)mission;                     \
    (void)group;

// FSM Variables usage

// Var(0) - waypoint index
// Var(1) - waypoint type
// Var(2) - waypoint idStatic
// Var(3) - waypoint id
// Var(4) - waiting for sychronization
// Var(5) - counter for Brown movement
// VarTime(0) - time limit

// Generic Mission Functions

void MissionSucceed(AIGroupContext* context)
{
    AIGroup* group = context->_group;
    AI_ERROR(group);

    group->SendAnswer(AI::MissionCompleted);
}

void CheckMissionSucceed(AIGroupContext* context)
{
    //	do not delete Arcade FSM - because of flee command
}

void MissionFailed(AIGroupContext* context)
{
    AIGroup* group = context->_group;
    AI_ERROR(group);

    group->SendAnswer(AI::MissionFailed);
}

void CheckMissionFailed(AIGroupContext* context)
{
    //	do not delete Arcade FSM - because of flee command
}

// No Mission FSM

static void NoMissionWait(AIGroupContext* context) {}

static void CheckNoMissionWait(AIGroupContext* context) {}

// Mission Arcade FSM

enum ArcadeFSMStates
{
    SArcadeInit,
    SArcadeTurn,
    SArcadeMoveMove,
    SArcadeTalkMove,
    SArcadeTalkGetOut,
    SArcadeTalkWalk,
    SArcadeDestroyMove,
    SArcadeDestroyBrown,
    SArcadeDestroyAttack,
    SArcadeGetInMove,
    SArcadeGetInSync,
    SArcadeGetInGetIn,
    SArcadeSeekAndDestroyMove,
    SArcadeSeekAndDestroyCheck,
    SArcadeSeekAndDestroyWait,
    SArcadeSeekAndDestroyOverlook,
    SArcadeSeekAndDestroyBrown,
    SArcadeJoinMove,
    SArcadeJoinSync,
    SArcadeJoinJoin,
    SArcadeLeaderMove,
    SArcadeLeaderSync,
    SArcadeLeaderJoin,
    SArcadeGetOutMove,
    SArcadeGetOutGetOut,
    SArcadeLoadMove,
    SArcadeLoadGetIn,
    SArcadeUnloadMove,
    SArcadeUnloadGetOut,
    SArcadeTransportUnloadMove,
    SArcadeTransportUnloadGetOut,
    SArcadeHoldMove,
    SArcadeHoldWait,
    SArcadeHoldOverlook,
    SArcadeSentryMove,
    SArcadeSentryWait,
    SArcadeSentryOverlook,
    SArcadeSentryBrown,
    SArcadeGuardMove,
    SArcadeGuardWait,
    SArcadeGuardAttack,
    SArcadeGuardOverlook,
    SArcadeGuardBrown,
    SArcadeGuardBrownTarget,
    SArcadeGravonWait,
    SArcadeGravonAttack,
    SArcadeGravonOverlook,
    SArcadeGravonMove,
    SArcadeSupportMove,
    SArcadeSupportWait,
    SArcadeSupportTransport,
    SArcadeSupportSupply,
    SArcadeScripted,
    SArcadeLogic,
    SArcadeSync,
    SArcadeCountdown,
    SArcadeNext,
    SArcadeUnlock,

    SArcadeFlee,

    SArcadeDone,
    SArcadeFail
};

void Flee(AIGroupContext* context)
{
    AIGroup* group = context->_group;
    AI_ERROR(group);

    // reset radio messages to be able to react quickly
    group->GetRadio().CancelAllMessages();
    // reset all commands, join all subgroups
    for (int i = 0; i < group->NSubgroups();)
    {
        AISubgroup* subgrp = group->GetSubgroup(i);
        AI_ERROR(subgrp); // RefArray
        if (subgrp == group->MainSubgroup())
        {
            subgrp->ClearAllCommands();
            i++;
        }
        else
        {
            int nSubgroups = group->NSubgroups();
            subgrp->JoinToSubgroup(group->MainSubgroup());
            if (group->NSubgroups() >= nSubgroups)
            {
                Fail("Subgroup not joined");
                i++;
            }
        }
    }

    context->_fsm->SetState(SArcadeFlee, context);
}

void Unflee(AIGroupContext* context)
{
    AIGroup* group = context->_group;
    AI_ERROR(group);

    AI::Semaphore sem = AI::SemaphoreYellow;
    SpeedMode speed = SpeedNormal;
    int& index = context->_fsm->Var(0);
    if (index < group->NWaypoints())
    {
        const ArcadeWaypointInfo& wInfo = group->GetWaypoint(index);
        speed = wInfo.speed;
        if (wInfo.combatMode >= 0)
        {
            sem = (AI::Semaphore)wInfo.combatMode;
        }
    }

    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* unit = group->UnitWithID(i + 1);
        if (unit)
        {
            unit->SetSemaphore(sem);
        }
    }
    group->SetSemaphore(sem);
    group->MainSubgroup()->SetSpeedMode(speed);

    context->_fsm->SetState(SArcadeTurn, context);
}

#define SYNC_TIMEOUT 600.0f // 10 min
#define PRECISION_COEF 5.0f // benevolence of waypoints accomplishment for player
#define MOVE_BACK_COEF 3.5f
#define MOVE_BACK_MINIMUM 10.0f

static void SetFormMode(AIGroup* group, int index)
{
    // combat mode, formation
    if (!group->IsPlayerGroup())
    {
        PackedBoolArray all;
        for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
        {
            AIUnit* unit = group->UnitWithID(i + 1);
            if (!unit)
            {
                continue;
            }
            all.Set(i, true);
        }

        const ArcadeWaypointInfo& wInfo = group->GetWaypoint(index);
        if (wInfo.formation >= 0)
        {
            AI::Formation f = wInfo.formation;
            group->SendFormation(f, group->MainSubgroup());
        }
        if (wInfo.combatMode >= 0)
        {
            AI::Semaphore s = wInfo.combatMode;
            group->SetSemaphore(s);
            group->SendSemaphore(s, all);
        }
        if (wInfo.speed != SpeedUnchanged)
        {
            group->MainSubgroup()->SetSpeedMode(wInfo.speed);
        }
        if (wInfo.combat != CMUnchanged)
        {
            group->SendBehaviour(wInfo.combat, all);
        }
    }
}

bool IsSyncActive(AIGroup* group, ArcadeWaypointInfo& wInfo, int oper = ACAND)
{
    AIUnit* leader = group->Leader();
    Object* leaderVeh = leader ? leader->GetPerson() : nullptr;

    GameState* gstate = GWorld->GetGameState();
    GameValue thisList = gstate->CreateGameValue(GameArray);
    GameArrayType& array = thisList;
    if (group)
    {
        array.Realloc(group->NUnits());
        for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
        {
            AIUnit* unit = group->UnitWithID(i + 1);
            if (!unit)
            {
                continue;
            }
            EntityAI* veh = unit->GetPerson();
            array.Add(GameValueExt(veh));
        }
    }
    gstate->VarSet("this", GameValueExt(leaderVeh), true);
    gstate->VarSet("thisList", thisList, true);
    if (!gstate->EvaluateBool(wInfo.expCond))
    {
        return true;
    }

    int n = wInfo.synchronizations.Size();
    if (n == 0)
    {
        return false;
    }
    switch (oper)
    {
        case ACOR:
            for (int i = 0; i < n; i++)
            {
                int sync = wInfo.synchronizations[i];
                AI_ERROR(sync >= 0);
                if (!synchronized[sync].IsActive(group))
                {
                    return false;
                }
            }
            return true;
        case ACAND:
        default:
            for (int i = 0; i < n; i++)
            {
                int sync = wInfo.synchronizations[i];
                AI_ERROR(sync >= 0);
                if (synchronized[sync].IsActive(group))
                {
                    return true;
                }
            }
            return false;
    }
}

static void ArcadeInit(AIGroupContext* context)
{
    int& index = context->_fsm->Var(0);
    index = 1;
    int& waiting = context->_fsm->Var(4);
    waiting = FALSE;
}

static void CheckArcadeInit(AIGroupContext* context)
{
    context->_fsm->SetState(SArcadeTurn, context);
}

static void ArcadeTurn(AIGroupContext* context)
{
    int& index = context->_fsm->Var(0);
    int& type = context->_fsm->Var(1);

    int& waiting = context->_fsm->Var(4);
    waiting = FALSE;

    STATE_PREFIX

    if (index < group->NWaypoints())
    {
    getWaypoint:
        const ArcadeWaypointInfo& wInfo = group->GetWaypoint(index);

        type = wInfo.type;
        if (type == ACCYCLE)
        {
            if (index == 0)
            {
                context->_fsm->SetState(SArcadeDone, context);
                return;
            }
            float minDist2 = FLT_MAX;
            int iBest = -1;
            for (int i = 0; i < index; i++)
            {
                float dist2 = (group->GetWaypoint(i).position - wInfo.position).SquareSizeXZ();
                if (dist2 < minDist2)
                {
                    iBest = i;
                    minDist2 = dist2;
                }
            }
            AI_ERROR(iBest >= 0);
            for (int i = iBest; i < index; i++)
            {
                ArcadeWaypointInfo& wInfo = group->GetWaypoint(i);
                for (int j = 0; j < wInfo.synchronizations.Size(); j++)
                {
                    int sync = wInfo.synchronizations[j];
                    AI_ERROR(sync >= 0);
                    synchronized[sync].SetActive(group, true);
                    GetNetworkManager().GroupSynchronization(group, sync, true);
                }
            }

            index = iBest;
            goto getWaypoint;
        }

        mission->_destination = wInfo.position;
        int& id = context->_fsm->Var(3);
        id = wInfo.id;
        int& idStatic = context->_fsm->Var(2);
        idStatic = wInfo.idStatic;
    }
    else
    {
        context->_fsm->SetState(SArcadeDone, context);
    }
}

} // namespace Poseidon
void OnWaypointsUpdated(AIGroupContext* context)
{
    using namespace Poseidon;
    if (context->_fsm->GetState() == SArcadeDone)
    {
        context->_fsm->SetState(SArcadeTurn, context); // for finished FSM get chance to process new waypoints
    }
}
namespace Poseidon
{

static void CheckArcadeTurn(AIGroupContext* context)
{
    int& type = context->_fsm->Var(1);
    AIGroup* group = context->_group;
    AI_ERROR(group);

    SetFormMode(group, context->_fsm->Var(0));
    switch (type)
    {
        case ACMOVE:
            context->_fsm->SetState(SArcadeMoveMove, context);
            break;
        case ACTALK:
            context->_fsm->SetState(SArcadeTalkMove, context);
            break;
        case ACDESTROY:
            context->_fsm->SetState(SArcadeDestroyMove, context);
            break;
        case ACGETIN:
            context->_fsm->SetState(SArcadeGetInMove, context);
            break;
        case ACSEEKANDDESTROY:
            context->_fsm->SetState(SArcadeSeekAndDestroyMove, context);
            break;
        case ACJOIN:
            context->_fsm->SetState(SArcadeJoinMove, context);
            break;
        case ACLEADER:
            context->_fsm->SetState(SArcadeLeaderMove, context);
            break;
        case ACGETOUT:
            context->_fsm->SetState(SArcadeGetOutMove, context);
            break;
        case ACLOAD:
            context->_fsm->SetState(SArcadeLoadMove, context);
            break;
        case ACUNLOAD:
            context->_fsm->SetState(SArcadeUnloadMove, context);
            break;
        case ACTRANSPORTUNLOAD:
            context->_fsm->SetState(SArcadeTransportUnloadMove, context);
            break;
        case ACHOLD:
            context->_fsm->SetState(SArcadeHoldMove, context);
            break;
        case ACSENTRY:
            context->_fsm->SetState(SArcadeSentryMove, context);
            break;
        case ACGUARD:
            context->_fsm->SetState(SArcadeGuardMove, context);
            break;
        case ACSUPPORT:
            context->_fsm->SetState(SArcadeSupportMove, context);
            break;
        case ACSCRIPTED:
            context->_fsm->SetState(SArcadeScripted, context);
            break;
        case ACOR:
        case ACAND:
            context->_fsm->SetState(SArcadeLogic, context);
            break;
    }
}

static Vector3 GetWaypointPosition(AIGroup* group, int index)
{
    AI_ERROR(group);
    AI_ERROR(index < group->NWaypoints());
    const ArcadeWaypointInfo& wInfo = group->GetWaypoint(index);

    EntityAI* target = nullptr;
    if (wInfo.id >= 0 && wInfo.id < vehiclesMap.Size())
    {
        target = vehiclesMap[wInfo.id];
    }
    else if (wInfo.idStatic >= 0)
    {
        target = dyn_cast<EntityAI>(GLOB_LAND->FindObject(wInfo.idStatic));
    }

    if (target)
    {
        const AITargetInfo* info = group->GetCenter()->FindTargetInfo(target);
        if (info)
        {
            return info->_realPos;
        }
    }

    return wInfo.position;
}

static void ArcadeMove(AIGroupContext* context)
{
    STATE_PREFIX

    int& index = context->_fsm->Var(0);
    mission->_destination = GetWaypointPosition(group, index);

    if (group->IsPlayerGroup())
    {
        return;
    }

    group->Move(group->MainSubgroup(), // who
                mission->_destination, // where
                Command::Undefined     // how
    );
}

static bool CheckArcadeMove(AIGroupContext* context, ArcadeFSMStates next)
{
    STATE_PREFIX

    AIUnit* leader = group->Leader();
    if (!leader)
    {
        return false;
    }

    if (group->IsPlayerDrivenGroup())
    {
        for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
        {
            AIUnit* unit = group->UnitWithID(i + 1);
            if (!unit)
            {
                continue;
            }

            Vector3Val posU = unit->Position();
            Vector3Val posD = mission->_destination;
            float prec = PRECISION_COEF * unit->GetVehicle()->GetPrecision();
            saturateMax(prec, 10.0F);
            if ((posD - posU).SquareSizeXZ() <= Square(prec))
            {
                context->_fsm->SetState(next, context);
                return true;
            }
        }
        return false;
    }
    else if (group->GetCenter()->GetSide() == TLogic)
    {
        context->_fsm->SetState(next, context);
        return true;
    }
    else
    {
        if (group->GetAllDone())
        {
            context->_fsm->SetState(next, context);
            return true;
        }
        else
        {
            AIUnit* vehicleCommander = leader->GetVehicle()->CommanderUnit();
            if (vehicleCommander && vehicleCommander->GetGroup() == group)
            {
                Vector3Val posL = leader->Position();
                Vector3Val posD = mission->_destination;
                float prec = 1.0 * leader->GetVehicle()->GetPrecision();
                if (posD.Distance2(posL) <= Square(prec))
                {
                    context->_fsm->SetState(next, context);
                    return true;
                }
            }
            return false;
        }
    }
}

static void ArcadeWait(AIGroupContext* context)
{
    /*
        STATE_PREFIX

        if (!group->IsPlayerGroup())
            group->Wait
            (
                group->MainSubgroup(),	// who
                TIME_MIN,								// until
                Command::Undefined			// how
            );
    */
}

void ApplyEffects(AIGroup* group, int index)
{
    AI_ERROR(group);
    if (!group)
    {
        return;
    }
    AI_ERROR(index < group->NWaypoints());
    if (index >= group->NWaypoints())
    {
        return;
    }
    const ArcadeWaypointInfo& wInfo = group->GetWaypoint(index);
    const ArcadeEffects& effects = wInfo.effects;

    OLink<AIUnit> leader = group->Leader();
    OLink<Object> leaderVeh = leader ? leader->GetPerson() : nullptr;

    GameState* gstate = GWorld->GetGameState();
    GameValue thisList = gstate->CreateGameValue(GameArray);
    GameArrayType& array = thisList;
    if (group)
    {
        array.Realloc(group->NUnits());
        for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
        {
            AIUnit* unit = group->UnitWithID(i + 1);
            if (!unit)
            {
                continue;
            }
            EntityAI* veh = unit->GetPerson();
            array.Add(GameValueExt(veh));
        }
    }
    gstate->VarSet("this", GameValueExt(leaderVeh), true);
    gstate->VarSet("thisList", thisList, true);
    gstate->Execute(wInfo.expActiv);
    GameValue result = gstate->Evaluate(effects.condition);
    if (result.GetType() == GameObject)
    {
        AIUnit* player = GWorld->FocusOn();
        if (!player)
        {
            return;
        }
        Object* obj = static_cast<GameDataObject*>(result.GetData())->GetObject();
        if (player->GetPerson() != obj && player->GetVehicle() != obj)
        {
            return;
        }
    }
    else if (result.GetType() == GameArray)
    {
        AIUnit* player = GWorld->FocusOn();
        if (!player)
        {
            return;
        }
        bool found = false;
        const GameArrayType& array = (const GameArrayType&)(GameArrayType&)result;
        for (int i = 0; i < array.Size(); i++)
        {
            const GameValue& item = array[i];
            Object* obj = static_cast<GameDataObject*>(item.GetData())->GetObject();
            if (player->GetPerson() == obj || player->GetVehicle() == obj)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            return;
        }
    }
    else if (result.GetType() & GameBool)
    {
        if (!(bool)result)
        {
            return;
        }
    }
    else
    {
        return;
    }

    Object* obj = leader ? leader->GetVehicle() : nullptr;

    if (effects.cameraEffect.GetLength() > 0)
    {
        if (stricmp(effects.cameraEffect, "$TERMINATE$") == 0)
        {
            GLOB_WORLD->SetCameraEffect(nullptr);
        }
        else
        {
            GLOB_WORLD->SetCameraEffect(CreateCameraEffect(obj, effects.cameraEffect, effects.cameraPosition));
        }
    }

    if (stricmp(effects.sound, "$NONE$") != 0)
    {
        Vehicle* veh = new SoundOnVehicle(effects.sound, nullptr);
        GWorld->AddBuilding(veh);
    }

    if (effects.voice.GetLength() > 0)
    {
        Vehicle* veh = new SoundOnVehicle(effects.voice, obj);
        if (obj)
        {
            veh->SetPosition(obj->Position());
        }
        GWorld->AddBuilding(veh);
    }

    if (effects.soundEnv.GetLength() > 0)
    {
        FindEnvSound(effects.soundEnv, EnvSoundPars[5], EnvSoundParsNight[5]);
    }

    // effects.soundDet - used only for detector

    if (stricmp(effects.track, "$NONE$") == 0)
    {
        // nothing to do
    }
    else if (stricmp(effects.track, "$STOP$") == 0)
    {
        GSoundScene->StopMusicTrack();
    }
    else
    {
        SoundPars sound;
        if (FindMusic(effects.track, sound))
        {
            GSoundScene->StartMusicTrack(sound);
        }
    }

    switch (effects.titleType)
    {
        case TitleNone:
            break;
        case TitleObject:
            GLOB_WORLD->SetTitleEffect(CreateTitleEffectObj(effects.titleEffect, Pars >> "CfgTitles" >> effects.title));
            break;
        case TitleResource:
        {
            const ParamEntry* cls = FindRscTitle(effects.title);
            if (cls)
            {
                GWorld->SetTitleEffect(CreateTitleEffectRsc(effects.titleEffect, *cls));
            }
        }
        break;
        case TitleText:
            GLOB_WORLD->SetTitleEffect(CreateTitleEffect(effects.titleEffect, Localize(effects.title)));
            break;
    }
}

static void ArcadeSync(AIGroupContext* context)
{
    AIGroup* group = context->_group;
    AI_ERROR(group);

    int& index = context->_fsm->Var(0);
    ArcadeWaypointInfo& wInfo = group->GetWaypoint(index);

    for (int j = 0; j < wInfo.synchronizations.Size(); j++)
    {
        int sync = wInfo.synchronizations[j];
        AI_ERROR(sync >= 0);
        synchronized[sync].SetActive(group, false);
        GetNetworkManager().GroupSynchronization(group, sync, false);
    }

    if (IsSyncActive(group, wInfo))
    {
        int& waiting = context->_fsm->Var(4);
        waiting = TRUE;

        if (!group->IsPlayerGroup())
        {
            group->Wait(group->MainSubgroup(),    // who
                        Glob.time + SYNC_TIMEOUT, // timeout
                        Command::Undefined        // how
            );
        }
    }
}

static void CheckArcadeSync(AIGroupContext* context, ArcadeFSMStates next)
{
    AIGroup* group = context->_group;
    AI_ERROR(group);
    int& index = context->_fsm->Var(0);
    ArcadeWaypointInfo& wInfo = group->GetWaypoint(index);

    if (!IsSyncActive(group, wInfo))
    {
        int& waiting = context->_fsm->Var(4);
        waiting = FALSE;

        context->_fsm->SetState(next, context);
    }
}

static void CheckArcadeSync(AIGroupContext* context)
{
    CheckArcadeSync(context, SArcadeCountdown);
}

static void ArcadeCountdown(AIGroupContext* context)
{
    AIGroup* group = context->_group;
    AI_ERROR(group);

    int& index = context->_fsm->Var(0);

    ArcadeWaypointInfo& wInfo = group->GetWaypoint(index);
    Time limit = Glob.time + GRandGen.Gauss(wInfo.timeoutMin, wInfo.timeoutMid, wInfo.timeoutMax);
    Time& time = context->_fsm->VarTime(0);
    time = limit;
}

static void CheckArcadeCountdown(AIGroupContext* context)
{
    Time& time = context->_fsm->VarTime(0);
    if (Glob.time >= time)
    {
        context->_fsm->SetState(SArcadeNext, context);
    }
}

static void ArcadeNext(AIGroupContext* context) {}

static void CheckArcadeNext(AIGroupContext* context)
{
    context->_fsm->SetState(SArcadeUnlock, context);

    AIGroup* group = context->_group;
    AI_ERROR(group);
    int& index = context->_fsm->Var(0);
    ApplyEffects(group, index);
}

static void ArcadeUnlock(AIGroupContext* context) {}

static void CheckArcadeUnlock(AIGroupContext* context)
{
    AIGroup* group = context->_group;
    AI_ERROR(group);
    if (!group->IsLockedWP())
    {
        int& index = context->_fsm->Var(0);
        index++;
        context->_fsm->SetState(SArcadeTurn, context);
    }
}

// Move waypoint

static void ArcadeMoveMove(AIGroupContext* context)
{
    STATE_PREFIX
    if (group->IsPlayerGroup())
    {
        return;
    }

    int& index = context->_fsm->Var(0);
    ArcadeWaypointInfo& wInfo = group->GetWaypoint(index);

    mission->_destination = GetWaypointPosition(group, index);

    if (group->GetCenter()->GetSide() == TLogic)
    {
        group->Leader()->GetVehicle()->Move(mission->_destination);
        return;
    }

    AISubgroup* subgrp = group->MainSubgroup();
    AI_ERROR(subgrp);
    AIUnit* leader = subgrp->Leader();
    Vector3Val posD = mission->_destination;
    if (leader)
    {
        Vector3Val posL = leader->Position();

        float dist = 200;
        if ((posL - posD).SquareSizeXZ() > Square(dist))
        {
            for (int i = 0; i < subgrp->NUnits(); i++)
            {
                AIUnit* unit = subgrp->GetUnit(i);
                if (unit)
                {
                    unit->OrderGetIn(true);
                }
            }
        }
        group->AssignVehicles();
        group->GetInVehicles();
    }

    Command cmd;
    cmd._message = Command::Move;
    cmd._destination = posD;
    if (wInfo.idStatic >= 0 && wInfo.housePos >= 0)
    {
        for (int i = 0; i < GWorld->NBuildings(); i++)
        {
            EntityAI* veh = dyn_cast<EntityAI>(GWorld->GetBuilding(i));
            if (!veh)
            {
                continue;
            }
            if (veh->ID() == wInfo.idStatic)
            {
                cmd._target = veh;
                cmd._param = wInfo.housePos;
                const IPaths* house = veh->GetIPaths();
                if (house)
                {
                    if (wInfo.housePos >= 0 && wInfo.housePos < house->NPos())
                    {
                        cmd._destination = house->GetPosition(house->GetPos(wInfo.housePos));
                    }
                }
                break;
            }
        }
    }
    cmd._discretion = Command::Undefined;
    cmd._context = Command::CtxMission;
    group->SendCommand(cmd);
}

static void CheckArcadeMoveMove(AIGroupContext* context)
{
    CheckArcadeMove(context, SArcadeSync);
}

// Scripted waypoint

static void ArcadeScripted(AIGroupContext* context)
{
    STATE_PREFIX

    int& index = context->_fsm->Var(0);
    ArcadeWaypointInfo& wInfo = group->GetWaypoint(index);

    GameArrayType position;
    position.Resize(3);
    position[0] = wInfo.position.X();
    position[1] = wInfo.position.Z();
    position[2] = 0.0f;

    EntityAI* target = nullptr;
    if (wInfo.id >= 0 && wInfo.id < vehiclesMap.Size())
    {
        target = vehiclesMap[wInfo.id];
    }
    else if (wInfo.idStatic >= 0)
    {
        target = dyn_cast<EntityAI>(GLOB_LAND->FindObject(wInfo.idStatic));
    }

    RString nameScript = wInfo.script;
    RString nameArgs;
    const char* space = strchr(nameScript, ' ');
    if (space)
    {
        nameArgs = space + 1;
        nameScript = nameScript.Substring(0, space - nameScript);
    }

    GameArrayType arguments;
    arguments.Add(GameValueExt(group));
    arguments.Add(GameValue(position));
    arguments.Add(GameValueExt(target));

    if (nameScript.GetLength() > 0)
    {
        GameState* gstate = GWorld->GetGameState();
        GameValue value = gstate->Evaluate(nameArgs);
        if (gstate->GetLastError() == EvalOK)
        {
            if (value.GetType() == GameArray)
            {
                GameArrayType& array = value;
                for (int i = 0; i < array.Size(); i++)
                {
                    arguments.Add(array[i]);
                }
            }
            else
            {
                arguments.Add(value);
            }
        }
    }

    Script* script = new Script(nameScript, GameValue(arguments));
    group->SetScript(script);
}

static void CheckArcadeScripted(AIGroupContext* context)
{
    STATE_PREFIX

    Script* script = group->GetScript();
    if (!script || script->OnSimulate())
    {
        group->SetScript(nullptr);
        context->_fsm->SetState(SArcadeSync, context);
    }
}

// Talk waypoint

static bool GetTalkTarget(AIGroupContext* context, Vector3& pos)
{
    STATE_PREFIX

    int& id = context->_fsm->Var(3);

    if (id >= 0 && id < vehiclesMap.Size())
    {
        EntityAI* veh = vehiclesMap[id];
        if (!veh)
        {
            return false;
        }
        const Target* target = group->FindTarget(veh);
        if (target)
        {
            pos = target->position;
            return true;
        }
        const AITargetInfo* info = group->GetCenter()->FindTargetInfo(veh);
        if (info)
        {
            pos = info->_realPos;
            return true;
        }
    }
    return false;
}

static void ArcadeTalkMove(AIGroupContext* context)
{
    STATE_PREFIX
    if (group->IsPlayerGroup())
    {
        return;
    }

    Vector3 posN;
    if (!GetTalkTarget(context, posN))
    {
        context->_fsm->SetState(SArcadeSync, context);
        return;
    }

    mission->_destination = posN;

    Command cmd;
    cmd._message = Command::Move;
    cmd._destination = posN;
    cmd._discretion = Command::Undefined;
    cmd._context = Command::CtxMission;
    group->SendCommand(cmd);
}

static void CheckArcadeTalkMove(AIGroupContext* context)
{
    STATE_PREFIX
    if (group->IsPlayerGroup())
    {
        context->_fsm->SetState(SArcadeTalkWalk, context);
        return;
    }

    Vector3 posN;
    if (!GetTalkTarget(context, posN))
    {
        context->_fsm->SetState(SArcadeSync, context);
        return;
    }

    if (!group->Leader())
    {
        return;
    }
    Vector3Val posL = group->Leader()->Position();
    Vector3Val posD = mission->_destination;

    float dist2LN = posL.Distance2(posN);
    if (dist2LN <= Square(25))
    {
        context->_fsm->SetState(SArcadeTalkGetOut, context);
        return;
    }
    float dist2DN = posD.Distance2(posN);
    if (dist2DN > 0.01 * dist2LN || group->GetAllDone())
    {
        mission->_destination = posN;

        Command cmd;
        cmd._message = Command::Move;
        cmd._destination = posN;
        cmd._discretion = Command::Undefined;
        cmd._context = Command::CtxMission;
        group->SendCommand(cmd);
        return;
    }
}

static void ArcadeTalkGetOut(AIGroupContext* context)
{
    STATE_PREFIX
    if (group->IsPlayerGroup())
    {
        return;
    }

    AllowGetIn(group, false);
}

static void CheckArcadeTalkGetOut(AIGroupContext* context)
{
    STATE_PREFIX
    if (group->IsPlayerGroup())
    {
        context->_fsm->SetState(SArcadeTalkWalk, context);
        return;
    }

    if (!group->Leader())
    {
        return;
    }
    if (group->Leader()->IsFreeSoldier())
    {
        context->_fsm->SetState(SArcadeTalkWalk, context);
    }
}

static void ArcadeTalkWalk(AIGroupContext* context)
{
    STATE_PREFIX
    if (group->IsPlayerGroup())
    {
        return;
    }

    int& id = context->_fsm->Var(3);
    if (id < 0 || id >= vehiclesMap.Size())
    {
        context->_fsm->SetState(SArcadeSync, context);
        return;
    }
    Object* veh = vehiclesMap[id];
    if (!veh)
    {
        context->_fsm->SetState(SArcadeSync, context);
        return;
    }

    // vehicle is near (25 m), can work directly with veh
    Vector3 pos = veh->Position() + 3.0 * veh->Direction();
    mission->_destination = pos;

    Command cmd;
    cmd._message = Command::Move;
    cmd._destination = pos;
    cmd._discretion = Command::Undefined;
    cmd._context = Command::CtxMission;
    group->SendCommand(cmd);
}

static void CheckArcadeTalkWalk(AIGroupContext* context)
{
    STATE_PREFIX
    if (group->IsPlayerGroup())
    {
        if (group->GetAllDone())
        {
            context->_fsm->SetState(SArcadeSync, context);
            return;
        }
    }
    else
    {
        int& id = context->_fsm->Var(3);
        if (id < 0 || id >= vehiclesMap.Size())
        {
            context->_fsm->SetState(SArcadeSync, context);
            return;
        }
        Object* veh = vehiclesMap[id];
        if (!veh)
        {
            context->_fsm->SetState(SArcadeSync, context);
            return;
        }

        if (!group->Leader())
        {
            return;
        }
        if (!group->Leader()->IsFreeSoldier())
        {
            return;
        }
        if (group->Leader()->Position().Distance2(veh->Position()) <= Square(5))
        {
            context->_fsm->SetState(SArcadeSync, context);
            return;
        }
    }
}

// Destroy waypoint

static TargetType* GetDestroyTarget(AIGroupContext* context)
{
    STATE_PREFIX

    int& id = context->_fsm->Var(3);
    int& idStatic = context->_fsm->Var(2);
    EntityAI* target = nullptr;

    if (id >= 0 && id < vehiclesMap.Size())
    {
        target = vehiclesMap[id];
    }
    else if (idStatic >= 0)
    {
        target = dyn_cast<TargetType>(GLOB_LAND->FindObject(idStatic));
    }
    else
    {
        target = dyn_cast<TargetType>(GLOB_LAND->NearestObject(mission->_destination, 100.0f, TypeVehicle));
    }
    return target;
}

static bool CheckArcadeDestroyMoveHelper(AIGroupContext* context)
{
    STATE_PREFIX

    AIUnit* leader = group->Leader();
    if (!leader)
    {
        return false;
    }

    Vector3Val posL = leader->Position();
    Vector3Val posD = mission->_destination;
    const float maxDist = 1000;
    const float minDist = 50;
    float dist2 = (posD - posL).SquareSizeXZ();
    if (dist2 <= Square(maxDist))
    {
        // check if target is visible
        TargetType* obj = GetDestroyTarget(context);
        if (!obj)
        {
            context->_fsm->SetState(SArcadeDestroyAttack, context);
            return true;
        }
        Target* tgt = group->FindTarget(obj);
        if (tgt && tgt->IsKnownBy(leader))
        {
            // target found - start attacking
            context->_fsm->SetState(SArcadeDestroyAttack, context);
            return true;
        }
        if (dist2 <= Square(minDist))
        {
            // target not found - start checking around
            context->_fsm->SetState(SArcadeDestroyBrown, context);
            return true;
        }
    }
    return false;
}

static void ArcadeDestroyMove(AIGroupContext* context)
{
    // Var(5) - counter for Brown movement
    int& counter = context->_fsm->Var(5);
    counter = 5;

    if (!CheckArcadeDestroyMoveHelper(context))
    {
        ArcadeMove(context);
    }
}

static void CheckArcadeDestroyMove(AIGroupContext* context)
{
    STATE_PREFIX

    if (group->IsPlayerGroup())
    {
        context->_fsm->SetState(SArcadeDestroyAttack, context);
        return;
    }
    else
    {
        if (group->GetAllDone())
        {
            context->_fsm->SetState(SArcadeDestroyBrown, context);
            return;
        }
        else
        {
            CheckArcadeDestroyMoveHelper(context);
        }
    }
}

static void SetBrownMove(AIGroupContext* context, float maxTime = 20.0f)
{
    STATE_PREFIX

    AIUnit* leader = group->Leader();

    const float maxSpeed = leader->GetVehicle()->GetType()->GetMaxSpeedMs();
    float maxDist = maxTime * maxSpeed;

    Vector3Val posL = mission->_destination;
    Vector3 posD = posL + Vector3(GRandGen.PlusMinus(0, maxDist), 0, GRandGen.PlusMinus(0, maxDist));
    posD[1] = GLandscape->RoadSurfaceY(posD[0], posD[2]);

    Command cmd;
    cmd._message = Command::Move;
    cmd._destination = posD;
    cmd._discretion = Command::Undefined;
    cmd._context = Command::CtxMission;
    group->SendCommand(cmd);
}

static void ArcadeDestroyBrown(AIGroupContext* context)
{
    STATE_PREFIX

    EntityAI* target = GetDestroyTarget(context);
    if (target->Static())
    {
        SetBrownMove(context, 0);
    }
    else
    {
        SetBrownMove(context);
    }
}

static void CheckArcadeDestroyBrown(AIGroupContext* context)
{
    STATE_PREFIX

    TargetType* target = GetDestroyTarget(context);
    if (!target || target->IsDammageDestroyed())
    {
        // target destroyed
        context->_fsm->SetState(SArcadeSync, context);
        return;
    }

    // check if target is visible
    AIUnit* leader = group->Leader();
    Target* tgt = group->FindTarget(target);
    if (tgt && tgt->IsKnownBy(leader))
    {
        // target seen recently - start attacking
        context->_fsm->SetState(SArcadeDestroyAttack, context);
        return;
    }

    if (group->GetAllDone())
    {
        int& counter = context->_fsm->Var(5);
        if (--counter <= 0)
        {
            context->_fsm->SetState(SArcadeSync, context);
            return;
        }
        SetBrownMove(context);
        return;
    }
}

static void ArcadeDestroyAttack(AIGroupContext* context)
{
    STATE_PREFIX

    TargetType* target = GetDestroyTarget(context);
    if (!target || target->IsDammageDestroyed())
    {
        context->_fsm->SetState(SArcadeSync, context);
        return;
    }

    mission->_target = target;
    if (!group->IsPlayerGroup())
    {
        AIUnit* leader = group->Leader();
        Target* tgt = group->FindTarget(target);
        if (!tgt || !tgt->IsKnownBy(leader))
        {
            // target lost - return to brown phase
            context->_fsm->SetState(SArcadeDestroyBrown, context);
            mission->_destination = tgt->AimingPosition();

            return;
        }
        // send target
        // consider: let mission designer set Engage / Fire status
        PackedBoolArray list;
        // set as target to all units of the group
        AISubgroup* subgrp = group->MainSubgroup();
        for (int i = 0; i < subgrp->NUnits(); i++)
        {
            AIUnit* unit = subgrp->GetUnit(i);
            list.Set(unit->ID() - 1, true);
        }
        group->SendTarget(tgt, true, true, list);
    }
}

static void CheckArcadeDestroyAttack(AIGroupContext* context)
{
    STATE_PREFIX

    TargetType* target = GetDestroyTarget(context);
    if (!target || target->IsDammageDestroyed())
    {
        context->_fsm->SetState(SArcadeSync, context);
        return;
    }

    if (!group->IsPlayerGroup())
    {
        Target* tgt = group->FindTarget(target);
        if (!tgt)
        {
            // target lost - seek for it
            context->_fsm->SetState(SArcadeDestroyBrown, context);
            return;
        }
        else
        {
            // target seen - update position
            mission->_destination = tgt->AimingPosition();
        }
    }
}

// Get In waypoint

static bool IsTargetVehicle(AIGroup* group, int index)
{
    ArcadeWaypointInfo& wInfo = group->GetWaypoint(index);
    if (wInfo.id >= 0 && wInfo.id < vehiclesMap.Size())
    {
        return true;
    }
    for (int j = 0; j < wInfo.synchronizations.Size(); j++)
    {
        int sync = wInfo.synchronizations[j];
        AI_ERROR(sync >= 0);
        if (synchronized[sync].groups.Size() == 2)
        {
            return true;
        }
    }
    return false;
}

static EntityAI* FindTargetVehicle(AIGroup* group, int index)
{
    ArcadeWaypointInfo& wInfo = group->GetWaypoint(index);

    if (wInfo.id >= 0 && wInfo.id < vehiclesMap.Size())
    {
        return dyn_cast<EntityAI>(vehiclesMap[wInfo.id].GetLink());
    }

    AIGroup* grpInto = nullptr;
    for (int j = 0; j < wInfo.synchronizations.Size(); j++)
    {
        int sync = wInfo.synchronizations[j];
        AI_ERROR(sync >= 0);
        if (synchronized[sync].groups.Size() == 2)
        {
            if (synchronized[sync].groups[0].group != group)
            {
                grpInto = synchronized[sync].groups[0].group;
            }
            else
            {
                grpInto = synchronized[sync].groups[1].group;
                AI_ERROR(grpInto != group);
            }
            break;
        }
    }

    if (grpInto)
    {
        for (int i = 0; i < grpInto->NVehicles(); i++)
        {
            Transport* veh = grpInto->GetVehicle(i);
            if (veh && veh->GetFreeManCargo() > 0)
            {
                return veh;
            }
        }
    }
    return nullptr;
}

static void CheckArcadeGetInMove(AIGroupContext* context)
{
    STATE_PREFIX

    int& index = context->_fsm->Var(0);
    if (!IsTargetVehicle(group, index))
    {
        context->_fsm->SetState(SArcadeGetInSync, context);
        return;
    }

    {
        AIUnit* leader = group->Leader();
        if (leader)
        {
            Vector3Val posL = leader->Position();
            Vector3Val posD = mission->_destination;

            // issue get-in when you are near the vehicle
            float prec = 100;
            if ((posD - posL).SquareSizeXZ() <= Square(prec))
            {
                int& index = context->_fsm->Var(0);
                EntityAI* veh = FindTargetVehicle(group, index);
                if (veh && veh->Position().Distance2(posL) < prec)
                {
                    context->_fsm->SetState(SArcadeGetInSync, context);
                    return;
                }
            }
        }
    }

    CheckArcadeMove(context, SArcadeGetInSync);
}

static void ArcadeGetInSync(AIGroupContext* context)
{
    AIGroup* group = context->_group;
    AI_ERROR(group);

    int& index = context->_fsm->Var(0);
    ArcadeWaypointInfo& wInfo = group->GetWaypoint(index);

    if (IsSyncActive(group, wInfo))
    {
        int& waiting = context->_fsm->Var(4);
        waiting = TRUE;

        if (!group->IsPlayerGroup())
        {
            group->Wait(group->MainSubgroup(),    // who
                        Glob.time + SYNC_TIMEOUT, // timeout
                        Command::Undefined        // how
            );
        }
    }
}

static void CheckArcadeGetInSync(AIGroupContext* context)
{
    CheckArcadeSync(context, SArcadeGetInGetIn);
}

static void ArcadeGetInGetIn(AIGroupContext* context)
{
    STATE_PREFIX
    int& index = context->_fsm->Var(0);

    Transport* veh = dyn_cast<Transport>(FindTargetVehicle(group, index));
    mission->_target = veh;

    AIUnit* leader = group->Leader();
    AI_ERROR(leader);
    if (veh)
    {
        if (group->IsPlayerGroup())
        {
            veh->WaitForGetIn(leader);
        }
        else if (veh->GetDriverAssigned())
        {
            // assign all members to cargo
            if (veh->GetDriverAssigned() != leader)
            {
                if (leader->AssignAsCargo(veh))
                {
                    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
                    {
                        AIUnit* unit = group->UnitWithID(i + 1);
                        if (!unit || unit == leader)
                        {
                            continue;
                        }
                        if (unit->VehicleAssigned() == veh)
                        {
                            continue;
                        }
                        if (!unit->AssignAsCargo(veh))
                        {
                            break;
                        }
                    }
                }
                else
                {
                    // nothing to do
                }
            }
        }
        else
        {
            if (!veh->GetGroupAssigned() && group->NVehicles() <= 0 && !leader->VehicleAssigned())
            {
                // specific case - 1st vehicle of the group
                // force leader to that vehicle, but not to any specific position?
                group->AddVehicle(veh);
            }
            else
            {
                AI_ERROR(!veh->QIsDriverIn());
                leader->AssignAsDriver(veh);
                group->AddVehicle(veh);
            }
        }
    }
    // assign anybody left to any free vehicles we have
    group->AssignVehicles();

    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* unit = group->UnitWithID(i + 1);
        if (unit)
        {
            unit->AllowGetIn(true);
            unit->OrderGetIn(true);
        }
    }
}

static void CheckArcadeGetInGetIn(AIGroupContext* context)
{
    STATE_PREFIX

    AIUnit* leader = group->Leader();
    if (!leader)
    {
        return;
    }

    int& index = context->_fsm->Var(0);
    ArcadeWaypointInfo& wInfo = group->GetWaypoint(index);

    EntityAI* veh = mission->_target;
    if (veh && veh->IsDammageDestroyed())
    {
        for (int j = 0; j < wInfo.synchronizations.Size(); j++)
        {
            int sync = wInfo.synchronizations[j];
            AI_ERROR(sync >= 0);
            synchronized[sync].SetActive(group, false);
            GetNetworkManager().GroupSynchronization(group, sync, false);
        }

        context->_fsm->SetState(SArcadeCountdown, context);
        return;
    }

    if (group->IsPlayerGroup())
    {
        // check if leader inside
        if (!veh)
        {
            veh = leader->VehicleAssigned();
        }
        if (veh && leader->GetVehicleIn() != veh)
        {
            return;
        }
    }
    else
    {
        // check if all vehicles
        for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
        {
            AIUnit* unit = group->UnitWithID(i + 1);
            if (!unit)
            {
                continue;
            }
            if (!unit->VehicleAssigned())
            {
                continue;
            }
            if (unit->VehicleAssigned() != unit->GetVehicleIn())
            {
                return; // wait
            }
        }
    }

    for (int j = 0; j < wInfo.synchronizations.Size(); j++)
    {
        int sync = wInfo.synchronizations[j];
        AI_ERROR(sync >= 0);
        synchronized[sync].SetActive(group, false);
        GetNetworkManager().GroupSynchronization(group, sync, false);
    }

    context->_fsm->SetState(SArcadeCountdown, context);
}

// Join waypoint

static AIGroup* FindTargetGroup(AIGroup* group, int index)
{
    ArcadeWaypointInfo& wInfo = group->GetWaypoint(index);

    if (wInfo.id >= 0 && wInfo.id < vehiclesMap.Size())
    {
        EntityAI* veh = dyn_cast<EntityAI>(vehiclesMap[wInfo.id].GetLink());
        AIUnit* unit = veh ? veh->CommanderUnit() : nullptr;
        return unit ? unit->GetGroup() : nullptr;
    }

    for (int j = 0; j < wInfo.synchronizations.Size(); j++)
    {
        int sync = wInfo.synchronizations[j];
        AI_ERROR(sync >= 0);
        if (synchronized[sync].groups.Size() == 2)
        {
            if (synchronized[sync].groups[0].group != group)
            {
                return synchronized[sync].groups[0].group;
            }
            else
            {
                AI_ERROR(synchronized[sync].groups[1].group != group);
                return synchronized[sync].groups[1].group;
            }
        }
    }

    return nullptr;
}

} // namespace Poseidon
void ProcessJoinGroups(AIGroup* from, AIGroup* to)
{
    using namespace Poseidon;
    // move assigned vehicles
    for (int i = 0; i < from->NVehicles(); i++)
    {
        Transport* veh = from->GetVehicle(i);
        if (veh)
        {
            to->AddVehicle(veh);
        }
    }

    // join
    AUTO_STATIC_ARRAY(AIUnit*, joined, 32)
    bool sendJoin = false;
    if (to->Leader() && from->Leader())
    {
        sendJoin = to->Leader() != GWorld->FocusOn() ||
                   to->Leader()->Position().Distance2(from->Leader()->Position()) < Square(200);
    }
    Person* player = GWorld->GetRealPlayer();
    AIUnit* playerUnit = player ? player->Brain() : nullptr;
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        if (to->NUnits() >= MAX_UNITS_PER_GROUP)
        {
            break;
        }

        Ref<AIUnit> unit = from->UnitWithID(i + 1);
        if (!unit)
        {
            continue;
        }
        int id = -1;
        if (GWorld->GetMode() != GModeNetware && unit == playerUnit)
        {
            id = unit->ID();
        }
        unit->ForceRemoveFromGroup();
        to->AddUnit(unit, id);
        joined.Add(unit);

        if (!sendJoin)
        {
            AISubgroup* subgrp = new AISubgroup();
            to->AddSubgroup(subgrp);
            subgrp->AddUnit(unit);
            subgrp->SelectLeader(unit);
            if (GWorld->GetMode() == GModeNetware)
            {
                GetNetworkManager().CreateObject(subgrp);
                GetNetworkManager().UpdateObject(subgrp);
            }
        }
    }

    if (GWorld->GetMode() == GModeNetware)
    {
        GetNetworkManager().UpdateObject(to->MainSubgroup());
        GetNetworkManager().UpdateObject(to);
    }
    if (from->NUnits() == 0)
    {
        from->ForceRemoveFromCenter();
    }
    else
    {
        if (!from->Leader())
        {
            from->GetCenter()->SelectLeader(from);
        }
    }

    // update values for flee
    to->CalculateMaximalStrength();

    if (to->NUnits() == 0)
    {
        return;
    }

    // select leader
    AICenter* center = to->GetCenter();
    if (!to->Leader())
    {
        center->SelectLeader(to);
    }
    AI_ERROR(to->Leader());

    // special case - formation leader inside vehicle
    AIUnit* formLeader = to->MainSubgroup()->Leader();
    if (formLeader && !formLeader->IsUnit())
    {
        to->MainSubgroup()->SelectLeader();
    }

    // radio
    GWorld->SetActiveChannels();
    PackedBoolArray list;
    Command cmd;
    cmd._message = Command::Join;
    cmd._context = Command::CtxMission;
    for (int i = 0; i < joined.Size(); i++)
    {
        list.Set(joined[i]->ID() - 1, true);
    }
    to->GetRadio().Transmit(new RadioMessageJoin(to, list), to->GetCenter()->GetLanguage());
    for (int i = 0; i < joined.Size(); i++)
    {
        if (joined[i] != playerUnit && joined[i]->GetLifeState() == AIUnit::LSAlive)
        {
            to->GetRadio().Transmit(new RadioMessageJoinDone(joined[i], to), to->GetCenter()->GetLanguage());
        }
    }

    // allow get in by leader
    bool allow = to->Leader()->IsGetInAllowed();
    AllowGetIn(to, allow);

    PoseidonAssert(to->AssertValid());
}
namespace Poseidon
{

static void JoinGroups(AIGroup* from, AIGroup* to)
{
    AI_ERROR(from);
    AI_ERROR(to);
    AI_ERROR(from != to);
    PoseidonAssert(from->AssertValid());
    PoseidonAssert(to->AssertValid());

    ApplyEffects(from, from->GetCurrent()->_fsm->Var(0));

    if (to->IsLocal())
    {
        ProcessJoinGroups(from, to);
    }
    else
    {
        GetNetworkManager().AskForJoin(to, from);
    }
}

#include <Poseidon/AI/AIArcadeActions.inc>

static AIGroupFSM::StateInfo noMissionStates[] = {AIGroupFSM::StateInfo("Wait", NoMissionWait, CheckNoMissionWait)};

static AIGroupFSM::StateInfo arcadeStates[] = {
    AIGroupFSM::StateInfo("Init", ArcadeInit, CheckArcadeInit),
    AIGroupFSM::StateInfo("Turn", ArcadeTurn, CheckArcadeTurn),
    AIGroupFSM::StateInfo("Move Move", ArcadeMoveMove, CheckArcadeMoveMove),
    AIGroupFSM::StateInfo("Talk Move", ArcadeTalkMove, CheckArcadeTalkMove),
    AIGroupFSM::StateInfo("Talk GetOut", ArcadeTalkGetOut, CheckArcadeTalkGetOut),
    AIGroupFSM::StateInfo("Talk Walk", ArcadeTalkWalk, CheckArcadeTalkWalk),
    AIGroupFSM::StateInfo("Destroy Move", ArcadeDestroyMove, CheckArcadeDestroyMove),
    AIGroupFSM::StateInfo("Destroy Brown", ArcadeDestroyBrown, CheckArcadeDestroyBrown),
    AIGroupFSM::StateInfo("Destroy Attack", ArcadeDestroyAttack, CheckArcadeDestroyAttack),
    AIGroupFSM::StateInfo("GetIn Move", ArcadeMove, CheckArcadeGetInMove),
    AIGroupFSM::StateInfo("GetIn Sync", ArcadeGetInSync, CheckArcadeGetInSync),
    AIGroupFSM::StateInfo("GetIn GetIn", ArcadeGetInGetIn, CheckArcadeGetInGetIn),
    AIGroupFSM::StateInfo("SAD Move", ArcadeMove, CheckArcadeSeekAndDestroyMove),
    AIGroupFSM::StateInfo("SAD Check", ArcadeSeekAndDestroyCheck, CheckArcadeSeekAndDestroyCheck),
    AIGroupFSM::StateInfo("SAD Wait", ArcadeWait, CheckArcadeSeekAndDestroyWait),
    AIGroupFSM::StateInfo("SAD Overlook", ArcadeSeekAndDestroyOverlook, CheckArcadeSeekAndDestroyOverlook),
    AIGroupFSM::StateInfo("SAD Brown", ArcadeSeekAndDestroyBrown, CheckArcadeSeekAndDestroyBrown),
    AIGroupFSM::StateInfo("Join Move", ArcadeMove, CheckArcadeJoinMove),
    AIGroupFSM::StateInfo("Join Sync", ArcadeSync, CheckArcadeJoinSync),
    AIGroupFSM::StateInfo("Join Join", ArcadeJoinJoin, CheckArcadeJoinJoin),
    AIGroupFSM::StateInfo("Leader Move", ArcadeMove, CheckArcadeLeaderMove),
    AIGroupFSM::StateInfo("Leader Sync", ArcadeSync, CheckArcadeLeaderSync),
    AIGroupFSM::StateInfo("Leader Join", ArcadeLeaderJoin, CheckArcadeLeaderJoin),
    AIGroupFSM::StateInfo("GetOut Move", ArcadeMove, CheckArcadeGetOutMove),
    AIGroupFSM::StateInfo("GetOut GetOut", ArcadeGetOutGetOut, CheckArcadeGetOutGetOut),
    AIGroupFSM::StateInfo("Load Move", ArcadeMove, CheckArcadeLoadMove),
    AIGroupFSM::StateInfo("Load GetIn", ArcadeLoadGetIn, CheckArcadeLoadGetIn),
    AIGroupFSM::StateInfo("Unload Move", ArcadeMove, CheckArcadeUnloadMove),
    AIGroupFSM::StateInfo("Unload GetOut", ArcadeUnloadGetOut, CheckArcadeUnloadGetOut),
    AIGroupFSM::StateInfo("TransportUnload Move", ArcadeMove, CheckArcadeTransportUnloadMove),
    AIGroupFSM::StateInfo("TransportUnload GetOut", ArcadeTransportUnloadGetOut, CheckArcadeTransportUnloadGetOut),
    AIGroupFSM::StateInfo("Hold Move", ArcadeMove, CheckArcadeHoldMove),
    AIGroupFSM::StateInfo("Hold Wait", ArcadeWait, CheckArcadeHoldWait),
    AIGroupFSM::StateInfo("Hold Overlook", ArcadeHoldOverlook, CheckArcadeHoldOverlook),
    AIGroupFSM::StateInfo("Sentry Move", ArcadeMove, CheckArcadeSentryMove),
    AIGroupFSM::StateInfo("Sentry Wait", ArcadeWait, CheckArcadeSentryWait),
    AIGroupFSM::StateInfo("Sentry Overlook", ArcadeSentryOverlook, CheckArcadeSentryOverlook),
    AIGroupFSM::StateInfo("Sentry Brown", ArcadeSentryBrown, CheckArcadeSentryBrown),
    AIGroupFSM::StateInfo("Guard Move", ArcadeMove, CheckArcadeGuardMove),
    AIGroupFSM::StateInfo("Guard Wait", ArcadeWait, CheckArcadeGuardWait),
    AIGroupFSM::StateInfo("Guard Attack", ArcadeGuardAttack, CheckArcadeGuardAttack),
    AIGroupFSM::StateInfo("Guard Overlook", ArcadeGuardOverlook, CheckArcadeGuardOverlook),
    AIGroupFSM::StateInfo("Guard Brown", ArcadeGuardBrown, CheckArcadeGuardBrown),
    AIGroupFSM::StateInfo("Guard BrownTarget", ArcadeGuardBrownTarget, CheckArcadeGuardBrownTarget),
    AIGroupFSM::StateInfo("Gravon Wait", ArcadeWait, CheckArcadeGravonWait),
    AIGroupFSM::StateInfo("Gravon Attack", ArcadeGravonAttack, CheckArcadeGravonAttack),
    AIGroupFSM::StateInfo("Gravon Overlook", ArcadeGravonOverlook, CheckArcadeGravonOverlook),
    AIGroupFSM::StateInfo("Gravon Move", ArcadeGravonMove, CheckArcadeGravonMove),
    AIGroupFSM::StateInfo("Support Move", ArcadeMove, CheckArcadeSupportMove),
    AIGroupFSM::StateInfo("Support Wait", ArcadeWait, CheckArcadeSupportWait),
    AIGroupFSM::StateInfo("Support Transport", ArcadeSupportTransport, CheckArcadeSupportTransport),
    AIGroupFSM::StateInfo("Support Supply", ArcadeSupportSupply, CheckArcadeSupportSupply),
    AIGroupFSM::StateInfo("Scripted", ArcadeScripted, CheckArcadeScripted),
    AIGroupFSM::StateInfo("Logic", ArcadeLogic, CheckArcadeLogic),
    AIGroupFSM::StateInfo("Sync", ArcadeSync, CheckArcadeSync),
    AIGroupFSM::StateInfo("Countdown", ArcadeCountdown, CheckArcadeCountdown),
    AIGroupFSM::StateInfo("Next", ArcadeNext, CheckArcadeNext),
    AIGroupFSM::StateInfo("Unlock", ArcadeUnlock, CheckArcadeUnlock),

    AIGroupFSM::StateInfo("Flee", ArcadeFlee, CheckArcadeFlee),

    AIGroupFSM::StateInfo("Succeed", MissionSucceed, CheckMissionSucceed),
    AIGroupFSM::StateInfo("Failed", MissionFailed, CheckMissionFailed)};

template <>
FSM* AbstractAIMachine<Mission, AIGroupContext>::CreateFSM(int taskType)
{
    switch (taskType)
    {
        case Mission::NoMission:
            return new AIGroupFSM(noMissionStates, sizeof(noMissionStates) / sizeof(*noMissionStates));
        case Mission::Arcade:
            return new AIGroupFSM(arcadeStates, sizeof(arcadeStates) / sizeof(*arcadeStates));
    }
    Fail("Unknown mission type");
    return nullptr;
}

} // namespace Poseidon
