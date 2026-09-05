#include <Poseidon/Core/Application.hpp>
#include <math.h>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/BoolArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Memory/FastAlloc.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

#pragma warning(disable : 4100 4511 4512 4201 4127)

#include <Poseidon/AI/AI.hpp>
#include <Poseidon/AI/AIRadio.hpp>
#include <Poseidon/Core/FSM/Fsm.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/World/Entities/Infantry/Person.hpp>
#include <Poseidon/World/Entities/Vehicles/House.hpp>

#include <Poseidon/World/Entities/Weapons/Shots.hpp>
#include <Poseidon/World/Entities/Weapons/Weapons.hpp>
#include <Poseidon/Game/UiActions.hpp>

#include <Poseidon/Graphics/Core/Engine.hpp>

namespace Poseidon
{
using namespace Foundation;

DEFINE_FAST_ALLOCATOR(AISubgroupFSM)

AISubgroupFSM::AISubgroupFSM(const StateInfo* states, int n, const pStateFunction* functions, int nFunc)
    : base(states, n, functions, nFunc)
{
}

AISubgroupFSM::~AISubgroupFSM() {}

#define LOST_UNIT_MIN 45.0F
#define LOST_UNIT_MAX 300.0F

#define SUBGROUP_REFRESH 20.0F

// Generic Command Functions

#define LOG_FSM 0

#define DIAG_ATTACK 0

inline void CheckDistance(AIUnit* unit, Vector3Val dest) {}

inline void LogFSM(AISubgroup* subgrp, const char* cmd, const char* state)
{
#if LOG_FSM
    if (subgrp && subgrp->GetGroup())
        LOG_DEBUG(AI, "FSM: {} {} - state {}, {} tasks", (const char*)subgrp->GetDebugName(), cmd, state,
                  subgrp->StackSize());
#endif
}

#define INIT_PREFIX                          \
    AISubgroup* subgrp = context->_subgroup; \
    AI_ERROR(subgrp);                        \
    (void)subgrp;

// A wiped subgroup (every unit dead, so Leader() is null) is an expected
// transient state under mass casualties, not an error — the guard below bails
// cleanly. Don't AI_ERROR it: that floods every FSM state and, under --strict,
// shuts the game down.
#define STATE_PREFIX                         \
    AISubgroup* subgrp = context->_subgroup; \
    AI_ERROR(subgrp);                        \
    Command* cmd = context->_task;           \
    AI_ERROR(cmd);                           \
    (void)cmd;                               \
    if (subgrp->NUnits() <= 0)               \
        return;                              \
    if (!subgrp->Leader())                   \
        return;

void CommandSucceed(AISubgroupContext* context)
{
    AISubgroup* subgroup = context->_subgroup;
    AI_ERROR(subgroup);
    Command* cmd = context->_task;
    AI_ERROR(cmd);
    LogFSM(subgroup, "Command", "SUCCEED");

    subgroup->DoNotGo();
    subgroup->ClearPlan();

    AIGroup* grp = subgroup->GetGroup();
    if (subgroup->IsAnyPlayerSubgroup())
    {
        grp->ReceiveAnswer(subgroup, AI::CommandCompleted);
    }
    else
    {
        subgroup->SendAnswer(AI::CommandCompleted);
    }
    if (grp->IsPlayerGroup())
    {
        SetCommandState(cmd->_id, CSSucceed, subgroup);
    }

    if (cmd->_joinToSubgroup && cmd->_joinToSubgroup != subgroup && subgroup != grp->MainSubgroup())
    {
        subgroup->JoinToSubgroup(cmd->_joinToSubgroup);
    }
}

void CheckCommandSucceed(AISubgroupContext* context)
{
    context->_fsm->SetState(FSM::FinalState, context);
}

void CommandFailed(AISubgroupContext* context)
{
    AISubgroup* subgroup = context->_subgroup;
    AI_ERROR(subgroup);
    Command* cmd = context->_task;
    AI_ERROR(cmd);
    LogFSM(subgroup, "Command", "FAILED");

    subgroup->DoNotGo();
    subgroup->ClearPlan();
    AIGroup* grp = subgroup->GetGroup();
    if (subgroup->IsAnyPlayerSubgroup())
    {
        grp->ReceiveAnswer(subgroup, AI::CommandFailed);
    }
    else
    {
        subgroup->SendAnswer(AI::CommandFailed);
    }
    if (cmd->_id >= 0)
    {
        SetCommandState(cmd->_id, CSFailed, subgroup);
    }

    if (cmd->_joinToSubgroup && cmd->_joinToSubgroup != subgroup && subgroup != grp->MainSubgroup())
    {
        subgroup->JoinToSubgroup(cmd->_joinToSubgroup);
    }
}

void CheckCommandFailed(AISubgroupContext* context)
{
    context->_fsm->SetState(FSM::FinalState, context);
}

// No Command FSM

static void NoCommandWait(AISubgroupContext* context)
{
    if (!context)
    {
        return;
    }

    AISubgroup* subgrp = context->_subgroup;
    if (!subgrp)
    {
        return;
    }

    LogFSM(subgrp, "NoCommand", "WAIT");
}

static void CheckNoCommandWait(AISubgroupContext* context) {}

static AISubgroupFSM::StateInfo noCommandStates[] = {
    AISubgroupFSM::StateInfo("Wait", NoCommandWait, CheckNoCommandWait)};

// Command Wait FSM

enum
{
    SWaitInit,
    SWaitWait,
    SWaitDone,
    SWaitFail
};

static void WaitInit(AISubgroupContext* context)
{
    INIT_PREFIX
    if (!subgrp->HasAI())
    {
        return;
    }

    LogFSM(subgrp, "Wait", "INIT");

    subgrp->SetRefreshTime(TIME_MAX);
    subgrp->DoNotGo();
}

static void CheckWaitInit(AISubgroupContext* context)
{
    AI_ERROR(context->_subgroup);

    context->_fsm->SetState(SWaitWait, context);
}

static void WaitWait(AISubgroupContext* context)
{
    STATE_PREFIX
    if (!subgrp->HasAI())
    {
        return;
    }

    LogFSM(subgrp, "Wait", "WAIT");

    subgrp->SetRefreshTime(TIME_MAX);
    subgrp->DoNotGo();
}

static void CheckWaitWait(AISubgroupContext* context)
{
    STATE_PREFIX

    if (cmd->_time >= Time(0) && Glob.time > cmd->_time)
    {
        context->_fsm->SetState(SWaitDone, context);
    }
}

static AISubgroupFSM::StateInfo waitStates[] = {
    AISubgroupFSM::StateInfo("Init", WaitInit, CheckWaitInit),
    AISubgroupFSM::StateInfo("Wait", WaitWait, CheckWaitWait),
    AISubgroupFSM::StateInfo("Succeed", CommandSucceed, CheckCommandSucceed),
    AISubgroupFSM::StateInfo("Failed", CommandFailed, CheckCommandFailed)};

// Command Attack FSM

enum
{
    SAttackInit,
    SAttackFarMove,
    SAttackMove,
    SAttackAttack,
    SAttackWait,
    SAttackRunAway,
    SAttackDone,
    SAttackFail
};

// use always _targetE

static Target* GetAttackTarget(const Command* cmd, AIGroup* group)
{
    Target* tgt = cmd->_targetE;
    if (!tgt)
    {
        tgt = group->FindTarget(cmd->_target);
    }
    if (!tgt)
    {
        return tgt;
    }
    if (tgt->destroyed)
    {
        return nullptr;
    }
    return tgt;
}

static bool IsAttackTarget(const Command* cmd, AIGroup* group)
{
    Target* tgt = cmd->_targetE;
    if (tgt)
    {
        return !tgt->destroyed;
    }
    return cmd->_target != nullptr;
}

static bool AttackReplan(AISubgroupContext* context)
{
    AISubgroup* subgrp = context->_subgroup;
    AI_ERROR(subgrp);
    Command* cmd = context->_task;
    AI_ERROR(cmd);

    EntityAI* lVehicle = subgrp->Leader()->GetVehicle();
    Target* tgt = GetAttackTarget(cmd, subgrp->GetGroup());

    // call BeginAttack to be sure vehicle is engaged
    if (tgt)
    {
        lVehicle->BegAttack(tgt);
    }

    Vector3 attackPos;
    FireResult result;

#if DIAG_ATTACK
    LOG_DEBUG(AI, "AttackThink");
#endif
    if (lVehicle->AttackThink(result, attackPos))
    {
        if (attackPos.Distance2(cmd->_destination) > Square(5))
        {
            if (attackPos.SquareSize() < 1)
            {
                Fail("Bad attack pos");
            }
            // Refreshing when attack position changed
            subgrp->GoPlanned(attackPos);
        }
        return true;
    }
    return false;
}

static bool AttackReplanIfLost(AISubgroupContext* context)
{
    AISubgroup* subgrp = context->_subgroup;
    AI_ERROR(subgrp);
    Command* cmd = context->_task;
    AI_ERROR(cmd);

    Target* tgt = GetAttackTarget(cmd, subgrp->GetGroup());
    if (!tgt->IsKnown())
    {
        Vector3 attackPos = tgt->AimingPosition();
        if (attackPos.Distance2(cmd->_destination) > Square(5))
        {
            if (attackPos.SquareSize() < 1)
            {
                Fail("Bad attack pos");
            }
            // Refreshing when attack position changed
            subgrp->GoPlanned(attackPos);
        }
        return true;
    }
    return false;
}

static bool CanAttack(AISubgroup* subgrp, Target& tar)
{
    for (int u = 0; u < subgrp->NUnits(); u++)
    {
        AIUnit* unit = subgrp->GetUnit(u);
        if (!unit)
        {
            continue;
        }
        EntityAI* veh;
        if (unit->IsInCargo())
        {
            veh = unit->GetPerson();
        }
        else if (unit->IsUnit())
        {
            veh = unit->GetVehicle();
        }
        else
        {
            continue;
        }

        FireResult result;
        float bestDist, minRange, maxRange;
        bool potential = veh->BestFireResult(result, tar, bestDist, minRange, maxRange, 30, true);
        if (potential)
        {
#if DIAG_ATTACK
            LOG_DEBUG(AI, "CanAttack true");
#endif
            return true;
        }
    }
#if DIAG_ATTACK
    LOG_DEBUG(AI, "CanAttack false");
#endif
    return false;
}

static void TryAttack(bool& somePossible, bool& somePotential, AISubgroup* subgrp, Target& tar)
{
    somePossible = somePotential = false;
    AIUnit* leader = subgrp->Leader();
    EntityAI* lVehicle = leader->GetVehicle();
    Shot* shot = dyn_cast<Shot>(lVehicle->GetLastShot());
    if (shot)
    {
        // if we have already placed a bomb, wait until it explodes
        switch (shot->Type()->_simulation)
        {
            case AmmoShotPipeBomb:
            case AmmoShotTimeBomb:
                // no fire possible until bomb exploded
                return;
        }
    }
    for (int u = 0; u < subgrp->NUnits(); u++)
    {
        AIUnit* unit = subgrp->GetUnit(u);
        if (!unit)
        {
            continue;
        }
        FireResult result;
        if (unit->IsUnit())
        {
            EntityAI* veh = unit->GetVehicle();
            bool possible = veh->WhatFireResult(result, tar, 30.0);
            if (possible)
            {
                somePossible = true;
                somePotential = true;
            }
            else
            {
                float bestDist, minRange, maxRange;
                if (veh->BestFireResult(result, tar, bestDist, minRange, maxRange, 30, true))
                {
                    somePotential = true;
                }
            }
        }
        else if (unit->IsInCargo())
        {
            // unit may be cargo
            float bestDist, minRange, maxRange;
            EntityAI* veh = unit->GetPerson();
            if (veh->BestFireResult(result, tar, bestDist, minRange, maxRange, 30, true))
            {
                somePotential = true;
            }
        }
    }
#if DIAG_ATTACK
    LOG_DEBUG(AI, "TryAttack {} {}", somePossible, somePotential);
#endif
}

static void AttackInit(AISubgroupContext* context)
{
    STATE_PREFIX
    if (!subgrp->HasAI())
    {
        return;
    }

    AIUnit* leader = subgrp->Leader();
    cmd->_destination = leader->Position();

    LogFSM(subgrp, "Attack", "INIT");

    subgrp->SetRefreshTime(Glob.time + SUBGROUP_REFRESH);
    subgrp->DoNotGo();
    if (subgrp != subgrp->GetGroup()->MainSubgroup())
    {
        subgrp->SetFormation(AI::FormLine);
    }
}

static void CheckAttackInit(AISubgroupContext* context)
{
    STATE_PREFIX

    if (!IsAttackTarget(cmd, subgrp->GetGroup()))
    {
        subgrp->Leader()->GetVehicle()->EndAttack();
        context->_fsm->SetState(SAttackDone, context); // Success
        subgrp->ClearPlan();
        return;
    }
    Target* tgt = GetAttackTarget(cmd, subgrp->GetGroup());
    if (tgt && !CanAttack(subgrp, *tgt))
    {
        subgrp->Leader()->GetVehicle()->EndAttack();
        context->_fsm->SetState(SAttackWait, context);
        return;
    }
    if (!subgrp->HasAI())
    {
        context->_fsm->SetState(SAttackAttack, context);
        subgrp->ClearPlan();
        return;
    }
    else
    {
        if (tgt)
        {
            subgrp->Leader()->GetVehicle()->BegAttack(tgt);
#if DIAG_ATTACK
            LOG_DEBUG(AI, "Engage - attack command.");
#endif
        }
        else
        {
            const AITargetInfo* info = nullptr;
            AIGroup* grp = subgrp->GetGroup();
            AICenter* center = grp ? grp->GetCenter() : nullptr;
            if (center)
            {
                info = center->FindTargetInfo(cmd->_target);
            }
            if (info)
            {
                cmd->_destination = info->_realPos;
            }
            else
            {
                subgrp->Leader()->GetVehicle()->EndAttack();
                context->_fsm->SetState(SAttackFail, context); // target lost
                subgrp->ClearPlan();
                return;
            }
        }

        context->_fsm->SetState(SAttackFarMove, context);
        subgrp->ClearPlan();
        return;
    }
}

static void AttackFarMove(AISubgroupContext* context)
{
    STATE_PREFIX
    if (!subgrp->HasAI())
    {
        return;
    }

    LogFSM(subgrp, "Attack", "FARMOVE");

    subgrp->SetRefreshTime(Glob.time + 180); // no refresh

    subgrp->GoPlanned(cmd->_destination);
    context->_fsm->SetTimeOut(30);

    Vector3Val posL = subgrp->Leader()->Position();
    Vector3Val posD = cmd->_destination;
    float dist = 300;
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
}

static void CheckAttackFarMove(AISubgroupContext* context)
{
    STATE_PREFIX

    if (!IsAttackTarget(cmd, subgrp->GetGroup()))
    {
        subgrp->Leader()->GetVehicle()->EndAttack();
        context->_fsm->SetState(SAttackDone, context); // Success
        subgrp->ClearPlan();
        return;
    }
    Target* tgt = GetAttackTarget(cmd, subgrp->GetGroup());
    if (tgt && !CanAttack(subgrp, *tgt))
    {
        subgrp->Leader()->GetVehicle()->EndAttack();
        context->_fsm->SetState(SAttackWait, context);
        return;
    }
    if (!subgrp->HasAI())
    {
        context->_fsm->SetState(SAttackAttack, context); // Attack
        return;
    }

    // refreshing when target position changed
    if (!AttackReplan(context))
    {
        // try to find some attack position
        // timeout when no attack possible
        if (context->_fsm->TimedOut())
        {
#if DIAG_ATTACK
            LOG_DEBUG(AI, "Far Move Replan Failed.");
#endif
            context->_fsm->SetState(SAttackWait, context);
        }
    }
    EntityAI* lai = subgrp->Leader()->GetVehicle();
    Vector3Val posL = lai->Position();
    Vector3Val posD = cmd->_destination;
    float dist = 200;
    // Precision is FormationX()*0.25 by default
    // for tank it is 5 m
    saturateMax(dist, lai->GetPrecision() * 10);
    saturateMin(dist, 1000);

#if LOG_FSM
    if (subgrp && subgrp->GetGroup())
        LOG_DEBUG(AI, "FSM: {} {} - state {}, dist {:.1f} posL {:.1f},{:.1f},{:.1f} posD {:.1f},{:.1f},{:.1f}",
                  (const char*)subgrp->GetDebugName(), "ATTACK", "MOVE", (posL - posD).SizeXZ(), posL[0], posL[1],
                  posL[2], posD[0], posD[1], posD[2]);
#endif

    if ((posL - posD).SquareSizeXZ() <= Square(dist))
    {
        context->_fsm->SetState(SAttackMove, context);
        return;
    }

    if (subgrp->HasAI() && subgrp->AllUnitsCompleted())
    {
        CheckDistance(subgrp->Leader(), cmd->_destination);

        LogFSM(subgrp, "Attack", "far move refresh");
        context->_fsm->Refresh(context);
        return;
    }
}

static void AttackMove(AISubgroupContext* context)
{
    STATE_PREFIX
    if (!subgrp->HasAI())
    {
        return;
    }

    LogFSM(subgrp, "Attack", "MOVE");

    // near attack position - get out from cargo
    AIGroup* grp = subgrp->GetGroup();

    PackedBoolArray list;
    for (int u = 0; u < subgrp->NUnits(); u++)
    {
        AIUnit* unit = subgrp->GetUnit(u);
        if (!unit)
        {
            continue;
        }
        if (!unit->IsInCargo())
        {
            continue;
        }
        Transport* veh = unit->GetVehicleIn();
        if (!veh)
        {
            continue;
        }
        AIUnit* driver = veh->DriverBrain();
        if (!driver || !driver->IsPlayer())
        {
            Target* tgt = GetAttackTarget(cmd, grp);
            if (tgt)
            {
                unit->AssignTarget(tgt);
            }
            unit->OrderGetIn(false);
        }
    }

    subgrp->SetRefreshTime(TIME_MAX);
    subgrp->GoPlanned(cmd->_destination);
    context->_fsm->SetTimeOut(30);
}

// how long should we wait when we have no ammo

const float PosTimeOut = 5;

static void CheckAttackMove(AISubgroupContext* context)
{
    STATE_PREFIX

    AIUnit* leader = subgrp->Leader();
    if (!IsAttackTarget(cmd, subgrp->GetGroup()))
    {
        leader->GetVehicle()->EndAttack();
        context->_fsm->SetState(SAttackDone, context); // Success
        subgrp->ClearPlan();
        return;
    }
    if (!subgrp->HasAI())
    {
        context->_fsm->SetState(SAttackAttack, context); // Attack
        subgrp->ClearPlan();
        return;
    }

#if DIAG_ATTACK
    LOG_DEBUG(AI, "FindTarget");
#endif
    Target* tar = GetAttackTarget(cmd, subgrp->GetGroup());
    if (tar)
    {
#if DIAG_ATTACK
        LOG_DEBUG(AI, "FindTarget OK");
#endif
        bool somePossible, somePotential;
        TryAttack(somePossible, somePotential, subgrp, *tar);
        if (!somePotential)
        {
            context->_fsm->SetState(SAttackWait, context);
            return;
        }
    }

    // update attack position if neccessary
    if (!AttackReplan(context))
    {
        // try to find some attack position
        // timeout when no attack possible
        if (context->_fsm->TimedOut())
        {
#if DIAG_ATTACK
            LOG_DEBUG(AI, "Move Replan Failed.");
#endif
            context->_fsm->SetState(SAttackWait, context);
        }
        return;
    }

    EntityAI* lai = subgrp->Leader()->GetVehicle();
    Vector3Val posL = lai->Position();
    Vector3Val posD = cmd->_destination;
    if ((posL - posD).SquareSizeXZ() <= Square(lai->GetPrecision() * 2))
    {
        if (tar && tar->IsKnown())
        {
            // check if we have something planned
            EntityAI* lVehicle = subgrp->Leader()->GetVehicle();
            if (lVehicle->AttackReady())
            {
// target seen recently  - and path is planned
#if DIAG_ATTACK
                LOG_DEBUG(AI, "Target seen - Attack");
#endif
                context->_fsm->SetState(SAttackAttack, context);
            }
        }
        else
        {
            // target not seen, we are in position - fail
            context->_fsm->SetState(SAttackWait, context);
        }
        return;
    }

#if LOG_FSM
    if (subgrp && subgrp->GetGroup())
        LOG_DEBUG(AI, "FSM: {} {} - state {}, dist {:.1f} posL {:.1f},{:.1f},{:.1f} posD {:.1f},{:.1f},{:.1f}",
                  (const char*)subgrp->GetDebugName(), "ATTACK", "MOVE", (posL - posD).SizeXZ(), posL[0], posL[1],
                  posL[2], posD[0], posD[1], posD[2]);
#endif
    if (subgrp->HasAI() && subgrp->AllUnitsCompleted())
    {
        CheckDistance(subgrp->Leader(), cmd->_destination);

        LogFSM(subgrp, "Attack", "move refresh");
        context->_fsm->Refresh(context);
        return;
    }
}

static void AttackAttack(AISubgroupContext* context)
{
    STATE_PREFIX
    if (!subgrp->HasAI())
    {
        return;
    }

    LogFSM(subgrp, "Attack", "ATTACK");

    subgrp->SetRefreshTime(TIME_MAX);
    subgrp->DoNotGo();

    context->_fsm->SetTimeOut(10);
    // unit in position - place bomb if neccessary
}

static void CheckAttackAttack(AISubgroupContext* context)
{
    STATE_PREFIX

    if (!IsAttackTarget(cmd, subgrp->GetGroup()))
    {
        subgrp->Leader()->GetVehicle()->EndAttack();
        context->_fsm->SetState(SAttackDone, context); // Success
        subgrp->ClearPlan();
        return;
    }

#if DIAG_ATTACK
    LOG_DEBUG(AI, "Check fire {:.1f},{:.1f}", lVehicle->Position().X(), lVehicle->Position().Z());
#endif

    Target* tar = GetAttackTarget(cmd, subgrp->GetGroup());
    if (tar)
    {
        if (!tar->IsKnown())
        {
            // target has not been seen for some time by any unit
            if (subgrp->HasAI())
            {
                // target disappeared - move to its last known position
                // check AI Center database

                AIGroup* grp = subgrp->GetGroup();
                AICenter* center = grp ? grp->GetCenter() : nullptr;
                if (center)
                {
                    const AITargetInfo* info = center->FindTargetInfo(cmd->_target);
                    if (info)
                    {
                        cmd->_destination = info->_realPos;
                    }
                }

                context->_fsm->SetState(SAttackMove, context);
                return;
            }
            else
            { // non-AI attack - target not known
                // if target is not seen for long time, fail
                if (tar->lastSeen < Glob.time - 60)
                {
                    context->_fsm->SetState(SAttackWait, context);
                    return;
                }
            }
        }
        bool somePossible = false, somePotential = false;
        TryAttack(somePossible, somePotential, subgrp, *tar);
        if (!somePossible)
        {
            if (!somePotential)
            {
                // wait until last shot is gone
                subgrp->Leader()->GetVehicle()->EndAttack();
                LogFSM(subgrp, "Attack", "Attack fail");
                context->_fsm->SetState(SAttackWait, context); // Fail
                subgrp->ClearPlan();
                return;
            }
            if (subgrp->HasAI())
            {
                // fire not possible
                if (context->_fsm->TimedOut())
                {
                    LogFSM(subgrp, "Attack", "Retry - no fire possible.");
                    context->_fsm->SetState(SAttackInit, context); // Retry
                    return;
                }
            }
        }
    }
    else
    {
        subgrp->Leader()->GetVehicle()->EndAttack();
        LogFSM(subgrp, "Attack", "Failed - target out of range.");
        context->_fsm->SetState(SAttackWait, context); // Fail
        return;
    }
}

static void AttackWait(AISubgroupContext* context)
{
    STATE_PREFIX
    if (!subgrp->HasAI())
    {
        return;
    }
}

static void CheckAttackWait(AISubgroupContext* context)
{
    STATE_PREFIX

    // check if target is destroyed
    if (!IsAttackTarget(cmd, subgrp->GetGroup()))
    {
        subgrp->Leader()->GetVehicle()->EndAttack();
        context->_fsm->SetState(SAttackDone, context); // Success
        subgrp->ClearPlan();
        return;
    }

    // wait until last shot at target is gone
    EntityAI* lVehicle = subgrp->Leader()->GetVehicle();
    Vehicle* lastShot = lVehicle->GetLastShot();
    if (subgrp->HasAI())
    {
        // check for a special case - last shot is mine or bomb
        Shot* shot = dyn_cast<Shot>(lastShot);
        if (shot)
        {
            switch (shot->Type()->_simulation)
            {
                case AmmoShotMine:
                case AmmoShotPipeBomb:
                case AmmoShotTimeBomb:
                    context->_fsm->SetState(SAttackRunAway, context); // failed
                    subgrp->ClearPlan();
                    return;
            }
        }
    }
    if (lVehicle->GetLastShotAtAssignedTarget() < Glob.time - 20)
    {
        subgrp->Leader()->GetVehicle()->EndAttack();
        context->_fsm->SetState(SAttackFail, context); // failed
        subgrp->ClearPlan();
        return;
    }
}

static void AttackRunAway(AISubgroupContext* context)
{
    STATE_PREFIX
    if (!subgrp->HasAI())
    {
        return;
    }

    AIUnit* leader = subgrp->Leader();
    EntityAI* lVehicle = leader->GetVehicle();
    // run away from target
    Target* tgt = GetAttackTarget(cmd, subgrp->GetGroup());
    if (tgt)
    {
        Vector3 tgtPos = tgt->LandAimingPosition();
        Vector3 pos = lVehicle->Position();
        AIUnit* gLeader = subgrp->GetGroup()->Leader();
        if (gLeader != leader)
        {
            // prefer running in group leader direction
            pos = gLeader->Position();
        }
        else
        {
            // (perhaps direction to first waypoint
        }
        Vector3 dir = pos - tgtPos;
        dir.Normalize();

        Vector3 posD = tgtPos + dir * 100;
        leader->FindNearestEmpty(posD);

        subgrp->SetRefreshTime(Glob.time + 60);
        subgrp->GoPlanned(posD);
    }
}

static void CheckAttackRunAway(AISubgroupContext* context)
{
    STATE_PREFIX
    if (!subgrp->HasAI())
    {
        subgrp->Leader()->GetVehicle()->EndAttack();
        context->_fsm->SetState(SAttackFail, context); // failed
        subgrp->ClearPlan();
        return;
    }
    // wait until leader is at given position

    if (!IsAttackTarget(cmd, subgrp->GetGroup()))
    {
        subgrp->Leader()->GetVehicle()->EndAttack();
        context->_fsm->SetState(SAttackDone, context); // Success
        subgrp->ClearPlan();
        return;
    }

    AIUnit* leader = subgrp->Leader();
    EntityAI* lVehicle = leader->GetVehicle();

    if (subgrp->AllUnitsCompleted())
    {
        CheckDistance(subgrp->Leader(), cmd->_destination);

        Vehicle* lastShot = lVehicle->GetLastShot();
        Shot* shot = dyn_cast<Shot>(lastShot);
        if (shot)
        {
            if (shot->Type()->_simulation == AmmoShotPipeBomb)
            {
                // find touch off
                UIAction action;
                action.type = ATTouchOff;
                // other params not used:
                action.target = lVehicle;
                action.param = 0;
                action.param2 = 0;
                action.hideOnUse = true;
                action.showWindow = false;
                action.priority = 0;
                lVehicle->StartActionProcessing(action, leader);
            }
        }

        // start over again
        context->_fsm->SetState(SAttackInit, context);
        subgrp->ClearPlan();
        return;
    }
}

static AISubgroupFSM::StateInfo attackStates[] = {
    AISubgroupFSM::StateInfo("Init", AttackInit, CheckAttackInit),
    AISubgroupFSM::StateInfo("FarMove", AttackFarMove, CheckAttackFarMove),
    AISubgroupFSM::StateInfo("Move", AttackMove, CheckAttackMove),
    AISubgroupFSM::StateInfo("Attack", AttackAttack, CheckAttackAttack),
    AISubgroupFSM::StateInfo("Wait", AttackWait, CheckAttackWait),
    AISubgroupFSM::StateInfo("RunAway", AttackRunAway, CheckAttackRunAway),
    AISubgroupFSM::StateInfo("Succeed", CommandSucceed, CheckCommandSucceed),
    AISubgroupFSM::StateInfo("Failed", CommandFailed, CheckCommandFailed)};

static void AttackEnterEnableFire(AISubgroupContext* context, bool fire)
{
    STATE_PREFIX

    Target* tgt = GetAttackTarget(cmd, subgrp->GetGroup());
    if (!tgt)
    {
        return;
    }

    for (int u = 0; u < subgrp->NUnits(); u++)
    {
        AIUnit* unit = subgrp->GetUnit(u);
        if (unit)
        {
            unit->AssignTarget(tgt);
            if (fire)
            {
                unit->EnableFireTarget(tgt);
            }
        }
    }
}

static void AttackExitEnableFire(AISubgroupContext* context, bool fire)
{
    STATE_PREFIX

    Target* tgt = GetAttackTarget(cmd, subgrp->GetGroup());
    if (!tgt)
    {
        return;
    }

    for (int u = 0; u < subgrp->NUnits(); u++)
    {
        AIUnit* unit = subgrp->GetUnit(u);
        if (unit)
        {
            unit->AssignTarget(nullptr);
            if (fire)
            {
                unit->EnableFireTarget(nullptr);
            }
        }
    }
}

static void AttackFireEnter(AISubgroupContext* context)
{
    AttackEnterEnableFire(context, true);
}
static void AttackFireExit(AISubgroupContext* context)
{
    AttackExitEnableFire(context, true);
}

static void AttackEnter(AISubgroupContext* context)
{
    AttackEnterEnableFire(context, false);
}

static void AttackExit(AISubgroupContext* context)
{
    AttackExitEnableFire(context, false);
}

static AISubgroupFSM::pStateFunction attackSpecial[] = {AttackEnter, AttackExit};

static AISubgroupFSM::pStateFunction attackFireSpecial[] = {AttackFireEnter, AttackFireExit};

// Command Hide FSM

enum
{
    SHideInit,
    SHideFail
};

static void HideInit(AISubgroupContext* context) {}

static void CheckHideInit(AISubgroupContext* context)
{
    STATE_PREFIX

    EntityAI* lVehicle = subgrp->Leader()->GetVehicle();
    lVehicle->HideThink();
}

static void HideEnter(AISubgroupContext* context)
{
    STATE_PREFIX
    if (!subgrp->HasAI())
    {
        return;
    }

    for (int u = 0; u < subgrp->NUnits(); u++)
    {
        AIUnit* unit = subgrp->GetUnit(u);
        if (!unit)
        {
            continue;
        }
        if (!unit->IsUnit())
        {
            continue;
        }
        unit->GetVehicle()->BegHide();
    }
}

static void HideExit(AISubgroupContext* context)
{
    STATE_PREFIX
    if (!subgrp->HasAI())
    {
        return;
    }

    for (int u = 0; u < subgrp->NUnits(); u++)
    {
        AIUnit* unit = subgrp->GetUnit(u);
        if (!unit)
        {
            continue;
        }
        if (!unit->IsUnit())
        {
            continue;
        }
        unit->GetVehicle()->EndHide();
    }
}

static AISubgroupFSM::StateInfo hideStates[] = {AISubgroupFSM::StateInfo("Init", HideInit, CheckHideInit),
                                                AISubgroupFSM::StateInfo("Failed", CommandFailed, CheckCommandFailed)};

static AISubgroupFSM::pStateFunction hideSpecial[] = {HideEnter, HideExit};

// Command Fire FSM

enum
{
    SFireInit,
    SFireFire,
    SFireDone,
    SFireFail
};

static void FireInit(AISubgroupContext* context)
{
    INIT_PREFIX
    Command* cmd = context->_task;
    AI_ERROR(cmd);
    if (!subgrp->HasAI())
    {
        return;
    }

    LogFSM(subgrp, "Fire", "INIT");

    subgrp->SetRefreshTime(Glob.time + SUBGROUP_REFRESH);
    subgrp->DoNotGo();

    for (int u = 0; u < subgrp->NUnits(); u++)
    {
        AIUnit* unit = subgrp->GetUnit(u);
        if (!unit || !unit->IsUnit())
        {
            continue;
        }
        Target* tgt = GetAttackTarget(cmd, subgrp->GetGroup());
        if (tgt)
        {
            unit->AssignTarget(tgt);
        }
    }
}

static void CheckFireInit(AISubgroupContext* context)
{
    STATE_PREFIX

    if (!IsAttackTarget(cmd, subgrp->GetGroup()))
    {
        context->_fsm->SetState(SFireDone, context); // Success
        subgrp->ClearPlan();
        return;
    }
    context->_fsm->SetState(SFireFire, context);
    subgrp->ClearPlan();
}

static void FireFire(AISubgroupContext* context)
{
    STATE_PREFIX
    if (!subgrp->HasAI())
    {
        return;
    }

    LogFSM(subgrp, "Fire", "FIRE");

    subgrp->SetRefreshTime(TIME_MAX);
    subgrp->DoNotGo();
}

static void CheckFireFire(AISubgroupContext* context)
{
    STATE_PREFIX

    if (!IsAttackTarget(cmd, subgrp->GetGroup()))
    {
        context->_fsm->SetState(SFireDone, context); // Success
        subgrp->ClearPlan();
        return;
    }

    Target* tar = GetAttackTarget(cmd, subgrp->GetGroup());
    bool somePossible = false;
    if (tar)
    {
        if (!subgrp->HasAI())
        {
            // check out of ammo
            bool outOfAmmo = true;
            for (int u = 0; u < subgrp->NUnits(); u++)
            {
                AIUnit* unit = subgrp->GetUnit(u);
                if (!unit || !unit->IsUnit())
                {
                    continue;
                }
                EntityAI* vehicle = unit->GetVehicle();
                if (cmd->_param >= 0 && cmd->_param < vehicle->NMagazineSlots())
                {
                    const Magazine* magazine = vehicle->GetMagazineSlot(cmd->_param)._magazine;
                    if (magazine && magazine->_ammo > 0)
                    {
                        outOfAmmo = false;
                    }
                }
                else
                {
                    for (int w = 0; w < vehicle->NMagazineSlots(); w++)
                    {
                        const Magazine* magazine = vehicle->GetMagazineSlot(w)._magazine;
                        if (magazine && magazine->_ammo > 0)
                        {
                            outOfAmmo = false;
                        }
                    }
                }
            }
            if (outOfAmmo)
            {
                context->_fsm->SetState(SFireFail, context); // Fail
                subgrp->ClearPlan();
            }
            return;
        }
        for (int u = 0; u < subgrp->NUnits(); u++)
        {
            AIUnit* unit = subgrp->GetUnit(u);
            if (!unit || !unit->IsUnit())
            {
                continue;
            }
            if (cmd->_param >= 0 && cmd->_param < unit->GetVehicle()->NMagazineSlots())
            {
                EntityAI* vehicle = unit->GetVehicle();
                const Magazine* magazine = vehicle->GetMagazineSlot(cmd->_param)._magazine;
                if (magazine && magazine->_ammo > 0)
                {
                    somePossible = true;
                }
            }
            else
            {
                FireResult result;
                bool possible = unit->GetVehicle()->WhatFireResult(result, *tar, 30.0);
                if (possible)
                {
                    somePossible = true;
                }
            }
        }
        if (!somePossible)
        {
            // no unit can fire - fail
            LogFSM(subgrp, "Fire", "Failed - no fire possible.");
            context->_fsm->SetState(SFireFail, context); // Fail
            subgrp->ClearPlan();
            return;
        }
    }
    else
    {
        LogFSM(subgrp, "Fire", "Failed - target out of range.");
        context->_fsm->SetState(SFireFail, context); // Fail
        subgrp->ClearPlan();
        return;
    }
}

static AISubgroupFSM::StateInfo fireStates[] = {
    AISubgroupFSM::StateInfo("Init", FireInit, CheckFireInit),
    AISubgroupFSM::StateInfo("Fire", FireFire, CheckFireFire),
    AISubgroupFSM::StateInfo("Succeed", CommandSucceed, CheckCommandSucceed),
    AISubgroupFSM::StateInfo("Failed", CommandFailed, CheckCommandFailed)};

// Command Move FSM

enum
{
    SMoveInit,
    SMoveMove,
    SMoveDone,
    SMoveFail
};

static void MoveInit(AISubgroupContext* context)
{
    INIT_PREFIX
    if (!subgrp->HasAI())
    {
        return;
    }

    LogFSM(subgrp, "Move", "INIT");

    subgrp->SetRefreshTime(TIME_MAX);
    subgrp->DoNotGo();
}

static void CheckMoveInit(AISubgroupContext* context)
{
    STATE_PREFIX

    context->_fsm->SetState(SMoveMove, context);
    subgrp->ClearPlan();
}

static void MoveMove(AISubgroupContext* context)
{
    STATE_PREFIX
    if (!subgrp->HasAI())
    {
        return;
    }

    LogFSM(subgrp, "Move", "MOVE");

    subgrp->SetRefreshTime(TIME_MAX);
    subgrp->GoPlanned(cmd->_destination);

    Vector3Val posL = subgrp->Leader()->Position();
    Vector3Val posD = cmd->_destination;
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
}

static void CheckMoveMove(AISubgroupContext* context)
{
    STATE_PREFIX

    if (!subgrp->HasAI())
    {
        Vector3Val posL = subgrp->Leader()->Position();
        Vector3Val posD = cmd->_destination;
        float maxDistance = 10;
        if (subgrp->Leader()->IsFreeSoldier())
        {
            maxDistance = 2.5;
        }
        if ((posL - posD).SquareSizeXZ() <= Square(maxDistance))
        {
            context->_fsm->SetState(SMoveDone, context);
            subgrp->ClearPlan();
        }
        return;
    }

    if (subgrp->AllUnitsCompleted())
    {
        CheckDistance(subgrp->Leader(), cmd->_destination);

        context->_fsm->SetState(SMoveDone, context);
        subgrp->ClearPlan();
        return;
    }
    if (subgrp->GetMode() != AISubgroup::PlanAndGo)
    {
        // Planning mode drifted from PlanAndGo while still in the Move state — a Stop or
        // idle DoNotGo parked the leader, or an MP player's unit became AI — which would
        // strand it short of the destination. Re-arm only after consuming any pending
        // completion; otherwise authored waypoint chains can miss MOVE completion.
        MoveMove(context);
    }
}

static AISubgroupFSM::StateInfo moveStates[] = {
    AISubgroupFSM::StateInfo("Init", MoveInit, CheckMoveInit),
    AISubgroupFSM::StateInfo("Move", MoveMove, CheckMoveMove),
    AISubgroupFSM::StateInfo("Succeed", CommandSucceed, CheckCommandSucceed),
    AISubgroupFSM::StateInfo("Failed", CommandFailed, CheckCommandFailed)};

// Command Stop FSM

enum
{
    SStopInit,
    SStopStop,
    SStopDone,
    SStopFail
};

static void StopEnter(AISubgroupContext* context)
{
    STATE_PREFIX
    if (!subgrp->HasAI())
    {
        return;
    }

    // set leader direction

    subgrp->DoNotGo();
}

static void StopExit(AISubgroupContext* context)
{
    STATE_PREFIX
}

static void StopInit(AISubgroupContext* context)
{
    INIT_PREFIX
    if (!subgrp->HasAI())
    {
        return;
    }

    LogFSM(subgrp, "Stop", "INIT");

    subgrp->SetRefreshTime(TIME_MAX);
    subgrp->DoNotGo();
}

static void CheckStopInit(AISubgroupContext* context)
{
    STATE_PREFIX

    context->_fsm->SetState(SStopStop, context);
    subgrp->ClearPlan();
}

static void StopStop(AISubgroupContext* context)
{
    // do nothing
}

static void CheckStopStop(AISubgroupContext* context) {}

static AISubgroupFSM::StateInfo stopStates[] = {
    AISubgroupFSM::StateInfo("Init", StopInit, CheckStopInit),
    AISubgroupFSM::StateInfo("Stop", StopStop, CheckStopStop),
    AISubgroupFSM::StateInfo("Succeed", CommandSucceed, CheckCommandSucceed),
    AISubgroupFSM::StateInfo("Failed", CommandFailed, CheckCommandFailed)};

// Command Expect FSM

enum
{
    SExpectInit,
    SExpectExpect,
    SExpectDone,
    SExpectFail
};

static void ExpectEnter(AISubgroupContext* context)
{
    STATE_PREFIX
    if (!subgrp->HasAI())
    {
        return;
    }

    // set leader direction

    subgrp->DoNotGo();
}

static void ExpectExit(AISubgroupContext* context)
{
    STATE_PREFIX
}

static void ExpectInit(AISubgroupContext* context)
{
    INIT_PREFIX
    if (!subgrp->HasAI())
    {
        return;
    }

    LogFSM(subgrp, "Expect", "INIT");

    subgrp->SetRefreshTime(TIME_MAX);
    subgrp->DoNotGo();
}

static void CheckExpectInit(AISubgroupContext* context)
{
    STATE_PREFIX

    context->_fsm->SetState(SExpectExpect, context);
    subgrp->ClearPlan();
}

static void ExpectExpect(AISubgroupContext* context)
{
    // do nothing
}

static void CheckExpectExpect(AISubgroupContext* context)
{
    // check if we are in formation
}

static AISubgroupFSM::StateInfo expectStates[] = {
    AISubgroupFSM::StateInfo("Init", ExpectInit, CheckExpectInit),
    AISubgroupFSM::StateInfo("Expect", ExpectExpect, CheckExpectExpect),
    AISubgroupFSM::StateInfo("Succeed", CommandSucceed, CheckCommandSucceed),
    AISubgroupFSM::StateInfo("Failed", CommandFailed, CheckCommandFailed)};

#include <Poseidon/AI/AISubgroupFSMSupply.inc>

static AISubgroupFSM::StateInfo supplyStates[] = {
    AISubgroupFSM::StateInfo("Init", SupplyInit, CheckSupplyInit),
    AISubgroupFSM::StateInfo("Alloc", SupplyAlloc, CheckSupplyAlloc),
    AISubgroupFSM::StateInfo("Move", SupplyMove, CheckSupplyMove),
    AISubgroupFSM::StateInfo("Direct", SupplyDirect, CheckSupplyDirect),
    AISubgroupFSM::StateInfo("Supply", SupplySupply, CheckSupplySupply),
    AISubgroupFSM::StateInfo("Succeed", CommandSucceed, CheckCommandSucceed),
    AISubgroupFSM::StateInfo("Failed", CommandFailed, CheckCommandFailed)};

static AISubgroupFSM::pStateFunction supplySpecial[] = {SupplyEnter, SupplyExit};

// Command Join FSM

enum
{
    SJoinInit,
    SJoinDone,
    SJoinFail
};

static void JoinInit(AISubgroupContext* context)
{
    STATE_PREFIX

    AISubgroup* joinTo = cmd->_joinToSubgroup;
    if (!joinTo)
    {
        joinTo = subgrp->GetGroup()->MainSubgroup();
    }

    if (joinTo && joinTo != subgrp && subgrp != subgrp->GetGroup()->MainSubgroup())
    {
        subgrp->JoinToSubgroup(joinTo);
    }
}

static void CheckJoinInit(AISubgroupContext* context)
{
    context->_fsm->SetState(SJoinDone, context);
}

static AISubgroupFSM::StateInfo joinStates[] = {
    AISubgroupFSM::StateInfo("Init", JoinInit, CheckJoinInit),
    AISubgroupFSM::StateInfo("Succeed", CommandSucceed, CheckCommandSucceed),
    AISubgroupFSM::StateInfo("Failed", CommandFailed, CheckCommandFailed)};

// Command GetIn FSM

static Vector3 GetGetInPoint(AIUnit* unit, Transport* veh)
{
    AI_ERROR(veh == unit->VehicleAssigned());
    Vector3 pos = veh->GetUnitGetInPos(unit);
    pos[1] = GLandscape->RoadSurfaceYAboveWater(pos[0], pos[2]);
    return pos;
}

static Vector3 GetGetInPointFar(AIUnit* unit, Transport* veh)
{
    Vector3 res;

    AI_ERROR(veh == unit->VehicleAssigned());

    if (veh->IsStopped())
    {
        res = veh->GetUnitGetInPos(unit);
    }
    else
    {
        res = veh->GetStopPosition();
        res += Vector3(30, 0, 0);
    }

    unit->FindNearestEmpty(res);

    res[1] = GLandscape->RoadSurfaceYAboveWater(res[0], res[2]);

    return res;
}

enum
{
    SGetInInit,
    SGetInMove,
    SGetInGetOut,
    SGetInWalk,
    SGetInDirect,
    SGetInGetIn,
    SGetInDone,
    SGetInFail
};

bool CheckGetInDone(AISubgroupContext* context)
{
    AISubgroup* subgrp = context->_subgroup;
    AI_ERROR(subgrp);
    Command* cmd = context->_task;
    AI_ERROR(cmd);

    AIUnit* leader = subgrp->Leader();
    Transport* vehInto = dyn_cast<Transport, Object>(cmd->_target);

    if (!vehInto || !vehInto->IsPossibleToGetIn())
    {
        context->_fsm->SetState(SGetInFail, context);
        subgrp->ClearPlan();
        return true; // state changed
    }

    if (leader->GetVehicle() == vehInto)
    {
        context->_fsm->SetState(SGetInDone, context);
        subgrp->ClearPlan();
        return true; // state changed
    }

    if (subgrp->HasAI() && leader->VehicleAssigned() != vehInto)
    {
        context->_fsm->SetState(SGetInFail, context);
        subgrp->ClearPlan();
        return true; // state changed
    }

    return false; // continue with other tests
}

static void GetInInit(AISubgroupContext* context)
{
    STATE_PREFIX
    LogFSM(subgrp, "GetIn", "INIT");

    AIUnit* leader = subgrp->Leader();
    Transport* vehInto = dyn_cast<Transport, Object>(cmd->_target);
    if (vehInto)
    {
        if (!subgrp->HasAI())
        {
            cmd->_destination = GetGetInPoint(leader, vehInto);
        }
    }

    if (!subgrp->HasAI())
    {
        return;
    }

    subgrp->SetRefreshTime(TIME_MAX);
    subgrp->DoNotGo();
}

static void CheckGetInInit(AISubgroupContext* context)
{
    STATE_PREFIX

    if (!subgrp->HasAI())
    {
        context->_fsm->SetState(SGetInGetIn, context);
    }
    else
    {
        context->_fsm->SetState(SGetInMove, context);
        subgrp->ClearPlan();
    }
}

static void GetInMove(AISubgroupContext* context)
{
    STATE_PREFIX
    if (!subgrp->HasAI())
    {
        return;
    }

    LogFSM(subgrp, "GetIn", "MOVE");

    // update command destination
    AIUnit* leader = subgrp->Leader();
    Transport* vehInto = dyn_cast<Transport, Object>(cmd->_target);
    if (vehInto)
    {
        cmd->_destination = GetGetInPointFar(leader, vehInto);
    }

    subgrp->SetRefreshTime(TIME_MAX);
    subgrp->GoPlanned(cmd->_destination);

    Vector3Val posL = leader->Position();
    Vector3Val posD = cmd->_destination;
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
}

static void CheckGetInMove(AISubgroupContext* context)
{
    STATE_PREFIX

    if (!subgrp->HasAI())
    {
        context->_fsm->SetState(SGetInGetIn, context);
        return;
    }
    if (CheckGetInDone(context))
    {
        return;
    }

    AIUnit* leader = subgrp->Leader();
    EntityAI* vehicle = leader->GetVehicle();
    Vector3Val posL = vehicle->Position();
    Vector3Val posD = cmd->_destination;

    Transport* vehInto = dyn_cast<Transport, Object>(cmd->_target);
    AI_ERROR(vehInto); // checked in CheckGetInDone
    Point3 posDNew = GetGetInPointFar(leader, vehInto);
    float radius = 2.5 * vehInto->GetRadius() + 15.0;

    bool completed = subgrp->AllUnitsCompleted();
    if (completed)
    {
        CheckDistance(subgrp->Leader(), cmd->_destination);
    }

    if ((posL - posDNew).SquareSizeXZ() <= Square(radius) || completed)
    {
        context->_fsm->SetState(SGetInGetOut, context);
        return;
    }

    float limit = 0.01 * (posL - posD).SquareSizeXZ();
    saturateMax(limit, Square(0.5 * radius));
    if ((posD - posDNew).SquareSizeXZ() > limit)
    {
        // update target position
        subgrp->GoPlanned(posDNew);
    }
}

static void GetInGetOut(AISubgroupContext* context)
{
    STATE_PREFIX
    if (!subgrp->HasAI())
    {
        return;
    }

    LogFSM(subgrp, "GetIn", "GETOUT");

    AIUnit* unit = subgrp->Leader();
    AI_ERROR(unit);

    unit->OrderGetIn(true);
    if (!unit->IsFreeSoldier())
    {
        PackedBoolArray list;
        list.Set(unit->ID() - 1, true);
        Command cmd;
        cmd._message = Command::GetOut;
        cmd._context = Command::CtxAutoSilent;
        cmd._id = subgrp->GetGroup()->GetNextCmdId();
        subgrp->GetGroup()->IssueCommand(cmd, list);
    }
}

static void CheckGetInGetOut(AISubgroupContext* context)
{
    STATE_PREFIX

    if (!subgrp->HasAI())
    {
        context->_fsm->SetState(SGetInGetIn, context);
        return;
    }
    if (CheckGetInDone(context))
    {
        return;
    }

    AIUnit* leader = subgrp->Leader();
    Transport* vehInto = dyn_cast<Transport, Object>(cmd->_target);

    if (!vehInto || !vehInto->QCanIGetInAny(leader->GetPerson()))
    {
        // fail - unable to get in
        context->_fsm->SetState(SGetInFail, context);
        subgrp->ClearPlan();
        return;
    }

    if (subgrp->Leader()->IsFreeSoldier())
    {
        context->_fsm->SetState(SGetInWalk, context);
        return;
    }

    if (!subgrp->GetGroup()->CommandSent(subgrp->Leader(), Command::GetOut))
    {
        // get out from current vehicle failed
        context->_fsm->SetState(SGetInFail, context);
        return;
    }
}

static void GetInWalk(AISubgroupContext* context)
{
    STATE_PREFIX
    LogFSM(subgrp, "GetIn", "GETIN");

    if (!subgrp->HasAI())
    {
        return;
    }

    // update command destination
    AIUnit* leader = subgrp->Leader();
    Transport* vehInto = dyn_cast<Transport, Object>(cmd->_target);
    if (vehInto)
    {
        cmd->_destination = GetGetInPointFar(leader, vehInto);
    }

    subgrp->SetRefreshTime(TIME_MAX);
    subgrp->GoPlanned(cmd->_destination);
}

static void CheckGetInWalk(AISubgroupContext* context)
{
    STATE_PREFIX

    if (!subgrp->HasAI())
    {
        context->_fsm->SetState(SGetInGetIn, context);
        return;
    }
    if (CheckGetInDone(context))
    {
        return;
    }

    AIUnit* leader = subgrp->Leader();
    EntityAI* vehicle = leader->GetVehicle();
    Vector3Val posL = vehicle->Position();
    Vector3Val posD = cmd->_destination;

    Transport* vehInto = dyn_cast<Transport, Object>(cmd->_target);
    AI_ERROR(vehInto); // checked in CheckGetInDone
    Point3 posDNew = GetGetInPointFar(leader, vehInto);
    float radius = vehInto->GetRadius() * 1.4 + 3.5;

    if (subgrp->AllUnitsCompleted())
    {
        CheckDistance(subgrp->Leader(), cmd->_destination);

        if (vehInto->IsStopped())
        {
            if (vehInto->QCanIGetInAny(leader->GetPerson()))
            {
                context->_fsm->SetState(SGetInDirect, context);
            }
            else
            {
                // fail - unable to get in
                context->_fsm->SetState(SGetInFail, context);
            }
            subgrp->ClearPlan();
            return;
        }
    }

    if ((posL - posDNew).SquareSizeXZ() <= Square(radius))
    {
        // check if vehicle is stopped
        if (vehInto->IsStopped())
        {
            CollisionBuffer collision;
            Vector3 posDExact = GetGetInPoint(leader, vehInto);
            if ((posL - posDExact).CosAngle(posDExact - vehInto->Position()) > cos(0.25 * H_PI))
            {
                GLandscape->ObjectCollision(collision, vehicle, nullptr, posL, posDExact, 0, ObjIntersectGeom);
                if (collision.Size() == 0)
                {
                    if (vehInto->QCanIGetInAny(leader->GetPerson()))
                    {
                        context->_fsm->SetState(SGetInDirect, context);
                    }
                    else
                    {
                        // fail - unable to get in
                        context->_fsm->SetState(SGetInFail, context);
                    }
                    subgrp->ClearPlan();
                    return;
                }
            }
        }
    }

    bool completed = subgrp->AllUnitsCompleted();
    if (completed)
    {
        CheckDistance(subgrp->Leader(), cmd->_destination);
    }

    float limit = 0.01 * (posL - posD).SquareSizeXZ();
    saturateMax(limit, Square(0.5 * radius));
    if ((posD - posDNew).SquareSizeXZ() > limit || completed)
    {
        // update target position
        subgrp->GoPlanned(posDNew);
    }
}

static void GetInDirect(AISubgroupContext* context)
{
    STATE_PREFIX
    if (!subgrp->HasAI())
    {
        return;
    }

    LogFSM(subgrp, "GetIn", "DIRECT");

    AIUnit* leader = subgrp->Leader();
    Transport* vehInto = dyn_cast<Transport, Object>(cmd->_target);
    if (vehInto)
    {
        // direct go
        subgrp->GoDirect(GetGetInPoint(leader, vehInto));
    }
}

static void CheckGetInDirect(AISubgroupContext* context)
{
    STATE_PREFIX

    if (!subgrp->HasAI())
    {
        context->_fsm->SetState(SGetInGetIn, context);
        return;
    }
    if (CheckGetInDone(context))
    {
        return;
    }

    AIUnit* leader = subgrp->Leader();
    EntityAI* vehicle = leader->GetVehicle();
    Transport* vehInto = dyn_cast<Transport, Object>(cmd->_target);
    AI_ERROR(vehInto); // checked in CheckGetInDone
    Vector3Val posL = vehicle->Position();
    Point3 posD = GetGetInPoint(leader, vehInto);

    if (cmd->_destination.Distance2(GetGetInPoint(leader, vehInto)) >= Square(3))
    {
        // vehicle moved - go back to non-direct state
        context->_fsm->SetState(SGetInWalk, context);
        return;
    }

    // if soldier crashes into the vehicle, GetIn
    CollisionBuffer bump;
    vehicle->Intersect(bump, vehInto);
    float radius = vehicle->GetPrecision();

    // avoid changes of destination
    if (bump.Size() > 0 || (posL - cmd->_destination).SquareSizeXZ() <= Square(radius))
    {
        context->_fsm->SetState(SGetInGetIn, context);
        subgrp->ClearPlan();
        return;
    }

    if ((posL - cmd->_destination).SquareSizeXZ() >= Square(100))
    {
        // vehicle is now very far - go back to non-direct state
        context->_fsm->SetState(SGetInWalk, context);
        return;
    }
}

static void GetInGetIn(AISubgroupContext* context)
{
    STATE_PREFIX
    if (!subgrp->HasAI())
    {
        return;
    }

    LogFSM(subgrp, "GetIn", "GETIN");

    AIUnit* leader = subgrp->Leader();
    AI_ERROR(leader->IsFreeSoldier());
    Transport* vehInto = dyn_cast<Transport, Object>(cmd->_target);
    if (!vehInto)
    {
        context->_fsm->SetState(SGetInFail, context);
        return;
    }
    if (!leader->ProcessGetIn(*vehInto))
    {
        context->_fsm->SetState(SGetInFail, context);
    }
    // else subgroup dies out
}

static void CheckGetInGetIn(AISubgroupContext* context)
{
    STATE_PREFIX
    CheckGetInDone(context);
}

static void GetInEnter(AISubgroupContext* context)
{
    STATE_PREFIX

    AIUnit* leader = subgrp->Leader();
    Transport* vehInto = dyn_cast<Transport, Object>(cmd->_target);
    if (vehInto)
    {
        vehInto->WaitForGetIn(leader);
    }
}

static void GetInExit(AISubgroupContext* context)
{
    STATE_PREFIX

    AIUnit* leader = subgrp->Leader();
    Transport* vehInto = dyn_cast<Transport, Object>(cmd->_target);
    if (vehInto)
    {
        vehInto->GetInFinished(leader);
    }
}

static AISubgroupFSM::StateInfo getinStates[] = {
    AISubgroupFSM::StateInfo("Init", GetInInit, CheckGetInInit),
    AISubgroupFSM::StateInfo("Move", GetInMove, CheckGetInMove),
    AISubgroupFSM::StateInfo("GetOut", GetInGetOut, CheckGetInGetOut),
    AISubgroupFSM::StateInfo("Walk", GetInWalk, CheckGetInWalk),
    AISubgroupFSM::StateInfo("Direct", GetInDirect, CheckGetInDirect),
    AISubgroupFSM::StateInfo("GetIn", GetInGetIn, CheckGetInGetIn),
    AISubgroupFSM::StateInfo("Succeed", CommandSucceed, CheckCommandSucceed),
    AISubgroupFSM::StateInfo("Failed", CommandFailed, CheckCommandFailed)};

static AISubgroupFSM::pStateFunction getinSpecial[] = {GetInEnter, GetInExit};

// Command GetOut FSM

enum
{
    SGetOutInit,
    SGetOutGetOut,
    SGetOutMove,
    SGetOutDone,
    SGetOutFail
};

static void GetOutInit(AISubgroupContext* context)
{
    INIT_PREFIX
    LogFSM(subgrp, "GetOut", "INIT");
    subgrp->SetRefreshTime(TIME_MAX);
}

static void CheckGetOutInit(AISubgroupContext* context)
{
    INIT_PREFIX
    context->_fsm->SetState(SGetOutGetOut, context);
}

static void GetOutGetOut(AISubgroupContext* context)
{
    STATE_PREFIX
    LogFSM(subgrp, "GetOut", "GETOUT");
}

static void CheckGetOutGetOut(AISubgroupContext* context)
{
    STATE_PREFIX

    AIUnit* leader = subgrp->Leader();

    if (leader->IsFreeSoldier())
    {
        if (subgrp->HasAI())
        {
            context->_fsm->SetState(SGetOutMove, context);
        }
        else
        {
            context->_fsm->SetState(SGetOutDone, context);
        }
    }
}

static void GetOutMove(AISubgroupContext* context)
{
    STATE_PREFIX

    AIUnit* leader = subgrp->Leader();

    LogFSM(subgrp, "GetOut", "Move");

    if (!cmd->_target)
    {
        context->_fsm->SetState(SGetOutDone, context);
        return;
    }

    Vector3Val posL = subgrp->Leader()->Position();
    Vector3Val posV = cmd->_target->Position();
    float distance = (cmd->_target->GetRadius() * 0.9f + 1.0f);

    Vector3 norm = (posL - posV);
    Point3 posD = posV + norm.Normalized() * distance;
    Vector3 normal;
    leader->FindNearestEmpty(posD);

    subgrp->GoDirect(posD);
}

static void CheckGetOutMove(AISubgroupContext* context)
{
    STATE_PREFIX

    AIUnit* leader = subgrp->Leader();

    EntityAI* lVehicle = leader->GetVehicle();

    if (!cmd->_target)
    {
        // target destroyed -
        context->_fsm->SetState(SGetOutDone, context);
        return;
    }
    const float tgtRadius = cmd->_target->GetRadius();
    const float radius = lVehicle->GetPrecision() + tgtRadius * 0.5;
    Vector3Val posL = lVehicle->Position();
    if ((posL - cmd->_destination).SquareSizeXZ() <= Square(radius))
    {
        context->_fsm->SetState(SGetOutDone, context);
    }
}

static void GetOutEnter(AISubgroupContext* context)
{
    STATE_PREFIX

    AIUnit* leader = subgrp->Leader();
    AI_ERROR(leader);
    if (leader->IsFreeSoldier())
    {
        return;
    }

    Transport* veh = leader->GetVehicleIn();
    AI_ERROR(veh);
    cmd->_target = veh;
    if (veh->Driver())
    {
        AIUnit* driver = veh->DriverBrain();
        if (driver)
        {
            veh->WaitForGetOut(leader);
            return;
        }
    }
    if (veh->GetGetOutTime() > Glob.time + 2)
    {
        veh->SetGetOutTime(Glob.time + 2);
    }
    veh->GetOutStarted(leader);
}

static void GetOutExit(AISubgroupContext* context)
{
    STATE_PREFIX

    AIUnit* leader = subgrp->Leader();
    Transport* veh = dyn_cast<Transport, Object>(cmd->_target);
    if (veh)
    {
        veh->GetOutFinished(leader);
    }
}

static AISubgroupFSM::StateInfo getoutStates[] = {
    AISubgroupFSM::StateInfo("Init", GetOutInit, CheckGetOutInit),
    AISubgroupFSM::StateInfo("GetOut", GetOutGetOut, CheckGetOutGetOut),
    AISubgroupFSM::StateInfo("Move", GetOutMove, CheckGetOutMove),
    AISubgroupFSM::StateInfo("Succeed", CommandSucceed, CheckCommandSucceed),
    AISubgroupFSM::StateInfo("Failed", CommandFailed, CheckCommandFailed)};

static AISubgroupFSM::pStateFunction getoutSpecial[] = {GetOutEnter, GetOutExit};

// Command Support FSM

enum
{
    SSupportInit,
    SSupportMove,
    SSupportDone,
    SSupportFail
};

static bool CheckSupportDone(AISubgroupContext* context)
{
    AISubgroup* subgrp = context->_subgroup;
    Command* cmd = context->_task;

    VehicleSupply* veh = dyn_cast<VehicleSupply>(subgrp->Leader()->GetVehicle());
    if (!veh)
    {
        // vehicle changed
        context->_fsm->SetState(SSupportFail, context);
        subgrp->ClearPlan();
        return true;
    }

    VehicleWithAI* target = cmd->_target;
    if (!target)
    {
        // target destroyed
        context->_fsm->SetState(SSupportFail, context);
        subgrp->ClearPlan();
        return true;
    }

    if (Glob.time > cmd->_time)
    {
        // timeout
        context->_fsm->SetState(SSupportFail, context);
        subgrp->ClearPlan();
        return true;
    }

    // target allocated - continue
    if (veh->GetAllocSupply() == target)
    {
        return false;
    }

    if (veh->GetAllocSupply())
    {
        // allocated by other unit
        context->_fsm->SetState(SSupportFail, context);
        subgrp->ClearPlan();
        return true;
    }

    const OLinkArray<AIUnit>& supplyUnits = veh->GetSupplyUnits();
    for (int i = 0; i < supplyUnits.Size(); i++)
    {
        if (!supplyUnits[i])
        {
            continue;
        }
        if (supplyUnits[i]->GetVehicle() == target)
        {
            return false; // continue
        }
    }

    // done
    context->_fsm->SetState(SSupportDone, context);
    subgrp->ClearPlan();
    return true;
}

static void SupportInit(AISubgroupContext* context)
{
    INIT_PREFIX
    if (!subgrp->HasAI())
    {
        return;
    }

    LogFSM(subgrp, "Support", "INIT");

    subgrp->SetRefreshTime(TIME_MAX);
    subgrp->DoNotGo();
}

static void CheckSupportInit(AISubgroupContext* context)
{
    STATE_PREFIX

    context->_fsm->SetState(SSupportMove, context);
    subgrp->ClearPlan();
}

static void SupportMove(AISubgroupContext* context)
{
    STATE_PREFIX
    if (!subgrp->HasAI())
    {
        return;
    }

    LogFSM(subgrp, "Support", "MOVE");

    // update command destination
    EntityAI* veh = cmd->_target;
    if (veh)
    {
        cmd->_destination = veh->Position();
    }

    subgrp->SetRefreshTime(TIME_MAX);
    subgrp->GoPlanned(cmd->_destination);
}

static void CheckSupportMove(AISubgroupContext* context)
{
    STATE_PREFIX

    if (CheckSupportDone(context))
    {
        return;
    }

    EntityAI* vehicle = subgrp->Leader()->GetVehicle();
    Vector3Val posL = vehicle->Position();
    Vector3Val posD = cmd->_destination;

    EntityAI* target = cmd->_target;
    AI_ERROR(target); // checked in CheckSupportDone
    Vector3Val posDNew = target->Position();
    float distanceToTarget2 = posL.DistanceXZ2(posD);
    float limit = Square(0.1f) * distanceToTarget2;
    saturateMax(limit, Square(5));
    float precision = vehicle->GetPrecision();
    saturateMax(limit, Square(precision));
    if (posD.DistanceXZ2(posDNew) > limit)
    {
        if (distanceToTarget2 > Square(precision * 4))
        {
            // update target position
            subgrp->GoPlanned(posDNew);
        }
    }
    else if (distanceToTarget2 < Square(precision * 2))
    {
        // check if target is in good position for supplying
        subgrp->Stop();
    }
}

static AISubgroupFSM::StateInfo supportStates[] = {
    AISubgroupFSM::StateInfo("Init", SupportInit, CheckSupportInit),
    AISubgroupFSM::StateInfo("Move", SupportMove, CheckSupportMove),
    AISubgroupFSM::StateInfo("Succeed", CommandSucceed, CheckCommandSucceed),
    AISubgroupFSM::StateInfo("Failed", CommandFailed, CheckCommandFailed)};

// Registration of FSMs

template <>
FSM* AbstractAIMachine<Command, AISubgroupContext>::CreateFSM(int taskType)
{
    int itemSize = sizeof(AISubgroupFSM::StateInfo);
    int funcSize = sizeof(AISubgroupFSM::pStateFunction);

    AI_ERROR(sizeof(waitStates) / itemSize == SWaitFail + 1);
    AI_ERROR(sizeof(attackStates) / itemSize == SAttackFail + 1);
    AI_ERROR(sizeof(hideStates) / itemSize == SHideFail + 1);
    AI_ERROR(sizeof(moveStates) / itemSize == SMoveFail + 1);
    AI_ERROR(sizeof(getoutStates) / itemSize == SGetOutFail + 1);
    AI_ERROR(sizeof(joinStates) / itemSize == SJoinFail + 1);
    AI_ERROR(sizeof(getinStates) / itemSize == SGetInFail + 1);
    AI_ERROR(sizeof(supplyStates) / itemSize == SSupplyFail + 1);
    AI_ERROR(sizeof(supportStates) / itemSize == SSupportFail + 1);
    AI_ERROR(sizeof(fireStates) / itemSize == SFireFail + 1);
    AI_ERROR(sizeof(stopStates) / itemSize == SStopFail + 1);

    switch (taskType)
    {
        case Command::NoCommand:
            return new AISubgroupFSM(noCommandStates, sizeof(noCommandStates) / itemSize);
        case Command::Wait:
            return new AISubgroupFSM(waitStates, sizeof(waitStates) / itemSize);
        case Command::Attack:
            return new AISubgroupFSM(attackStates, sizeof(attackStates) / itemSize, attackSpecial,
                                     sizeof(attackSpecial) / funcSize);
#if ENABLE_HOLDFIRE_FIX
        case Command::AttackAndFire:
            return new AISubgroupFSM(attackStates, sizeof(attackStates) / itemSize, attackFireSpecial,
                                     sizeof(attackFireSpecial) / funcSize);
#endif
        case Command::Hide:
            return new AISubgroupFSM(hideStates, sizeof(hideStates) / itemSize, hideSpecial,
                                     sizeof(hideSpecial) / funcSize);
        case Command::Move:
            return new AISubgroupFSM(moveStates, sizeof(moveStates) / itemSize);
        case Command::Heal:
        case Command::Repair:
        case Command::Refuel:
        case Command::Rearm:
        case Command::Action:
            return new AISubgroupFSM(supplyStates, sizeof(supplyStates) / itemSize, supplySpecial,
                                     sizeof(supplySpecial) / funcSize);
        case Command::Support:
            return new AISubgroupFSM(supportStates, sizeof(supportStates) / itemSize);
        case Command::Join:
            return new AISubgroupFSM(joinStates, sizeof(joinStates) / itemSize);
        case Command::GetIn:
            return new AISubgroupFSM(getinStates, sizeof(getinStates) / itemSize, getinSpecial,
                                     sizeof(getinSpecial) / funcSize);
        case Command::GetOut:
            return new AISubgroupFSM(getoutStates, sizeof(getoutStates) / itemSize, getoutSpecial,
                                     sizeof(getoutSpecial) / funcSize);
        case Command::Fire:
            return new AISubgroupFSM(fireStates, sizeof(fireStates) / itemSize);
        case Command::Stop:
            return new AISubgroupFSM(stopStates, sizeof(stopStates) / itemSize);
        case Command::Expect:
            return new AISubgroupFSM(expectStates, sizeof(expectStates) / itemSize);
    }
    Fail("Unknown command type");
    return nullptr;
}

void FailCommand(AISubgroupContext* context)
{
    AI_ERROR(context->_subgroup);
    Command* cmd = context->_task;
    AI_ERROR(cmd);
    FSM* fsm = context->_fsm;
    AI_ERROR(fsm);

    if (fsm)
    {
        switch (cmd->_message)
        {
            case Command::NoCommand:
                break;
            case Command::Wait:
                fsm->SetState(SWaitFail, context);
                break;
            case Command::Attack:
#if ENABLE_HOLDFIRE_FIX
            case Command::AttackAndFire:
                fsm->SetState(SAttackFail, context);
                break;
#endif
            case Command::Hide:
                fsm->SetState(SHideFail, context);
                break;
            case Command::Move:
                fsm->SetState(SMoveFail, context);
                break;
            case Command::Heal:
            case Command::Repair:
            case Command::Refuel:
            case Command::Rearm:
            case Command::Action:
                fsm->SetState(SSupplyFail, context);
                break;
            case Command::Support:
                fsm->SetState(SSupportFail, context);
                break;
            case Command::Join:
                fsm->SetState(SJoinFail, context);
                break;
            case Command::GetIn:
                fsm->SetState(SGetInFail, context);
                break;
            case Command::Fire:
                fsm->SetState(SFireFail, context);
                break;
            case Command::GetOut:
                fsm->SetState(SGetOutFail, context);
                break;
            case Command::Stop:
                fsm->SetState(SStopFail, context);
                break;
            case Command::Expect:
                fsm->SetState(SExpectFail, context);
                break;
        }
    }
}

} // namespace Poseidon
