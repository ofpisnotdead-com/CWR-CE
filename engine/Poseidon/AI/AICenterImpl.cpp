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
#include <Poseidon/Network/NetworkCustomAssets.hpp>
#include <Poseidon/Game/Chat.hpp>

#include <Poseidon/Foundation/Enums/EnumNames.hpp>

#include <Poseidon/Game/UiActions.hpp>

#include <Poseidon/Foundation/Strings/Mbcs.hpp>
#include <Poseidon/Foundation/Strings/Bstring.hpp>
#include <Poseidon/Foundation/Strings/StrFormat.hpp>
#include <float.h>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Common/Global.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Memory/MemAlloc.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

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

void AICenter::SupportDone(AIGroup* grp)
{
    for (int i = 0; i < _supportTargets.Size(); i++)
    {
        if (_supportTargets[i].group == grp)
        {
            _supportTargets.Delete(i);
            break;
        }
    }

    for (int i = 0; i < _supportGroups.Size(); i++)
    {
        if (_supportGroups[i].assigned == grp)
        {
            _supportGroups[i].assigned = nullptr;
            _supportGroups[i].group->CancelSupport();
        }
    }
}

bool AICenter::IsSupported(AIGroup* grp, UIActionType type)
{
    for (int i = 0; i < _supportGroups.Size(); i++)
    {
        if (_supportGroups[i].assigned == grp && _supportGroups[i].group && _supportGroups[i].group->NUnits() > 0)
        {
            switch (type)
            {
                case ATHeal:
                    if (_supportGroups[i].heal)
                    {
                        return true;
                    }
                    break;
                case ATRepair:
                    if (_supportGroups[i].repair)
                    {
                        return true;
                    }
                    break;
                case ATRefuel:
                    if (_supportGroups[i].refuel)
                    {
                        return true;
                    }
                    break;
                case ATRearm:
                    if (_supportGroups[i].rearm)
                    {
                        return true;
                    }
                    break;
                case ATNone:
                    if (_supportGroups[i].heal)
                    {
                        return true;
                    }
                    if (_supportGroups[i].repair)
                    {
                        return true;
                    }
                    if (_supportGroups[i].refuel)
                    {
                        return true;
                    }
                    if (_supportGroups[i].rearm)
                    {
                        return true;
                    }
                    break;
            }
        }
    }
    return false;
}

bool AICenter::WaitingForSupport(AIGroup* grp, UIActionType type)
{
    for (int i = 0; i < _supportTargets.Size(); i++)
    {
        if (_supportTargets[i].group == grp)
        {
            switch (type)
            {
                case ATHeal:
                    if (_supportTargets[i].heal)
                    {
                        return true;
                    }
                    break;
                case ATRepair:
                    if (_supportTargets[i].repair)
                    {
                        return true;
                    }
                    break;
                case ATRefuel:
                    if (_supportTargets[i].refuel)
                    {
                        return true;
                    }
                    break;
                case ATRearm:
                    if (_supportTargets[i].rearm)
                    {
                        return true;
                    }
                    break;
                case ATNone:
                    if (_supportTargets[i].heal)
                    {
                        return true;
                    }
                    if (_supportTargets[i].repair)
                    {
                        return true;
                    }
                    if (_supportTargets[i].refuel)
                    {
                        return true;
                    }
                    if (_supportTargets[i].rearm)
                    {
                        return true;
                    }
                    break;
            }
        }
    }
    return false;
}

bool AICenter::CanSupport(UIActionType type)
{
    for (int i = 0; i < _supportGroups.Size(); i++)
    {
        if (_supportGroups[i].group && _supportGroups[i].group->NUnits() > 0)
        {
            switch (type)
            {
                case ATHeal:
                    if (_supportGroups[i].heal)
                    {
                        return true;
                    }
                    break;
                case ATRepair:
                    if (_supportGroups[i].repair)
                    {
                        return true;
                    }
                    break;
                case ATRefuel:
                    if (_supportGroups[i].refuel)
                    {
                        return true;
                    }
                    break;
                case ATRearm:
                    if (_supportGroups[i].rearm)
                    {
                        return true;
                    }
                    break;
            }
        }
    }
    return false;
}

int CmpGuardedVehicles(const AITargetInfo* const* i1, const AITargetInfo* const* i2)
{
    const AITargetInfo* info1 = *i1;
    const AITargetInfo* info2 = *i2;
    if (info1->_side == TSideUnknown)
    {
        if (info2->_side != TSideUnknown)
        {
            return 1;
        }
    }
    else if (info2->_side == TSideUnknown)
    {
        return -1;
    }

    float diff = info2->_type->GetCost() - info1->_type->GetCost();
    if (diff != 0)
    {
        return sign(diff);
    }
    else
    {
        return info1->_idExact - info2->_idExact;
    }
}

void AICenter::UpdateGuarding()
{
    // algorithm for assigning groups to targets
    _guardingValid = Glob.time + 25.0f + 10.0f * GRandGen.RandomValue();

    // 1. remove (temporary) groups with no units
    int nGroups = 0;
    for (int i = 0; i < _guardingGroups.Size(); i++)
    {
        if (_guardingGroups[i].group && _guardingGroups[i].group->NUnits() > 0)
        {
            _guardingGroups[i].guarding = false;
            nGroups++;
        }
        else
        {
            _guardingGroups[i].guarding = true;
        }
    }
    if (nGroups == 0)
    {
        return;
    }

    // 2. reset points
    for (int i = 0; i < _guardedPoints.Size(); i++)
    {
        _guardedPoints[i].by = nullptr;
    }

    // 3. prepare list of guarded vehicles
    AutoArray<const AITargetInfo*> targets;
    for (int i = 0; i < NTargets(); i++)
    {
        const AITargetInfo& info = GetTarget(i);
        if (!info._idExact)
        {
            continue;
        }
        if (info._idExact->IsDammageDestroyed())
        {
            continue;
        }
        if (info._type->IsKindOf(GWorld->Preloaded(VTypeStatic)))
        {
            continue;
        }
        if (info._side == TSideUnknown)
        {
            targets.Add(&info);
        }
        else if (IsEnemy(info._side))
        {
            targets.Add(&info);
        }
    }

    // 4. sort list
    QSort(targets.Data(), targets.Size(), CmpGuardedVehicles);

    // 5. assign vehicles to groups
    int n = targets.Size();
    _guardedVehicles.Resize(n);
    for (int i = 0; i < n; i++)
    {
        AIGurdedVehicle& info = _guardedVehicles[i];
        info.vehicle = targets[i]->_idExact;
        info.by = nullptr;
        // try to cover instead of 1:1 assignment
        if (nGroups > 0)
        {
            float minDist2 = FLT_MAX;
            int jBest = -1;
            Vector3Val posD = targets[i]->_realPos;
            for (int j = 0; j < _guardingGroups.Size(); j++)
            {
                if (_guardingGroups[j].guarding)
                {
                    continue;
                }
                AIGroup* grp = _guardingGroups[j].group;
                AI_ERROR(grp);
                AI_ERROR(grp->Leader());
                Vector3Val posL = grp->Leader()->Position();
                float dist2 = (posD - posL).SquareSizeXZ();
                if (dist2 < minDist2)
                {
                    minDist2 = dist2;
                    jBest = j;
                }
            }
            AI_ERROR(jBest >= 0);
            info.by = _guardingGroups[jBest].group;
            _guardingGroups[jBest].guarding = true;
            nGroups--;
        }
    }

    // 6. assign points to groups
    for (int i = 0; i < _guardedPoints.Size() && nGroups > 0; i++)
    {
        AIGurdedPoint& info = _guardedPoints[i];
        float minDist2 = FLT_MAX;
        int jBest = -1;
        Vector3Val posD = info.position;
        for (int j = 0; j < _guardingGroups.Size(); j++)
        {
            if (_guardingGroups[j].guarding)
            {
                continue;
            }
            AIGroup* grp = _guardingGroups[j].group;
            AI_ERROR(grp);
            AI_ERROR(grp->Leader());
            Vector3Val posL = grp->Leader()->Position();
            float dist2 = (posD - posL).SquareSizeXZ();
            if (dist2 < minDist2)
            {
                minDist2 = dist2;
                jBest = j;
            }
        }
        AI_ERROR(jBest >= 0);
        info.by = _guardingGroups[jBest].group;
        _guardingGroups[jBest].guarding = true;
        nGroups--;
    }
}

AIGuardTarget AICenter::GetGuardTarget(AIGroup* grp)
{
    AIGuardTarget result;
    result.type = GTTNothing;

    AI_ERROR(grp);
    if (!grp)
    {
        return result;
    }

    int found = -1;
    for (int i = 0; i < _guardingGroups.Size();)
    {
        if (!_guardingGroups[i].group)
        {
            _guardingGroups.Delete(i);
            continue;
        }

        if (_guardingGroups[i].group == grp)
        {
            found = i;
            // continue - delete obsolete items
        }
        i++;
    }

    if (found < 0)
    {
        found = _guardingGroups.Add();
        _guardingGroups[found].group = grp;
        _guardingGroups[found].guarding = false;
        UpdateGuarding(); // for new group recalculate guarding immediately
    }

    if (_guardingGroups[found].guarding)
    {
        for (int i = 0; i < _guardedVehicles.Size(); i++)
        {
            if (_guardedVehicles[i].by == grp)
            {
                result.type = GTTVehicle;
                result.vehicle = _guardedVehicles[i].vehicle;
                return result;
            }
        }
        for (int i = 0; i < _guardedPoints.Size(); i++)
        {
            if (_guardedPoints[i].by == grp)
            {
                result.type = GTTPoint;
                result.position = _guardedPoints[i].position;
                return result;
            }
        }
    }
    return result;
}

int AICenter::AddGuardedPoint(Vector3Par pos)
{
    int index = _guardedPoints.Add();
    _guardedPoints[index].position = pos;
    return index;
}

int AICenter::FindTargetIndex(TargetType* id) const
{
    for (int i = 0; i < _targets.Size(); i++)
    {
        const AITargetInfo& item = _targets[i];
        if (id == item._idExact)
        {
            // target found
            return i;
        }
    }
    // target not found
    return -1;
}

const AITargetInfo* AICenter::FindTargetInfo(TargetType* id) const
{
    int index;
    index = FindTargetIndex(id);
    if (index < 0)
    {
        return nullptr;
    }
    return &_targets[index];
}

AITargetInfo* AICenter::FindTargetInfo(TargetType* id)
{
    int index;
    index = FindTargetIndex(id);
    if (index < 0)
    {
        return nullptr;
    }
    return &_targets[index];
}

void AICenter::InitPreview(const ArcadeUnitInfo& info)
{
    _lastUpdateTime = Glob.time;

    if (_map)
    {
        _map->Init();
    }
    _row = 0;
    _column = 0;
    BeginPreviewUnit(info);
}

void AICenter::Init(ArcadeTemplate& t, AutoArray<VehicleInitCmd, MemAllocSA>& inits)
{
    _lastUpdateTime = Glob.time;

    if (_map)
    {
        _map->Init();
    }
    _row = 0;
    _column = 0;

    BeginArcade(t, inits);

    PoseidonAssert(AssertValid());
}

void AICenter::InitSensors(bool initialize)
{
    for (int i = 0; i < NGroups(); i++)
    {
        AIGroup* grp = GetGroup(i);
        if (!grp)
        {
            continue;
        }
        grp->CreateTargetList(initialize, false);
        if (grp->NEnemiesDetected() > 0 && !grp->IsAnyPlayerGroup())
        {
            grp->ReactToEnemyDetected();
        }
    }

    if (initialize)
    {
        if (_map)
        {
            AddNewExposures();
            while (_map->ProcessChange())
            {
                ;
            }
        }
        // setup combat mode accordingly
    }
}

#define MIN_DIST2 50.0F * 50.0F
bool ValidPos(Vector3Val pos, AutoArray<Point3>& forbidden)
{
    int i;
    for (i = 0; i < forbidden.Size(); i++)
    {
        if ((forbidden[i] - pos).SquareSizeXZ() < MIN_DIST2)
        {
            return false;
        }
    }
    return true;
}

static Vector3 FindStartPosition(Vector3Val center, float radius, AutoArray<Point3>* avoid = nullptr)
{
    if (radius <= 0.1)
    {
        return center;
    }

    //  use paper car - it is cheap in terms of textures and memory
    //  it is defined so that it gives most constraints on position of all all vehicles
    Vector3 bestPoint = center;
    bool bestPointValid = false;
    int j;
    float minCost = 1e10;
    for (j = 0; j < 100; j++)
    {
        float xr = (2.0 * GRandGen.RandomValue() - 1.0) * radius;
        float zr = (2.0 * GRandGen.RandomValue() - 1.0) * radius;
        if (Square(xr) + Square(zr) > radius * radius)
        {
            continue;
        }
        Point3 pos;
        pos.Init();
        pos[0] = center[0] + xr;
        pos[1] = 0;
        pos[2] = center[2] + zr;
        int xtest = toIntFloor(pos.X() * InvLandGrid);
        int ztest = toIntFloor(pos.Z() * InvLandGrid);
        for (int x = -1; x <= 1; x++)
        {
            for (int z = -1; z <= 1; z++)
            {
                GeographyInfo info = GLOB_LAND->GetGeography(xtest + x, ztest + z);
                if (GDummyVehicle->GetCost(info) > 1e10)
                {
                    goto Break;
                }
            }
        }
        if (avoid && !ValidPos(pos, *avoid))
        {
            continue;
        }

        {
            GeographyInfo info = GLOB_LAND->GetGeography(xtest, ztest);
            float cost = GDummyVehicle->GetCost(info);
            if (cost < minCost)
            {
                minCost = cost;
                bestPoint = pos;
                bestPointValid = true;
            }
        }

    Break:
        continue;
    }
    if (!bestPointValid)
    {
        LOG_DEBUG(AI, "No valid start position.");
    }
    return bestPoint;
}

} // namespace Poseidon
Vector3 FindWaypointPosition(Vector3Par center, float radius)
{
    using namespace Poseidon;
    // use paper car - it is cheap in terms of textures and memory
    // it is defined so that it gives most constraints on position of all all vehicles
    Vector3 normal = VUp;
    for (int j = 0; j < 100; j++)
    {
        float xr = (2.0 * GRandGen.RandomValue() - 1.0) * radius;
        float zr = (2.0 * GRandGen.RandomValue() - 1.0) * radius;
        if (Square(xr) + Square(zr) > radius * radius)
        {
            continue;
        }
        Vector3 pos;
        pos.Init();
        pos[0] = center[0] + xr;
        pos[1] = 0;
        pos[2] = center[2] + zr;
        int xtest = toIntFloor(pos.X() * InvLandGrid);
        int ztest = toIntFloor(pos.Z() * InvLandGrid);
        for (int x = -1; x <= 1; x++)
        {
            for (int z = -1; z <= 1; z++)
            {
                GeographyInfo info = GLOB_LAND->GetGeography(xtest + x, ztest + z);
                if (GDummyVehicle->GetCost(info) >= 1e10)
                {
                    goto BadPosition;
                }
            }
        }

        if (AIUnit::FindFreePosition(pos, normal, false, GDummyVehicle))
        {
            return pos;
        }

    BadPosition:
        continue;
    }

    Vector3 pos = center;
    AIUnit::FindFreePosition(pos, normal, false, GDummyVehicle);
    return pos;
}
namespace Poseidon
{

OLinkArray<EntityAI> vehiclesMap;
OLinkArray<Vehicle> sensorsMap;
AutoArray<ArcadeMarkerInfo> markersMap;
AutoArray<SynchronizedItem> synchronized;

void SynchronizedItem::Add(AIGroup* grp)
{
    int index = groups.Add();
    groups[index].group = grp;
    groups[index].active = true;
}

void SynchronizedItem::Add(Vehicle* sensor)
{
    int index = sensors.Add();
    sensors[index].sensor = sensor;
    sensors[index].active = true;
}

void SynchronizedItem::SetActive(AIGroup* grp, bool active)
{
    int i, n = groups.Size();
    for (i = 0; i < n; i++)
    {
        if (groups[i].group == grp)
        {
            groups[i].active = active;
        }
    }
}

void SynchronizedItem::SetActive(Vehicle* sensor, bool active)
{
    int i, n = sensors.Size();
    for (i = 0; i < n; i++)
    {
        if (sensors[i].sensor == sensor)
        {
            sensors[i].active = active;
        }
    }
}

bool SynchronizedItem::IsActive(AIGroup* grp)
{
    // two items in one synchronization
    // return true <=> some is active
    int n = groups.Size();
    for (int i = 0; i < n; i++)
    {
        if (groups[i].group && groups[i].group != grp && groups[i].group->NUnits() > 0 && groups[i].active)
        {
            return true;
        }
    }
    n = sensors.Size();
    for (int i = 0; i < n; i++)
    {
        if (sensors[i].sensor && sensors[i].active)
        {
            return true;
        }
    }
    return false;
}

// Init / load / save
} // namespace Poseidon
void AIGlobalInit()
{
    using namespace Poseidon;
    synchronized.Clear();
    vehiclesMap.Clear();
    sensorsMap.Clear();
    markersMap.Clear();
}
namespace Poseidon
{

LSError SynchronizedGroup::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.SerializeRef("Group", group, 1))
    PARAM_CHECK(ar.Serialize("active", active, 1, false))
    return LSOK;
}

LSError SynchronizedSensor::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.SerializeRef("Sensor", sensor, 1))
    PARAM_CHECK(ar.Serialize("active", active, 1, false))
    return LSOK;
}

LSError SynchronizedItem::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.Serialize("Groups", groups, 1))
    PARAM_CHECK(ar.Serialize("Sensors", sensors, 1))
    return LSOK;
}

LSError AIGlobalSerialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.Serialize("Synchronized", synchronized, 1))
    PARAM_CHECK(ar.SerializeRefs("VehiclesMap", vehiclesMap, 1))
    PARAM_CHECK(ar.SerializeRefs("SensorsMap", sensorsMap, 1))
    PARAM_CHECK(ar.Serialize("Stats", GStats, 1))
    PARAM_CHECK(ar.Serialize("Markers", markersMap, 1))
    return LSOK;
}

const float MinHealth = 0.03;

static Vehicle* CreateSoundSource(ArcadeUnitInfo& info, ArcadeTemplate& t, bool multiplayer)
{
    RString sim = Pars >> "CfgVehicles" >> info.vehicle >> "sound";
    DynSoundSource* vehicle = new DynSoundSource(sim);
    Vector3 pos = info.position;
    int n = info.markers.Size();
    if (n > 0)
    { // randomized placement
        int i = toIntFloor((n + 1) * GRandGen.RandomValue());
        if (i < n)
        {
            RString name = info.markers[i];
            int m = t.markers.Size();
            for (int j = 0; j < m; j++)
            {
                ArcadeMarkerInfo& mInfo = t.markers[j];
                if (stricmp(mInfo.name, name) == 0)
                {
                    pos = mInfo.position;
                    break;
                }
            }
        }
    }
    float xr, zr;
    do
    {
        xr = (2.0 * GRandGen.RandomValue() - 1.0) * info.placement;
        zr = (2.0 * GRandGen.RandomValue() - 1.0) * info.placement;
    } while (Square(xr) + Square(zr) > info.placement * info.placement);
    pos[0] += xr;
    pos[2] += zr;
    pos[1] = GLOB_LAND->SurfaceYAboveWater(pos[0], pos[2]);
    vehicle->SetPosition(pos);
    GWorld->AddBuilding(vehicle);
    if (multiplayer)
    {
        GetNetworkManager().CreateVehicle(vehicle, VLTBuilding, "", -1);
    }
    return vehicle;
}

static Vehicle* CreateMine(ArcadeUnitInfo& info, ArcadeTemplate& t, bool multiplayer)
{
    RString typeName = Pars >> "CfgVehicles" >> info.vehicle >> "ammo";
    Ref<VehicleNonAIType> type = VehicleTypes.New(typeName);
    AmmoType* aType = dynamic_cast<AmmoType*>(type.GetRef());
    Vehicle* vehicle = NewShot(nullptr, aType, nullptr);
    Vector3 pos = info.position;
    int n = info.markers.Size();
    if (n > 0)
    { // randomized placement
        int i = toIntFloor((n + 1) * GRandGen.RandomValue());
        if (i < n)
        {
            RString name = info.markers[i];
            int m = t.markers.Size();
            for (int j = 0; j < m; j++)
            {
                ArcadeMarkerInfo& mInfo = t.markers[j];
                if (stricmp(mInfo.name, name) == 0)
                {
                    pos = mInfo.position;
                    break;
                }
            }
        }
    }
    float xr, zr;
    do
    {
        xr = (2.0 * GRandGen.RandomValue() - 1.0) * info.placement;
        zr = (2.0 * GRandGen.RandomValue() - 1.0) * info.placement;
    } while (Square(xr) + Square(zr) > info.placement * info.placement);
    pos[0] += xr;
    pos[2] += zr;
    pos[1] = GLOB_LAND->SurfaceYAboveWater(pos[0], pos[2]);
    vehicle->SetPosition(pos);
    GWorld->AddFastVehicle(vehicle);
    if (multiplayer)
    {
        GetNetworkManager().CreateVehicle(vehicle, VLTFast, "", -1);
    }
    return vehicle;
}

struct MagazineInfo
{
    const MagazineType* type;
    int n;
};

static EntityAI* CreateVehicle(const ArcadeUnitInfo& info, ArcadeTemplate& t, TargetSide side, bool multiplayer,
                               bool disableC, bool disableD, bool disableG)
{
    // avoid sounds
    RString vehClass;
    if (info.type)
    {
        vehClass = info.type->_simName;
    }
    else
    {
        vehClass = Pars >> "CfgVehicles" >> info.vehicle >> "vehicleClass";
    }
    if (stricmp(vehClass, "Sounds") == 0)
    {
        return nullptr;
    }

    Vector3 pos = info.position, normal;
    int n = info.markers.Size();
    if (n > 0)
    { // randomized placement
        int i = toIntFloor((n + 1) * GRandGen.RandomValue());
        if (i < n)
        {
            RString name = info.markers[i];
            int m = t.markers.Size();
            for (int j = 0; j < m; j++)
            {
                ArcadeMarkerInfo& mInfo = t.markers[j];
                if (stricmp(mInfo.name, name) == 0)
                {
                    pos = mInfo.position;
                    break;
                }
            }
        }
    }
    pos[1] = GLOB_LAND->RoadSurfaceY(pos[0], pos[2]);
    pos = FindStartPosition(pos, info.placement);
    float azimut = -HDegree(info.azimut);

    Ref<EntityAI> veh;
    if (info.type)
    {
        veh = NewVehicle(info.type);
    }
    else
    {
        veh = NewVehicle(info.vehicle);
    }
    if (!veh)
    {
        return nullptr;
    }

    Person* soldier = dyn_cast<Person>(veh.GetRef());
    if (soldier && disableD)
    {
        return nullptr;
    }

    if (side != TSideUnknown)
    {
        veh->SetTargetSide(side);
    }

    if (info.name.GetLength() > 0)
    {
        veh->SetVarName(info.name);
        GWorld->GetGameState()->VarSet(info.name, GameValueExt(veh.GetRef()), true);
    }

    // Add into World
    veh->SetDammage(1 - floatMax(MinHealth, info.health));
    veh->Refuel(info.fuel * veh->GetType()->GetFuelCapacity() - veh->GetFuel());

    // ammo
    AUTO_STATIC_ARRAY(MagazineInfo, infos, 32);
    for (int i = 0; i < veh->NMagazines(); i++)
    {
        Magazine* magazine = veh->GetMagazine(i);
        if (!magazine)
        {
            continue;
        }
        MagazineType* type = magazine->_type;
        if (type->_maxAmmo == 0)
        {
            continue;
        }
        int index = -1;
        for (int j = 0; j < infos.Size(); j++)
        {
            if (infos[j].type == type)
            {
                index = j;
                infos[j].n++;
                break;
            }
        }
        if (index < 0)
        {
            index = infos.Add();
            infos[index].type = type;
            infos[index].n = 1;
        }
    }
    for (int k = 0; k < infos.Size(); k++)
    {
        const MagazineType* type = infos[k].type;
        int ammo = toInt(info.ammo * infos[k].n * type->_maxAmmo);
        for (int i = 0; i < veh->NMagazines(); i++)
        {
            Magazine* magazine = veh->GetMagazine(i);
            if (!magazine)
            {
                continue;
            }
            if (magazine->_type != type)
            {
                continue;
            }
            if (ammo == 0)
            {
                veh->RemoveMagazine(magazine);
                i--;
            }
            else if (ammo >= type->_maxAmmo)
            {
                magazine->_ammo = type->_maxAmmo;
                ammo -= type->_maxAmmo;
            }
            else
            {
                magazine->_ammo = ammo;
                ammo = 0;
            }
        }
    }

    Matrix3 rotY(MRotationY, azimut);

    if (info.placement > 0 && !AIUnit::FindFreePosition(pos, normal, soldier != nullptr, veh))
    {
        Fail("Bad position");
        normal = VUp;
    }
    else
    {
        float dx, dz;
        pos[1] = GLOB_LAND->SurfaceYAboveWater(pos[0], pos[2], &dx, &dz);

        if (soldier)
        {
            // enable placing soldier onto object road
            pos[1] = GLOB_LAND->RoadSurfaceYAboveWater(pos[0], pos[2], &dx, &dz);
            normal = VUp;
        }
        else
        {
            normal = Vector3(-dx, 1, -dz);
        }
    }

    Matrix3 dir;
    Matrix4 transform;

    if (info.special == ASpFlying && veh->GetType()->IsKindOf(GWorld->Preloaded(VTypeAir)))
    {
        dir.SetUpAndDirection(VUp, VForward);
        transform.SetOrientation(dir * rotY);
        veh->SetTransform(transform);

        pos[1] += veh->MakeAirborne();
        normal = VUp;

        transform.SetPosition(pos);
    }
    else
    {
        transform.SetPosition(pos);
        dir.SetUpAndDirection(normal, VForward);
        transform.SetOrientation(dir * rotY);

        veh->PlaceOnSurface(transform);
    }

    veh->SetTransform(transform);
    veh->Init(transform);

    // if vehicle is static, add it to building list

    if (veh->GetType()->IsKindOf(GWorld->Preloaded(VTypeStatic)))
    {
        GWorld->AddBuilding(veh);
        if (multiplayer)
        {
            GetNetworkManager().CreateVehicle(veh, VLTBuilding, info.name, info.id);
        }
    }
    else
    {
        GWorld->AddVehicle(veh);
        if (multiplayer)
        {
            GetNetworkManager().CreateVehicle(veh, VLTVehicle, info.name, info.id);
        }
    }

    if (info.id >= vehiclesMap.Size())
    {
        vehiclesMap.Resize(info.id + 1);
    }
    vehiclesMap[info.id] = veh.GetRef();

    // Add into target list
    if (side != Glob.header.playerSide)
    {
        AICenter* center = GWorld->GetCenter((TargetSide)Glob.header.playerSide);
        if (center)
        {
            float time = 0;
            switch (info.age)
            {
                case AAActual:
                    time = 0;
                    break;
                case AA5Min:
                    time = 5 * 60;
                    break;
                case AA10Min:
                    time = 10 * 60;
                    break;
                case AA15Min:
                    time = 15 * 60;
                    break;
                case AA30Min:
                    time = 30 * 60;
                    break;
                case AA60Min:
                    time = 60 * 60;
                    break;
                case AA120Min:
                    time = 120 * 60;
                    break;
                case AAUnknown:
                    time = AccuracyLast;
                    break;
            }
            center->InitTarget(veh, time);
        }
    }

    Transport* transport = dyn_cast<Transport>(veh.GetRef());
    if (transport)
    {
        transport->SetLock(info.lock);

        // generate plate number from "regulations"
        const ParamEntry& world = Pars >> "CfgWorlds" >> Glob.header.worldname;
        RString format = world >> "plateFormat";
        RString letters = world >> "plateLetters";

        // Unsigned: this is a hash-style plate seed where wraparound is intended;
        // signed int*const overflows (UB) on large randomSeed values.
        int seed =
            static_cast<int>(static_cast<unsigned>(info.id) * 4586u + static_cast<unsigned>(t.randomSeed) * 871u);

        int plateId = toLargeInt(GRandGen.RandomValue(seed) * 0x1000000);

        int nLetters = letters.GetLength();

        char plate[256];
        char* p = plate;
        for (const char* f = format; *f; f++)
        {
            if (*f == '$')
            {
                *p++ = letters[plateId % nLetters];
                plateId /= nLetters;
            }
            else if (*f == '#')
            {
                *p++ = '0' + plateId % 10;
                plateId /= 10;
            }
            else
            {
                *p++ = *f;
            }
        }
        *p = 0;
        transport->SetPlateNumber(plate);
    }

    return veh;
}

Vehicle* CreateSensor(const ArcadeSensorInfo& info, AIGroup* grp, bool multiplayer)
{
    Vector3 pos = info.position, normal = VUp;
    pos[1] = GLOB_LAND->RoadSurfaceYAboveWater(pos[0], pos[2]);

    AICenter* center = nullptr;
    switch (info.type) // special cases - guarded by ...
    {
        case ASTEastGuarded:
            center = GWorld->GetEastCenter();
            goto Guarded;
        case ASTWestGuarded:
            center = GWorld->GetWestCenter();
            goto Guarded;
        case ASTGuerrilaGuarded:
            center = GWorld->GetGuerrilaCenter();
            goto Guarded;
        Guarded:
            if (!center)
            {
                return nullptr;
            }
            if (grp)
            {
                AI_ERROR(grp->Leader());
                if (grp->Leader())
                {
                    pos = grp->Leader()->Position();
                }
            }
            else
            {
                if (info.idStatic >= 0)
                {
                    Object* obj = GLandscape->FindObject(info.idStatic);
                    if (obj)
                    {
                        pos = obj->Position();
                    }
                }
                else
                {
                    if (info.idVehicle >= 0 && info.idVehicle < vehiclesMap.Size())
                    {
                        Vehicle* veh = vehiclesMap[info.idVehicle];
                        if (veh)
                        {
                            pos = veh->Position();
                        }
                    }
                }
            }
            {
                int index = center->_guardedPoints.Add();
                center->_guardedPoints[index].position = pos;
            }
            return nullptr;
    }

    Ref<Vehicle> vehicle = NewNonAIVehicle(info.object);
    if (!vehicle)
    {
        return nullptr;
    }

    if (info.name.GetLength() > 0)
    {
        GWorld->GetGameState()->VarSet(info.name, GameValueExt(vehicle.GetRef()), true);
    }

    // position is on sea level now
    if (vehicle->GetShape())
    {
        pos += vehicle->GetShape()->BoundingCenter();
    }
    Matrix4 transform;
    transform.SetUpAndDirection(normal, Vector3(0, 0, 1));
    transform.SetPosition(pos);
    vehicle->SetTransform(transform);

    AI_ERROR(dyn_cast<Detector>(vehicle.GetRef()));
    Detector* sensor = static_cast<Detector*>(vehicle.GetRef());
    sensor->FromTemplate(info);
    if (grp)
    {
        AI_ERROR(sensor->GetActivationBy() == ASAGroup);
        sensor->AssignGroup(grp);
    }

    // Add into World
    GWorld->AddBuilding(vehicle);
    if (multiplayer)
    {
        GetNetworkManager().CreateVehicle(vehicle, VLTBuilding, info.name, -1);
    }

    sensorsMap.Add(vehicle.GetRef());

    center = GWorld->GetCenter((TargetSide)Glob.header.playerSide);
    if (center)
    {
        float time = 0;
        switch (info.age)
        {
            case AAActual:
                time = 0;
                break;
            case AA5Min:
                time = 5 * 60;
                break;
            case AA10Min:
                time = 10 * 60;
                break;
            case AA15Min:
                time = 15 * 60;
                break;
            case AA30Min:
                time = 30 * 60;
                break;
            case AA60Min:
                time = 60 * 60;
                break;
            case AA120Min:
                time = 120 * 60;
                break;
            case AAUnknown:
                time = AccuracyLast;
                break;
        }
    }

    for (int k = 0; k < sensor->NSynchronizations(); k++)
    {
        int sync = sensor->GetSynchronization(k);
        AI_ERROR(sync >= 0);
        if (sync >= synchronized.Size())
        {
            synchronized.Resize(sync + 1);
        }
        synchronized[sync].Add(sensor);
    }

    return vehicle;
}

void CreateMarker(const ArcadeMarkerInfo& info, bool multiplayer)
{
    markersMap.Add(info);
}

void InitNoCenters(ArcadeTemplate& t, AutoArray<VehicleInitCmd, MemAllocSA>& inits, bool multiplayer)
{
    // empty vehicles
    int m = t.emptyVehicles.Size();
    for (int j = 0; j < m; j++)
    {
        ArcadeUnitInfo& uInfo = t.emptyVehicles[j];
        if (GRandGen.RandomValue() > uInfo.presence)
        {
            continue;
        }
        if (!GWorld->GetGameState()->EvaluateBool(uInfo.presenceCondition))
        {
            continue;
        }

        RString vehClass = Pars >> "CfgVehicles" >> uInfo.vehicle >> "vehicleClass";
        if (stricmp(vehClass, "Sounds") == 0)
        {
            CreateSoundSource(uInfo, t, multiplayer);
        }
        else if (stricmp(vehClass, "Mines") == 0)
        {
            CreateMine(uInfo, t, multiplayer);
        }
        else
        {
            EntityAI* veh = CreateVehicle(uInfo, t, TSideUnknown, multiplayer, false, false, false);
            if (veh && uInfo.init.GetLength() > 0)
            {
                int index = inits.Add();
                inits[index].vehicle = veh;
                inits[index].init = uInfo.init;
            }
        }
    }
    // create sensors - must be after empty vehicles
    m = t.sensors.Size();
    for (int j = 0; j < m; j++)
    {
        ArcadeSensorInfo& sInfo = t.sensors[j];
        CreateSensor(sInfo, nullptr, multiplayer);
    }
    // markers
    m = t.markers.Size();
    for (int j = 0; j < m; j++)
    {
        ArcadeMarkerInfo& mInfo = t.markers[j];
        CreateMarker(mInfo, multiplayer);
    }
}
const ParamEntry& AICenter::NextSoldierIdentity(bool woman)
{
    const char* nameSide = nullptr;
    switch (_side)
    {
        case TEast:
            nameSide = "East";
            break;
        case TWest:
            nameSide = "West";
            break;
        case TCivilian:
            nameSide = "Civilian";
            break;
        case TGuerrila:
        case TLogic:
            nameSide = "Guerrila";
            break;
        default:
            Fail("No such side");
            break;
    }
    AI_ERROR(nameSide);

    const char* nameType = woman ? "Women" : "Soldiers";
    int& index = woman ? _nWoman : _nSoldier;

    const ParamEntry& cfgSide = Pars >> "CfgWorlds" >> nameSide;
    int nNames = (cfgSide >> nameType).GetEntryCount();

    while (true)
    {
        if (index >= nNames)
        {
            // soldier database exhausted, start again from the beginning
            _nSoldier = 0;
            _nWoman = 0;
            EmptyDeadIdentities();
        }
        const ParamEntry& entry = (cfgSide >> nameType).GetEntry(index++);
        if (!IsIdentityDead(entry.GetContext()))
        {
            return entry;
        }
    }
    Fail("Unaccessible");
}

extern void ApplyEffects(AIGroup* group, int index);

AIUnit* AICenter::CreateSoldier(Transport* transport, int rank, const ParamEntry& cfgSide, bool multiplayer)
{
    RString nameType = transport->Type()->GetCrew();
    Person* soldier = dyn_cast<Person>(NewVehicle(nameType));
    // FIX: better handling of invalid crew specification
    if (!soldier)
    {
        ErrorMessage("Invalid crew %s", (const char*)nameType);
    }
    soldier->SetTargetSide(_side);

    // Add into World
    // maintain correct position in hierarchy
    soldier->SetPosition(transport->Position());

    // we are creating the soldier inside of some vehicle
    GWorld->AddOutVehicle(soldier);
    soldier->SetMoveOutDone(transport);

    AIUnit* unit = soldier->Brain();
    unit->Load(NextSoldierIdentity(soldier->IsWoman()));

    AIUnitInfo& info = soldier->GetInfo();
    info._rank = (Rank)rank;
    info._initExperience = info._experience = ExpForRank((Rank)rank);
    return unit;
}

AIUnit* AICenter::CreateUnit(const ArcadeUnitInfo& info, ArcadeTemplate& t, bool multiplayer, AIGroup* grp,
                             bool disableC, bool disableD, bool disableG)
{
    if (info.player == APNonplayable)
    {
        if (GRandGen.RandomValue() > info.presence)
        {
            return nullptr;
        }
        if (!GWorld->GetGameState()->EvaluateBool(info.presenceCondition))
        {
            return nullptr;
        }
    }

    const char* nameSide = nullptr;
    switch (_side)
    {
        case TEast:
            nameSide = "East";
            break;
        case TWest:
            nameSide = "West";
            break;
        case TGuerrila:
        case TCivilian:
        case TLogic:
            nameSide = "Guerrila";
            break;
        default:
            Fail("No such side");
            return nullptr;
    }
    AI_ERROR(nameSide);
    const ParamEntry& cfgSide = Pars >> "CfgWorlds" >> nameSide;

    Ref<EntityAI> veh = CreateVehicle(info, t, _side, multiplayer, disableC, disableD, disableG);
    if (!veh)
    {
        return nullptr;
    }

    AI_ERROR(veh->RefCounter() > 1);

    Person* soldier = dyn_cast<Person>(veh.GetRef());
    if (soldier)
    {
        if (info.special == ASpCargo)
        {
            for (int u = 0; u < MAX_UNITS_PER_GROUP; u++)
            {
                AIUnit* unit = grp->_units[u];
                if (!unit || !unit->IsUnit())
                {
                    continue;
                }
                Transport* veh = unit->GetVehicleIn();
                if (!veh)
                {
                    continue;
                }
                if (veh->GetFreeManCargo() > 0)
                {
                    veh->GetInCargo(soldier, false);
                    soldier->Brain()->SetState(AIUnit::InCargo);
                    soldier->Brain()->AssignAsCargo(veh);
                    soldier->Brain()->OrderGetIn(true);
                    break;
                }
            }
        }
        else if (_side != TLogic)
        {
            // soldier is free soldier - we should add sensor
            GWorld->AddSensor(soldier);
        }

        AIUnit* unit = soldier->Brain();
        unit->Load(NextSoldierIdentity(soldier->IsWoman()));

        AIUnitInfo& aiInfo = soldier->GetInfo();
        aiInfo._rank = (Rank)info.rank;
        aiInfo._initExperience = aiInfo._experience = ExpForRank((Rank)info.rank);
        unit->SetAbility(info.skill);

        // Add into AIGroup
        grp->AddUnit(unit);
        return unit;
    }
    else
    {
        Transport* transport = dyn_cast<Transport>(veh.GetRef());
        if (!transport)
        {
            RptF("%s is not soldier nor transport.", (const char*)veh->GetDebugName());
            Fail("No transport");
            return nullptr;
        }

        AI_ERROR(transport);
        // Add transport into group
        grp->AddVehicle(transport);

        AIUnit* unit = nullptr;

        // Create crew
        if (transport->GetType()->HasDriver() && !disableD)
        {
            AIUnit* driver = CreateSoldier(transport, info.rank, cfgSide, multiplayer);

            RString name;
            if (info.name.GetLength() > 0)
            {
                name = info.name + RString("d");
                driver->GetPerson()->SetVarName(name);
                GWorld->GetGameState()->VarSet(name, GameValueExt(driver->GetPerson()), true);
            }
            if (multiplayer)
            {
                GetNetworkManager().CreateVehicle(driver->GetPerson(), VLTVehicle, name, -1);
            }
            transport->GetInDriver(driver->GetPerson(), false);
            driver->AssignAsDriver(transport);
            driver->OrderGetIn(true);
            grp->AddUnit(driver);
            unit = driver;
            driver->SetAbility(info.skill);
        }

        int commanderOffset = 1;
        int gunnerOffset = -1;
        // for vehicle where gunner is commander gunner rank
        // should be higher that driver
        if (!transport->Type()->DriverIsCommander())
        {
            commanderOffset = 2;
            gunnerOffset = 1;
        }

        if (transport->GetType()->HasCommander() && !disableC)
        {
            int commanderRank = info.rank + commanderOffset;
            saturate(commanderRank, 0, NRanks - 1);
            AIUnit* commander = CreateSoldier(transport, (Rank)commanderRank, cfgSide, multiplayer);
            RString name;
            if (info.name.GetLength() > 0)
            {
                name = info.name + RString("c");
                commander->GetPerson()->SetVarName(name);
                GWorld->GetGameState()->VarSet(name, GameValueExt(commander->GetPerson()), true);
            }
            else
            {
                name = "";
            }
            if (multiplayer)
            {
                GetNetworkManager().CreateVehicle(commander->GetPerson(), VLTVehicle, name, -1);
            }
            transport->GetInCommander(commander->GetPerson(), false);
            commander->AssignAsCommander(transport);
            commander->OrderGetIn(true);
            grp->AddUnit(commander);
            unit = commander;
            commander->SetAbility(info.skill);
        }

        if (transport->GetType()->HasGunner() && !disableG)
        {
            int gunnerRank = info.rank + gunnerOffset;
            saturate(gunnerRank, 0, NRanks - 1);
            AIUnit* gunner = CreateSoldier(transport, (Rank)gunnerRank, cfgSide, multiplayer);
            RString name;
            if (info.name.GetLength() > 0)
            {
                name = info.name + RString("g");
                gunner->GetPerson()->SetVarName(name);
                GWorld->GetGameState()->VarSet(name, GameValueExt(gunner->GetPerson()), true);
            }
            else
            {
                name = "";
            }
            if (multiplayer)
            {
                GetNetworkManager().CreateVehicle(gunner->GetPerson(), VLTVehicle, name, -1);
            }
            transport->GetInGunner(gunner->GetPerson(), false);
            gunner->AssignAsGunner(transport);
            gunner->OrderGetIn(true);
            grp->AddUnit(gunner);

            if (!unit)
            {
                unit = gunner;
            }
            gunner->SetAbility(info.skill);
        }

        if (unit)
        {
            if (_side != TLogic)
            {
                GWorld->AddSensor(unit->GetPerson());
            }
        }
        return unit;
    }
}

void AICenter::BeginArcade(ArcadeTemplate& t, AutoArray<VehicleInitCmd, MemAllocSA>& inits)
{
    _nSoldier = 0;
    _nWoman = 0;
    SetResources(0); // resources are not used

    bool multiplayer = _mode == AICMNetwork;

    // create groups
    AIUnit* playerUnit = nullptr;
    if (_side == TCivilian)
    {
        _friends[TCivilian] = 1.0f;

        // friendship of military units is equal to resistance
        _friends[TGuerrila] = 1.0f;
        _friends[TWest] = t.intel.friends[TGuerrila][TWest];
        _friends[TEast] = t.intel.friends[TGuerrila][TEast];
    }
    else if (_side >= TSideUnknown)
    {
        // logic etc
        _friends[TEast] = 1.0;
        _friends[TWest] = 1.0;
        _friends[TGuerrila] = 1.0;
        _friends[TCivilian] = 1.0;
    }
    else
    {
        _friends[TEast] = t.intel.friends[_side][TEast];
        _friends[TWest] = t.intel.friends[_side][TWest];
        _friends[TGuerrila] = t.intel.friends[_side][TGuerrila];
        _friends[TCivilian] = 1.0;
    }

    int n = t.groups.Size();
    int group = -1;
    for (int i = 0; i < n; i++)
    {
        ArcadeGroupInfo& gInfo = t.groups[i];
        if (gInfo.side != _side)
        {
            continue;
        }
        group++;

        AIGroup* grp = new AIGroup();
        AddGroup(grp);
        AIUnit* leaderUnit = nullptr;
        OLinkArray<AIUnit> inFormation;
        // create vehicles pass 1
        int m = gInfo.units.Size();
        for (int j = 0; j < m; j++)
        {
            ArcadeUnitInfo& uInfo = gInfo.units[j];
            if (uInfo.special == ASpCargo)
            {
                continue;
            }

            bool disableC = false;
            bool disableD = false;
            bool disableG = false;
            if (multiplayer)
            {
                for (int r = 0; r < GetNetworkManager().NPlayerRoles(); r++)
                {
                    const PlayerRole* role = GetNetworkManager().GetPlayerRole(r);
                    if (role->player == NO_PLAYER && role->side == _side && role->group == group && role->unit == j)
                    {
                        switch (role->position)
                        {
                            case PRPCommander:
                                disableC = true;
                                break;
                            case PRPNone:
                            case PRPDriver:
                                disableD = true;
                                break;
                            case PRPGunner:
                                disableG = true;
                                break;
                        }
                    }
                }
            }

            AIUnit* unit = CreateUnit(uInfo, t, multiplayer, grp, disableC, disableD, disableG);
            if (!unit)
            {
                continue;
            }

            EntityAI* veh = unit->GetVehicle();
            if (multiplayer)
            {
                for (int r = 0; r < GetNetworkManager().NPlayerRoles(); r++)
                {
                    const PlayerRole* role = GetNetworkManager().GetPlayerRole(r);
                    if (role->player != NO_PLAYER && role->side == _side && role->group == group && role->unit == j)
                    {
                        AIUnit* player = nullptr;
                        switch (role->position)
                        {
                            case PRPNone:
                            case PRPCommander:
                                player = veh->CommanderUnit();
                                break;
                            case PRPDriver:
                                player = veh->PilotUnit();
                                break;
                            case PRPGunner:
                                player = veh->GunnerUnit();
                                break;
                        }
                        AI_ERROR(player);
                        player->SetPlayable();
                        if (role->player != AI_PLAYER)
                        {
                            player->GetPerson()->SetRemotePlayer(role->player);
                            const PlayerIdentity* identity = GetNetworkManager().FindIdentity(role->player);
                            if (identity)
                            {
                                AIUnitInfo& info = player->GetPerson()->GetInfo();
                                info._identityContext = RString();
                                info._name = identity->name;
                                info._face = identity->face;
                                info._glasses = identity->glasses;
                                info._speaker = identity->speaker;
                                info._pitch = identity->pitch;
                                if (identity->squad)
                                {
                                    info._squadTitle = identity->squad->title;
                                    if (identity->squad->picture.GetLength() > 0)
                                    {
                                        RString picture = Poseidon::FindNetworkSquadPictureTmpPath(
                                            identity->squad->nick, identity->squad->picture,
                                            [](const RString& path) { return QIFStream::FileExists(path); });
                                        if (picture.GetLength() > 0)
                                        {
                                            info._squadPicture = GlobLoadTexture(picture);
                                        }
                                    }
                                }
                                player->GetPerson()->SetFace(info._face, info._name);
                                player->GetPerson()->SetGlasses(info._glasses);
                                player->SetSpeaker(info._speaker, info._pitch);
                            }
                        }
                    }
                }
            }
            else
            {
                switch (uInfo.player)
                {
                    case APPlayerCommander:
                        playerUnit = veh->CommanderUnit();
                        goto PlayerFound;
                    case APPlayerDriver:
                        playerUnit = veh->PilotUnit();
                        goto PlayerFound;
                    case APPlayerGunner:
                        playerUnit = veh->GunnerUnit();
                        goto PlayerFound;
                    PlayerFound:
                    {
                        playerUnit->GetPerson()->SetRemotePlayer(0);
                        AIUnitInfo& info = playerUnit->GetPerson()->GetInfo();
                        info._identityContext = RString();
                        info._name = Glob.header.playerName;
                        info._face = Glob.header.playerFace;
                        info._glasses = Glob.header.playerGlasses;
                        info._speaker = Glob.header.playerSpeaker;
                        info._pitch = Glob.header.playerPitch;

                        playerUnit->GetPerson()->SetFace(info._face);
                        playerUnit->GetPerson()->SetGlasses(info._glasses);
                        playerUnit->SetSpeaker(info._speaker, info._pitch);
                    }
                    break;
                }
            }

            if (uInfo.leader)
            {
                leaderUnit = unit;
            }
            if (uInfo.special == ASpForm)
            {
                inFormation.Add(unit);
            }

            if (uInfo.init.GetLength() > 0)
            {
                int index = inits.Add();
                inits[index].vehicle = veh;
                inits[index].init = uInfo.init;
            }
        }
        // create vehicles pass 2
        // m is still # of units
        for (int j = 0; j < m; j++)
        {
            ArcadeUnitInfo& uInfo = gInfo.units[j];
            if (uInfo.special != ASpCargo)
            {
                continue;
            }

            if (multiplayer)
            {
                bool disable = false;
                for (int r = 0; r < GetNetworkManager().NPlayerRoles(); r++)
                {
                    const PlayerRole* role = GetNetworkManager().GetPlayerRole(r);
                    if (role->side == _side && role->group == group && role->unit == j)
                    {
                        disable = role->player == NO_PLAYER;
                        break;
                    }
                }
                if (disable)
                {
                    continue;
                }
            }

            AIUnit* unit = CreateUnit(uInfo, t, multiplayer, grp);
            if (!unit)
            {
                continue;
            }

            if (multiplayer)
            {
                for (int r = 0; r < GetNetworkManager().NPlayerRoles(); r++)
                {
                    const PlayerRole* role = GetNetworkManager().GetPlayerRole(r);
                    if (role->player != NO_PLAYER && role->side == _side && role->group == group && role->unit == j)
                    {
                        unit->SetPlayable();
                        if (role->player != AI_PLAYER)
                        {
                            unit->GetPerson()->SetRemotePlayer(role->player);
                            const PlayerIdentity* identity = GetNetworkManager().FindIdentity(role->player);
                            if (identity)
                            {
                                AIUnitInfo& info = unit->GetPerson()->GetInfo();
                                info._identityContext = RString();
                                info._name = identity->name;
                                info._face = identity->face;
                                info._glasses = identity->glasses;
                                info._speaker = identity->speaker;
                                info._pitch = identity->pitch;
                                if (identity->squad)
                                {
                                    info._squadTitle = identity->squad->title;
                                    if (identity->squad->picture.GetLength() > 0)
                                    {
                                        RString picture = RString("tmp\\squads\\") + identity->squad->nick +
                                                          RString("\\") + identity->squad->picture;
                                        if (QIFStream::FileExists(picture))
                                        {
                                            info._squadPicture = GlobLoadTexture(picture);
                                        }
                                    }
                                }
                                unit->GetPerson()->SetFace(info._face, info._name);
                                unit->GetPerson()->SetGlasses(info._glasses);
                                unit->SetSpeaker(info._speaker, info._pitch);
                            }
                        }
                    }
                }
            }
            else
            {
                switch (uInfo.player)
                {
                    case APPlayerCommander:
                    case APPlayerDriver:
                    case APPlayerGunner:
                        playerUnit = unit;
                        playerUnit->GetPerson()->SetRemotePlayer(0);
                        {
                            AIUnitInfo& info = playerUnit->GetPerson()->GetInfo();
                            info._identityContext = RString();
                            info._name = Glob.header.playerName;
                            info._face = Glob.header.playerFace;
                            info._glasses = Glob.header.playerGlasses;
                            info._speaker = Glob.header.playerSpeaker;
                            info._pitch = Glob.header.playerPitch;

                            playerUnit->GetPerson()->SetFace(info._face);
                            playerUnit->GetPerson()->SetGlasses(info._glasses);
                            playerUnit->SetSpeaker(info._speaker, info._pitch);
                        }
                        break; // in cargo
                }
            }
            if (uInfo.leader)
            {
                leaderUnit = unit;
            }

            if (uInfo.init.GetLength() > 0)
            {
                int index = inits.Add();
                inits[index].vehicle = unit->GetPerson();
                inits[index].init = uInfo.init;
            }
        }

        if (grp->NUnits() == 0)
        {
            grp->RemoveFromCenter();
            continue;
        }

        grp->CalculateMaximalStrength();
        if (leaderUnit)
        {
            grp->SelectLeader(leaderUnit);
            grp->SortUnits();
        }
        else
        {
            grp->SortUnits();
            SelectLeader(grp);
            leaderUnit = grp->Leader();
            AI_ERROR(leaderUnit);
        }

        // create waypoints
        m = gInfo.waypoints.Size();
        grp->_wp.Resize(m + 1);
        ArcadeWaypointInfo& wInfo = grp->_wp[0];
        wInfo.Init();
        wInfo.position = leaderUnit->Position();
        wInfo.type = ACMOVE;
        for (int j = 0; j < m; j++)
        {
            ArcadeWaypointInfo& wInfo = gInfo.waypoints[j];
            grp->_wp[j + 1].ArcadeWaypointInfo::operator=(wInfo);
            // Add synchronization into map
            int o = wInfo.synchronizations.Size();
            for (int k = 0; k < o; k++)
            {
                int sync = wInfo.synchronizations[k];
                AI_ERROR(sync >= 0);
                if (sync >= synchronized.Size())
                {
                    synchronized.Resize(sync + 1);
                }
                synchronized[sync].Add(grp);
            }

            if (wInfo.placement > 0)
            {
                // set random position
                grp->_wp[j + 1].position = FindWaypointPosition(wInfo.position, wInfo.placement);
            }
        }
        // create sensors - must be after selecting leader
        m = gInfo.sensors.Size();
        for (int j = 0; j < m; j++)
        {
            ArcadeSensorInfo& sInfo = gInfo.sensors[j];
            CreateSensor(sInfo, grp, multiplayer);
        }

        // set formation and semaphores
        if (grp->NWaypoints() > 1)
        {
            const ArcadeWaypointInfo& wInfo = grp->GetWaypoint(1);
            if (wInfo.formation >= 0)
            {
                AI::Formation f = (AI::Formation)wInfo.formation;
                grp->MainSubgroup()->SetFormation(f);
            }
            if (wInfo.combatMode >= 0)
            {
                AI::Semaphore s = (AI::Semaphore)wInfo.combatMode;
                grp->SetSemaphore(s);
                for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
                {
                    AIUnit* unit = grp->UnitWithID(i + 1);
                    if (unit)
                    {
                        unit->SetSemaphore(s);
                    }
                }
            }
            if (wInfo.speed != SpeedUnchanged)
            {
                grp->MainSubgroup()->SetSpeedMode(wInfo.speed);
            }
            if (wInfo.combat != CMUnchanged)
            {
                grp->SetCombatModeMajor(wInfo.combat);
            }
        }

        // force units into formation
        AIUnit* leader = grp->MainSubgroup()->Leader();
        Vector3 direction = leader->Direction();
        direction[1] = 0;
        direction.Normalize();
        grp->MainSubgroup()->SetDirection(direction);
        grp->MainSubgroup()->UpdateFormationPos();
        m = inFormation.Size();
        for (int j = 0; j < m; j++)
        {
            AIUnit* unit = inFormation[j];
            AI_ERROR(unit);
            if (unit == leader)
            {
                continue;
            }
            if (!unit->IsUnit())
            {
                continue;
            }

            EntityAI* veh = unit->GetVehicle();
            Vector3Val oldPos = veh->Position();

            Vector3 pos = unit->GetFormationAbsolute();
            Vector3 normal;
            if (!AIUnit::FindFreePosition(pos, normal, unit->IsSoldier(), veh))
            {
                LOG_DEBUG(AI, "Bad position");
                normal = VUp;
            }

            if (veh->GetType()->IsKindOf(GWorld->Preloaded(VTypeAir)))
            {
                float yAbove = oldPos[1] - GLOB_LAND->RoadSurfaceYAboveWater(oldPos[0], oldPos[2]);
                pos[1] += yAbove;
            }

            Matrix4 transform;
            transform.SetPosition(pos);
            transform.SetDirectionAndUp(direction, normal);
            veh->PlaceOnSurface(transform);
            veh->Move(transform);
        }
        grp->MainSubgroup()->UpdateFormationCoef();

        // send mission
        Mission mis;
        mis._action = Mission::Arcade;
        SendMission(grp, mis);
    }

    if (playerUnit)
    {
        if (_mode == AICMIntro)
        {
            if (playerUnit)
            {
                GWorld->SwitchCameraTo(playerUnit->GetVehicle(), CamExternal);
            }
        }
        else
        {
            GStats._campaign._playerInfo.unit = playerUnit;

            // keep rank and exp. from editor
            if (_mode != AICMNetwork)
            {
                PlayerInfo().rank = playerUnit->GetPerson()->GetInfo()._rank;
                PlayerInfo().experience = playerUnit->GetPerson()->GetInfo()._experience;
            }

            GWorld->SwitchCameraTo(playerUnit->GetVehicle(), CamInternal);
            GWorld->SetPlayerManual(true);
            GWorld->SwitchPlayerTo(playerUnit->GetPerson());
            GWorld->SetRealPlayer(playerUnit->GetPerson());
        }
    }
}
} // namespace Poseidon
