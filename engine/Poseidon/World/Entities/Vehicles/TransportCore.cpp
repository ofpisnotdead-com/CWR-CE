#include <Poseidon/Core/Application.hpp>
#include <Poseidon/World/Entities/Vehicles/Transport.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/Entities/Weapons/ProxyWeapon.hpp>
#include <Poseidon/World/Entities/Infantry/Person.hpp>
#include <Poseidon/UI/Settings/AspectRatio.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Random/randomGen.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Dev/Diag/DiagModes.hpp>
#include <Poseidon/Dev/Debug/DebugCheats.hpp>
#include <string.h>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

#include <Poseidon/Game/TitEffects.hpp>
#include <Poseidon/World/Scene/Camera/CamEffects.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/Game/UiActions.hpp>
#include <Poseidon/World/Simulation/FrameInv.hpp>
#include <Poseidon/World/Entities/Infantry/MoveActions.hpp>
#include <Poseidon/World/Entities/Infantry/ManActs.hpp>
#include <Poseidon/Graphics/Rendering/Draw/SpecLods.hpp>
#include <Poseidon/Foundation/Enums/EnumNames.hpp>

namespace Poseidon
{
using namespace Dev;

void Transport::DriverConstruct(Person* driver)
{
    _driver = driver;
    // change brain vehicle
    if (driver)
    {
        AIUnit* brain = _driver->Brain();
        if (brain)
        {
            brain->SetVehicleIn(this);
        }
    }
}

void Transport::PlaceOnSurface(Matrix4& trans)
{
    if (!GetShape())
    {
        return;
    }

    Vector3 pos = trans.Position();
    Matrix3 orient = trans.Orientation();

    float dx, dz;
    pos[1] = GLandscape->RoadSurfaceYAboveWater(pos, &dx, &dz);

    if (!Type()->HasDriver())
    {
        pos += orient * GetShape()->BoundingCenter();
    }
    else
    {
        Vector3 up(-dx, 1, -dz);
        Matrix3 orient;
        orient.SetUpAndDirection(up, trans.Direction());
        trans.SetOrientation(orient);

        Shape* geom = _shape->LandContactLevel();
        if (!geom)
        {
            geom = _shape->GeometryLevel();
        }
        if (!geom)
        {
            geom = _shape->Level(0);
        }

        if (geom)
        {
            Vector3 minC(0, geom->Min().Y(), 0);
            pos -= orient * minC;
        }
    }
    trans.SetPosition(pos);
}

void Transport::EngineOn()
{
    bool isStatic = GetType()->GetFuelCapacity() <= 0;
    if (isStatic)
    {
        return;
    }
    if (!_engineOff)
    {
        return;
    }
    _engineOff = false;
    OnEvent(EEEngine, !_engineOff);
}

void Transport::EngineOff()
{
    if (_engineOff)
    {
        return;
    }
    if (!EngineCanBeOff())
    {
        return;
    }
    _engineOff = true;
    OnEvent(EEEngine, !_engineOff);
}

AIUnit* Transport::CommanderBrain() const
{
    return _commander ? _commander->Brain() : nullptr;
}

Vector3 Transport::GetEnginePos() const
{
    return Vector3(0, 0, -2);
}

Vector3 Transport::GetEnvironPos() const
{
    return Vector3(0, 0, 2);
}

AIUnit* Transport::DriverBrain() const
{
    return _driver ? _driver->Brain() : nullptr;
}

AIUnit* Transport::GunnerBrain() const
{
    return _gunner ? _gunner->Brain() : nullptr;
}

AIUnit* Transport::ObserverUnit() const
{
    return _commander ? _commander->Brain() : nullptr;
}

AIUnit* Transport::CommanderUnit() const
{
    if (_effCommander)
    {
        AIUnit* unit = _effCommander->Brain();
        if (unit && unit->GetVehicleIn() == this)
        {
            return unit;
        }
    }
    if (CommanderBrain())
    {
        return CommanderBrain();
    }
    else if (Type()->DriverIsCommander())
    {
        if (DriverBrain())
        {
            return DriverBrain();
        }
        else
        {
            return GunnerBrain();
        }
    }
    else
    {
        if (GunnerBrain())
        {
            return GunnerBrain();
        }
        else
        {
            return DriverBrain();
        }
    }
}

AIUnit* Transport::PilotUnit() const
{
    return DriverBrain();
}

AIUnit* Transport::GunnerUnit() const
{
    if (Type()->_hasGunner)
    {
        return GunnerBrain();
    }
    else
    {
        return DriverBrain();
    }
}

AIUnit* Transport::EffectiveGunnerUnit() const
{
    if (_manualFire)
    {
        return CommanderUnit();
    }
    else
    {
        return GunnerUnit();
    }
}

TargetSide Transport::GetTargetSide() const
{
    if (!IsDammageDestroyed())
    {
        if (Commander() && !Commander()->IsDammageDestroyed())
        {
            goto NotEmpty;
        }
        if (Driver() && !Driver()->IsDammageDestroyed())
        {
            goto NotEmpty;
        }
        if (Gunner() && !Gunner()->IsDammageDestroyed())
        {
            goto NotEmpty;
        }
        for (int i = 0; i < GetManCargo().Size(); i++)
        {
            if (GetManCargo()[i] && !GetManCargo()[i])
            {
                goto NotEmpty;
            }
        }
        return TCivilian;
    }
NotEmpty:
    return base::GetTargetSide();
}

float Transport::VisibleLights() const
{
    return _pilotLight;
}

bool Transport::QIsManual() const
{
    if (!GLOB_WORLD->PlayerManual())
    {
        return false;
    }
    if (!GLOB_WORLD->PlayerOn())
    {
        return false;
    }
    if (GLOB_WORLD->PlayerOn() == Commander())
    {
        return true;
    }
    if (GLOB_WORLD->PlayerOn() == Driver())
    {
        return true;
    }
    if (GLOB_WORLD->PlayerOn() == Gunner())
    {
        return true;
    }
    return false;
}

bool Transport::QIsManual(const AIUnit* unit) const
{
    if (!GLOB_WORLD->PlayerManual())
    {
        return false;
    }
    if (!GLOB_WORLD->PlayerOn())
    {
        return false;
    }
    return unit == GLOB_WORLD->PlayerOn()->Brain();
}

static void RemoveMan(Person* man, EntityAI* killer)
{
    GLOB_WORLD->RemoveSensor(man);
}

static bool CrewDammage(Transport* transport, Person* man, EntityAI* killer, float overkill, RString ammo)
{
    if (!man)
    {
        return false;
    }
    float dammage = overkill * (GRandGen.RandomValue() * 0.5 + 0.5);
    {
        if (dammage >= 0)
        {
            if (man->IsLocal())
            {
                man->DoDammage(killer, VZero, dammage, 1.0, ammo);
                if (man->GetNetworkId().creator == 1)
                {
                    GetNetworkManager().AskForDammage(man, killer, VZero, dammage, 1.0, ammo);
                }
            }
            else
            {
                GetNetworkManager().AskForDammage(man, killer, VZero, dammage, 1.0, ammo);
            }

            if (man->IsDammageDestroyed())
            {
                RemoveMan(man, killer);
                return true;
            }
        }
    }
    return false;
}

void Transport::DammageCrew(EntityAI* killer, float howMuch, RString ammo)
{
    if (!IsLocal())
    {
        LOG_ERROR(Physics, "Canot dammage remote transport {}", (const char*)GetDebugName());
        // return;
    }
    for (int i = _manCargo.Size(); --i >= 0;)
    {
        Person* man = _manCargo[i];
        if (!man)
        {
            continue;
        }
        if (CrewDammage(this, man, killer, howMuch, ammo))
        {
            if (man)
            {
                man->KilledBy(killer);
            }
        }
    }
    if (CrewDammage(this, _driver, killer, howMuch, ammo))
    {
        if (_driver)
        {
            _driver->KilledBy(killer);
        }
    }
    if (CrewDammage(this, _gunner, killer, howMuch, ammo))
    {
        if (_gunner)
        {
            _gunner->KilledBy(killer);
        }
    }
    if (CrewDammage(this, _commander, killer, howMuch, ammo))
    {
        if (_commander)
        {
            _commander->KilledBy(killer);
        }
    }

    if (!IsPossibleToGetIn() || !IsAbleToMove())
    {
        if (_getOutAfterDammage > Glob.time + 15)
        { // not set yet
            _getOutAfterDammage = Glob.time + GRandGen.Gauss(1.5, 3, 7);
        }
    }
}

void Transport::HitBy(EntityAI* killer, float howMuch, RString ammo)
{
    if (!IsLocal())
    {
        return;
    }
    base::HitBy(killer, howMuch, ammo);
    DammageCrew(killer, howMuch, ammo);
}

void Transport::Destroy(EntityAI* killer, float overkill, float minExp, float maxExp)
{
    base::Destroy(killer, overkill, minExp, maxExp);
    DammageCrew(killer, overkill, "");
    GLOB_WORLD->RemoveSensor(this);
    UpdateStop();
}

bool Transport::EjectCrew(Person* man)
{
    AIUnit* unit = man->Brain();
    if (!unit)
    {
        return false;
    }
    AIGroup* group = unit->GetGroup();
    if (!group)
    {
        return false;
    }

    Eject(unit);

    if (group)
    {
        group->UnassignVehicle(this);
    }
    return true;
}

bool Transport::EjectIfAlive(Person* man)
{
    if (!man || man->IsDammageDestroyed())
    {
        return false;
    }
    if (man->IsNetworkPlayer())
    {
        return false;
    }
    AIUnit* unit = man->Brain();
    if (!unit || unit->GetLifeState() != AIUnit::LSAlive)
    {
        return false;
    }

    AIGroup* group = unit->GetGroup();

    Eject(unit);

    if (group)
    {
        group->UnassignVehicle(this);
    }

    return true;
}

bool Transport::EjectIfDead(Person* man)
{
    if (!man || !man->IsDammageDestroyed())
    {
        return false;
    }
    AIUnit* unit = man->Brain();
    if (!unit)
    {
        return false;
    }
    PoseidonAssert(IsLocal());
    unit->DoGetOut(this, false);
    return true;
}

void Transport::EjectAllNotFixed()
{
    if (Driver() && Type()->_ejectDeadDriver)
    {
        EjectCrew(Driver());
    }
    if (Commander() && Type()->_ejectDeadCommander)
    {
        EjectCrew(Commander());
    }
    if (Gunner() && Type()->_ejectDeadGunner)
    {
        EjectCrew(Gunner());
    }
    if (Type()->_ejectDeadCargo)
    {
        for (int i = 0; i < GetManCargo().Size(); i++)
        {
            Person* crew = GetManCargo()[i];
            if (crew)
            {
                EjectCrew(crew);
            }
        }
    }
}

void Transport::SetDammage(float dammage)
{
    bool doRepair = dammage < GetTotalDammage();
    base::SetDammage(dammage);
    if (doRepair)
    {
        _getOutAfterDammage = TIME_MAX;
        _explosionTime = TIME_MAX;
    }
}

void Transport::ReactToDammage()
{
    if (IsDammageDestroyed())
    {
        _isDead = true;
    }
    if (_isDead || _isUpsideDown)
    {
        EngineOff();
    }

    if (Glob.time > _getOutAfterDammage)
    {
        for (int i = _manCargo.Size(); --i >= 0;)
        {
            EjectIfAlive(_manCargo[i]);
        }
        EjectIfAlive(_driver);
        EjectIfAlive(_gunner);
        EjectIfAlive(_commander);
    }

    if (IsLocal())
    {
        if (Type()->_ejectDeadDriver)
        {
            EjectIfDead(_driver);
        }
        if (Type()->_ejectDeadGunner)
        {
            EjectIfDead(_gunner);
        }
        if (Type()->_ejectDeadDriver)
        {
            EjectIfDead(_commander);
        }
        if (Type()->_ejectDeadCargo)
        {
            for (int i = _manCargo.Size(); --i >= 0;)
            {
                EjectIfDead(_manCargo[i]);
            }
        }
    }

    if (Glob.time > _explosionTime)
    {
        if (GetRawTotalDammage() < 1)
        {
            EntityAI* killer = this;
            if (_lastDammage && _lastDammageTime > Glob.time - 60)
            {
                killer = _lastDammage;
            }

            DammageCrew(killer, 10, "");
            Destroy(killer, 1.0, 0.5, 0.75);
            SetTotalDammage(1);
        }
    }
}

float Transport::GetExplosives() const
{
    return base::GetExplosives();
}

void Transport::CrashDammage(float ammount, const Vector3& pos)
{
    if (IsLocal())
    {
        ammount *= GetType()->GetInvArmor();
        LocalDammage(nullptr, this, pos, ammount, GetRadius());
    }
}

void Transport::RemoveAssignement(AIUnit* unit)
{
    if (_driverAssigned == unit)
    {
        _driverAssigned = nullptr;
    }
    if (_gunnerAssigned == unit)
    {
        _gunnerAssigned = nullptr;
    }
    if (_commanderAssigned == unit)
    {
        _commanderAssigned = nullptr;
    }
    int i, n = _cargoAssigned.Size();
    for (i = 0; i < n; i++)
    {
        if (_cargoAssigned[i] == unit)
        {
            _cargoAssigned[i] = nullptr;
        }
    }
    _cargoAssigned.Compact();
}

void Transport::KeyboardAny(AIUnit* unit, float deltaT) {}

ManVehAction Transport::DriverAction() const
{
    if (_driverHidden > 0.5)
    {
        return Type()->_driverInAction;
    }
    return Type()->_driverAction;
}
ManVehAction Transport::CommanderAction() const
{
    if (_commanderHidden > 0.5)
    {
        return Type()->_commanderInAction;
    }
    return Type()->_commanderAction;
}
ManVehAction Transport::GunnerAction() const
{
    if (_gunnerHidden > 0.5)
    {
        return Type()->_gunnerInAction;
    }
    return Type()->_gunnerAction;
}
ManVehAction Transport::CargoAction(int pos) const
{
    int maxPos = Type()->_cargoAction.Size() - 1;
    if (maxPos < 0)
    {
        return ManVehActNone;
    }
    saturateMin(pos, maxPos);
    return Type()->_cargoAction[pos];
}

float Transport::DriverAnimSpeed() const
{
    return 1;
}
float Transport::CommanderAnimSpeed() const
{
    return 1;
}
float Transport::GunnerAnimSpeed() const
{
    return 1;
}
float Transport::CargoAnimSpeed(int position) const
{
    return 1;
}

bool Transport::IsPersonHidden(Person* person) const
{
    if (person == Commander())
    {
        return IsCommanderHidden();
    }
    else if (person == Driver())
    {
        return IsDriverHidden();
    }
    else if (person == Gunner())
    {
        return IsGunnerHidden();
    }

    return false;
}

void Transport::HidePerson(Person* person, float hide)
{
    if (person == Commander())
    {
        HideCommander(hide);
    }
    else if (person == Driver())
    {
        HideDriver(hide);
    }
    else if (person == Gunner())
    {
        HideGunner(hide);
    }
}

void Transport::AimDriver(Vector3Par direction)
{
    if (QIsManual())
    {
        if (InputSubsystem::Instance().IsLookAroundEnabled())
        {
            _mouseDirWanted = Direction();
            //_mouseTurnWanted = 0;
        }
        else
        {
            _mouseDirWanted = direction;
            // Vector3 relDir(VMultiply,DirWorldToModel(),direction);
            //_mouseTurnWanted = atan2(relDir.X(),relDir.Z()) * 0.7;
        }
    }
}

void Transport::SelectWeaponCommander(AIUnit* unit, int weapon)
{
    // used when weapon is selected from UI
    if (unit == GunnerUnit() || IsManualFire())
    {
        base::SelectWeaponCommander(unit, weapon);
    }
    else
    {
        if (IsLocal())
        {
            SendLoad(weapon);
        }
        else
        {
            RadioMessageVLoad msg(this, weapon);
            GetNetworkManager().SendRadioMessage(&msg);
        }
    }
}

void Transport::AICommander(AIUnit* unit, float deltaT)
{
    PoseidonAssert(unit);
    PoseidonAssert(unit->GetSubgroup());

    if (unit != PilotUnit())
    {
        if (unit->IsSubgroupLeader())
        {
            if (unit->GetPlanningMode() == AIUnit::LeaderPlanned)
            {
                // new valid path
                Vector3Val wantedPosition = unit->GetWantedPosition();
                float prec = GetPrecision();
                if (_moveMode != VMMMove || wantedPosition.Distance2(_driverPos) > 0)
                {
                    if (wantedPosition.Distance2(_driverPos) > Square(prec))
                    {
                        if (_radio.IsEmpty()) // radio protocol is empty
                        {
                            SendMove(wantedPosition);
                        }
                    }
                    else
                    {
                        _moveMode = VMMMove;
                        _driverPos = wantedPosition;
                    }
                }
            }
        }
        else
        {
            if (_moveMode != VMMFormation && _radio.IsEmpty())
            {
                SendJoin();
            }
        }
    }

    if (unit == GunnerUnit())
    {
        SelectFireWeapon();
        if (_fire._fireMode >= 0)
        {
            SelectWeapon(_fire._fireMode);
        }
    }
    else
    {
        // old state
        int oldWeapon = _commanderFire._fireMode;
        // commander must remember his decision
        SelectFireWeapon(_commanderFire);

        if (_commanderFire._firePrepareOnly)
        {
            Target* tgt = _commanderFire._fireTarget;
            if (tgt)
            {
                AIGroup* grp = unit->GetGroup();
                if (grp && !grp->IsAnyPlayerGroup() || tgt != unit->GetTargetAssigned())
                {
                    AICenter* center = grp->GetCenter();
                    if (tgt->State(unit) < TargetEnemyEmpty || center->IsFriendly(tgt->side))
                    {
                        _commanderFire._fireTarget = nullptr;
                        _commanderFire._fireMode = -1;
                        if (tgt == unit->GetTargetAssigned())
                        {
                            unit->AssignTarget(nullptr);
                        }
                    }
                }
            }
        }

        if (_commanderFire._fireMode >= 0 && _commanderFire._fireMode != oldWeapon)
        {
            if (IsLocal())
            {
                SendLoad(_commanderFire._fireMode);
            }
            else
            {
                RadioMessageVLoad msg(this, _commanderFire._fireMode);
                GetNetworkManager().SendRadioMessage(&msg);
            }
        }

        if (_radio.IsEmpty())
        {
            if (_commanderFire._fireTarget != _fire._fireTarget)
            {
                if (_commanderFire._fireTarget)
                {
                    SendTarget(_commanderFire._fireTarget);
                }
                else if (_radio.IsSilent())
                {
                    // when channel is silent, we may transmit "no target"
                    // (very low priority message)
                    SendTarget(nullptr);
                }
            }
            else if (!_commanderFire._firePrepareOnly)
            {
                if (_fire._firePrepareOnly && _commanderFire._fireTarget)
                {
                    if (_currentWeapon >= 0 && _currentWeapon < NMagazineSlots() && GetWeaponLoaded(_currentWeapon) &&
                        GetAimed(_currentWeapon, _commanderFire._fireTarget) >= 0.75 &&
                        GetWeaponReady(_currentWeapon, _commanderFire._fireTarget))
                    {
                        if (!GetAIFireEnabled(_commanderFire._fireTarget))
                        {
                            ReportFireReady();
                        }
                        else
                        {
                            SendSimpleCommand(SCFire);
                        }
                    }
                }
            }
            else
            {
                if (!_fire._firePrepareOnly)
                {
                    SendSimpleCommand(SCCeaseFire);
                }
            }
        }
    }

    if (Type()->_hideProxyInCombat)
    {
        if (unit->GetCombatMode() >= CMCombat || Type()->_forceHideCommander)
        {
            Person* person = unit->GetPerson();
            HidePerson(person, 1);
        }
        else if (unit->GetCombatMode() <= CMSafe)
        {
            Person* person = unit->GetPerson();
            HidePerson(person, 0);
        }
    }
}

static void ReportIfDead(AIUnit*& unit, AIUnit* reportedBy)
{
    if (unit && unit->GetLifeState() != AIUnit::LSAlive)
    {
        if (reportedBy)
        {
            AIGroup* grp = unit->GetGroup();
            if (grp)
            {
                grp->SetReportBeforeTime(unit, Glob.time + 10);
            }
        }
        unit = nullptr;
    }
}

bool Transport::SimulateUnits(float deltaT)
{
    bool manual = GLOB_WORLD->PlayerManual() && GLOB_WORLD->PlayerOn();
    AIUnit* player = manual ? GLOB_WORLD->PlayerOn()->Brain() : nullptr;

#if _ENABLE_CHEATS
    extern bool disableUnitAI;
    if (disableUnitAI)
        return true;
#endif

    bool isCommanderHidden = false;

    AIUnit* commanderUnit = CommanderUnit();
    AIUnit* gunnerUnit = GunnerUnit();
    AIUnit* driverUnit = PilotUnit();

    AIUnit* reportedBy = nullptr;
    if (driverUnit && driverUnit->GetLifeState() == AIUnit::LSAlive)
    {
        reportedBy = driverUnit;
    }
    if (gunnerUnit && gunnerUnit->GetLifeState() == AIUnit::LSAlive)
    {
        reportedBy = gunnerUnit;
    }
    if (commanderUnit && commanderUnit->GetLifeState() == AIUnit::LSAlive)
    {
        reportedBy = commanderUnit;
    }
    for (int i = 0; i < _manCargo.Size(); i++)
    {
        Person* person = _manCargo[i];
        if (!person)
        {
            continue;
        }
        AIUnit* unit = person->Brain();
        if (!unit || unit->GetLifeState() != AIUnit::LSAlive)
        {
            continue;
        }
        reportedBy = unit;
    }

    ReportIfDead(gunnerUnit, reportedBy);
    ReportIfDead(driverUnit, reportedBy);
    ReportIfDead(commanderUnit, reportedBy);

    if (commanderUnit && commanderUnit->GetLifeState() == AIUnit::LSAlive)
    {
        if (manual && commanderUnit == player)
        {
            if (commanderUnit->IsGroupLeader())
            {
                Person* person = commanderUnit->GetPerson();
                const TransportType* type = Type();
                bool hidden = false;
                if (person == Commander())
                {
                    hidden = type->_hideProxyInCombat && IsCommanderHidden();
                }
                else if (person == Driver())
                {
                    hidden = type->_hideProxyInCombat && IsDriverHidden();
                }
                else if (person == Gunner())
                {
                    hidden = type->_hideProxyInCombat && IsGunnerHidden();
                }

                commanderUnit->GetGroup()->SetCombatModeMinor(hidden ? CMCombat : CMSafe);
            }
        }
        else if (commanderUnit->GetPerson()->IsRemotePlayer())
        {
        }
        else
        {
            AICommander(commanderUnit, deltaT);
        }

        isCommanderHidden = IsPersonHidden(commanderUnit->GetPerson());
    }

    if (gunnerUnit)
    {
        if (manual && gunnerUnit == player)
        {
        }
        else if (gunnerUnit->GetPerson()->IsRemotePlayer())
        {
        }
        else if (gunnerUnit->IsLocal())
        {
            if (!_fire._fireTarget || _fire.GetTargetFinished(CommanderUnit()))
            {
                _fire._fireMode = -1;
                _fire._fireTarget = nullptr;
            }
            if (_currentWeapon >= 0)
            {
                const WeaponModeType* mode = GetWeaponMode(_currentWeapon);
                if (!_fire._firePrepareOnly && mode && mode->_autoFire)
                {
                    if (_fire._fireTarget)
                    {
                        if (!_fire._fireTarget->IsKnownBy(gunnerUnit) || _fire.GetTargetFinished(gunnerUnit) ||
                            _fire._fireTarget->lastSeen < Glob.time - 10 ||
                            _fire._fireTarget->idExact && !_fire._fireTarget->idExact->LockPossible(mode->_ammo))
                        {
                            _fire.SetTarget(gunnerUnit, nullptr);
                        }
                    }

                    float timeToLive = gunnerUnit->GetTimeToLive();
                    AIGroup* grp = gunnerUnit->GetGroup();
                    AICenter* center = grp->GetCenter();
                    FireResult result;
                    if (!gunnerUnit->IsHoldingFire() &&
                        (!_fire._fireTarget || !WhatFireResult(result, *_fire._fireTarget, _currentWeapon, timeToLive)))
                    {
                        const TargetList& list = grp->GetTargetList();
                        int maxEnemies = 4;
                        for (int i = 0; i < list.Size(); i++)
                        {
                            FireResult tResult;
                            Target* tgtI = list[i];
                            if (!tgtI->IsKnownBy(gunnerUnit))
                            {
                                continue;
                            }
                            if (tgtI->State(gunnerUnit) < TargetEnemyCombat)
                            {
                                continue;
                            }
                            if (!center->IsEnemy(tgtI->side))
                            {
                                continue;
                            }
                            if (WhatFireResult(tResult, *tgtI, _currentWeapon, timeToLive))
                            {
                                if (tResult.Surplus() > result.Surplus())
                                {
                                    result = tResult;
                                    _fire.SetTarget(gunnerUnit, tgtI);
                                }
                            }
                            if (--maxEnemies <= 0)
                            {
                                break;
                            }
                        }
                    }
                }
            }
            AIGunner(gunnerUnit, deltaT);
            if (gunnerUnit && gunnerUnit != commanderUnit)
            {
                HidePerson(gunnerUnit->GetPerson(), isCommanderHidden || Type()->_forceHideGunner ? 1 : 0);
            }
        }
    }
    if (driverUnit)
    {
        if (manual && driverUnit == player)
        {
            if (!GWorld->GetPlayerSuspended())
            {
                CheckAway();
                KeyboardPilot(driverUnit, deltaT);
            }
            else
            {
                SuspendedPilot(driverUnit, deltaT);
            }
        }
        else if (!driverUnit->GetPerson()->IsLocal())
        {
            FakePilot(deltaT);
            return true;
        }
        else
        {
            AIPilot(driverUnit, deltaT);
            if (driverUnit && driverUnit != CommanderUnit())
            {
                bool hideDriver = isCommanderHidden || GetFireTarget() != nullptr;
                HidePerson(driverUnit->GetPerson(), hideDriver || Type()->_forceHideDriver);
            }
        }
        return true;
    }

    return false; // no pilot performed
}

void Transport::InitUnits()
{
    AIUnit* unit = CommanderUnit();
    if (unit && Type()->_hideProxyInCombat)
    {
        CombatMode cm = unit->GetCombatMode();
        bool hide = cm >= CMCombat;
        if (Type()->_hasDriver && Driver())
        {
            _driverHidden = _driverHiddenWanted = hide || Type()->_forceHideDriver;
            _driver->SwitchVehicleAction(DriverAction());
        }
        if (Type()->_hasGunner && Gunner())
        {
            _gunnerHidden = _gunnerHiddenWanted = hide || Type()->_forceHideGunner;
            _gunner->SwitchVehicleAction(GunnerAction());
        }
        if (Type()->_hasCommander && Commander())
        {
            _commanderHidden = _commanderHiddenWanted = hide || Type()->_forceHideCommander;
            _commander->SwitchVehicleAction(CommanderAction());
        }
    }
    base::InitUnits();
}

bool Transport::ValidateCrew(Person* crew, bool complex) const
{
    if (!crew)
    {
        return true;
    }
    bool ok = true;
    if (crew->IsInLandscape())
    {
        RptF("Error: Crew %s of %s in landscape", (const char*)crew->GetDebugName(), (const char*)GetDebugName());
        ok = false;
    }
    return ok;
}

bool Transport::Validate(bool complex) const
{
    bool ok = true;
    if (!ValidateCrew(_driver, complex))
    {
        RptF("Driver invalid");
        ok = false;
    }
    if (!ValidateCrew(_commander, complex))
    {
        RptF("Commander invalid");
        ok = false;
    }
    if (!ValidateCrew(_gunner, complex))
    {
        RptF("Gunner invalid");
        ok = false;
    }
    for (int i = 0; i < _manCargo.Size(); i++)
    {
        if (!ValidateCrew(_manCargo[i], complex))
        {
            RptF("ManCargo %d invalid", i);
            ok = false;
        }
    }
    return ok;
}

void Transport::Init(Matrix4Par pos)
{
    if (!EngineCanBeOff())
    {
        _engineOff = false;
    }
    base::Init(pos);
}

void Transport::TrackTargets(TargetList& res, bool initialize, float trackTargetsPeriod)
{
    const VehicleType* type = GetType();
    AIUnit* unit = CommanderBrain();
    if (unit)
    {
        base::TrackTargets(res, unit, type->_commanderCanSee, initialize, 1e10, trackTargetsPeriod);
    }
    unit = GunnerBrain();
    if (unit)
    {
        base::TrackTargets(res, unit, type->_gunnerCanSee, initialize, 1e10, trackTargetsPeriod);
    }
    unit = DriverBrain();
    if (unit)
    {
        base::TrackTargets(res, unit, type->_driverCanSee, initialize, 1e10, trackTargetsPeriod);
    }

    _trackTargetsTime = Glob.time;
}

Vector3 Transport::GetCameraDirection(CameraType camType) const
{
    Matrix4 transf;
    if (GetProxyCamera(transf, camType))
    {
        return DirectionModelToWorld(transf.Direction());
    }
    return Direction();
}

Matrix4 Transport::InsideCamera(CameraType camType) const
{
    Matrix4 transf;
    if (camType == CamGunner && GetOpticsCamera(transf, camType))
    {
        return transf;
    }
    if (GetProxyCamera(transf, camType))
    {
        return transf;
    }
    return MIdentity;
}

int Transport::InsideLOD(CameraType camType) const
{
    bool hidden = false;
    AIUnit* unit = GWorld->FocusOn();
    Person* player = unit ? unit->GetPerson() : nullptr;
    if (player)
    {
        if (player == Commander())
        {
            hidden = IsCommanderHidden();
        }
        else if (player == Driver())
        {
            hidden = IsDriverHidden();
        }
        else if (player == Gunner())
        {
            hidden = IsGunnerHidden();
        }
        else
        {
            int index = GetShape()->FindSpecLevel(VIEW_CARGO);
            if (index >= 0)
            {
                return index;
            }
        }
        if (!Type()->_hideProxyInCombat)
        {
            // always use internal views if there is no in/out position
            hidden = true;
        }
        if (hidden)
        {
            if (player == Gunner())
            {
                int index = GetShape()->FindSpecLevel(VIEW_GUNNER);
                if (index < 0)
                {
                    if (camType == CamGunner)
                    {
                        index = 0;
                    }
                    else
                    {
                        if (Type()->_gunnerUsesPilotView)
                        {
                            index = GetShape()->FindSpecLevel(VIEW_PILOT);
                        }
                        else
                        {
                            index = GetShape()->FindSpecLevel(VIEW_CARGO);
                        }
                    }
                }
                return index;
            }
            else if (player == Commander())
            {
                if (camType == CamGunner)
                {
                    return 0;
                }
                int index = -1;
                if (Type()->_commanderUsesPilotView)
                {
                    index = GetShape()->FindSpecLevel(VIEW_PILOT);
                }
                else
                {
                    index = GetShape()->FindSpecLevel(VIEW_CARGO);
                }
                if (index < 0)
                {
                    index = GetShape()->FindSpecLevel(VIEW_GUNNER);
                }
                return index;
            }
            else
            {
                if (camType == CamGunner)
                {
                    return 0;
                }
                int index = GetShape()->FindSpecLevel(VIEW_PILOT);
                if (index < 0)
                {
                    index = GetShape()->FindSpecLevel(VIEW_CARGO);
                }
                return index;
            }
        }
    }

    return 0;
}

static CursorMode CursorMouseModeDetect()
{
    auto& input = InputSubsystem::Instance();
    if (input.IsKeyboardCursorMoreRecent())
    {
        return CMouseRel;
    }
    if (!input.IsMouseCursorRecentlyActive())
    {
        return CMouseRel;
    }
    return CMouseAbs;
}

CursorMode Transport::GetCursorRelMode(CameraType camType) const
{
    auto& input = InputSubsystem::Instance();
    AIUnit* unit = GWorld->FocusOn();
    Person* player = unit ? unit->GetPerson() : nullptr;
    if (player)
    {
        if (player == Driver())
        {
            if (camType == CamInternal || camType == CamExternal)
            {
                if (IsVirtual(camType))
                {
                    if (input.IsJoystickActive())
                    {
                        return CMouseRel;
                    }
                    if (!input.IsMouseCursorActive() || !input.IsMouseTurnActive() && !input.IsLookAroundEnabled())
                    {
                        return CKeyboard;
                    }
                }
                if (input.IsLookAroundEnabled())
                {
                    return CursorMouseModeDetect();
                }
                return CMouseAbs;
            }
            else if (camType == CamGunner)
            {
                return CMouseAbs;
            }
            else
            {
                return base::GetCursorRelMode(camType);
            }
        }
    }
    if (camType == CamInternal || camType == CamExternal)
    {
        return CursorMouseModeDetect();
    }
    else if (camType == CamGunner)
    {
        return CMouseAbs;
    }
    else
    {
        return base::GetCursorRelMode(camType);
    }
}

bool Transport::IsVirtual(CameraType camType) const
{
    return camType != CamGunner;
}

bool Transport::IsVirtualX(CameraType camType) const
{
    AIUnit* unit = GWorld->FocusOn();
    Person* player = unit ? unit->GetPerson() : nullptr;
    if (player)
    {
        if (player == Driver())
        {
            if (InputSubsystem::Instance().IsLookAroundEnabled())
            {
                return true;
            }
            return camType != CamInternal && camType != CamExternal;
        }
    }
    return true;
}

bool Transport::IsGunner(CameraType camType) const
{
    AIUnit* unit = GWorld->FocusOn();
    if (unit && unit == PilotUnit())
    {
        return base::IsGunner(camType);
    }
    const TransportType* type = Type();
    if (type->GetOutGunnerMayFire() || !type->_hideProxyInCombat)
    {
        if (camType == CamInternal)
        {
            return true;
        }
    }
    return camType == CamGunner || camType == CamExternal;
}

void Transport::LimitVirtual(CameraType camType, float& heading, float& dive, float& fov) const
{
    if (camType == CamInternal || camType == CamExternal)
    {
        AIUnit* unit = GWorld->FocusOn();
        Person* person = unit ? unit->GetPerson() : nullptr;
        if (person)
        {
            if (person == _commander)
            {
                Type()->_viewCommander.LimitVirtual(camType, heading, dive, fov);
            }
            else if (person == _gunner)
            {
                Type()->_viewGunner.LimitVirtual(camType, heading, dive, fov);

                // sometimes view is forced to follow weapon
                if (_gunnerHidden < 0.5 && Type()->GetOutGunnerMayFire())
                {
                    heading = 0;
                    dive = 0;
                }
            }
            else if (person == _driver)
            {
                Type()->_viewPilot.LimitVirtual(camType, heading, dive, fov);
            }
            else
            {
                Type()->_viewCargo.LimitVirtual(camType, heading, dive, fov);
            }
            return;
        }
    }
    else if (camType == CamGunner)
    {
        Type()->_viewOptics.LimitVirtual(camType, heading, dive, fov);
    }

    base::LimitVirtual(camType, heading, dive, fov);
}

void Transport::LimitCursor(CameraType camType, Vector3& dir) const {}

void Transport::InitVirtual(CameraType camType, float& heading, float& dive, float& fov) const
{
    AIUnit* unit = GWorld->FocusOn();
    Person* person = unit ? unit->GetPerson() : nullptr;
    if (camType == CamGunner)
    {
        Type()->_viewOptics.InitVirtual(camType, heading, dive, fov);
        return;
    }
    else if (person)
    {
        if (person == _commander)
        {
            Type()->_viewCommander.InitVirtual(camType, heading, dive, fov);
        }
        else if (person == _gunner)
        {
            Type()->_viewGunner.InitVirtual(camType, heading, dive, fov);
        }
        else if (person == _driver)
        {
            Type()->_viewPilot.InitVirtual(camType, heading, dive, fov);
        }
        else
        {
            Type()->_viewCargo.InitVirtual(camType, heading, dive, fov);
        }
        return;
    }
    base::InitVirtual(camType, heading, dive, fov);
}

bool Transport::ConsumeFuel(float ammount)
{
    // Infinite fuel cheat — refund positive consumption when the real
    // player is inside this vehicle.  Negative `ammount` (refuel) goes
    // through unchanged so script refuels still work.  Hook lives here
    // because ConsumeFuel is the single chokepoint Refuel() also calls
    // (it just passes -amount).
    if (ammount > 0 && DebugCheats::Cmd_InfiniteFuel::IsActive() && GWorld)
    {
        Person* p = GWorld->GetRealPlayer();
        if (p && p->Brain() && p->Brain()->GetVehicleIn() == this)
            return _fuel > 0;
    }

    bool wasFuel = _fuel > 0;
    _fuel -= ammount;
    bool isFuel = _fuel > 0;
    if (isFuel != wasFuel)
    {
        OnEvent(EEFuel, isFuel);
    }
    saturate(_fuel, 0, GetType()->GetFuelCapacity());
    return isFuel;
}

void Transport::Refuel(float ammount)
{
    ConsumeFuel(-ammount);
}

void Transport::Simulate(float deltaT, SimulationImportance prec)
{
    // turn in / turn out
    const float turnInOutSpeed = 1.0;
    if (_commanderHiddenWanted > _commanderHidden)
    {
        _commanderHidden += turnInOutSpeed * deltaT;
        saturateMin(_commanderHidden, _commanderHiddenWanted);
        if (_commander)
        {
            _commander->SwitchVehicleAction(CommanderAction());
        }
    }
    else if (_commanderHiddenWanted < _commanderHidden)
    {
        _commanderHidden -= turnInOutSpeed * deltaT;
        saturateMax(_commanderHidden, _commanderHiddenWanted);
        if (_commander)
        {
            _commander->SwitchVehicleAction(CommanderAction());
        }
    }
    if (_driverHiddenWanted > _driverHidden)
    {
        _driverHidden += turnInOutSpeed * deltaT;
        saturateMin(_driverHidden, _driverHiddenWanted);
        if (_driver)
        {
            _driver->SwitchVehicleAction(DriverAction());
        }
    }
    else if (_driverHiddenWanted < _driverHidden)
    {
        _driverHidden -= turnInOutSpeed * deltaT;
        saturateMax(_driverHidden, _driverHiddenWanted);
        if (_driver)
        {
            _driver->SwitchVehicleAction(DriverAction());
        }
    }
    if (_gunnerHiddenWanted > _gunnerHidden)
    {
        _gunnerHidden += turnInOutSpeed * deltaT;
        saturateMin(_gunnerHidden, _gunnerHiddenWanted);
        if (_gunner)
        {
            _gunner->SwitchVehicleAction(GunnerAction());
        }
    }
    else if (_gunnerHiddenWanted < _gunnerHidden)
    {
        _gunnerHidden -= turnInOutSpeed * deltaT;
        saturateMax(_gunnerHidden, _gunnerHiddenWanted);
        if (_gunner)
        {
            _gunner->SwitchVehicleAction(GunnerAction());
        }
    }

    // advance animation of proxy objects
    if (_driver)
    {
        _driver->BasicSimulation(deltaT, prec, DriverAnimSpeed());
    }
    if (_gunner)
    {
        _gunner->BasicSimulation(deltaT, prec, GunnerAnimSpeed());
    }
    if (_commander)
    {
        _commander->BasicSimulation(deltaT, prec, CommanderAnimSpeed());
    }
    for (int i = 0; i < _manCargo.Size(); i++)
    {
        ManCargoItem& item = _manCargo.Set(i);
        Person* man = item.man;
        if (man)
        {
            man->BasicSimulation(deltaT, prec, CargoAnimSpeed(i));
        }
    }

    _radio.Simulate(deltaT);
    base::Simulate(deltaT, prec);
    ReactToDammage();

    if (!CommanderUnit() || CommanderUnit()->IsInCargo())
    {
        _pilotLight = false;
    }
    else if (CommanderUnit()->HasAI())
    {
        _pilotLight = false;
        float thold = _randomizer * 0.6 + 0.2;
        if (GScene->MainLight()->NightEffect() > thold)
        {
            if (!IsCautiousOrDanger())
            {
                _pilotLight = true;
            }
        }
    }

    if (_markers.Size() == 0 && _markersBlink.Size() == 0)
    {
        if (_pilotLight)
        {
            LODShapeWithShadow* shape = GLOB_SCENE->Preloaded(Marker);
            for (int i = 0; i < GetType()->_lights.Size(); i++)
            {
                const LightInfo& info = GetType()->_lights[i];
                LightPointOnVehicle* light =
                    new LightPointOnVehicle(shape, info.color, info.ambient, this, info.position);
                light->SetBrightness(info.brightness);

                if (info.type == LightTypeMarker)
                {
                    _markers.Add(light);
                }
                else if (info.type == LightTypeMarkerBlink)
                {
                    _markersBlink.Add(light);
                }

                GLOB_SCENE->AddLight(light);
            }
            _markersOn = Glob.time - GRandGen.RandomValue();
        }
    }
    else
    {
        if (!_pilotLight)
        {
            _markers.Resize(0);
            _markersBlink.Resize(0);
        }
    }

    if (_pilotLight)
    {
        bool on = (toInt(2.0 * (Glob.time - _markersOn)) % 2) != 0;
        for (int i = 0; i < _markersBlink.Size(); i++)
        {
            _markersBlink[i]->Switch(on);
        }
    }

    if (Glob.time > _showDmgValid)
    {
        _showDmg = false;
    }
}

void Transport::ShowDammage(int part)
{
    if (IsDammageDestroyed())
    {
        return;
    }
    if (!EngineIsOn())
    {
        return;
    }
    _showDmg = true;
    _showDmgValid = Glob.time + 5.0f;
    base::ShowDammage(part);
}

bool Transport::IsAnimated(int level) const
{
    return true;
}

void Transport::Animate(int level)
{
    if (GScene->MainLight()->NightEffect() > 0.01 && EngineIsOn())
    {
        GetType()->_dashboard.Unhide(_shape, level);
    }
    else
    {
        GetType()->_dashboard.Hide(_shape, level);
    }

    bool on = (toInt(2.0 * (_showDmgValid - Glob.time)) % 2) != 0;
    if (_showDmg && on)
    {
        GetType()->_showDmg.Unhide(_shape, level);
    }
    else
    {
        GetType()->_showDmg.Hide(_shape, level);
    }

    base::Animate(level);
}

void Transport::AnimateManProxyMatrix(int level, const ManProxy& proxy, Matrix4& proxyTransform) const
{
    AnimateMatrix(proxyTransform, level, proxy.selection);
}

void Transport::Deanimate(int level)
{
    GetType()->_dashboard.Unhide(_shape, level);
    base::Deanimate(level);
}

LODShapeWithShadow* Transport::GetOpticsModel(Person* person)
{
    if (!person)
    {
        return nullptr;
    }
    if (person == _driver)
    {
        if (Type()->_hideProxyInCombat && _driverHidden < 0.5)
        {
            return nullptr;
        }
        return Type()->_driverOpticsModel;
    }
    else if (person == _gunner)
    {
        if (Type()->_hideProxyInCombat && _gunnerHidden < 0.5)
        {
            return nullptr;
        }
        return Type()->_gunnerOpticsModel;
    }
    else if (person == _commander)
    {
        if (Type()->_hideProxyInCombat && _commanderHidden < 0.5)
        {
            return nullptr;
        }
        return Type()->_commanderOpticsModel;
    }
    return nullptr;
}

bool Transport::GetForceOptics(Person* person) const
{
    return false;
}

PackedColor Transport::GetOpticsColor(Person* person)
{
    if (!person)
    {
        return PackedBlack;
    }

    if (person == _driver)
    {
        return Type()->_driverOpticsColor;
    }
    else if (person == _gunner)
    {
        return Type()->_gunnerOpticsColor;
    }
    else if (person == _commander)
    {
        return Type()->_commanderOpticsColor;
    }
    return PackedBlack;
}

Texture* Transport::GetFlagTexture()
{
    if (_commander && _commander->GetFlagTexture())
    {
        return _commander->GetFlagTexture();
    }
    if (_driver && _driver->GetFlagTexture())
    {
        return _driver->GetFlagTexture();
    }
    if (_gunner && _gunner->GetFlagTexture())
    {
        return _gunner->GetFlagTexture();
    }
    for (int i = 0; i < _manCargo.Size(); i++)
    {
        Person* cargo = _manCargo[i];
        if (cargo && cargo->GetFlagTexture())
        {
            return cargo->GetFlagTexture();
        }
    }
    return nullptr;
}

ManProxy::ManProxy()
{
    selection = -1;
}

int Transport::PassNum(int lod)
{
    if (GWorld->GetCameraType() == CamGunner && GWorld->CameraOn() == this)
    {
        AIUnit* unit = GWorld->FocusOn();
        Person* person = unit ? unit->GetPerson() : nullptr;
        if (person)
        {
            LODShapeWithShadow* oShape = GetOpticsModel(person);
            if (oShape)
            {
                return 3;
            }
        }
    }
    return base::PassNum(lod);
}

void Transport::Draw(int level, ClipFlags clipFlags, const FrameBase& pos)
{
    if (level == LOD_INVISIBLE)
    {
        return;
    }
#if _ENABLE_CHEATS
    if (CHECK_DIAG(DETransparent))
    {
        return;
    }
#endif

    bool zSpace = (_shape->IsSpecLevel(level, VIEW_CARGO) || _shape->IsSpecLevel(level, VIEW_GUNNER) ||
                   _shape->IsSpecLevel(level, VIEW_COMMANDER) || _shape->IsSpecLevel(level, VIEW_PILOT));
    float oldCNear = 0;
    float oldCFar = 0;
    if (zSpace)
    {
        Camera* cam = GScene->GetCamera();
        oldCNear = cam->ClipNear();
        oldCFar = cam->ClipFar();
        cam->SetClipRange(0.01, 50);
        GEngine->UpdateProjection();
        GEngine->Clear(true, false);
    }

    base::Draw(level, clipFlags, pos);

    if (GWorld->GetCameraType() == CamGunner && GWorld->CameraOn() == this)
    {
        AIUnit* unit = GWorld->FocusOn();
        Person* person = unit ? unit->GetPerson() : nullptr;
        if (person)
        {
            LODShapeWithShadow* oShape = GetOpticsModel(person);
            if (oShape)
            {
                // vehicle gunner optics are a 4:3 vignette — stretch when bars off.
                const bool preserve4x3 = AspectRatio::ArePillarboxBarsEnabled();
                Draw2D(oShape, 0, GetOpticsColor(person), /*preserveAspect4x3*/ preserve4x3);
                Object::DrawWidescreenPillarbox();
            }
        }
    }
    if (zSpace)
    {
        Camera* cam = GScene->GetCamera();
        cam->SetClipRange(oldCNear, oldCFar);
        GEngine->UpdateProjection();
    }
}

void Transport::DrawDiags()
{
#if _ENABLE_CHEATS
    LODShapeWithShadow* forceArrow = GScene->ForceArrow();

    if (CHECK_DIAG(DEPath))
    {
        LODShapeWithShadow* shape = GScene->Preloaded(SphereModel);
        PackedColor colorGetIn = PackedColor(Color(0, 1, 0, 1));
        PackedColor colorGetOut = PackedColor(Color(1, 0, 0, 1));
        if (CommanderBrain())
        {
            Vector3Val pos = GetCommanderGetOutPos(Commander());
            Ref<Object> obj = new ObjectColored(shape, -1);
            obj->SetPosition(pos);
            obj->SetConstantColor(colorGetOut);
            GScene->ObjectForDrawing(obj);
        }
        else if (GetCommanderAssigned())
        {
            Vector3Val pos = GetCommanderGetInPos(GetCommanderAssigned()->GetPerson(),
                                                  GetCommanderAssigned()->GetPerson()->WorldPosition());
            Ref<Object> obj = new ObjectColored(shape, -1);
            obj->SetPosition(pos);
            obj->SetConstantColor(colorGetIn);
            GScene->ObjectForDrawing(obj);
        }
        if (DriverBrain())
        {
            Vector3Val pos = GetDriverGetOutPos(Driver());
            Ref<Object> obj = new ObjectColored(shape, -1);
            obj->SetPosition(pos);
            obj->SetConstantColor(colorGetOut);
            GScene->ObjectForDrawing(obj);
        }
        else if (GetDriverAssigned())
        {
            Vector3Val pos =
                GetDriverGetInPos(GetDriverAssigned()->GetPerson(), GetDriverAssigned()->GetPerson()->WorldPosition());
            Ref<Object> obj = new ObjectColored(shape, -1);
            obj->SetPosition(pos);
            obj->SetConstantColor(colorGetIn);
            GScene->ObjectForDrawing(obj);
        }
        if (GunnerBrain())
        {
            Vector3Val pos = GetGunnerGetOutPos(Gunner());
            Ref<Object> obj = new ObjectColored(shape, -1);
            obj->SetPosition(pos);
            obj->SetConstantColor(colorGetOut);
            GScene->ObjectForDrawing(obj);
        }
        else if (GetGunnerAssigned())
        {
            Vector3Val pos =
                GetGunnerGetInPos(GetGunnerAssigned()->GetPerson(), GetGunnerAssigned()->GetPerson()->WorldPosition());
            Ref<Object> obj = new ObjectColored(shape, -1);
            obj->SetPosition(pos);
            obj->SetConstantColor(colorGetIn);
            GScene->ObjectForDrawing(obj);
        }
        for (int i = 0; i < _cargoAssigned.Size(); i++)
        {
            AIUnit* unit = _cargoAssigned[i];
            if (!unit)
                continue;
            if (unit->GetVehicleIn() == this)
            {
                Vector3Val pos = GetCargoGetOutPos(unit->GetPerson());
                Ref<Object> obj = new ObjectColored(shape, -1);
                obj->SetPosition(pos);
                obj->SetConstantColor(colorGetOut);
                GScene->ObjectForDrawing(obj);
            }
            else if (!unit->GetVehicleIn())
            {
                Vector3Val pos = GetCargoGetInPos(unit->GetPerson(), unit->GetPerson()->WorldPosition());
                Ref<Object> obj = new ObjectColored(shape, -1);
                obj->SetPosition(pos);
                obj->SetConstantColor(colorGetIn);
                GScene->ObjectForDrawing(obj);
            }
        }

        {
            {
                Matrix3 mat(MRotationY, -_azimutWanted);
                Ref<Object> arrow = new ObjectColored(forceArrow, -1);

                float size = 0.15;
                arrow->SetPosition(Position() + VUp * 1.0);
                arrow->SetOrient(mat.Direction(), VUp);
                arrow->SetPosition(arrow->PositionModelToWorld(forceArrow->BoundingCenter() * size));
                arrow->SetScale(size);
                arrow->SetConstantColor(PackedColor(Color(1, 1, 0, 0.5)));

                GScene->ObjectForDrawing(arrow);
            }
            {
                float dirWanted = atan2(_dirWanted.X(), _dirWanted.Z());
                Matrix3 mat(MRotationY, -dirWanted);
                Ref<Object> arrow = new ObjectColored(forceArrow, -1);

                float size = 0.1;
                arrow->SetPosition(Position() + VUp * 1.5);
                arrow->SetOrient(mat.Direction(), VUp);
                arrow->SetPosition(arrow->PositionModelToWorld(forceArrow->BoundingCenter() * size));
                arrow->SetScale(size);
                arrow->SetConstantColor(PackedColor(Color(1, 1, 0, 0.5)));

                GScene->ObjectForDrawing(arrow);
            }
        }
    }
    if (CHECK_DIAG(DEPath) && QIsManual())
    {
        {
            Ref<Object> arrow = new ObjectColored(forceArrow, -1);

            float size = 1.0f;
            arrow->SetPosition(Position() + Direction() * 10 + VUp);
            arrow->SetOrient(_mouseDirWanted, VUp);
            arrow->SetPosition(arrow->PositionModelToWorld(forceArrow->BoundingCenter() * size));
            arrow->SetScale(size);
            arrow->SetConstantColor(PackedColor(Color(0.7, 1, 0, 0.5)));

            GScene->ObjectForDrawing(arrow);
        }
    }
#endif
    base::DrawDiags();
}

void Transport::DrawCameraCockpit()
{
    AIUnit* unit = GWorld->FocusOn();
    if (!unit)
    {
        return;
    }
    Person* man = unit->GetPerson();
    if (!man)
    {
        return;
    }

    float alpha = 0;
    if (man == _commander)
    {
        alpha = floatMin(_commanderHidden, 1.0 - _commanderHidden);
    }
    else if (man == _driver)
    {
        alpha = floatMin(_driverHidden, 1.0 - _driverHidden);
    }
    else if (man == _gunner)
    {
        alpha = floatMin(_gunnerHidden, 1.0 - _gunnerHidden);
    }

    if (alpha <= 0)
    {
        return;
    }

    alpha *= 2;
    saturateMin(alpha, 1);
    PackedColor color = PackedColor(Color(0, 0, 0, alpha));
    const float w = GEngine->Width();
    const float h = GEngine->Height();
    MipInfo mip = GEngine->TextBank()->UseMipmap(nullptr, 0, 0);
    GEngine->Draw2D(mip, color, Rect2DAbs(0, 0, w, h));
}

void Transport::DrawCrewMember(int level, ClipFlags clipFlags, const Matrix4& transform, const Matrix4& invTransform,
                               float dist2, float z2, const LightList& lights, const ManProxy& proxy, Person* man,
                               bool hideWeapons)
{
    LODShapeWithShadow* manShape = man->GetShape();

    Matrix4 proxyTransform = proxy.transform;

    AnimateManProxyMatrix(level, proxy, proxyTransform);

    proxyTransform.SetPosition(proxyTransform.FastTransform(manShape->BoundingCenter()));
    Matrix4Val pTransform = transform * proxyTransform;

    int pLevel;
    bool insidePlayer = false;
    CameraType camType = GWorld->GetCameraType();
    if (camType != CamExternal && camType != CamGroup)
    {
        if (!GWorld->GetCameraEffect())
        {
            AIUnit* unit = GWorld->FocusOn();
            Person* player = unit ? unit->GetPerson() : nullptr;
            insidePlayer = man == player;
        }
    }

    if (insidePlayer)
    {
        pLevel = 0;
    }
    else
    {
        pLevel = GScene->LevelFromDistance2(manShape, dist2, pTransform.Scale(), pTransform.Direction(),
                                            GScene->GetCamera()->Direction());
    }
    if (pLevel != LOD_INVISIBLE)
    {
        Matrix4Val invPTransform = pTransform.InverseScaled();

        FrameWithInverse pFrame(pTransform, invPTransform);
        man->ShowWeapons(!hideWeapons, false);
        if (insidePlayer)
        {
            man->ShowHead(pLevel, false);
        }
        man->ShowFlag(false);
        man->Draw(pLevel, clipFlags, pFrame);
        man->ShowFlag(true);
        if (insidePlayer)
        {
            man->ShowHead(pLevel, true);
        }
        man->ShowWeapons(true, true);
    }
}

int Transport::GetCrewMemberComplexity(int level, const FrameBase& pos, float dist2, const ManProxy& proxy,
                                       Person* man) const
{
    LODShapeWithShadow* manShape = man->GetShape();

    Matrix4 proxyTransform = proxy.transform;

    AnimateManProxyMatrix(level, proxy, proxyTransform);

    proxyTransform.SetPosition(proxyTransform.FastTransform(manShape->BoundingCenter()));
    Matrix4Val pTransform = pos.Transform() * proxyTransform;

    int pLevel;
    bool insidePlayer = false;
    CameraType camType = GWorld->GetCameraType();
    if (camType != CamExternal && camType != CamGroup)
    {
        AIUnit* unit = GWorld->FocusOn();
        Person* player = unit ? unit->GetPerson() : nullptr;
        insidePlayer = man == player;
    }

    if (insidePlayer)
    {
        pLevel = 0;
    }
    else
    {
        pLevel = GScene->LevelFromDistance2(manShape, dist2, pTransform.Scale(), pTransform.Direction(),
                                            GScene->GetCamera()->Direction());
    }
    if (pLevel != LOD_INVISIBLE)
    {
        return man->GetComplexity(pLevel, pos);
    }
    return 0;
}

bool Transport::CastProxyShadow(int level, int i) const
{
    const TransportType* type = Type();
    const LevelProxies& proxies = type->_proxies[level];
    // find corresponding proxy
    int cargoCount = proxies._cargoProxy.Size();
    const ManProxy* proxy = nullptr;
    bool castShadow = false;
    Person* man = nullptr;
    if (i < cargoCount)
    {
        proxy = &proxies._cargoProxy[i];
        if (i < _manCargo.Size())
        {
            man = _manCargo[i];
            castShadow = type->_castCargoShadow;
        }
    }
    else
    {
        i -= cargoCount;
        if (proxies._driverProxy.Present())
        {
            if (i == 0 && !IsDriverHidden())
            {
                proxy = &proxies._driverProxy, man = _driver;
                castShadow = type->_castDriverShadow;
            }
            i--;
        }
        if (proxies._gunnerProxy.Present())
        {
            if (i == 0 && !IsGunnerHidden())
            {
                proxy = &proxies._gunnerProxy, man = _gunner;
                castShadow = type->_castGunnerShadow;
            }
            i--;
        }
        if (proxies._commanderProxy.Present())
        {
            if (i == 0 && !IsCommanderHidden())
            {
                proxy = &proxies._commanderProxy, man = _commander;
                castShadow = type->_castCommanderShadow;
            }
            i--;
        }
    }
    return proxy && man && castShadow;
}

int Transport::GetProxyCount(int level) const
{
    const TransportType* type = Type();
    const LevelProxies& proxies = type->_proxies[level];
    int count = proxies._cargoProxy.Size();
    if (proxies._driverProxy.Present())
    {
        count++;
    }
    if (proxies._gunnerProxy.Present())
    {
        count++;
    }
    if (proxies._commanderProxy.Present())
    {
        count++;
    }
    return count;
}

Object* Transport::GetProxy(LODShapeWithShadow*& shape, int level, Matrix4& transform, Matrix4& invTransform,
                            const FrameBase& parentPos, int i) const
{
    const TransportType* type = Type();
    const LevelProxies& proxies = type->_proxies[level];
    int cargoCount = proxies._cargoProxy.Size();
    const ManProxy* proxy = nullptr;
    Person* man = nullptr;
    if (i < cargoCount)
    {
        proxy = &proxies._cargoProxy[i];
        if (i < _manCargo.Size())
        {
            man = _manCargo[i];
        }
    }
    else
    {
        i -= cargoCount;
        if (proxies._driverProxy.Present())
        {
            if (i == 0 && !IsDriverHidden())
            {
                proxy = &proxies._driverProxy, man = _driver;
            }
            i--;
        }
        if (proxies._gunnerProxy.Present())
        {
            if (i == 0 && !IsGunnerHidden())
            {
                proxy = &proxies._gunnerProxy, man = _gunner;
            }
            i--;
        }
        if (proxies._commanderProxy.Present())
        {
            if (i == 0 && !IsCommanderHidden())
            {
                proxy = &proxies._commanderProxy, man = _commander;
            }
            i--;
        }
    }
    if (!proxy || !man)
    {
        return nullptr;
    }

    LODShapeWithShadow* manShape = man->GetShape();

    Matrix4 proxyTransform = proxy->transform;

    AnimateManProxyMatrix(level, *proxy, proxyTransform);

    proxyTransform.SetPosition(proxyTransform.FastTransform(manShape->BoundingCenter()));
    transform = parentPos.Transform() * proxyTransform;
    invTransform = transform.InverseScaled();

    shape = manShape;

    return man;
}

Matrix4 Transport::ProxyWorldTransform(const Object* obj) const
{
    int level = 0;
    int n = GetProxyCount(level);
    for (int i = 0; i < n; i++)
    {
        Matrix4 trans = Transform();
        Matrix4 invTrans = GetInvTransform();
        LODShapeWithShadow* shape = nullptr;
        Object* proxy = GetProxy(shape, level, trans, invTrans, *this, i);
        if (proxy == obj)
        {
            return trans;
        }
    }
    return Transform();
}
Matrix4 Transport::ProxyInvWorldTransform(const Object* obj) const
{
    int level = 0;
    int n = GetProxyCount(level);
    for (int i = 0; i < n; i++)
    {
        Matrix4 trans = Transform();
        Matrix4 invTrans = GetInvTransform();
        LODShapeWithShadow* shape = nullptr;
        Object* proxy = GetProxy(shape, level, trans, invTrans, *this, i);
        if (proxy == obj)
        {
            return invTrans;
        }
    }
    return GetInvTransform();
}

void Transport::DrawProxies(int level, ClipFlags clipFlags, const Matrix4& transform, const Matrix4& invTransform,
                            float dist2, float z2, const LightList& lights)
{
    bool external = !_shape->IsSpecLevel(level, VIEW_CARGO) && !_shape->IsSpecLevel(level, VIEW_PILOT) &&
                    !_shape->IsSpecLevel(level, VIEW_GUNNER);

    const TransportType* type = Type();
    const LevelProxies& proxies = type->_proxies[level];
    if (_driver && proxies._driverProxy.Present() && !(external && IsDriverHidden()))
    {
        DrawCrewMember(level, clipFlags, transform, invTransform, dist2, z2, lights, proxies._driverProxy, _driver,
                       Type()->_hideWeaponsDriver);
    }
    if (_gunner && proxies._gunnerProxy.Present() &&
        (!external || !IsGunnerHidden() || type->GetViewGunnerInExternal()))
    {
        DrawCrewMember(level, clipFlags, transform, invTransform, dist2, z2, lights, proxies._gunnerProxy, _gunner,
                       Type()->_hideWeaponsGunner);
    }
    if (_commander && proxies._commanderProxy.Present() && !(external && IsCommanderHidden()))
    {
        DrawCrewMember(level, clipFlags, transform, invTransform, dist2, z2, lights, proxies._commanderProxy,
                       _commander, Type()->_hideWeaponsCommander);
    }
    for (int i = 0; i < _manCargo.Size(); i++)
    {
        Person* cargo = _manCargo[i];
        if (!cargo)
        {
            continue;
        }
        if (i >= proxies._cargoProxy.Size())
        {
            break;
        }
        if (!proxies._cargoProxy[i].Present())
        {
            break;
        }
        DrawCrewMember(level, clipFlags, transform, invTransform, dist2, z2, lights, proxies._cargoProxy[i], cargo,
                       Type()->_hideWeaponsCargo);
    }

    int nMissiles = CountMissiles();

    Shape* sShape = _shape->LevelOpaque(level);
    for (int i = 0; i < sShape->NProxies(); i++)
    {
        const ProxyObject& proxy = sShape->Proxy(i);
        Object* obj = proxy.obj;
        const EntityType* type = obj->GetVehicleType();
        if (!type)
        {
            continue;
        }
        if (!strcmp(type->_simName, "maverickweapon"))
        {
            if (proxy.id > nMissiles)
            {
                continue;
            }
            Matrix4Val pTransform = transform * obj->Transform();
            Matrix4Val invPTransform = proxy.invTransform * invTransform;

            LODShapeWithShadow* pshape = GetMissileShape();
            if (!pshape)
            {
                continue;
            }
            int level = GScene->LevelFromDistance2(pshape, dist2, pTransform.Scale(), pTransform.Direction(),
                                                   GScene->GetCamera()->Direction());
            if (level == LOD_INVISIBLE)
            {
                continue;
            }

            FrameWithInverse pFrame(pTransform, invPTransform);

            LODShapeWithShadow* oldShape = obj->GetShape();
            obj->SetShape(pshape);
            obj->Draw(level, ClipAll, pFrame);
            obj->SetShape(oldShape);
        }
    }

    base::DrawProxies(level, clipFlags, transform, invTransform, dist2, z2, lights);
}

int Transport::GetProxyComplexity(int level, const FrameBase& pos, float dist2) const
{
    int nFaces = 0;

    bool external = !_shape->IsSpecLevel(level, VIEW_CARGO) && !_shape->IsSpecLevel(level, VIEW_PILOT);

    const TransportType* type = Type();
    const LevelProxies& proxies = type->_proxies[level];
    if (_driver && proxies._driverProxy.Present() && !(external && IsDriverHidden()))
    {
        nFaces += GetCrewMemberComplexity(level, pos, dist2, proxies._driverProxy, _driver);
    }
    if (_gunner && proxies._gunnerProxy.Present() && !(external && IsGunnerHidden()))
    {
        nFaces += GetCrewMemberComplexity(level, pos, dist2, proxies._gunnerProxy, _gunner);
    }
    if (_commander && proxies._commanderProxy.Present() && !(external && IsCommanderHidden()))
    {
        nFaces += GetCrewMemberComplexity(level, pos, dist2, proxies._commanderProxy, _commander);
    }
    for (int i = 0; i < _manCargo.Size(); i++)
    {
        Person* cargo = _manCargo[i];
        if (!cargo)
        {
            continue;
        }
        if (i >= proxies._cargoProxy.Size())
        {
            break;
        }
        if (!proxies._cargoProxy[i].Present())
        {
            break;
        }
        nFaces += GetCrewMemberComplexity(level, pos, dist2, proxies._cargoProxy[i], cargo);
    }

    int nMissiles = CountMissiles();

    Shape* sShape = _shape->LevelOpaque(level);
    for (int i = 0; i < sShape->NProxies(); i++)
    {
        const ProxyObject& proxy = sShape->Proxy(i);
        Object* obj = proxy.obj;
        if (!obj)
        {
            continue;
        }
        const EntityType* type = obj->GetVehicleType();
        if (!type)
        {
            continue;
        }
        RString simulation = type->_simName;
        if (strcmp(simulation, "maverickweapon") == 0)
        {
            if (proxy.id > nMissiles)
            {
                continue;
            }
            Matrix4Val pTransform = pos.Transform() * proxy.obj->Transform();

            LODShapeWithShadow* pshape = obj->GetShapeOnPos(pTransform.Position());
            if (!pshape)
            {
                continue;
            }
            int level = GScene->LevelFromDistance2(pshape, dist2, pTransform.Scale(), pTransform.Direction(),
                                                   GScene->GetCamera()->Direction());
            if (level == LOD_INVISIBLE)
            {
                continue;
            }

            Matrix4Val invPTransform = proxy.invTransform * pos.GetInvTransform();
            FrameWithInverse pFrame(pTransform, invPTransform);

            nFaces += obj->GetComplexity(level, pFrame);
        }
    }

    return nFaces;
}

Vector3 Transport::FindMissilePos(int index, bool& found) const
{
    found = false;
    Shape* sShape = _shape->LevelOpaque(0);
    for (int i = 0; i < sShape->NProxies(); i++)
    {
        const ProxyObject& proxy = sShape->Proxy(i);
        Object* obj = proxy.obj;
        const EntityType* type = obj->GetVehicleType();
        if (!type)
        {
            continue;
        }
        if (strcmp(type->_simName, "maverickweapon"))
        {
            continue;
        }
        if (proxy.id != index)
        {
            continue;
        }
        found = true;
        return proxy.obj->Position();
    }

    return VZero;
}

bool Transport::GetOpticsCamera(Matrix4& transf, CameraType camType) const
{
    AIUnit* unit = GWorld->FocusOn();
    if (!unit)
    {
        return false;
    }

    Person* man = unit->GetPerson();
    if (!man)
    {
        return false;
    }

    const TransportType* type = Type();

    int index = -1;
    if (man == _driver)
    {
        index = type->_driverOpticsPos;
    }
    else if (man == _gunner)
    {
        index = type->_gunnerOpticsPos;
    }
    else if (man == _commander)
    {
        index = type->_commanderOpticsPos;
    }
    if (index < 0)
    {
        return false;
    }

    Shape* memory = _shape->MemoryLevel();
    transf.SetPosition(memory->Pos(memory->NamedSel(index)[0]));
    transf.SetOrientation(M3Identity);
    AnimateMatrix(transf, _shape->FindMemoryLevel(), index);
    return true;
}

bool Transport::GetProxyCamera(Matrix4& transf, CameraType camType) const
{
    AIUnit* unit = GWorld->FocusOn();
    if (!unit)
    {
        return false;
    }

    Person* man = unit->GetPerson();
    if (!man)
    {
        return false;
    }

    const TransportType* type = Type();
    int level = InsideLOD(camType);
    if (level == LOD_INVISIBLE)
    {
        return false;
    }
    const LevelProxies& proxies = type->_proxies[level];

    const ManProxy* proxy = nullptr;
    if (man == _driver)
    {
        proxy = &proxies._driverProxy;
    }
    else if (man == _gunner)
    {
        proxy = &proxies._gunnerProxy;
    }
    else if (man == _commander)
    {
        proxy = &proxies._commanderProxy;
    }
    else
    {
        for (int i = 0; i < _manCargo.Size(); i++)
        {
            if (i >= proxies._cargoProxy.Size())
            {
                break;
            }
            if (!proxies._cargoProxy[i].Present())
            {
                continue;
            }
            if (man == _manCargo[i])
            {
                proxy = &proxies._cargoProxy[i];
                break;
            }
        }
    }
    if (!proxy || !proxy->Present())
    {
        return false;
    }

    LODShapeWithShadow* manShape = man->GetShape();
    transf = proxy->transform;
    Vector3 pos = man->GetPilotPosition(camType);
    transf.SetPosition(transf.FastTransform(manShape->BoundingCenter()));
    transf.SetPosition(transf.FastTransform(pos));
    AnimateMatrix(transf, level, proxy->selection);
    return true;
}

} // namespace Poseidon
