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
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

#if _ENABLE_CHEATS

#define DIAG_RESULT 0
#define DIAG_SHOOT_RESULT 0
#define DIAG_ATTACK 0

#define DIAG_TARGET 0

#endif

namespace Poseidon
{
static const float MinVisibleFire = 0.63f;
static const float TimeToAttack = 5;

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

int EntityAI::EstimateAttack(const Vector3& hPos, float height, const EntityAI* who) const
{
#if DIAG_ATTACK
    LOG_DEBUG(AI, "Est attack at {:.1f},{:.1f} - {:.1f}", hPos.X(), hPos.Z(), hPos.Y());
    LOG_DEBUG(AI, "  econom {:.0f}", _attackEconomicalResult.Surplus());
    LOG_DEBUG(AI, "  aggres {:.0f}", _attackAggresiveResult.CleanSurplus());
#endif
    Matrix4 transform; // predict what position will the vehicle have

    Vector3 hePos = hPos;
    Ref<AIUnit> unit = CommanderUnit();
    if (!unit)
    {
        return 0;
    }
    unit->FindNearestEmpty(hePos);

    float dx, dz;
    float posY = GLandscape->RoadSurfaceYAboveWater(hePos.X(), hePos.Z(), &dx, &dz);
    Vector3 normal(-dx, 1, -dz);
    if (who->GetType()->GetKind() == VAir)
    {
        normal = VUp;
    }
    transform.SetPosition(Vector3(hePos.X(), posY + height, hePos.Z()));
    Vector3Val pos = transform.Position();
    Vector3Val tgtPos = _attackTarget->LandAimingPosition();

    Vector3Val tgtRel = tgtPos - pos;
    transform.SetUpAndDirection(normal, tgtRel);
    Matrix4 invTransform(MInverseRotation, transform);

    int ix = toIntFloor(pos.X() * InvLandGrid), iz = toIntFloor(pos.Z() * InvLandGrid);
    GeographyInfo info = GLOB_LAND->GetGeography(ix, iz);
    float typicalCost = who->GetCost(info);
    if (typicalCost > 1e10)
    {
#if DIAG_ATTACK
        LOG_DEBUG(AI, "Field unreachable.");
#endif
        return false;
    }

    int improved = 0;
    float bestDist, minRange, maxRange;
    FireResult bestFire;
    who->BestFireResult(bestFire, *_attackTarget, bestDist, minRange, maxRange, 30, true);
    float dist2 = tgtRel.SquareSize();
    if (dist2 > Square(minRange) && dist2 < Square(maxRange))
    {
        EntityAI* ai = _attackTarget->idExact;
        float irRange = GetType()->GetIRScanRange();
        if (!ai)
        {
            return 0;
        }
        if (!ai->GetType()->GetIRTarget())
        {
            irRange = 0;
        }
        else if (!GetType()->GetIRScanGround() && !ai->Airborne())
        {
            irRange = 0;
        }

        float maxDistance2 = Square(floatMax(irRange, TACTICAL_VISIBILITY));
        if (dist2 > maxDistance2)
        {
            return false; // too far to see it - cannot fire
        }

        float inRange = who->FireAngleInRange(0, invTransform * tgtPos);
        if (inRange < 0.5)
        {
            goto Done;
        }

        Vector3Val weaponPos = transform * GetWeaponCenter(0);
        float visible = GLOB_LAND->Visible(weaponPos, who, ai, 1.5);

        improved |= EstVisibility;

#if DIAG_ATTACK
        LOG_DEBUG(AI, "  est attack vis {:.2f}", visible);
#endif
        if (visible < MinVisibleFire)
        {
#if DIAG_ATTACK
            LOG_DEBUG(AI, "Est attack failed");
#endif
            return improved; // not suitable for firing
        }

        AIGroup* grp = unit->GetGroup();
        if (!grp)
        {
#if DIAG_ATTACK
            LOG_DEBUG(AI, "Est attack failed - no group");
#endif
            return improved; // not suitable for firing
        }
        AICenter* center = grp->GetCenter();
        float distance = pos.Distance(Position());
        float exposure = center->GetExposureOptimistic(pos);
        float timeToLive = 120;
        if (exposure > 0)
        {
            timeToLive = 120 * who->GetType()->GetArmor() / exposure;
            saturate(timeToLive, 5, 120);
        }

        float timeToAim = TimeToAttack + distance * typicalCost;
#if DIAG_ATTACK
        LOG_DEBUG(AI, "  exposure {:.1f}", exposure);
        LOG_DEBUG(AI, "  timeToLive {:.1f}", timeToLive);
        LOG_DEBUG(AI, "  distance {:.1f}", distance);
        LOG_DEBUG(AI, "  timeToAim {:.1f}", timeToAim);
#endif
        float dist = dist2 * InvSqrt(dist2);
        for (int w = 0; w < who->NMagazineSlots(); w++)
        {
            FireResult result;
#if DIAG_ATTACK
            LOG_DEBUG(AI, "  weapon {} WhatShootResult", w);
#endif
            if (who->WhatShootResult(result, *_attackTarget, w, inRange, timeToAim, timeToLive, visible, dist,
                                     30 + timeToAim, false))
            {
#if DIAG_ATTACK
                LOG_DEBUG(AI, "  est attack shoot {}", result.weapon);
                LOG_DEBUG(AI, "  sursplus {:.1f}", result.Surplus());
                LOG_DEBUG(AI, "  clean sursplus {:.1f}", result.CleanSurplus());
                LOG_DEBUG(AI, "  gain {:.1f}, cost {:.1f}, loan {:.1f}", result.gain, result.cost, result.loan);
#endif
                if (this != who)
                {
                    result.weapon = -1;
                }

                const float conservative = 0.7f;
                if (_attackEconomicalResult.Surplus() < result.Surplus() * conservative)
                {
                    _attackEconomicalResult = result;
                    _attackEconomicalPos = transform.Position();
                    improved |= EstImproved;
#if DIAG_ATTACK
                    LOG_DEBUG(AI, "  est attack econom improved {:.0f}, gain {:.0f}, cost {:.0f}, loan {:.0f}",
                              result.Surplus(), result.gain, result.cost, result.loan);
#endif
                }
                if (_attackAggresiveResult.CleanSurplus() < result.CleanSurplus() * conservative)
                {
                    _attackAggresiveResult = result;
                    _attackAggresivePos = transform.Position();
                    improved |= EstImproved;
#if DIAG_ATTACK
                    LOG_DEBUG(AI, "  est attack aggres improved {:.0f}, gain {:.0f}, cost {:.0f}, loan {:.0f}",
                              result.CleanSurplus(), result.gain, result.cost, result.loan);
#endif
                }
            }
            else
            {
#if DIAG_ATTACK
                LOG_DEBUG(AI, "  impossible");
#endif
            }
        }
    }
    else
    {
#if DIAG_ATTACK
        LOG_DEBUG(AI, "  out of range");
#endif
    }
Done:
#if 1
    if (!(improved & EstImproved) && who->GetType()->HasCargo())
    {
        PoseidonAssert(dyn_cast<const Transport>(who));
        const Transport* transport = static_cast<const Transport*>(who);
        const ManCargo& cargo = transport->GetManCargo();
        for (int c = 0; c < cargo.Size(); c++)
        {
            Person* soldier = cargo[c];
            if (!soldier)
            {
                continue;
            }
            if (soldier->GetGroup() != GetGroup())
            {
                continue;
            }
            improved |= EstimateAttack(pos, 1.1f, soldier);
            break;
        }
    }
#endif
#if DIAG_ATTACK
    LOG_DEBUG(AI, "Est attack done");
#endif
    return improved;
}

int EntityAI::EstimateAttack(const Vector3& hPos, float height) const
{
    return EstimateAttack(hPos, height, this);
}

void EntityAI::BegAttack(Target* target)
{
    PoseidonAssert(target);
    if (!_attackTarget || _attackTarget->idExact != target->idExact)
    {
        _attackTarget = target;
#if DIAG_ATTACK
        LOG_DEBUG(AI, "Engage target.");
#endif
        FireResult bestFire;
        float bestDist, minDist, maxDist;
        BestFireResult(bestFire, *target, bestDist, minDist, maxDist, 30, true);
        Vector3Val tgtPos = _attackTarget->LandAimingPosition();
        Vector3Val relTgt = tgtPos - Position();
        Vector3Val dir = relTgt.Normalized();
        float dist = dir * relTgt;
        if (minDist > maxDist || dist < maxDist)
        {
            _attackAggresivePos = (Position() + tgtPos) * 0.5;
            _attackEconomicalPos = Position();
        }
        else
        {
            _attackEconomicalPos = tgtPos - dir * maxDist;
            _attackAggresivePos = tgtPos - dir * (maxDist + minDist) * 0.5;
        }
        _attackEngageTime = Glob.time;
        _attackRefreshTime = Glob.time - 60;
        _attackEconomicalResult.Reset();
        _attackAggresiveResult.Reset();
        EstimateAttack(Position(), GetCombatHeight());
    }
    _attackTarget = target;
}

void EntityAI::EndAttack()
{
    _attackTarget = nullptr;
    _attackEconomicalResult.Reset();
    _attackAggresiveResult.Reset();
    _attackAggresivePos = VZero;
    _attackEconomicalPos = VZero;
}

bool EntityAI::AttackThink(FireResult& result, Vector3& pos)
{
    if (!_attackTarget)
    {
        return false;
    }
    Object* tgt = _attackTarget->idExact;
    if (!tgt)
    {
        return false;
    }
    if (tgt->IsDammageDestroyed())
    {
        EndAttack();
        return false;
    }
    AIUnit* brain = CommanderUnit();
    if (!brain)
    {
        return false;
    }
    PoseidonAssert(brain->GetSubgroup());
    PoseidonAssert(brain->GetSubgroup()->GetGroup());
    if (_attackTarget->IsKnown() && !_attackTarget->IsKnownBy(brain))
    {
#if DIAG_ATTACK
        LOG_DEBUG(AI, "Target not known by unit");
#endif
        pos = Position();
        return true;
    }
    if (!_attackTarget->IsKnown() || _attackTarget->lastSeen < Glob.time - 20)
    {
        pos = _attackTarget->LandAimingPosition();
        return true;
    }
    float bestDist, minDist, maxDist;
    FireResult bestFire;
    BestFireResult(bestFire, *_attackTarget, bestDist, minDist, maxDist, 30, true);

#if 1
    if (minDist > maxDist && GetType()->HasCargo())
    {
        PoseidonAssert(dyn_cast<const Transport>(this));
        const Transport* transport = static_cast<const Transport*>(this);
        const ManCargo& cargo = transport->GetManCargo();
        for (int c = 0; c < cargo.Size(); c++)
        {
            Person* soldier = cargo[c];
            if (!soldier)
            {
                continue;
            }
            if (soldier->GetGroup() != GetGroup())
            {
                continue;
            }
            soldier->BestFireResult(bestFire, *_attackTarget, bestDist, minDist, maxDist, 30, true);
            bestFire.weapon = -1;
            break;
        }
    }
#endif

#if DIAG_ATTACK
    LOG_DEBUG(AI, "EngageThink best gain {:.1f}, cost {:.1f}", bestFire.gain, bestFire.cost);
#endif
    if (minDist > maxDist)
    {
        return false;
    }
    if (bestFire.gain <= 0)
    {
        return false;
    }
    const float goodEnough = 0.9f;
    const float notBad = 0.4f;

    if (bestFire.weapon >= 0)
    {
        const WeaponModeType* info = GetWeaponMode(bestFire.weapon);
        if (info && info->_ammo)
        {
            switch (info->_ammo->_simulation)
            {
                case AmmoShotPipeBomb:
                case AmmoShotTimeBomb:
                {
                    pos = _attackTarget->LandAimingPosition();
                    if (_attackTarget->idExact)
                    {
                        // eye direction is cheaper for soldiers and more appropriate (avoid being seen)
                        Vector3 weaponDir = _attackTarget->idExact->GetEyeDirection();
                        pos -= weaponDir * 3;
                    }
                    bool found = brain->FindNearestEmpty(pos);
                    pos[1] = GLandscape->RoadSurfaceYAboveWater(pos.X(), pos.Z());
                    pos[1] += GetCombatHeight();

#if DIAG_ATTACK
                    LOG_DEBUG(AI, "Bomb result {:.1f},{:.1f}", pos.X(), pos.Z());
#endif

                    result = bestFire;

                    return found;
                }
            }
        }
    }
    Vector3Val tgtPos = _attackTarget->LandAimingPosition() + _attackTarget->speed * 5;
    if ((_attackEconomicalPos - tgtPos).SquareSizeXZ() > Square(maxDist))
    {
        Vector3 norm = (_attackEconomicalPos - tgtPos);
        Vector3Val off = norm.Normalized() * maxDist * 0.7f;
        _attackEconomicalPos = tgtPos + off;
        _attackEconomicalResult.Reset();
    }
    if ((_attackAggresivePos - tgtPos).SquareSizeXZ() > Square(maxDist))
    {
        Vector3 norm = (_attackAggresivePos - tgtPos);
        Vector3Val off = norm.Normalized() * maxDist * 0.5;
        _attackAggresivePos = tgtPos + off;
        _attackAggresiveResult.Reset();
    }

    const float myHeight = GetCombatHeight();
    const float maxHeight = GetMaxCombatHeight();
    const float minHeight = GetMinCombatHeight();

    const float refreshDelay = 6; // how often we refresh results we have
    int visTests = 0;
    if (Glob.time - _attackRefreshTime > refreshDelay)
    {
        _attackEconomicalResult.Reset();
        _attackAggresiveResult.Reset();
        if (EstVisibility & EstimateAttack(_attackAggresivePos, GetCombatHeight()))
        {
            visTests++;
        }
        if (EstVisibility & EstimateAttack(_attackEconomicalPos, GetCombatHeight()))
        {
            visTests++;
        }
        Vector3 norm = (tgtPos - Position());
        Vector3Val pos = Position() + norm.Normalized() * bestDist;
        if (EstVisibility & EstimateAttack(pos, GetCombatHeight()))
        {
            visTests++;
        }
        if (EstVisibility & EstimateAttack(Position(), GetCombatHeight()))
        {
            visTests++;
        }
        _attackRefreshTime = Glob.time;
    }

    if (_attackEconomicalResult.Surplus() >= goodEnough * bestFire.Surplus())
    {
        pos = _attackEconomicalPos;
        result = _attackEconomicalResult;
#if DIAG_ATTACK
        LOG_DEBUG(AI, "Good EconomicalResult {:.1f},{:.1f}", pos.X(), pos.Z());
#endif
        return true;
    }

    bool noSolution = true;
    bool noEconomSolution = true;
    if (_attackEconomicalResult.Surplus() >= notBad * bestFire.Surplus())
    {
        noSolution = false;
        noEconomSolution = false;
    }
    else if (_attackAggresiveResult.CleanSurplus() >= notBad * bestFire.CleanSurplus())
    {
        noSolution = false;
    }

    const float spread = noSolution ? 200 : 100;
    int maxTests = noSolution ? 8 : 2;
    int maxN = noSolution ? 8 : 2;
    for (int i = maxN; --i >= 0 && visTests < maxTests;)
    {
        {
            float xRand = GRandGen.RandomValue() * spread * 2 - spread;
            float zRand = GRandGen.RandomValue() * spread * 2 - spread;
            float height = myHeight;
            if (maxHeight > minHeight)
            {
                float yRand = GRandGen.RandomValue() + GRandGen.RandomValue();
                if (yRand > 1)
                {
                    height = myHeight + (maxHeight - myHeight) * (yRand - 1);
                }
                else
                {
                    height = minHeight + (myHeight - minHeight) * yRand;
                }
            }
            Vector3Val pos = _attackEconomicalPos + Vector3(xRand, 0, zRand);
            if (EstVisibility & EstimateAttack(pos, height))
            {
                visTests++;
            }
        }
        if (noEconomSolution && !noSolution)
        {
            float xRand = GRandGen.RandomValue() * spread * 2 - spread;
            float zRand = GRandGen.RandomValue() * spread * 2 - spread;
            float height = myHeight;
            if (maxHeight > minHeight)
            {
                float yRand = GRandGen.RandomValue() + GRandGen.RandomValue();
                if (yRand > 1)
                {
                    height = myHeight + (maxHeight - myHeight) * (yRand - 1);
                }
                else
                {
                    height = minHeight + (myHeight - minHeight) * yRand;
                }
            }
            Vector3Val pos = _attackAggresivePos + Vector3(xRand, 0, zRand);
            if (EstVisibility & EstimateAttack(pos, height))
            {
                visTests++;
            }
        }
    }

    if (Glob.time - _attackEngageTime < 5.0)
    {
#if DIAG_ATTACK
        LOG_DEBUG(AI, "  time {:.1f}", Glob.time - _attackEngageTime);
#endif
        return false;
    }
#if DIAG_ATTACK
    LOG_DEBUG(AI, "  econom best {:.1f}", bestFire.Surplus());
    LOG_DEBUG(AI, "         curr {:.1f}", _attackEconomicalResult.Surplus());
    LOG_DEBUG(AI, "  aggres best {:.1f}", bestFire.CleanSurplus());
    LOG_DEBUG(AI, "         curr {:.1f}", _attackAggresiveResult.CleanSurplus());
#endif
    const float bad = 0.1f;
    if (_attackEconomicalResult.Surplus() >= bad * bestFire.Surplus())
    {
        pos = _attackEconomicalPos;
        result = _attackEconomicalResult;
#if DIAG_ATTACK
        LOG_DEBUG(AI, "Bad EconomicalResult {:.1f},{:.1f}", pos.X(), pos.Z());
#endif
        return true;
    }
    else if (_attackAggresiveResult.CleanSurplus() >= bad * bestFire.CleanSurplus())
    {
        pos = _attackAggresivePos;
        result = _attackAggresiveResult;
#if DIAG_ATTACK
        LOG_DEBUG(AI, "Bad AggresiveResult {:.1f},{:.1f}", pos.X(), pos.Z());
#endif
        return true;
    }
    return false;
}

bool EntityAI::AttackReady()
{
    float bestDist, minDist, maxDist;
    FireResult bestFire;
    BestFireResult(bestFire, *_attackTarget, bestDist, minDist, maxDist, 30, true);

#if 1
    if (minDist > maxDist && GetType()->HasCargo())
    {
        PoseidonAssert(dyn_cast<const Transport>(this));
        const Transport* transport = static_cast<const Transport*>(this);
        const ManCargo& cargo = transport->GetManCargo();
        for (int c = 0; c < cargo.Size(); c++)
        {
            Person* soldier = cargo[c];
            if (!soldier)
            {
                continue;
            }
            if (soldier->GetGroup() != GetGroup())
            {
                continue;
            }
            soldier->BestFireResult(bestFire, *_attackTarget, bestDist, minDist, maxDist, 30, true);
            bestFire.weapon = -1;
            break;
        }
    }
#endif

    const float bad = 0.1f;
    if (_attackEconomicalResult.Surplus() >= bad * bestFire.Surplus())
    {
        return true;
    }
    else if (_attackAggresiveResult.CleanSurplus() >= bad * bestFire.CleanSurplus())
    {
        return true;
    }
    return false;
}

#define HIDE_DIAGS 0

bool ObstacleFree(AIUnit* unit, Object* object)
{
    AIGroup* grp = unit->GetGroup();
    for (int u = 1; u < MAX_UNITS_PER_GROUP; u++)
    {
        AIUnit* un = grp->UnitWithID(u);
        if (!un || un == unit || !unit->IsUnit())
        {
            continue;
        }
        EntityAI* veh = un->GetVehicle();
        Vector3Val pos = veh->Position();
        Vector3 predict = pos + veh->Speed() * 2;
        if (pos.DistanceXZ2(object->Position()) < Square(5) || predict.DistanceXZ2(object->Position()) < Square(5))
        {
            return false;
        }
        if (un->GetVehicle()->GetHideBehind() == object)
        {
            return false;
        }
    }
    return true;
}

Vector3 EntityAI::HideFrom() const
{
    if (_hideTarget)
    {
        return _hideTarget->AimingPosition();
    }
    AIUnit* unit = CommanderUnit();
    float radius = GetType()->GetMaxSpeedMs() * 20;
    saturate(radius, 20, 500);
    AISubgroup* subgrp = unit->GetSubgroup();
    if (subgrp)
    {
        return Position() + subgrp->GetFormationDirection() * radius;
    }
    else
    {
        return Position() + Direction() * radius;
    }
}

void EntityAI::FindHideBehind(Vector3 pos, float maxDist)
{
    AIUnit* unit = CommanderUnit();
    if (!unit)
    {
        return;
    }

    AIGroup* grp = unit->GetGroup();
    AICenter* center = grp->GetCenter();

    {
        Target* target = nullptr;
        float minFunc = 1e10;
        const TargetList& targetList = grp->GetTargetList();

        for (int i = 0; i < targetList.Size(); i++)
        {
            Target* tar = targetList[i];
            if (!tar->idExact)
            {
                continue;
            }
            if (tar->idExact->IsDammageDestroyed())
            {
                continue;
            }
            if (!tar->IsKnownBy(unit))
            {
                continue;
            }
            if (unit->GetCombatMode() != CMStealth)
            {
                if (!center->IsEnemy(tar->side))
                {
                    break;
                }
            }
            else
            {
                if (!center->IsEnemy(tar->side) && tar->side != TSideUnknown)
                {
                    break;
                }
            }

            float func = tar->AimingPosition().Distance(unit->Position());
            if (func < minFunc)
            {
                minFunc = func;
                target = tar;
            }
        }

        _hideTarget = target;
    }

    float radius = GetType()->GetMaxSpeedMs() * 20;
    saturate(radius, 20, 500);
    Vector3 tgtPos = HideFrom();

    _hideRefreshTime = Glob.time;

    float size = Object::CollisionSize();

    int xMin, xMax, zMin, zMax;

    ObjRadiusRectangle(xMin, xMax, zMin, zMax, pos, pos, maxDist);
    int x, z;
    float minFunc = 1e10;
    Object* obstacle = nullptr;

    float wantsAway = -0.5f + GRandGen.RandomValue() * 2;
    float isLazy = 1 + GRandGen.RandomValue() * 2;

#if HIDE_DIAGS
    LOG_DEBUG(AI, "{}: Finding cover", (const char*)GetDebugName());
    LOG_DEBUG(AI, "  isLazy {:.1f}, wantsAway {:.1f}", isLazy, wantsAway);
#endif

    AIUnit* leader = grp->Leader();

    for (x = xMin; x <= xMax; x++)
    {
        for (z = zMin; z <= zMax; z++)
        {
            const ObjectList& list = GLandscape->GetObjects(z, x);
            int n = list.Size();
            for (int i = 0; i < n; i++)
            {
                Object* obj = list[i];
                if (obj == this)
                {
                    continue;
                }
                if (obj->GetType() == Network)
                {
                    continue;
                }
                if (obj->GetType() == Temporary)
                {
                    continue;
                }
                if (obj->GetType() == TypeTempVehicle)
                {
                    continue;
                }
                if (!obj->Static())
                {
                    continue;
                }
                if (obj->CollisionSize() < size)
                {
                    continue;
                }
                float distC2 = obj->Position().Distance2(pos);
                if (distC2 > Square(maxDist))
                {
                    continue;
                }

                float dist2 = obj->Position().Distance2(Position());

                float sizeBonus = (obj->CollisionSize() - size) * 5;
                saturateMin(sizeBonus, 50);
                float dist = dist2 * InvSqrt(dist2);
                float distToEnemy = tgtPos.DistanceXZ(obj->Position());
                if (distToEnemy > radius * 2.5f)
                {
                    continue;
                }
                if (distToEnemy < radius * 0.3f)
                {
                    continue;
                }

                float distToLeader = 0;
                if (leader && !leader->IsPlayer())
                {
                    distToLeader = pos.DistanceXZ(obj->Position());
                    dist += distToLeader * 2;
                }

                float func = dist * isLazy - distToEnemy * wantsAway + sizeBonus;

#if HIDE_DIAGS
                {
                    float behind = obj->CollisionSize() + CollisionSize() + 2;
                    Vector3 tgtDir = (tgtPos - obj->Position()).Normalized();
                    Vector3 wantedPos = obj->Position() - tgtDir * behind;
                    if (unit->CheckEmpty(wantedPos) && ObstacleFree(unit, obj))
                    {
                        LOG_DEBUG(AI, "  candidate {}: func {:.1f} ", (const char*)obj->GetDebugName(), func);
                        LOG_DEBUG(AI, "  dist {:.1f}, distToEnemy {:.1f}, distToLeader {:.1f}, sizeBonus {:.1f}", dist,
                                  distToEnemy, distToLeader, sizeBonus, wantsAway);
                    }
                }
#endif

                if (minFunc > func)
                {
                    float behind = obj->CollisionSize() + CollisionSize() + 2;
                    Vector3 norm = (tgtPos - obj->Position());
                    Vector3 tgtDir = norm.Normalized();
                    Vector3 wantedPos = obj->Position() - tgtDir * behind;

                    if (!unit->CheckEmpty(wantedPos))
                    {
                        continue;
                    }
                    if (!ObstacleFree(unit, obj))
                    {
                        continue;
                    }

                    minFunc = func;
                    obstacle = obj;
                }
            }
        }
    }
    if (_hideBehind != obstacle)
    {
        _inFormation = false;
    }
    _hideBehind = obstacle;

#if HIDE_DIAGS
    if (!_hideBehind)
    {
        LOG_DEBUG(AI, "  Nowhere to hide");
    }
    else
    {
        LOG_DEBUG(AI, "  Hide behind: {}", (const char*)_hideBehind->GetDebugName());
    }
#endif
}

void EntityAI::FindHideBehind()
{
    AIUnit* unit = CommanderUnit();
    if (!unit)
    {
        return;
    }
    AIGroup* grp = unit->GetGroup();
    if (!grp)
    {
        return;
    }

    float radius = GetType()->GetMaxSpeedMs() * 20;
    saturate(radius, 20, 500);

    float maxDist = radius;
    AIUnit* leader = grp->Leader();
    if (leader && !leader->IsAnyPlayer())
    {
        if (!leader->GetSubgroup())
        {
            Fail("Leader, but no subgroup");
            return;
        }
        FindHideBehind(unit->GetFormationAbsolute(leader), maxDist);
    }
    else
    {
        FindHideBehind(Position(), maxDist);
    }
}

void EntityAI::BegHide()
{
    FindHideBehind();
}

void EntityAI::EndHide()
{
    _hideTarget = nullptr;
    _hideBehind = nullptr;
    _hideRefreshTime = TIME_MIN;
}

void EntityAI::HideThink()
{
    if (_hideRefreshTime > Glob.time - 5)
    {
        return;
    }

    AIUnit* unit = CommanderUnit();
    if (!unit)
    {
        return;
    }

    if (_hideBehind && !_inFormation)
    {
        return;
    }

    Vector3 tgtPos = HideFrom();

    if (_hideBehind)
    {
        float radius = GetType()->GetMaxSpeedMs() * 20;
        saturate(radius, 20, 500);

        float myDist2ToEnemy = tgtPos.DistanceXZ2(Position());

        if (myDist2ToEnemy > Square(radius * 0.3f) && myDist2ToEnemy < Square(radius * 2.5f))
        {
            AIUnit* unit = CommanderUnit();
            AIUnit* leader = GetGroup()->Leader();
            if (!leader)
            {
                return;
            }
            float maxDistance = leader->IsAnyPlayer() ? radius : radius * 0.2;
            if (unit->GetFormationAbsolute(leader).DistanceXZ2(Position()) < Square(maxDistance))
            {
                return;
            }
        }
    }

    FindHideBehind();
}

float EntityAI::FireAngleInRange(int weapon, Vector3Par rel) const
{
    float size2 = rel.SquareSizeXZ();
    float y2 = Square(rel.Y());
    const float maxY = 0.5;
    if (y2 > size2 * Square(maxY))
    {
        return 0;
    }
    return 1 - fabs(rel.Y()) * InvSqrt(size2) * (1 / maxY);
}

bool EntityAI::BestFireResult(FireResult& result, const Target& target, float& bestDist, float& minDist, float& maxDist,
                              float timeToShoot, bool enableAttack) const
{
    bestDist = 600; // default best distance - be far
    if (!IsAbleToFire())
    {
        return false;
    }

    if (!CommanderUnit())
    {
        return false;
    }

    float timeToLive = CommanderUnit()->GetTimeToLive();

    minDist = +1e10, maxDist = -1e10;
    bool someFire = false;
    for (int w = 0; w < NMagazineSlots(); w++)
    {
        const MagazineSlot& slot = GetMagazineSlot(w);
        if (enableAttack && !slot._muzzle->_enableAttack)
        {
            continue;
        }
        const WeaponModeType* mode = GetWeaponMode(w);
        if (!mode)
        {
            continue;
        }
        if (!mode->_ammo)
        {
            continue;
        }
        const AmmoType& aInfo = *mode->_ammo;
        float aMinDist = aInfo.minRange * 0.75f + aInfo.midRange * 0.25f;
        float aMaxDist = aInfo.midRange * 0.25f + aInfo.maxRange * 0.75f;
        saturateMin(minDist, aMinDist);
        saturateMax(maxDist, aMaxDist);
        float bestDistance = BestDistance(aInfo);
#if DIAG_SHOOT_RESULT
        LOG_DEBUG(AI, "  BestFireResult {}", w);
#endif

        FireResult tResult;
        if (WhatShootResult(tResult, target, w, 1.0, 0, timeToLive, 1.0, bestDistance, 30, false))
        {
            if (tResult.CleanSurplus() > result.CleanSurplus())
            {
                result = tResult;
                someFire = true;
                bestDist = bestDistance;
            }
        }
    }
    return someFire;
}

bool EntityAI::WhatAttackResult(FireResult& result, const Target& target, float timeToShoot) const
{
    if (!CommanderUnit())
    {
        return false;
    }

#if DIAG_RESULT
    LOG_DEBUG(AI, "WhatAttackResult {} to {}", (const char*)GetDebugName(), (const char*)target.type->GetDisplayName());
#endif

    bool someFire = false;

    if (IsAbleToFire())
    {
        float timeToLive = CommanderUnit()->GetTimeToLive();

        saturate(timeToLive, 5, 60);

        float invSpeed = 2.0f / GetType()->GetMaxSpeedMs();
        float targetDistance = target.AimingPosition().Distance(Position());

        for (int w = 0; w < NMagazineSlots(); w++)
        {
            const MagazineSlot& slot = GetMagazineSlot(w);
            if (!slot._muzzle->_enableAttack)
            {
                continue;
            }
            const WeaponModeType* mode = GetWeaponMode(w);
            if (!mode)
            {
                continue;
            }
            if (!mode->_ammo)
            {
                continue;
            }
            const AmmoType& aInfo = *mode->_ammo;
            float bestDistance = BestDistance(aInfo);

            float travelDistance = targetDistance - bestDistance;
            saturateMax(travelDistance, 0);

#if DIAG_RESULT
            LOG_DEBUG(AI, "bestDist {:.1f}, travel {:.1f}", bestDistance, travelDistance);
#endif

            float travelTime = travelDistance * invSpeed;
            if (travelTime > 60)
            {
#if DIAG_RESULT
                LOG_DEBUG(AI, "   traveltime {:.1f}", travelTime);
#endif
                continue;
            }
            const float timeToAim = TimeToAttack + travelTime;
            FireResult tResult;
            if (WhatShootResult(tResult, target, w, 1.0, timeToAim, timeToLive, 1.0, bestDistance, timeToShoot, false))
            {
                if (tResult.CleanSurplus() > result.CleanSurplus())
                {
                    result = tResult;
                    someFire = true;
                }
            }
        }
    }

#if 1
    if (!someFire && GetType()->HasCargo())
    {
        PoseidonAssert(dyn_cast<const Transport>(this));
        const Transport* transport = static_cast<const Transport*>(this);
        const ManCargo& cargo = transport->GetManCargo();
        for (int c = 0; c < cargo.Size(); c++)
        {
            Person* soldier = cargo[c];
            if (!soldier)
            {
                continue;
            }
            if (soldier->GetGroup() != GetGroup())
            {
                continue;
            }
            FireResult tResult;
            float bestDist, minDist, maxDist;
            if (soldier->BestFireResult(tResult, target, bestDist, minDist, maxDist, timeToShoot, true))
            {
                result = tResult;
                result.weapon = -1;
                someFire = true;
            }
            break;
        }
    }
#endif
#if DIAG_RESULT
    LOG_DEBUG(AI, "  dammage {:.2f}, gain {:.0f}, cost {:.0f}, loan {:.0f}, weapon {}", result.dammage, result.gain,
              result.cost, result.loan, result.weapon);
#endif
    return someFire;
}

bool EntityAI::WhatFireResult(FireResult& result, const Target& target, float timeToShoot) const
{
    result.weapon = -1;
#if DIAG_RESULT
    LOG_DEBUG(AI, "WhatFireResult {} to {}", (const char*)GetDebugName(), (const char*)target.type->GetDisplayName());
#endif
    if (!IsAbleToFire())
    {
#if DIAG_RESULT
        LOG_DEBUG(AI, "  cannot fire.");
#endif
        return false;
    }

    AIUnit* unit = CommanderUnit();
    if (!unit)
    {
        return false;
    }
    if (unit->GetLifeState() == AIUnit::LSDead)
    {
        return false;
    }

    if (!target.idExact)
    {
        return false;
    }

    float visibility = _visTracker.KnownValue(this, _currentWeapon, target.idExact, 0.9f);
    if (visibility < 0)
    {
        visibility = GLOB_WORLD->Visibility(unit, target.idExact);

        AICenter* center = unit->GetGroup()->GetCenter();
        if (center->IsFriendly(target.side))
        {
            visibility = 1;
        }
    }

    if (visibility < MinVisibleFire)
    {
#if DIAG_RESULT
        LOG_DEBUG(AI, "  visibility low ({:.3f})", visibility);
#endif
        return false;
    }

    bool possible = false;
    float maxSurplus = -1e10f;

    float timeToLive = unit->GetTimeToLive();

    for (int w = 0; w < NMagazineSlots(); w++)
    {
        float timeToAim = 0;
        float inRange = FireInRange(w, timeToAim, target);
#if DIAG_RESULT
        LOG_DEBUG(AI, " inRange {:.3f}", inRange);
#endif
        const Magazine* magazine = GetMagazineSlot(w)._magazine;
        if (!magazine)
        {
            continue;
        }
        const WeaponModeType* mode = GetWeaponMode(w);
        PoseidonAssert(mode);
        const AmmoType* ammo = mode->_ammo;
        if (!ammo)
        {
            continue;
        }

        if (target.posError.SquareSize() > Square(ammo->indirectHitRange * 2))
        {
#if DIAG_RESULT
            LOG_DEBUG(AI, "   pos Error {:.1f}", target.posError.Size());
#endif
            continue;
        }

        if (target.lastSeen < Glob.time - 10)
        {
#if DIAG_RESULT
            LOG_DEBUG(AI, "   lastSeen {:.1f}", Glob.time - 10 - target.lastSeen);
#endif
            continue;
        }

        float timeToReload = magazine->_reload + magazine->_reloadMagazine;
        timeToAim = floatMax(timeToReload, timeToAim);
        float distance = target.position.Distance(Position());
        FireResult tResult;
#if DIAG_RESULT
        LOG_DEBUG(AI, " times: live {:.3f}, aim {:.3f}", timeToShoot, timeToAim);
#endif
        if (inRange > 1e-6 && WhatShootResult(tResult, target, w, inRange, timeToAim, timeToLive, visibility, distance,
                                              timeToShoot, true))
        {
            float surplus = tResult.Surplus();
            if (timeToReload > timeToShoot)
            { // do not have enough time to shoot - make result worse
                surplus *= 0.2;
            }
            if (maxSurplus < surplus)
            {
                possible = true;
                maxSurplus = surplus;
                result = tResult;
            }
        }
    }
#if DIAG_RESULT
    LOG_DEBUG(AI, "  dammage {:.2f}, gain {:.0f}, cost {:.0f}, loan {:.0f}, weapon {}", result.dammage, result.gain,
              result.cost, result.loan, result.weapon);
    if (!possible)
        LOG_DEBUG(AI, "  impossible");
#endif
    return possible;
}

bool EntityAI::WhatFireResult(FireResult& result, const Target& target, int weapon, float timeToShoot) const
{
    result.weapon = weapon;
#if DIAG_RESULT
    LOG_DEBUG(AI, "WhatFireResult {} to {} (using {})", (const char*)GetDebugName(),
              (const char*)target.type->GetDisplayName(), weapon);
#endif
    if (!IsAbleToFire())
    {
#if DIAG_RESULT
        LOG_DEBUG(AI, "  cannot fire.");
#endif
        return false;
    }

    AIUnit* unit = CommanderUnit();
    if (!unit)
    {
        return false;
    }
    if (unit->GetLifeState() == AIUnit::LSDead)
    {
        return false;
    }

    if (!target.idExact)
    {
        return false;
    }

    float visibility = _visTracker.KnownValue(this, _currentWeapon, target.idExact, 0.9f);
    if (visibility < 0)
    {
        visibility = GLOB_WORLD->Visibility(unit, target.idExact);
        AICenter* center = unit->GetGroup()->GetCenter();
        if (center->IsFriendly(target.side))
        {
            visibility = 1;
        }
    }

    if (visibility < MinVisibleFire)
    {
#if DIAG_RESULT
        LOG_DEBUG(AI, "  visibility low ({:.3f})", visibility);
#endif
        return false;
    }

    bool possible = false;
    float maxSurplus = -1e10f;

    float timeToLive = unit->GetTimeToLive();

    float timeToAim = 0;
    float inRange = FireInRange(weapon, timeToAim, target);
#if DIAG_RESULT
    LOG_DEBUG(AI, " inRange {:.3f}", inRange);
#endif
    if (weapon < 0 || weapon >= NMagazineSlots())
    {
        return false;
    }
    const Magazine* magazine = GetMagazineSlot(weapon)._magazine;
    if (!magazine)
    {
        return false;
    }
    const WeaponModeType* mode = GetWeaponMode(weapon);
    PoseidonAssert(mode);
    const AmmoType* ammo = mode->_ammo;
    if (!ammo)
    {
        return false;
    }

    if (target.posError.SquareSize() > Square(ammo->indirectHitRange * 2))
    {
        return false;
    }

    if (target.lastSeen < Glob.time - 10)
    {
        return false;
    }

    float timeToReload = magazine->_reload + magazine->_reloadMagazine;
    timeToAim = floatMax(timeToReload, timeToAim);
    float distance = target.position.Distance(Position());
    FireResult tResult;
#if DIAG_RESULT
    LOG_DEBUG(AI, " times: live {:.3f}, aim {:.3f}", timeToShoot, timeToAim);
#endif
    if (timeToReload <= timeToShoot && inRange > 1e-6 &&
        WhatShootResult(tResult, target, weapon, inRange, timeToAim, timeToLive, visibility, distance, timeToShoot,
                        true))
    {
        float surplus = tResult.Surplus();
        if (maxSurplus < surplus)
        {
            possible = true;
            maxSurplus = surplus;
            result = tResult;
        }
    }
#if DIAG_RESULT
    LOG_DEBUG(AI, "  dammage {:.2f}, gain {:.0f}, cost {:.0f}, loan {:.0f}, weapon {}", result.dammage, result.gain,
              result.cost, result.loan, result.weapon);
    if (!possible)
        LOG_DEBUG(AI, "  impossible");
#endif
    return possible;
}

Threat EntityAIType::GetStrategicThreat(float distance2, float visibility, float cosAngle) const
{
    float maxRange = 0;

    for (int i = 0; i < NWeaponSystems(); i++)
    {
        const WeaponType* weapon = GetWeaponSystem(i);
        for (int j = 0; j < weapon->_muzzles.Size(); j++)
        {
            const MuzzleType* muzzle = weapon->_muzzles[j];
            const MagazineType* magazine = muzzle->_typicalMagazine;
            if (!magazine)
            {
                continue;
            }
            for (int k = 0; k < magazine->_modes.Size(); k++)
            {
                const AmmoType* ammo = magazine->_modes[k]->_ammo;
                if (ammo)
                {
                    saturateMax(maxRange, ammo->maxRange);
                }
            }
        }
    }
    float sum = 0;
    if (distance2 < maxRange * maxRange)
    {
        float invDist = InvSqrt(distance2);
        float distance = distance2 * invDist;
        for (int i = 0; i < NWeaponSystems(); i++)
        {
            const WeaponType* weapon = GetWeaponSystem(i);
            for (int j = 0; j < weapon->_muzzles.Size(); j++)
            {
                const MuzzleType* muzzle = weapon->_muzzles[j];
                const MagazineType* magazine = muzzle->_typicalMagazine;
                if (!magazine)
                {
                    continue;
                }
                for (int k = 0; k < magazine->_modes.Size(); k++)
                {
                    const WeaponModeType* mode = magazine->_modes[k];
                    const AmmoType* ammo = mode->_ammo;
                    if (!ammo)
                    {
                        continue;
                    }

                    float probab;
                    float bestDist = BestDistance(*ammo, probab);
                    if (distance > bestDist)
                    {
                        probab = HitProbability(distance, *ammo) * visibility;
                    }
                    float oneHit = probab * ammo->hit * visibility;
                    float hitsPerMinute = 60.0f / mode->_reloadTime;
                    saturateMin(hitsPerMinute, magazine->_maxAmmo);
                    sum += hitsPerMinute * oneHit;
                }
            }
        }
        float backCoef = 1.0;
        float frontCoef = 1.0;
        if (distance > 150)
        {
            sum *= 1 - distance / maxRange;
            frontCoef = 2.5f * _sensitivity;
            backCoef = 2.5f * (_sensitivity * 0.3 + _sensitivityEar * 0.7);
        }
        else
        {
            frontCoef = 2.5f * _sensitivity;
            backCoef = 2.5f * (_sensitivity * 0.7 + _sensitivityEar * 0.3);
        }

        if (cosAngle > 0)
        {
            sum *= frontCoef;
        }
        else
        {
            sum *= backCoef;
        }
    }
    return _threat * sum;
}

Threat EntityAIType::GetDammagePerMinute(float distance2, float visibility, EntityAI* vehicle) const
{
    float maxRange = 0;
    for (int i = 0; i < NWeaponSystems(); i++)
    {
        const WeaponType* weapon = GetWeaponSystem(i);
        for (int j = 0; j < weapon->_muzzles.Size(); j++)
        {
            const MuzzleType* muzzle = weapon->_muzzles[j];
            const MagazineType* magazine = muzzle->_typicalMagazine;
            if (!magazine)
            {
                continue;
            }
            for (int k = 0; k < magazine->_modes.Size(); k++)
            {
                const AmmoType* ammo = magazine->_modes[k]->_ammo;
                if (ammo)
                {
                    saturateMax(maxRange, ammo->maxRange);
                }
            }
        }
    }
    float sum = 0;
    if (distance2 < maxRange * maxRange)
    {
        float distance = sqrt(distance2);
        if (vehicle)
        {
            for (int i = 0; i < vehicle->NMagazineSlots(); i++)
            {
                const MagazineSlot& slot = vehicle->GetMagazineSlot(i);
                const Magazine* magazine = slot._magazine;
                if (!magazine)
                {
                    continue;
                }
                const WeaponModeType* mode = vehicle->GetWeaponMode(i);
                PoseidonAssert(mode);
                const AmmoType* ammo = mode->_ammo;
                if (!ammo)
                {
                    continue;
                }

                float probab = HitProbability(distance, *ammo) * visibility;
                float oneHit = probab * ammo->hit * visibility;
                float hitsPerMinute = 60.0f / mode->_reloadTime;
                saturateMin(hitsPerMinute, magazine->_ammo);
                sum += hitsPerMinute * oneHit;
            }
        }
        else
        {
            for (int i = 0; i < NWeaponSystems(); i++)
            {
                const WeaponType* weapon = GetWeaponSystem(i);
                for (int j = 0; j < weapon->_muzzles.Size(); j++)
                {
                    const MuzzleType* muzzle = weapon->_muzzles[j];
                    const MagazineType* magazine = muzzle->_typicalMagazine;
                    if (!magazine)
                    {
                        continue;
                    }
                    for (int k = 0; k < magazine->_modes.Size(); k++)
                    {
                        const WeaponModeType* mode = magazine->_modes[k];
                        const AmmoType* ammo = mode->_ammo;
                        if (!ammo)
                        {
                            continue;
                        }

                        float probab = HitProbability(distance, *ammo) * visibility;
                        float oneHit = probab * ammo->hit * visibility;
                        float hitsPerMinute = 60.0f / mode->_reloadTime;
                        saturateMin(hitsPerMinute, magazine->_maxAmmo);
                        sum += hitsPerMinute * oneHit;
                    }
                }
            }
        }
    }
    return _threat * sum;
}

bool EntityAI::GetAIFireEnabled(Target* tgt) const
{
    AIUnit* unit = CommanderUnit();
    if (!unit)
    {
        return false;
    }
    return unit->IsFireEnabled(tgt);
}

void EntityAI::ReportFireReady() const
{
    if (_lastWeaponNotReady > Glob.time - 5)
    {
        return;
    }
    if (_lastWeaponReady < Glob.time - 30 || _lastWeaponNotReady > _lastWeaponReady)
    {
        _lastWeaponReady = Glob.time;
        AIUnit* unit = CommanderUnit();
        if (!unit)
        {
            return;
        }
        AIGroup* grp = unit->GetGroup();
        if (!grp)
        {
            return;
        }
        grp->ReportFire(unit, true);
    }
}

void EntityAI::ReportFireNotReady() const
{
    if (_lastWeaponReady > Glob.time - 5)
    {
        return;
    }
    if (_lastWeaponNotReady < Glob.time - 30 || _lastWeaponReady > _lastWeaponNotReady)
    {
        _lastWeaponNotReady = Glob.time;
        AIUnit* unit = CommanderUnit();
        if (!unit)
        {
            return;
        }
        AIGroup* grp = unit->GetGroup();
        if (!grp)
        {
            return;
        }
        grp->ReportFire(unit, true);
    }
}

FireDecision::FireDecision()
{
    _fireMode = -1;
    _firePrepareOnly = true;
    _nextWeaponSwitch = Glob.time;
    _nextTargetAquire = Glob.time;
    _nextTargetChange = Glob.time;
    _initState = TargetAlive;
}

void FireDecision::SetTarget(AIUnit* sensor, Target* tgt)
{
    if (tgt == _fireTarget)
    {
        return;
    }
    _fireTarget = tgt;
    if (!tgt)
    {
        _initState = TargetAlive;
    }
    else
    {
        _initState = tgt->State(sensor);
    }
}
bool FireDecision::GetTargetFinished(AIUnit* sensor) const
{
    if (_firePrepareOnly)
    {
        return false; // target never finished
    }
    TargetState state = _fireTarget->State(sensor);
    return state < _initState;
}

void EntityAI::SelectFireWeapon()
{
    SelectFireWeapon(_fire);
}

void EntityAI::SelectFireWeapon(FireDecision& fire)
{
    AIUnit* unit = CommanderUnit();

    if (_userStopped)
    {
        return; // no targeting
    }

    PoseidonAssert(unit);
    if (!unit)
    {
        return;
    }
    if (_forceFireWeapon >= 0)
    {
        fire.SetTarget(unit, nullptr);
        if (_forceFireWeapon != SelectedWeapon())
        {
            SelectWeapon(_forceFireWeapon);
        }
        return;
    }

    AIGroup* grp = unit->GetGroup();
    PoseidonAssert(grp);
    if (!grp)
    {
        return;
    }
    AICenter* center = grp->GetCenter();
    if (!center)
    {
        return;
    }

#if DIAG_TARGET
    if (this != GWorld->CameraOn())
        return;
#endif

    if (fire._fireTarget)
    {
        if (fire.GetTargetFinished(unit))
        {
            fire.SetTarget(unit, nullptr);
        }
        else if (fire._fireTarget->vanished)
        {
            fire.SetTarget(unit, nullptr);
        }
    }
    if (unit->GetCombatMode() <= CMAware || fire._fireTarget)
    {
        if (fire._nextWeaponSwitch > Glob.time)
        {
            return;
        }
    }
    else
    {
        if (fire._nextTargetAquire > Glob.time)
        {
            return;
        }
    }

    float targetMinSubjCost = 0;

    Target* tgt = unit->GetTargetAssigned();
    if (tgt)
    {
        if (tgt->State(unit) > TargetDestroyed)
        {
            // if we do not know target precision exactly enough
            // target may be very old
            targetMinSubjCost = tgt->subjectiveCost * 0.5;
            if (!tgt->IsKnownBy(unit) || tgt->lastSeen < Glob.time - 5)
            {
                tgt = nullptr;
            }
        }
        else
        {
            tgt = nullptr;
        }
    }

    float timeToLive = unit->GetTimeToLive();

    FireResult fResult;
    bool fResultPossible = false;
    bool enableOtherTarget = false;

    if (fire._fireTarget && !fire._firePrepareOnly && fire._nextTargetChange >= Glob.time)
    {
        tgt = fire._fireTarget;
        fResultPossible = WhatFireResult(fResult, *tgt, timeToLive);

#if DIAG_TARGET >= 3
        if (fResult.weapon != fire._fireMode)
        {
            LOG_DEBUG(AI, "{}: SelectWeapon {:.1f}, weapon {}", (const char*)GetDebugName(), Glob.time - Time(0),
                      fResult.weapon);
        }
#endif
    }
    else
    {
        if (tgt)
        {
            fResultPossible = WhatFireResult(fResult, *tgt, timeToLive);
        }
        enableOtherTarget = true;
#if DIAG_TARGET > 2
        LOG_DEBUG(AI, "{}: SelectTarget {:.1f}, weapon {}", (const char*)GetDebugName(), Glob.time - Time(0),
                  fResult.weapon);
        LOG_DEBUG(AI, "  time expired ({:.2f})", Glob.time - fire._nextTargetChange);
        LOG_DEBUG(AI, "  target {}",
                  fire._fireTarget ? (const char*)fire._fireTarget->idExact->GetDebugName() : "<null>");
#endif
    }

    if (unit->GetAIDisabled() & AIUnit::DAAutoTarget)
    {
        enableOtherTarget = false;
    }

    if (tgt && !center->IsEnemy(tgt->side))
    {
        if (unit->GetEnableFireTarget() != tgt)
        {
            fResultPossible = false;
        }
    }
    if (fResultPossible && !enableOtherTarget && !unit->IsHoldingFire())
    {
#if DIAG_TARGET >= 2
        if (fire._firePrepareOnly)
        {
            LOG_DEBUG(AI, "  fire possible, active fire enabled");
        }
#endif
        fire._firePrepareOnly = false;
    }
    if (!fResultPossible && enableOtherTarget && !unit->IsHoldingFire())
    {
        const TargetList& list = grp->GetTargetList();
        int maxEnemies = 4;
        for (int i = 0; i < list.Size(); i++)
        {
            FireResult tResult;
            Target* tgtI = list[i];
            if (!tgtI->IsKnownBy(unit))
            {
                continue;
            }
            if (tgtI->destroyed)
            {
                continue;
            }
            if (tgtI->vanished)
            {
                continue;
            }
            if (!center->IsEnemy(tgtI->side))
            {
                continue;
            }
            if (tgtI->State(unit) < TargetEnemyCombat)
            {
                continue;
            }
            if (tgtI->subjectiveCost < targetMinSubjCost)
            {
                continue;
            }
            if (tgtI->lastSeen < Glob.time - 5)
            {
                continue;
            }
            if (WhatFireResult(tResult, *tgtI, timeToLive))
            {
#if DIAG_TARGET
                LOG_DEBUG(AI, "{} to {}: surplus {}", (const char*)GetDebugName(),
                          (const char*)tgtI->idExact->GetDebugName(), tResult.Surplus());
#endif
                if (tResult.Surplus() > fResult.Surplus())
                {
                    fResult = tResult;
                    tgt = tgtI;
                    fResultPossible = true;
                }
            }
#if DIAG_TARGET >= 2
            else
            {
                LOG_DEBUG(AI, "{} to {}: WhatFireResult => false", (const char*)GetDebugName(),
                          (const char*)tgtI->idExact->GetDebugName());
            }
#endif
            if (--maxEnemies <= 0)
            {
                break;
            }
        }
    }

    if (!tgt && enableOtherTarget)
    {
        const TargetList& list = grp->GetTargetList();
        if (!unit->GetTargetAssigned())
        {
            for (int i = 0; i < list.Size(); i++)
            {
                Target* tgtI = list[i];
                if (!tgtI->IsKnownBy(unit))
                {
                    continue;
                }
                if (tgtI->vanished)
                {
                    continue;
                }
                if (tgtI->destroyed)
                {
                    continue;
                }
                if (tgtI->side != TSideUnknown)
                {
                    continue;
                }
                if (tgtI->lastSeen < Glob.time - 60)
                {
                    continue;
                }
                if (tgtI->idExact && tgtI->idExact->Static())
                {
                    continue;
                }
                tgt = tgtI;
                break;
            }
        }
        if (!tgt)
        {
            tgt = unit->GetTargetAssigned();
            if (tgt && tgt->State(unit) < TargetAlive)
            {
                tgt = nullptr;
            }
        }
        if (!tgt)
        {
            for (int i = 0; i < list.Size(); i++)
            {
                Target* tgtI = list[i];
                if (tgtI->vanished)
                {
                    continue;
                }
                if (tgtI->destroyed)
                {
                    continue;
                }
                if (!tgtI->IsKnownBy(unit))
                {
                    continue;
                }
                if (!center->IsEnemy(tgtI->side))
                {
                    continue;
                }
                if (tgtI->posError.SquareSize() < Square(0.9))
                {
                    continue;
                }
                if (tgtI->lastSeen < Glob.time - 60)
                {
                    continue;
                }
                tgt = tgtI;
                break;
            }
        }

        if (tgt)
        {
            float minFov = 1e10;
            int weapon = fire._fireMode;
            if (weapon < 0)
            {
                weapon = _currentWeapon;
            }
            if (weapon >= 0 && weapon < NMagazineSlots())
            {
                const MagazineSlot& slot = GetMagazineSlot(weapon);
                const MuzzleType* muzzle = slot._muzzle;
                minFov = muzzle->_opticsZoomMax;
            }

            if (tgt->posError.SquareSize() < Square(0.9))
            {
                for (int i = 0; i < NMagazineSlots(); i++)
                {
                    const MagazineSlot& slot = GetMagazineSlot(i);
                    const MuzzleType* muzzle = slot._muzzle;
                    float fov = muzzle->_opticsZoomMax;
                    if (minFov > fov)
                    {
                        minFov = fov;
                        weapon = i;
                    }
                }
                if (_fireState == FireDone && Glob.time > _fireStateDelay || fire._fireMode < 0 ||
                    fire._fireTarget == nullptr)
                {
                    _fireState = FireInit;
                    _fireStateDelay = Glob.time + 0.2 + GRandGen.RandomValue() * 0.5;
                }
            }
#if DIAG_TARGET >= 2
            LOG_DEBUG(AI, "Change look weapon {} to {}", fire._fireMode, weapon);
#endif

            fire._fireMode = weapon;
        }
        else
        {
            fire._fireMode = -1;
        }

        if (fire._fireTarget != tgt)
        {
            fire._nextTargetChange = Glob.time + floatMin(10, GetType()->GetMinFireTime());
            fire.SetTarget(unit, tgt);
#if DIAG_TARGET
            LOG_DEBUG(AI, "{} : prepare target {} until {:.1f}, state {}", (const char*)GetDebugName(),
                      tgt ? (const char*)tgt->idExact->GetDebugName() : "<null>", fire._nextTargetChange - Time(0),
                      tgt ? tgt->State(unit) : -1);
#endif
        }
        fire._firePrepareOnly = true;
        fire._initState = TargetEnemyCombat; // if switched to fire, stop immediatelly
        fire._nextWeaponSwitch = Glob.time + 0.5;
        fire._nextTargetAquire = Glob.time + 0.1;

        return;
    }

    if (!tgt)
    {
        fire._firePrepareOnly = true;
        fire.SetTarget(unit, nullptr);
        fire._nextWeaponSwitch = Glob.time + 0.5;
        fire._nextTargetAquire = Glob.time + 0.1;
        return;
    }

    if (fResult.weapon >= 0)
    {
        if (_fireState == FireDone && Glob.time > _fireStateDelay || fire._fireMode < 0 || fire._fireTarget == nullptr)
        {
            _fireState = FireInit;
            _fireStateDelay = Glob.time + 0.2 + GRandGen.RandomValue() * 0.5;
        }
        int weapon = fResult.weapon;
        if (fire._fireMode != weapon)
        {
            fire._nextWeaponSwitch = Glob.time + 1.5;
            fire._nextTargetAquire = Glob.time + 1.5;
        }

        fire._fireMode = weapon;
    }
    else
    {
        if (fire._fireMode >= 0 && fire._fireMode < NMagazineSlots())
        {
            const Magazine* magazine = GetMagazineSlot(fire._fireMode)._magazine;
            if (!magazine || magazine->_ammo == 0)
            {
                fire._fireMode = -1;
            }
        }
    }
    if (fire._fireTarget != tgt)
    {
        fire._nextWeaponSwitch = Glob.time + 1.5;
        fire._nextTargetAquire = Glob.time + 1.5;
        fire._nextTargetChange = Glob.time + GetType()->GetMinFireTime();
#if DIAG_TARGET
        LOG_DEBUG(AI, "{} : select target {} until {:.1f}, state {}, weapon {}", (const char*)GetDebugName(),
                  tgt->idExact ? (const char*)tgt->idExact->GetDebugName() : "<null>", fire._nextTargetChange - Time(0),
                  tgt ? tgt->State(unit) : -1, fire._fireMode);
#endif
    }
    fire.SetTarget(unit, tgt);

    fire._firePrepareOnly = (!fResultPossible || fResult.weapon < 0 || fire.GetTargetFinished(unit));
#if DIAG_TARGET
    if (fire._firePrepareOnly)
    {
        LOG_DEBUG(AI, "  prepare only");
    }
#endif
}

} // namespace Poseidon
