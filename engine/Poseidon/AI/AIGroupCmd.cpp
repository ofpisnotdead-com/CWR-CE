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
#include <Poseidon/Game/UiActions.hpp>

#include <Poseidon/World/Entities/Infantry/MoveActions.hpp>
#include <limits.h>
#include <Poseidon/Foundation/Containers/BoolArray.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

namespace Poseidon
{

void AIGroup::IssueCommand(Command& cmd, PackedBoolArray list)
{
    if (list.IsEmpty())
    {
        return;
    }

    AIUnit* leader = Leader();
    // if leader is not alive, he should not be able to send commands
    if (leader && leader->GetLifeState() != AIUnit::LSAlive)
    {
        // auto commands may be issued even without leader alive
        if (cmd._context != Command::CtxAuto && cmd._context != Command::CtxAutoSilent)
        {
            LOG_DEBUG(AI, "IssueCommand: No command, leader {} is not alive", (const char*)leader->GetDebugName());
            return;
        }
    }

    if (cmd._message == Command::GetOut)
    {
        // Get Out processed by units
        int i;
        for (i = 0; i < MAX_UNITS_PER_GROUP; i++)
        {
            if (!list[i])
            {
                continue;
            }
            AIUnit* unit = UnitWithID(i + 1);
            if (!unit)
            {
                continue;
            }

            if (unit->IsFreeSoldier())
            {
                continue;
            }

            AISubgroup* subgrp = unit->GetSubgroup();
            AI_ERROR(subgrp);
            if (subgrp != MainSubgroup() && subgrp->NUnits() == 1)
            {
                cmd._joinToSubgroup = nullptr;
                subgrp->ReceiveCommand(cmd);
            }
            else
            {
                if (cmd._context == Command::CtxAuto || cmd._context == Command::CtxAutoSilent)
                {
                    cmd._joinToSubgroup = subgrp;
                }
                subgrp = new AISubgroup();
                AddSubgroup(subgrp);
                subgrp->AddUnit(unit);
                subgrp->SelectLeader(unit);
                if (GWorld->GetMode() == GModeNetware)
                {
                    GetNetworkManager().CreateObject(subgrp);
                }
                subgrp->ReceiveCommand(cmd);
            }
        }
    }
    else if (cmd._message == Command::GetIn || cmd._message == Command::Heal || cmd._message == Command::Repair ||
             cmd._message == Command::Refuel || cmd._message == Command::Rearm || cmd._message == Command::Support ||
             cmd._message == Command::Stop || cmd._message == Command::Expect || cmd._message == Command::Join)
    {
        // Get In and supply commands processed by vehicles
        int i;
        for (i = 0; i < MAX_UNITS_PER_GROUP; i++)
        {
            if (!list[i])
            {
                continue;
            }
            AIUnit* unit = UnitWithID(i + 1);
            if (!unit)
            {
                continue;
            }
            if (cmd._message == Command::GetIn && unit->GetVehicle() == cmd._target)
            {
                // unit already in vehicle
                continue;
            }

            Transport* veh = unit->GetVehicleIn();
            AIUnit* commander = nullptr;
            if (veh)
            {
                commander = veh->CommanderUnit();
            }

            if (unit->IsInCargo())
            {
                if (!commander || commander->GetGroup() != this || !list[commander->ID() - 1])
                {
                    unit->IssueGetOut();
                }
            }
            else if (!unit->IsUnit())
            {
                if (commander && commander->GetGroup() == this)
                {
                    if (list[commander->ID() - 1])
                    {
                        continue;
                    }
                    else
                    {
                        list.Set(commander->ID() - 1, true);
                        if (commander->ID() - 1 > i)
                        {
                            continue;
                        }
                        unit = commander;
                    }
                }
            }

            AISubgroup* subgrp = unit->GetSubgroup();
            AI_ERROR(subgrp);
            if (subgrp != MainSubgroup())
            {
                bool allInside = true;
                if (veh)
                {
                    for (int i = 0; i < subgrp->NUnits(); i++)
                    {
                        AIUnit* u = subgrp->GetUnit(i);
                        if (u && u->GetVehicleIn() != veh)
                        {
                            allInside = false;
                            break;
                        }
                    }
                }
                else
                {
                    allInside = subgrp->NUnits() == 1;
                }
                if (allInside)
                {
                    subgrp->ReceiveCommand(cmd);
                    continue;
                }
            }
            if (cmd._context == Command::CtxAuto || cmd._context == Command::CtxAutoSilent)
            {
                cmd._joinToSubgroup = subgrp;
            }
            subgrp = new AISubgroup();
            AddSubgroup(subgrp);
            if (veh)
            {
                if (veh->CommanderBrain() && veh->CommanderBrain()->GetGroup() == this)
                {
                    subgrp->AddUnit(veh->CommanderBrain());
                    unit = veh->CommanderBrain();
                }
                if (veh->DriverBrain() && veh->DriverBrain()->GetGroup() == this)
                {
                    subgrp->AddUnit(veh->DriverBrain());
                    unit = veh->DriverBrain();
                }
                if (veh->GunnerBrain() && veh->GunnerBrain()->GetGroup() == this)
                {
                    subgrp->AddUnit(veh->GunnerBrain());
                }
                subgrp->SelectLeader(unit);
            }
            else
            {
                subgrp->AddUnit(unit);
                subgrp->SelectLeader(unit);
            }
            if (GWorld->GetMode() == GModeNetware)
            {
                GetNetworkManager().CreateObject(subgrp);
            }
            subgrp->ReceiveCommand(cmd);
        }
    }
    else
    {
        // create subgroup
        AISubgroup* subgrp = nullptr;
        {
            for (int i = 0; i < NSubgroups(); i++)
            {
                AISubgroup* s = GetSubgroup(i);
                if (!s || s == MainSubgroup())
                {
                    continue;
                }
                if (list.Contain(s->GetUnitsListNoCargo()))
                {
                    subgrp = s;
                    break;
                }
            }
            if (!subgrp)
            {
                subgrp = new AISubgroup();
                AddSubgroup(subgrp);
                if (GWorld->GetMode() == GModeNetware)
                {
                    GetNetworkManager().CreateObject(subgrp);
                }
            }
        }
        AI_ERROR(subgrp);
        AI_ERROR(subgrp->GetGroup());
        AI_ERROR(subgrp->GetGroup() == this);
        subgrp->AvoidRefresh(); // avoid refresh during subgrp creation
        // add units from other subgroups
        for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
        {
            if (!list[i])
            {
                continue;
            }
            AIUnit* unit = UnitWithID(i + 1);
            if (!unit)
            {
                continue;
            }
            if (unit->GetSubgroup() == subgrp)
            {
                continue;
            }
            AI_ERROR(unit->GetGroup() == this);
            if (!unit->IsUnit())
            {
                AIUnit* u = unit->GetVehicleIn()->CommanderUnit();
                if (!u || u->GetGroup() != this || u->IsPlayer())
                {
                    // unit in cargo with other group or no driver
                    unit->IssueGetOut();
                }
                else if (list[u->ID() - 1])
                {
                    continue;
                }
                else
                {
                    unit = u;
                }
            }
            AI_ERROR(unit->GetGroup() == this);
            AISubgroup* oldSubgrp = unit->GetSubgroup();
            AI_ERROR(oldSubgrp != subgrp);
            if (!unit->IsUnit())
            {
                subgrp->AddUnit(unit);
                subgrp->SelectLeader();
                AI_ERROR(subgrp->GetGroup() == this);
            }
            else
            {
                AI_ERROR(subgrp->GetGroup() == this);
                subgrp->AddUnitWithCargo(unit);
                AI_ERROR(subgrp->GetGroup() == this);
            }
        }
        subgrp->AvoidRefresh(false);

        // send command to subgroup
        subgrp->ReceiveCommand(cmd);
    }
}

void AIGroup::SendFormation(Formation f, AISubgroup* to)
{
    AI_ERROR(to);
    if (!to)
    {
        return;
    }

    AI_ERROR(Leader());
    if (!Leader())
    {
        return;
    }

    // check radio channel
    int index = INT_MAX;
    while (true)
    {
        RadioMessage* msg = GetRadio().FindPrevMessage(RMTFormation, index);
        if (!msg)
        {
            break;
        }
        AI_ERROR(dynamic_cast<RadioMessageFormation*>(msg));
        RadioMessageFormation* msgForm = static_cast<RadioMessageFormation*>(msg);
        AI_ERROR(msgForm);
        AI_ERROR(msgForm->GetFrom() == this);
        if (msgForm->GetTo() == to)
        {
            if (msgForm->GetFormation() == f)
            {
                return;
            }
            else
            {
                msgForm->SetFormation(f);
                return;
            }
        }
    }

    {
        RadioMessage* msg = GetRadio().GetActualMessage();
        if (msg && msg->GetType() == RMTFormation)
        {
            AI_ERROR(dynamic_cast<RadioMessageFormation*>(msg));
            RadioMessageFormation* msgForm = static_cast<RadioMessageFormation*>(msg);
            AI_ERROR(msgForm);
            AI_ERROR(msgForm->GetFrom() == this);
            if (msgForm->GetTo() == to)
            {
                if (msgForm->GetFormation() == f)
                {
                    return;
                }
                else
                {
                    goto TransmitSendFormation;
                }
            }
        }
    }

    if (to->GetFormation() == f)
    {
        return;
    }

    if (NUnits() == 1)
    {
        to->SetFormation(f);
        return;
    }

TransmitSendFormation:
    GetRadio().Transmit(new RadioMessageFormation(this, to, f), GetCenter()->GetLanguage());
}

void AIGroup::SendSemaphore(Semaphore sem, PackedBoolArray list)
{
    switch (sem)
    {
        case AI::SemaphoreBlue:
            SendLooseFormation(false, list);
            SendOpenFire(OFSNeverFire, list);
            break;
        case AI::SemaphoreGreen:
            SendLooseFormation(false, list);
            SendOpenFire(OFSHoldFire, list);
            break;
        case AI::SemaphoreWhite:
            SendLooseFormation(true, list);
            SendOpenFire(OFSHoldFire, list);
            break;
        case AI::SemaphoreYellow:
            SendLooseFormation(false, list);
            SendOpenFire(OFSOpenFire, list);
            break;
        case AI::SemaphoreRed:
            SendLooseFormation(true, list);
            SendOpenFire(OFSOpenFire, list);
            break;
    }
}

void AIGroup::SendBehaviour(CombatMode mode, PackedBoolArray list)
{
    AIUnit* leader = Leader();
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        if (!list.Get(i))
        {
            continue;
        }
        AIUnit* unit = UnitWithID(i + 1);
        if (!unit)
        {
            list.Set(i, false);
            continue;
        }

        if (!leader || unit == leader)
        {
            unit->SetCombatModeMajor(mode);
            list.Set(i, false);
            continue;
        }

        // check radio channel
        int index = INT_MAX;
        while (true)
        {
            RadioMessage* msg = GetRadio().FindPrevMessage(RMTBehaviour, index);
            if (!msg)
            {
                break;
            }
            AI_ERROR(dynamic_cast<RadioMessageBehaviour*>(msg));
            RadioMessageBehaviour* msgSem = static_cast<RadioMessageBehaviour*>(msg);
            AI_ERROR(msgSem);
            AI_ERROR(msgSem->GetFrom() == this);
            if (msgSem->IsTo(unit))
            {
                if (msgSem->GetBehaviour() == mode)
                {
                    list.Set(i, false);
                    goto SendBehaviourForContinue;
                }
                else
                {
                    msgSem->DeleteTo(unit);
                    break;
                }
            }
            else if (msgSem->GetBehaviour() == mode)
            {
                msgSem->AddTo(unit);
                list.Set(i, false);
                goto SendBehaviourForContinue;
            }
        }

        {
            RadioMessage* msg = GetRadio().GetActualMessage();
            if (msg && msg->GetType() == RMTBehaviour)
            {
                AI_ERROR(dynamic_cast<RadioMessageBehaviour*>(msg));
                RadioMessageBehaviour* msgSem = static_cast<RadioMessageBehaviour*>(msg);
                AI_ERROR(msgSem);
                AI_ERROR(msgSem->GetFrom() == this);
                if (msgSem->IsTo(unit))
                {
                    if (msgSem->GetBehaviour() == mode)
                    {
                        list.Set(i, false);
                        continue;
                    }
                    else
                    {
                        goto SendBehaviourForContinue;
                    }
                }
            }
        }

        if (unit->GetCombatModeMajor() == mode)
        {
            list.Set(i, false);
            continue;
        }

    SendBehaviourForContinue:
        continue;
    }

    if (leader && !list.IsEmpty())
    {
        GetRadio().Transmit(new RadioMessageBehaviour(this, list, mode), GetCenter()->GetLanguage());
    }
}

void AIGroup::SendLooseFormation(bool loose, PackedBoolArray list)
{
    AIUnit* leader = Leader();
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        if (!list.Get(i))
        {
            continue;
        }
        AIUnit* unit = UnitWithID(i + 1);
        if (!unit)
        {
            list.Set(i, false);
            continue;
        }

        if (!leader || unit == leader)
        {
            AI::Semaphore s = unit->GetSemaphore();
            s = ApplyLooseFormation(s, loose);
            unit->SetSemaphore(s);
            list.Set(i, false);
            continue;
        }

        // check radio channel
        int index = INT_MAX;
        while (true)
        {
            RadioMessage* msg = GetRadio().FindPrevMessage(RMTLooseFormation, index);
            if (!msg)
            {
                break;
            }
            AI_ERROR(dynamic_cast<RadioMessageLooseFormation*>(msg));
            RadioMessageLooseFormation* msgSem = static_cast<RadioMessageLooseFormation*>(msg);
            AI_ERROR(msgSem);
            AI_ERROR(msgSem->GetFrom() == this);
            if (msgSem->IsTo(unit))
            {
                if (msgSem->IsLooseFormation() == loose)
                {
                    list.Set(i, false);
                    goto SendLooseFormationForContinue;
                }
                else
                {
                    msgSem->DeleteTo(unit);
                    break;
                }
            }
            else if (msgSem->IsLooseFormation() == loose)
            {
                msgSem->AddTo(unit);
                list.Set(i, false);
                goto SendLooseFormationForContinue;
            }
        }

        {
            RadioMessage* msg = GetRadio().GetActualMessage();
            if (msg && msg->GetType() == RMTLooseFormation)
            {
                AI_ERROR(dynamic_cast<RadioMessageLooseFormation*>(msg));
                RadioMessageLooseFormation* msgSem = static_cast<RadioMessageLooseFormation*>(msg);
                AI_ERROR(msgSem);
                AI_ERROR(msgSem->GetFrom() == this);
                if (msgSem->IsTo(unit))
                {
                    if (msgSem->IsLooseFormation() == loose)
                    {
                        list.Set(i, false);
                        continue;
                    }
                    else
                    {
                        goto SendLooseFormationForContinue;
                    }
                }
            }
        }

        {
            AI::Semaphore s = unit->GetSemaphore();
            if (ApplyLooseFormation(s, loose) == s)
            {
                list.Set(i, false);
                continue;
            }
        }

    SendLooseFormationForContinue:
        continue;
    }

    if (leader && !list.IsEmpty())
    {
        GetRadio().Transmit(new RadioMessageLooseFormation(this, list, loose), GetCenter()->GetLanguage());
    }
}

void AIGroup::SendOpenFire(OpenFireState open, PackedBoolArray list)
{
    AIUnit* leader = Leader();
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        if (!list.Get(i))
        {
            continue;
        }
        AIUnit* unit = UnitWithID(i + 1);
        if (!unit)
        {
            list.Set(i, false);
            continue;
        }

        if (!leader || unit == leader)
        {
            AI::Semaphore s = unit->GetSemaphore();
            s = ApplyOpenFire(s, open);
            unit->SetSemaphore(s);
            list.Set(i, false);
            continue;
        }

        // check radio channel
        int index = INT_MAX;
        while (true)
        {
            RadioMessage* msg = GetRadio().FindPrevMessage(RMTOpenFire, index);
            if (!msg)
            {
                break;
            }
            AI_ERROR(dynamic_cast<RadioMessageOpenFire*>(msg));
            RadioMessageOpenFire* msgSem = static_cast<RadioMessageOpenFire*>(msg);
            AI_ERROR(msgSem);
            AI_ERROR(msgSem->GetFrom() == this);
            if (msgSem->IsTo(unit))
            {
                if (msgSem->GetOpenFireState() == open)
                {
                    list.Set(i, false);
                    goto SendOpenFireForContinue;
                }
                else
                {
                    msgSem->DeleteTo(unit);
                    break;
                }
            }
            else if (msgSem->GetOpenFireState() == open)
            {
                msgSem->AddTo(unit);
                list.Set(i, false);
                goto SendOpenFireForContinue;
            }
        }

        {
            RadioMessage* msg = GetRadio().GetActualMessage();
            if (msg && msg->GetType() == RMTOpenFire)
            {
                AI_ERROR(dynamic_cast<RadioMessageOpenFire*>(msg));
                RadioMessageOpenFire* msgSem = static_cast<RadioMessageOpenFire*>(msg);
                AI_ERROR(msgSem);
                AI_ERROR(msgSem->GetFrom() == this);
                if (msgSem->IsTo(unit))
                {
                    if (msgSem->GetOpenFireState() == open)
                    {
                        list.Set(i, false);
                        continue;
                    }
                    else
                    {
                        goto SendOpenFireForContinue;
                    }
                }
            }
        }

        {
            AI::Semaphore s = unit->GetSemaphore();
            if (ApplyOpenFire(s, open) == s)
            {
                list.Set(i, false);
                continue;
            }
        }

    SendOpenFireForContinue:
        continue;
    }

    if (leader && !list.IsEmpty())
    {
        GetRadio().Transmit(new RadioMessageOpenFire(this, list, open), GetCenter()->GetLanguage());
    }
}

Target* AIGroup::EngageSent(AIUnit* unit) const
{
    Target* tgt = unit->GetEngageTarget();
    Target* defTgt = unit->GetTargetAssigned();

    int index = INT_MAX;
    while (true)
    {
        RadioMessage* msg = GetRadio().FindPrevMessage(RMTTarget, index);
        if (!msg)
        {
            break;
        }
        AI_ERROR(dynamic_cast<RadioMessageTarget*>(msg));
        RadioMessageTarget* msgTgt = static_cast<RadioMessageTarget*>(msg);
        if (!msgTgt->IsTo(unit))
        {
            continue;
        }
        // we have some message to this unit
        if (msgTgt->GetTarget())
        {
            defTgt = msgTgt->GetTarget();
        }
        if (msgTgt->GetEngage())
        {
            tgt = defTgt;
        }
    }

    // check actual message
    RadioMessage* msg = GetRadio().GetActualMessage();
    if (msg && msg->GetType() == RMTTarget)
    {
        AI_ERROR(dynamic_cast<RadioMessageTarget*>(msg));
        RadioMessageTarget* msgTgt = static_cast<RadioMessageTarget*>(msg);
        if (msgTgt->IsTo(unit))
        {
            if (msgTgt->GetTarget())
            {
                defTgt = msgTgt->GetTarget();
            }
            if (msgTgt->GetEngage())
            {
                tgt = defTgt;
            }
        }
    }

    return tgt;
}

Target* AIGroup::FireSent(AIUnit* unit) const
{
    Target* tgt = unit->GetEnableFireTarget();
    Target* defTgt = unit->GetTargetAssigned();

    int index = INT_MAX;
    while (true)
    {
        RadioMessage* msg = GetRadio().FindPrevMessage(RMTTarget, index);
        if (!msg)
        {
            break;
        }
        AI_ERROR(dynamic_cast<RadioMessageTarget*>(msg));
        RadioMessageTarget* msgTgt = static_cast<RadioMessageTarget*>(msg);
        if (!msgTgt->IsTo(unit))
        {
            continue;
        }
        // we have some message to this unit
        if (msgTgt->GetTarget())
        {
            defTgt = msgTgt->GetTarget();
        }
        if (msgTgt->GetFire())
        {
            tgt = defTgt;
        }
    }

    // check actual message
    RadioMessage* msg = GetRadio().GetActualMessage();
    if (msg && msg->GetType() == RMTTarget)
    {
        AI_ERROR(dynamic_cast<RadioMessageTarget*>(msg));
        RadioMessageTarget* msgTgt = static_cast<RadioMessageTarget*>(msg);
        if (msgTgt->IsTo(unit))
        {
            if (msgTgt->GetTarget())
            {
                defTgt = msgTgt->GetTarget();
            }
            if (msgTgt->GetFire())
            {
                tgt = defTgt;
            }
        }
    }

    return tgt;
}

Target* AIGroup::TargetSent(AIUnit* unit) const
{
    Target* tgt = unit->GetTargetAssigned();

    int index = INT_MAX;
    while (true)
    {
        RadioMessage* msg = GetRadio().FindPrevMessage(RMTTarget, index);
        if (!msg)
        {
            break;
        }
        AI_ERROR(dynamic_cast<RadioMessageTarget*>(msg));
        RadioMessageTarget* msgTgt = static_cast<RadioMessageTarget*>(msg);
        if (!msgTgt->IsTo(unit))
        {
            continue;
        }
        // we have some message to this unit
        if (msgTgt->GetTarget())
        {
            tgt = msgTgt->GetTarget();
        }
    }

    // check actual message
    RadioMessage* msg = GetRadio().GetActualMessage();
    if (msg && msg->GetType() == RMTTarget)
    {
        AI_ERROR(dynamic_cast<RadioMessageTarget*>(msg));
        RadioMessageTarget* msgTgt = static_cast<RadioMessageTarget*>(msg);
        if (msgTgt->IsTo(unit))
        {
            if (msgTgt->GetTarget())
            {
                tgt = msgTgt->GetTarget();
            }
        }
    }

    return tgt;
}

void AIGroup::SendTarget(Target* target, bool engage, bool fire, PackedBoolArray list, bool silent)
{
    AIUnit* leader = Leader();
    if (!leader)
    {
        return;
    }

    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        if (!list.Get(i))
        {
            continue;
        }
        AIUnit* unit = UnitWithID(i + 1);
        if (!unit)
        {
            list.Set(i, false);
            continue;
        }

        if (target)
        {
            EntityAI* tgtAI = target->idExact;
            if (tgtAI == unit->GetVehicleIn() || tgtAI == unit->GetPerson())
            {
                // unit cannot target itself
                list.Set(i, false);
                continue;
            }
        }

        // check if unit is already doing what we tell it
        if (TargetSent(unit) == target && (!fire || target == FireSent(unit)) &&
            (!engage || target == EngageSent(unit)))
        {
            list.Set(i, false);
            continue;
        }
        if (unit == leader || silent)
        {
            // leader - direct processing
            if (!target)
            {
                target = unit->GetTargetAssigned();
            }
            else
            {
                unit->AssignTarget(target);
            }
            if (target)
            {
                if (fire)
                {
                    unit->EnableFireTarget(target);
                }
                if (engage)
                {
                    unit->EngageTarget(target);
                }
            }

            list.Set(i, false);
            continue;
        }

        // check radio channel
        int index = INT_MAX;
        while (true)
        {
            RadioMessage* msg = GetRadio().FindPrevMessage(RMTTarget, index);
            if (!msg)
            {
                break;
            }
            AI_ERROR(dynamic_cast<RadioMessageTarget*>(msg));
            RadioMessageTarget* msgTgt = static_cast<RadioMessageTarget*>(msg);
            AI_ERROR(msgTgt);
            AI_ERROR(msgTgt->GetFrom() == this);
            if (msgTgt->IsTo(unit))
            {
                // we have found some target command to same unit
                // if parameters wanted are subset of actual parameters
                // we may consider target transmitted
                if (target && msgTgt->GetTarget() != target)
                {
                    // we may remove the old message - it is being superseded
                    msgTgt->DeleteTo(unit);
                    break;
                }
                // we might want to change target parameters
                // we transmit either no target command (adding par to last target)
                // or same target command
                // this means: !target || msgTgt->GetTarget()==target
                // see if above -
                if (msgTgt->IsOnlyTo(unit))
                {
                    // message only to given unit - we may add new parameters
                    if (fire)
                    {
                        msgTgt->SetFire(true);
                    }
                    if (engage)
                    {
                        msgTgt->SetEngage(true);
                    }
                    list.Set(i, false);
                    goto SendTargetForContinue;
                }
                // check if some target (even to multiple units)
                // that whould be superset of current target is on the way
                if (msgTgt->GetEngage() >= engage && msgTgt->GetFire() >= fire)
                {
                    list.Set(i, false);
                    goto SendTargetForContinue;
                }
            }
            else if (msgTgt->GetTarget() == target && msgTgt->GetEngage() == engage && msgTgt->GetFire() == fire)
            {
                // some message with same arguments to other unit is on the way
                // we add unit to address list
                msgTgt->AddTo(unit);
                list.Set(i, false);
                goto SendTargetForContinue;
            }
        }

    SendTargetForContinue:
        continue;
    }

    if (!list.IsEmpty())
    {
        GetRadio().Transmit(new RadioMessageTarget(this, list, target, engage, fire), GetCenter()->GetLanguage());
    }
}

void AIGroup::SendState(RadioMessageState* msg, bool silent)
{
    // usually called with new
    // if message is not queued we have the only reference to the message
    Ref<RadioMessageState> ref = msg;
    AIUnit* leader = Leader();
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* unit = UnitWithID(i + 1);
        if (!unit)
        {
            continue;
        }
        if (!msg->IsTo(unit))
        {
            continue;
        }

        if (!leader || unit == leader || silent)
        {
            // leader command is executed directly
            Ref<RadioMessageState> temp = msg->Clone();
            temp->ClearTo();
            temp->AddTo(unit);
            temp->Transmitted();
            msg->DeleteTo(unit);
            continue;
        }
    }
    if (leader && msg->IsToSomeone())
    {
        GetRadio().Transmit(msg, GetCenter()->GetLanguage());
    }
}

void AIGroup::SendReportStatus(PackedBoolArray list)
{
    AI_ERROR(Leader());
    if (!Leader())
    {
        return;
    }

    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        if (!list.Get(i))
        {
            continue;
        }
        AIUnit* unit = UnitWithID(i + 1);
        if (!unit)
        {
            list.Set(i, false);
            continue;
        }

        if (unit == Leader())
        {
            list.Set(i, false);
            continue;
        }

        int index = INT_MAX;
        while (true)
        {
            RadioMessage* msg = GetRadio().FindPrevMessage(RMTReportStatus, index);
            if (!msg)
            {
                break;
            }
            AI_ERROR(dynamic_cast<RadioMessageReportStatus*>(msg));
            RadioMessageReportStatus* msgRep = static_cast<RadioMessageReportStatus*>(msg);
            AI_ERROR(msgRep);
            AI_ERROR(msgRep->GetFrom() == this);
            if (msgRep->IsTo(unit))
            {
                list.Set(i, false);
                goto SendReportStatusForContinue;
            }
        }

        {
            RadioMessage* msg = GetRadio().GetActualMessage();
            if (msg && msg->GetType() == RMTReportStatus)
            {
                AI_ERROR(dynamic_cast<RadioMessageReportStatus*>(msg));
                RadioMessageReportStatus* msgRep = static_cast<RadioMessageReportStatus*>(msg);
                AI_ERROR(msgRep);
                AI_ERROR(msgRep->GetFrom() == this);
                if (msgRep->IsTo(unit))
                {
                    list.Set(i, false);
                    continue;
                }
            }
        }

    SendReportStatusForContinue:
        continue;
    }

    if (!list.IsEmpty())
    {
        GetRadio().Transmit(new RadioMessageReportStatus(this, list), GetCenter()->GetLanguage());
    }
}

void AIGroup::SendObjectDestroyed(AIUnit* sender, const VehicleType* type)
{
    if (!sender)
    {
        return;
    }

    GetRadio().Transmit(new RadioMessageObjectDestroyed(sender, this, type), GetCenter()->GetLanguage());
}

void AIGroup::SendContact(AIUnit* sender)
{
    if (!sender)
    {
        return;
    }

    GetRadio().Transmit(new RadioMessageContact(sender, this), GetCenter()->GetLanguage());
}

void AIGroup::SendUnitDown(AIUnit* sender, AIUnit* down)
{
    // check if report already exists
    // check actual message
    RadioMessage* msg = GetRadio().GetActualMessage();
    if (msg && msg->GetSender() && msg->GetSender()->GetLifeState() == AIUnit::LSAlive &&
        msg->GetType() == RMTUnitKilled)
    {
        RadioMessageUnitKilled* msgT = static_cast<RadioMessageUnitKilled*>(msg);
        if (msgT && msgT->GetWhoKilled() == down)
        {
            return;
        }
    }
    // check message queue
    int index = INT_MAX;
    while (true)
    {
        RadioMessage* msg = GetRadio().FindPrevMessage(RMTUnitKilled, index);
        if (!msg)
        {
            break;
        }

        if (!msg->GetSender())
        {
            continue;
        }
        if (msg->GetSender()->GetLifeState() != AIUnit::LSAlive)
        {
            continue;
        }
        RadioMessageUnitKilled* msgT = static_cast<RadioMessageUnitKilled*>(msg);
        if (msgT->GetWhoKilled() == down)
        {
            return;
        }
    }

    if (!GetReportedDown(down))
    {
        GetRadio().Transmit(new RadioMessageUnitKilled(sender, down), GetCenter()->GetLanguage());
    }
    else
    {
        // it is already suppossed to be dead
        // react immediatelly
        if (down->GetLifeState() == AIUnit::LSDead)
        {
            AISubgroup* subgrp = down->GetSubgroup();
            if (subgrp)
            {
                subgrp->ReceiveAnswer(down, AI::UnitDestroyed);
            }
        }
    }
}

void AIGroup::SendUnderFire(AIUnit* sender)
{
    if (!sender)
    {
        return;
    }

    GetRadio().Transmit(new RadioMessageUnderFire(sender, this), GetCenter()->GetLanguage());
}

void AIGroup::ReportFire(AIUnit* who, bool status)
{
    // check radio for messages of the same type
    int index = INT_MAX;
    while (true)
    {
        RadioMessage* msg = GetRadio().FindPrevMessage(RMTFireStatus, index);
        if (!msg)
        {
            break;
        }
        GetRadio().Cancel(msg);
    }

    GetRadio().Transmit(new RadioMessageFireStatus(who, status), GetCenter()->GetLanguage());
}

void AIGroup::SendClear(AIUnit* sender)
{
    if (!sender)
    {
        return;
    }

    GetRadio().Transmit(new RadioMessageClear(sender, this), GetCenter()->GetLanguage());
}

void AIGroup::SendGetOut(PackedBoolArray list)
{
    Command cmd;
    cmd._message = Command::GetOut;
    cmd._context = Command::CtxAuto;
    SendCommand(cmd, list);
}

void AIGroup::SendAutoCommandToUnit(Command& cmd, AIUnit* unit, bool join, bool channelCenter)
{
    AI_ERROR(unit);
    if (!unit || !unit->IsUnit())
    {
        return;
    }
    AISubgroup* subgrp = unit->GetSubgroup();
    AI_ERROR(subgrp);
    if (!subgrp)
    {
        return;
    }

    PackedBoolArray list;
    list.Set(unit->ID() - 1, true);

    if (join)
    {
        cmd._joinToSubgroup = subgrp;
        cmd._context = Command::CtxAuto;
    }
    else
    {
        cmd._context = Command::CtxAuto;
    }

    AI_ERROR(cmd._message != Command::NoCommand);
    SendCommand(cmd, list, channelCenter);
}

void AIGroup::IssueAutoCommand(Command& cmd, AIUnit* unit)
{
    AI_ERROR(unit);
    if (!unit || !unit->IsUnit())
    {
        return;
    }
    AISubgroup* subgrp = unit->GetSubgroup();
    AI_ERROR(subgrp);
    if (!subgrp)
    {
        return;
    }

    PackedBoolArray list;
    list.Set(unit->ID() - 1, true);

    cmd._joinToSubgroup = subgrp;
    cmd._context = Command::CtxAuto;
    cmd._id = _nextCmdId++;

    AI_ERROR(cmd._message != Command::NoCommand);

    IssueCommand(cmd, list);
}
void AIGroup::NotifyAutoCommand(Command& cmd, AIUnit* unit)
{
    // unit is not able to issue any commands when it is not alive
    if (unit->GetLifeState() != AIUnit::LSAlive)
    {
        return;
    }
    IssueAutoCommand(cmd, unit);
    GetRadio().Transmit(new RadioMessageNotifyCommand(unit, this, cmd), GetCenter()->GetLanguage());
}

void AIGroup::ReceiveUnitStatus(AIUnit* unit, Answer answer)
{
    AI_ERROR(unit->GetGroup() == this);
    if (unit->GetGroup() != this)
    {
        return;
    }
    int id = unit->ID();
    switch (answer)
    {
        case HealthCritical:
            _healthState[id - 1] = AIUnit::RSCritical;
            break;
        case DammageCritical:
            _dammageState[id - 1] = AIUnit::RSCritical;
            break;
        case FuelCritical:
            _fuelState[id - 1] = AIUnit::RSCritical;
            break;
        case FuelLow:
            _fuelState[id - 1] = AIUnit::RSLow;
            break;
        case AmmoCritical:
            _ammoState[id - 1] = AIUnit::RSCritical;
            break;
        case AmmoLow:
            _ammoState[id - 1] = AIUnit::RSLow;
            break;
        // Resource recovered -- clear the cached "reported" state so CheckHealth()/
        // CheckFuel()/CheckAmmo()/CheckDammage() stop re-issuing commands and radio
        // chatter for a unit that's no longer in trouble (was previously a one-way
        // ratchet: nothing ever reset these back to RSNormal on recovery).
        case HealthOk:
            _healthState[id - 1] = AIUnit::RSNormal;
            break;
        case DammageOk:
            _dammageState[id - 1] = AIUnit::RSNormal;
            break;
        case FuelOk:
            _fuelState[id - 1] = AIUnit::RSNormal;
            break;
        case AmmoOk:
            _ammoState[id - 1] = AIUnit::RSNormal;
            break;
    }
}

void AIGroup::ReceiveAnswer(AISubgroup* from, Answer answer)
{
    if (!from)
    {
        return;
    }

#if LOG_COMM
    Log("Receive answer: Group %s: From subgroup %s: Answer %d)", (const char*)GetDebugName(),
        (const char*)from->GetDebugName(), answer);
#endif

    switch (answer)
    {
        case AI::CommandCompleted:
        {
        }
        break;
        case AI::CommandFailed:
        case AI::SubgroupDestinationUnreacheable:
        {
        }
        break;
    }
}

// Communication with center
void AIGroup::SendAnswer(Answer answer)
{
#if LOG_COMM
    Log("Send answer: Group %s: Answer %d", (const char*)GetDebugName(), answer);
#endif

    if (_center)
    {
        _center->ReceiveAnswer(this, answer);
    }
}

void AIGroup::SendRadioReport(ReportSubject subject, Target& target)
{
    if (NUnits() <= 1)
    {
        return;
    }

    // select any unit as reporting
    AIUnit* from = target.idSensor ? target.idSensor->CommanderUnit() : nullptr;
    // find first unit that is alive
    if (!from)
    {
        for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
        {
            AIUnit* unit = _units[i];
            if (!unit || unit->GetLifeState() != AIUnit::LSAlive)
            {
                from = unit;
            }
        }
    }

    if (!from || from->GetLifeState() != AIUnit::LSAlive)
    {
        return;
    }

    AI_ERROR(subject == ReportNew);
    if (ReportSent(subject, target.type))
    {
        return;
    }

    GetRadio().Transmit(new RadioMessageReportTarget(from, this, subject, target), GetCenter()->GetLanguage());
}

void AIGroup::SendReport(ReportSubject subject, Target& target)
{
    if (_center)
    {
        // send report about units only when leader is alive
        AIUnit* leader = Leader();
        if (leader && leader->GetLifeState() == AIUnit::LSAlive && target.IsKnownBy(leader))
        {
            _center->ReceiveReport(this, subject, target);
        }
    }
}

void AIGroup::UnassignVehicle(Transport* veh)
{
    if (!veh)
    {
        return;
    }
    // remove from list
    for (int i = 0; i < _vehicles.Size(); i++)
    {
        if (_vehicles[i] == veh)
        {
            _vehicles.Delete(i);
            break;
        }
    }
    veh->AssignGroup(nullptr);

    AIUnit* unit;
    unit = veh->GetDriverAssigned();
    if (unit)
    {
        // faster than unit->UnassignVehicle
        unit->_vehicleAssigned = nullptr;
        veh->AssignDriver(nullptr);
    }
    unit = veh->GetCommanderAssigned();
    if (unit)
    {
        // faster than unit->UnassignVehicle
        unit->_vehicleAssigned = nullptr;
        veh->AssignCommander(nullptr);
    }
    unit = veh->GetGunnerAssigned();
    if (unit)
    {
        // faster than unit->UnassignVehicle
        unit->_vehicleAssigned = nullptr;
        veh->AssignGunner(nullptr);
    }
    for (int i = 0; i < veh->NCargoAssigned(); i++)
    {
        // faster than unit->UnassignVehicle
        unit = veh->GetCargoAssigned(i);
        if (unit)
        {
            unit->_vehicleAssigned = nullptr;
        }
    }
    veh->EmptyCargo();
}

// Implementation

void AIGroup::AssignVehicles()
{
    // if there is nothing to assign, do nothing
    if (_vehicles.Size() <= 0)
    {
        return;
    }

    AUTO_STATIC_ARRAY(AIUnit*, soldiers, MAX_UNITS_PER_GROUP)
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* unit = _units[i];
        if (!unit)
        {
            continue;
        }
        Transport* veh = unit->VehicleAssigned();
        if (veh)
        {
            if (veh->IsAbleToMove() && veh->GetDriverAssigned())
            {
                continue;
            }
            // we need to remove this vehicle
        }

        float exp = unit->GetPerson()->GetExperience();
        for (int j = 0; j < soldiers.Size(); j++)
        {
            AIUnit* with = soldiers[j];
            if (exp >= with->GetPerson()->GetExperience())
            {
                soldiers.Insert(j, unit);
                goto NextUnit;
            }
        }
        soldiers.Add(unit);
    NextUnit:
        continue;
    }

    if (soldiers.Size() <= 0)
    {
        return;
    }

    for (int i = 0; i < _vehicles.Size(); i++)
    {
        Transport* veh = _vehicles[i];
        if (!veh || !veh->IsPossibleToGetIn())
        {
            continue;
        }

        AIUnit* commander = nullptr;
        AIUnit* driver = nullptr;
        AIUnit* gunner = nullptr;
        if (veh->GetType()->HasCommander() && !veh->GetCommanderAssigned())
        {
            for (int j = 0; j < soldiers.Size(); j++)
            {
                AIUnit* unit = soldiers[j];
                if (veh->QCanIGetInCommander(unit->GetPerson()))
                {
                    commander = unit;
                    soldiers.Delete(j);
                    break;
                }
            }
        }
        if (veh->GetType()->HasGunner() && !veh->GetGunnerAssigned())
        {
            for (int j = 0; j < soldiers.Size(); j++)
            {
                AIUnit* unit = soldiers[j];
                if (veh->QCanIGetInGunner(unit->GetPerson()))
                {
                    gunner = unit;
                    soldiers.Delete(j);
                    break;
                }
            }
            if (!gunner)
            {
                // no suitable gunner found - use commander if possible
                gunner = commander, commander = nullptr;
            }
        }

        if (veh->GetType()->HasDriver() && !veh->GetDriverAssigned())
        {
            for (int j = 0; j < soldiers.Size(); j++)
            {
                AIUnit* unit = soldiers[j];
                if (veh->QCanIGetIn(unit->GetPerson()))
                {
                    driver = unit;
                    soldiers.Delete(j);
                    break;
                }
            }

            if (!driver)
            {
                // no suitable driver found - use gunner or commander if possible
                driver = commander, commander = nullptr;
                if (!driver)
                {
                    driver = gunner, gunner = nullptr;
                }
            }
        }

        // assign most needed positions first

        if (driver)
        {
            driver->AssignAsDriver(veh);
        }
        if (gunner)
        {
            gunner->AssignAsGunner(veh);
        }
        if (commander)
        {
            commander->AssignAsCommander(veh);
        }

        if (soldiers.Size() <= 0)
        {
            return;
        }
    }

    for (int i = 0; i < _vehicles.Size(); i++)
    {
        Transport* veh = _vehicles[i];
        if (!veh || !veh->IsAbleToMove())
        {
            continue;
        }

        int nFree = veh->GetMaxManCargo() - veh->NCargoAssigned();
        if (nFree > 0)
        {
            for (int j = 0; j < soldiers.Size();)
            {
                AIUnit* unit = soldiers[j];
                if (veh->QCanIGetInCargo(unit->GetPerson()))
                {
                    soldiers.Delete(j);
                    unit->AssignAsCargo(veh);
                    if (--nFree <= 0)
                    {
                        break;
                    }
                }
                else
                {
                    j++;
                }
            }

            if (soldiers.Size() <= 0)
            {
                return;
            }
        }
    }
}

} // namespace Poseidon
