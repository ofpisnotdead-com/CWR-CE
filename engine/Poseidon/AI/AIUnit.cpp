#include <Poseidon/Core/Application.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/UI/Locale/Stringtable/CodepageTranscode.hpp>
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
#include <Poseidon/Foundation/Time/Time.hpp>
#include <cmath>
#include <utility>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Memory/MemAlloc.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

#include <Poseidon/AI/Path/AIDefs.hpp>
#pragma warning(disable : 4355)

namespace Poseidon
{
using namespace Foundation;
using Foundation::EnumName;

// speed in nodes
#define SPEED_COEF 6.0F

// A* algoritm for searching the best path
int directions8[8][2] = {{0, -1}, {-1, -1}, {-1, 0}, {-1, 1}, {0, 1}, {1, 1}, {1, 0}, {1, -1}};
int directions20[20][3] = {{0, -1, 0},  {-1, -2, 1}, {-1, -1, 2}, {-2, -1, 3}, {-1, 0, 4}, {-2, 1, 5}, {-1, 1, 6},
                           {-1, 2, 7},  {0, 1, 8},   {1, 2, 9},   {1, 1, 10},  {2, 1, 11}, {1, 0, 12}, {2, -1, 13},
                           {1, -1, 14}, {1, -2, 15}, {0, -2, 0},  {-2, 0, 4},  {0, 2, 8},  {2, 0, 12}};

// exposures
#define VALID_EXPOSURE_CHANGE 10.0F
#define COEF_EXPOSURE 4e-6F // coeficient for including exposure into cost

#define DIAG_PLANNING 0

AILocker::AILocker() = default; // avoid inlined construct/destruct
AILocker::~AILocker() = default;

void AILocker::LockItem(int x, int z, bool soldier)
{
    if (x < 0 || x >= OperItemRange * LandRange || z < 0 || z >= OperItemRange * LandRange)
    {
        return;
    }

    int x0 = x / OperItemRange;
    int z0 = z / OperItemRange;
    int x1 = x - x0 * OperItemRange;
    int z1 = z - z0 * OperItemRange;
    LockField* fld = nullptr;
    for (int i = 0; i < _fields.Size(); i++)
    {
        if (_fields[i]->_x == x0 && _fields[i]->_z == z0)
        {
            fld = _fields[i];
            break;
        }
    }
    if (fld == nullptr)
    // note: field may be locked twice with the same locker
    {
        fld = GLOB_LAND->LockingCache()->GetLockField(x0, z0);
        _fields.Add(fld);
    }
    fld->Lock(x1, z1, soldier, true);
}

void AILocker::UnlockItem(int x, int z, bool soldier)
{
    if (x < 0 || x >= OperItemRange * LandRange || z < 0 || z >= OperItemRange * LandRange)
    {
        return;
    }

    int x0 = x / OperItemRange;
    int z0 = z / OperItemRange;
    int x1 = x - x0 * OperItemRange;
    int z1 = z - z0 * OperItemRange;
    LockField* fld = nullptr;
    for (int i = 0; i < _fields.Size(); i++)
    {
        if (_fields[i]->_x == x0 && _fields[i]->_z == z0)
        {
            fld = _fields[i];
            break;
        }
    }
    if (fld == nullptr)
    {
        Fail("Cannot unlock item that was not locked.");
        return;
    }
    fld->Lock(x1, z1, soldier, false);
}

void AILocker::LockPosition(Vector3Val pos, float radius, bool soldier, float size)
{
    int LockRange = toIntCeil(radius * InvOperItemGrid);
    if (LockRange < 0)
    {
        LockRange = 0;
    }

    int xx, x = toIntFloor(pos.X() * InvOperItemGrid);
    int zz, z = toIntFloor(pos.Z() * InvOperItemGrid);
    for (xx = -LockRange; xx <= LockRange; xx++)
    {
        for (zz = -LockRange; zz <= LockRange; zz++)
        {
            float xr = (x + xx + 0.5f) * OperItemGrid - pos.X();
            float zr = (z + zz + 0.5f) * OperItemGrid - pos.Z();
            if (xr * xr + zr * zr < radius * radius || xx == 0 && zz == 0)
            {
                LockItem(x + xx, z + zz, true);
            }
        }
    }

    AI_ERROR(GRoadNet);
    x = toIntFloor(pos.X() * InvLandGrid);
    z = toIntFloor(pos.Z() * InvLandGrid);
    // lock road item if vehicle is inside "boundingSphere" + size
    for (zz = z - 1; zz <= z + 1; zz++)
    {
        for (xx = x - 1; xx <= x + 1; xx++)
        {
            if (!InRange(xx, zz))
            {
                continue;
            }
            RoadList& roadList = GRoadNet->GetRoadList(xx, zz);
            int i;
            for (i = 0; i < roadList.Size(); i++)
            {
                RoadLink* item = roadList[i];
                if (item->IsInside(pos, size))
                {
                    item->Lock();
                    _roads.Add(item);
                }
            }
        }
    }

    if (soldier)
    {
        // this is not clear - pos is AimingPosition, we need position on surface
        Vector3 realPos = pos - Vector3(0, 1, 0);

        // lock position in house
        int xMin, xMax, zMin, zMax;
        ObjRadiusRectangle(xMin, xMax, zMin, zMax, pos, pos, 50);
        for (int x = xMin; x <= xMax; x++)
        {
            for (int z = zMin; z <= zMax; z++)
            {
                const ObjectList& list = GLandscape->GetObjects(z, x);
                int n = list.Size();
                for (int i = 0; i < n; i++)
                {
                    Object* obj = list[i];

                    IPaths* building = const_cast<IPaths*>(obj->GetIPaths());
                    if (!building)
                    {
                        continue;
                    }
                    for (int j = 0; j < building->NPos(); j++)
                    {
                        Vector3 dest = building->GetPosition(building->GetPos(j));
                        float dist2 = dest.Distance2(realPos);
                        if (dist2 <= Square(size))
                        {
                            building->Lock(j);
                            int index = _buildings.Add();
                            _buildings[index].house = const_cast<Object*>(building->GetObject());
                            _buildings[index].index = j;
                        }
                    }
                }
            }
        }
    }
}

void AILocker::UnlockPosition(Vector3Val pos, float radius, bool soldier)
{
    int LockRange = toIntCeil(radius * InvOperItemGrid);
    if (LockRange < 0)
    {
        LockRange = 0;
    }

    int x = toIntFloor(pos.X() * InvOperItemGrid);
    int z = toIntFloor(pos.Z() * InvOperItemGrid);
    for (int xx = -LockRange; xx <= LockRange; xx++)
    {
        for (int zz = -LockRange; zz <= LockRange; zz++)
        {
            float xr = (x + xx + 0.5f) * OperItemGrid - pos.X();
            float zr = (z + zz + 0.5f) * OperItemGrid - pos.Z();
            if (xr * xr + zr * zr < radius * radius || xx == 0 && zz == 0)
            {
                UnlockItem(x + xx, z + zz, true);
            }
        }
    }
    int i, n = _fields.Size();
    for (i = 0; i < n; i++)
    {
        GLOB_LAND->LockingCache()->ReleaseLockField(_fields[i]->_x, _fields[i]->_z);
    }

    n = _roads.Size();
    for (i = 0; i < n; i++)
    {
        if (_roads[i])
        {
            _roads[i]->Unlock();
        }
    }

    if (soldier)
    {
        // unlock position in house
        for (int i = 0; i < _buildings.Size(); i++)
        {
            IPaths* building = _buildings[i].house ? const_cast<IPaths*>(_buildings[i].house->GetIPaths()) : nullptr;
            if (_buildings[i].house)
            {
                building->Unlock(_buildings[i].index);
            }
        }
    }

    // avoid dealocation
    _fields.Resize(0);
    _roads.Resize(0);
    _buildings.Resize(0);
}

void AILocker::LockPositionMan(Vector3Val pos, float radius)
{
    int LockRange = toIntCeil(radius * InvOperItemGrid);
    if (LockRange < 0)
    {
        LockRange = 0;
    }

    int xx, x = toIntFloor(pos.X() * InvOperItemGrid);
    int zz, z = toIntFloor(pos.Z() * InvOperItemGrid);
    for (xx = -LockRange; xx <= LockRange; xx++)
    {
        for (zz = -LockRange; zz <= LockRange; zz++)
        {
            float xr = (x + xx + 0.5f) * OperItemGrid - pos.X();
            float zr = (z + zz + 0.5f) * OperItemGrid - pos.Z();
            if (xr * xr + zr * zr < radius * radius || xx == 0 && zz == 0)
            {
                LockItem(x + xx, z + zz, false);
            }
        }
    }
}

void AILocker::UnlockPositionMan(Vector3Val pos, float radius)
{
    int LockRange = toIntCeil(radius * InvOperItemGrid);
    if (LockRange < 0)
    {
        LockRange = 0;
    }

    int xx, x = toIntFloor(pos.X() * InvOperItemGrid);
    int zz, z = toIntFloor(pos.Z() * InvOperItemGrid);
    for (xx = -LockRange; xx <= LockRange; xx++)
    {
        for (zz = -LockRange; zz <= LockRange; zz++)
        {
            float xr = (x + xx + 0.5f) * OperItemGrid - pos.X();
            float zr = (z + zz + 0.5f) * OperItemGrid - pos.Z();
            if (xr * xr + zr * zr < radius * radius || xx == 0 && zz == 0)
            {
                UnlockItem(x + xx, z + zz, false);
            }
        }
    }
}

const Vector3 VUndefined(0, 1e9, 0);

static float AIUnitGetFieldCost(int x, int z, void* param)
{
    if (!param)
    {
        return SET_UNACCESSIBLE;
    }
    AIUnit* unit = (AIUnit*)param;
    switch (unit->GetPlanningMode())
    {
        case AIUnit::DoNotPlan:
        case AIUnit::LeaderDirect:
        default:
        {
            Fail("planning mode");
            return SET_UNACCESSIBLE;
        }
        case AIUnit::LeaderPlanned:
        {
            AISubgroup* subgrp = unit->GetSubgroup();
            if (!subgrp)
            {
                return SET_UNACCESSIBLE;
            }
            return subgrp->GetFieldCost(x, z);
        }
        case AIUnit::FormationPlanned:
        case AIUnit::VehiclePlanned:
        {
            if (!InRange(x, z))
            {
                return SET_UNACCESSIBLE;
            }

            // base cost
            EntityAI* veh = unit->GetVehicle();
            GeographyInfo geogr = GLandscape->GetGeography(x, z);
            float cost = veh->GetCost(geogr) * veh->GetFieldCost(geogr);
            cost *= LandGrid; // cost = time for distance == LandGrid

            // exposure
            float exposure = veh->CalculateExposure(x, z); // dammage (in $) per time cost
            return cost * (1.0 + COEF_EXPOSURE * exposure);
        }
    }
}

AIUnit::AIUnit(Person* vehicle) : _planner(CreateAIPathPlanner(AIUnitGetFieldCost, this))
{
    _person = vehicle;
    _inVehicle = nullptr;
    _subgroup = nullptr;

    _id = 0;
    SetSpeaker((Pars >> "CfgVoice" >> "voices")[0], 1.0);

    _captive = false;

    _playable = false;
    _lifeState = LSAlive;
    _disabledAI = 0;

    _path.SetOperIndex(1);
    _path.SetMaxIndex(1);
    _path.SetOnRoad(false);

    _expensiveThinkFrac = toIntFloor(GRandGen.RandomValue() * 1000);
    // perform first expensive think as soon as possible
    _expensiveThinkTime = Glob.time;

    _delay = Time(0);
    _iter = 0;
    _isAway = false;

    _state = Wait;
    _mode = Normal;

    _lastFuelState = RSNormal;
    _lastHealthState = RSNormal;
    _lastArmorState = RSNormal;
    _lastAmmoState = RSNormal;

    _formPos = AI::PosInFormation;
    _formPosCoef = 0;

    _semaphore = SemaphoreYellow;
    _combatModeMajor = CMAware;
    _dangerUntil = Glob.time - 60.0f;

    _expPosition = VZero;

    _formationPos = VZero;
    _formationAngle = 0;

    _watchDir = VZero;
    _watchDirHead = VZero;
    _watchPos = VZero;
    _watchMode = WMNo;

    _getInAllowed = true;
    _getInOrdered = false;

    _nearestEnemyDist2 = 100;

    _housePos = -1;

    _wantedPosition = VUndefined;
    _plannedPosition = VUndefined;
    _planningMode = DoNotPlan;

    _completedReceived = false;

    _ability = _invAbility = 1.0;

    ClearStrategicPlan();
}

void AIUnit::ExpensiveThinkDone()
{
    // calculate time of next expensive think
    // find start of this second
    // advance to start of next second, advance to our frac
    _expensiveThinkTime = Glob.time.Floor().AddMs(1000 + _expensiveThinkFrac);
}

AIUnit::~AIUnit()
{
    if (IsLocal())
    {
        NetworkId ni = GetNetworkId();
        GetNetworkManager().DeleteObject(ni);
    }
}

void AIUnit::Load(const ParamEntry& cls)
{
    Person* soldier = GetPerson();
    if (soldier)
    {
        AIUnitInfo& info = soldier->GetInfo();
        info._identityContext = cls.GetContext();
        info._name = DecodeLegacyTextToRString(cls >> "name", GLanguage);
        info._face = cls >> "face";
        info._glasses = cls >> "glasses";
        soldier->SetFace(info._face);
        soldier->SetGlasses(info._glasses);
        info._speaker = cls >> "speaker";
        info._pitch = cls >> "pitch";
        SetSpeaker(info._speaker, info._pitch);
    }
}

template <>
const EnumName* Foundation::GetEnumNames(AIUnit::ResourceState dummy)
{
    static const EnumName ResourceStateNames[] = {EnumName(AIUnit::RSNormal, "NORMAL"), EnumName(AIUnit::RSLow, "LOW"),
                                                  EnumName(AIUnit::RSCritical, "CRITICAL"), EnumName()};
    return ResourceStateNames;
}

template <>
const EnumName* Foundation::GetEnumNames(AIUnit::Mode dummy)
{
    static const EnumName UnitModeNames[] = {
        EnumName(AIUnit::DirectNormal, "DIRECT NORMAL"), EnumName(AIUnit::DirectExact, "DIRECT EXACT"),
        EnumName(AIUnit::Normal, "NORMAL"), EnumName(AIUnit::Exact, "EXACT"), EnumName()};
    return UnitModeNames;
}

template <>
const EnumName* Foundation::GetEnumNames(AIUnit::PlanningMode dummy)
{
    static const EnumName UnitPlanningModeNames[] = {EnumName(AIUnit::DoNotPlan, "DoNotPlan"),
                                                     EnumName(AIUnit::LeaderPlanned, "LEADER PLANNED"),
                                                     EnumName(AIUnit::LeaderDirect, "LEADER DIRECT"),
                                                     EnumName(AIUnit::FormationPlanned, "FORMATION PLANNED"),
                                                     EnumName(AIUnit::VehiclePlanned, "VEHICLE PLANNED"),
                                                     EnumName()};
    return UnitPlanningModeNames;
}

template <>
const EnumName* Foundation::GetEnumNames(AIUnit::State dummy)
{
    static const EnumName UnitStateNames[] = {EnumName(AIUnit::Wait, "WAIT"),
                                              EnumName(AIUnit::Init, "INIT"),
                                              EnumName(AIUnit::Busy, "BUSY"),
                                              EnumName(AIUnit::Completed, "OK"),
                                              EnumName(AIUnit::Delay, "DELAY"),
                                              EnumName(AIUnit::InCargo, "CARGO"),
                                              EnumName(AIUnit::Stopping, "STOPPING"),
                                              EnumName(AIUnit::Replan, "REPLAN"),
                                              EnumName(AIUnit::Stopped, "STOPPED"),
                                              EnumName(AIUnit::Sending, "SENDING"),
                                              EnumName()};
    return UnitStateNames;
}

template <>
const EnumName* Foundation::GetEnumNames(AIUnit::WatchMode dummy)
{
    static const EnumName UnitWatchModeNames[] = {EnumName(AIUnit::WMNo, "NO"),         EnumName(AIUnit::WMDir, "DIR"),
                                                  EnumName(AIUnit::WMPos, "POS"),       EnumName(AIUnit::WMTgt, "TGT"),
                                                  EnumName(AIUnit::WMAround, "AROUND"), EnumName()};
    return UnitWatchModeNames;
}

template <>
const EnumName* Foundation::GetEnumNames(AIUnit::LifeState dummy)
{
    static const EnumName LifeStateNames[] = {EnumName(AIUnit::LSAlive, "ALIVE"),
                                              EnumName(AIUnit::LSDead, "DEAD"),
                                              EnumName(AIUnit::LSDeadInRespawn, "DEAD-RESPAWN"),
                                              EnumName(AIUnit::LSAsleep, "ASLEEP"),
                                              EnumName(AIUnit::LSUnconscious, "UNCONSCIOUS"),
                                              EnumName()};
    return LifeStateNames;
}

template <>
const EnumName* Foundation::GetEnumNames(AIUnit::DisabledAI dummy)
{
    static const EnumName DisabledAINames[] = {
        EnumName(AIUnit::DATarget, "TARGET"), EnumName(AIUnit::DAAutoTarget, "AUTOTARGET"),
        EnumName(AIUnit::DAMove, "MOVE"), EnumName(AIUnit::DAAnim, "ANIM"), EnumName()};
    return DisabledAINames;
}

LSError AIUnitInfo::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.Serialize("identityContext", _identityContext, 1, RString()))
    PARAM_CHECK(ar.Serialize("name", _name, 1))
    PARAM_CHECK(ar.Serialize("experience", _experience, 1, 0))
    PARAM_CHECK(ar.SerializeEnum("rank", _rank, 1, RankPrivate))
    PARAM_CHECK(ar.Serialize("face", _face, 1, ""))
    PARAM_CHECK(ar.Serialize("glasses", _glasses, 1, "None"))
    PARAM_CHECK(ar.Serialize("speaker", _speaker, 1, ""))
    PARAM_CHECK(ar.Serialize("pitch", _pitch, 1, 1.0))
    return LSOK;
}

LSError AIUnit::Serialize(ParamArchive& ar)
{
    // structure
    PARAM_CHECK(ar.SerializeRef("Person", _person, 1))
    PARAM_CHECK(ar.SerializeRef("InVehicle", _inVehicle, 1))
    PARAM_CHECK(ar.SerializeRef("Subgroup", _subgroup, 1))

    // info
    PARAM_CHECK(ar.Serialize("id", _id, 1))
    if (ar.IsSaving())
    {
        if (_person)
            PARAM_CHECK(ar.Serialize("Info", _person->GetInfo(), 1))
    }
    else if (ar.GetPass() == ParamArchive::PassSecond)
    {
        if (_person)
        {
            AIUnitInfo& info = _person->GetInfo();
            ar.FirstPass();
            PARAM_CHECK(ar.Serialize("Info", info, 1))
            ar.SecondPass();
            PARAM_CHECK(ar.Serialize("Info", info, 1))

            if (info._face.GetLength() > 0)
            {
                _person->SetFace(info._face);
            }

            if (info._glasses.GetLength() > 0)
            {
                _person->SetGlasses(info._glasses);
            }

            if (info._speaker.GetLength() > 0)
            {
                SetSpeaker(info._speaker, info._pitch);
            }
        }
    }
    PARAM_CHECK(ar.Serialize("ability", _ability, 1, 0.2))
    if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassSecond)
    {
        _invAbility = 1.0 / _ability;
    }

    PARAM_CHECK(ar.SerializeRef("VehicleAssigned", _vehicleAssigned, 1))
    PARAM_CHECK(ar.SerializeEnum("semaphore", _semaphore, 1, SemaphoreYellow))
    PARAM_CHECK(ar.SerializeEnum("combatModeMajor", _combatModeMajor, 1, CMAware))
    PARAM_CHECK(ar.Serialize("dangerUntil", _dangerUntil, 1, Time(0)))
    PARAM_CHECK(ar.Serialize("captive", _captive, 1, false))
    PARAM_CHECK(ar.Serialize("isAway", _isAway, 1, false))

    PARAM_CHECK(ar.SerializeEnum("lifeState", _lifeState, 1, LSAlive))
    PARAM_CHECK(ar.Serialize("disabledAI", _disabledAI, 1, 0))

    PARAM_CHECK(ar.SerializeRef("TargetAssigned", _targetAssigned, 1))
    PARAM_CHECK(ar.SerializeRef("TargetEngage", _targetEngage, 1))
    PARAM_CHECK(ar.SerializeRef("TargetEnableFire", _targetEnableFire, 1))

    PARAM_CHECK(ar.Serialize("expensiveThinkFrac", _expensiveThinkFrac, 1, 0));
    PARAM_CHECK(ar.Serialize("expensiveThinkTime", _expensiveThinkTime, 1, Time(0)));

    PARAM_CHECK(ar.Serialize("nearestEnemyDist2", _nearestEnemyDist2, 1))

    // actual instructions
    PARAM_CHECK(ar.Serialize("Path", _path, 1))
    PARAM_CHECK(ar.SerializeRef("House", _house, 1))
    PARAM_CHECK(ar.Serialize("housePos", _housePos, 1, -1))

    PARAM_CHECK(ar.Serialize("wantedPosition", _wantedPosition, 1, VUndefined))
    PARAM_CHECK(ar.Serialize("plannedPosition", _plannedPosition, 1, VUndefined))
    PARAM_CHECK(ar.SerializeEnum("planningMode", _planningMode, 1, DoNotPlan))
    PARAM_CHECK(ar.Serialize("completedReceived", _completedReceived, 1, false))

    PARAM_CHECK(ar.Serialize("formationAngle", _formationAngle, 1))
    PARAM_CHECK(ar.Serialize("formationPos", _formationPos, 1))
    PARAM_CHECK(ar.Serialize("watchDirection", _watchDir, 1, VZero))
    PARAM_CHECK(ar.Serialize("watchDirectionHead", _watchDirHead, 1, VZero))
    PARAM_CHECK(ar.Serialize("watchPosition", _watchPos, 1))
    PARAM_CHECK(ar.SerializeRef("watchTarget", _watchTgt, 1))
    PARAM_CHECK(ar.SerializeEnum("watchMode", _watchMode, 1))

    PARAM_CHECK(ar.Serialize("expPosition", _expPosition, 1))

    PARAM_CHECK(ar.SerializeEnum("state", _state, 1, Busy))
    PARAM_CHECK(ar.SerializeEnum("mode", _mode, 1, Normal))

    // counter for trying of finding path
    PARAM_CHECK(ar.Serialize("delay", _delay, 1))
    PARAM_CHECK(ar.Serialize("iter", _iter, 1, 0))

    PARAM_CHECK(ar.Serialize("getInAllowed", _getInAllowed, 1, true))
    PARAM_CHECK(ar.Serialize("getInOrdered", _getInOrdered, 1, false))

    // messages
    PARAM_CHECK(ar.SerializeEnum("lastFuelState", _lastFuelState, 1, RSNormal))
    PARAM_CHECK(ar.SerializeEnum("lastHealthState", _lastHealthState, 1, RSNormal))
    PARAM_CHECK(ar.SerializeEnum("lastArmorState", _lastArmorState, 1, RSNormal))
    PARAM_CHECK(ar.SerializeEnum("lastAmmoState", _lastAmmoState, 1, RSNormal))

    PARAM_CHECK(ar.Serialize("_fuelCriticalTime", _fuelCriticalTime, 1))
    PARAM_CHECK(ar.Serialize("_healthCriticalTime", _healthCriticalTime, 1))
    PARAM_CHECK(ar.Serialize("_dammageCriticalTime", _dammageCriticalTime, 1))
    PARAM_CHECK(ar.Serialize("_ammoCriticalTime", _ammoCriticalTime, 1))

    // strategic plan
    if (ar.GetArVersion() >= 8)
    {
        PARAM_CHECK(ar.Serialize("Planner", *_planner, 1))
        PARAM_CHECK(ar.Serialize("completedTime", _completedTime, 1)) // not used - why ??
        PARAM_CHECK(ar.Serialize("waitWithPlan", _waitWithPlan, 1))
        PARAM_CHECK(ar.Serialize("attemptPlan", _attemptPlan, 1, 0))

        PARAM_CHECK(ar.Serialize("lastPlan", _lastPlan, 1, false))
        PARAM_CHECK(ar.Serialize("noPath", _noPath, 1, false))
        PARAM_CHECK(ar.Serialize("updatePath", _updatePath, 1, false))

        PARAM_CHECK(ar.Serialize("exposureChange", _exposureChange, 1, 0))
    }
    return LSOK;
}

AIUnit* AIUnit::LoadRef(ParamArchive& ar)
{
    TargetSide side = TSideUnknown;
    int idGroup;
    int id;
    if (ar.SerializeEnum("side", side, 1) != LSOK)
    {
        return nullptr;
    }
    if (ar.Serialize("idGroup", idGroup, 1) != LSOK)
    {
        return nullptr;
    }
    if (ar.Serialize("id", id, 1) != LSOK)
    {
        return nullptr;
    }
    AICenter* center = nullptr;
    switch (side)
    {
        case TWest:
            center = GWorld->GetWestCenter();
            break;
        case TEast:
            center = GWorld->GetEastCenter();
            break;
        case TGuerrila:
            center = GWorld->GetGuerrilaCenter();
            break;
        case TCivilian:
            center = GWorld->GetCivilianCenter();
            break;
        case TLogic:
            center = GWorld->GetLogicCenter();
            break;
    }
    if (!center)
    {
        return nullptr;
    }
    AIGroup* group = nullptr;
    for (int i = 0; i < center->NGroups(); i++)
    {
        AIGroup* grp = center->GetGroup(i);
        if (grp && grp->ID() == idGroup)
        {
            group = grp;
            break;
        }
    }
    if (!group)
    {
        return nullptr;
    }
    return group->UnitWithID(id);
}

LSError AIUnit::SaveRef(ParamArchive& ar) const
{
    AIGroup* grp = GetGroup();
    AICenter* center = grp ? grp->GetCenter() : nullptr;
    TargetSide side = center ? center->GetSide() : TSideUnknown;
    int idGroup = grp ? grp->ID() : -1;
    int id = ID();
    PARAM_CHECK(ar.SerializeEnum("side", side, 1))
    PARAM_CHECK(ar.Serialize("idGroup", idGroup, 1))
    PARAM_CHECK(ar.Serialize("id", id, 1))
    return LSOK;
}

NetworkMessageType AIUnit::GetNMType(NetworkMessageClass cls) const
{
    switch (cls)
    {
        case NMCCreate:
            return NMTCreateAIUnit;
        case NMCUpdateGeneric:
            return NMTUpdateAIUnit;
        default:
            return NMTNone;
    }
}

IndicesCreateAIUnit::IndicesCreateAIUnit()
{
    person = -1;
    subgroup = -1;
    id = -1;
    name = -1;
    face = -1;
    glasses = -1;
    speaker = -1;
    pitch = -1;
    rank = -1;
    experience = -1;
    initExperience = -1;
    playable = -1;
    squadPicture = -1;
    squadTitle = -1;
}

void IndicesCreateAIUnit::Scan(NetworkMessageFormatBase* format)
{
    base::Scan(format);

    SCAN(person)
    SCAN(subgroup)
    SCAN(id)
    SCAN(name)
    SCAN(face)
    SCAN(glasses)
    SCAN(speaker)
    SCAN(pitch)
    SCAN(rank)
    SCAN(experience)
    SCAN(initExperience)
    SCAN(playable)
    SCAN(squadPicture)
    SCAN(squadTitle)
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesCreateAIUnit()
{
    using namespace Poseidon;
    return new IndicesCreateAIUnit();
}
namespace Poseidon
{

class IndicesUpdateAIUnit : public IndicesNetworkObject
{
    typedef IndicesNetworkObject base;

  public:
    int person;
    int experience;
    int ability;
    int semaphore;
    int combatModeMajor;
    int dangerUntil;
    int captive;
    int house;
    int housePos;
    int wantedPosition;
    int planningMode;
    int formationAngle;
    int formationPos;
    int state;
    int mode;
    int lifeState;
    // ?? _delay
    int getInAllowed;
    int getInOrdered;

    IndicesUpdateAIUnit();
    ~IndicesUpdateAIUnit() override;
    NetworkMessageIndices* Clone() const override { return new IndicesUpdateAIUnit; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesUpdateAIUnit::IndicesUpdateAIUnit()
{
    person = -1;
    experience = -1;
    ability = -1;
    semaphore = -1;
    combatModeMajor = -1;
    dangerUntil = -1;
    captive = -1;
    house = -1;
    housePos = -1;
    wantedPosition = -1;
    planningMode = -1;
    formationAngle = -1;
    formationPos = -1;
    state = -1;
    mode = -1;
    lifeState = -1;
    // ?? _delay
    getInAllowed = -1;
    getInOrdered = -1;
}

IndicesUpdateAIUnit::~IndicesUpdateAIUnit()
{
    /*
        void DeleteIndicesPath(IndicesPath *path);
        DeleteIndicesPath(path);
    */
}

void IndicesUpdateAIUnit::Scan(NetworkMessageFormatBase* format)
{
    base::Scan(format);

    SCAN(person)
    SCAN(experience)
    SCAN(ability)
    SCAN(semaphore)
    SCAN(combatModeMajor)
    SCAN(dangerUntil)
    SCAN(captive)
    SCAN(house)
    SCAN(housePos)
    SCAN(wantedPosition)
    SCAN(planningMode)
    SCAN(formationAngle)
    SCAN(formationPos)
    SCAN(state)
    SCAN(mode)
    SCAN(lifeState)
    // ?? _delay
    SCAN(getInAllowed)
    SCAN(getInOrdered)
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesUpdateAIUnit()
{
    using namespace Poseidon;
    return new IndicesUpdateAIUnit();
}
namespace Poseidon
{

NetworkMessageFormat& AIUnit::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    switch (cls)
    {
        case NMCCreate:
            NetworkObject::CreateFormat(cls, format);
            format.Add("person", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Attached body"));
            format.Add("subgroup", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Superior subgroup"));
            format.Add("id", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("ID in group"));
            format.Add("name", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Full name"));
            format.Add("face", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Face"));
            format.Add("glasses", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Glasses type"));
            format.Add("speaker", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Speaker"));
            format.Add("pitch", NDTFloat, NCTNone, DEFVALUE(float, 1.0f), DOC_MSG("Voice pitch"));
            format.Add("rank", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, RankPrivate), DOC_MSG("Current rank"));
            format.Add("experience", NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Amount of experience"));
            format.Add("initExperience", NDTFloat, NCTNone, DEFVALUE(float, 0),
                       DOC_MSG("Initial amount of experience"));
            format.Add("playable", NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Is unit playable"));
            format.Add("squadPicture", NDTString, NCTNone, DEFVALUE(RString, ""),
                       DOC_MSG("Squad picture (shown on vehicles)"));
            format.Add("squadTitle", NDTString, NCTNone, DEFVALUE(RString, ""),
                       DOC_MSG("Squad title (shown on vehicles)"));
            break;
        case NMCUpdateGeneric:
            NetworkObject::CreateFormat(cls, format);
            format.Add("person", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Attached body"));
            format.Add("experience", NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Amount of experience"), ET_ABS_DIF,
                       0.01 * ERR_COEF_VALUE_MINOR);
            format.Add("ability", NDTFloat, NCTFloat0To1, DEFVALUE(float, 0.2), DOC_MSG("Ability of (AI) unit"),
                       ET_ABS_DIF, ERR_COEF_VALUE_MAJOR);
            format.Add("semaphore", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, SemaphoreYellow),
                       DOC_MSG("Combat mode"), ET_NOT_EQUAL, ERR_COEF_MODE);
            format.Add("combatModeMajor", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, CMAware), DOC_MSG("Behaviour"),
                       ET_NOT_EQUAL, ERR_COEF_MODE);
            format.Add("dangerUntil", NDTTime, NCTNone, DEFVALUE(Time, Time(0)), DOC_MSG("In danger mode until..."),
                       ET_NOT_EQUAL, ERR_COEF_MODE);
            format.Add("captive", NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Captive unit"));
            format.Add("house", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("House, where destination is"), ET_NOT_EQUAL,
                       ERR_COEF_VALUE_MAJOR);
            format.Add("housePos", NDTInteger, NCTSmallSigned, DEFVALUE(int, -1),
                       DOC_MSG("Destination position in house"), ET_NOT_EQUAL, ERR_COEF_VALUE_MAJOR);
            format.Add("wantedPosition", NDTVector, NCTNone, DEFVALUE(Vector3, VUndefined),
                       DOC_MSG("Destination position"), ET_NOT_EQUAL, ERR_COEF_VALUE_MINOR);
            format.Add("planningMode", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, DoNotPlan),
                       DOC_MSG("How to plan path"), ET_NOT_EQUAL, ERR_COEF_VALUE_MINOR);
            format.Add("formationAngle", NDTFloat, NCTFloatAngle, DEFVALUE(float, 0),
                       DOC_MSG("Relative orientation in formation"));
            format.Add("formationPos", NDTVector, NCTNone, DEFVALUE(Vector3, VZero),
                       DOC_MSG("Relative position in formation"));
            format.Add("state", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, Wait), DOC_MSG("Unit (FSM) state"),
                       ET_NOT_EQUAL, ERR_COEF_MODE);
            format.Add("mode", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, Normal),
                       DOC_MSG("Precision of path planning"), ET_NOT_EQUAL, ERR_COEF_MODE);
            format.Add("lifeState", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, LSAlive),
                       DOC_MSG("Life state of unit (Alive, Death, Asleep etc.)"), ET_NOT_EQUAL, ERR_COEF_MODE);
            // ?? _delay
            format.Add("getInAllowed", NDTBool, NCTNone, DEFVALUE(bool, true), DOC_MSG("Can get in vehicles"),
                       ET_NOT_EQUAL, ERR_COEF_MODE);
            format.Add("getInOrdered", NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Must get in vehicles"),
                       ET_NOT_EQUAL, ERR_COEF_MODE);
            break;
        default:
            NetworkObject::CreateFormat(cls, format);
            break;
    }
    return format;
}

AIUnit* AIUnit::CreateObject(NetworkMessageContext& ctx)
{
    AI_ERROR(dynamic_cast<const IndicesCreateAIUnit*>(ctx.GetIndices()))
    const IndicesCreateAIUnit* indices = static_cast<const IndicesCreateAIUnit*>(ctx.GetIndices());

    Person* person;
    if (ctx.IdxTransferRef(indices->person, person) != TMOK)
    {
        return nullptr;
    }
    if (!person)
    {
        return nullptr;
    }
    AISubgroup* subgrp;
    if (ctx.IdxTransferRef(indices->subgroup, subgrp) != TMOK)
    {
        return nullptr;
    }
    if (!subgrp)
    {
        return nullptr;
    }
    AIGroup* grp = subgrp->GetGroup();
    if (!grp)
    {
        return nullptr;
    }
    int id;
    if (ctx.IdxTransfer(indices->id, id) != TMOK)
    {
        return nullptr;
    }
    AIUnit* unit = person->Brain();
    unit->_id = id;
    grp->SetUnit(id - 1, unit);
    subgrp->AddUnit(unit);

    // temporary select leader (avoid diag output)
    if (!grp->Leader())
    {
        grp->GetCenter()->SelectLeader(grp);
    }
    if (!subgrp->Leader())
    {
        subgrp->SelectLeader();
    }

    NetworkId objectId;
    if (ctx.IdxTransfer(indices->objectCreator, objectId.creator) != TMOK)
    {
        return nullptr;
    }
    if (ctx.IdxTransfer(indices->objectId, objectId.id) != TMOK)
    {
        return nullptr;
    }
    unit->SetNetworkId(objectId);
    unit->SetLocal(false);

    unit->TransferMsg(ctx);

    return unit;
}

void AIUnit::DestroyObject()
{
    AddRef();

    // remove from AI structure
    Ref<AISubgroup> oldSubgroup = (AISubgroup*)_subgroup;
    Ref<AIGroup> oldGroup = GetGroup();
    bool bSubgroupLeader = oldSubgroup && oldSubgroup->Leader() == this;
    bool bGroupLeader = oldGroup && oldGroup->Leader() == this;
    if (oldGroup)
    {
        oldGroup->UnitRemoved(this);
        _subgroup = nullptr;
        _id = 0;
    }
    if (bGroupLeader)
    {
        oldGroup->GetCenter()->SelectLeader(oldGroup);
    }
    if (bSubgroupLeader)
    {
        oldSubgroup->SelectLeader();
    }

    // remove from vehicle
    Transport* veh = VehicleAssigned();
    if (veh)
    {
        veh->UpdateStop();
    }

    Person* person = _person;
    if (person)
    {
        _person = nullptr;
        GWorld->RemoveSensor(person);
        person->SetBrain(nullptr);
    }
    Release();
}

TMError AIUnit::TransferMsg(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCCreate:
        {
            TMCHECK(NetworkObject::TransferMsg(ctx))
            {
                AI_ERROR(dynamic_cast<const IndicesCreateAIUnit*>(ctx.GetIndices()))
                const IndicesCreateAIUnit* indices = static_cast<const IndicesCreateAIUnit*>(ctx.GetIndices());

                if (ctx.IsSending())
                {
                    ITRANSF_REF(person)
                    ITRANSF_REF(subgroup)
                    ITRANSF(id)
                }
                AIUnitInfo& info = GetPerson()->GetInfo();
                TMCHECK(ctx.IdxTransfer(indices->name, info._name))
                TMCHECK(ctx.IdxTransfer(indices->face, info._face))
                TMCHECK(ctx.IdxTransfer(indices->glasses, info._glasses))
                TMCHECK(ctx.IdxTransfer(indices->speaker, info._speaker))
                TMCHECK(ctx.IdxTransfer(indices->pitch, info._pitch))
                TMCHECK(ctx.IdxTransfer(indices->rank, (int&)info._rank))
                TMCHECK(ctx.IdxTransfer(indices->experience, info._experience))
                TMCHECK(ctx.IdxTransfer(indices->initExperience, info._initExperience))
                TMCHECK(ctx.IdxTransfer(indices->playable, _playable))
                // ADDED in 1.04
                TMCHECK(ctx.IdxTransfer(indices->squadTitle, info._squadTitle))
                if (ctx.IsSending())
                {
                    // ADDED in 1.04
                    RString picture;
                    if (info._squadPicture)
                    {
                        picture = info._squadPicture->GetName();
                    }
                    TMCHECK(ctx.IdxTransfer(indices->squadPicture, picture))
                }
                else
                {
                    // ADDED in 1.04
                    RString picture;
                    TMCHECK(ctx.IdxTransfer(indices->squadPicture, picture))
                    if (picture.GetLength() > 0)
                    {
                        info._squadPicture = GlobLoadTexture(picture);
                    }

                    GetPerson()->SetFace(info._face, info._name);
                    GetPerson()->SetGlasses(info._glasses);
                    SetSpeaker(info._speaker, info._pitch);
                }
            }
        }
        break;
        case NMCUpdateGeneric:
            TMCHECK(NetworkObject::TransferMsg(ctx))
            {
                AI_ERROR(dynamic_cast<const IndicesUpdateAIUnit*>(ctx.GetIndices()))
                const IndicesUpdateAIUnit* indices = static_cast<const IndicesUpdateAIUnit*>(ctx.GetIndices());

                if (ctx.IsSending())
                {
                    ITRANSF_REF(person)
                }
                else
                {
                    VehicleWithBrain* person = nullptr;
                    TMCHECK(ctx.IdxTransferRef(indices->person, person))
                    // FIX
                    if (person && person != _person)
                    {
                        AddRef();
                        if (_person)
                        {
                            if (person)
                            {
                                AIUnitInfo& info = _person->GetInfo();
                                person->SetInfo(info);
                                person->SetFace(info._face, info._name);
                                person->SetGlasses(info._glasses);
                            }
                            GWorld->RemoveSensor(_person);
                            _person->SetBrain(nullptr);
                            _person = nullptr;
                        }
                        if (person)
                        {
                            person->SetBrain(this);
                            _person = person;
                        }
                        Release();
                        _inVehicle = nullptr;
                    }
                }
                if (_person)
                {
                    TMCHECK(ctx.IdxTransfer(indices->experience, _person->GetInfo()._experience))
                }
                ITRANSF(ability)
                if (!ctx.IsSending())
                {
                    _invAbility = 1.0 / _ability;
                }
                ITRANSF_ENUM(semaphore)
                ITRANSF_ENUM(combatModeMajor)
                ITRANSF(dangerUntil)
                ITRANSF(captive)
                ITRANSF_REF(house)
                ITRANSF(housePos)
                ITRANSF(wantedPosition)
                ITRANSF_ENUM(planningMode)
                ITRANSF(formationAngle)
                ITRANSF(formationPos)
                ITRANSF_ENUM(state)
                ITRANSF_ENUM(mode)
                ITRANSF_ENUM(lifeState)
                // ?? _delay
                ITRANSF(getInAllowed)
                ITRANSF(getInOrdered)
                if (!ctx.IsSending())
                {
                    _path.Clear();
                    _plannedPosition = _wantedPosition;
                    _completedReceived = false;
                    _expPosition = _wantedPosition;
                    _lastPlan = false;
                    _noPath = true;
                }
            }
            break;
        default:
            TMCHECK(NetworkObject::TransferMsg(ctx))
            break;
    }
    return TMOK;
}

float AIUnit::CalculateError(NetworkMessageContext& ctx)
{
    float error = NetworkObject::CalculateError(ctx);

    AI_ERROR(dynamic_cast<const IndicesUpdateAIUnit*>(ctx.GetIndices()))
    const IndicesUpdateAIUnit* indices = static_cast<const IndicesUpdateAIUnit*>(ctx.GetIndices());

    if (_person)
    {
        ICALCERRE_ABSDIF(float, experience, _person->GetInfo()._experience, 0.01 * ERR_COEF_VALUE_MINOR)
    }
    ICALCERRE_ABSDIF(float, ability, _ability, ERR_COEF_VALUE_MAJOR)
    ICALCERR_NEQ(int, semaphore, ERR_COEF_MODE)
    ICALCERR_NEQ(int, combatModeMajor, ERR_COEF_MODE)
    ICALCERR_NEQ(Time, dangerUntil, ERR_COEF_MODE)
    ICALCERR_NEQ(bool, captive, ERR_COEF_MODE)
    ICALCERR_NEQREF(Object, house, ERR_COEF_VALUE_MAJOR)
    ICALCERR_NEQ(int, housePos, ERR_COEF_VALUE_MAJOR)
    ICALCERR_NEQ(Vector3, wantedPosition, ERR_COEF_VALUE_MINOR)
    ICALCERR_NEQ(int, planningMode, ERR_COEF_VALUE_MINOR)
    ICALCERR_NEQ(int, state, ERR_COEF_MODE)
    ICALCERR_NEQ(int, mode, ERR_COEF_MODE)
    ICALCERR_NEQ(bool, getInAllowed, ERR_COEF_MODE)
    ICALCERR_NEQ(bool, getInOrdered, ERR_COEF_MODE)
    return error;
}

EntityAI* AIUnit::GetVehicle() const
{
    EntityAI* inVeh = _inVehicle.GetLink();
    if (inVeh)
    {
        return inVeh;
    }
    else
    {
        return _person.GetLink();
    }
}

void AIUnit::SetSpeaker(RString speaker, float pitch)
{
    const ParamEntry& cfg = Pars >> "CfgVoice";
    if (speaker.GetLength() <= 0)
    {
        // Note: remove hack (to patch nullptr speaker in MP)
        Fail("Speaker");
        speaker = (cfg >> "voices")[0];
    }
    const ParamEntry* entry = cfg.FindEntry(speaker);
    if (!entry)
    {
        speaker = (cfg >> "voices")[0];
        entry = cfg.FindEntry(speaker);
    }
    AI_ERROR(entry);
    Ref<BasicSpeaker> basic = new BasicSpeaker(*entry);
    _speaker = new Speaker(basic, pitch);
}

CombatMode AIUnit::GetCombatMode() const
{
    CombatMode mode = _combatModeMajor;
    if (mode != CMCareless)
    {
        AIGroup* grp = GetGroup();
        if (grp)
        {
            CombatMode gMode = grp->GetCombatModeMinor();
            if (mode < gMode)
            {
                mode = gMode;
            }
        }
        EntityAI* veh = GetVehicle();
        if (veh->GetFireTarget() && !veh->IsFirePrepare())
        {
            CombatMode gMode = CMCombat;
            if (mode < gMode)
            {
                mode = gMode;
            }
        }
    }
    return mode;
}

bool AIUnit::HasAI() const
{
    if (_inVehicle)
    {
        if (_inVehicle->QIsManual(this))
        {
            return false;
        }
    }
    else if (_person->QIsManual())
    {
        return false;
    }

    return !_person->IsRemotePlayer();
}

bool AIUnit::IsUnit() const
{
    if (!_inVehicle)
    {
        return true;
    }
    return _inVehicle->CommanderUnit() == this;
}

bool AIUnit::IsCommander() const
{
    Transport* vehIn = GetVehicleIn();
    return vehIn && vehIn->CommanderBrain() == this;
}

bool AIUnit::IsDriver() const
{
    Transport* vehIn = GetVehicleIn();
    return vehIn && vehIn->DriverBrain() == this;
}

bool AIUnit::IsGunner() const
{
    Transport* vehIn = GetVehicleIn();
    return vehIn && vehIn->GunnerBrain() == this;
}

bool AIUnit::IsInCargo() const
{
    return _state == InCargo;
}

void AIUnit::SetAbility(float ability)
{
    AI_ERROR(ability > 0);
    _ability = ability;
    _invAbility = 1.0 / ability;
}
float AIUnit::GetAbility() const
{
    // returns from 1 (able) to 0.2 (unable)
    if (IsAnyPlayer() || USER_CONFIG.IsEnabled(DTUltraAI))
    {
        return 1; // always maximal
    }
    return _ability;
}

float AIUnit::GetInvAbility() const
{
    // returns from 1 (able) to 5 (unable)
    if (IsAnyPlayer() || USER_CONFIG.IsEnabled(DTUltraAI))
    {
        return 1; // always maximal
    }
    return _invAbility;
}

void AIUnit::AddExp(float exp)
{
    float limit = floatMin(GetPerson()->GetInfo()._experience, 0);
    GetPerson()->GetInfo()._experience += exp;
    if (GetPerson() != GLOB_WORLD->GetRealPlayer())
    {
        saturateMax(GetPerson()->GetInfo()._experience, limit);
    }
}

void AIUnit::ClearOperativePlan()
{
    _path.Clear();
    _path.SetOperIndex(1);
    _path.SetMaxIndex(1);
    _path.SetOnRoad(false);
}

void AIUnit::ClearStrategicPlan()
{
    _noPath = true;
    _updatePath = false;
    _lastPlan = false;

    _attemptPlan = 0;
    _planner->Init();
    _waitWithPlan = Time(0);
    _completedTime = Glob.clock.GetTimeInYear();

    if (GetState() != InCargo && GetState() != Stopping && GetState() != Stopped)
        Verify(SetState(Wait));

    _exposureChange = 0;
}

void AIUnit::RefreshStrategicPlan()
{
    if (_noPath)
    {
        ClearStrategicPlan();
    }
    else
    {
        AI_ERROR(_planner->GetPlanSize() > 0);
        _planner->StopSearching();
        _updatePath = true;
    }
    _exposureChange = 0;
}

void AIUnit::ForceReplan(bool clear)
{
    _plannedPosition = _wantedPosition;
    if (clear)
    {
        ClearStrategicPlan();
        ClearOperativePlan();
    }
    else
    {
        RefreshStrategicPlan();
    }
}

void AIUnit::ExposureChanged(int x, int z, float optimistic, float pessimistic)
{
    float exposure = IsHoldingFire() ? fabs(pessimistic) : fabs(optimistic);
    if (exposure < VALID_EXPOSURE_CHANGE)
    {
        return;
    }

    if (_planner->IsOnPath(x, z, _planner->FindBestIndex(Position()), _planner->GetPlanSize() - 1))
    {
        _exposureChange += exposure;
    }
}

bool AIUnit::SetState(State state)
{
    if (state == Completed)
    {
        if (_planningMode == DoNotPlan)
        {
            return true;
        }

        if (_completedReceived)
        {
            return true;
        }
        _completedReceived = true;
    }

    if (state == InCargo)
    {
        _state = InCargo;
        return true;
    }

    if (_inVehicle)
    {
        AIUnit* unit;
        unit = _inVehicle->PilotUnit();
        if (unit)
        {
            if (state == Wait)
            {
                ClearOperativePlan();
            }
            unit->_state = state;
        }
        unit = _inVehicle->ObserverUnit();
        if (unit)
        {
            if (state == Wait)
            {
                ClearOperativePlan();
            }
            unit->_state = state;
        }
        unit = _inVehicle->GunnerUnit();
        if (unit)
        {
            if (state == Wait)
            {
                ClearOperativePlan();
            }
            unit->_state = state;
        }
    }
    else
    {
        AI_ERROR(_state != InCargo);
        if (_state == InCargo)
        {
            return false;
        }
        if (state == Wait)
        {
            ClearOperativePlan();
        }
        _state = state;
    }
    return true;
}

// check size of formation

static float FormSize(AISubgroup* subgrp)
{
    return 0.0f;
}

Vector3 AIUnit::GetFormationRelative() const
{
    // most common case
    // no need to calculate formation size
    if (_formPos == AI::PosInFormation)
    {
        return _formationPos;
    }
    // relative to leader
    // consider formation position (advance etc...)
    Vector3 offset = VZero;
    // calculate formation size
    float sizeX = 0;
    float sizeZ = 0;
    AISubgroup* subgrp = GetSubgroup();
    for (int i = 0; i < subgrp->NUnits(); i++)
    {
        AIUnit* unit = subgrp->GetUnit(i);
        if (!unit)
        {
            continue;
        }
        EntityAI* veh = unit->GetVehicle();
        sizeX += veh->GetFormationX();
        sizeZ += veh->GetFormationZ();
    }
    if (subgrp->NUnits() < 8)
    {
        float nCoef = 8.0 / subgrp->NUnits();
        sizeX *= nCoef;
        sizeZ *= nCoef;
    }
    float coef = _formPosCoef;
    switch (_formPos)
    {
        case AI::PosAdvance:
            offset[2] += sizeZ * coef;
            break;
        case AI::PosStayBack:
            offset[2] -= sizeZ * coef;
            break;
        case AI::PosFlankLeft:
            offset[0] -= sizeX * coef;
            break;
        case AI::PosFlankRight:
            offset[0] += sizeX * coef;
            break;
    }
    return _formationPos + offset;
}

Vector3 AIUnit::GetFormationAbsolute(AIUnit* leader) const
{
    // absolute formation position
    // suppose leader is formation leader
    EntityAI* lVehicle = leader->GetVehicle();

    AISubgroup* subgrp = leader->GetSubgroup();
    Matrix4 transform;
    Vector3Val formDir = subgrp->GetFormationDirection();
    transform.SetDirectionAndUp(formDir, VUp);
    transform.SetPosition(lVehicle->Position());

    Vector3 pos = transform.FastTransform(GetFormationRelative() - leader->GetFormationRelative());
    if (GetVehicle()->GetType()->GetKind() != VAir)
    {
        // absolute formation position
        pos[1] = GLandscape->SurfaceYAboveWater(pos[0], pos[2]);
    }
    else
    {
        float landY = GLandscape->SurfaceYAboveWater(pos[0], pos[2]);
        saturateMax(pos[1], landY + 25);
    }
    return pos;
}

Vector3 AIUnit::GetFormationAbsolute() const
{
    if (!_subgroup)
    {
        if (!IsAnyPlayer())
        {
            Fail("No subgroup");
        }
        return GetVehicle()->Position();
    }
    AIUnit* leader = _subgroup->Leader();
    if (!leader)
    {
        Fail("No leader");
        return GetVehicle()->Position();
    }
    return GetFormationAbsolute(leader);
}

bool AIUnit::IsSimplePath(Vector3Val from, Vector3Val pos)
{
    const float maxDist = 300;
    float dist2 = (from - pos).SquareSizeXZ();
    if (dist2 > Square(maxDist))
    {
        return false;
    }

    GetVehicle()->PerformUnlock();

    int xs = toIntFloor(from.X() * InvOperItemGrid);
    int zs = toIntFloor(from.Z() * InvOperItemGrid);
    int xe = toIntFloor(pos.X() * InvOperItemGrid);
    int ze = toIntFloor(pos.Z() * InvOperItemGrid);

    int xMin = xs / OperItemRange;
    int zMin = zs / OperItemRange;
    int xMax = xe / OperItemRange;
    int zMax = ze / OperItemRange;
    if (xMax < xMin)
    {
        swap(xMax, xMin);
    }
    if (zMax < zMin)
    {
        swap(zMax, zMin);
    }

    OperMap map;
    if ((xMax - xMin) > 50 || (zMax - zMin) > 50)
    {
        LOG_DEBUG(AI, "xMax {}, xMin {}", xMax, xMin);
        LOG_DEBUG(AI, "zMax {}, zMin {}", xMax, xMin);
        LOG_DEBUG(AI, "pos {:.2f},{:.2f},{:.2f}", pos[0], pos[1], pos[2]);
        LOG_DEBUG(AI, "from {:.2f},{:.2f},{:.2f}", from[0], from[1], from[2]);
    }
    map.CreateMap(GetVehicle(), xMin, zMin, xMax, zMax);
    bool retVal = map.IsSimplePath(this, xs, zs, xe, ze);

    GetVehicle()->PerformLock();
    return retVal;
}

bool AIUnit::VerifyPath()
{
    // do not verify fresh path
    if (_path.GetSearchTime() > Glob.time - 0.5)
    {
        // assume fresh path is always valid
        return true;
    }

    EntityAI* veh = GetVehicle();
    if (_path.GetOnRoad())
    {
        veh->PerformUnlock();
        int index = _path.GetOperIndex();
        for (int i = index; i < _path.GetMaxIndex(); i++)
        {
            if (GRoadNet->IsLocked(_path[i]._pos, 0.1))
            {
                return false;
            }
        }
        veh->PerformLock();
    }
    else
    {
        return true;
    }
    return true;
}

void AIUnit::CopyPath(const IAIPathPlanner& planner)
{
    EntityAI* veh = GetVehicle();
    float combatHeight = veh->GetCombatHeight();

    int planIndex = planner.FindBestIndex(Position());
    int i, n = planner.GetPlanSize() - planIndex;
    if (planner.GetPlanSize() > 0 && n > 0)
    {
        float sumCost = 0;
        Point3 lastPos = Position();

        _path.Resize(n + 1);
        _path[0]._pos = lastPos;
        _path[0]._cost = sumCost;
        for (i = 0; i < n; i++)
        {
            Point3 pos;
            planner.GetPlanPosition(planIndex + i, pos);
            GeographyInfo geogr = planner.GetGeography(planIndex + i);
            float cost = veh->GetCost(geogr) * veh->GetFieldCost(geogr);
            if (cost >= GET_UNACCESSIBLE)
            {
                cost = 1.0;
            }
            cost *= (pos - lastPos).SizeXZ();
            sumCost += cost;
            lastPos = GLandscape->PointOnSurface(pos[0], combatHeight, pos[2]);
            _path[i + 1]._pos = lastPos;
            _path[i + 1]._cost = sumCost;
        }
    }
    else
    {
        int x = toIntFloor(Position().X() * InvLandGrid);
        int z = toIntFloor(Position().Z() * InvLandGrid);
        GeographyInfo geogr = GLOB_LAND->GetGeography(x, z);
        float cost = veh->GetCost(geogr) * veh->GetFieldCost(geogr);
        if (cost >= GET_UNACCESSIBLE)
        {
            cost = 1.0;
        }
        cost *= (_expPosition - Position()).SizeXZ();
        _path.Resize(2);
        _path[0]._pos = Position();
        _path[0]._cost = 0;
        _path[1]._pos = GLandscape->PointOnSurface(_expPosition[0], combatHeight, _expPosition[2]);
        _path[1]._cost = cost;
    }
    _path.SetMaxIndex(2);
    _path.SetOperIndex(1);
    _path.SetOnRoad(false);
    _path.SetSearchTime(Glob.time);
}

static const IPaths* InsideBuilding(Vector3Val pos)
{
    int xMin, xMax, zMin, zMax;
    ObjRadiusRectangle(xMin, xMax, zMin, zMax, pos, pos, 50);
    int x, z;
    for (x = xMin; x <= xMax; x++)
    {
        for (z = zMin; z <= zMax; z++)
        {
            const ObjectList& list = GLandscape->GetObjects(z, x);
            int n = list.Size();
            for (int i = 0; i < n; i++)
            {
                Object* obj = list[i];

                if (!obj->GetShape())
                {
                    continue;
                }
                if (obj->GetShape()->FindPaths() < 0)
                {
                    continue;
                }
                // vehicle with paths - must be building
                float dist2 = obj->Position().Distance2(pos);
                if (dist2 > Square(obj->GetShape()->BoundingSphere()))
                {
                    continue;
                }
                // pos is inside vehicle's bounding

                const IPaths* building = obj->GetIPaths();
                if (!building)
                {
                    continue;
                }

                // check if point is near of some path in building
                Vector3 nearest;
                int nearestIndex = building->FindNearestPoint(pos, nearest, Square(2.5));
                if (nearestIndex >= 0)
                {
                    return building;
                }
            }
        }
    }
    return nullptr;
}

#define ROAD_DIAGS 0
#define HOUSE_DIAGS 0

#define DIST_NOT_SEARCH 5.0F

bool AIUnit::CreatePath(Vector3Val from, Vector3Val to)
{
    // this function always sets path.time to time of last search
    _path.SetSearchTime(Glob.time);
    const IPaths* fromBuilding = InsideBuilding(from);
    const IPaths* toBuilding = _house ? _house->GetIPaths() : InsideBuilding(to);
    EntityAI* veh = GetVehicle();
    float combatHeight = veh->GetCombatHeight();
    if (IsFreeSoldier() && (fromBuilding || toBuilding))
    {
        // how much slower should units move in buildings
        if (fromBuilding == toBuilding)
        {
#if HOUSE_DIAGS
            LOG_DEBUG(AI, "{}: fromTo {}", (const char*)GetDebugName(),
                      (const char*)toBuilding->GetObject()->GetDebugName());
#endif
            Vector3 dummy;
            int posFrom = fromBuilding->FindNearestPoint(from - Vector3(0, combatHeight, 0), dummy);
            if (posFrom < 0)
            {
                return false;
            }
            int posTo = _house ? toBuilding->GetPos(_housePos) : toBuilding->FindNearestPoint(to, dummy);
            if (posTo < 0)
            {
                posTo = toBuilding->GetPos(0);
                if (posTo < 0)
                {
                    return false;
                }
            }
            HOUSE_PATH_ARRAY(path, 64);

            if (!fromBuilding->SearchPath(posFrom, posTo, path))
            {
                return false; // cannot get outside - failed
            }
            int n = path.Size();
            _path.Resize(n);
            float cost = 0;
            Vector3 lastPos = path[0].pos;
            _path[0]._pos = lastPos;
            _path[0]._pos[1] += combatHeight;
            _path[0]._cost = cost;
            _path[0]._house = const_cast<Object*>(fromBuilding->GetObject());
            _path[0]._index = path[0].index;
            for (int i = 1; i < n; i++)
            {
                cost += fromBuilding->GetBType()->GetCoefInside() * path[i].pos.Distance(lastPos) *
                        GetVehicle()->GetType()->GetMinCost();
                lastPos = path[i].pos;
                _path[i]._pos = lastPos;
                _path[i]._pos[1] += combatHeight;
                _path[i]._cost = cost;
                _path[i]._house = const_cast<Object*>(fromBuilding->GetObject());
                _path[i]._index = path[i].index;
            }
            _path.SetOperIndex(1);
            _path.SetMaxIndex(n);
            _path.SetOnRoad(false);
            return true;
        }
        else
        {
            return CreateNoRoadPath(from, to, fromBuilding, toBuilding);
        }
    }
    if (!GetVehicle()->IsCautious() && CreateRoadPath(from, to))
    {
        return true;
    }
    return CreateNoRoadPath(from, to);
}

bool AIUnit::CheckEmpty(Vector3Par pos)
{
    EntityAI* veh = GetVehicle();
    veh->PerformUnlock();

    float xEnd = pos.X();
    float zEnd = pos.Z();
    int xe = toIntFloor(xEnd * InvOperItemGrid);
    int ze = toIntFloor(zEnd * InvOperItemGrid);

    int xei = xe / OperItemRange;
    int zei = ze / OperItemRange;

    const int border = 0;

    OperMap map;
    map.CreateMap(veh, xei - border, zei - border, xei + border, zei + border);

    bool ret = map.GetFieldCost(xe, ze, true, GetVehicle(), IsSoldier()) < GET_UNACCESSIBLE;
    veh->PerformLock();
    return ret;
}

bool AIUnit::FindNearestEmpty(Vector3& pos, bool soldier, EntityAI* veh)
{
    veh->PerformUnlock();

    float xEnd = pos.X();
    float zEnd = pos.Z();
    float xef = xEnd * InvOperItemGrid;
    float zef = zEnd * InvOperItemGrid;
    int xe = toIntFloor(xef);
    int ze = toIntFloor(zef);

    int xei = xe / OperItemRange;
    int zei = ze / OperItemRange;

    const int border = 1;

    OperMap map;
    map.CreateMap(veh, xei - border, zei - border, xei + border, zei + border);

    if (map.GetFieldCost(xe, ze, true, veh, soldier) >= GET_UNACCESSIBLE)
    {
        int oxe = xe, oze = ze;
        if (map.FindNearestEmpty(xe, ze, xef, zef, (xei - border) * OperItemRange, (zei - border) * OperItemRange,
                                 (1 + 2 * border) * OperItemRange, (1 + 2 * border) * OperItemRange, true, veh, soldier,
                                 DefFindFreePositionCallback, nullptr))
        {
            // xEnd, zEnd must be changed (is forced into last path point)
            if (xe != oxe || ze != oze)
            {
                pos[0] = xe * OperItemGrid + 0.5 * OperItemGrid;
                pos[2] = ze * OperItemGrid + 0.5 * OperItemGrid;
            }
        }
        else
        {
#if LOG_PROBL
            Log("Unit %s: End point not found", (const char*)veh->GetDebugName());
            map.LogMap(xei - border, xei + border, zei - border, zei + border, xe, xe, ze, ze, false);
#endif
            veh->PerformLock();
            return false;
        }
    }
    veh->PerformLock();
    return true;
}

bool AIUnit::FindNearestEmpty(Vector3& pos)
{
    return FindNearestEmpty(pos, IsSoldier(), GetVehicle());
}

bool AIUnit::CreateNoRoadPath(Vector3Val from, Vector3Val pos, const IPaths* fromBuilding, const IPaths* toBuilding)
{
    AI_ERROR(!fromBuilding || !toBuilding || fromBuilding != toBuilding);

    GetPath().SetSearchTime(Glob.time);

    const float maxDist = 300;
    float dist2 = (from - pos).SquareSizeXZ();

    if (dist2 > Square(maxDist))
    {
        LOG_DEBUG(AI, "Error: Unit {} is searching path per distance {:.0f}", (const char*)GetDebugName(), sqrt(dist2));
        ClearOperativePlan();
        return false;
    }

    EntityAI* veh = GetVehicle();
    float combatHeight = veh->GetCombatHeight();

    veh->PerformUnlock();

    Vector3 fromOutside = from;
    int fromBuildingFromPos = -1;
    int fromBuildingToPos = -1;
    if (fromBuilding)
    {
        Vector3 dummy;
        fromBuildingFromPos = fromBuilding->FindNearestPoint(from - Vector3(0, combatHeight, 0), dummy);
        if (fromBuildingFromPos < 0)
        {
            LOG_DEBUG(AI, "Unit {}: From building from point not found", (const char*)GetDebugName());
            veh->PerformLock();
            return false; // no point
        }
        fromBuildingToPos = fromBuilding->FindNearestExit(pos, fromOutside);
        if (fromBuildingToPos < 0)
        {
            LOG_DEBUG(AI, "Unit {}: From building exit not found", (const char*)GetDebugName());
            veh->PerformLock();
            return false; // no exit
        }
    }
    Vector3 toOutside = pos;
    int toBuildingFromPos = -1;
    int toBuildingToPos = -1;
    if (toBuilding)
    {
        Vector3 dummy;
        toBuildingToPos = _house ? toBuilding->GetPos(_housePos) : toBuilding->FindNearestPoint(pos, dummy);
        if (toBuildingToPos < 0)
        {
            LOG_DEBUG(AI, "Unit {}: To building to position not found", (const char*)GetDebugName());
            veh->PerformLock();
            return false; // no point
        }
        toBuildingFromPos = toBuilding->FindNearestExit(from, toOutside);
        if (toBuildingFromPos < 0)
        {
            LOG_DEBUG(AI, "Unit {}: To building exit not found", (const char*)GetDebugName());
            veh->PerformLock();
            return false; // no exit
        }
    }

    float xStart = fromOutside.X();
    float zStart = fromOutside.Z();
    float xEnd = toOutside.X();
    float zEnd = toOutside.Z();

    float xsf = xStart * InvOperItemGrid;
    float zsf = zStart * InvOperItemGrid;
    int xs = toIntFloor(xsf);
    int zs = toIntFloor(zsf);

    float xef = xEnd * InvOperItemGrid;
    float zef = zEnd * InvOperItemGrid;
    int xe = toIntFloor(xef);
    int ze = toIntFloor(zef);

    int xMin = xs / OperItemRange;
    int zMin = zs / OperItemRange;
    int xMax = xe / OperItemRange;
    int zMax = ze / OperItemRange;
    if (xMax < xMin)
    {
        swap(xMax, xMin);
    }
    if (zMax < zMin)
    {
        swap(zMax, zMin);
    }

#if FIELD_ON_DEMAND
    const int border = 0;
#else
    const int border = 1;
#endif

    OperMap map;
    map.CreateMap(veh, xMin - border, zMin - border, xMax + border, zMax + border);

    if (map.GetFieldCost(xs, zs, true, veh, IsSoldier()) >= GET_UNACCESSIBLE)
    {
        if (map.FindNearestEmpty(xs, zs, xsf, zsf, (xMin - border) * OperItemRange, (zMin - border) * OperItemRange,
                                 (xMax - xMin + 1 + 2 * border) * OperItemRange,
                                 (zMax - zMin + 1 + 2 * border) * OperItemRange, true, veh, IsSoldier(),
                                 DefFindFreePositionCallback, nullptr))
        {
        }
        else
        {
            LOG_DEBUG(AI, "Unit {}: Start point not found", (const char*)GetDebugName());
#if LOG_PROBL
            map.LogMap(xMin - border, xMax + border, zMin - border, zMax + border, xs, xe, zs, ze, false);
#endif
            veh->PerformLock();
            return false;
        }
    }

    if (map.GetFieldCost(xe, ze, true, veh, IsSoldier()) >= GET_UNACCESSIBLE)
    {
        if (map.FindNearestEmpty(xe, ze, xef, zef, (xMin - border) * OperItemRange, (zMin - border) * OperItemRange,
                                 (xMax - xMin + 1 + 2 * border) * OperItemRange,
                                 (zMax - zMin + 1 + 2 * border) * OperItemRange, true, veh, IsSoldier(),
                                 DefFindFreePositionCallback, nullptr))
        {
        }
        else
        {
            LOG_DEBUG(AI, "Unit {}: End point not found", (const char*)GetDebugName());
#if LOG_PROBL
            map.LogMap(xMin - border, xMax + border, zMin - border, zMax + border, xs, xe, zs, ze, false);
#endif
            veh->PerformLock();
            return false;
        }
    }

    int dir = 0, origSize = 0;

    if (xe == xs && ze == zs)
    {
        // trivial path
        map._path.Resize(2);
        map._path[0]._x = xs;
        map._path[0]._z = zs;
        map._path[0]._cost = 0;
        map._path[0].house = nullptr;
        map._path[0].from = -1;
        map._path[0].to = -1;
        int x = toIntFloor(fromOutside.X() * InvLandGrid);
        int z = toIntFloor(fromOutside.Z() * InvLandGrid);
        GeographyInfo info = GLOB_LAND->GetGeography(x, z);
        map._path[1]._x = xe;
        map._path[1]._z = ze;
        map._path[1]._cost = veh->GetCost(info) * (toOutside - fromOutside).SizeXZ();
        map._path[1].house = nullptr;
        map._path[1].from = -1;
        map._path[1].to = -1;

        goto PrefixPostfix;
    }

    {
        if (GetVehicleIn() && GetVehicleIn()->GetMoveMode() == VMMBackward)
        {
            dir = CalcDirection(-Direction());
        }
        else
        {
            dir = CalcDirection(Direction());
        }

        if (xs == 3092 && zs == 1309 && xe == 3099 && ze == 1290 && dir == 0)
        {
            LOG_DEBUG(AI, "Here.");
        }
        bool result = map.FindPath(this, dir, xs, zs, xe, ze, true);

        origSize = map._path.Size();
        if (!result)
        {
            LOG_DEBUG(AI, "Unit {}: Operative path not found", (const char*)GetDebugName());
#if LOG_PROBL
            map.LogMap(xMin - border, xMax + border, zMin - border, zMax + border, xs, xe, zs, ze, false);
#endif
            veh->PerformLock();
            return false;
        }
    }

PrefixPostfix:
    if (fromBuilding)
    {
#if HOUSE_DIAGS
        LOG_DEBUG(AI, "{}: from {}", (const char*)GetDebugName(),
                  (const char*)fromBuilding->GetObject()->GetDebugName());
#endif
        map._path.Insert(0);
        AI_ERROR(map._path.Size() > 1);
        map._path[0]._x = toIntFloor(from.X() * InvOperItemGrid);
        map._path[0]._z = toIntFloor(from.Z() * InvOperItemGrid);
        map._path[0]._cost = 0;
        map._path[0].house = nullptr;
        map._path[0].from = -1;
        map._path[0].to = -1;
        map._path[1].house = const_cast<Object*>(fromBuilding->GetObject());
        map._path[1].from = fromBuildingFromPos;
        map._path[1].to = fromBuildingToPos;
    }
    if (toBuilding && !map.IsAlternateGoal())
    {
#if HOUSE_DIAGS
        LOG_DEBUG(AI, "{}: to {}", (const char*)GetDebugName(), (const char*)toBuilding->GetObject()->GetDebugName());
#endif
        int index = map._path.Add();
        AI_ERROR(map._path.Size() > 1);
        map._path[index]._x = toIntFloor(pos.X() * InvOperItemGrid);
        map._path[index]._z = toIntFloor(pos.Z() * InvOperItemGrid);
        map._path[index]._cost = map._path[index - 1]._cost;
        map._path[index].house = const_cast<Object*>(toBuilding->GetObject());
        map._path[index].from = toBuildingFromPos;
        map._path[index].to = toBuildingToPos;
    }

    int maxIndex = map.ResultPath(this);
    if (_lastPlan)
    {
        maxIndex = _path.Size();
    }

    _path.SetOperIndex(1);
    _path.SetMaxIndex(maxIndex);
    _path.SetOnRoad(false);

    if (_path.Size() < 2)
    {
        RptF("Error %s: Invalid path from [%.2f, %.2f, %.2f] (building %x) to [%.2f, %.2f, %.2f] (building %x).",
             (const char*)GetDebugName(), from.X(), from.Y(), from.Z(), fromBuilding, pos.X(), pos.Y(), pos.Z(),
             toBuilding);
        RptF(" - from [%d, %d] dir %d to [%d, %d] - original size %d", xs, zs, dir, xe, ze, origSize);
    }

    veh->PerformLock();

    if (!VerifyPath())
    {
        Fail("Path not verified.");
    }

    /**/
    _path[0]._pos = GLandscape->PointOnSurface(from[0], combatHeight, from[2]);

    if (!map.IsAlternateGoal())
    {
        int last = _path.Size() - 1;
        if (!_path[last]._house)
        {
            _path[last]._pos = GLandscape->PointOnSurface(pos[0], combatHeight, pos[2]);
        }
    }
    /**/

    return true;
}

bool AIUnit::CreateRoadPath(Vector3Val from, Vector3Val to)
{
    GetPath().SetSearchTime(Glob.time);

    EntityAI* veh = GetVehicle();

    veh->PerformUnlock();
    float precision = veh->GetPrecision();
    float combatHeight = veh->GetCombatHeight();
#if ROAD_DIAGS
    LOG_DEBUG(AI, "{}: Searching road path with precision {:.1f}", (const char*)GetDebugName(), precision);
#endif

    RoadPathArray roadPath; // StaticArray
    static StaticStorage<Vector3> vectorStorage;
    roadPath.SetStorage(vectorStorage.Init(128));

    if (!GRoadNet->SearchPath(from, to, roadPath, 1.25 * precision))
    {
#if ROAD_DIAGS
        LOG_DEBUG(AI, "  Road path not found");
#endif
        veh->PerformLock();
        return false;
    }
#if ROAD_DIAGS
    LOG_DEBUG(AI, "  Road path found");
#endif

    veh->PerformLock();

    int n = roadPath.Size();
    if (n == 1)
    {
        // trivial path
        _path.Resize(2);
        _path[0]._pos = GLandscape->PointOnSurface(from[0], combatHeight, from[2]);
        _path[0]._cost = 0;

        _path[1]._pos = GLandscape->PointOnSurface(to[0], combatHeight, to[2]);
        int x = toIntFloor(from.X() * InvLandGrid);
        int z = toIntFloor(from.Z() * InvLandGrid);
        GeographyInfo info = GLOB_LAND->GetGeography(x, z);
        _path[1]._cost = veh->GetCost(info) * (to - from).SizeXZ();

        _path.SetOperIndex(1);
        _path.SetMaxIndex(2);
        _path.SetOnRoad(false);
        return true;
    }
    AI_ERROR(n >= 2);

    _path.Resize(n);
    int maxIndex = -1;
    for (int i = 0; i < n; i++)
    {
        // update cost info if necessary
        Vector3 lastPos = GLandscape->PointOnSurface(roadPath[i][0], combatHeight, roadPath[i][2]);
        _path[i]._pos = lastPos;
        _path[i]._cost = 0;
        float actDist2 = (lastPos - from).SquareSizeXZ();
        if (maxIndex < 0 && i > 1 && actDist2 >= Square(DIST_MAX_OPER))
        {
            maxIndex = i;
        }
    }
    veh->FillPathCost(_path);

    _path.SetOperIndex(1);
    if (maxIndex < 0)
    {
        maxIndex = _path.Size();
    }
    _path.SetMaxIndex(maxIndex);
    if (_lastPlan)
    {
        _path.SetMaxIndex(_path.Size());
    }

    _path.SetOnRoad(true);

    return true;
}

#define HEALTH_LOW 0.6F
#define HEALTH_CRITICAL 0.5F

#define ARMOR_LOW 0.6F
#define ARMOR_CRITICAL 0.5F

#define FUEL_LOW 0.3F
#define FUEL_CRITICAL 0.1F

#define AMMO_LOW 0.4F
#define AMMO_CRITICAL 0.2F

AIUnit::ResourceState AIUnit::GetFuelState() const
{
    if (IsSoldier())
    {
        return RSNormal;
    }

    float curFuel = GetVehicle()->GetFuel();
    float maxFuel = GetVehicle()->GetType()->GetFuelCapacity();

    if (curFuel < FUEL_CRITICAL * maxFuel)
    {
        return RSCritical;
    }
    if (curFuel < FUEL_LOW * maxFuel)
    {
        return RSLow;
    }
    return RSNormal;
}

AIUnit::ResourceState AIUnit::GetHealthState() const
{
    float health = 1.0 - GetPerson()->NeedsAmbulance();

    if (health < HEALTH_CRITICAL)
    {
        return RSCritical;
    }
    if (health < HEALTH_LOW)
    {
        return RSLow;
    }
    return RSNormal;
}

AIUnit::ResourceState AIUnit::GetArmorState() const
{
    if (IsSoldier())
    {
        return RSNormal;
    }

    float armor = 1.0 - GetVehicle()->NeedsRepair();

    if (armor < ARMOR_CRITICAL)
    {
        return RSCritical;
    }
    if (armor < ARMOR_LOW)
    {
        return RSLow;
    }
    return RSNormal;
}

AIUnit::ResourceState AIUnit::GetAmmoState() const
{
    if (IsFreeSoldier())
    {
        float needs = GetPerson()->NeedsInfantryRearm();
        if (needs > 1 - AMMO_CRITICAL)
        {
            return RSCritical;
        }
        if (needs > 1 - AMMO_LOW)
        {
            return RSLow;
        }
        return RSNormal;
    }

    // used for soldiers in vehicle - ammo state of vehicle
    if (!IsUnit())
    {
        return RSNormal;
    }

    float curAmmo = 0;
    float maxAmmo = 0;
    EntityAI* veh = GetVehicle();
    for (int i = 0; i < veh->NMagazines(); i++)
    {
        Magazine* magazine = veh->GetMagazine(i);
        if (!magazine)
        {
            continue;
        }
        curAmmo += magazine->_ammo;
        maxAmmo += magazine->_type->_maxAmmo;
    }

    if (curAmmo < AMMO_CRITICAL * maxAmmo)
    {
        return RSCritical;
    }
    if (curAmmo < AMMO_LOW * maxAmmo)
    {
        return RSLow;
    }
    return RSNormal;
}

void AIUnit::CheckAmmo(const MuzzleType*& muzzle1, const MuzzleType*& muzzle2, int& slots1, int& slots2, int& slots3)
{
    GetPerson()->CheckAmmo(muzzle1, muzzle2, slots1, slots2, slots3);
}

#define INFANTRY_AMMO_NEAR 50.0f
#define INFANTRY_AMMO_FAR 100.0f
#define COMMAND_TIMEOUT 480.0f // 8 min

void AIUnit::CheckAmmo()
{
    ResourceState state = GetAmmoState();
    if (state == RSNormal)
    {
        return;
    }

    const AITargetInfo* target = CheckAmmo(state);

    if (target)
    {
        bool channelCenter = false;
        EntityAI* veh = target->_idExact;
        if (veh)
        {
            AIUnit* unit = veh->CommanderUnit();
            if (unit && unit->GetLifeState() == AIUnit::LSAlive)
            {
                channelCenter = unit->GetGroup() != GetGroup();
            }
        }

        Command cmd;
        cmd._message = Command::Rearm;
        cmd._destination = target->_realPos;
        cmd._target = target->_idExact;
        cmd._time = Glob.time + COMMAND_TIMEOUT;
        GetGroup()->SendAutoCommandToUnit(cmd, this, true, channelCenter);
    }
}

const AITargetInfo* AIUnit::CheckAmmo(ResourceState state)
{
    float maxDist;
    if (state == RSCritical)
    {
        maxDist = INFANTRY_AMMO_FAR;
    }
    else
    {
        maxDist = INFANTRY_AMMO_NEAR;
    }
    float maxDist2 = Square(maxDist);

    // which magazines we needed
    const MuzzleType* muzzle1 = nullptr;
    const MuzzleType* muzzle2 = nullptr;
    int slots1 = 0, slots2 = 0, slots3 = 0;
    CheckAmmo(muzzle1, muzzle2, slots1, slots2, slots3);
    if (slots1 == 0 && slots2 == 0 && slots3 == 0)
    {
        return nullptr; // no empty slots
    }

    // check candidates
    float maxCoef = 0;
    const AITargetInfo* target = nullptr;

    for (int i = 0; i < GetGroup()->GetCenter()->NTargets(); i++)
    {
        const AITargetInfo& info = GetGroup()->GetCenter()->GetTarget(i);
        VehicleSupply* veh = dyn_cast<VehicleSupply, Object>(info._idExact);
        if (!veh)
        {
            continue;
        }
        float dist2 = Position().DistanceXZ2(info._realPos);
        if (dist2 > maxDist2)
        {
            continue;
        }
        saturateMax(dist2, 1);
        float coef = 0;
        if (info._type->IsKindOf(GWorld->Preloaded(VTypeMan)))
        {
            if (!veh->IsDammageDestroyed())
            {
                continue;
            }
            // check magazines
            int slots1Wanted = slots1, slots2Wanted = slots2, slots3Wanted = slots3;
            for (int j = 0; j < veh->NMagazines(); j++)
            {
                const Magazine* magazine = veh->GetMagazine(j);
                if (!magazine)
                {
                    continue;
                }
                if (magazine->_ammo == 0)
                {
                    continue;
                }
                const MagazineType* type = magazine->_type;
                int slots = GetItemSlotsCount(type->_magazineType);
                if (muzzle1)
                {
                    for (int k = 0; k < muzzle1->_magazines.Size(); k++)
                    {
                        if (muzzle1->_magazines[k] == type)
                        {
                            if (slots <= slots1Wanted)
                            {
                                slots1Wanted -= slots;
                                coef += 4.0 * slots;
                            }
                            goto NextMagazine;
                        }
                    }
                }
                if (muzzle2)
                {
                    for (int k = 0; k < muzzle2->_magazines.Size(); k++)
                    {
                        if (muzzle2->_magazines[k] == type)
                        {
                            if (slots <= slots2Wanted)
                            {
                                slots2Wanted -= slots;
                                coef += 2.0 * slots;
                            }
                            goto NextMagazine;
                        }
                    }
                }
                if (GetPerson()->IsMagazineUsable(type))
                {
                    if (slots <= slots3Wanted)
                    {
                        slots3Wanted -= slots;
                        coef += 1.0 * slots;
                    }
                }
            NextMagazine:;
            }
        }
        else
        {
            if (veh->IsDammageDestroyed())
            {
                continue;
            }
            // check magazine cargo
            int slots1Wanted = slots1, slots2Wanted = slots2, slots3Wanted = slots3;
            for (int j = 0; j < veh->GetMagazineCargoSize(); j++)
            {
                const Magazine* magazine = veh->GetMagazineCargo(j);
                if (!magazine)
                {
                    continue;
                }
                if (magazine->_ammo == 0)
                {
                    continue;
                }
                const MagazineType* type = magazine->_type;
                int slots = GetItemSlotsCount(type->_magazineType);
                if (muzzle1)
                {
                    for (int k = 0; k < muzzle1->_magazines.Size(); k++)
                    {
                        if (muzzle1->_magazines[k] == type)
                        {
                            if (slots <= slots1Wanted)
                            {
                                slots1Wanted -= slots;
                                coef += 4.0 * slots;
                            }
                            goto NextMagazineCargo;
                        }
                    }
                }
                if (muzzle2)
                {
                    for (int k = 0; k < muzzle2->_magazines.Size(); k++)
                    {
                        if (muzzle2->_magazines[k] == type)
                        {
                            if (slots <= slots2Wanted)
                            {
                                slots2Wanted -= slots;
                                coef += 2.0 * slots;
                            }
                            goto NextMagazineCargo;
                        }
                    }
                }
                if (GetPerson()->IsMagazineUsable(type))
                {
                    if (slots <= slots3Wanted)
                    {
                        slots3Wanted -= slots;
                        coef += 1.0 * slots;
                    }
                }
            NextMagazineCargo:;
            }
        }
        coef *= InvSqrt(dist2);
        if (coef > maxCoef)
        {
            maxCoef = coef;
            target = &info;
        }
    }
    return target;
}

} // namespace Poseidon
