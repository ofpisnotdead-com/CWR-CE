#include <Poseidon/Core/Application.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/Entities/Infantry/Person.hpp>
#include <Poseidon/AI/AIRadio.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>

#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/IO/Serialization/ParamArchive.hpp>
#include <Random/randomGen.hpp>

#include <Poseidon/World/Scene/Camera/CamEffects.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>

#include <Poseidon/Foundation/Algorithms/Qsort.hpp>

#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/Network/Network.hpp>

#include <Poseidon/Foundation/Enums/EnumNames.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <float.h>
#include <limits.h>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/BoolArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

#include <Poseidon/Game/UiActions.hpp>
#include <Poseidon/World/Entities/Infantry/MoveActions.hpp>
#include <Poseidon/AI/Path/AIDefs.hpp>
#pragma warning(disable : 4355)

namespace Poseidon
{
using namespace Foundation;

// Diagnostic for msgCmd->GetFrom() == this invariant violations in the radio
// queue scans below. Returns the result of the check so call sites can be
// written as `if (!CheckMsgCmdFrom(...)) continue;` in future patches.
static bool CheckMsgCmdFrom(const AIGroup* self, RadioMessageCommand* msgCmd, const char* callSite)
{
    if (msgCmd->GetFrom() == self)
    {
        return true;
    }
    LOG_ERROR(AI, "AIGroup radio queue holds foreign command at {}: this={} ({}), from={} ({}), msgType={}", callSite,
              self ? (const char*)self->GetDebugName() : "null", (const void*)self,
              msgCmd->GetFrom() ? (const char*)msgCmd->GetFrom()->GetDebugName() : "null",
              (const void*)msgCmd->GetFrom(), msgCmd->GetType());
    return false;
}

// Parameters

bool AIGroup::CommandSent(bool channelCenter)
{
    // channelCenter == true ... check radio of center
    // channelCenter == false ... check radio of group and FSM stack

    if (!channelCenter)
    {
        // check stack
        if (MainSubgroup()->_stack.Size() > 0)
        {
            return true;
        }
    }

    AI_ERROR(GetCenter());
    RadioChannel& radio = channelCenter ? GetCenter()->GetRadio() : GetRadio();

    // check radio channel
    int index = INT_MAX;
    while (true)
    {
        RadioMessage* msg = radio.FindPrevMessage(RMTCommand, index);
        if (!msg)
        {
            break;
        }
        AI_ERROR(dynamic_cast<RadioMessageCommand*>(msg));
        RadioMessageCommand* msgCmd = static_cast<RadioMessageCommand*>(msg);
        AI_ERROR(msgCmd);
        if (!channelCenter)
        {
            CheckMsgCmdFrom(this, msgCmd, "CommandSent(bool)/FindPrevMessage");
        }
        if (msgCmd->IsToMainSubgroup())
        {
            return true;
        }
    }

    // check actual message
    RadioMessage* msg = radio.GetActualMessage();
    if (msg && msg->GetType() == RMTCommand)
    {
        AI_ERROR(dynamic_cast<RadioMessageCommand*>(msg));
        RadioMessageCommand* msgCmd = static_cast<RadioMessageCommand*>(msg);
        AI_ERROR(msgCmd);
        if (!channelCenter)
        {
            CheckMsgCmdFrom(this, msgCmd, "CommandSent(bool)/GetActualMessage");
        }
        if (msgCmd->IsToMainSubgroup())
        {
            return true;
        }
    }

    return false;
}

bool AIGroup::CommandSent(Command::Message message, bool channelCenter)
{
    // channelCenter == true ... check radio of center
    // channelCenter == false ... check radio of group and FSM stack

    if (!channelCenter)
    {
        int j, m = NSubgroups();
        for (j = 0; j < m; j++)
        {
            AISubgroup* subgrp = GetSubgroup(j);
            if (!subgrp)
            {
                continue;
            }

            int i, n = subgrp->_stack.Size();
            for (i = 0; i < n; i++)
            {
                const Command* cmd = subgrp->_stack[i]._task;
                if (cmd->_message == message)
                {
                    return true;
                }
            }
        }
    }

    AI_ERROR(GetCenter());
    RadioChannel& radio = channelCenter ? GetCenter()->GetRadio() : GetRadio();

    // check radio channel
    int index = INT_MAX;
    while (true)
    {
        RadioMessage* msg = radio.FindPrevMessage(RMTCommand, index);
        if (!msg)
        {
            break;
        }
        AI_ERROR(dynamic_cast<RadioMessageCommand*>(msg));
        RadioMessageCommand* msgCmd = static_cast<RadioMessageCommand*>(msg);
        AI_ERROR(msgCmd);
        if (!channelCenter)
        {
            CheckMsgCmdFrom(this, msgCmd, "CommandSent(msg,bool)/FindPrevMessage");
        }
        if (msgCmd->GetCmdMessage() == message)
        {
            return true;
        }
    }

    RadioMessage* msg = radio.GetActualMessage();
    if (msg && msg->GetType() == RMTCommand)
    {
        AI_ERROR(dynamic_cast<RadioMessageCommand*>(msg));
        RadioMessageCommand* msgCmd = static_cast<RadioMessageCommand*>(msg);
        AI_ERROR(msgCmd);
        if (!channelCenter)
        {
            CheckMsgCmdFrom(this, msgCmd, "CommandSent(msg,bool)/GetActualMessage");
        }
        if (msgCmd->GetCmdMessage() == message)
        {
            return true;
        }
    }

    return false;
}

bool AIGroup::CommandSent(AIUnit* to, Command::Message message, bool channelCenter)
{
    // channelCenter == true ... check radio of center
    // channelCenter == false ... check radio of group and FSM stack

    if (!channelCenter)
    {
        AISubgroup* subgrp = to->GetSubgroup();
        if (!subgrp)
        {
            return false; // BUG in MP
        }
        int i, n = subgrp->_stack.Size();
        for (i = 0; i < n; i++)
        {
            const Command* cmd = subgrp->_stack[i]._task;
            if (cmd->_message == message)
            {
                return true;
            }
        }
    }

    AI_ERROR(GetCenter());
    RadioChannel& radio = channelCenter ? GetCenter()->GetRadio() : GetRadio();

    // check radio channel
    int index = INT_MAX;
    while (true)
    {
        RadioMessage* msg = radio.FindPrevMessage(RMTCommand, index);
        if (!msg)
        {
            break;
        }
        AI_ERROR(dynamic_cast<RadioMessageCommand*>(msg));
        RadioMessageCommand* msgCmd = static_cast<RadioMessageCommand*>(msg);
        AI_ERROR(msgCmd);
        if (!channelCenter)
        {
            CheckMsgCmdFrom(this, msgCmd, "CommandSent(to,msg,bool)/FindPrevMessage");
        }
        if (msgCmd->IsTo(to) && msgCmd->GetCmdMessage() == message)
        {
            return true;
        }
    }

    RadioMessage* msg = radio.GetActualMessage();
    if (msg && msg->GetType() == RMTCommand)
    {
        AI_ERROR(dynamic_cast<RadioMessageCommand*>(msg));
        RadioMessageCommand* msgCmd = static_cast<RadioMessageCommand*>(msg);
        AI_ERROR(msgCmd);
        if (!channelCenter)
        {
            CheckMsgCmdFrom(this, msgCmd, "CommandSent(to,msg,bool)/GetActualMessage");
        }
        if (msgCmd->IsTo(to) && msgCmd->GetCmdMessage() == message)
        {
            return true;
        }
    }

    return false;
}

void AIGroup::ClearGetInCommands(AIUnit* to)
{
    // cancel in radio
    int index = INT_MAX;
    while (true)
    {
        RadioMessage* msg = _radio.FindPrevMessage(RMTCommand, index);
        if (!msg)
        {
            break;
        }
        AI_ERROR(dynamic_cast<RadioMessageCommand*>(msg));
        RadioMessageCommand* msgCmd = static_cast<RadioMessageCommand*>(msg);
        AI_ERROR(msgCmd);
        CheckMsgCmdFrom(this, msgCmd, "ClearGetInCommands/FindPrevMessage");
        if (msgCmd->IsTo(to) && msgCmd->GetCmdMessage() == Command::GetIn &&
            (msgCmd->GetContext() == Command::CtxAuto || msgCmd->GetContext() == Command::CtxAutoSilent))
        {
            _radio.Cancel(msg);
            index = INT_MAX;
        }
    }

    RadioMessage* msg = _radio.GetActualMessage();
    if (msg && msg->GetType() == RMTCommand)
    {
        AI_ERROR(dynamic_cast<RadioMessageCommand*>(msg));
        RadioMessageCommand* msgCmd = static_cast<RadioMessageCommand*>(msg);
        AI_ERROR(msgCmd);
        CheckMsgCmdFrom(this, msgCmd, "ClearGetInCommands/GetActualMessage");
        if (msgCmd->IsTo(to) && msgCmd->GetCmdMessage() == Command::GetIn &&
            (msgCmd->GetContext() == Command::CtxAuto || msgCmd->GetContext() == Command::CtxAutoSilent))
        {
            _radio.Cancel(msg);
        }
    }
}

bool AIGroup::ReportSent(ReportSubject subject, const VehicleType* type)
{
    // check radio channel
    int index = INT_MAX;
    while (true)
    {
        RadioMessage* msg = GetRadio().FindPrevMessage(RMTReportTarget, index);
        if (!msg)
        {
            break;
        }
        AI_ERROR(dynamic_cast<RadioMessageReportTarget*>(msg));
        RadioMessageReportTarget* msgReport = static_cast<RadioMessageReportTarget*>(msg);
        AI_ERROR(msgReport);
        if (msgReport->GetSubject() == subject && msgReport->HasType(type))
        {
            return true;
        }
    }

    RadioMessage* msg = GetRadio().GetActualMessage();
    if (msg && msg->GetType() == RMTReportTarget)
    {
        AI_ERROR(dynamic_cast<RadioMessageReportTarget*>(msg));
        RadioMessageReportTarget* msgReport = static_cast<RadioMessageReportTarget*>(msg);
        AI_ERROR(msgReport);
        if (msgReport->GetSubject() == subject && msgReport->HasType(type))
        {
            return true;
        }
    }

    return false;
}

struct GetInPair
{
    TargetId vehicle;
    PackedBoolArray list;
};

class GetInPairs : public AutoArray<GetInPair>
{
    typedef AutoArray<GetInPair> base;

  public:
    void Add(EntityAI* veh, AIUnit* unit);
};

void GetInPairs::Add(EntityAI* veh, AIUnit* unit)
{
    int i, n = Size();
    for (i = 0; i < n; i++)
    {
        GetInPair& pair = Set(i);
        if (pair.vehicle == veh)
        {
            pair.list.Set(unit->ID() - 1, true);
            return;
        }
    }
    i = base::Add();
    GetInPair& pair = Set(i);
    pair.vehicle = TargetId(veh);
    pair.list.Set(unit->ID() - 1, true);
}

void AIGroup::GetInVehicles()
{
    if (IsAnyPlayerGroup())
    {
        return;
    }

    // get in / get out
    PackedBoolArray getout;
    GetInPairs getin;
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* unit = _units[i];
        if (!unit)
        {
            continue;
        }

        if (CommandSent(unit, Command::GetIn))
        {
            continue;
        }
        if (CommandSent(unit, Command::GetOut))
        {
            continue;
        }

        Transport* assigned = unit->VehicleAssigned();
        if (assigned && !assigned->IsAbleToMove())
        {
            unit->UnassignVehicle();
        }
        if (unit->IsGetInAllowed() && unit->IsGetInOrdered() && assigned)
        {
            // must be inside
            if (unit->IsFreeSoldier())
            {
                if (!unit->GetPerson()->IsActionInProgress(MFGetIn))
                {
                    getin.Add(assigned, unit);
                }
            }
            // wrong vehicle or wrong position
            else if (assigned != unit->GetVehicle() ||
                     (assigned->GetDriverAssigned() == unit) != (assigned->DriverBrain() == unit) ||
                     (assigned->GetCommanderAssigned() == unit) != (assigned->CommanderBrain() == unit) ||
                     (assigned->GetGunnerAssigned() == unit) != (assigned->GunnerBrain() == unit))
            {
                getout.Set(unit->ID() - 1, true);
            }
        }
        else
        {
            // must be outside
            if (!unit->IsFreeSoldier())
            {
                getout.Set(unit->ID() - 1, true);
            }
        }
    }

    Command cmd;
    cmd._joinToSubgroup = MainSubgroup();
    cmd._context = Command::CtxAuto;
    if (!getout.IsEmpty())
    {
        cmd._message = Command::GetOut;
        SendCommand(cmd, getout);
    }

    cmd._message = Command::GetIn;
    for (int i = 0; i < getin.Size(); i++)
    {
        GetInPair& pair = getin[i];
        cmd._target = pair.vehicle;
        SendCommand(cmd, pair.list);
    }
}

static int BetterTarget(const Ref<Target>* v0, const Ref<Target>* v1)
{
    Target* t0 = *v0;
    Target* t1 = *v1;
    return sign(t1->subjectiveCost - t0->subjectiveCost);
}

void AIGroup::ReactToEnemyDetected()
{
    AIUnit* leader = Leader();
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* unit = _units[i];
        if (!unit)
        {
            continue;
        }
        unit->SetDanger();
        if (unit->GetCombatModeMajor() == CMCareless)
        {
            continue;
        }
        if (_combatModeMinor >= CMCombat)
        {
            continue;
        }

        // going to combat - get out of some vehicles
        if (unit->IsInCargo())
        {
            Transport* trans = unit->GetVehicleIn();
            if (trans && trans->Type()->GetUnloadInCombat())
            {
                unit->OrderGetIn(false);
            }
        }
    }
    if (_combatModeMinor < CMCombat)
    {
        if (leader && leader->GetLifeState() == AIUnit::LSAlive && NUnits() > 1)
        {
            SendContact(leader);
        }
        _combatModeMinor = CMCombat;
    }
}

bool AIGroup::CreateTargetList(bool initialize, bool report)
{
    float nearestEnemyDist2 = 1e10;
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* unit = _units[i];
        if (!unit)
        {
            continue;
        }
        if (!unit->IsUnit())
        {
            continue;
        }
        if (unit->GetLifeState() != AIUnit::LSAlive)
        {
            continue;
        }
        if (!unit->GetVehicle())
        {
            continue;
        }
        unit->GetVehicle()->WhatIsVisible(_targetList, initialize);
        // check unit for nearestEnemyDist2
        saturateMin(nearestEnemyDist2, unit->GetNearestEnemyDist2());
    }

    _nearestEnemyDist2 = nearestEnemyDist2;

    _targetList.Manage(this);

    _enemiesDetected = 0;
    _unknownsDetected = 0;

    AICenter* center = GetCenter();

    bool someTarget = false;
    int n = _targetList.Size();
    for (int i = 0; i < n; i++)
    {
        Target* vtar = _targetList[i];
        // any target that is vanished or destroyed should be updated
        // and no more processing is required
        if (vtar->vanished || vtar->destroyed)
        {
            SendReport(ReportDestroy, *vtar);
            // we might also send radio report

            if (vtar->destroyed && vtar->isKnown && vtar->idKiller && vtar->timeReported <= TIME_MIN && vtar->idExact &&
                vtar->idExact->IsInLandscape())
            {
                AIUnit* killer = vtar->idKiller->CommanderUnit();
                if (killer && killer->IsLocal() && !killer->IsAnyPlayer())
                {
                    if (killer->GetLifeState() == AIUnit::LSAlive)
                    {
                        SendObjectDestroyed(killer, vtar->type);
                    }
                    vtar->timeReported = Glob.time;
                    vtar->posReported = vtar->position;
                }
            }

            continue;
        }
        bool doReport = report ? vtar->IsKnown() : vtar->isKnown;
        // report only recently seen targets
        if (report && doReport)
        {
            // check if we can report it

            if (vtar->lastSeen >= Glob.time - 10 && vtar->idSensor && vtar->idSensor->IsLocal() &&
                (!vtar->idSensor->IsNetworkPlayer() || vtar->timeReported <= TIME_MIN))
            {
                // check how large position change is required to report
                float time = 240; // time from reporting
                if (vtar->timeReported > Glob.time - time)
                {
                    time = Glob.time - vtar->timeReported;
                }
                float minDist = 1000 - time * 4;
                float minTime = 60;
                saturateMax(minDist, 0);

                // calculate how big distance is required to report the change
                AIUnit* unit = vtar->idSensor->Brain();
                if (unit && unit->GetCombatMode() == CMStealth)
                {
                    // in stealth mode report targets more often
                    minDist *= 0.125;
                    minTime = 10;
                }

                if (vtar->timeReported < Glob.time - minTime &&
                    vtar->posReported.Distance2(vtar->position) > Square(minDist))
                {
                    // timeReported - when was target reported
                    if (initialize)
                    {
                        vtar->timeReported = TIME_MIN;
                        vtar->posReported = VZero;
                    }
                    else
                    {
                        if (vtar->type->IsKindOf(GWorld->Preloaded(VTypeStatic)) || center->IsFriendly(vtar->side) ||
                            center->IsNeutral(vtar->side))
                        {
                            // never report friendly or static
                            vtar->timeReported = Glob.time;
                            vtar->posReported = vtar->position;
                        }
                        else if (vtar->side == TSideUnknown || center->IsEnemy(vtar->side))
                        {
                            // report enemy or unknown - only once
                            vtar->timeReported = Glob.time;
                            vtar->posReported = vtar->position;
                            SendRadioReport(ReportNew, *vtar);
                        }
                    }
                } // if (some change to report)
            } // if (can report)
        } // if (report)
        if (doReport)
        {
            SendReport(ReportNew, *vtar);

            if (center->IsEnemy(vtar->side) && IsLocal() && !IsAnyPlayerGroup())
            {
                for (int j = 0; j < NSubgroups(); j++)
                {
                    AISubgroup* subgrp = GetSubgroup(j);
                    if (subgrp)
                    {
                        subgrp->OnEnemyDetected(vtar->type, vtar->position);
                    }
                }
            }
        }

        if (vtar->IsKnownBySome())
        {
            if (vtar->side == TSideUnknown)
            {
                someTarget = true;
                _unknownsDetected++;
            }
            else if (center->IsEnemy(vtar->side))
            {
                Threat threat = vtar->type->GetDammagePerMinute(Square(200), 1);
                if ((threat[VSoft] + threat[VArmor] + threat[VAir]) > 0)
                { // calculate only dangerous enemies
                    _enemiesDetected++;
                }
                someTarget = true;
            }
        }
    }

    // check center database sometimes
    if (Glob.time > _checkCenterDBase + 10 && !IsAnyPlayerGroup())
    {
        // use AICenter database for target recognition
        _checkCenterDBase = Glob.time;

        for (int i = 0; i < _targetList.Size(); i++)
        {
            Target* tar = _targetList[i];

            float sideAccuracy = tar->FadingSideAccuracy();
            float typeAccuracy = tar->FadingSideAccuracy();

            if (sideAccuracy < 1.5 || typeAccuracy < 1.5 || tar->side == TSideUnknown)
            { // inaccurate info, consult AICenter
                const AITargetInfo* tgt = center->FindTargetInfo(tar->idExact);
                if (tgt)
                {
                    if (tgt->FadingSideAccuracy() > sideAccuracy)
                    {
                        tar->side = tgt->_side;
                        tar->sideAccuracy = tgt->_accuracySide;
                        tar->sideAccuracyTime = tgt->_timeSide;
                        if (tar->sideAccuracy > 1.5f)
                        {
                            tar->sideChecked = true;
                        }
                    }
                    if (tgt->FadingTypeAccuracy() > typeAccuracy)
                    {
                        tar->type = tgt->_type;
                        tar->accuracy = tgt->_accuracyType;
                        tar->accuracyTime = tgt->_timeType;
                    }
                }
            }
        }
    }
    // prepare subjective costs
    for (int i = 0; i < _targetList.Size(); i++)
    {
        Target* tar = _targetList[i];

        if (!tar->idExact || !tar->IsKnown() || tar->destroyed || tar->vanished ||
            tar->State(Leader()) < TargetEnemyCombat)
        {
            // reset subjective cost
            tar->dammagePerMinute = 0;
            tar->subjectiveCost = -1e10;
            continue;
        }

        if (center->IsEnemy(tar->side))
        {
            // calculate dammagePerMinute and subjectiveCost
            tar->dammagePerMinute = GetDammagePerMinute(tar);
            tar->subjectiveCost = GetSubjectiveCost(tar);
        }
        else if (tar->side == TSideUnknown)
        {
            tar->dammagePerMinute = 0;
            tar->subjectiveCost = 0;
        }
        else
        {
            // friendly or unknown - no need to test it
            tar->dammagePerMinute = 0;
            tar->subjectiveCost = -1e5;
        }
    }

    // sort
    QSort(_targetList.Data(), _targetList.Size(), BetterTarget);

    // realloc target list if allocated space is too big
    int allocSize = _targetList.MaxSize();
    if (allocSize > 32 && allocSize > _targetList.Size() * 4)
    {
        _targetList.Realloc(_targetList.Size() * 2);
    }

#if LOG_THINK
    Log("Group %s, %d enemies, %d visible", (const char*)GetDebugName(), _targetList.Size(), visibleList.Size());
#endif

    return someTarget;
}

Target* AIGroup::AddTarget(EntityAI* object, float accuracy, float sideAccuracy, float delay, const Vector3* pos,
                           AIUnit* sensor, float sensorDelay)
{
    // check if target is already in list
    Target* target = nullptr;
    for (int i = 0; i < _targetList.Size(); i++)
    {
        Target* t = _targetList[i];
        if (t->idExact == object)
        {
            // set new accuracy
            target = t;
            break;
        }
    }

    if (!target)
    {
        target = new Target(this);
        _targetList.Add(target);
        target->idExact = object;
        target->side = TSideUnknown;
        target->sideChecked = false;
        target->type = GWorld->Preloaded(VTypeAllVehicles);
        target->timeReported = TIME_MIN;
        target->isKnown = false;
    }
    if (!target->isKnown)
    {
        target->isKnown = true;
        if (!pos)
        {
            Vector3 apos = object->AimingPosition();
            // introduce some random error
            if (accuracy < 1)
            {
                float randomX = GRandGen.RandomValue() * 200 - 100;
                float randomZ = GRandGen.RandomValue() * 200 - 100;
                // y above ground should be correct
                float aboveGround = GLandscape->SurfaceYAboveWater(apos.X(), apos.Z()) - apos.Y();
                apos[0] += randomX;
                apos[2] += randomZ;
                apos[1] = GLandscape->SurfaceYAboveWater(apos[0], apos[2]) + aboveGround;
            }
            target->position = apos;
        }
        else
        {
            target->position = *pos;
        }
        target->speed = VZero; // speed not known
        if (sensor)
        {
            target->lastSeen = Glob.time - 40 + sensorDelay;
            target->delaySensor = Glob.time + sensorDelay;
            target->delay = Glob.time + delay;
            target->idSensor = sensor->GetPerson();
        }
        else
        {
            target->lastSeen = Glob.time - 40 + delay;
            target->delay = Glob.time + delay;
        }
        // we assume we have seen it, but not very recently
        // we set lastSeen so that target is known only for 5 sec
        // that should be enough to activate defensive reaction
        // usually some Attack is issued
    }
    else
    {
        Time delayUntil = Glob.time + delay;
        if (target->delay > delayUntil)
        {
            target->delay = delayUntil;
        }
    }
    const VehicleType* type = object->GetTypeAtLeast(accuracy);
    accuracy = type->GetAccuracy();
    if (target->FadingAccuracy() < accuracy)
    {
        // find first type description so that accuracy >=0.01
        target->accuracy = accuracy;
        target->accuracyTime = Glob.time;
        target->type = type;
    }
    if (target->FadingSideAccuracy() < sideAccuracy)
    {
        target->sideAccuracy = sideAccuracy;
        target->sideAccuracyTime = Glob.time;
        target->side = object->GetTargetSide(sideAccuracy);
        AI_ERROR(target->sideAccuracy < 3 || target->side != TSideUnknown);
        target->sideChecked = sideAccuracy >= 1.5f;
    }
    float spotability = floatMin(accuracy * 2.5, 1);
    if (target->FadingSpotability() < spotability)
    {
        target->spotability = spotability;
        target->spotabilityTime = Glob.time;
    }
    return target;
}

float AIGroup::GetDammagePerMinute(Target* tar) const
{
    // calculate how much are assigned unit able to destroy this target
    const VehicleType* type = tar->type;
    float dpm = 0;
    for (int j = 0; j < MAX_UNITS_PER_GROUP; j++)
    {
        AIUnit* unit = _units[j];
        if (!unit || !unit->IsUnit())
        {
            continue;
        }
        if (_assignTarget[j] == tar)
        {
            VehicleKind kind = type->GetKind();
            float dist2 = tar->position.Distance2(unit->Position());
            EntityAI* veh = unit->GetVehicle();
            if (!veh)
            {
                continue;
            }
            Threat threat = veh->GetType()->GetDammagePerMinute(dist2, 1.0, veh);
            dpm += threat[kind];
        }
    }
    return dpm;
}

float AIGroup::GetSubjectiveCost(Target* tar) const
{
    const VehicleType* type = tar->type;
    float baseCost = type->GetCost();

    Object* obj = tar->idExact;
    EntityAI* enemy = dyn_cast<EntityAI>(obj);
    if (!enemy || !enemy->IsAbleToFire())
    {
        return baseCost;
    }

    float maxCost = 0;
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* unit = _units[i];
        if (!unit || !unit->IsUnit())
        {
            continue;
        }

        EntityAI* veh = unit->GetVehicle();
        if (!veh)
        {
            continue;
        }

        VehicleKind vehKind = veh->GetType()->GetKind();
        float dist2 = veh->Position().Distance2(tar->position);

        Threat threat = type->GetDammagePerMinute(dist2, 1.0);
        float dammagePerMinute = threat[vehKind];
        if (dammagePerMinute > 0)
        {
            float lTimeToLive = veh->GetArmor() * 60 / dammagePerMinute;
            float danger = unit->GetTimeToLive() / lTimeToLive;
            // note: lTimeToLive may be higher that timeToLive if tgt is relatively new
            saturateMin(danger, 10);
            danger *= veh->GetType()->GetCost();
            saturateMax(maxCost, danger);
        }
    }

    return baseCost + maxCost;
}

#define COEF_TTL 0.6
#define COEF_TIMEOUT 5.0
#define MIN_TIMEOUT 2.0

#define DIAGS 0

void AIGroup::UnitAssignCanceled(AIUnit* unit)
{
    AI_ERROR(unit->GetGroup() == this);
    _assignTarget[unit->ID() - 1] = nullptr;
}

void AIGroup::AssignTargets()
{
    AICenter* center = GetCenter();
    AI_ERROR(center);
    if (!center)
    {
        return;
    }

    AIUnit* leader = Leader();
    if (!leader)
    {
        return;
    }
    if (leader->GetPerson()->IsUserStopped())
    {
        return;
    }

#if DIAGS
    LOG_DEBUG(AI, "Group {} - targets assignement", (const char*)GetDebugName());
#endif
    // calculate TimeToLive for all units and whole group
    float ttl[MAX_UNITS_PER_GROUP];
    float groupTTL = FLT_MAX;
    int nUnits = 0;
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* unit = _units[i];
        if (!unit || !unit->IsUnit())
        {
            ttl[i] = FLT_MAX;
            continue;
        }

        Target* at = _assignTarget[i];
        if (!at || _assignValidUntil[i] < Glob.time || at->destroyed || at->vanished ||
            at->State(unit) < _assignTargetState[i])
        {
            _assignTarget[i] = nullptr;
            nUnits++;
        }
#if DIAGS >= 2
        else
        {
            LOG_DEBUG(AI, "  Unit {} - TAS = {:.3f}", unit->ID(), _assignValidUntil[i] - Glob.time);
        }
#endif
        ttl[i] = unit->GetTimeToLive();
        saturateMin(groupTTL, ttl[i]);
#if DIAGS >= 2
        LOG_DEBUG(AI, "  Unit {} - TTL = {:.3f}", unit->ID(), ttl[i]);
#endif
    }
    groupTTL *= COEF_TTL;
#if DIAGS >= 2
    LOG_DEBUG(AI, "  Group's TTL = {:.3f}\n", groupTTL);
#endif

    if (nUnits <= 0)
    {
#if DIAGS >= 2
        LOG_DEBUG(AI, "  No unassigned units, exitting");
#endif
        return; // no unassigned units
    }

    // sort group target list

    if (IsAnyPlayerGroup())
    {
        // player has to assign targets manualy
        return;
    }

    // try to assign units to
    const float MinTimeToLive = 1e-10;
    const float MaxTimeToLive = 2419200.0;
    // first targets in the list are the most dangerous

    for (int i = 0; i < _targetList.Size(); i++)
    {
        Target* tar = _targetList[i];
        if (!tar->idExact)
        {
            continue;
        }
        if (tar->idExact->IsDammageDestroyed())
        {
            continue;
        }
        if (!tar->IsKnown())
        {
            continue;
        }
        if (!center->IsEnemy(tar->side))
        {
            break;
        }

#if DIAGS >= 2
        PackedBoolArray array;
        for (int j = 0; j < MAX_UNITS_PER_GROUP; j++)
        {
            AIUnit* unit = _units[j];
            if (!unit || !unit->IsUnit())
                continue;
            if (_assignTarget[j] == tar)
            {
                array.Set(j, true);
            }
        }
        char buffer[256];
        CreateUnitsList(array, buffer);

        LOG_DEBUG(AI, "  Target {} (side {}): cost {:.0f}, dpm {:.0f}, targeted by {}",
                  (const char*)tar->idExact->GetDebugName(), tar->side, tar->subjectiveCost, tar->dammagePerMinute,
                  buffer);
        if (tar->dammagePerMinute > 0 && array.IsEmpty())
        {
            float dammage = GetDammagePerMinute(tar);
            LOG_DEBUG(AI, "Dammage with no dammage source: {}", dammage);
        }
        const AITargetInfo* tgt = center->FindTargetInfo(tar->idExact);
        if (tgt)
        {
            LOG_DEBUG(AI, "    - center info: side {} ({:.3f}), type {} ({:.3f})", tgt->_side,
                      tgt->FadingSideAccuracy(), (const char*)tgt->_type->GetDisplayName(), tgt->FadingTypeAccuracy());
        }
#endif

        float enemyTTL = MaxTimeToLive;
        if (tar->dammagePerMinute > 0)
        {
            enemyTTL = 60 * tar->type->GetArmor() / tar->dammagePerMinute;
            saturate(enemyTTL, MinTimeToLive, MaxTimeToLive);
        }
#if DIAGS
        if (enemyTTL > groupTTL)
            LOG_DEBUG(AI, "  Try to cover target {} (side {})", (const char*)tar->idExact->GetDebugName(), tar->side);
#endif
        bool someAssigned = false;
        while (enemyTTL > groupTTL)
        {
            // select best unit
            float maxSurplus = -FLT_MAX;
            AIUnit* bestUnit = nullptr;
            FireResult bestResult;
            bool attack = false;

            for (int j = 0; j < MAX_UNITS_PER_GROUP; j++)
            {
                AIUnit* unit = _units[j];
                if (!unit || !unit->IsUnit())
                {
                    continue;
                }
                if (_assignTarget[j])
                {
                    continue;
                }
                if (unit->IsHoldingFire())
                {
                    continue; // ???
                }
                if (unit->GetAIDisabled() & AIUnit::DATarget)
                {
                    continue;
                }
                // note: group leader may assign target to himself
                if (!unit->GetVehicle()->IsAbleToFire())
                {
                    continue;
                }

                bool canFire = true;
                float attackCoef = unit->IsKeepingFormation() ? 1.0 / 2 : 1.0 / 16;
                float attackCost = unit->IsKeepingFormation() ? 1000 : 10000;

                bool canAttack = (!CommandSent(unit, Command::Attack) && !CommandSent(unit, Command::AttackAndFire) &&
                                  !CommandSent(unit, Command::GetOut) && !CommandSent(unit, Command::GetIn));

                // before issuing Attack there is delay
                // unit is assigned to target but attack is not issued
                // if unit is not able to fire during this time
                // attack is issued (if there is still some surplus from using it)
                if (unit == leader && unit->IsKeepingFormation())
                {
                    canAttack = false;
                }

                FireResult result;
                if (canFire)
                {
                    if (unit->GetVehicle()->WhatFireResult(result, *tar, ttl[j]))
                    {
                        float surplus = result.Surplus();
                        if (surplus > maxSurplus)
                        {
                            maxSurplus = surplus;
                            bestUnit = unit;
                            bestResult = result;
                            attack = false;
                        }
                    }
                    else
                    {
#if DIAGS
                        LOG_DEBUG(AI, "  - Unit {} - fire is not possible", unit->ID());
#endif
                    }
                }
                else
                {
#if DIAGS
                    LOG_DEBUG(AI, "  - Unit {} - cannot fire", unit->ID());
#endif
                }
                if (canAttack)
                {
                    if (unit->GetVehicle()->WhatAttackResult(result, *tar, ttl[j]))
                    {
                        float surplus = result.Surplus() * attackCoef - attackCost;
                        if (surplus > maxSurplus)
                        {
                            maxSurplus = surplus;
                            bestUnit = unit;
                            bestResult = result;
                            attack = true;
                        }
                    }
                    else
                    {
#if DIAGS
                        LOG_DEBUG(AI, "  - Unit {} - attack is not possible", unit->ID());
#endif
                    }
                } // if( canAttack )
                else
                {
#if DIAGS
                    LOG_DEBUG(AI, "  - Unit {} - attack is forbidden{}{}{}{}", unit->ID(),
                              unit->IsKeepingFormation() ? " Semaphore" : "",
                              CommandSent(unit, Command::Attack) ? " Attack" : "",
                              CommandSent(unit, Command::AttackAndFire) ? " Attack" : "",
                              CommandSent(unit, Command::GetIn) ? " GetIn" : "",
                              CommandSent(unit, Command::GetOut) ? " GetOut" : "");
#endif
                }
            }
            if (!bestUnit)
            {
                break;
            }

            // update ttl
            VehicleKind kind = tar->type->GetKind();
            float dist2 = tar->position.Distance2(bestUnit->Position());
            Threat threat = bestUnit->GetVehicle()->GetType()->GetDammagePerMinute(dist2, 1.0, bestUnit->GetVehicle());
            tar->dammagePerMinute += threat[kind];
            if (tar->dammagePerMinute > 0)
            {
                enemyTTL = 60 * tar->type->GetArmor() / tar->dammagePerMinute;
            }

#if DIAGS
            LOG_DEBUG(AI, "    Unit {} assigned", bestUnit->ID());
#endif
            {
                float dt = COEF_TIMEOUT * enemyTTL;
                float minTimeout = bestUnit->GetVehicle()->GetMinFireTime();
                saturate(dt, minTimeout, 120);

                // send AssignTarget to radio
                PackedBoolArray list;
                int bestI = bestUnit->ID() - 1;
                list.Set(bestI, true);
                _assignTarget[bestI] = tar;
                _assignTargetState[bestI] = tar->State(bestUnit);
                _assignValidUntil[bestI] = Glob.time + dt;

                SendTarget(tar, false, false, list);
                _lastSendTargetTime = Glob.time;
                someAssigned = true;
                if (attack)
                {
                    SendTarget(tar, true, false, list);
                }

                // no hide nor attack - "IGNORE" behaviour
                bestUnit->GetSubgroup()->ClearAttackCommands();
#if DIAGS
                LOG_DEBUG(AI, "    - fire for {:.1f} sec", dt);
#endif
            }

            if (--nUnits <= 0)
            {
                return;
            }
        } // while (ttl)
        if (someAssigned)
        {
            break; // only one target per function call
        }
    } // for (target)

    // all units left should hide
    // select hide command target

    bool groupEnableHideEnemy = !GetFlee();
    bool groupEnableHideUnknown = groupEnableHideEnemy;

    if (groupEnableHideEnemy)
    {
        // check for enemy / unknown targets
        groupEnableHideEnemy = false;
        groupEnableHideUnknown = false;
        for (int i = 0; i < _targetList.Size(); i++)
        {
            Target* tar = _targetList[i];
            if (!tar->idExact)
            {
                continue;
            }
            if (tar->idExact->IsDammageDestroyed())
            {
                continue;
            }
            if (!tar->IsKnown())
            {
                continue;
            }
            if (center->IsEnemy(tar->side))
            {
                groupEnableHideEnemy = true;
                groupEnableHideUnknown = true;
                break;
            }
            else if (tar->side == TSideUnknown)
            {
                groupEnableHideUnknown = true;
            }
        }
    }

    for (int j = 0; j < MAX_UNITS_PER_GROUP; j++)
    {
        AIUnit* unit = _units[j];
        if (!unit || !unit->IsUnit())
        {
            continue;
        }
        if (IsAnyPlayerGroup() && unit->IsGroupLeader())
        {
            continue;
        }

        // check if there is some hide command issued
        // if there is valid hide, leave it

        AISubgroup* subgrp = unit->GetSubgroup();
        bool oldHideTgt = subgrp->CheckHide();
        bool enableHide = false;

        if (_assignTarget[j] == nullptr && unit->GetCombatMode() != CMCareless)
        {
            if (groupEnableHideEnemy ||
                // in stealth hide also from unknown targets
                groupEnableHideUnknown && unit->GetCombatMode() == CMStealth)
            {
                if (!unit->IsKeepingFormation())
                {
                    if (unit->IsFreeSoldier())
                    {
                        enableHide = true;
                    }
                }
            }
        }

        // select hide target

        if (oldHideTgt == enableHide)
        {
            continue;
        }

        // hide target changed - issue command
        subgrp->ClearAttackCommands();

        if (enableHide)
        {
            Command cmd;
            cmd._message = Command::Hide;
            cmd._targetE = nullptr;
            cmd._destination = unit->Position();
            cmd._id = GetNextCmdId();

            IssueAutoCommand(cmd, unit);
        }
    }
}

#define ATTACK_DISTANCE 250.0
Threat AIGroup::GetAttackInfluence()
{
    Threat sum;
    int i;
    for (i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* unit = UnitWithID(i + 1);
        if (!unit)
        {
            continue;
        }
        const VehicleType* type = unit->GetPerson()->GetType();
        sum += type->GetDammagePerMinute(ATTACK_DISTANCE, 1.0);
        if (!unit->IsSoldier())
        { // driver
            type = unit->GetVehicle()->GetType();
            sum += type->GetDammagePerMinute(ATTACK_DISTANCE, 1.0);
        }
    }
    for (int v = 0; v < _vehicles.Size(); v++)
    {
        Transport* veh = _vehicles[v];
        if (!veh || veh->IsDammageDestroyed())
        {
            continue;
        }
        sum += veh->GetType()->GetDammagePerMinute(ATTACK_DISTANCE, 1.0);
    }
    return sum; // return total dammage per minute
}

Threat AIGroup::GetDefendInfluence()
{
    Threat sum;
    int i;
    for (i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* unit = UnitWithID(i + 1);
        if (!unit)
        {
            continue;
        }
        const VehicleType* type = unit->GetPerson()->GetType();
        sum[type->GetKind()] += type->GetArmor();
        if (!unit->IsSoldier())
        { // driver
            type = unit->GetVehicle()->GetType();
            sum[type->GetKind()] += type->GetArmor();
        }
    }
    for (int v = 0; v < _vehicles.Size(); v++)
    {
        Transport* veh = _vehicles[v];
        if (!veh || veh->IsDammageDestroyed())
        {
            continue;
        }
        const VehicleType* type = veh->GetType();
        sum[type->GetKind()] += type->GetArmor();
    }
    return sum; // return armor
}

void AIGroup::AddFirstWaypoint(Vector3Par pos)
{
    _wp.Insert(0);
    ArcadeWaypointInfo& wInfo = _wp[0];
    wInfo.Init();
    wInfo.position = pos;
    wInfo.type = ACMOVE;
}

void AIGroup::Move(AISubgroup* who, Vector3Val destination, Command::Discretion discretion)
{
    if (!who)
    {
        return;
    }
    if (who->Leader())
    {
        Vector3Val pos = who->Leader()->Position();
        float dist = 200;
        if ((pos - destination).SquareSizeXZ() > Square(dist))
        {
            for (int i = 0; i < who->NUnits(); i++)
            {
                AIUnit* unit = who->GetUnit(i);
                if (unit)
                {
                    unit->OrderGetIn(true);
                }
            }
        }
        AssignVehicles();
        GetInVehicles();
    }

    Command cmd;
    cmd._message = Command::Move;
    cmd._destination = destination;
    cmd._discretion = discretion;
    cmd._context = Command::CtxMission;
    if (who == MainSubgroup())
    {
        SendCommand(cmd);
    }
    else
    {
        SendCommand(cmd, who->GetUnitsList());
    }
}

void AIGroup::Wait(AISubgroup* who, Time until, Command::Discretion discretion)
{
    Command cmd;
    cmd._message = Command::Wait;
    cmd._time = until;
    cmd._discretion = discretion;
    cmd._context = Command::CtxMission;
    if (who == MainSubgroup())
    {
        SendCommand(cmd);
    }
    else
    {
        SendCommand(cmd, who->GetUnitsList());
    }
}

void AIGroup::Attack(AISubgroup* who, TargetType* target, Command::Discretion discretion)
{
    Command cmd;
    cmd._message = Command::Attack;
    cmd._target = target;
    cmd._discretion = discretion;
    cmd._context = Command::CtxMission;
    if (who == MainSubgroup())
    {
        cmd._destination = who->GetGroup()->Leader()->GetVehicle()->Position();
        SendCommand(cmd);
    }
    else
    {
        cmd._destination = who->Leader()->GetVehicle()->Position();
        SendCommand(cmd, who->GetUnitsList());
    }
}

void AIGroup::Fire(AISubgroup* who, TargetType* target, int weapon, Command::Discretion discretion)
{
    Command cmd;
    cmd._message = Command::Fire;
    cmd._target = target;
    cmd._param = weapon;
    cmd._discretion = discretion;
    cmd._context = Command::CtxMission;
    if (who == MainSubgroup())
    {
        SendCommand(cmd);
    }
    else
    {
        SendCommand(cmd, who->GetUnitsList());
    }
}

void AIGroup::GetIn(AISubgroup* who, TargetType* target, Command::Discretion discretion)
{
    Command cmd;
    cmd._message = Command::GetIn;
    cmd._target = target;
    cmd._discretion = discretion;
    cmd._context = Command::CtxMission;
    if (who == MainSubgroup())
    {
        SendCommand(cmd);
    }
    else
    {
        SendCommand(cmd, who->GetUnitsList());
    }
}

void AIGroup::MoveUnit(AIUnit* who, Vector3Val destination, Command::Discretion discretion)
{
    Command cmd;
    cmd._message = Command::Move;
    cmd._destination = destination;
    cmd._discretion = discretion;
    SendAutoCommandToUnit(cmd, who, false);
}

float AIGroup::UpdateAndGetThreshold()
{
    if (Glob.time >= _thresholdValid)
    {
        _threshold = GRandGen.RandomValue();
        _thresholdValid = Glob.time + 60.0f;
    }
    return _threshold;
}

bool AIGroup::GetAllDone() const
{
    if (IsPlayerGroup())
    {
        return false;
    }

    // query group status
    if (!_radio.Done())
    {
        return false;
    }
    // check if main subgroup completed
    if (MainSubgroup()->HasCommand())
    {
        return false;
    }
    AIUnit* leader = Leader();
    // group may not be done if leader is not ready
    return !leader || leader->GetLifeState() == AIUnit::LSAlive;
}

void AIGroup::AllGetOut()
{
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* unit = _units[i];
        if (unit)
        {
            unit->AllowGetIn(false);
        }
    }
}

void AIGroup::CargoGetOut()
{
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* unit = _units[i];
        if (unit && unit->IsInCargo())
        {
            unit->AllowGetIn(false);
        }
    }
}

Target* AIGroup::FindTargetAll(TargetType* id) const
{
    if (!id)
    {
        return nullptr;
    }
    for (int i = 0; i < _targetList.Size(); i++)
    {
        const Target* t = _targetList[i];
        if (t->idExact == id)
        {
            return const_cast<Target*>(t);
        }
    }
    return nullptr;
}

Target* AIGroup::FindTarget(TargetType* id) const
{
    if (!id)
    {
        return nullptr;
    }
    for (int i = 0; i < _targetList.Size(); i++)
    {
        const Target* t = _targetList[i];
        if (!t->IsKnown())
        {
            continue;
        }
        if (t->idExact == id)
        {
            return const_cast<Target*>(t);
        }
    }
    return nullptr;
}

bool AIGroup::FindTarget(TargetType* id, TargetSide& side, const VehicleType*& type) const
{
    bool result = false;
    Target* tar = FindTarget(id);
    if (tar)
    {
        result = true;
        side = tar->side;
        type = tar->type;
    }

    return result;
}

#define COMMAND_TIMEOUT 480.0 // 8 min

#define FUEL_NEAR 100.0f
#define FUEL_FAR 500.0f

const AITargetInfo* AIGroup::FindRefuelPosition(AIUnit::ResourceState state) const
{
    if (state == AIUnit::RSNormal)
    {
        return nullptr;
    }
    AI_ERROR(Leader());

    // find fuel in close range
    const AITargetInfo* depot = nullptr;
    float dist2Depot = FLT_MAX;
    const AITargetInfo* truck = nullptr;
    float dist2Truck = FLT_MAX;

    for (int i = 0; i < GetCenter()->NTargets(); i++)
    {
        const AITargetInfo& info = GetCenter()->GetTarget(i);
        if (info._destroyed)
        {
            continue;
        }
        if (!(info._side == TCivilian) && !(GetCenter()->IsFriendly(info._side)))
        {
            continue;
        }
        if (info._type->GetMaxFuelCargo() <= 0)
        {
            continue;
        }
        VehicleSupply* veh = dyn_cast<VehicleSupply, Object>(info._idExact);
        if (!veh)
        {
            continue;
        }
        if (veh->GetFuelCargo() <= 0)
        {
            continue;
        }
        if (GetCenter()->GetExposurePessimistic(info._realPos) > 0)
        {
            continue;
        }
        float dist2 = (Leader()->Position() - info._realPos).SquareSizeXZ();

        if (info._type->IsKindOf(GLOB_WORLD->Preloaded(VTypeStatic)))
        {
            if (dist2 < dist2Depot)
            {
                dist2Depot = dist2;
                depot = &info;
            }
        }
        else
        {
            if (dist2 < dist2Truck)
            {
                dist2Truck = dist2;
                truck = &info;
            }
        }
    }

    if (depot && dist2Depot < Square(FUEL_NEAR))
    {
        // refuel in near depot
        return depot;
    }
    if (truck && dist2Truck < Square(FUEL_NEAR))
    {
        return truck;
    }

    if (state == AIUnit::RSLow)
    {
        return nullptr;
    }

    // find fuel in wider range
    if (depot && dist2Depot < Square(FUEL_FAR))
    {
        // refuel in far depot
        return depot;
    }
    if (truck && dist2Truck < Square(FUEL_FAR))
    {
        // refuel in far truck
        return truck;
    }

    // fuel not found
    return nullptr;
}

} // namespace Poseidon
