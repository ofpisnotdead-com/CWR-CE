// Commands Heal, Repair, Refuel, Rearm, TakeWeapon, TakeMagazine FSM

static Vector3 GetSupplyPoint(VehicleSupply* veh)
{
    const VehicleType* type = veh->GetType();
    return veh->PositionModelToWorld(type->GetSupplyPoint());
}

static Vector3 GetActionPoint(VehicleSupply* veh, const Command* cmd)
{
    switch (cmd->_action)
    {
        case ATLadderOnUp:
        case ATLadderOnDown:
        case ATLadderUp:
        case ATLadderDown:
        {
            // check corresponding ladder point
            Building* house = dyn_cast<Building>(veh);
            if (!house)
            {
                break;
            }
            Vector3 pos = house->GetLadderPos(cmd->_param, cmd->_param2 != 0);
            return pos;
        }
        break;
    }
    return GetSupplyPoint(veh);
}

static Vector3 GetSupplyPointFar(VehicleSupply* veh)
{
    const VehicleType* type = veh->GetType();
    float distance = veh->GetRadius() * 1.4 + 3.5;
    Vector3 offset = type->GetSupplyPoint().Normalized() * distance;
    Vector3 res = veh->PositionModelToWorld(offset);
    res[1] = GLandscape->RoadSurfaceYAboveWater(res[0], res[2]);

    return res;
}

static Vector3 GetActionPointFar(VehicleSupply* veh, const Command* cmd)
{
    Vector3 res = GetActionPoint(veh, cmd);
    Vector3 pos = veh->Position();

    float distance = veh->GetRadius() * 1.4 + 3.5;
    Vector3 norm = res - pos;
    res = norm.Normalized() * distance + pos;

    res[1] = GLandscape->RoadSurfaceYAboveWater(res[0], res[2]);
    return res;
}

enum SupplyFSMStates
{
    SSupplyInit,
    SSupplyAlloc,
    SSupplyMove,
    SSupplyDirect,
    SSupplySupply,
    SSupplySucceed,
    SSupplyFail
};

static void ReportStatus(AIUnit* unit, Command::Message msg)
{
    AIGroup* grp = unit->GetGroup();
    if (!grp)
    {
        return;
    }
    switch (msg)
    {
        case Command::Heal:
            grp->SetHealthStateReported(unit, AIUnit::RSNormal);
            break;
        case Command::Rearm:
            grp->SetAmmoStateReported(unit, AIUnit::RSNormal);
            break;
        case Command::Repair:
            grp->SetDammageStateReported(unit, AIUnit::RSNormal);
            break;
        case Command::Refuel:
            grp->SetFuelStateReported(unit, AIUnit::RSNormal);
            break;
    }
}

static Command::Message ActionToCommand(UIActionType action)
{
    switch (action)
    {
        case ATHeal:
            return Command::Heal;
        case ATRepair:
            return Command::Repair;
        case ATRefuel:
            return Command::Refuel;
        case ATRearm:
            return Command::Rearm;
            //		case ATTakeWeapon: return  Command::Rearm;
            //		case ATTakeMagazine: return  Command::Rearm;
        default:
            return Command::Action;
    }
}

/*
static void ReportStatus(AIUnit *unit,UIActionType action)
{
    Command::Message msg = ActionToCommand(action);
    ReportStatus(unit,msg);
}
*/

//
static bool CheckAbleToRearm(EntityAI* veh, VehicleSupply* transport)
{
    // adapted from Man::Supply, action == ATRearm
    Person* man = dyn_cast<Person>(veh);

    // check which magazines we need
    const MuzzleType* muzzle1 = nullptr;
    const MuzzleType* muzzle2 = nullptr;
    int slots1 = 0, slots2 = 0, slots3 = 0;
    man->CheckAmmo(muzzle1, muzzle2, slots1, slots2, slots3);
    for (int i = 0; i < transport->NMagazines();)
    {
        Ref<const Magazine> magazine = transport->GetMagazine(i);
        if (!magazine || magazine->_ammo == 0)
        {
            i++;
            continue;
        }
        if (transport->IsMagazineUsed(magazine))
        {
            i++;
            continue;
        }
        // check if magazine can be used
        const MagazineType* type = magazine->_type;
        int slots = GetItemSlotsCount(type->_magazineType);

        bool add = false;
        if (muzzle1 && muzzle1->CanUse(type))
        {
            if (slots <= slots1)
            {
                slots1 -= slots;
                add = true;
            }
        }
        else if (muzzle2 && muzzle2->CanUse(type))
        {
            if (slots <= slots2)
            {
                slots2 -= slots;
                add = true;
            }
        }
        else if (man->IsMagazineUsable(type))
        {
            if (slots <= slots3)
            {
                slots3 -= slots;
                add = true;
            }
        }
        if (add)
        {
            // we are able to take magazine
            return true;
        }
        else
        {
            i++;
        }
    }
    return false;
}

static bool CheckSupplyDone(AISubgroupContext* context)
{
    AISubgroup* subgrp = context->_subgroup;
    Command* cmd = context->_task;

    EntityAI* veh = subgrp->Leader()->GetVehicle();
    AI_ERROR(veh);

    VehicleSupply* transport = dyn_cast<VehicleSupply, Object>(cmd->_target);
    auto finish = [&](int state, bool clearPlan, bool reportStatus)
    {
        if (clearPlan)
            subgrp->ClearPlan();
        if (transport && transport->GetAllocSupply() == veh)
            transport->SetAllocSupply(nullptr);
        if (reportStatus)
            ReportStatus(subgrp->Leader(), cmd->_message);
        context->_fsm->SetState(state, context);
        return true;
    };

    if (Glob.time > cmd->_time)
    { // timeout
        return finish(SSupplyFail, true, false);
    }

    // wait until we are fully filled or source is exhausted
    //	if (!transport || transport->GetTotalDammage() >= 1.0)
    if (!transport)
    {
        context->_fsm->SetState(SSupplyFail, context);
        return true;
    }

    Command::Message msg = cmd->_message;
    if (msg == Command::Action)
    {
        msg = ActionToCommand(cmd->_action);
    }

    switch (msg)
    {
        case Command::Heal:
            if (veh->NeedsAmbulance() <= 0.01)
            {
                return finish(SSupplySucceed, false, true);
            }
            if (!transport->GetType()->IsAttendant() || transport->IsDammageDestroyed())
            {
                return finish(SSupplyFail, false, true);
            }
            return false;
        case Command::Repair:
            if (veh->NeedsRepair() <= 0.05)
            {
                return finish(SSupplySucceed, false, true);
            }
            if (transport->GetRepairCargo() <= 10 || transport->IsDammageDestroyed())
            {
                return finish(SSupplyFail, false, true);
            }
            return false;
        case Command::Refuel:
            if (veh->NeedsRefuel() <= 0.01)
            {
                return finish(SSupplySucceed, false, true);
            }
            if (transport->GetFuelCargo() <= 10 || transport->IsDammageDestroyed())
            {
                return finish(SSupplyFail, false, true);
            }
            return false;
        case Command::Rearm:
            if (subgrp->Leader()->IsFreeSoldier())
            {
                // check if we are able to take something from the supply
                if (veh->NeedsInfantryRearm() <= 0.01 || !CheckAbleToRearm(veh, transport))
                {
                    return finish(SSupplySucceed, false, true);
                }
            }
            else
            {
                if (veh->NeedsRearm() <= 0.01)
                {
                    return finish(SSupplySucceed, false, true);
                }
                if (transport->GetAmmoCargo() <= 10 || transport->IsDammageDestroyed())
                {
                    return finish(SSupplyFail, false, true);
                }
            }
            return false;
            /*
                    case Command::TakeWeapon:
                        {
                            Ref<WeaponType> weapon = WeaponTypes.New(cmd->_param3);
                            for (int i=0; i<veh->NWeaponSystems(); i++)
                            {
                                if (veh->GetWeaponSystem(i) == weapon)
                                {
                                    context->_fsm->SetState(SSupplySucceed, context);
                                    UNALLOC(); // unalloc supply
                                    return true;
                                }
                            }
                        }
                        return false;
                    case Command::TakeMagazine:
                        {
                            int creator = cmd->_param;
                            int id = cmd->_param2;
                            for (int i=0; i<veh->NMagazines(); i++)
                            {
                                const Magazine *magazine = veh->GetMagazine(i);
                                if (magazine->_creator == creator && magazine->_id == id)
                                {
                                    context->_fsm->SetState(SSupplySucceed, context);
                                    UNALLOC(); // unalloc supply
                                    return true;
                                }
                            }
                        }
                        return false;
            */
        case Command::Action:
            return false;
        default:
            Fail("Command");
            return finish(SSupplyFail, false, false);
    }
}

static bool SupplyDoneImmediatelly(AISubgroupContext* context)
{
    Command* cmd = context->_task;
    Command::Message msg = cmd->_message;
    if (msg == Command::Action)
    {
        msg = ActionToCommand(cmd->_action);
    }

    switch (msg)
    {
        case Command::Heal:
        case Command::Refuel:
        case Command::Repair:
            return false;
        case Command::Rearm:
        {
            AISubgroup* subgrp = context->_subgroup;
            AI_ERROR(subgrp);
            AIUnit* leader = subgrp->Leader();
            AI_ERROR(leader);
            return leader->IsFreeSoldier();
        }
        default:
            return true;
    }
}

static void SupplyInit(AISubgroupContext* context)
{
    STATE_PREFIX
    LogFSM(subgrp, "Supply", "INIT");

    if (!subgrp->HasAI())
    {
        VehicleSupply* transport = dyn_cast<VehicleSupply, Object>(cmd->_target);
        if (transport)
        {
            cmd->_destination = GetSupplyPoint(transport);
        }
        return;
    }

    subgrp->SetRefreshTime(TIME_MAX);
    subgrp->DoNotGo();
}

static void CheckSupplyInit(AISubgroupContext* context)
{
    STATE_PREFIX

    if (!subgrp->HasAI())
    {
        context->_fsm->SetState(SSupplySupply, context);
    }
    else
    {
        context->_fsm->SetState(SSupplyMove, context);
        subgrp->ClearPlan();
    }
}

static void SupplyAlloc(AISubgroupContext* context)
{
    STATE_PREFIX
    if (!subgrp->HasAI())
    {
        return;
    }

    LogFSM(subgrp, "Supply", "ALLOC");

    // stop and wait
    subgrp->Stop();
}

const float SupplyWaitDistance = 70;

static void CheckSupplyAlloc(AISubgroupContext* context)
{
    STATE_PREFIX

    if (!subgrp->HasAI())
    {
        context->_fsm->SetState(SSupplySupply, context);
        return;
    }
    if (CheckSupplyDone(context))
    {
        return;
    }

    EntityAI* vehicle = subgrp->Leader()->GetVehicle();

    VehicleSupply* transport = dyn_cast<VehicleSupply, Object>(cmd->_target);
    AI_ERROR(transport); // checked in CheckSupplyDone

    // if supply is allocated, we cannot go direct
    EntityAI* alloc = transport ? transport->GetAllocSupply() : nullptr;
    if (!alloc || alloc == vehicle)
    {
        // start moving - supply is free
        context->_fsm->SetState(SSupplyMove, context);
        return;
    }

    // if supply is far (moved), we can get nearer
    if (transport->Position().Distance2(vehicle->Position()) > Square(SupplyWaitDistance * 1.5))
    {
        context->_fsm->SetState(SSupplyMove, context);
        return;
    }
}

static bool IsDropAction(Command* cmd)
{
    return cmd->_message == Command::Action && (cmd->_action == ATDropWeapon || cmd->_action == ATDropMagazine) &&
           cmd->_target == nullptr;
}

static void SupplyMove(AISubgroupContext* context)
{
    STATE_PREFIX
    if (!subgrp->HasAI())
    {
        return;
    }

    LogFSM(subgrp, "Supply", "MOVE");

    // update command destination
    VehicleSupply* transport = dyn_cast<VehicleSupply, Object>(cmd->_target);
    if (transport)
    {
        cmd->_destination = GetActionPointFar(transport, cmd);
    }

    subgrp->SetRefreshTime(TIME_MAX);
    subgrp->GoPlanned(cmd->_destination);
}

static void CheckSupplyMove(AISubgroupContext* context)
{
    STATE_PREFIX

    if (!subgrp->HasAI())
    {
        context->_fsm->SetState(SSupplySupply, context);
        return;
    }
    if (IsDropAction(context->_task))
    {
        context->_fsm->SetState(SSupplySupply, context);
        return;
    }

    if (CheckSupplyDone(context))
    {
        return;
    }

    EntityAI* vehicle = subgrp->Leader()->GetVehicle();
    Vector3Val posL = vehicle->Position();
    Vector3Val posD = cmd->_destination;

    VehicleSupply* transport = dyn_cast<VehicleSupply, Object>(cmd->_target);
    AI_ERROR(transport); // checked in CheckSupplyDone

    if (transport && transport->Position().Distance2(vehicle->Position()) < Square(SupplyWaitDistance))
    {
        // if supply is allocated, we cannot go direct
        EntityAI* alloc = transport->GetAllocSupply();
        if (alloc && alloc != vehicle)
        {
            context->_fsm->SetState(SSupplyAlloc, context);
            return;
        }
        // allocate - we are quite near
        transport->SetAllocSupply(vehicle);
#if LOG_FSM
        LOG_DEBUG(AI, "{} allocated {}", (const char*)vehicle->GetDebugName(), (const char*)transport->GetDebugName());
#endif
    }

    Point3 posDNew = GetActionPointFar(transport, cmd);
    float radius = transport->GetRadius() * 1.4 + 3.5;
    if ((posL - posDNew).SquareSizeXZ() <= Square(radius))
    {
        bool ok = subgrp->AllUnitsCompleted();
        if (!ok)
        {
            Vector3 posDExact = GetActionPoint(transport, cmd);
            if ((posL - posDExact).CosAngle(posDExact - transport->Position()) > cos(0.25 * H_PI))
            {
                CollisionBuffer collision;
                GLandscape->ObjectCollision(collision, vehicle, nullptr, posL, posDExact, 0, ObjIntersectGeom);
                ok = collision.Size() == 0;
#if _ENABLE_CHEATS
                if (!ok)
                {
                    for (int i = 0; i < collision.Size(); i++)
                    {
                        const CollisionInfo& info = collision[i];

                        LOG_DEBUG(AI, "{} at {}: collision with {}", (const char*)vehicle->GetDebugName(),
                                  (const char*)transport->GetDebugName(), (const char*)info.object->GetDebugName());
                    }
                }
#endif
            }
        }
        if (ok)
        {
            context->_fsm->SetState(SSupplyDirect, context);
            subgrp->ClearPlan();
            return;
        }
    }

    float limit = 0.01 * (posL - posD).SquareSizeXZ();
    saturateMax(limit, Square(0.5 * radius));
    if ((posD - posDNew).SquareSizeXZ() > limit)
    {
        // update target position
        subgrp->GoPlanned(posDNew);
    }
}

static void SupplyDirect(AISubgroupContext* context)
{
    STATE_PREFIX
    if (!subgrp->HasAI())
    {
        return;
    }

    LogFSM(subgrp, "Supply", "DIRECT");

    VehicleSupply* transport = dyn_cast<VehicleSupply, Object>(cmd->_target);
    if (transport)
    {
        // direct go
        subgrp->GoDirect(GetSupplyPoint(transport));
    }
}

static void CheckSupplyDirect(AISubgroupContext* context)
{
    STATE_PREFIX

    if (!subgrp->HasAI())
    {
        context->_fsm->SetState(SSupplySupply, context);
        return;
    }
    if (IsDropAction(context->_task))
    {
        context->_fsm->SetState(SSupplySupply, context);
        return;
    }
    if (CheckSupplyDone(context))
    {
        return;
    }

    VehicleSupply* transport = dyn_cast<VehicleSupply, Object>(cmd->_target);
    AI_ERROR(transport); // checked in CheckSupplyDone
    // check if supply is allocated
    AIUnit* leader = subgrp->Leader();
    EntityAI* vehicle = leader->GetVehicle();

    Vector3Val posL = vehicle->Position();
    Vector3Val posD = cmd->_destination;
    Point3 posDNew = GetActionPoint(transport, cmd);
    float radius = transport->GetType()->GetSupplyRadius() + vehicle->CollisionSize() * 1.4f + 1;
    if ((posL - posDNew).SquareSizeXZ() <= Square(radius))
    {
        context->_fsm->SetState(SSupplySupply, context);
        subgrp->ClearPlan();
        return;
    }

    float limit = Square(0.5 * radius);
    if ((posD - posDNew).SquareSizeXZ() > limit)
    {
        // target moved
        context->_fsm->SetState(SSupplyMove, context);
        return;
    }
#if 1
    // if we are not moving any longer and our supply is not moving as well
    // it is dead-lock
    // we have to recover somehow
    if (transport->Speed().SquareSize() < 1 && leader->HasAI() && leader->GetPath().Size() <= 0)
    {
        if (posDNew.Distance2(vehicle->Position()) < Square(50))
        {
            LOG_DEBUG(AI, "{}: supply dead-lock recovery - OK.", (const char*)vehicle->GetDebugName());
            context->_fsm->SetState(SSupplySupply, context);
            subgrp->ClearPlan();
            return;
        }
        else
        {
            Time maxTimeout = Glob.time + 30;
            if (cmd->_time > maxTimeout)
            {
                LOG_DEBUG(AI, "{}: supply dead-lock recovery - Fail.", (const char*)vehicle->GetDebugName());
                cmd->_time = maxTimeout;
            }
        }
    }
#endif
}

static void ProcessSupply(VehicleSupply* transport, AIUnit* unit, Command* cmd)
{
    EntityAI* vehicle = unit->GetVehicle();

    UIActionType action;
    // translate message to action
    switch (cmd->_message)
    {
        default:
            Fail("Unknown action");
        case Command::Heal:
            action = ATHeal;
            break;
        case Command::Repair:
            action = ATRepair;
            break;
        case Command::Refuel:
            action = ATRefuel;
            break;
        case Command::Rearm:
            action = ATRearm;
            break;
            /*
                case Command::TakeWeapon:
                    action = ATTakeWeapon;
                    break;
                case Command::TakeMagazine:
                    action = ATTakeMagazine;
                    break;
            */
        case Command::Action:
            action = cmd->_action;
            break;
    }
    if (!vehicle->CheckActionProcessing(action, unit))
    {
        LOG_DEBUG(AI, "{} ProcessSupply {}", (const char*)vehicle->GetDebugName(),
                  transport ? (const char*)transport->GetDebugName() : "nullptr");
        UIAction uiAction;
        uiAction.type = action;
        uiAction.target = cmd->_target;
        uiAction.param = cmd->_param;
        uiAction.param2 = cmd->_param2;
        uiAction.param3 = cmd->_param3;
        uiAction.priority = 0;
        uiAction.showWindow = false;
        uiAction.hideOnUse = false;
        vehicle->StartActionProcessing(uiAction, unit);
    }
}

static void SupplySupply(AISubgroupContext* context)
{
    STATE_PREFIX
    LogFSM(subgrp, "Supply", "SUPPLY");

    if (!subgrp->HasAI())
    {
        return;
    }

    cmd->_time = Glob.time + 60;
    subgrp->Stop();

    if (IsDropAction(context->_task))
    {
        ProcessSupply(nullptr, subgrp->Leader(), cmd);
        context->_fsm->SetState(SSupplySucceed, context);
        return;
    }

    VehicleSupply* transport = dyn_cast<VehicleSupply, Object>(cmd->_target);
    if (transport)
    {
        ProcessSupply(transport, subgrp->Leader(), cmd);
        if (SupplyDoneImmediatelly(context))
        {
            // supply command succeed immediatelly
            EntityAI* veh = subgrp->Leader()->GetVehicle();

#define UNALLOC()                           \
    if (transport->GetAllocSupply() == veh) \
        transport->SetAllocSupply(nullptr);
            VehicleSupply* transport = dyn_cast<VehicleSupply, Object>(cmd->_target);
            if (transport)
            {
                UNALLOC();
            }

            context->_fsm->SetState(SSupplySucceed, context);
        }
    }
}

static void CheckSupplySupply(AISubgroupContext* context)
{
    STATE_PREFIX

    if (CheckSupplyDone(context))
    {
        return;
    }

    if (!subgrp->HasAI())
    {
        VehicleSupply* transport = dyn_cast<VehicleSupply, Object>(cmd->_target);
        if (transport)
        {
            cmd->_destination = GetSupplyPoint(transport);
        }
        return;
    }

    EntityAI* vehicle = subgrp->Leader()->GetVehicle();
    VehicleSupply* transport = dyn_cast<VehicleSupply, Object>(cmd->_target);
    AI_ERROR(transport); // checked in CheckSupplyDone
    Vector3Val posL = vehicle->Position();
    Point3 posD = GetSupplyPoint(transport);
    float radius = transport->GetType()->GetSupplyRadius() + vehicle->CollisionSize() * 1.2 + 1;

    if ((posL - posD).SquareSizeXZ() > Square(radius))
    {
        context->_fsm->SetState(SSupplyMove, context);
        subgrp->ClearPlan();
        return;
    }
    if (transport->GetSupplying() != vehicle)
    {
        // refresh supply
        ProcessSupply(transport, subgrp->Leader(), cmd);
    }
}

static void SupplyEnter(AISubgroupContext* context)
{
    STATE_PREFIX
    // if (!subgrp->HasAI()) return;

    AIUnit* leader = subgrp->Leader();
    VehicleSupply* vehInto = dyn_cast<VehicleSupply, Object>(cmd->_target);
    if (vehInto)
    {
        /*
                if (!vehInto->GetType()->IsKindOf(GWorld->Preloaded(VTypeStatic)))
                {
                    AIUnit *unit = vehInto->CommanderUnit();
                    AIGroup *grp = subgrp->GetGroup();
                    AICenter *center = grp->GetCenter();
                    if (unit && unit->GetLifeState() == AIUnit::LSAlive)
                    {
                        if (unit->GetGroup() == grp)
                        {
                            grp->GetRadio().Transmit
                            (
                                new RadioMessageAskSupply(leader, cmd->_message),
                                center->GetLanguage()
                            );
                        }
                        else
                        {
                            center->GetRadio().Transmit
                            (
                                new RadioMessageAskSupply(leader, cmd->_message),
                                center->GetLanguage()
                            );
                        }
                    }
                }
        */
        vehInto->WaitForSupply(leader);
    }
}

static void SupplyExit(AISubgroupContext* context)
{
    STATE_PREFIX
    // if (!subgrp->HasAI()) return;

    AIUnit* leader = subgrp->Leader();
    VehicleSupply* vehInto = dyn_cast<VehicleSupply, Object>(cmd->_target);
    if (vehInto)
    {
        vehInto->SupplyFinished(leader);
    }
}
