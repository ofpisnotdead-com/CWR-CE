#include <Poseidon/Core/Application.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/AI/AIRadio.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/World/Entities/Infantry/Person.hpp>

#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/IO/Serialization/ParamArchive.hpp>

#include <Random/randomGen.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>

#include <Poseidon/World/Terrain/Roads.hpp>
#include <Poseidon/Game/OperMap.hpp>

#include <Poseidon/World/Entities/Vehicles/House.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/World/Entities/Infantry/MoveActions.hpp>

#include <Poseidon/Game/UiActions.hpp>

#include <Poseidon/Foundation/Enums/EnumNames.hpp>

// Parameters

#include <Poseidon/AI/Path/AIDefs.hpp>
#include <stdio.h>
#include <string.h>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
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

using namespace Poseidon;
ActionContextBase* CreateGetInActionContext(Transport* veh, UIActionType pos);

namespace Poseidon
{
void AddDeadIdentity(RString);
}

using Poseidon::Foundation::Time;

#define DIAG_PLANNING 0
#define DIST_NOT_SEARCH 5.0F

float AIUnit::GetFeeling() const
{
    int x = toIntFloor(Position().X() * InvLandGrid);
    int z = toIntFloor(Position().Z() * InvLandGrid);
    if (!GetGroup() || !GetGroup()->GetCenter())
    {
        Fail("AI structure corrputed.");
        return 1;
    }
    float feeling = 0.0001 * GetGroup()->GetCenter()->GetExposureOptimistic(x, z);
    saturateMin(feeling, 1);
    return feeling;
}

void AIUnit::Disclose(bool discloseGroup)
{
    // no effect on player group
    if (IsPlayer())
    {
        return;
    }
    if (!IsLocal())
    {
        return;
    }

    AIGroup* grp = GetGroup();
    if (!grp)
    {
        return;
    }

    if (!grp->GetFlee())
    {
        // unit was disclosed - do something
        SetDanger();
        if (!grp->IsAnyPlayerGroup())
        {
            if (discloseGroup)
            {
                grp->Disclose(this);
            }
            if (_semaphore != SemaphoreBlue)
            {
                _semaphore = ApplyOpenFire(_semaphore, OFSOpenFire);
            }
        }
    }
    else
    {
        SetDanger(GRandGen.PlusMinus(1.0f, 0.2f));
    }
}

AIGroup* AIUnit::GetGroup() const
{
    if (!_subgroup)
    {
        return nullptr;
    }
    return _subgroup->GetGroup();
}

void AIUnit::RemoveFromSubgroup()
{
    AISubgroup* subgroup = _subgroup;
    _subgroup = nullptr;
    if (subgroup)
    {
        subgroup->UnitRemoved(this);
    }
}

void AIUnit::RemoveFromGroup()
{
    Ref<AIUnit> unit = this;
    AIGroup* group = GetGroup();
    if (group)
    {
        if (!group->IsLocal())
        {
            return;
        }
        if (!IsLocal())
        {
            LOG_DEBUG(AI, "Non-local unit {} removed from group", (const char*)GetDebugName());
        }
        group->UnitRemoved(this);
        _subgroup = nullptr;
        _id = 0;
    }
}

void AIUnit::ForceRemoveFromGroup()
{
    Ref<AIUnit> unit = this;
    AIGroup* group = GetGroup();
    if (group)
    {
        group->UnitRemoved(this);
        _subgroup = nullptr;
        _id = 0;
    }
}

void AIUnit::IssueGetOut()
{
    if (IsFreeSoldier())
    {
        return;
    }

    AI_ERROR(_inVehicle); // !IsFreeSoldier
    Person* driverMan = _inVehicle->Driver();
    AIUnit* driver = driverMan ? driverMan->Brain() : nullptr;
    if (driver)
    {
        _inVehicle->WaitForGetOut(this);
    }
    else
    {
        _inVehicle->GetOutStarted(this);
    }
}

CameraType ValidateCamera(CameraType cam)
{
    if (cam == CamGunner)
    {
        return CamInternal;
    }
    return cam;
}

void MoveToGetInPos(AIUnit* unit, Transport& veh)
{
    Vector3 pos = veh.GetUnitGetInPos(unit);
    pos[1] = GLandscape->RoadSurfaceYAboveWater(pos[0], pos[2]);
    Vector3 dir = veh.Position() - pos;
    dir[1] = 0;
    Matrix4 trans;
    trans.SetUpAndDirection(VUp, dir);
    trans.SetPosition(pos);
    unit->GetPerson()->Move(trans);
}

static bool AvoidBeInWith(AICenter* center, Person* person)
{
    if (!person)
    {
        return false;
    }
    if (!center->IsEnemy(person->GetTargetSide()))
    {
        return false;
    }
    if (person->Brain() && person->Brain()->GetCaptive())
    {
        return false;
    }

    return true;
}

bool AIUnit::ProcessGetIn(Transport& veh)
{
    if (!IsLocal())
    {
        return false;
    }
    if (!IsFreeSoldier())
    {
        return false;
    }
    if (IsPlayer() && veh.GetLock() == LSLocked)
    {
        return false;
    }
    if (GetGroup()->IsPlayerGroup() && veh.GetLock() == LSLocked)
    {
        return false;
    }

    AIGroup* grp = GetGroup();
    AI_ERROR(grp);
    AICenter* center = grp->GetCenter();
    AI_ERROR(center);

    // avoid get in enemy vehicle
    if (AvoidBeInWith(center, veh.Commander()))
    {
        if (_vehicleAssigned == &veh)
        {
            UnassignVehicle();
            grp->UnassignVehicle(&veh);
        }
        return false;
    }
    if (AvoidBeInWith(center, veh.Driver()))
    {
        if (_vehicleAssigned == &veh)
        {
            UnassignVehicle();
            grp->UnassignVehicle(&veh);
        }
        return false;
    }
    if (AvoidBeInWith(center, veh.Gunner()))
    {
        if (_vehicleAssigned == &veh)
        {
            UnassignVehicle();
            grp->UnassignVehicle(&veh);
        }
        return false;
    }
    for (int i = 0; i < veh.GetManCargo().Size(); i++)
    {
        Person* man = veh.GetManCargo()[i];
        if (AvoidBeInWith(center, man))
        {
            if (_vehicleAssigned == &veh)
            {
                UnassignVehicle();
                grp->UnassignVehicle(&veh);
            }
            return false;
        }
    }

    if (IsPlayer() && (veh.GetLock() == LSUnlocked || IsGroupLeader() || GWorld->GetMode() == GModeNetware))
    {
        // can get into any vehicle - try to assign position
        if (!veh.QIsDriverIn() && veh.GetType()->HasDriver())
        {
            if (veh.QCanIGetIn(GetPerson()))
            {
                GetGroup()->AddVehicle(&veh);
                AssignAsDriver(&veh);
                AllowGetIn(true);
                OrderGetIn(true);
            }
            else
            {
                // cannot get in as driver
                return false;
            }
        }
        else
        {
            AI_ERROR(veh.GetGroupAssigned()); // driver is in
            bool assigned = GetGroup() == veh.GetGroupAssigned();
            // if !assigned - can get in cargo only
            if (assigned && veh.GetType()->HasCommander() && !veh.QIsCommanderIn())
            {
                if (veh.QCanIGetInCommander(GetPerson()))
                {
                    AssignAsCommander(&veh);
                    AllowGetIn(true);
                    OrderGetIn(true);
                }
                else
                {
                    // cannot get in as driver
                    return false;
                }
            }
            else if (assigned && veh.GetType()->HasGunner() && !veh.QIsGunnerIn())
            {
                if (veh.QCanIGetInGunner(GetPerson()))
                {
                    AssignAsGunner(&veh);
                    AllowGetIn(true);
                    OrderGetIn(true);
                }
                else
                {
                    // cannot get in as driver
                    return false;
                }
            }
            else if (veh.GetFreeManCargo() > 0)
            {
                if (veh.QCanIGetInCargo(GetPerson()))
                {
                    AssignAsCargo(&veh);
                    AllowGetIn(true);
                    OrderGetIn(true);
                }
                else
                {
                    // cannot get in as cargo
                    return false;
                }
            }
            else
            {
                // vehicle is full
                return false;
            }
        }
    }

    if (VehicleAssigned() == &veh)
    {
        // move soldier to get in position
        MoveToGetInPos(this, veh);

        UIActionType pos = ATGetInCargo;
        if (veh.GetDriverAssigned() == this)
        {
            pos = ATGetInDriver;
        }
        else if (veh.GetCommanderAssigned() == this)
        {
            pos = ATGetInCommander;
        }
        else if (veh.GetGunnerAssigned() == this)
        {
            pos = ATGetInGunner;
        }

        // play animation
        ActionContextBase* context = CreateGetInActionContext(&veh, pos);
        context->function = MFGetIn;
        GetPerson()->PlayAction(veh.Type()->GetGetInAction(), context);
        return true;
    }
    else
    {
        return false;
    }
}

static void JoinSubgroups(AISubgroup* subgrp1, AISubgroup* subgrp2)
{
    if (!subgrp2 || subgrp2 == subgrp1)
    {
        return;
    }
    AIGroup* grp = subgrp1->GetGroup();
    if (subgrp2->GetGroup() != grp)
    {
        return;
    }

    if (subgrp2 == grp->MainSubgroup())
    {
        subgrp1->JoinToSubgroup(subgrp2);
    }
    else
    {
        subgrp2->JoinToSubgroup(subgrp1);
    }
}

static void JoinSubgroups(Transport* veh)
{
    AIUnit* commander = veh->CommanderUnit();
    if (!commander)
    {
        return;
    }

    AISubgroup* subgrp = commander->GetSubgroup();
    if (!subgrp)
    {
        return;
    }

    // note: JoinSubgroups may join former commanders subgroup
    AIUnit* unit = veh->DriverBrain();
    if (unit)
    {
        JoinSubgroups(subgrp, unit->GetSubgroup());
        subgrp = commander->GetSubgroup();
    }
    unit = veh->CommanderBrain();
    if (unit)
    {
        JoinSubgroups(subgrp, unit->GetSubgroup());
        subgrp = commander->GetSubgroup();
    }
    unit = veh->GunnerBrain();
    if (unit)
    {
        JoinSubgroups(subgrp, unit->GetSubgroup());
        subgrp = commander->GetSubgroup();
    }
    const ManCargo& cargo = veh->GetManCargo();
    for (int i = 0; i < cargo.Size(); i++)
    {
        Person* man = cargo[i];
        if (!man)
        {
            continue;
        }
        AIUnit* unit = man->Brain();
        if (unit)
        {
            JoinSubgroups(subgrp, unit->GetSubgroup());
            subgrp = commander->GetSubgroup();
        }
    }
}

void __cdecl GunnerGetIn(Transport* veh, AIUnit* unit);

void __cdecl GunnerGetIn(Transport* veh, AIUnit* unit)
{
    Person* person = unit->GetPerson();

    if (veh->IsLocal())
    {
        veh->GetInFinished(unit);
        veh->GetInGunner(person);
    }
    else
    {
        GetNetworkManager().AskForGetIn(person, veh, GIPGunner);
    }
    if (!veh->CanCancelStop())
    {
        veh->UpdateStopTimeout();
        Verify(unit->SetState(AIUnit::Stopped));
    }

    // join subgroups
    JoinSubgroups(veh);

    // big warning: subgrp may change now

    // avoid leader in cargo
    AISubgroup* subgrp = unit->GetSubgroup();

    if (!subgrp->Leader()->IsUnit())
    {
        subgrp->SelectLeader();
    }

    // fixed - remote getin can fail - do not switch camera yet (will be switched later)
    if (GLOB_WORLD->FocusOn() == unit && veh->IsLocal())
    {
        GLOB_WORLD->SwitchCameraTo(veh, ValidateCamera(GLOB_WORLD->GetCameraType()));
    }
    subgrp->RefreshPlan();
}

bool AIUnit::ProcessGetIn2(Transport* veh, UIActionType pos)
{
    if (!veh)
    {
        return false;
    }

    // fixed - do not continue with get in if unit is dead
    if (GetPerson()->IsDammageDestroyed())
    {
        return false;
    }

    AI_ERROR(veh->IsLocal()); // warning: BUG
    AIGroup* grp = GetGroup();
    AI_ERROR(grp);
    AICenter* center = grp->GetCenter();
    AI_ERROR(center);

    // avoid get in enemy vehicle
    if (AvoidBeInWith(center, veh->Commander()))
    {
        if (_vehicleAssigned == veh)
        {
            UnassignVehicle();
            grp->UnassignVehicle(veh);
        }
        return false;
    }
    if (AvoidBeInWith(center, veh->Driver()))
    {
        if (_vehicleAssigned == veh)
        {
            UnassignVehicle();
            grp->UnassignVehicle(veh);
        }
        return false;
    }
    if (AvoidBeInWith(center, veh->Gunner()))
    {
        if (_vehicleAssigned == veh)
        {
            UnassignVehicle();
            grp->UnassignVehicle(veh);
        }
        return false;
    }
    for (int i = 0; i < veh->GetManCargo().Size(); i++)
    {
        Person* man = veh->GetManCargo()[i];
        if (AvoidBeInWith(center, man))
        {
            if (_vehicleAssigned == veh)
            {
                UnassignVehicle();
                grp->UnassignVehicle(veh);
            }
            return false;
        }
    }

    switch (pos)
    {
        case ATGetInDriver:
        {
            if (veh->QCanIGetIn(GetPerson()))
            {
                if (veh->IsLocal())
                {
                    veh->GetInFinished(this);
                    veh->GetInDriver(GetPerson());
                }
                else
                {
                    GetNetworkManager().AskForGetIn(GetPerson(), veh, GIPDriver);
                }
                GetGroup()->CalculateMaximalStrength();
                if (!veh->CanCancelStop())
                {
                    veh->UpdateStopTimeout();
                    Verify(SetState(Stopped));
                }

                // join subgroups
                JoinSubgroups(veh);
                // avoid leader in cargo
                if (!GetSubgroup()->Leader()->IsUnit())
                {
                    GetSubgroup()->SelectLeader();
                }

                // fixed - remote getin can fail - do not switch camera yet (will be switched later)
                if (GLOB_WORLD->FocusOn() == this && veh->IsLocal())
                {
                    GLOB_WORLD->SwitchCameraTo(veh, ValidateCamera(GLOB_WORLD->GetCameraType()));
                }
                GetSubgroup()->RefreshPlan();
                return true;
            }
            else
            {
                // cannot get in as driver
                return false;
            }
        }
        case ATGetInGunner:
        {
            if (veh->QCanIGetInGunner(GetPerson()))
            {
                GunnerGetIn(veh, this);
                return true;
            }
            else
            {
                // cannot get in as gunner
                return false;
            }
        }
        case ATGetInCommander:
        {
            if (veh->QCanIGetInCommander(GetPerson()))
            {
                if (veh->IsLocal())
                {
                    veh->GetInFinished(this);
                    veh->GetInCommander(GetPerson());
                }
                else
                {
                    GetNetworkManager().AskForGetIn(GetPerson(), veh, GIPCommander);
                }
                if (!veh->CanCancelStop())
                {
                    veh->UpdateStopTimeout();
                    Verify(SetState(Stopped));
                }

                // join subgroups
                JoinSubgroups(veh);

                // avoid leader in cargo
                if (!GetSubgroup()->Leader()->IsUnit())
                {
                    GetSubgroup()->SelectLeader();
                }

                // fixed - remote getin can fail - do not switch camera yet (will be switched later)
                if (GLOB_WORLD->FocusOn() == this && veh->IsLocal())
                {
                    GLOB_WORLD->SwitchCameraTo(veh, ValidateCamera(GLOB_WORLD->GetCameraType()));
                }
                GetSubgroup()->RefreshPlan();
                return true;
            }
            else
            {
                // cannot get in as commander
                return false;
            }
        }
        default:
            AI_ERROR(pos == ATGetInCargo);
            {
                if (veh->QCanIGetInCargo(GetPerson()))
                {
                    if (veh->IsLocal())
                    {
                        veh->GetInFinished(this);
                        veh->GetInCargo(GetPerson());
                    }
                    else
                    {
                        GetNetworkManager().AskForGetIn(GetPerson(), veh, GIPCargo);
                    }
                    SetState(InCargo);

                    // join subgroups
                    JoinSubgroups(veh);

                    // avoid leader in cargo
                    if (!GetSubgroup()->Leader()->IsUnit())
                    {
                        GetSubgroup()->SelectLeader();
                    }

                    // fixed - remote getin can fail - do not switch camera yet (will be switched later)
                    if (GLOB_WORLD->FocusOn() == this && veh->IsLocal())
                    {
                        GLOB_WORLD->SwitchCameraTo(veh, ValidateCamera(GLOB_WORLD->GetCameraType()));
                    }
                    GetSubgroup()->RefreshPlan();
                    return true;
                }
                else
                {
                    // cannot get in as cargo
                    return false;
                }
            }
    }
}

void AIUnit::DoGetOut(Transport* veh, bool parachute)
{
    bool isFocused = (GLOB_WORLD->FocusOn() == this);

    Person* person = GetPerson();
    Vector3 ejectSpeed = person->Speed();

    // calculate get out position
    Vector3 pos = veh->GetUnitGetOutPos(this);
    Matrix4 transform;
    transform.SetPosition(pos);
    transform.SetUpAndDirection(VUp, pos - veh->Position());

    veh->GetOutFinished(this);

    if (veh->DriverBrain() == this) // driver
    {
        veh->GetOutDriver(transform);
        if (IsLocal() && GetGroup())
        {
            GetGroup()->CalculateMaximalStrength();
        }
    }
    else if (veh->GunnerBrain() == this) // gunner
    {
        veh->GetOutGunner(transform);
    }
    else if (veh->CommanderBrain() == this) // commander
    {
        veh->GetOutCommander(transform);
    }
    else // cargo
    {
        veh->GetOutCargo(person, transform);

        // the only place where reset InCargo flag
        if (IsLocal())
        {
            AI_ERROR(_state == InCargo);
            _state = Wait;
            ClearOperativePlan();
        }
    }

    if (parachute)
    {
        const char* paraName = "ParachuteWest";
        TargetSide side = GetGroup()->GetCenter()->GetSide();
        if (side == TEast)
        {
            paraName = "ParachuteEast";
        }
        else if (side == TGuerrila)
        {
            paraName = "ParachuteG";
        }
        else if (side == TCivilian)
        {
            paraName = "ParachuteC";
        }

        Ref<EntityAI> ent = NewVehicle(paraName, "");
        Ref<Transport> getOutTo = dyn_cast<Transport, EntityAI>(ent);
        if (getOutTo)
        {
            getOutTo->SetAllowDammage(person->GetAllowDammage());
            if (getOutTo->Driver())
            {
                LOG_ERROR(AI, "{} already has a driver", (const char*)getOutTo->GetDebugName());
            }
            else
            {
                Vector3 aimCenter = getOutTo->GetShape()->AimingCenter();
                // aimCenter should be on pos position

                Matrix4 trans = person->Transform();
                trans.SetPosition(trans.Position() + getOutTo->Speed() * 0.3f - aimCenter);

                getOutTo->SetSpeed(veh->Speed() + ejectSpeed);
                getOutTo->SetTransform(trans);

                GWorld->AddVehicle(getOutTo);
                GetNetworkManager().CreateVehicle(getOutTo, VLTVehicle, "", -1);

                AI_ERROR(getOutTo->IsLocal());

                getOutTo->GetInFinished(this);
                getOutTo->GetInDriver(person, false);
            }
        }
    }
    if (isFocused)
    {
        GLOB_WORLD->SwitchCameraTo(GetVehicle(), ValidateCamera(GLOB_WORLD->GetCameraType()));
    }
}

void AIUnit::CheckIfAliveInTransport()
{
    if (GetLifeState() == AIUnit::LSAlive)
    {
        return;
    }
    Transport* veh = GetVehicleIn();
    if (!veh)
    {
        return;
    }
    AIGroup* grp = GetGroup();
    if (!grp)
    {
        return;
    }
    grp->SendIsDown(veh, this);
}

bool AIUnit::ProcessGetOut(bool parachute)
{
    if (!IsLocal())
    {
        return false;
    }
    if (IsFreeSoldier())
    {
        return false;
    }
    if (GetLifeState() != AIUnit::LSAlive)
    {
        return false;
    }

    Transport* veh = GetVehicleIn();
    if (!veh)
    {
        return false;
    }

    if (veh->IsLocal())
    {
        DoGetOut(veh, parachute);
    }
    else
    {
        GetNetworkManager().AskForGetOut(GetPerson(), veh, parachute);

        // actions on unit's side
        if (veh->DriverBrain() == this) // driver
        {
            if (GetGroup())
            {
                GetGroup()->CalculateMaximalStrength();
            }
        }
        if (_state == InCargo)
        {
            _state = Wait;
            ClearOperativePlan();
        }
    }

    // rearm if possible
    if (GetSubgroup())
    {
        if (IsGroupLeader() && !IsSubgroupLeader())
        {
            GetSubgroup()->SelectLeader(this);
        }

        GetSubgroup()->RefreshPlan();
    }

    return true;
}

void AIUnit::SendAnswer(Answer answer)
{
    if (!IsLocal())
    {
        return;
    }
    AISubgroup* subgrp = GetSubgroup();

    if (!subgrp)
    {
        return;
    }
    AIGroup* grp = subgrp->GetGroup();
    if (!grp)
    {
        return;
    }
    if (!grp->IsLocal())
    {
        // note: this should be player that wants to transmit some information
        //  all AI units should be local where group is local
        AI_ERROR(IsAnyPlayer());
        if (IsAnyPlayer())
        {
            // we need to transmit the message to the remote group
            // do two things
            // 1) transmit it on the radio
            // 2) send answer accross network (done in Transmitted())
            grp->GetRadio().Transmit(new RadioMessageUnitAnswer(this, subgrp, answer), grp->GetCenter()->GetLanguage());
        }
        return; // no simulation for remote groups
    }

    if (answer == AI::UnitDestroyed)
    {
        // subgroup does not know it yet (until someone will report it)
        // check: dead or deadinrespawn
        if (GetLifeState() != LSDead)
        {
            SetLifeState(LSDead);
            GetNetworkManager().UpdateObject(GetPerson());
            GetNetworkManager().UpdateObject(this);
            if (grp->Leader() == this)
            {
                // if we are leader, units expect us to communicate
                grp->SetReportBeforeTime(this, Glob.time + 30);
            }

            RString identity = GetPerson()->GetInfo()._identityContext;
            if (identity.GetLength() > 0)
            {
                // check if unit is from player group
                Person* player = GWorld->GetRealPlayer();
                AIUnit* playerUnit = player ? player->Brain() : nullptr;
                AIGroup* playerGroup = playerUnit ? playerUnit->GetGroup() : nullptr;
                if (playerGroup == grp)
                {
                    Poseidon::AddDeadIdentity(identity);
                }
            }
        }

        return;
    }

    if (answer == AI::StepTimeOut)
    {
        if (_state == Stopping)
        {
            // Get out cannot be processed
            Transport* veh = dyn_cast<Transport>(GetVehicle());
            AI_ERROR(veh);
            if (veh)
            {
                veh->WhoIsGettingOut().Clear();
            }
            else
                Verify(SetState(Wait));
        }
        else if (_state == AIUnit::Busy)
        {
            Verify(SetState(Replan));
        }
        return;
    }
    else if (answer == AI::StepCompleted)
    {
        if (_state == Stopping)
        {
            SetState(Stopped);
            Transport* veh = dyn_cast<Transport>(GetVehicle());
            if (veh)
            {
                veh->LandFinished();

                veh->WhoIsGettingOut().Compact();
                int n = veh->WhoIsGettingOut().Size();
                AIUnit* unit = nullptr;
                if (n > 0)
                {
                    for (int i = 0; i < n; i++)
                    {
                        AIUnit* u = veh->WhoIsGettingOut()[i];
                        if (u->IsInCargo())
                        {
                            unit = u;
                            break;
                        }
                    }
                    if (!unit)
                    {
                        for (int i = 0; i < n; i++)
                        {
                            AIUnit* u = veh->WhoIsGettingOut()[i];
                            if (u->IsGunner())
                            {
                                unit = u;
                                break;
                            }
                        }
                    }
                    if (!unit)
                    {
                        for (int i = 0; i < n; i++)
                        {
                            AIUnit* u = veh->WhoIsGettingOut()[i];
                            if (u->IsCommander())
                            {
                                unit = u;
                                break;
                            }
                        }
                    }
                    if (!unit)
                    {
                        unit = veh->WhoIsGettingOut()[0];
                    }
                    AI_ERROR(unit);
                    if (unit)
                    {
                        veh->SetGetOutTime(Glob.time + 2.0);
                        // FIX
                        unit->AddRef();
                        unit->CheckIfAliveInTransport(); // unit can be destroyed inside
                        unit->ProcessGetOut(false);
                        unit->Release();
                    }
                }
            }
        }
        else if (_state == AIUnit::Busy)
        {
            if (_lastPlan)
            {
                EntityAI* veh = GetVehicle();
                float precision = veh->GetPrecision();
                if (_wantedPosition.Distance(_plannedPosition) > Square(precision))
                {
                    ForceReplan();
                }
                else
                {
                    Vector3 pos = _path[_path.Size() - 1]._pos;
                    if (pos.DistanceXZ2(_expPosition) > Square(1))
                    {
                        // try to find better path
                        _iter = 0;
                        Verify(SetState(Wait));
                    }
                    else
                    {
                        Verify(SetState(Completed));
                    }
                }
            }
            else
            {
                Verify(SetState(Replan));
            }
        }
        return;
    }

#if LOG_COMM
    Log("Send answer: Unit %s: Answer %d", (const char*)GetDebugName(), answer);
#endif

    if (grp->NUnits() > 1)
    {
        // transmit to radio only where there is somebody to listen
        grp->GetRadio().Transmit(new RadioMessageUnitAnswer(this, subgrp, answer), grp->GetCenter()->GetLanguage());
    }
    else
    {
        subgrp->ReceiveAnswer(this, answer);
    }
}

void AIUnit::SetWatchDirection(Vector3Val dir)
{
    _watchDirHead = _watchDir = dir;
    _watchMode = WMDir;
    _watchDirSet = Glob.time;
    _targetAssigned = nullptr;
}

void AIUnit::SetWatchAround()
{
    _watchDirHead = _watchDir = GetVehicle()->Direction();
    _watchMode = WMAround;
    _watchDirSet = Glob.time;
    _targetAssigned = nullptr;
}

void AIUnit::SetNoWatch()
{
    _watchTgt = nullptr, _watchMode = WMNo;
    _targetAssigned = nullptr;
}
void AIUnit::SetWatchPosition(Vector3Val pos)
{
    _watchPos = pos, _watchMode = WMPos;
    _targetAssigned = nullptr;
}
void AIUnit::SetWatchTarget(Target* tgt)
{
    _watchTgt = tgt, _watchMode = WMTgt;
}

float HowMuchInteresting(AIUnit* unit, const Target* tgt)
{
    EntityAI* veh = unit->GetVehicle();
    Vector3Val unitDir = veh->Direction();
    Vector3Val unitPos = veh->Position();
    AIGroup* grp = unit->GetGroup();
    AICenter* cnt = grp->GetCenter();
    float interesting = 1;
    // we get some points when unknown
    if (tgt->side == TSideUnknown)
    {
        interesting += 10;
    }
    // we get many point for being enemy
    if (cnt->IsEnemy(tgt->side))
    {
        interesting += 50;
    }
    // we get some points for being human
    if (tgt->type->IsKindOf(GWorld->Preloaded(VTypeMan)))
    {
        interesting += 10;
    }
    // we get some points for moving
    EntityAI* entity = tgt->idExact;
    if (entity)
    {
        // we get many points for firing, screaming and moving
        // this is all calculated in audibility - making noise
        // if target is visible, we should also check VisibleMovement()
        // we check it always - hopefully it will not matter too much
        interesting += entity->Audible() * 5;
        interesting += entity->VisibleMovement();
        // and also for speaking
        interesting += entity->GetSpeaking() * 30;
    }
    // we loose a lot for being out of natural focus area
    Vector3 tgtDir = tgt->position - unitPos;
    float cosAlpha = tgtDir * unitDir * tgtDir.InvSize();
    float focus = floatMax(cosAlpha, 0.2);
    return interesting * focus;
}

Target* SelectInterestingTarget(AIUnit* unit, Target* thn)
{
    Vector3Val uPos = unit->Position();
    AIGroup* grp = unit->GetGroup();
    if (!grp)
    {
        return nullptr;
    }
    if (!grp->GetCenter())
    {
        return nullptr;
    }
    // find something known that is near
    const TargetList& list = grp->GetTargetList();
    // never select target less interesting than certain limit
    float thnInteresting = 20;
    float thnDist = 50;
    if (thn)
    {
        // increase old target interest to maintain some stability
        thnInteresting = HowMuchInteresting(unit, thn) * 3.0f;
        thnDist = uPos.Distance(thn->position);
    }
    for (int i = 0; i < list.Size(); i++)
    {
        Target* tgt = list[i];
        if (tgt->idExact == unit->GetPerson())
        {
            continue;
        }
        if (tgt->idExact == unit->GetVehicleIn())
        {
            continue;
        }
        if (!tgt->IsKnownBy(unit))
        {
            continue;
        }
        if (tgt->type->IsKindOf(GWorld->Preloaded(VTypeStatic)))
        {
            continue;
        }
        if (tgt->idExact && tgt->idExact->Static())
        {
            continue;
        }

        float tgtDist = tgt->position.Distance(uPos);
        float tgtInteresting = HowMuchInteresting(unit, tgt);
        if (tgtInteresting * thnDist > thnInteresting * tgtDist)
        {
            thn = tgt;
            thnDist = tgtDist;
            thnInteresting = tgtInteresting;
        }
    }
    return thn;
}

void AIUnit::SetWatch()
{
    // cosider engaging target
    AIGroup* grp = GetGroup();
    if (!IsPlayer() && _targetAssigned && !grp->CommandSent(this, Command::Attack) &&
        !grp->CommandSent(this, Command::AttackAndFire) && !grp->CommandSent(this, Command::GetOut) &&
        !grp->CommandSent(this, Command::GetIn) && (_targetAssigned == _targetEngage || !IsKeepingFormation()))
    {
        EntityAI* veh = GetVehicle();
        FireResult result;
        Vector3 pos;
        if (veh->AttackThink(result, pos))
        {
            // check if we are ready to fire
            // if not, we probably will have to attack
            float ttl = GetTimeToLive();
            FireResult fResult;
            veh->WhatFireResult(fResult, *_targetAssigned, ttl);
            if (result.CleanSurplus() > fResult.CleanSurplus() * 1.2 + 200)
            {
                // some good attack position found
                // issue auto attack command

                Command cmd;
                cmd._message = Command::Attack;
                cmd._targetE = _targetAssigned;
                cmd._destination = pos;

                // automatic attack command
                grp->NotifyAutoCommand(cmd, this);
            }
        }
    }

    // consider formation and watch mode
    switch (_watchMode)
    {
        case WMAround:
        {
            float time = Glob.time - _watchDirSet;
            Matrix3 rotY(MRotationY, time * ((H_PI * 2) / 30));
            _watchDir = rotY * _watchDir;
            _watchDir[1] = 0;
            _watchDir.Normalize();
            _watchDirSet = Glob.time;
            _watchDirHead = _watchDir;
        }
        break;
        case WMPos:
        {
            _watchDir = _watchPos - GetVehicle()->CameraPosition();
            _watchDir.Normalize();
            _watchDirHead = _watchDir;
        }
        break;
        case WMTgt:
            if (_watchTgt)
            {
                _watchPos = _watchTgt->AimingPosition();
                _watchDir = _watchPos - GetVehicle()->CameraPosition();
                _watchDir.Normalize();
                _watchDirHead = _watchDir;
                break;
            }
        // fall trough from case WMTgt to case WMNo
        case WMNo:
        {
            // watch direction given by formation
            AISubgroup* subgrp = GetSubgroup();
            Vector3Val dir = subgrp->GetFormationDirection();
            float relAngle = _formationAngle;
            Matrix3 rot(MRotationY, -relAngle);
            _watchDirHead = _watchDir = rot * dir;
            // if auto targeting is disabled, we must not be curious
            // mission designer has full control over us
            if (IsSoldier() && (_disabledAI & DAAutoTarget) == 0)
            {
                // there is no need to simulate this for units
                // that are far

                //  To considre: maybe combat units may be affected by this?
                //  when soldier is idle, automatically select "interesting" targets
                Target* tgt = nullptr;
                tgt = SelectInterestingTarget(this, _watchTgt);

                if (tgt)
                {
                    // we see something interesting - watch it
                    _watchTgt = tgt;
                    _watchPos = tgt->AimingPosition();
                    if (tgt->idExact)
                    {
                        // make correction for different aiming/camera position
                        if (_watchPos.Distance2(tgt->idExact->AimingPosition()) < 0.5f)
                        {
                            _watchPos = tgt->idExact->CameraPosition();
                        }
                    }
                    _watchDirHead = _watchPos - GetVehicle()->CameraPosition();
                    _watchDirHead.Normalize();
                }
            }
        }
        break;
    }
}

void AIUnit::CheckResources()
{
    ResourceState state = GetHealthState();
    if (state == RSCritical)
    {
        if (_lastHealthState < RSCritical)
        {
            _healthCriticalTime = Glob.time + FIRST_REPORT_TIME;
        }
        else if (Glob.time > _healthCriticalTime)
        {
            _healthCriticalTime = Glob.time + REPEAT_REPORT_TIME;
            SendAnswer(HealthCritical);
        }
    }
    _lastHealthState = state;

    if (IsUnit())
    {
        state = GetFuelState();
        if (state == RSCritical)
        {
            if (_lastFuelState < RSCritical)
            {
                _fuelCriticalTime = Glob.time + FIRST_REPORT_TIME;
            }
            else if (Glob.time > _fuelCriticalTime)
            {
                _fuelCriticalTime = Glob.time + REPEAT_REPORT_TIME;
                SendAnswer(FuelCritical);
            }
        }
        else if (state == RSLow)
        {
            if (_lastFuelState < RSLow)
            {
                SendAnswer(FuelLow);
            }
        }
        _lastFuelState = state;

        state = GetArmorState();
        if (state == RSCritical)
        {
            if (_lastArmorState < RSCritical || Glob.time > _dammageCriticalTime)
            {
                _dammageCriticalTime = Glob.time + REPEAT_REPORT_TIME;
                SendAnswer(DammageCritical);
            }
        }
        _lastArmorState = state;

        state = GetAmmoState();
        if (state == RSCritical)
        {
            if (_lastAmmoState < RSCritical || Glob.time > _ammoCriticalTime)
            {
                _ammoCriticalTime = Glob.time + REPEAT_REPORT_TIME;
                SendAnswer(AmmoCritical);
            }
        }
        else if (state == RSLow)
        {
            if (_lastAmmoState < RSLow)
            {
                SendAnswer(AmmoLow);
            }
        }
        _lastAmmoState = state;
    }
}

void AIUnit::CheckGetOut()
{
    Transport* veh = _inVehicle;
    if (veh && (!veh->Driver() || !veh->Driver()->Brain()) && veh->GetGetOutTime() <= Glob.time)
    {
        veh->WhoIsGettingOut().Compact();
        int n = veh->WhoIsGettingOut().Size();
        for (int i = 0; i < n; i++)
        {
            if (veh->WhoIsGettingOut()[i].GetLink() == this)
            {
                veh->SetGetOutTime(Glob.time + 2.0);
                // FIX
                AddRef();
                CheckIfAliveInTransport(); // unit can be destroyed inside
                ProcessGetOut(false);
                Release();
            }
        }
    }
}

void AIUnit::CheckGetInGetOut()
{
    Transport* veh = _inVehicle;
    if (veh)
    {
        if (veh->GetGetInTimeout() <= Glob.time || veh->WhoIsGettingIn().Count() <= 0)
        {
            veh->SetGetInTimeout(Time(0));
            veh->WhoIsGettingIn().Clear();
        }
        veh->WhoIsGettingOut().Compact();
        int n = veh->WhoIsGettingOut().Size();
        if (n > 0 && Glob.time >= veh->GetGetOutTime())
        {
            AIUnit* unit = nullptr;
            for (int i = 0; i < n; i++)
            {
                AIUnit* u = veh->WhoIsGettingOut()[i];
                if (u->IsInCargo())
                {
                    unit = u;
                    break;
                }
            }
            if (!unit)
            {
                for (int i = 0; i < n; i++)
                {
                    AIUnit* u = veh->WhoIsGettingOut()[i];
                    if (u->IsGunner())
                    {
                        unit = u;
                        break;
                    }
                }
            }
            if (!unit)
            {
                for (int i = 0; i < n; i++)
                {
                    AIUnit* u = veh->WhoIsGettingOut()[i];
                    if (u->IsCommander())
                    {
                        unit = u;
                        break;
                    }
                }
            }
            if (!unit)
            {
                unit = veh->WhoIsGettingOut()[0];
            }
            AI_ERROR(unit);
            if (unit)
            {
                veh->SetGetOutTime(Glob.time + 2.0);
                // FIX
                unit->AddRef();
                unit->CheckIfAliveInTransport(); // unit can be destroyed inside
                unit->ProcessGetOut(false);
                unit->Release();
            }
        }
        if (veh->CanCancelStop())
        {
            SetState(Wait); // valid pass Stopped -> Wait
        }
    }
    else
    {
        if (HasAI())
        {
            if (GetPerson()->CanCancelStop())
            {
                Verify(SetState(Wait));
                // ?? Fail("Unit stopped");
            }
        }
    }
}

void AIUnit::OnStrategicPathFound()
{
    _noPath = false;
    _updatePath = false;
    _lastPlan = false;
    State state = GetState();
    if (state != Delay && state != InCargo && state != Stopping && state != Stopped)
        Verify(SetState(Wait));
    _iter = 0;

    float totalCost = _planner->GetTotalCost();
    _completedTime = Glob.clock.GetTimeInYear() + totalCost * OneSecond;
}

void AIUnit::OnStrategicPathNotFound(bool update)
{
    _waitWithPlan = Glob.time + 10.0F;

    if (update)
    {
#if LOG_POSITION_PROBL
        LOG_DEBUG(AI, "Did not found path in update.");
#endif
        _completedTime = Glob.clock.GetTimeInYear() + OneDay; // set conditions for timeout
    }
    else
    {
        switch (_planningMode)
        {
            case LeaderPlanned:
                if (++_attemptPlan >= 3)
                {
                    GetSubgroup()->FailCommand();
                }
                else
                {
                    GetSubgroup()->SendAnswer(AI::SubgroupDestinationUnreacheable);
                }
                break;
            case FormationPlanned:
            case VehiclePlanned:
                // nothing to do
                break;
            default:
                Fail("Unexpected mode");
                break;
        }
    }
}

void AIUnit::SetWantedPosition(Vector3Par pos, PlanningMode mode, bool forceReplan)
{
#if DIAG_PLANNING
    LOG_DEBUG(AI, "{:.3f}, {}: SetWantedPosition to {:.0f}, {:.0f}, mode {} {}", Glob.time.toFloat(),
              (const char*)GetDebugName(), pos.X(), pos.Z(), (const char*)FindEnumName(mode),
              forceReplan ? ", force replan" : "");
#endif

    _planningMode = mode;
    _wantedPosition = pos;
    _completedReceived = false;
    if (mode != DoNotPlan)
    {
        GetVehicle()->GoToStrategic(pos);
    }

    switch (mode)
    {
        case DoNotPlan:
            // continue with plan
            if (forceReplan)
            {
                ClearStrategicPlan();
                ClearOperativePlan();
            }
            break;
        case LeaderPlanned:
        case VehiclePlanned:
        case FormationPlanned:
            if (!forceReplan)
            {
                float error2 = _plannedPosition.Distance2(_wantedPosition);
                float limit2 = Square(0.1) * Position().Distance2(_wantedPosition);
                forceReplan = error2 > limit2;
            }

            if (forceReplan)
            {
                _plannedPosition = _wantedPosition;
                if (mode != FormationPlanned)
                {
                    _noPath = true;
                }
                RefreshStrategicPlan();
            }
            // else replan only when _wantedPosition differs from _plannedPosition enough
            break;
        case LeaderDirect:
            // force replan
            _plannedPosition = pos;
            _expPosition = pos;
            ClearStrategicPlan();
            ClearOperativePlan();
            break;
    }
}

void AIUnit::CreateStrategicPath(ThinkImportance prec)
{
    Vector3Val dest = _plannedPosition;
    if (_noPath && _waitWithPlan <= Glob.time)
    {
#if LOG_THINK
        Log("  - Searching plan (strategic) to %.1f, %.1f (%d,%d).", dest.X(), dest.Z(), xe, ze);
#endif
        if (!_planner->IsSearching())
        {
            // begin new search
            if (!_planner->StartSearching(prec, GetVehicle(), Position(), dest))
            {
                OnStrategicPathNotFound(false);
            }
            else if (!_planner->IsSearching())
            {
                OnStrategicPathFound();
            }
        }
        if (_planner->IsSearching())
        {
            // continue with searching
            if (!_planner->ProcessSearching())
            {
                OnStrategicPathNotFound(false);
            }
            else if (!_planner->IsSearching())
            {
                OnStrategicPathFound();
            }
        }
    }
    else if (_updatePath && _waitWithPlan <= Glob.time)
    {
#if LOG_THINK
        Log("  - Update plan (strategic) to %.1f, %.1f (%d,%d).", dest.X(), dest.Z(), xe, ze);
#endif
        if (!_planner->IsSearching())
        {
            // begin new search
            if (!_planner->StartSearching(prec, GetVehicle(), Position(), dest))
            {
                OnStrategicPathNotFound(true);
            }
            else if (!_planner->IsSearching())
            {
                OnStrategicPathFound();
            }
        }
        if (_planner->IsSearching())
        {
            // continue with searching
            if (!_planner->ProcessSearching())
            {
                OnStrategicPathNotFound(true);
            }
            else if (!_planner->IsSearching())
            {
                OnStrategicPathFound();
            }
        }
    }
}

void AIUnit::OperPath(ThinkImportance prec)
{
    EntityAI* veh = GetVehicle();

    SetHouse(nullptr, -1);
    Vector3Val pos = Position();
    float dist2;

    if (_state != Stopped && _state != Stopping && _state != Delay && _state != InCargo)
    {
        if (!_noPath)
        { // construct path by strategic plan
            int planIndex = _planner->FindBestIndex(pos);
            int minPlanIndex = planIndex;
            float precision = veh->GetPrecision();
            bool onRoad = GRoadNet->IsOnRoad(pos, precision * 1.2) != nullptr;
            do
            {
                _lastPlan = _planner->GetPlanPosition(planIndex, _expPosition);

                dist2 = (_expPosition - pos).SquareSizeXZ();
                if (dist2 >= Square(1.1 * precision))
                // do not force target under vehicle's precision
                {
                    FieldPassing::Mode planMode = _planner->GetPlanMode(planIndex);
                    if (!onRoad)
                    {
                        if (planMode == FieldPassing::MoveOnRoad)
                        {
                            break; // move onto road
                        }
                    }
                    else
                    {
                        if (planMode != FieldPassing::MoveOnRoad)
                        {
                            break; // move out of road
                        }
                    }
                }
            } while (dist2 < Square(DIST_MIN_OPER) && ++planIndex < _planner->GetPlanSize());

            if (_iter == 1)
            {
                if (planIndex > minPlanIndex)
                {
                    planIndex--;
                    _lastPlan = _planner->GetPlanPosition(planIndex, _expPosition);
                }
            }
            else if (_iter == 2)
            {
                if (planIndex < _planner->GetPlanSize() - 1)
                {
                    planIndex++;
                    _lastPlan = _planner->GetPlanPosition(planIndex, _expPosition);
                }
            }

            saturateMin(planIndex, _planner->GetPlanSize() - 1);

            if (_lastPlan)
            {
                if (prec <= LevelFastOperative)
                {
                    SetMode(AIUnit::Exact);
                }
                else
                {
                    SetMode(AIUnit::DirectExact);
                }
                Command* cmd = GetSubgroup()->GetCommand();
                if (cmd && cmd->_message == Command::Move && cmd->_target)
                {
                    const IPaths* house = cmd->_target->GetIPaths();
                    if (house && cmd->_target->GetShape()->FindPaths() >= 0)
                    {
                        SetHouse(cmd->_target, cmd->_param);
                    }
                }
            }
            else
            {
                if (prec <= LevelFastOperative)
                {
                    SetMode(AIUnit::Normal);
                }
                else
                {
                    SetMode(AIUnit::DirectNormal);
                }
            }

            Verify(SetState(AIUnit::Init));
        }
    }
    else
    {
        _expPosition = pos;
        _lastPlan = false;
        // do not change state (Stopping, Stopped, InCargo, Delay)
    }
}

bool AIUnit::CreateOperativePath()
{
    EntityAI* veh = GetVehicle();

    // new attempt
    if (_state == Delay && _delay <= Glob.time)
    {
        Verify(SetState(Wait));
        return false;
    }

    if (_state != Init)
    {
        // check if current path is valid
        if (!VerifyPath())
        {
            if (IsSubgroupLeader())
            {
                Verify(SetState(Replan));
            }
            else
            {
                Verify(SetState(Wait)); // clear plan
            }
        }
        // do not plan
        return false;
    }

    float combatHeight = veh->GetCombatHeight();
    if (veh->GetType()->IsKindOf(GLOB_WORLD->Preloaded(VTypeAir)) ||
        veh->GetType()->IsKindOf(GLOB_WORLD->Preloaded(VTypeShip)))
    {
        if (_mode == Exact)
        {
            _mode = DirectExact;
        }
        else if (_mode == Normal)
        {
            _mode = DirectNormal;
        }
    }
    if (_mode == DirectExact || _mode == DirectNormal)
    {
        // creates path from Position() to _expPosition if none in _planner
        CopyPath(*_planner);
        Verify(SetState(Busy));
        return false;
    }
    else
    {
        float dist2 = (Position() - _expPosition).SquareSizeXZ();
        if (dist2 < Square(DIST_NOT_SEARCH))
        {
            if (_lastPlan)
            {
                float precision = veh->GetPrecision();
                if (_wantedPosition.Distance(_plannedPosition) > Square(precision))
                {
                    ForceReplan();
                }
                else
                {
                    Verify(SetState(Completed));
                }
            }
            else
            {
                Verify(SetState(Replan));
            }
            return false;
        }
        if (!CreatePath(Position(), _expPosition))
        {
            LOG_DEBUG(AI, "{}: path not found - iteration {}", (const char*)GetDebugName(), _iter);
            if (++_iter > 2)
            {
                LOG_DEBUG(AI, "  fail command");
                GetSubgroup()->FailCommand();
                _iter = 0;
            }
            else
            {
                _delay = Glob.time + 5.0;
                Verify(SetState(Delay));
            }
            return true;
        }
        _iter = 0;

        // add strategic plan to path
        int index = -1;
        int n = _planner->GetPlanSize();
        for (int i = 0; i < n; i++)
        {
            Point3 pos;
            _planner->GetPlanPosition(i, pos);
            if ((pos - _expPosition).SquareSizeXZ() < 1.0f)
            {
                index = i;
                break;
            }
        }
        if (index >= 0)
        {
            int j = _path.Size() - 1;
            float lastCost = _path[j]._cost;
            Point3 lastPos = GLandscape->PointOnSurface(_expPosition[0], combatHeight, _expPosition[2]);
            for (int i = index + 1; i < n; i++)
            {
                Point3 pos;
                _planner->GetPlanPosition(i, pos);
                GeographyInfo geogr = _planner->GetGeography(i);
                float cost = veh->GetCost(geogr) * veh->GetFieldCost(geogr);
                if (cost >= GET_UNACCESSIBLE)
                {
                    cost = 1.0;
                }
                cost *= (pos - lastPos).SizeXZ();
                lastCost += cost;
                lastPos = GLandscape->PointOnSurface(pos[0], combatHeight, pos[2]);
                j = _path.Add();
                _path[j]._pos = lastPos;
                _path[j]._cost = lastCost;
            }
        }
        Verify(SetState(Busy));
        return true;
    }
}

bool AIUnit::Think(ThinkImportance prec)
{
    if (!IsLocal())
    {
        return false;
    }

    if (GetLifeState() != LSAlive)
    {
        return false;
    }
#if LOG_THINK
    Log("    Unit %s think.", (const char*)GetDebugName());
#endif

    EntityAI* veh = GetVehicle();
    // unassign target if not valid

    if (!_subgroup)
    {
        return false;
    }

    // find attack position if neccessary

    SetWatch();
    // some parts of AIUnit::Think may be executed less often

    if (Glob.time > _expensiveThinkTime)
    {
        ExpensiveThinkDone();
        if (!IsAnyPlayer() || USER_CONFIG.IsEnabled(DTAutoSpot))
        {
            CheckResources();
        }
        if (!IsSubgroupLeader() && IsUnit())
        {
            bool away = veh->IsAway();
            if (away)
            {
                if (!_isAway)
                {
                    _awayTime = Glob.time + FIRST_AWAY_TIME;
                }
                else if (Glob.time > _awayTime)
                {
                    if (!_subgroup->IsPlayerSubgroup() && veh->IsAway(1.5))
                    {
                        _awayTime = Glob.time + REPEAT_AWAY_TIME;
                        AIGroup* grp = GetGroup();
                        grp->GetRadio().Transmit(new RadioMessageReturnToFormation(_subgroup, this),
                                                 grp->GetCenter()->GetLanguage());
                    }
                    else if (!IsPlayer())
                    {
                        AIGroup* grp = GetGroup();
                        grp->GetRadio().Transmit(new RadioMessageWhereAreYou(this, grp),
                                                 grp->GetCenter()->GetLanguage());
                        // all units of the group should be marked not to ask this question
                        // to avoid asking it multiple times
                        for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
                        {
                            AIUnit* u = grp->UnitWithID(i + 1);
                            if (!u)
                            {
                                continue;
                            }
                            u->SetAwayTime(Glob.time + REPEAT_AWAY_TIME + GRandGen.RandomValue() * 20);
                        }
                    }
                }
            }
            _isAway = away;
        }

        if (_targetAssigned)
        {
            if (_targetAssigned->destroyed || _targetAssigned->vanished || !veh->IsAbleToFire())
            {
                AssignTarget(nullptr);
            }
        }

        // FIX
        // consider if we should use nvg
        if (!IsPlayer())
        {
            Person* person = GetPerson();
            if (person->IsNVEnabled())
            {
                const LightSun* sun = GScene->MainLight();
                float night = sun->NightEffect();
                if (night > 0)
                {
                    // check light intensity
                    float dark = 1.02f - sun->Ambient().Brightness() - sun->Diffuse().Brightness();
                    saturateMin(night, dark);

                    float thold = 0.5f + ID() * 0.02f - GetAbility() * 0.2f;

                    person->SetNVWanted(night > thold);
                }
            }
        }
    }

    if (veh->PilotUnit() != this)
    {
        // Warning: unit can be destroyed inside, return immediatelly
        CheckGetOut();
        // create path only for pilot units
        return false;
    }

    // free soldier or driver
    if (!HasAI())
    {
        if (veh->Speed().SquareSize() < Square(0.5) &&
            veh->Position().Y() - GLandscape->RoadSurfaceYAboveWater(veh->Position().X(), veh->Position().Z()) < 4.0)
        {
            CheckGetInGetOut();
        }
        return false;
    }

    if (_state == Stopped)
    {
        // waiting for get in / get out
        CheckGetInGetOut();
        return false;
    }

    switch (_planningMode)
    {
        case DoNotPlan:
            // do not create new plan (if exist, can continue with the old one)
            if (_noPath)
            {
                return false;
            }
            else
            {
                AI_ERROR(IsSubgroupLeader());
                //			AI_ERROR(IsUnit());

                // operative planning target
                if (_state == Wait || _state == Replan)
                {
                    OperPath(prec);
                }

                // operative planning
                return CreateOperativePath();
            }
        case LeaderDirect:
        {
            AI_ERROR(IsSubgroupLeader());
            AI_ERROR(IsUnit());

            SetMode(AIUnit::DirectExact);
            Verify(SetState(AIUnit::Init));
            _iter = 0;
            _lastPlan = true;

            // operative planning
            return CreateOperativePath();
        }
        case LeaderPlanned:
        case FormationPlanned:
        case VehiclePlanned:
        {
            // check strategic target
            float error2 = _plannedPosition.Distance2(_wantedPosition);
            float limit2 = Square(0.1) * Position().Distance2(_wantedPosition);
            // replan if target position changed by 10% of total distance
            if (error2 > limit2)
            {
                _plannedPosition = _wantedPosition;
                _noPath = true;
                RefreshStrategicPlan();
            }

            // strategic planning
            CreateStrategicPath(prec);

            // operative planning target
            if (_state == Wait || _state == Replan)
            {
                OperPath(prec);
            }

            // operative planning
            return CreateOperativePath();
        }
        default:
            Fail("Planning mode");
            return false;
    }
}

void AIUnit::SetSemaphore(Semaphore status)
{
    _semaphore = status;
}

bool AIUnit::IsHoldingFire() const
{
    return _semaphore == AI::SemaphoreBlue || _semaphore == AI::SemaphoreGreen || _semaphore == AI::SemaphoreWhite;
}

bool AIUnit::IsKeepingFormation() const
{
    return _semaphore == AI::SemaphoreBlue || _semaphore == AI::SemaphoreGreen || _semaphore == AI::SemaphoreYellow;
}

bool AIUnit::IsFireEnabled(Target* tgt) const
{
    // check if fire enabled at given target
    if (!IsHoldingFire())
    {
        return true;
    }
    if (tgt && tgt == GetEnableFireTarget())
    {
        return true;
    }
    return false;
}
bool AIUnit::IsEngageEnabled(Target* tgt) const
{
    // check if fire enabled at given target
    if (!IsKeepingFormation())
    {
        return true;
    }
    if (tgt && tgt == GetEngageTarget())
    {
        return true;
    }
    return false;
}

void AIUnit::SetFormationPos(FormationPos status)
{
    _formPos = status;
    _formPosCoef = 1;
}

void AIUnit::AddFormationPos(FormationPos status)
{
    if (_formPos == status)
    {
        _formPosCoef += 1;
    }
    else
    {
        _formPos = status;
        _formPosCoef = 1;
    }
}

void AIUnit::SetUnitPosition(UnitPosition status)
{
    Person* person = GetPerson();
    if (person)
    {
        person->SetUnitPosition(status);
    }
}

UnitPosition AIUnit::GetUnitPosition() const
{
    Person* person = GetPerson();
    if (person)
    {
        return person->GetUnitPosition();
    }
    return UPAuto;
}

bool AIUnit::IsDanger() const
{
    return Glob.time <= _dangerUntil;
}

void AIUnit::SetDanger(float until)
{
    if (IsPlayer())
    {
        return;
    }
    switch (GetCombatMode())
    {
        case CMSafe:
        case CMAware:
            if (until < 0)
            {
                until = GRandGen.PlusMinus(5.0f, 1.0f);
            }
            _dangerUntil = Glob.time + until;
            EntityAI* veh = GetVehicle();
            if (veh)
            {
                veh->OnDanger();
            }
            break;
            // don't set danger in Combat mode
    }
}

void AIUnit::ReportStatus()
{
    if (!_subgroup)
    {
        return;
    }

    ResourceState state;

    state = GetFuelState();
    if (state == RSCritical)
    {
        _fuelCriticalTime = Glob.time + REPEAT_REPORT_TIME;
        SendAnswer(FuelCritical);
    }
    else if (state == RSLow)
    {
        SendAnswer(FuelLow);
    }
    _lastFuelState = state;

    state = GetHealthState();
    if (state == RSCritical)
    {
        _healthCriticalTime = Glob.time + REPEAT_REPORT_TIME;
        SendAnswer(HealthCritical);
    }
    _lastHealthState = state;

    state = GetArmorState();
    if (state == RSCritical)
    {
        _dammageCriticalTime = Glob.time + REPEAT_REPORT_TIME;
        SendAnswer(DammageCritical);
    }
    _lastArmorState = state;

    state = GetAmmoState();
    if (state == RSCritical)
    {
        _ammoCriticalTime = Glob.time + REPEAT_REPORT_TIME;
        SendAnswer(AmmoCritical);
    }
    else if (state == RSLow)
    {
        SendAnswer(AmmoLow);
    }
    _lastAmmoState = state;

    if (GetCombatMode() == CMStealth)
    {
        AIGroup* grp = GetGroup();
        if (grp)
        {
            // stealth: mark all targets watch by this unit as not reported
            const TargetList& list = grp->GetTargetList();
            for (int i = 0; i < list.Size(); i++)
            {
                Target* tgt = list[i];
                if (tgt->idSensor == GetPerson())
                {
                    // set as never reported - force reporting targets
                    tgt->timeReported = TIME_MIN;
                    tgt->posReported = VZero;
                }
            }
        }
    }

    SendAnswer(AI::ReportPosition);
}

bool AIUnit::IsAnyPlayer() const
{
    if (IsPlayer())
    {
        return true;
    }
    Person* person = GetPerson();
    return person && person->IsNetworkPlayer();
}

bool AIUnit::IsPlayer() const
{
    return _person == GLOB_WORLD->PlayerOn();
}

bool AIUnit::IsPlayerDriven() const
{
    return _person == GLOB_WORLD->PlayerOn() ||
           _inVehicle && _inVehicle->CommanderUnit() == this && _inVehicle->Driver() == GLOB_WORLD->PlayerOn();
}

float AIUnit::GetRandomizedExperience() const
{
    float exp = GetPerson()->GetExperience();
    const float coef = 0.2;
    // result is from <(1 - coef) * exp ; (1 + coef) * exp>
    return exp * ((1.0 - coef) + (2.0 * coef) * GRandGen.RandomValue());
}

float AIUnit::GetInvAverageSpeed() const
{
    GeographyInfo geogr;
    geogr.packed = 0;
    geogr.u.gradient = 1;

    return GetVehicle()->GetCost(geogr);
}

float AIUnit::GetAverageSpeed() const
{
    return 1.0 / GetInvAverageSpeed();
}

void AIUnit::IncreaseExperience(const VehicleType& type, TargetSide side)
{
    float coef;

    AIGroup* grp = GetGroup();
    AI_ERROR(grp);
    if (!grp)
    {
        return;
    }

    AICenter* center = grp->GetCenter();
    if (side == TCivilian)
    {
        coef = ExperienceDestroyCivilian;
    }
    else if (center->IsEnemy(side))
    {
        coef = ExperienceDestroyEnemy;
    }
    else
    {
        coef = ExperienceDestroyFriendly;
    }

    if (!IsPlayer() && coef <= 0)
    {
        return;
    }

    float base = ExperienceForDestroyedCost(type.GetCost());

    AddExp(coef * base);
}

namespace Poseidon
{
bool DefFindFreePositionCallback(Vector3Par pos, void* context)
{
    return true;
}
} // namespace Poseidon

bool AIUnit::FindFreePosition(Vector3& pos, Vector3& normal, bool soldier, EntityAI* veh,
                              FindFreePositionCallback* isFree, void* context)
{
    OperMap map;
    int mask = MASK_AVOID_OBJECTS | MASK_AVOID_VEHICLES;
    float xf = pos.X() * InvLandGrid;
    float zf = pos.Z() * InvLandGrid;
    int x = toIntFloor(xf);
    int z = toIntFloor(zf);
    int border = 1;
    int xMin = x - border;
    int xMax = x + border + 1;
    int zMin = z - border;
    int zMax = z + border + 1;
    float dx, dz;
    if (xMax <= 0 || xMin >= LandRange || zMax <= 0 || zMin >= LandRange)
    {
        pos[1] = GLOB_LAND->GetSeaLevel();
        normal = VUp;
        return false;
    }
    if (xMin < 0)
    {
        xMin = 0;
    }
    if (xMax > LandRange)
    {
        xMax = LandRange;
    }
    if (zMin < 0)
    {
        zMin = 0;
    }
    if (zMax > LandRange)
    {
        zMax = LandRange;
    }
    for (z = zMin; z < zMax; z++)
    {
        for (x = xMin; x < xMax; x++)
        {
            map.CreateField(x, z, mask, veh);
        }
    }

    bool ok = true;
    x = toIntFloor(pos.X() * InvOperItemGrid);
    z = toIntFloor(pos.Z() * InvOperItemGrid);
    if (map.GetFieldCost(x, z, true, veh, soldier) >= GET_UNACCESSIBLE || !isFree(pos, context))
    {
        ok = map.FindNearestEmpty(x, z, xf, zf, xMin * OperItemRange, zMin * OperItemRange,
                                  (xMax - xMin) * OperItemRange, (zMax - zMin) * OperItemRange, true, veh, soldier,
                                  isFree, context);

        if (ok)
        {
            AI_ERROR(map.GetFieldCost(x, z, true, veh, soldier) < GET_UNACCESSIBLE);
        }

        pos[0] = x * OperItemGrid + 0.5 * OperItemGrid;
        pos[2] = z * OperItemGrid + 0.5 * OperItemGrid;
        pos[1] = GLOB_LAND->RoadSurfaceYAboveWater(pos[0], pos[2], &dx, &dz);
    }
    else
    {
        // given field is accessible. Check neighbouring fields.
        // If all are acessible, do not channge pos, at it is certainly valid
        // ignore locks
        if (map.GetFieldCost(x - 1, z - 1, false, veh, soldier) >= GET_UNACCESSIBLE ||
            map.GetFieldCost(x, z - 1, false, veh, soldier) >= GET_UNACCESSIBLE ||
            map.GetFieldCost(x + 1, z - 1, false, veh, soldier) >= GET_UNACCESSIBLE ||
            map.GetFieldCost(x - 1, z, false, veh, soldier) >= GET_UNACCESSIBLE ||
            map.GetFieldCost(x + 1, z, false, veh, soldier) >= GET_UNACCESSIBLE ||
            map.GetFieldCost(x - 1, z + 1, false, veh, soldier) >= GET_UNACCESSIBLE ||
            map.GetFieldCost(x, z + 1, false, veh, soldier) >= GET_UNACCESSIBLE ||
            map.GetFieldCost(x + 1, z + 1, false, veh, soldier) >= GET_UNACCESSIBLE)
        {
            pos[0] = x * OperItemGrid + 0.5 * OperItemGrid;
            pos[2] = z * OperItemGrid + 0.5 * OperItemGrid;
            pos[1] = GLOB_LAND->RoadSurfaceYAboveWater(pos[0], pos[2], &dx, &dz);
        }
        else
        {
            pos[1] = GLOB_LAND->RoadSurfaceYAboveWater(pos, &dx, &dz);
        }
    }

    map.ClearMap();
    if (soldier)
    {
        normal = VUp;
    }
    else
    {
        // use different
        pos[1] -= veh->GetShape()->Min().Y();
        normal = Vector3(-dx, 1, -dz);
    }
    return ok;
}

bool AIUnit::FindFreePosition()
{
    Point3 pos = Position();
    Vector3 normal;
    EntityAI* vehicle = GetVehicle();
    if (FindFreePosition(pos, normal, IsSoldier(), vehicle))
    {
        Matrix4 transform = vehicle->Transform();
        transform.SetPosition(pos);
        transform.SetUpAndDirection(normal, vehicle->Direction());
        vehicle->MoveNetAware(transform);
        return true;
    }

    return false;
}

bool AIUnit::FindFreePosition(Vector3& pos, Vector3& normal)
{
    EntityAI* vehicle = GetVehicle();
    return FindFreePosition(pos, normal, IsSoldier(), vehicle);
}

void AIUnit::RefreshMission()
{
    if (_state != Stopped && _state != Stopping && _state != Delay && _state != InCargo)
        Verify(SetState(AIUnit::Wait));

    AIGroup* grp = GetGroup();
    if (!grp)
    {
        return;
    }

    if (grp->Leader() == this && !GetSubgroup()->HasCommand())
    {
        grp->DoRefresh();
    }
}

float AIUnit::GetTimeToLive() const
{
    const float MinTimeToLive = 5.0;
    // assume we can live max. 24 hours
    float maxTimeToLive = 3600 * 24.0;
    // when in combat, assume we can live max. 1 hour
    if (GetCombatMode() >= CMCombat)
    {
        maxTimeToLive = 3600;
    }

    if (!GetGroup())
    {
        Fail("No group in GetTimeToLive");
        return MinTimeToLive;
    }

    // exposure is updated quite slowly
    // when we calculate timeToLive from current target,
    // we will almost certainly not underestimate

    /*
    Target *tgt = GetTargetAssigned();
    if (tgt)
    {
        EntityAI *veh = GetVehicle();
        float dist2 = veh->Position().Distance2(tgt->AimingPosition());
        float visibility = 1;
        Threat threat = tgt->type->GetDammagePerMinute(dist2,visibility);
        VehicleKind kind = veh->GetType()->GetKind();
        float dpm = threat[kind];

        if (dpm>0)
        {
            float ttl = 60 * GetVehicle()->GetArmor() / dpm;
            saturateMin(maxTimeToLive,ttl);
        }
    }
    */

    int x = toIntFloor(GetVehicle()->Position().X() * InvLandGrid);
    int z = toIntFloor(GetVehicle()->Position().Z() * InvLandGrid);
    float exposure;
    if (IsHoldingFire())
    {
        exposure = GetGroup()->GetCenter()->GetExposurePessimistic(x, z);
    }
    else
    {
        exposure = GetGroup()->GetCenter()->GetExposureOptimistic(x, z);
    }
    if (exposure <= 0)
    {
        return maxTimeToLive;
    }

    float timeToLive = 60 * GetVehicle()->GetArmor() / exposure;
    saturate(timeToLive, MinTimeToLive, maxTimeToLive);
    return timeToLive;
}

void AIUnit::AllowGetIn(bool flag)
{
    _getInAllowed = flag;
    if (!flag)
    {
        AISubgroup* subgrp = GetSubgroup();
        if (subgrp)
        {
            subgrp->ClearGetInCommands();
            AIGroup* grp = subgrp->GetGroup();
            if (grp)
            {
                grp->ClearGetInCommands(this);
            }
        }
    }
}

void AIUnit::UnassignVehicle()
{
    if (_vehicleAssigned)
    {
        _vehicleAssigned->RemoveAssignement(this);
        _vehicleAssigned = nullptr;
    }
}

bool AIUnit::AssignAsDriver(Transport* veh)
{
    if (!veh->GetType()->HasDriver())
    {
        return false;
    }

    if (veh && veh == _vehicleAssigned && veh->GetDriverAssigned() == this)
    {
        return true; // already assigned as driver
    }

    if (_vehicleAssigned) // unassign
    {
        _vehicleAssigned->RemoveAssignement(this);
        _vehicleAssigned = nullptr;
    }

    if (veh)
    {
        AIUnit* oldDriver = veh->GetDriverAssigned();
        if (oldDriver) // unassign
        {
            if (veh->Driver() && !veh->Driver()->IsDammageDestroyed())
            { // cannot assign - driver is inside
                AI_ERROR(veh->DriverBrain() == oldDriver);
                return false;
            }
            veh->RemoveAssignement(oldDriver);
            oldDriver->_vehicleAssigned = nullptr;
        }
        veh->AssignDriver(this);
        _vehicleAssigned = veh;
    }
    return true;
}

bool AIUnit::AssignAsGunner(Transport* veh)
{
    if (!veh->GetType()->HasGunner())
    {
        return false;
    }

    if (veh == _vehicleAssigned && veh->GetGunnerAssigned() == this)
    {
        return true; // already assigned as gunner
    }

    if (_vehicleAssigned) // unassign
    {
        _vehicleAssigned->RemoveAssignement(this);
        _vehicleAssigned = nullptr;
    }

    if (veh)
    {
        AIUnit* oldGunner = veh->GetGunnerAssigned();
        if (oldGunner) // unassign
        {
            if (veh->Gunner() && !veh->Gunner()->IsDammageDestroyed())
            { // cannot assign - driver is inside
                AI_ERROR(veh->GunnerBrain() == oldGunner);
                return false;
            }
            veh->RemoveAssignement(oldGunner);
            oldGunner->_vehicleAssigned = nullptr;
        }
        veh->AssignGunner(this);
        _vehicleAssigned = veh;
    }
    return true;
}

bool AIUnit::AssignAsCommander(Transport* veh)
{
    if (veh && !veh->GetType()->HasCommander())
    {
        return false;
    }

    if (veh && veh == _vehicleAssigned && veh->GetCommanderAssigned() == this)
    {
        return true; // already assigned as commander
    }

    if (_vehicleAssigned) // unassign
    {
        _vehicleAssigned->RemoveAssignement(this);
        _vehicleAssigned = nullptr;
    }

    if (veh)
    {
        AIUnit* oldCommander = veh->GetCommanderAssigned();
        if (oldCommander) // unassign
        {
            if (veh->Commander() && !veh->Commander()->IsDammageDestroyed())
            { // cannot assign - driver is inside
                AI_ERROR(veh->CommanderBrain() == oldCommander);
                return false;
            }
            veh->RemoveAssignement(oldCommander);
            oldCommander->_vehicleAssigned = nullptr;
        }
        veh->AssignCommander(this);
        _vehicleAssigned = veh;
    }
    return true;
}

bool AIUnit::AssignAsCargo(Transport* veh)
{
    if (veh && veh == _vehicleAssigned && veh->GetCommanderAssigned() != this && veh->GetDriverAssigned() != this &&
        veh->GetGunnerAssigned() != this)
    {
        bool found = false;
        for (int i = 0; i < veh->NCargoAssigned(); i++)
        {
            if (veh->GetCargoAssigned(i) == this)
            {
                found = true;
                break;
            }
        }
        AI_ERROR(found);
        return true; // already assigned as cargo
    }

    if (_vehicleAssigned) // unassign
    {
        _vehicleAssigned->RemoveAssignement(this);
        _vehicleAssigned = nullptr;
    }

    if (veh)
    {
        int n = veh->NCargoAssigned();
        if (n >= veh->Type()->GetMaxManCargo())
        {
            for (int i = 0; i < n; i++)
            {
                AIUnit* oldCargo = veh->GetCargoAssigned(i);
                if (oldCargo->GetVehicle() == veh && !oldCargo->GetPerson()->IsDammageDestroyed())
                {
                    AI_ERROR(oldCargo->IsInCargo());
                    continue;
                }
                AI_ERROR(oldCargo->IsFreeSoldier() || oldCargo->GetPerson()->IsDammageDestroyed());
                // unassign
                veh->RemoveAssignement(oldCargo);
                oldCargo->_vehicleAssigned = nullptr;
                break;
            }
        }
        n = veh->NCargoAssigned();
        if (n >= veh->Type()->GetMaxManCargo())
        {
            return false; // cannot assign - cargo is full
        }
        veh->AssignCargo(this);
        _vehicleAssigned = veh;
    }
    return true;
}

RString AIUnit::GetDebugName() const
{
    if (!GetGroup())
    {
        return RString("no group");
    }
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s:%d", (const char*)GetGroup()->GetDebugName(), ID());
    Person* person = GetPerson();
    if (person && person->IsNetworkPlayer())
    {
        sprintf(buffer + strlen(buffer), " (%s)", (const char*)person->GetInfo()._name);
    }
    if (person && !person->IsLocal())
    {
        strncat(buffer, " REMOTE", sizeof(buffer) - strlen(buffer) - 1);
    }
    return buffer;
}

bool AIUnit::AssertValid() const
{
    bool ok = true;
    if (!GetPerson())
    {
        // all units must be represented by some soldier
        Fail("no person");
        ok = false;
    }

    AISubgroup* subgrp = GetSubgroup();
    if (subgrp)
    {
        AIGroup* grp = subgrp->GetGroup();
        if (grp)
        {
            AICenter* center = grp->GetCenter();
            if (!center)
            {
                Fail("no center");
                ok = false;
            }
        }
        else
        {
            Fail("no group");
            ok = false;
        }
    }
    else
    {
        Fail("no subgroup");
        ok = false;
    }

    return ok;
}

void AIUnit::Dump(int indent) const {}
