#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>

#include <Poseidon/AI/VehicleAI.hpp>
#include <Poseidon/World/Entities/Infantry/Person.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Random/randomGen.hpp>
#include <Poseidon/IO/Serialization/ParamArchive.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/World/Terrain/Visibility.hpp>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Memory/FastAlloc.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

namespace Poseidon
{
using namespace Foundation;

#if _ENABLE_CHEATS

#define DIAG_RESULT 0
#define DIAG_SHOOT_RESULT 0
#define DIAG_ATTACK 0

#define DIAG_TARGET 0 // SelectFireWeapon function (0..3)

#define LOG_TARGETS 0

#endif

const float ValidTime = 5.0f;

const float MinVisibleFire = 0.63f;

float Target::FadingSpotability() const
{
    float old = Glob.time - spotabilityTime - ValidTime - 1;
    if (old < 0)
    {
        return spotability;
    }
    float ret = spotability - old * (1.0f / 30);
    saturate(ret, 0, 4);
    return ret;
}

float Target::FadingAccuracy() const
{
    float fade = 1 - (Glob.time - accuracyTime - ValidTime - 1) * (1.0f / 240);
    saturate(fade, 0, 1);
    return accuracy * fade;
}

float Target::FadingSideAccuracy() const
{
    float fade = 1 - (Glob.time - sideAccuracyTime - ValidTime - 1) * (1.0f / 240);
    saturate(fade, 0, 1);
    return sideAccuracy * fade;
}

DEFINE_FAST_ALLOCATOR(Target)

Target::Target(AIGroup* grp) : group(grp)
{
    Init();
}

void Target::Init()
{
    position = VZero;
    posError = Vector3(GRandGen.RandomValue() * 400 - 200, GRandGen.RandomValue() * 20 - 10,
                       GRandGen.RandomValue() * 400 - 200);
    speed = VZero;
    side = TSideUnknown;
    sideChecked = false;
    type = nullptr;
    spotability = 0;
    spotabilityTime = Time(0);
    accuracy = 0;
    accuracyTime = Time(0);
    sideAccuracy = 0;
    sideAccuracyTime = Time(0);
    isKnown = false;
    posReported = VZero;
    timeReported = TIME_MIN;
    vanished = false;
    destroyed = false;
    idExact = nullptr;
    idSensor = nullptr;
    delay = TIME_MAX;
    delaySensor = TIME_MAX;
    lastSeen = TIME_MIN;
    dammagePerMinute = 0;
    subjectiveCost = 0;
}

bool Target::IsKnownBy(AIUnit* unit) const
{
    if (!isKnown)
    {
        return false;
    }
    if (delay <= Glob.time)
    {
        return true;
    }
    EntityAI* vehicle = unit->GetPerson();
    if (vehicle != idSensor)
    {
        return false;
    }
    return delaySensor <= Glob.time;
}

bool Target::IsKnownByAll() const
{
    return isKnown && delay < Glob.time;
}

bool Target::IsKnownBySome() const
{
    return isKnown && (delay <= Glob.time || delaySensor <= Glob.time);
}

bool Target::IsKnown() const
{
    return IsKnownBySome();
}

void Target::LimitError(float error)
{
    if (posError.SquareSize() > Square(error))
    {
        posError = posError.Normalized() * error * 0.999f;
    }
}

Vector3 Target::ExactAimingPosition() const
{
    Object* exact = idExact;
    if (exact)
    {
        if (Glob.time < lastSeen + 1.0f)
        {
            return exact->AimingPosition();
        }
    }
    float timeFromSeen = Glob.time.Diff(lastSeen);
    saturateMin(timeFromSeen, 10);
    return position + speed * timeFromSeen;
}

static bool IsCombatUnit(EntityAI* veh)
{
    if (veh->NMagazines() > 0)
    {
        return true;
    }
    if (veh->NMagazineSlots() <= 0)
    {
        return false;
    }
    for (int i = 0; i < veh->NMagazineSlots(); i++)
    {
        const MagazineSlot& slot = veh->GetMagazineSlot(i);
        const MuzzleType* muzzle = slot._muzzle;
        if (muzzle->_magazines.Size() > 0)
        {
            return true;
        }
    }
    return false;
}

TargetState Target::State(AIUnit* sensor) const
{
    if (destroyed)
    {
        return TargetDestroyed;
    }
    if (vanished)
    {
        return TargetDestroyed;
    }
    if (!sensor)
    {
        return TargetAlive;
    }
    AIGroup* grp = sensor->GetGroup();
    if (!grp)
    {
        return TargetDestroyed;
    }
    AICenter* center = grp->GetCenter();
    if (!center)
    {
        return TargetDestroyed;
    }
    if (!center->IsEnemy(side))
    {
        return TargetAlive;
    }
    if (!idExact)
    {
        return TargetEnemyEmpty;
    }
    if (!idExact->IsAbleToMove())
    {
        return TargetEnemyEmpty;
    }
    if (IsCombatUnit(idExact))
    {
        if (!idExact->IsAbleToFire())
        {
            return TargetEnemy;
        }
    }
    return TargetEnemyCombat;
}

Vector3 Target::LandAimingPosition() const
{
    Object* exact = idExact;
    if (exact)
    {
        if (Glob.time < lastSeen + 1.0f)
        {
            return exact->AimingPosition();
        }
    }

    Vector3 pos = ExactAimingPosition();
    float aboveSurface = position.Y() - GLandscape->SurfaceYAboveWater(position.X(), position.Z());
    pos += posError;
    pos[1] = GLandscape->SurfaceYAboveWater(pos.X(), pos.Z()) + aboveSurface;
    return pos;
}

Vector3 Target::AimingPosition() const
{
    return ExactAimingPosition() + posError;
}

float Target::VisibleSize() const
{
    Object* exact = idExact;
    if (exact)
    {
        return exact->VisibleSize();
    }
    LODShapeWithShadow* shape = type->GetShape();
    if (!shape)
    {
        RptF("type %s - no shape", (const char*)type->GetName());
        return 0.5f;
    }
    return shape->BoundingSphere() * 0.5f;
}

LSError Target::Serialize(ParamArchive& ar)
{
    if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassFirst)
    {
        Init();
    }

    PARAM_CHECK(ar.SerializeRef("idExact", idExact, 1))
    PARAM_CHECK(ar.SerializeRef("idSensor", idSensor, 1))
    PARAM_CHECK(ar.SerializeRef("idKiller", idKiller, 1))
    PARAM_CHECK(ar.SerializeRef("group", group, 1))

    PARAM_CHECK(ar.Serialize("position", position, 1))
    PARAM_CHECK(ar.Serialize("posError", posError, 1, VZero))
    PARAM_CHECK(ar.Serialize("speed", speed, 1, VZero))
    PARAM_CHECK(ar.SerializeEnum("side", side, 1, TSideUnknown))
    PARAM_CHECK(ar.Serialize("sideChecked", sideChecked, 1, true))
    PARAM_CHECK(Poseidon::Serialize(ar, "type", type, 1))

    PARAM_CHECK(ar.Serialize("spotability", spotability, 1, 4.0));
    PARAM_CHECK(ar.Serialize("spotabilityTime", spotabilityTime, 1));

    PARAM_CHECK(ar.Serialize("accuracy", accuracy, 1, 4.0));
    PARAM_CHECK(ar.Serialize("accuracyTime", accuracyTime, 1));

    PARAM_CHECK(ar.Serialize("sideAccuracy", sideAccuracy, 1, 4.0));
    PARAM_CHECK(ar.Serialize("sideAccuracyTime", sideAccuracyTime, 1));

    PARAM_CHECK(ar.Serialize("delay", delay, 1, Time(0)));
    PARAM_CHECK(ar.Serialize("delaySensor", delaySensor, 1, Time(0)));
    PARAM_CHECK(ar.Serialize("lastSeen", lastSeen, 1, TIME_MIN));

    PARAM_CHECK(ar.Serialize("isKnown", isKnown, 1, false));
    PARAM_CHECK(ar.Serialize("vanished", vanished, 1, false));
    PARAM_CHECK(ar.Serialize("destroyed", destroyed, 1, false));
    PARAM_CHECK(ar.Serialize("timeReported", timeReported, 1, TIME_MIN));
    PARAM_CHECK(ar.Serialize("posReported", posReported, 1, VZero));

    PARAM_CHECK(ar.Serialize("dammagePerMinute", dammagePerMinute, 1, 0.0));
    PARAM_CHECK(ar.Serialize("subjectiveCost", subjectiveCost, 1, 0.0));

    return LSOK;
}

Target* Target::LoadRef(ParamArchive& ar)
{
    TargetSide side = TSideUnknown;
    int idGroup;
    int index;
    if (ar.SerializeEnum("side", side, 1) != LSOK)
    {
        return nullptr;
    }
    if (ar.Serialize("idGroup", idGroup, 1) != LSOK)
    {
        return nullptr;
    }
    if (ar.Serialize("index", index, 1) != LSOK)
    {
        return nullptr;
    }
    AICenter* center = GWorld->GetCenter(side);
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
    if (index < 0)
    {
        return nullptr;
    }
    const TargetList& list = group->GetTargetList();
    return list[index];
}

LSError Target::SaveRef(ParamArchive& ar)
{
    AIGroup* grp = group;
    AICenter* center = grp ? grp->GetCenter() : nullptr;
    TargetSide side = center ? center->GetSide() : TSideUnknown;
    int idGroup = grp ? grp->ID() : -1;
    int index = -1;
    if (grp)
    {
        const TargetList& list = grp->GetTargetList();
        for (int i = 0; i < list.Size(); i++)
        {
            Target* tgt = list[i];
            if (tgt == this)
            {
                index = i;
                break;
            }
        }
    }
    PARAM_CHECK(ar.SerializeEnum("side", side, 1))
    PARAM_CHECK(ar.Serialize("idGroup", idGroup, 1))
    PARAM_CHECK(ar.Serialize("index", index, 1))
    return LSOK;
}

void TargetList::Manage(AIGroup* group)
{
    for (int s = 0; s < Size();)
    {
        Target* tVis = Get(s);

        EntityAI* ai = tVis->idExact;
        if (!ai)
        {
            Delete(s);
            continue;
        }

        bool canSee = false;
        for (int u = 1; u <= MAX_UNITS_PER_GROUP; u++)
        {
            AIUnit* unit = group->UnitWithID(u);
            if (!unit)
            {
                continue;
            }
            if (unit->IsInCargo())
            {
                continue;
            }
            EntityAI* veh = unit->GetVehicle();
            if (!veh)
            {
                LOG_ERROR(AI, "Unit {} has no vehicle", (const char*)unit->GetDebugName());
                continue;
            }
            float dist2 = veh->Position().Distance2(ai->Position());

            float irRange = veh->GetType()->GetIRScanRange();
            if (!ai || !ai->GetType()->GetIRTarget())
            {
                irRange = 0;
            }
            else if (!veh->GetType()->GetIRScanGround() && !ai->Airborne())
            {
                irRange = 0;
            }

            float maxDistance2 = Square(floatMax(irRange, TACTICAL_VISIBILITY));
            if (dist2 < maxDistance2)
            {
                canSee = true;
                break;
            }
        }
        if (!canSee)
        {
            Delete(s);
            continue;
        }

        if (tVis->isKnown)
        {
            tVis->vanished = !ai->IsInLandscape();
        }
        else
        {
            if (!ai->IsInLandscape())
            {
                Delete(s);
                continue;
            }
        }
        s++;
    }
}

int TargetList::Find(TargetType* obj) const
{
    for (int i = 0; i < Size(); i++)
    {
        if (Get(i)->idExact == obj)
        {
            return i;
        }
    }
    return -1;
}

void EntityAI::AddNewTargets(TargetList& res, bool initialize)
{
    float nearestEnemyDist2 = 1e10;

    _newTargetsTime = Glob.time;

    AIUnit* brain = CommanderUnit();
    if (!brain)
    {
        return;
    }
    AIGroup* group = brain->GetGroup();
    if (!group)
    {
        return;
    }
    AICenter* center = group->GetCenter();
    if (!center)
    {
        return;
    }

    float irRange = GetType()->GetIRScanRange();
    const float maxVisibility2 = Square(floatMax(TACTICAL_VISIBILITY, irRange));

    int nv = GLOB_WORLD->NVehicles();
    int nb = GLOB_WORLD->NBuildings();
    for (int i = 0; i < nv + nb; i++)
    {
        Vehicle* vehicle = i < nv ? GLOB_WORLD->GetVehicle(i) : GLOB_WORLD->GetBuilding(i - nv);
        float dist2 = vehicle->Position().Distance2Inline(Position());
        if (dist2 > maxVisibility2)
        {
            continue;
        }
        EntityAI* ai = dyn_cast<EntityAI>(vehicle);
        if (!ai)
        {
            continue;
        }

        const EntityAIType* type = ai->GetType();
        if (dist2 > Square(TACTICAL_VISIBILITY))
        {
            if (!type->GetIRTarget() || !GetType()->GetIRScanGround() && !ai->Airborne())
            {
                continue;
            }
        }

        if (!ai->IsInLandscape())
        {
            continue;
        }
        if (center->IsEnemy(ai->GetTargetSide()))
        {
            saturateMin(nearestEnemyDist2, dist2);
        }

        int index = 0;
        for (; index < res.Size(); index++)
        {
            Target* target = res[index];
            EntityAI* obj = target->idExact;
            if (obj == ai)
            {
                break;
            }
        }
        if (index >= res.Size())
        {
            Target* target = new Target(group);
            res.Add(target);
            target->position = ai->AimingPosition();
            target->speed = ai->Speed();
            target->sideChecked = false;
            target->side = TSideUnknown;
            target->type = ai->GetType(0);
            target->idExact = ai;
            target->spotabilityTime = Glob.time - 60;
            target->accuracyTime = Glob.time - 60;
            target->sideAccuracyTime = Glob.time - 60;
            target->accuracy = 0;
            target->sideAccuracy = 0;
            target->spotability = 0;
            target->isKnown = false;
            target->timeReported = TIME_MIN;
            target->posReported = VZero;
            target->destroyed = false;
            target->vanished = false;
#if LOG_TARGETS
            LOG_DEBUG(AI, "New target {} {}", (const char*)GetDebugName(), (const char*)ai->GetDebugName());
#endif
        }
    }
    brain->SetNearestEnemyDist2(nearestEnemyDist2);
}

static void TargetSeesItself(Target* target, EntityAI* ai, AIUnit* unit, float trackTargetsPeriod)
{
    target->type = ai->GetType(4);
    target->side = ai->GetTargetSide(4);
    target->sideChecked = true;
    target->spotability = 4;
    target->accuracy = 4;
    target->sideAccuracy = 4;
    target->spotabilityTime = Glob.time;
    target->accuracyTime = Glob.time;
    target->sideAccuracyTime = Glob.time;
    target->isKnown = true;
    target->lastSeen = Glob.time + trackTargetsPeriod;
    target->idSensor = unit->GetPerson();
    target->delay = Glob.time - 5;       // no delay
    target->delaySensor = Glob.time - 5; // no delay
    target->position = ai->AimingPosition();
    target->posError = VZero;
    target->speed = ai->Speed();
    target->timeReported = Glob.time; // no need to report itself
    target->posReported = target->position;
}

float EntityAI::CalcVisibility(EntityAI* ai, float dist2, float* audibility, bool assumeLOS)
{
    if (audibility)
    {
        *audibility = 0;
    }

    AIUnit* brain = CommanderUnit();
    if (!brain || brain->GetLifeState() != AIUnit::LSAlive)
    {
        return 0;
    }

    float vis = 1;
    const EntityAIType* type = GetType();
    float irRange = type->GetIRScanRange();
    if (ai->GetType()->GetLaserTarget())
    {
        if (!type->GetLaserScanner())
        {
            return 0;
        }
    }
    bool irScan = dist2 < Square(irRange) && ai->GetType()->GetIRTarget();
    if (!type->GetIRScanGround() && !ai->Airborne())
    {
        irScan = false;
    }
    if (!irScan)
    {
        if (dist2 > Square(TACTICAL_VISIBILITY))
        {
            vis = 0;
        }
        vis = 1 - GLOB_SCENE->TacticalFog8(dist2) * (1.0f / 255);
    }
    else
    {
        if (dist2 > Square(irRange))
        {
            vis = 0;
        }
        else
        {
            float dist = dist2 * InvSqrt(dist2);
            vis = 1 - dist / irRange;
        }
    }

    // aiRadius intentionally uses GetRadius, not VisibleSize; VisibleMovement accounts for size separately
    float aiRadius = ai->GetRadius();
    float landVis = assumeLOS ? 1 : GWorld->Visibility(brain, ai);

    if (ai->GetType()->GetLaserTarget())
    {
        aiRadius = 160;
    }
    float radiusCoef = 5000;
    if (irScan)
    {
        radiusCoef *= 25;
    }
    float sizeVis = dist2 > 0 ? aiRadius * radiusCoef / dist2 : 100;
    saturateMin(sizeVis, 40);

    vis *= sizeVis;
    vis *= landVis;

    if (audibility)
    {
        float aud = 1;

        // units can hear even behind obstructions to some extent
        PoseidonAssert(landVis >= 0 && landVis <= 1);
        float landAud = 1 - (1 - landVis) * 0.90f;
        float audRadiusCoef = 5000;
        float audRadis = ai->GetRadius();
        float sizeAud = dist2 > 0 ? audRadis * audRadiusCoef / dist2 : 1000;
        saturateMin(sizeAud, 240);

        aud *= sizeAud;
        aud *= landAud;

        *audibility = aud;
    }
    return vis;
}

void EntityAI::TrackTargets(TargetList& res, AIUnit* unit, int canSee, bool initialize, float maxDist,
                            float trackTargetsPeriod)
{
    if (!canSee)
    {
        return;
    }

    AIGroup* group = unit->GetGroup();
    if (!group)
    {
        return;
    }
    AICenter* center = group->GetCenter();
    if (!center)
    {
        return;
    }

    PoseidonAssert(GLOB_WORLD->CheckVehicleStructure());

    float opticsFov = 0.30;
    float opticsZoom = 1;
    float cosOpt = 0.95393920142;       // no optics - cos 17 deg
    float cosEye = 0.7f;                // cos 45 deg
    float cosEyeFocus = 0.96592582629f; // cos 15 deg (r-button zoom ability)

    if (canSee & CanSeeOptics)
    {
        if (_currentWeapon >= 0 && EnableViewThroughOptics())
        {
            const MagazineSlot& slot = GetMagazineSlot(_currentWeapon);
            if (slot._muzzle)
            {
                opticsFov = slot._muzzle->_opticsZoomMax;
            }
        }

        if (opticsFov < 0.3)
        {
            cosOpt = sqrt(1 - opticsFov * opticsFov);
            opticsZoom = Square(floatMax(0.30 / opticsFov, 1));
        }
    }

    if (!(canSee & CanSeeEye))
    {
        cosEye = cosOpt;
    }

    float sensitivityEar = GetType()->_sensitivityEar;
    if (!(canSee & CanSeeEar))
    {
        sensitivityEar = 0;
    }

    bool notManual = unit->HasAI() && !unit->GetPerson()->IsRemotePlayer();
    float nearestEnemy2 = 1e10;

    float irRange = GetType()->GetIRScanRange();
    if (!(canSee & CanSeeRadar))
    {
        irRange = 0;
    }

    saturateMax(maxDist, irRange);
    saturateMax(maxDist, TACTICAL_VISIBILITY);

    const LightSun* sun = GScene->MainLight();
    float night = sun->NightEffect();
    if (night > 0)
    {
        float dark = 1.02 - sun->Ambient().Brightness() - sun->Diffuse().Brightness();
        saturateMin(night, dark);

        Person* person = unit->GetPerson();
        bool hasNV = (person->IsNVEnabled() && person->IsNVWanted() || GetType()->GetNightVision());

        if (hasNV)
        {
            saturateMin(night, 0.75f);
        }
        else
        {
            saturateMin(night, 0.97f);
        }
    }

    Vector3 wepDir = Direction();
    if (_currentWeapon >= 0)
    {
        wepDir = GetWeaponDirection(_currentWeapon);
    }

    for (int index = 0; index < res.Size(); index++)
    {
        Target* target = res[index];
        EntityAI* ai = target->idExact;
        if (!ai)
        {
            continue;
        }

        if (ai == this)
        {
            TargetSeesItself(target, ai, unit, trackTargetsPeriod);
            continue;
        }
        if (!ai->IsInLandscape())
        {
            continue;
        }

        float dist2 = ai->Position().Distance2Inline(Position());
        if (dist2 > Square(maxDist))
        {
            continue;
        }

        float incDist2 = 1 + (unit->GetInvAbility() - 1) * 0.4f;
        dist2 *= incDist2;
        if (dist2 > Square(maxDist))
        {
            continue;
        }

        bool laserContact = (ai->GetType()->GetLaserTarget() && GetType()->GetLaserScanner());
        bool radarContact = (dist2 < Square(irRange) && ai->GetType()->GetIRTarget() &&
                             (GetType()->GetIRScanGround() || ai->Airborne()));

        Target* assigned = unit->GetTargetAssigned();
        float spotTreshold = assigned ? 0.75f : 0.5f;
        if (!notManual)
        {
            // player auto-spotting: veteran mode never auto-spots; friendly/neutral can be spotted but not radioed
            TargetSide aiSide = ai->GetTargetSide();

            spotTreshold = USER_CONFIG.IsEnabled(DTAutoSpot) ? 0.75f : 100.0f;

            if (center->IsFriendly(aiSide) || center->IsNeutral(aiSide))
            {
                saturateMin(spotTreshold, 0.6);
            }
        }

        float movement = ai->VisibleMovement();
        float aiAudible = ai->Audible();

        float audibility = 0;
        float visibility = 0;
        if (!ai->GetType()->GetLaserTarget() || GetType()->GetLaserScanner())
        {
            visibility = CalcVisibility(ai, dist2, &audibility);
        }

        if (!radarContact && !laserContact)
        {
            float mySpeed = _speed.Size();
            float speedCoef = mySpeed * (1.0f / 200);
            saturateMin(speedCoef, 0.8f);
            visibility -= speedCoef;
            audibility -= speedCoef;
        }

        float actSpotabilityEye = visibility;
        float actSpotabilityEar = audibility;

        if (!radarContact && !laserContact)
        {
            saturate(visibility, 0, 10);
        }
        saturate(audibility, 0, 120);

        float visibleAccuracy = visibility * 3;
        saturateMin(visibleAccuracy, 30);

        float audibleAccuracy = audibility * 3;

        Vector3Val eDir = GetEyeDirection();
        float cosFiEye = eDir.CosAngle(ai->Position() - Position());
        float cosFiWep = cosFiEye;

        if (_currentWeapon >= 0)
        {
            cosFiWep = wepDir.CosAngle(ai->Position() - Position());
        }

        float cosEyeAdjusted = cosEye;
        if (ai->GetType()->GetLaserTarget())
        {
            cosEyeAdjusted = -1;
        }

        float nightAccCoef = 1 - night;
        if (radarContact || laserContact)
        {
            visibleAccuracy *= GetType()->_sensitivity;
        }
        else if (cosFiWep > cosOpt)
        {
            visibleAccuracy *= GetType()->_sensitivity * opticsZoom * nightAccCoef; // optics
        }
        else if (cosFiEye > cosEyeFocus)
        {
            visibleAccuracy *= GetType()->_sensitivity * nightAccCoef;
        }
        else if (cosFiEye > cosEyeAdjusted)
        {
            visibleAccuracy *= GetType()->_sensitivity * 0.5f * nightAccCoef;
        }
        else
        {
            visibleAccuracy = 0;
        }

        audibleAccuracy *= sensitivityEar * aiAudible * 0.25;

        // real side can only be recognized at accuracy >= 1.5; hearing alone never reaches it
        float audibleSideAccuracy = floatMin(audibleAccuracy * 0.5f, 1.4f);
        float visibleSideAccuracy = floatMin(visibleAccuracy * 0.15f, 4);

        saturateMin(visibleAccuracy, 4);
        saturateMin(audibleAccuracy, 4);

        float fadingAccuracy = target->FadingAccuracy();
        float fadingSideAccuracy = target->FadingSideAccuracy();
        float sensorAccuracy = floatMax(audibleAccuracy, visibleAccuracy);
        if (initialize)
        {
            if (center->IsFriendly(ai->GetTargetSide()))
            {
                sensorAccuracy = 4;
                visibleAccuracy = 4;
                audibleAccuracy = 4;
                if (ai->CommanderUnit())
                {
                    saturateMax(visibleSideAccuracy, 1.6);
                    saturateMax(audibleSideAccuracy, 1.6);
                }
            }
        }
        if (sensorAccuracy > fadingAccuracy)
        {
            target->accuracy = sensorAccuracy;
            target->accuracyTime = Glob.time;
            fadingAccuracy = sensorAccuracy;
        }

        if (target->destroyed)
        {
            // destroyed units are civilian until respawn; sideChecked=false lets them re-classify
            target->side = TCivilian;
            target->sideChecked = false;
        }
        else
        {
            float sensorSideAccuracy = floatMax(audibleSideAccuracy, visibleSideAccuracy);
            if (sensorSideAccuracy >= fadingSideAccuracy)
            {
                TargetSide tSide = ai->GetTargetSide(sensorSideAccuracy);
                bool checked = sensorSideAccuracy >= 1.5f;
                if (center->IsFriendly(tSide) || tSide == TCivilian)
                {
                    if (sensorSideAccuracy >= 1.35f && center->IsEnemy(ai->GetTargetSide()))
                    {
                        // stolen vehicle moving in friendly markings — treat as unknown until side is confirmed
                        tSide = TSideUnknown;
                        checked = true;
                    }
                }
                target->side = tSide;
                target->sideChecked = checked;
                target->sideAccuracy = sensorSideAccuracy;
                target->sideAccuracyTime = Glob.time;
                fadingSideAccuracy = sensorSideAccuracy;
            }
        }
        float spotability = target->FadingSpotability();

        const VehicleType* aiType = ai->GetType();
        float spotCoef = 1;
        float spotError = 0;
        float lightCoef = 1;

        if (!radarContact && !laserContact && night > 0)
        {
            float lightsOnOff = ai->VisibleLights();
            float nightSpotable = (aiType->GetSpotableNightLightsOff() * (1 - lightsOnOff) +
                                   aiType->GetSpotableNightLightsOn() * lightsOnOff);
            lightCoef = nightSpotable * night + (1 - night);
        }
        float delayCoef = 1;
        float hidden = ai->GetHidden(); // 1 = fully visible; lower = concealed
        // camouflage pattern is less effective at close range
        const float patternFullDistance = 60;
        if (dist2 < Square(patternFullDistance) && hidden < 1)
        {
            float dist = dist2 * InvSqrt(dist2);
            float patternFactor = dist / patternFullDistance;
            float moveFactor = movement;
            saturate(moveFactor, 0, 1);
            patternFactor = 1 - (1 - patternFactor) * moveFactor;
            hidden = 1 - (1 - hidden) * patternFactor;
        }

        float visibleFire = visibility * ai->VisibleFire();
        float audibleFire = audibility * ai->AudibleFire() * 0.3f;

        if (radarContact || laserContact)
        {
            spotCoef = GetType()->_sensitivity * 8;
            delayCoef = 0.5f;
        }
        else if (cosFiWep > cosOpt)
        {
            float spotOptics = floatMax(opticsZoom * 0.5, 1);
            spotCoef = movement * spotOptics * GetType()->_sensitivity;
            spotCoef *= lightCoef;
            spotCoef *= hidden;
            delayCoef = 1.0f;
        }
        else if (cosFiEye > cosEyeAdjusted)
        {
            spotCoef = movement * GetType()->_sensitivity;
            spotCoef *= lightCoef;
            spotCoef *= hidden;
            delayCoef = 1.5f;
        }
        else
        {
            spotCoef = 0;
            visibleFire = 0;
        }

        actSpotabilityEye *= spotCoef;
        actSpotabilityEar *= aiAudible * sensitivityEar;

        float actSpotability = actSpotabilityEye;
        if (actSpotability < 1 && actSpotability < actSpotabilityEar)
        {
            spotError = ai->Position().Distance(Position()) * 0.8;
            delayCoef = 2.5f;
            actSpotability = actSpotabilityEar;
            hidden = 1;
        }

        saturateMin(actSpotability, 4);
        float newSpotability = actSpotability;

        float newSideAccuracy = fadingSideAccuracy;

        float sensorFire = floatMax(visibleFire, audibleFire);
        if (sensorFire > 0.8f)
        {
            TargetType* fireTarget = ai->FiredAt();
            int index = res.Find(fireTarget);
            if (index >= 0)
            {
                Target* fireTVis = res[index];
                saturateMax(newSideAccuracy, fireTVis->FadingAccuracy());
                saturateMax(newSideAccuracy, fireTVis->FadingSideAccuracy());
            }
            if (unit->GetCombatMode() < CMCombat && group != ai->GetGroup() && !group->IsAnyPlayerGroup())
            {
                if (!unit->IsDanger())
                {
                    unit->SetDanger();
                }
            }
        }

        if (newSideAccuracy > fadingSideAccuracy)
        {
            target->side = ai->GetTargetSide(newSideAccuracy);
            target->sideChecked = true;
            target->sideAccuracy = newSideAccuracy;
            target->sideAccuracyTime = Glob.time;
            fadingSideAccuracy = newSideAccuracy;
        }

        target->type = ai->GetType(fadingAccuracy);
        if (!target->sideChecked && !target->destroyed)
        {
            TargetSide side = target->type->_typicalSide;
            if (side != TSideUnknown)
            {
                AIUnit* aiUnit = ai->CommanderUnit();
                if (aiUnit)
                {
                    if (aiUnit->GetCaptive())
                    {
                        side = TCivilian;
                    }
                    else
                    {
                        TargetSide realSide = ai->GetTargetSide(4);
                        if (center->IsFriendly(realSide))
                        {
                            side = realSide;
                        }
                    }
                }
                else
                {
                    if (!ai->EngineIsOn() && center->IsEnemy(side))
                    {
                        // empty vehicle with no engine — classify as civilian so AI ignores it
                        side = TCivilian;
                    }
                }
                target->side = side;
            }
        }
        if (!target->destroyed && ai->IsDammageDestroyed())
        {
            target->destroyed = true;
            target->timeReported = TIME_MIN;
            target->posReported = VZero;
            target->side = TCivilian;
            target->sideChecked = false;
        }

        if (center->IsEnemy(target->side) && target->isKnown && target->delay < Glob.time && !target->destroyed &&
            !target->vanished)
        {
            if (Position().Distance2(target->position) < Square(20 * VisibleSize()) && ai->CommanderUnit() &&
                center->IsEnemy(ai->GetTargetSide()))
            {
                if (unit->IsLocal() && !unit->IsPlayer())
                {
                    unit->Disclose();
                }
            }
        }

        float myDelay = 1.0f * GetInvAbility() * delayCoef;

        if (radarContact || laserContact)
        {
            if (dist2 < Square(1000))
            {
                float maxTime = dist2 * 0.000003f + 0.6f * GetInvAbility();
                saturateMin(myDelay, maxTime);
            }
        }
        else
        {
            if (dist2 < Square(100))
            {
                if (spotError < 1)
                {
                    float maxTime = dist2 * 0.0002f + 0.02f * GetInvAbility();
                    saturateMin(myDelay, maxTime);
                }
                else
                {
                    float maxTime = dist2 * 0.001f + 0.1f * GetInvAbility();
                    saturateMin(myDelay, maxTime);
                }
            }
        }

        if (target->isKnown)
        {
            if (initialize)
            {
                target->delay = Glob.time;
                target->delaySensor = Glob.time;
                myDelay = 0;
            }
            else if (newSpotability > target->FadingSpotability())
            {
                Target* assigned = unit->GetTargetAssigned();
                if (assigned && assigned->IsKnownBy(unit))
                {
                    myDelay *= 3;
                }
                if (notManual)
                {
                    Time newDelay = Glob.time + myDelay;
                    if (newDelay < target->delaySensor)
                    {
                        target->delaySensor = newDelay;
                        target->idSensor = unit->GetPerson();
                    }
                }
            }
        }
        else
        {
            if (newSpotability > spotTreshold)
            {
                target->posError = Vector3(GRandGen.RandomValue() * spotError * 2 - spotError,
                                           GRandGen.RandomValue() * spotError * 0.2f - spotError * 0.1f,
                                           GRandGen.RandomValue() * spotError * 2 - spotError);

                target->isKnown = true;
                target->position = ai->AimingPosition();
                target->speed = ai->Speed();

                float groupDelay = 2.5f * myDelay + 2.5f;
                if (assigned && assigned->IsKnownBy(unit))
                {
                    myDelay *= 3;
                }

                saturateMin(groupDelay, myDelay + 10.0f);
                target->delaySensor = Glob.time + myDelay;
                target->delay = Glob.time + groupDelay;
                target->idSensor = unit->GetPerson();
            }
        }

        if (actSpotability > 0.05)
        {
            target->LimitError(spotError);
        }

        if (target->isKnown && actSpotability > 0.05)
        {
            target->position = ai->AimingPosition();
            target->speed = ai->Speed();
            AIUnit* aiUnit = ai->CommanderUnit();
            if (notManual || !aiUnit || aiUnit->GetLifeState() != AIUnit::LSDead)
            {
                // player skips this so dead bodies aren't marked as recently seen — AI updates regardless
                target->lastSeen = Glob.time + trackTargetsPeriod;
            }

            if (target->destroyed ||
                aiUnit && aiUnit->GetLifeState() == AIUnit::LSDead && (notManual || USER_CONFIG.IsEnabled(DTAutoSpot)))
            {
                if (aiUnit && aiUnit->GetGroup() == group)
                {
                    AIUnit::LifeState ls = aiUnit->GetLifeState();

                    if (ls == AIUnit::LSDead || ls == AIUnit::LSDeadInRespawn)
                    {
                        if (unit->IsLocal())
                        {
                            if (aiUnit->IsLocal() || unit->IsGroupLeader())
                            {
                                group->SendUnitDown(unit, aiUnit);
                            }
                        }
                    }
                }
            }
        }
        if (newSpotability > spotability)
        {
            target->spotability = newSpotability;
            target->spotabilityTime = Glob.time;
        }
        float minSpotability = !notManual ? 0.01f : 0.06f;
        if (target->FadingSpotability() < minSpotability)
        {
            if (target->lastSeen < Glob.time - 120)
            {
                target->isKnown = false;
            }
        }

        if (target->isKnown && target->delay <= Glob.time && target->lastSeen < Glob.time - 10 &&
            target->lastSeen > TIME_MIN)
        {
            float sinceLastSeen = Glob.time - target->lastSeen;
            float minRadius = sinceLastSeen;
            if (target->posError.SquareSize() < Square(minRadius))
            {
                float radiusXZ = minRadius * 2;
                float radiusY = minRadius * 0.2;
                target->posError = Vector3(GRandGen.RandomValue() * radiusXZ - radiusXZ * 0.5,
                                           GRandGen.RandomValue() * radiusY - radiusY * 0.5,
                                           GRandGen.RandomValue() * radiusXZ - radiusXZ * 0.5);
                if (target->posError.SquareSize() < Square(minRadius * 1.5))
                {
                    target->posError.Normalize();
                    target->posError *= minRadius * 1.5;
                }
            }
        }

        if (center->IsEnemy(target->side) && target->isKnown &&
            (ai->GetType()->GetIRTarget() || !ai->GetType()->GetLaserTarget()))
        {
            float dist2 = target->position.Distance2(Position());
            saturateMin(nearestEnemy2, dist2);
        }
    }

    for (int i = 0; i < center->NTargets(); i++)
    {
        const AITargetInfo& target = center->GetTarget(i);
        if (center->IsEnemy(target._side))
        {
            EntityAI* ai = target._idExact;
            if (ai && (ai->GetType()->GetIRTarget() || !ai->GetType()->GetLaserTarget()))
            {
                float dist2 = target._realPos.Distance2Inline(Position());
                saturateMin(nearestEnemy2, dist2);
            }
        }
    }
    _nearestEnemy = sqrt(nearestEnemy2);
}

void EntityAI::TrackTargets(TargetList& res, bool initialize, float trackTargetsPeriod)
{
    const VehicleType* type = GetType();
    AIUnit* unit = CommanderUnit();
    if (unit)
    {
        TrackTargets(res, unit, type->_commanderCanSee, initialize, 1e10, trackTargetsPeriod);
    }

    _trackTargetsTime = Glob.time;
}

void EntityAI::TrackNearTargets(TargetList& res)
{
    const VehicleType* type = GetType();
    AIUnit* unit = CommanderUnit();
    if (unit)
    {
        TrackTargets(res, unit, type->_commanderCanSee, false, 100, 0);
    }

    _trackNearTargetsTime = Glob.time;
}

void EntityAI::WhatIsVisible(TargetList& res, bool initialize)
{
    if (initialize)
    {
        AddNewTargets(res, initialize);
        TrackTargets(res, initialize, 0);
        return;
    }

    AIUnit* unit = CommanderUnit();
    if (!unit)
    {
        return;
    }

    float newTargetsPeriod = 1.0;
    float trackTargetsPeriod = 1.0;
    if (GetType()->_irScanRangeMax <= 0)
    {
        newTargetsPeriod = 5.0;
    }

    float trackRange = floatMax(GetType()->GetIRScanRange(), TACTICAL_VISIBILITY);
    float trackCoef = 1;
    if (unit->GetNearestEnemyDist2() > Square(trackRange))
    {
        trackCoef = 5;
    }

    trackTargetsPeriod *= trackCoef;
    newTargetsPeriod *= trackCoef;

    if (Glob.time > _newTargetsTime + newTargetsPeriod)
    {
        AddNewTargets(res, initialize);
    }
    if (Glob.time > _trackTargetsTime + trackTargetsPeriod)
    {
        TrackTargets(res, initialize, trackTargetsPeriod);
    }
}

VisibilityTracker::VisibilityTracker() = default;

VisibilityTracker::VisibilityTracker(EntityAI* obj) : _obj(obj), _lastTime(Glob.time - 60) {}

VisibilityTracker::~VisibilityTracker() = default;

float VisibilityTracker::Value(const EntityAI* sensor, int weapon, float reserve, float maxDelay)
{
    if (!_obj)
    {
        LOG_DEBUG(AI, "VisibilityTracker expired");
        return _lastValue;
    }
    if (Glob.time - _lastTime > maxDelay)
    {
        _lastTime = Glob.time;

        EntityAI* ai = _obj;
        float irRange = sensor->GetType()->GetIRScanRange();
        if (!ai->GetType()->GetIRTarget())
        {
            irRange = 0;
        }
        else if (!sensor->GetType()->GetIRScanGround() && !ai->Airborne())
        {
            irRange = 0;
        }
        float maxDistance2 = Square(floatMax(irRange, TACTICAL_VISIBILITY));
        float visible = 0;
        if (sensor->Position().Distance2(ai->Position()) < maxDistance2)
        {
            Time lastVisTime = GWorld->VisibilityTime(sensor->CommanderUnit(), ai);
            if (lastVisTime >= Glob.time - 30)
            {
                Vector3 weaponPos;
                if (weapon < 0)
                {
                    weaponPos = sensor->CameraPosition();
                }
                else
                {
                    weaponPos = sensor->PositionModelToWorld(sensor->GetWeaponPoint(weapon));
                }
                visible = GLandscape->Visible(weaponPos, sensor, ai, reserve, ObjIntersectFire);
            }
        }

        _lastValue = visible;
        return visible;
    }
    return _lastValue;
}

VisibilityTrackerCache::VisibilityTrackerCache() = default;

void VisibilityTrackerCache::Clear()
{
    _trackers.Clear();
}

VisibilityTrackerCache::~VisibilityTrackerCache() = default;

float VisibilityTrackerCache::KnownValue(const EntityAI* sensor, int weapon, EntityAI* obj, float reserve,
                                         float maxDelay)
{
    DoAssert(obj);
    const float discardAfter = 1.0;
    PoseidonAssert(maxDelay < discardAfter);
    VisibilityTracker* found = nullptr;
    for (int i = 0; i < _trackers.Size(); i++)
    {
        VisibilityTracker& tracker = _trackers[i];
        if (tracker._obj == obj)
        {
            if (Glob.time - tracker._lastTime <= maxDelay)
            {
                return tracker._lastValue;
            }
            found = &tracker;
        }
        else if (Glob.time - tracker._lastTime > discardAfter || !tracker._obj)
        {
            _trackers.Delete(i), i--;
        }
    }
    if (found)
    {
        return found->Value(sensor, weapon, reserve, maxDelay);
    }
    if (_trackers.Size() <= 4)
    {
        VisibilityTracker& tracker = _trackers[_trackers.Add(obj)];
        return tracker.Value(sensor, weapon, reserve, maxDelay);
    }
    return -1;
}

float VisibilityTrackerCache::Value(const EntityAI* sensor, int weapon, EntityAI* obj, float reserve, float maxDelay)
{
    DoAssert(obj);
    const float discardAfter = 1.0;
    PoseidonAssert(maxDelay < discardAfter);
    for (int i = 0; i < _trackers.Size(); i++)
    {
        VisibilityTracker& tracker = _trackers[i];
        if (tracker._obj == obj)
        {
            if (Glob.time - tracker._lastTime <= maxDelay)
            {
                return tracker._lastValue;
            }
        }
        else if (Glob.time - tracker._lastTime > discardAfter || !tracker._obj)
        {
            _trackers.Delete(i), i--;
        }
    }
    VisibilityTracker& tracker = _trackers[_trackers.Add(obj)];
    return tracker.Value(sensor, weapon, reserve, maxDelay);
}

inline float MaxProbability(const AmmoType& aInfo)
{
    return floatMax(floatMax(aInfo.midRangeProbab, aInfo.minRangeProbab), aInfo.maxRangeProbab);
}

static float BestDistance(const AmmoType& aInfo)
{
    if (aInfo.midRangeProbab > aInfo.minRangeProbab)
    {
        if (aInfo.maxRangeProbab > aInfo.midRangeProbab)
        {
            return aInfo.maxRange;
        }
        return aInfo.midRange;
    }
    return aInfo.minRange;
}

static float BestDistance(const AmmoType& aInfo, float& propab)
{
    if (aInfo.midRangeProbab > aInfo.minRangeProbab)
    {
        if (aInfo.maxRangeProbab > aInfo.midRangeProbab)
        {
            propab = aInfo.maxRangeProbab;
            return aInfo.maxRange;
        }
        propab = aInfo.midRangeProbab;
        return aInfo.midRange;
    }
    propab = aInfo.minRangeProbab;
    return aInfo.minRange;
}

static float HitProbability(float dist, const AmmoType& aInfo)
{
    if (dist < aInfo.midRange)
    {
        if (dist < aInfo.minRange)
        {
            return 0;
        }
        return ((dist - aInfo.minRange) * aInfo.invMidRangeMinusMinRange + aInfo.minRangeProbab);
    }
    else
    {
        if (dist > aInfo.maxRange)
        {
            return 0;
        }
        return ((dist - aInfo.maxRange) * aInfo.invMidRangeMinusMaxRange + aInfo.maxRangeProbab);
    }
}

bool EntityAI::WhatShootResult(FireResult& result, const Target& target, int weapon, float inRange, float timeToAim,
                               float timeToLive, float visible, float dist, float timeToShoot,
                               bool considerIndirect) const
{
    if (!IsAbleToFire())
    {
        return false;
    }

    if (target.destroyed)
    {
        return false;
    }
    if (target.vanished)
    {
        return false;
    }

    EntityAI* tgt = target.idExact;
    if (!tgt)
    {
        return false;
    }

#if DIAG_SHOOT_RESULT
    LOG_DEBUG(AI, "   WhatShootResult {} to {} (with {})", (const char*)GetDebugName(),
              (const char*)target.type->GetName(), weapon);
#endif

    const Magazine* magazine = GetMagazineSlot(weapon)._magazine;
    if (!magazine)
    {
        return false;
    }
    const MagazineType* ammoInfo = magazine->_type;
#if DIAG_SHOOT_RESULT
    LOG_DEBUG(AI, "    weapon name {}", (const char*)ammoInfo->GetName());
#endif
    PoseidonAssert(ammoInfo);
    const WeaponModeType* mode = GetWeaponMode(weapon);
    PoseidonAssert(mode);
    const AmmoType* aInfo = mode->_ammo;
    if (!aInfo)
    {
        return false;
    }

    if (magazine->_ammo <= 0)
    {
#if DIAG_SHOOT_RESULT
        LOG_DEBUG(AI, "    x: no ammo.");
#endif
        return false;
    }

    if (dist > aInfo->maxRange || dist < aInfo->minRange)
    {
#if DIAG_SHOOT_RESULT
        LOG_DEBUG(AI, "    x: distance ({:.1f} <> {:.1f}..{:.1f})", dist, aInfo->minRange, aInfo->maxRange);
#endif
        return false;
    }
    EntityAI* ai = tgt;

    if (!ai->LockPossible(aInfo))
    {
#if DIAG_SHOOT_RESULT
        LOG_DEBUG(AI, "    x: cannot lock.");
#endif
        return false;
    }
    float myCost = GetType()->_cost;
    float mySecondCost = myCost / timeToLive;
    result.cost = aInfo->cost;

    saturateMin(timeToShoot, 2 * 60);

    result.loan = timeToAim * mySecondCost;
    float hitProbab = HitProbability(dist, *aInfo);
    if (visible < MinVisibleFire)
    {
#if DIAG_SHOOT_RESULT
        LOG_DEBUG(AI, "    x: visibility {:.3f}", visible);
#endif
        return false;
    }
    else
    {
        hitProbab *= Square(visible);
    }
    if (hitProbab < 0.05)
    {
#if DIAG_SHOOT_RESULT
        LOG_DEBUG(AI, "    x: hit probability {:.3f}", hitProbab);
#endif
        return false;
    }

    hitProbab *= inRange;

    LODShape* tgtShape = target.type->GetShape();
    float tgtRadius = tgtShape ? tgtShape->GeometrySphere() : 3;

    float ammoSpeed = floatMax(floatMax(ammoInfo->_initSpeed, aInfo->maxSpeed), 0.5);
    if (Square(ammoInfo->_maxLeadSpeed) < target.speed.SquareSize())
    {
        float tgtSpeed = target.speed.Size();
        float leadMiss = tgtSpeed - ammoInfo->_maxLeadSpeed;
        float leadError = (dist / ammoSpeed + 0.3f) * leadMiss;        // time to fly * speed error
        float maxError = (aInfo->indirectHitRange + tgtRadius) * 0.3f; // max distance to ignore lead error
        if (leadError > maxError)
        {
            float considerLead = maxError / leadError; // 0 <= considerLead <= 1
#if DIAG_SHOOT_RESULT
            LOG_DEBUG(AI, "    lead {:.1f}, spd {:.1f}, error {:.1f}, max {:.1f}, factor {:.4f}",
                      ammoInfo->_maxLeadSpeed, tgtSpeed, leadError, maxError, considerLead);
#endif
            if (considerLead < 0.2)
            {
#if DIAG_SHOOT_RESULT
                LOG_DEBUG(AI, "    x: lead {:.3f}", considerLead);
#endif
                return false;
            }
            hitProbab *= considerLead;
        }
    }
    if (target.speed.SquareSize() > Square(0.5))
    {
        saturateMax(ammoSpeed, 2); // minimum is crawling speed; avoids div-by-near-zero
        float considerSpeed = ammoSpeed * target.speed.InvSize() * 0.03f;
        saturate(considerSpeed, 0, 1);
#if DIAG_SHOOT_RESULT
        LOG_DEBUG(AI, "    speed {:.1f}, ammo {:.1f}, factor {:.4f}", target.speed.Size(), ammoSpeed, considerSpeed);
#endif
        hitProbab *= considerSpeed;
    }
    float danger = 0;
    if (tgt->GetTotalDammage() < MaxDammageWorking)
    {
        const float predictTime = 2.0f;
        float distPredict = dist - GetType()->GetTypSpeedMs() * predictTime;
        float dist2 = Square(floatMax(distPredict, 0));
        Threat threat = target.type->GetDammagePerMinute(dist2, visible);
        VehicleKind myKind = GetType()->GetKind();
        float dammagePerMinute = threat[myKind];
        if (dammagePerMinute > 0)
        {
            float lTimeToLive = GetArmor() * 60 / dammagePerMinute;
            if (lTimeToLive < 2 * 3600)
            {
                danger = timeToLive / lTimeToLive;
#if DIAG_SHOOT_RESULT
                LOG_DEBUG(AI, "    TTL {:.2f}, L TTL {:.2f}, raw danger {:.2f}, dpm {:.2f}", timeToLive, lTimeToLive,
                          danger, dammagePerMinute);
#endif
                saturateMin(danger, 10);
                danger *= myCost;
            }
        }
    }

    float targetInvArmor = target.type->GetInvArmor();
    float relStrength = aInfo->hit * targetInvArmor;
    if (relStrength < 0.1)
    {
#if DIAG_SHOOT_RESULT
        LOG_DEBUG(AI, "    x: armor strength {:.4f}", relStrength);
#endif
        return false;
    }
    float changeDammage = Square(aInfo->hit) * targetInvArmor * Square(0.27f / tgtRadius);

    if (changeDammage < 0.004)
    {
#if DIAG_SHOOT_RESULT
        LOG_DEBUG(AI, "    x: dammage change {:.4f}", changeDammage);
#endif
        return false;
    }
    saturateMin(changeDammage, 4);
    changeDammage *= hitProbab;

    float targetCost = target.type->GetCost() + danger;
    timeToShoot -= timeToAim;
    float cumChangeDammage = changeDammage;
    if (result.dammage + cumChangeDammage < 1 && timeToShoot >= mode->_reloadTime)
    {
        int maxShots = mode->_reloadTime > 0 ? toIntFloor(timeToShoot / mode->_reloadTime) : 10000;
        saturateMin(maxShots, magazine->_ammo - 1);
        float fNeedShots = (1 - result.dammage) / changeDammage - 1;
        saturateMin(fNeedShots, maxShots);
        int needShots = toIntCeil(fNeedShots);
        cumChangeDammage += changeDammage * needShots;
        result.cost += aInfo->cost * needShots;
        result.loan += mode->_reloadTime * mySecondCost * needShots;
#if DIAG_SHOOT_RESULT
        LOG_DEBUG(AI, "    WhatShootResult: needShots {}, cumChangeDammage {:.2f}", needShots, cumChangeDammage);
#endif
    }
    saturateMin(cumChangeDammage, 1.01f - result.dammage);
    float oldDammage = result.dammage;
    result.dammage += cumChangeDammage;
    if (result.dammage >= 1)
    {
        result.gain += targetCost * 0.5f;
        result.dammage = 1;
    }
    result.gain = (result.dammage - oldDammage) * targetCost * 0.5f;
    result.weapon = weapon;
#if DIAG_SHOOT_RESULT
    LOG_DEBUG(AI, "    probab {:.4f}, inRange {:.2f}, visible {:.3f}, danger {:.2f}, time {:.2f}", hitProbab, inRange,
              visible, danger, timeToShoot);
    LOG_DEBUG(AI, "    {:c}: dammage {:.4f}, gain {:.0f}, cost {:.0f}, loan {:.0f}, weapon {}",
              result.gain > 0 ? '#' : 'x', result.dammage, result.gain, result.cost, result.loan, result.weapon);
#endif
    if (considerIndirect && result.gain > 0)
    {
        float ihRange = aInfo->indirectHitRange;
        float secRange = ihRange * aInfo->indirectHit * (1.0 / 20);
        if (secRange >= 4)
        {
#define DIAG_SEC 0
            AIUnit* unit = CommanderUnit();
            if (!unit)
            {
                return false;
            }
            AIGroup* group = GetGroup();
            AICenter* center = group->GetCenter();
            Vector3 secCenter = target.AimingPosition();

            const TargetList& tgt = group->GetTargetList();
            for (int i = 0; i < tgt.Size(); i++)
            {
                const Target* tgtI = tgt[i];
                if (tgtI->vanished)
                {
                    continue;
                }
                if (tgtI->destroyed)
                {
                    continue;
                }
                if (tgtI->idExact == target.idExact)
                {
                    continue;
                }
                float dist2 = tgtI->AimingPosition().Distance2(secCenter);
                if (dist2 >= Square(secRange))
                {
                    continue;
                }
                if (!tgtI->IsKnownBy(unit))
                {
                    continue;
                }
                TargetSide side = tgtI->side;
                float coef = 1;
                if (center->IsEnemy(side))
                {
                    coef = 1;
                }
                else if (center->IsFriendly(side))
                {
                    coef = -6;
                }
                else if (side == TCivilian)
                {
                    coef = -6;
                }
                else if (side == TSideUnknown)
                {
                    coef = -0.5;
                }
                const VehicleType* typeI = tgtI->type;
                float rangeCoef = dist2 < Square(ihRange) ? 1 : Square(ihRange) / dist2;
                float dammage = aInfo->indirectHit * typeI->GetInvArmor() * rangeCoef;
                saturate(dammage, 0, 1);
                if (coef > 0)
                {
                    result.gain += dammage * coef * typeI->GetCost();
                }
                else
                {
                    result.cost += dammage * -coef * typeI->GetCost();
                }
            }
        }
    }
    return result.gain > 0;
}

inline void UseMax(int& val, int v)
{
    if (val < v)
    {
        val = v;
    }
}

const float TimeToAttack = 5;
const float MinTimeToLive = 30;

} // namespace Poseidon
