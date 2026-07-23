#include <Poseidon/Foundation/Framework/PoTime.hpp>
#include <Poseidon/Core/Application.hpp>

#include <Poseidon/AI/AI.hpp>
#include <Poseidon/AI/Path/AIDefs.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/World/Entities/Infantry/Person.hpp>

#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/Audio/DynSound.hpp>
#include <Poseidon/World/Detection/Detector.hpp>
#include <Poseidon/World/Entities/Weapons/Shots.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Random/randomGen.hpp>
#include <Poseidon/World/Scene/Camera/CamEffects.hpp>

#include <Poseidon/UI/Locale/Languages.hpp>

#include <Poseidon/Foundation/Algorithms/Qsort.hpp>

#include <Poseidon/AI/ArcadeTemplate.hpp>
#include <Poseidon/IO/Serialization/ParamArchive.hpp>

#include <Poseidon/Game/Commands/GameStateExt.hpp>

#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Game/Chat.hpp>

#include <Poseidon/Foundation/Enums/EnumNames.hpp>

#include <Poseidon/Game/UiActions.hpp>

#include <Poseidon/Foundation/Strings/Mbcs.hpp>
#include <Poseidon/Foundation/Strings/Bstring.hpp>
#include <Poseidon/Foundation/Strings/StrFormat.hpp>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Common/Global.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

extern SRef<EntityAI> GDummyVehicle;
bool IsEnemy(const AICenter* center, TargetSide side);
bool IsUnknown(const AICenter* center, TargetSide side);

namespace Poseidon
{
using namespace Foundation;
void EmptyDeadIdentities();
bool IsIdentityDead(RString);
} // namespace Poseidon

namespace Poseidon
{

#define FieldsPerCycle 10
#define GroupsPerCycle 1
#define TargetsPerCycle 8
#define EXPOSURE_COEF 50.0F
#define INV_EXPOSURE_COEF (1.0F / EXPOSURE_COEF)
#define LOG_TARGETS 0
#define DIRTY_ENEMY 0x01
#define DIRTY_UNKNOWN 0x10

const float AccuracyLast = 7200;   // how long we can remmember the information
const float ExposureTimeout = 450; // how long exposure persist when target is not visible

struct AITargetChange
{
    bool erase;
    bool enemy;
    TargetId idExact;
    const VehicleType* type;
    int x;
    int z;
    Time time;
};
} // namespace Poseidon

#include <Poseidon/Foundation/Containers/Array2D.hpp>

namespace Poseidon
{
class AIMap
{
  public:
    Array2D<BYTE> _dirty;
    AutoArray<BYTE> _dirtyRow;
    AutoArray<AITargetChange> _changesQueue;
    Array2D<AIThreat> _exposureEnemy;
    Array2D<AIThreat> _exposureUnknown;

    AIMap() { Init(); }
    void Init();
    void AddChange(bool erase, bool enemy, const AITargetInfo& info);
    bool ProcessChange();

  protected:
    void SetDirty(int x, int z, BYTE dirty);
};

void AICenter::BeginPreviewUnit(const ArcadeUnitInfo& info)
{
    _lastUpdateTime = Glob.time;

    if (_map)
    {
        _map->Init();
    }
    _row = 0;
    _column = 0;

    ArcadeTemplate t;

    _nSoldier = 0;
    _nWoman = 0;
    SetResources(0); // resources are not used

    // create groups
    for (int i = 0; i < TSideUnknown; i++)
    {
        _friends[i] = 1.0f;
    }

    AIGroup* grp = new AIGroup();
    AddGroup(grp);
    OLinkArray<AIUnit> inFormation;
    // create vehicles pass 1

    const ArcadeUnitInfo& uInfo = info;

    AIUnit* unit = CreateUnit(uInfo, t, false, grp);
    if (!unit)
    {
        return;
    }

    EntityAI* veh = unit->GetVehicle();
    GWorld->GetGameState()->VarSet("bis_buldozer_zero", GameValueExt(veh), true);

    AIUnit* playerUnit = veh->CommanderUnit();

    AIUnit* leaderUnit = playerUnit;

    if (grp->NUnits() == 0)
    {
        grp->RemoveFromCenter();
        return;
    }

    grp->SortUnits();
    grp->CalculateMaximalStrength();
    if (leaderUnit)
    {
        grp->SelectLeader(leaderUnit);
    }
    else
    {
        SelectLeader(grp);
        leaderUnit = grp->Leader();
        AI_ERROR(leaderUnit);
    }

    grp->MainSubgroup()->UpdateFormationCoef();

    if (playerUnit)
    {
        playerUnit->GetPerson()->GetInfo()._name = "Dummy Bummy";

        GWorld->SwitchCameraTo(playerUnit->GetVehicle(), CamInternal);
        GWorld->GetGameState()->VarSet("bis_buldozer_cursor", GameValueExt(playerUnit->GetVehicle()), true);
        GWorld->SetPlayerManual(true);
        GWorld->SwitchPlayerTo(playerUnit->GetPerson());
        GWorld->SetRealPlayer(playerUnit->GetPerson());
    }
}

} // namespace Poseidon
void AICenter::SelectLeader(AIGroup* grp)
{
    using namespace Poseidon;
    AIUnit* unit = grp->LeaderCandidate(nullptr);
    if (unit)
    {
        grp->SelectLeader(unit);
    }
}
namespace Poseidon
{

void AICenter::AddGroup(AIGroup* grp, int id)
{
    if (id == 0)
    { // attach new id
        id = 1;
        for (int i = 0; i < _groups.Size(); i++)
        {
            AIGroup* grp = _groups[i];
            if (!grp)
            {
                continue;
            }
            saturateMax(id, grp->_id + 1);
        }
    }
    _groups.Add(grp);
    grp->_center = this;
    grp->_id = id;
    grp->Init();
}

void AICenter::GroupRemoved(AIGroup* grp)
{
    AI_ERROR(grp->NUnits() == 0);
    AI_ERROR(grp->ID() > 0);
    for (int i = 0; i < NGroups(); i++)
    {
        if (grp == _groups[i])
        {
            _groups.Delete(i);
            return;
        }
    }
    Fail("Not found.");
}

float AICenter::RecalculateExposure(int x, int z, AIThreat& result, SideFunction func)
{
    // return value - change of exposure
    float change = 0;
    Threat exposure;

    Vector3 fieldPos(x * LandGrid + LandGrid * 0.5, 0, z * LandGrid + LandGrid * 0.5);
    fieldPos[1] = GLOB_LAND->SurfaceY(fieldPos[0], fieldPos[2]);
    saturateMax(fieldPos[1], 0); // zero - normal sea level
    fieldPos[1] += 3;

    for (int i = 0; i < _targets.Size(); i++)
    {
        AITargetInfo& info = _targets[i];
        if (!info._exposure)
        {
            continue;
        }
        if (info._vanished)
        {
            continue;
        }
        if (info._destroyed)
        {
            continue;
        }
        if (!func(this, info._side))
        {
            continue;
        }
        const VehicleType* type = info._type;
        // consider weapons of target
        if (type->NWeaponSystems() <= 0)
        {
            continue;
        }

        float maxRange = -FLT_MAX;
        for (int w = 0; w < type->NWeaponSystems(); w++)
        {
            const WeaponType* weapon = type->GetWeaponSystem(w);
            for (int i = 0; i < weapon->_muzzles.Size(); i++)
            {
                const MuzzleType* muzzle = weapon->_muzzles[i];
                const MagazineType* magazine = muzzle->_typicalMagazine;
                if (!magazine)
                {
                    continue;
                }
                for (int j = 0; j < magazine->_modes.Size(); j++)
                {
                    const AmmoType* ammo = magazine->_modes[j]->_ammo;
                    if (ammo)
                    {
                        saturateMax(maxRange, ammo->maxRange);
                    }
                }
            }
        }
        if (maxRange <= 0)
        {
            continue;
        }

        float dist2 = Square(info._x - x) + Square(info._z - z);
        dist2 *= Square(LandGrid);
        // check maxRange of vehicle
        if (dist2 <= Square(maxRange))
        {
            // check strategic visibility
            // here must be info._pos (not info._realPos)
            float dirCoef = info._dir.CosAngle(fieldPos - info._pos);
            float visibility = GLOB_LAND->VisibleStrategic(info._pos, fieldPos);
            if (visibility > 0.5)
            {
                exposure += info._type->GetStrategicThreat(dist2, visibility, dirCoef);
            }
            // check azimut
        }
    }

    GeographyInfo geog = GLandscape->GetGeography(x, z);
    // consider unit will be cover by objects
    float coverFactor = 1 - geog.u.howManyObjects * 0.3;
    exposure = exposure * coverFactor;
    int expInt;
    expInt = toInt(exposure[VSoft] * INV_EXPOSURE_COEF);
    saturate(expInt, 0, 255);
    change += EXPOSURE_COEF * abs(expInt - result.u.soft);
    result.u.soft = expInt;
    expInt = toInt(exposure[VArmor] * INV_EXPOSURE_COEF);
    saturate(expInt, 0, 255);
    change += EXPOSURE_COEF * abs(expInt - result.u.armor);
    result.u.armor = expInt;
    expInt = toInt(exposure[VAir] * INV_EXPOSURE_COEF);
    saturate(expInt, 0, 255);
    change += EXPOSURE_COEF * abs(expInt - result.u.air);
    result.u.air = expInt;

    return change;
}

void AICenter::UpdateField(int x, int z)
{
    float changeEnemy = 0;
    float changeUnknown = 0;
    BYTE& dirty = _map->_dirty(x, z);
    if (dirty & DIRTY_ENEMY)
    {
        changeEnemy = RecalculateExposure(x, z, _map->_exposureEnemy(x, z), ::IsEnemy);
    }
    if (dirty & DIRTY_UNKNOWN)
    {
        changeUnknown = RecalculateExposure(x, z, _map->_exposureUnknown(x, z), ::IsUnknown);
    }

    dirty = 0;

    if (changeEnemy > 0.5 || changeUnknown > 0.5)
    {
        int g;
        for (g = 0; g < NGroups(); g++)
        {
            AIGroup* grp = GetGroup(g);
            if (!grp)
            {
                continue;
            }
            for (int sg = 0; sg < grp->NSubgroups(); sg++)
            {
                AISubgroup* subgrp = grp->GetSubgroup(sg);
                if (!subgrp)
                {
                    continue;
                }
                subgrp->ExposureChanged(x, z, changeEnemy, changeEnemy + changeUnknown);
            }
        }
    }
}

void AICenter::UpdateMap()
{
    //  due to bus limitation it is not possible to check all fields
    //  for dirty flag in every simulation cycle
    //  (it took 10-15% for 20fps when there were no dirty fields)
    //  limit dirty flag checking is important when there are little dirty fields
    int i;
    int n = TargetsPerCycle;
    for (i = 0; i < n; i++)
    {
        _map->ProcessChange();
    }

    n = FieldsPerCycle;
    int nRows = 16;
    int nAll = LandRange * LandRange;

    // finish row
    if (_column != 0)
    {
        bool dirty = false;
        for (i = 0; i < _column; i++)
        {
            if (_map->_dirty(i, _row) != 0)
            {
                dirty = true;
            }
        }

        while (_column != 0)
        {
            if (n <= 0 || nAll <= 0)
            {
                goto EndOfUpdateMap;
            }
            if (_map->_dirty(_column, _row) != 0)
            {
                UpdateField(_column, _row);
                n--;
            }
            _column = (_column + 1) & (LandRangeMask);
            nAll--;
        }
        if (!dirty)
        {
            _map->_dirtyRow[_row] = 0;
        }
        _row = (_row + 1) & (LandRangeMask);
        nRows--;
    }

    while (true)
    {
        AI_ERROR(_column == 0);
        if (n <= 0 || nRows <= 0 || nAll <= 0)
        {
            goto EndOfUpdateMap;
        }

        while (_map->_dirtyRow[_row] == 0)
        {
            _row = (_row + 1) & (LandRangeMask);
            nAll -= LandRange;
            if (nAll <= 0)
            {
                goto EndOfUpdateMap;
            }
        }
        for (i = 0; i < LandRange; i++)
        {
            if (n <= 0 || nAll <= 0)
            {
                goto EndOfUpdateMap;
            }
            if (_map->_dirty(_column, _row) != 0)
            {
                UpdateField(_column, _row);
                n--;
            }
            _column = (_column + 1) & (LandRangeMask);
            nAll--;
        }
        _map->_dirtyRow[_row] = 0;
        _row = (_row + 1) & (LandRangeMask);
        nRows--;
    }

EndOfUpdateMap:;
}
} // namespace Poseidon

#include <Poseidon/Foundation/Framework/PoTime.hpp>

namespace Poseidon
{
void EmptyDeadIdentities();
bool IsIdentityDead(RString);
} // namespace Poseidon

namespace Poseidon
{
void AICenter::UpdateGroup()
{
    // maintain CPU load under certain limit
    // get current time (QueryPerformanceCounter)
    Foundation::SectionTimeHandle sectionTime = Foundation::StartSectionTime();
    float maxTimePerCenter = 0.020;

    for (int i = 0; i < GroupsPerCycle;)
    {
        // search for oldest non-updated group
        float maxAge = 0;
        AIGroup* maxGrp = nullptr;
        for (int j = 0; j < NGroups(); j++)
        {
            AIGroup* grp = GetGroup(j);
            if (!grp)
            {
                continue;
            }
            float age = _lastUpdateTime - grp->_lastUpdateTime;
            if (maxAge < age)
            {
                maxAge = age;
                maxGrp = grp;
            }
        }
        if (!maxGrp)
        {
            // Not found
            _lastUpdateTime = Glob.time;
            break; // no more group to update
        }
        bool think = maxGrp->Think();

        if (think)
        {
            i++;
        }
        maxGrp->_lastUpdateTime = _lastUpdateTime;
        // guarantee no center takes too much CPU time
        if (Foundation::CompareSectionTimeGE(sectionTime, maxTimePerCenter))
        {
            break;
        }
    }
}

void AICenter::Think()
{
    PoseidonAssert(AssertValid());

    if (_side == TLogic)
    {
        UpdateGroup();
        PoseidonAssert(AssertValid());
        return;
    }

    // Update database - remove targets older than AccuracyLast
    for (int i = 0; i < _targets.Size();)
    {
        AITargetInfo& info = _targets[i];
        if (info._exposure)
        {
            i++;
            continue; // cannot delete
        }

        if (Glob.time > info._time + AccuracyLast)
        {
            _targets.Delete(i);
        }
        else
        {
            i++;
        }
    }

    RemoveOldExposures();
    UpdateMap();
    if (Glob.time >= _guardingValid)
    {
        UpdateGuarding();
    }
    if (Glob.time >= _supportValid)
    {
        UpdateSupport();
    }
    UpdateGroup();
    AddNewExposures();

    PoseidonAssert(AssertValid());
}

void AICenter::SendMission(AIGroup* to, Mission& mis)
{
    AI_ERROR(to);

#if LOG_COMM
    Log("Send Mission: Group %s: Mission %d", (const char*)to->GetDebugName(), mis._action);
#endif

    to->ReceiveMission(mis);
}

void AICenter::ReceiveAnswer(AIGroup* from, Answer answer)
{
    if (!from)
    {
        return;
    }

#if LOG_COMM
    Log("Receive answer: Group %s: Answer %d", (const char*)from->GetDebugName(), answer);
#endif

    switch (answer)
    {
        case AI::MissionCompleted:
        case AI::WorkCompleted:
        {
            AIUnit* leader = from->Leader();
            if (leader)
            {
                leader->AddExp(ExperienceMissionCompleted);
            }
        }
        break;
        case AI::MissionFailed:
        case AI::WorkFailed:
        {
            AIUnit* leader = from->Leader();
            if (leader)
            {
                leader->AddExp(ExperienceMissionFailed);
            }
        }
        break;
    }
}

#if LOG_COMM
static const char* sideNames[] = {"East", "West", "Guerrila", "Civilian", "Unknown"};
#endif

void AICenter::RemoveOldExposures()
{
    for (int i = 0; i < _targets.Size(); i++)
    {
        AITargetInfo& info = _targets[i];
        if (!info._exposure)
        {
            continue;
        }

        if (Glob.time > info._time + ExposureTimeout)
        {
            if (info._side == TSideUnknown)
            {
                _map->AddChange(true, false, info);
            }
            else if (IsEnemy(info._side))
            {
                _map->AddChange(true, true, info);
            }
            else
                Fail("Illegal exposure");
            info._exposure = false;
        }
    }
}

void AICenter::AddNewExposures()
{
    for (int i = 0; i < _targets.Size(); i++)
    {
        AITargetInfo& info = _targets[i];
        if (info._exposure)
        {
            continue;
        }
        if (info._vanished)
        {
            continue;
        }
        if (info._destroyed)
        {
            continue;
        }

        if (Glob.time <= info._time + ExposureTimeout)
        {
            if (info._side == TSideUnknown)
            {
                _map->AddChange(false, false, info);
                info._exposure = true;
            }
            else if (IsEnemy(info._side))
            {
                _map->AddChange(false, true, info);
                info._exposure = true;
            }
        }
    }
}

void AICenter::InitTarget(EntityAI* veh, float age)
{
    // InitTarget can be called only once for every target
    // age is in seconds
    int index = FindTargetIndex(veh);
    AI_ERROR(index < 0);
    if (index >= 0)
    {
        return;
    }

    index = _targets.Add();
    AITargetInfo& item = _targets[index];
    item._idExact = veh;
    // nasty trick: avoid detecting as empty
    item._side = veh->EntityAI::GetTargetSide();
    item._type = veh->GetType();
    item._realPos = veh->Position();
    item._pos = veh->Position();
    item._x = toIntFloor(item._pos.X() * InvLandGrid);
    item._z = toIntFloor(item._pos.Z() * InvLandGrid);
    item._accuracySide = 1.0;
    item._timeSide = Glob.time - age;
    item._accuracyType = 1.0;
    item._timeType = Glob.time - age;
    item._time = Glob.time - age;
    item._exposure = false;
    if (age <= 5 * 60)
    {
        item._dir = veh->Direction();
    }

#if LOG_TARGETS
    LOG_DEBUG(AI, "{} target inserted (init) {} (side {}, age {:.0f})", (const char*)GetDebugName(),
              (const char*)item._idExact->GetDebugName(), item._side, age);
#endif
}

void AICenter::DeleteTarget(TargetType* id)
{
    int index = FindTargetIndex(id);
    if (index < 0)
    {
        return;
    }

    AITargetInfo& item = _targets[index];
    AI_ERROR(item._idExact == id);

#if LOG_TARGETS
    LOG_DEBUG(AI, "{} target deleted {} (side {})", (const char*)GetDebugName(), (const char*)id->GetDebugName(),
              item._side);
#endif

    if (item._exposure)
    {
        if (item._side == TSideUnknown)
        {
            _map->AddChange(true, false, item);
        }
        else if (IsEnemy(item._side))
        {
            _map->AddChange(true, true, item);
        }
        else
            Fail("Illegal exposure");
    }
    _targets.Delete(index);
}

void AICenter::UpdateTarget(Target& target)
{
    int index = FindTargetIndex(target.idExact);
    if (index >= 0)
    {
        AITargetInfo& item = _targets[index];
        AI_ERROR(item._idExact == target.idExact);
        float oldAccuracyType = item.FadingTypeAccuracy();
        float oldAccuracySide = item.FadingSideAccuracy();
        float newAccuracyType = target.FadingAccuracy();
        float newAccuracySide = target.FadingSideAccuracy();
        TargetSide newSide;

        bool change = false;
        if (newAccuracySide >= floatMin(oldAccuracySide, 2.1))
        {
            newSide = target.side;
            item._timeSide = Glob.time;
            item._accuracySide = newAccuracySide;
            change = true;
        }
        else
        {
            newSide = item._side;
        }
        const VehicleType* newType;
        if (newAccuracyType > oldAccuracyType)
        {
            newType = target.type;
            item._timeType = Glob.time;
            item._accuracyType = newAccuracyType;
            change = true;
        }
        else
        {
            newType = item._type;
        }

        float errSize2 = target.posError.SquareSize();
        if (target.lastSeen > item._time && errSize2 <= item._precisionPos)
        { // change only if target information is more recent
            item._time = target.lastSeen;
            item._realPos = target.AimingPosition();
            item._precisionPos = target.posError.SquareSize();
            change = true;
        }

        bool forceUpdate = false;
        if (errSize2 <= Square(2)) // we see it very precisely
        {
            Vector3Val dir = target.idExact->GetEyeDirection();
            // limitDist = 2 * sin(alfa/2)
            // for alfa = 45 deg limitDist = 0.76536686473
            const float limitDist = 0.76;
            if (dir.Distance2(item._dir) > Square(limitDist))
            {
                item._dir = dir;
                change = true;
                forceUpdate = true;
            }
        }

        if (target.vanished != item._vanished)
        {
            item._vanished = target.vanished;
            forceUpdate = true;
        }

        if (target.destroyed != item._destroyed)
        {
            item._destroyed = target.destroyed;
            newSide = target.side;
            forceUpdate = true;
        }

        if (change) // some change in target information
        {
            int x = toIntFloor(item._realPos.X() * InvLandGrid);
            int z = toIntFloor(item._realPos.Z() * InvLandGrid);
            if (forceUpdate || item._x != x || item._z != z || item._side != newSide || item._type != newType)
            {
                if (item._exposure)
                {
                    if (item._side == TSideUnknown)
                    {
                        _map->AddChange(true, false, item);
                    }
                    else if (IsEnemy(item._side))
                    {
                        _map->AddChange(true, true, item);
                    }
                    else
                        Fail("Illegal exposure");
                    item._exposure = false;
                }
                item._pos = item._realPos;
                item._x = x;
                item._z = z;
                item._type = newType;
                item._side = newSide;
            }
        }
    }
    else
    {
        // target not found
        index = _targets.Add();
        AITargetInfo& item = _targets[index];
        item._idExact = target.idExact;
        item._side = target.side;
        item._type = target.type;
        item._realPos = target.position;
        item._precisionPos = target.posError.SquareSize();
        item._pos = target.position;
        item._x = toIntFloor(item._pos.X() * InvLandGrid);
        item._z = toIntFloor(item._pos.Z() * InvLandGrid);
        item._accuracySide = target.FadingSideAccuracy();
        item._timeSide = Glob.time;
        item._accuracyType = target.FadingAccuracy();
        item._timeType = Glob.time;
        item._time = target.lastSeen;
        item._exposure = false;

#if LOG_TARGETS
        LOG_DEBUG(AI, "{} target inserted (update) {} (side {})", (const char*)GetDebugName(),
                  (const char*)target.idExact->GetDebugName(), item._side);
#endif
    }
}

void AICenter::ReceiveReport(AIGroup* from, ReportSubject subject, Target& target)
{
    UpdateTarget(target);
}

float AICenter::GetExposurePessimistic(int x, int z) const
{
    if (!_map)
    {
        return 0;
    }
    if (!InRange(x, z))
    {
        return 0;
    }
    AIThreat& expE = _map->_exposureEnemy(x, z);
    AIThreat& expU = _map->_exposureUnknown(x, z);
    int exposureE = expE.u.soft + expE.u.armor + expE.u.air;
    int exposureU = expU.u.soft + expU.u.armor + expU.u.air;
    return EXPOSURE_COEF * 0.33 * (exposureE + exposureU);
}

float AICenter::GetExposureOptimistic(int x, int z) const
{
    if (!_map)
    {
        return 0;
    }
    if (!InRange(x, z))
    {
        return 0;
    }
    AIThreat& expE = _map->_exposureEnemy(x, z);
    int exposureE = expE.u.soft + expE.u.armor + expE.u.air;
    return EXPOSURE_COEF * 0.33 * exposureE;
}

float AICenter::GetExposureUnknown(int x, int z) const
{
    if (!_map)
    {
        return 0;
    }
    if (!InRange(x, z))
    {
        return 0;
    }
    AIThreat& expU = _map->_exposureUnknown(x, z);
    int exposureU = expU.u.soft + expU.u.armor + expU.u.air;
    return EXPOSURE_COEF * 0.33 * exposureU;
}

float AICenter::GetExposurePessimistic(Vector3Par pos) const
{
    return GetExposurePessimistic(toIntFloor(pos.X() * InvLandGrid), toIntFloor(pos.Z() * InvLandGrid));
}
float AICenter::GetExposureOptimistic(Vector3Par pos) const
{
    return GetExposureOptimistic(toIntFloor(pos.X() * InvLandGrid), toIntFloor(pos.Z() * InvLandGrid));
}
float AICenter::GetExposureUnknown(Vector3Par pos) const
{
    return GetExposureUnknown(toIntFloor(pos.X() * InvLandGrid), toIntFloor(pos.Z() * InvLandGrid));
}

RString AICenter::GetDebugName() const
{
    switch (_side)
    {
        case TEast:
            return "EAST";
        case TWest:
            return "WEST";
        case TGuerrila:
            return "GUER";
        case TCivilian:
            return "CIVL";
        case TLogic:
            return "LOGIC";
        default:
            Fail("Side");
            return nullptr;
    }
}

bool AICenter::AssertValid() const
{
    bool result = true;
    for (int i = 0; i < NGroups(); i++)
    {
        AIGroup* grp = GetGroup(i);
        if (!grp)
        {
            continue;
        }
        if (grp->GetCenter() == nullptr)
        {
            Fail("group with no center");
            result = false;
        }
        else if (grp->GetCenter() != this)
        {
            Fail("group with other center");
            result = false;
        }
        if (!grp->AssertValid())
        {
            result = false;
        }
    }
    return result;
}

void AICenter::Dump(int indent) const {}

ArcadeTemplate& GetCurrentTemplate()
{
    static ArcadeTemplate instance;
    return instance;
}

int GetTemplateSeed()
{
    return CurrentTemplate.randomSeed;
}

RString CurrentCampaign;
RString CurrentBattle;
RString CurrentMission;
RString CurrentWorld;
RString CurrentFile;
} // namespace Poseidon
