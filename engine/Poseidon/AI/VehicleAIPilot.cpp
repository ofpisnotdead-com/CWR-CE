#include <Poseidon/Game/Scripting/Scripts.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/AI/VehicleAI.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Entities/Vehicles/Transport.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/World/Terrain/Roads.hpp>
#include <Poseidon/AI/Path/PathSteer.hpp>
#include <Poseidon/Game/UiActions.hpp>
#include <Poseidon/Game/OperMap.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Random/randomGen.hpp>
#include <Poseidon/World/Simulation/FrameInv.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Dev/Diag/DiagModes.hpp>
#include <Poseidon/World/Detection/Detector.hpp>
#include <Poseidon/Game/Commands/GameStateExt.hpp>
#include <stdint.h>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Foundation/Enums/EnumNames.hpp>
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

namespace Poseidon
{
const float LostUnitMin = 150.0f;
const float LostUnitMax = 500.0f;

#define DIAG_SPEED 0
#define DIAG_COL 0

const float MaxBackAngle = H_PI * 0.25;

/*! Common functionality for FormationPilot and LeaderPilot*/

bool EntityAI::PathPilot(float& speedWanted, float& headChange, float& turnPredict, float speedCoef)
{
    // check path position
    AIUnit* unit = PilotUnit();
    const Path& path = unit->GetPath();
    AI_ERROR(path.Size() >= 2);

    Vector3 steerPos = SteerPoint(GetSteerAheadSimul(), GetSteerAheadPlan());
    Vector3Val steerPredict = SteerPoint(GetPredictTurnSimul(), GetPredictTurnPlan());

    float hcOffset = 0;

    float spdFactor = ModelSpeed()[2] * (1.0 / 15);
    saturate(spdFactor, 0, 1);

    Vector3 steerWant;
    if (spdFactor > 0)
    {
        steerWant = PositionWorldToModel(steerPos);

        if (steerWant.Z() > 0)
        {
            hcOffset = steerWant.X() * 0.02;
            saturate(hcOffset, -0.25, +0.25);
        }
    }

    steerPos += DirectionAside() * _avoidAside;

    Matrix4 vertical, invVertical;
    vertical.SetUpAndDirection(VUp, Direction());
    vertical.SetPosition(Position());
    invVertical = vertical.InverseRotation();

    steerWant = invVertical.FastTransform(steerPos);

    Vector3Val steerPredictRel = invVertical.FastTransform(steerPredict);

    if (speedCoef >= 0)
    {
        headChange = atan2(steerWant.X(), steerWant.Z()) + hcOffset * spdFactor;
        turnPredict = atan2(steerPredictRel.X(), steerPredictRel.Z());
    }
    else
    {
        headChange = atan2(-steerWant.X(), -steerWant.Z()) - hcOffset * spdFactor;
        turnPredict = atan2(-steerPredictRel.X(), -steerPredictRel.Z());
    }

    float precision = GetPrecision();

    EngineOn();
    float cost = path.CostAtPos(Position());
    Vector3Val pos = path.PosAtCost(cost, Position());

    float distPath2 = Position().Distance2(pos);
    float distEnd2 = Position().Distance2(path.End());

    speedWanted = path.SpeedAtCost(cost) * speedCoef;

    AIUnit* leader = unit->GetSubgroup()->Leader();

    float tholdDist2 = Square(precision);
    if (unit->GetSubgroup()->GetMode() == AISubgroup::DirectGo)
    {
        tholdDist2 = Square(precision * 0.9);
    }
    else if (_inFormation)
    {
        tholdDist2 = Square(precision * 3);
    }

    if (leader->GetVehicle()->Speed().SquareSize() < Square(3) && distEnd2 < tholdDist2)
    {
        _inFormation = true;
        speedWanted = 0;
        headChange = 0;
        turnPredict = 0;
    }
    else
    {
        _inFormation = false;
        if (distPath2 > tholdDist2)
        {
            saturateMax(speedWanted, GetType()->GetMaxSpeedMs() * 0.25);
        }
    }

    if (Glob.time < _avoidSpeedTime)
    {
        saturate(speedWanted, -_avoidSpeed, +_avoidSpeed);
    }
    return _inFormation;
}

void EntityAI::LeaderPathPilot(AIUnit* unit, float& speedWanted, float& headChange, float& turnPredict, float speedCoef)
{
    // check path position
    const Path& path = unit->GetPath();
    if (path.Size() >= 2)
    {
        bool done = PathPilot(speedWanted, headChange, turnPredict, speedCoef);

        float cost = path.CostAtPos(Position());
        Vector3Val pos = path.PosAtCost(cost, Position());

        // measure distance from corresponding surface point?
        float distPath2 = (Position() - pos).SquareSizeXZ();
        float distEnd2 = Position().Distance2(path.End());

        float precision = GetPrecision();

#if DIAG_SPEED
        if (this == GWorld->CameraOn())
        {
            LOG_DEBUG(AI, "SpeedAtCost {:.1f}", speedWanted * 3.6);
        }
#endif

        // do not report in DirectGo mode
        if (unit->GetSubgroup()->GetMode() != AISubgroup::DirectGo)
        {
            if (distEnd2 < Square(precision) || done)
            {
                unit->SendAnswer(AI::StepCompleted);
            }
            else if (distPath2 > Square(precision * 0.8))
            {
                if (distPath2 > Square(precision * 2) || path.GetSearchTime() < Glob.time - 2)
                {
                    // path is not recent
                    unit->SendAnswer(AI::StepTimeOut);
                }
            }
            else if (path.GetMaxIndex() < path.Size())
            {
                int lastValidIndex = path.GetMaxIndex() - 1;
                AI_ERROR(lastValidIndex >= 1);
                // check if we are in valid region
                if (cost > path[lastValidIndex]._cost)
                {
                    // we need to replan
                    unit->SendAnswer(AI::StepTimeOut);
                }
            }
        }
    }
    else
    {
        speedWanted = 0;
        turnPredict = 0;
        headChange = 0;
    }

    if (fabs(speedWanted) < 0.1f)
    {
        AIUnit* commander = CommanderUnit();
        Vector3Val watchDir = commander->GetWatchDirection();
        float watchDirSize2 = watchDir.SquareSize();
        if (_fire._fireTarget && !_fire._firePrepareOnly)
        {
            Vector3Val tgtDir = PositionWorldToModel(_fire._fireTarget->AimingPosition());
            headChange = atan2(tgtDir.X(), tgtDir.Z());
        }
        else if (watchDirSize2 > 0.1f && commander->GetWatchMode() != AIUnit::WMNo)
        {
            // if watch is commanded watch target only if you fire
            Vector3Val tgtDir = DirectionWorldToModel(watchDir);
            headChange = atan2(tgtDir.X(), tgtDir.Z());
        }
        else if (_fire._fireTarget)
        {
            Vector3Val tgtDir = PositionWorldToModel(_fire._fireTarget->AimingPosition());
            headChange = atan2(tgtDir.X(), tgtDir.Z());
        }
        else if (watchDirSize2 > 0.1f)
        {
            // formation watch - if no interesting target
            Vector3Val tgtDir = DirectionWorldToModel(watchDir);
            headChange = atan2(tgtDir.X(), tgtDir.Z());
        }
    }

    if (GetStopped() && fabs(headChange) < 0.05f)
    {
        EngineOff();
    }

    AISubgroup* subgrp = unit->GetSubgroup();
    if (_stratGoToPos.SquareSize() > 0.1 && subgrp->GetMode() != AISubgroup::DirectGo && StopAtStrategicPos())
    {
        // in some modes we should not stop
        // we will replan route before we reach the position

        // strategic target known
        float finalDist2 = _stratGoToPos.Distance2(Position());
        float bDist = GetType()->GetBrakeDistance();
        float maxSpeed = GetType()->GetMaxSpeedMs();
        if (finalDist2 < Square(bDist * 4))
        {
            float limSpd = maxSpeed * 0.5;
            if (finalDist2 < Square(bDist * 2))
            {
                limSpd = maxSpeed * 0.3;
                if (finalDist2 < Square(bDist))
                {
                    limSpd = maxSpeed * 0.1;
                }
                saturateMax(limSpd, 1);
            }
            saturate(speedWanted, -limSpd, +limSpd);
        }
    }

    // DirectGo is isued only on short distance
    // we should not move too fast
    if (subgrp && subgrp->GetMode() == AISubgroup::DirectGo)
    {
        float limSpd = 4; // 4m/s - i.e. 14 km/h
        saturate(speedWanted, -limSpd, +limSpd);
    }

    // check exposure
    AIUnit* commander = CommanderUnit();
    if (!commander)
    {
        LOG_DEBUG(AI, "Pilot, no commander");
        return;
    }

    AIGroup* group = subgrp->GetGroup();

    // do not limit speed if we are in danger and fleeing
    if (!commander->IsFreeSoldier() || !group->GetFlee())
    {
        saturateMin(speedWanted, _limitSpeed); // move max. by given speed

        float maxSpeed = GetType()->GetMaxSpeedMs();
        // wait for convoy successor
        AIUnit* unitFollowed = subgrp->GetFormationNext(commander);
        if (unitFollowed)
        {
            EntityAI* followed = unitFollowed->GetVehicle();
            // each of vehicles has half influence to formation distance
            float factorZ = (GetFormationZ() + followed->GetFormationZ()) * 0.5;

            float dist = followed->Position().Distance(Position());
            // wanted distance must be bigger than 1.0
            // to force followed to catch me up
            float wantedDistance = factorZ * 3;
            // if we are at wantedDistance from him, we want to go at his speed
            float isFar = (dist - wantedDistance) * (0.1 / factorZ);
            saturate(isFar, -1, 1);
            // if we are further, we want to go slower (positive isFar)
            // if we are nearer, we want to go faster (negative isFar)
            float waitSpeed = -GetType()->GetMaxSpeedMs() * isFar;
            Vector3 followedRelSpeed = DirectionWorldToModel(followed->Speed());
            // his speed relative to me (positiove - he's going away)
            float followedSpeedZ = followedRelSpeed.Z();
            // wanted speed is given by his speed (followedSpeedZ)
            // and by waitSpeed (my wanted speed relative to him)
            saturateMin(speedWanted, floatMax(followedSpeedZ + waitSpeed, maxSpeed * 0.1));
        }
    }
}

void EntityAI::LeaderPilot(float& speedWanted, float& headChange, float& turnPredict)
{ // subgroup leader -
    // if we are near the target we have to operate more precisely
    if (_userStopped)
    {
        speedWanted = 0;
        headChange = 0;
        turnPredict = 0;
        return;
    }

    speedWanted = 0; // go faster
    AIUnit* unit = PilotUnit();

    if (unit->GetAIDisabled() & AIUnit::DAMove)
    {
        return;
    }

#if DIAG_SPEED
    if (this == GWorld->CameraOn())
    {
        LOG_DEBUG(AI, "Basic speed {:.1f}", speedWanted * 3.6);
    }
#endif

    // check if hiding
    // (similiar to keeping formation)

    Vector3 tgtPos = HideFrom();
    if (_hideBehind)
    {
        Vector3Val hideBC = _hideBehind->GetShape()->BoundingCenter();
        Vector3Val hideBehindPos = _hideBehind->PositionModelToWorld(-hideBC);

        Vector3 offset = tgtPos - hideBehindPos;

        float behind = _hideBehind->CollisionSize() + CollisionSize();

        Vector3Val aPos = Position();
        float aboveSurface = aPos.Y() - GLandscape->SurfaceYAboveWater(aPos.X(), aPos.Z());

        Vector3 pos = hideBehindPos - offset.Normalized() * behind;

#if _ENABLE_CHEATS
        if (CHECK_DIAG(DECombat))
        {
            Ref<Object> obj = new ObjectColored(GScene->Preloaded(SphereModel), -1);
            obj->SetPosition(pos);
            obj->SetScale(0.5);
            obj->SetConstantColor(PackedColor(Color(0, 0, 0)));
            GLandscape->ShowObject(obj);
        }
#endif

        pos[1] = GLandscape->SurfaceYAboveWater(pos.X(), pos.Z()) + aboveSurface;

        Vector3Val watchDir = _fire._fireTarget ? _fire._fireTarget->AimingPosition() : tgtPos;
        Vector3 norm = (watchDir - Position());
        Vector3 dir = norm.Normalized();

        float precision = floatMin(_hideBehind->CollisionSize() * 0.25, Object::CollisionSize());
        saturate(precision, 2, 50);
        PositionPilot(speedWanted, headChange, turnPredict, pos, dir, precision);
        return;
    }

    LeaderPathPilot(unit, speedWanted, headChange, turnPredict);
}

float EntityAI::PilotSpeed() const
{
    float speedWanted = 0; // go faster
    // check path position
    const Path& path = PilotUnit()->GetPath();
    if (path.Size() >= 2)
    {
        float cost = path.CostAtPos(Position());
        speedWanted = path.SpeedAtCost(cost);
    }
    return speedWanted;
}

void EntityAI::PositionPilot(float& speedWanted, float& headChange, float& turnPredict, Vector3Par pos, Vector3Par dir,
                             float precision)
{
    AIUnit* unit = PilotUnit();

    // control vehicle to get to given position
    // default: no speed limit
    _limitSpeed = GetType()->GetMaxSpeedMs() * 1.5;
    float dist2 = pos.Distance2(Position());

    saturateMin(precision, floatMax(4, GetPrecision()));
    float limit = precision;
    if (!_inFormation)
    {
        limit *= 0.5;
    }
    else
    {
        limit *= 2;
    }

    if (dist2 < Square(limit))
    {
        // in position - turn to direction
        _inFormation = true;
        speedWanted = 0;
        if (_fire._fireTarget)
        {
            Vector3Val tgtDir = PositionWorldToModel(_fire._fireTarget->AimingPosition());
            headChange = atan2(tgtDir.X(), tgtDir.Z());
        }
        else
        {
            Vector3Val relMoveDir = DirectionWorldToModel(dir);
            headChange = atan2(relMoveDir.X(), relMoveDir.Z());
            if (fabs(headChange) < 0.05 && GetStopped())
            {
                EngineOff();
            }
        }
    }
    else
    {
        // out of position
        _inFormation = false;
        // check if trivial solution is good enough

        // check if we are already there
        // find nearest empry position
        Vector3 freePos = pos;
        unit->FindNearestEmpty(freePos);

        dist2 = freePos.Distance2(Position());

        if (dist2 < Square(limit))
        {
            // we cannot be in formation - in-formation position is not free
            _inFormation = true;
        }

        bool simple = false;
        if (Glob.time > _lastSimplePath + 0.5)
        {
            simple = unit->IsSimplePath(Position(), freePos);
            if (simple)
            {
                _lastSimplePath = Glob.time;
            }
        }
        else
        {
            simple = true;
        }
        if (simple)
        {
            Vector3Val moveDir = PositionWorldToModel(freePos);
            headChange = atan2(moveDir.X(), moveDir.Z());
            speedWanted = sqrt(dist2) * GetInvFormationTime();
            saturateMax(speedWanted, 1.5);
            if (moveDir.SquareSize() < Square(GetType()->GetMaxSpeedMs() * 3) &&
                fabs(AngleDifference(headChange, H_PI)) < MaxBackAngle)
            {
                // go back
                headChange = AngleDifference(headChange, H_PI);
                speedWanted = -speedWanted;
            }
            unit->SetWantedPosition(freePos, AIUnit::DoNotPlan, true);
        }
        else
        {
            // non-trivial solution needed
            Path& path = unit->GetPath();
            bool update = false;

            bool pathInvalid = (path.Size() < 2 || path.End().DistanceXZ2(freePos) > Square(limit));

            if (path.Size() < 2)
            { // no known solution
                // avoid searching too often
                if (Glob.time - path.GetSearchTime() > 2)
                {
                    update = true;
                }
            }
            else if (pathInvalid && Glob.time - path.GetSearchTime() > 5)
            { // solution is invalid and too old
                update = true;
            }
            else if (Glob.time - path.GetSearchTime() > 15)
            { // solution is too old
                update = true;
            }
            unit->SetWantedPosition(freePos, AIUnit::FormationPlanned, update);

            if (path.Size() >= 2)
            {
                PathPilot(speedWanted, headChange, turnPredict);
                // note: pathpilot does AvoidCollision
            }
            else
            {
                speedWanted = 0, headChange = 0;
            }
            // plan is used by default handler (steer pos)
        }
    }
    turnPredict = headChange;
}

bool EntityAI::IsOnRoad() const
{
    AIUnit* unit = CommanderUnit();
    if (!unit)
    {
        return false;
    }
    if (QIsManual())
    {
        return GRoadNet->IsOnRoad(Position(), CollisionSize()) != nullptr;
    }
    else
    {
        const Path& path = unit->GetPath();
        return (path.GetOnRoad() && path.Size() >= 2);
    }
}

bool EntityAI::IsOnRoadMoving(float minSpeed) const
{
    if (Speed().SquareSize() < Square(minSpeed))
    {
        return false;
    }
    return IsOnRoad();
}

void EntityAI::CheckAway()
{
    AIUnit* unit = CommanderUnit();
    if (!unit)
    {
        return;
    }
    bool away = IsAway();
    if (away)
    {
        unit->SetAway();
    }
}

bool EntityAI::IsAway(float factor)
{
    AIUnit* unit = CommanderUnit();
    if (!unit)
    {
        return false;
    }
    AISubgroup* subgrp = unit->GetSubgroup();

    float lostUnit = 12.0 * floatMax(GetFormationX(), GetFormationZ());
    saturate(lostUnit, LostUnitMin, LostUnitMax);
    lostUnit *= factor;

    if (!unit->IsSubgroupLeader())
    {
        // check if we keep in formation
        if (!IsCautious())
        {
            EntityAI* follow = subgrp->GetFormationPrevious(unit)->GetVehicle();
            // each of vehicles has half influence to formation distance
            float factorZ = (GetFormationZ() + follow->GetFormationZ()) * 0.5;
            Vector3 relPos(0, 0, -0.4 * factorZ);

            if ((follow->Position() - Position()).SquareSizeXZ() > Square(lostUnit))
            {
                return true;
            }
        }
        else
        {
            Vector3 formPos = unit->GetFormationAbsolute();
            if (formPos.Distance2(Position()) > Square(lostUnit))
            {
                return true;
            }
        }
    }
    return false;
}

void EntityAI::SwitchToFormation()
{
    _hideBehind = nullptr;
    _inFormation = false;
}
void EntityAI::SwitchToLeader()
{
    _hideBehind = nullptr;
    _inFormation = false;
}

void EntityAI::FormationPilot(float& speedWanted, float& headChange, float& turnPredict)
{
    if (_userStopped)
    {
        speedWanted = 0;
        headChange = 0;
        turnPredict = 0;
        return;
    }

    // predict leader position
    AIUnit* unit = PilotUnit();
    if (!unit)
    {
        return;
    }
    AIUnit* commander = CommanderUnit();
    if (!commander)
    {
        return;
    }
    if (unit->GetAIDisabled() & AIUnit::DAMove)
    {
        return;
    }

    AISubgroup* subgrp = commander->GetSubgroup();
    AI_ERROR(subgrp);
    if (!subgrp)
    {
        return;
    }

    AIGroup* grp = commander->GetGroup();
    AI_ERROR(grp);
    if (!grp)
    {
        return;
    }

    AIUnit* leader = subgrp->Leader();
    AI_ERROR(leader);
    if (!leader)
    {
        return;
    }

    EntityAI* lVehicle = leader->GetVehicle();

    // check if leader is on road
    bool onRoad = false;
    if (!IsCautious())
    {
        onRoad = lVehicle->IsOnRoad();

        // use follow mode - ignore formation

        // special case - leader on road
        // follow previous vehicle in subgroup
        EntityAI* follow = subgrp->GetFormationPrevious(commander)->GetVehicle();
        // each of vehicles has half influence to formation distance
        float factorZ = (GetFormationZ() + follow->GetFormationZ()) * 0.5;
        Vector3 relPos(0, 0, -0.4 * factorZ);

        Vector3 followPos = follow->PositionModelToWorld(relPos);
        bool forceReplan = false;

        if (!onRoad)
        {
            followPos += follow->Speed() * GetFormationTime() * 0.6;
        }
        else
        {
            followPos += follow->Speed() * GetFormationTime() * 0.3;
        }

        Path& path = unit->GetPath();
        if (onRoad && path.GetOnRoad())
        {
            // road path refresh should be quite cheap
            if (path.Size() >= 2 && path.CostAtPos(Position() + Speed()) >= path.EndCost() ||
                Glob.time - path.GetSearchTime() > 0.5)
            {
                // or we are on the end of the path
                // try cheap search
                followPos = GRoadNet->GetNearestRoadPoint(followPos);
                forceReplan = true;
            }
        }

        float precision = GetPrecision();
        if (path.Size() >= 2 && path.End().Distance2(Position()) < Square(precision) &&
            Glob.time - path.GetSearchTime() > 0.5)
        {
            // end of path reached - replan
            unit->FindNearestEmpty(followPos);
            forceReplan = true;
        }
        if (Glob.time - path.GetSearchTime() > 2)
        {
            // path is old - consider replan
            if (path.Size() < 2 || path.End().Distance2(followPos) > Square(precision))
            {
                unit->FindNearestEmpty(followPos);
                // no path or path invalid
                forceReplan = true;
            }
        }
        // ask unit to create path
        unit->SetWantedPosition(followPos, AIUnit::FormationPlanned, forceReplan);

        if (path.Size() >= 2)
        {
            PathPilot(speedWanted, headChange, turnPredict);
            // note: path pilot does AvoidCollision

            // dynamic stability
            float limitSpeed;
            float maxSpeed = GetType()->GetMaxSpeedMs();
            {
                // do not try to overtake the vehicle you should follow
                float dist = follow->Position().Distance(Position());
                // keep safe distance based on his speed
                float wantedDistance = factorZ * 0.6 + +fabs(follow->ModelSpeed().Z()) * 0.5;
                // if we are at wantedDistance from him, we want to go at his speed
                float isFar = (dist - wantedDistance) * (0.4 / factorZ);
                saturate(isFar, -0.3, 1);
                // if we are further, we want to go faster (positive isFar)
                // if we are nearer, we want to go slower (negative isFar)
                float catchUpSpeed = maxSpeed * isFar;
                Vector3 followRelSpeed = DirectionWorldToModel(follow->Speed());
                // his speed relative to me (positiove - he's going away)
                float followSpeedZ = followRelSpeed.Z();
                // wanted speed is given by his speed (followSpeedZ)
                // and by catchUpSpeed (my wanted speed relative to him)
                limitSpeed = floatMax(followSpeedZ + catchUpSpeed, maxSpeed * 0.1);
                // speedWanted is currently max speed given by path planning
            }

            AIUnit* unitFollowed = subgrp->GetFormationNext(commander);
            if (unitFollowed)
            {
                EntityAI* followed = unitFollowed->GetVehicle();
                // each of vehicles has half influence to formation distance
                float factorZ = (GetFormationZ() + followed->GetFormationZ()) * 0.5;

                float dist = followed->Position().Distance(Position());
                // wanted distance must be bigger than 1.0
                // to force followed to catch me up
                float wantedDistance = factorZ * 3;
                // if we are at wantedDistance from him, we want to go at his speed
                float isFar = (dist - wantedDistance) * (0.1 / factorZ);
                saturate(isFar, -1, 1);
                // if we are further, we want to go slower (positive isFar)
                // if we are nearer, we want to go faster (negative isFar)
                float waitSpeed = -GetType()->GetMaxSpeedMs() * isFar;
                Vector3 followedRelSpeed = DirectionWorldToModel(followed->Speed());
                // his speed relative to me (positiove - he's going away)
                float followedSpeedZ = followedRelSpeed.Z();
                // wanted speed is given by his speed (followedSpeedZ)
                // and by waitSpeed (my wanted speed relative to him)
                saturateMin(limitSpeed, floatMax(followedSpeedZ + waitSpeed, maxSpeed * 0.1));
            }

            saturate(speedWanted, -limitSpeed, +limitSpeed);
        }
        else
        {
            speedWanted = 0;
            headChange = 0;
            turnPredict = 0;
        }
        return;
    }

    float estT = GetFormationTime();

    //
    bool enableHide = commander->IsFreeSoldier() && commander->GetCombatMode() >= CMCombat;
    if (enableHide)
    {
        if (leader->IsPlayer())
        {
            enableHide = !commander->IsKeepingFormation();
        }
    }

    // predict leader position
    Vector3 predSpeed = lVehicle->Speed();
    float maxSpeed = lVehicle->GetType()->GetMaxSpeedMs();
    float spSize2 = predSpeed.SquareSize();
    if (spSize2 < Square(maxSpeed * 0.5) && spSize2 > Square(maxSpeed * 0.1))
    {
        predSpeed.Normalize();
        predSpeed *= maxSpeed * 0.5;
    }
    Vector3Val estPos = lVehicle->Position() + estT * predSpeed;
    // predict leader orientation
    Matrix4 estTransform;
    estTransform.SetPosition(estPos);
    // get formation orientation
    Vector3Val formDir = leader->GetSubgroup()->GetFormationDirection();
    estTransform.SetDirectionAndUp(formDir, VUp);

    Vector3Val formPos = commander->GetFormationRelative() - leader->GetFormationRelative();
    Vector3Val moveDir = commander->GetWatchDirection();

    // check if we are already in cover
    if (enableHide)
    {
        Vector3 movePos = lVehicle->PositionModelToWorld(formPos);
        // check position is combat height
        float height = GetCombatHeight();

        movePos += formDir * GetType()->GetMaxSpeedMs() * 10;

        movePos[1] = GLandscape->SurfaceYAboveWater(movePos[0], movePos[2]) + height;

        unit->FindNearestEmpty(movePos);

        float radius = GetType()->GetMaxSpeedMs() * 20;
        float maxDist = radius * 0.25;

        // hiding enabled
        if (_hideBehind && _hideBehind->Position().DistanceXZ2(movePos) > Square(maxDist * 1.5f))
        {
            // hideBehind too far
            _hideBehind = nullptr;
        }
        if (_inFormation || !_hideBehind)
        {
            // find some cover near formation position
            if (_hideRefreshTime < Glob.time - 5)
            {
                // hide in front of formation
                FindHideBehind(movePos, maxDist);
            }
        }
        if (_hideBehind)
        {
            Vector3Val hideBC = _hideBehind->GetShape()->BoundingCenter();
            Vector3Val hideBehindPos = _hideBehind->PositionModelToWorld(-hideBC);

            Vector3 tgtPos = HideFrom();
            Vector3 offset = tgtPos - hideBehindPos;

            float behind = _hideBehind->CollisionSize() + CollisionSize();

            Vector3Val aPos = Position();
            float aboveSurface = aPos.Y() - GLandscape->SurfaceYAboveWater(aPos.X(), aPos.Z());

            Vector3 pos = hideBehindPos - offset.Normalized() * behind;

            pos[1] = GLandscape->SurfaceYAboveWater(pos.X(), pos.Z()) + aboveSurface;

            Vector3Val watchDir = _fire._fireTarget ? _fire._fireTarget->AimingPosition() : tgtPos;
            Vector3 norm = (watchDir - Position());
            Vector3 dir = norm.Normalized();

#if _ENABLE_CHEATS
            if (CHECK_DIAG(DECombat))
            {
                Ref<Object> obj = new ObjectColored(GScene->Preloaded(SphereModel), -1);
                obj->SetPosition(pos);
                obj->SetScale(0.5);
                obj->SetConstantColor(PackedColor(Color(0.3, 0, 0)));
                GLandscape->ShowObject(obj);
            }
#endif

            float precision = floatMin(_hideBehind->CollisionSize() * 0.25, Object::CollisionSize());
            saturate(precision, 2, 50);
            PositionPilot(speedWanted, headChange, turnPredict, pos, dir, precision);
            return;
        }
    }
    else
    {
        _hideBehind = nullptr;
    }

    Vector3 movePos = estTransform.FastTransform(formPos);
    // check position is combat height
    float height = GetCombatHeight();
    movePos[1] = GLandscape->SurfaceYAboveWater(movePos[0], movePos[2]) + height;

#if _ENABLE_CHEATS
    if (CHECK_DIAG(DECombat))
    {
        Ref<Object> obj = new ObjectColored(GScene->Preloaded(SphereModel), -1);
        obj->SetPosition(movePos);
        obj->SetScale(0.6);
        obj->SetConstantColor(PackedColor(Color(1, 0, 1)));
        GLandscape->ShowObject(obj);
    }
#endif

    // calculate "ideal" speed
    // based on current position and current formation position
    Vector3Val formPosAct = commander->GetFormationAbsolute();
    Vector3Val formPosRel = PositionWorldToModel(formPosAct);
    Vector3Val leadSpdRel = DirectionWorldToModel(lVehicle->Speed());

    // ideal speed is such speed that we will reach position in estT time
    float speedZ = floatMax(formPosRel.Z() / estT + leadSpdRel.Z(), 1);

    PositionPilot(speedWanted, headChange, turnPredict, movePos, moveDir, 1e10);
    saturateMin(speedWanted, speedZ);
}

void EntityAI::PerformFF(FFEffects& effects)
{
    effects.engineMag = 0;
    effects.engineFreq = 1;
}

void EntityAI::ResetFF()
{
    //_ff.gunCount=0; // reset guns
}

void EntityAI::LimitSpeed(float speed)
{
    _limitSpeed = speed;
}

AIGroup* EntityAI::GetGroup() const
{
    AIUnit* unit = CommanderUnit();
    if (!unit)
    {
        unit = PilotUnit();
    }
    if (!unit)
    {
        unit = GunnerUnit();
    }
    if (!unit)
    {
        return nullptr;
    }
    return unit->GetGroup();
}

bool EntityAI::DisableWeapons() const
{
    return false;
}

RString EntityAI::GetActionName(const UIAction& action)
{
    if (action.type == ATUser)
    {
        const UserActionDescription* desc = FindUserAction(action.param);
        if (desc)
        {
            return desc->text;
        }
    }
    else if (action.type == ATUserType)
    {
        const EntityAIType* type = GetType();
        return type->_userTypeActions[action.param].displayName;
    }
    return RString("Unknown action") + Foundation::FindEnumName(action.type);
}

bool EntityAI::CheckActionProcessing(UIActionType action, AIUnit* unit) const
{
    // return action is not processing
    // default is process all action immediatelly
    return false;
}

void EntityAI::StartActionProcessing(const UIAction& action, AIUnit* unit)
{
    // default: process action directly
    action.Process(unit);
}

void EntityAI::PerformAction(const UIAction& action, AIUnit* unit)
{
    switch (action.type)
    {
        case ATLightOn:
            SetPilotLight(true);
            return;
        case ATLightOff:
            SetPilotLight(false);
            return;
        case ATSwitchWeapon:
            SelectWeaponCommander(unit, action.param);
            return;
        case ATUseWeapon:
            FireWeapon(action.param, nullptr);
            return;
        case ATUseMagazine:
        {
            // find magazine
            int m = -1;
            for (int i = 0; i < _magazines.Size(); i++)
            {
                Magazine* magazine = _magazines[i];
                if (magazine && magazine->_creator == action.param && magazine->_id == action.param2)
                {
                    m = i;
                    break;
                }
            }
            if (m < 0)
            {
                return;
            }
            Magazine* magazine = _magazines[m];

            // find slot
            int s = -1;
            for (int i = 0; i < _magazineSlots.Size();)
            {
                MagazineSlot& slot = _magazineSlots[i];
                if (!slot._muzzle)
                {
                    i++;
                    continue;
                }
                if (slot._muzzle->CanUse(magazine->_type))
                {
                    s = i;
                    break;
                }
                i += slot._muzzle->_nModes;
            }
            if (s < 0)
            {
                return;
            }

            // reload and fire
            ReloadMagazineTimed(s, m, true);
            magazine->_reload = 0;
            FireWeapon(s, nullptr);
        }
            return;
        case ATUser:
            if (unit)
            {
                const UserActionDescription* desc = FindUserAction(action.param);
                if (desc && desc->script.GetLength() > 0)
                {
                    GameArrayType arguments;
                    arguments.Add(GameValueExt(this));
                    arguments.Add(GameValueExt(unit->GetPerson()));
                    arguments.Add(GameValue((float)desc->id));
                    Script* script = new Script(desc->script, GameValue(arguments));
                    GWorld->AddScript(script);
                }
            }
            return;
        case ATUserType:
            if (unit)
            {
                const UserTypeAction& act = GetType()->_userTypeActions[action.param];
                GameState* gstate = GWorld->GetGameState();
                // Declaring "GameVarSpace local" here to execute statement locally. This is learnt from
                // "CreateUnit" script command. Since CreateUnit works normally, maybe userActions can 
                // have similar design and can thus uses "_this" to indicate the action caller
                GameVarSpace local;
                gstate->BeginContext(&local);
                gstate->VarSet("this", GameValueExt(this), true);
                gstate->VarSetLocal("_this", GameValueExt(unit->GetPerson()), true);
                gstate->Execute(act.statement);
                gstate->EndContext();
            }
            return;
    }
    // note: action is performed by target of the action
    // unit is who activated the action
    LOG_ERROR(AI, "{} ({}): Unknown action {}, target {}", (const char*)GetDebugName(),
              (const char*)GetType()->GetDisplayName(), (const char*)Foundation::FindEnumName(action.type),
              (const char*)action.target->GetDebugName());
}

bool EntityAI::CheckMagazines()
{
    bool doubleId = false;
    // check all magazines for unique id
    for (int i = 0; i < _magazines.Size(); i++)
    {
        const Magazine* magI = _magazines[i];
        if (!magI)
        {
            continue;
        }
        for (int j = 0; j < i; j++)
        {
            const Magazine* magJ = _magazines[j];
            if (!magJ)
            {
                continue;
            }
            if (magJ->_id != magI->_id)
            {
                continue;
            }
            // take action - double ID
            // remove magazine I

            LOG_ERROR(AI, "Double magazine {}:{} ({:x}) {}:{} ({:x}) in {}", magI->_id,
                      (const char*)magI->_type->GetName(), (uintptr_t)magI, magJ->_id,
                      (const char*)magJ->_type->GetName(), (uintptr_t)magJ, (const char*)GetDebugName());

            _magazines.Delete(i);
            i--; // to compensate for i++ in for loop commande
            doubleId = true;

            break;
        }
    }
    return doubleId;
}

LODShapeWithShadow* EntityAI::GetMissileShape() const
{
    for (int i = 0; i < NMagazines(); i++)
    {
        const Magazine* mag = GetMagazine(i);
        if (mag->_type->_modes.Size() <= 0)
        {
            continue;
        }
        const WeaponModeType* mode = mag->_type->_modes[0];
        const AmmoType* ammo = mode->_ammo;
        if (!ammo)
        {
            continue;
        }
        if (ammo->_simulation != AmmoShotMissile)
        {
            continue;
        }
        if (ammo->maxControlRange < 10)
        {
            continue;
        }
        if (ammo->_proxyShape)
        {
            return ammo->_proxyShape;
        }
    }
    return nullptr;
}

int EntityAI::CountMissiles() const
{
    int total = 0;
    for (int i = 0; i < NMagazines(); i++)
    {
        const Magazine* mag = GetMagazine(i);
        if (mag->_type->_modes.Size() <= 0)
        {
            continue;
        }
        const WeaponModeType* mode = mag->_type->_modes[0];
        const AmmoType* ammo = mode->_ammo;
        if (!ammo)
        {
            continue;
        }
        if (ammo->_simulation != AmmoShotMissile)
        {
            continue;
        }
        if (ammo->maxControlRange < 10)
        {
            continue;
        }
        total += mag->_ammo;
    }
    return total;
}

/*!
\param type given magazine type
\param ammo minimal amount of ammo magazine must contain
\return index of magazine in array or -1 if not found
*/
int EntityAI::FindBestMagazine(const MagazineType* type, int ammo) const
{
    int best = -1;
    for (int j = 0; j < NMagazines(); j++)
    {
        const Magazine* reserve = GetMagazine(j);
        if (!reserve)
        {
            continue;
        }
        if (reserve->_type != type)
        {
            continue;
        }
        if (reserve->_ammo > ammo)
        {
            ammo = reserve->_ammo;
            best = j;
        }
    }
    return best;
}

static void GetReloadActions(EntityAI* veh, UIActions& actions, int iSlot, const MuzzleType* currentMuzzle)
{
    const MagazineSlot& slot = veh->GetMagazineSlot(iSlot);
    const Magazine* magazine = slot._magazine;
    const MagazineType* mType = magazine ? magazine->_type : nullptr;
    bool empty = false;
    if (mType && slot._mode >= 0 && slot._mode < mType->_modes.Size())
    {
        const WeaponModeType* mode = mType->_modes[slot._mode];
        if (mode->_ammo && mType->_maxAmmo > 0)
        {
            empty = magazine->_ammo == 0;
        }
        if (!empty && currentMuzzle->_autoReload &&
            (mType->_maxAmmo == 1 || (slot._weapon->_weaponType & MaskHardMounted) != 0))
        {
            // do not offer reload HandGrenade etc.
        }
        else
        {
            // magazine of the same type
            int best = veh->FindBestMagazine(mType, 0);
            if (best >= 0)
            {
                float prior = empty ? 1.5 : 0.35;
                const Magazine* magazine = veh->GetMagazine(best);
                RString muzzleID = slot._weapon->GetName() + RString("|") + slot._muzzle->GetName();
                actions.Add(ATLoadMagazine, veh, prior, magazine->_creator, empty, true, magazine->_id, muzzleID);
            }
        }
    }
    else
    {
        empty = true;
    }
    // magazines of other type
    for (int i = 0; i < currentMuzzle->_magazines.Size(); i++)
    {
        const MagazineType* change = currentMuzzle->_magazines[i];
        if (change == mType)
        {
            continue;
        }
        int best = veh->FindBestMagazine(change, 0);
        if (best >= 0)
        {
            const Magazine* magazine = veh->GetMagazine(best);
            RString muzzleID = slot._weapon->GetName() + RString("|") + slot._muzzle->GetName();
            actions.Add(ATLoadMagazine, veh, 0.34, magazine->_creator, empty, true, magazine->_id, muzzleID);
        }
    }
}

void EntityAI::GetActions(UIActions& actions, AIUnit* unit, bool now)
{
    if (!unit)
    {
        return;
    }
    // user actions
    // FIX
    if (_userActions.Size() > 0)
    {
        bool ok = true;
        if (now && unit->GetVehicle() != this)
        {
            // check if in front
            Vector3Val relPos = unit->GetPerson()->PositionWorldToModel(Position());
            ok = relPos.Z() > 0 && relPos.SquareSize() < Square(10);
        }
        if (ok)
        {
            for (int i = 0; i < _userActions.Size(); i++)
            {
                int id = _userActions[i].id;
                actions.Add(ATUser, this, 1.5 - 0.001 * id, id, true);
            }
        }
    }

    // user type actions
    const EntityAIType* type = GetType();
    if (type->_userTypeActions.Size() > 0)
    {
        Vector3Val relPos = PositionWorldToModel(unit->GetPerson()->WorldPosition());
        GameState* gstate = GWorld->GetGameState();
        gstate->VarSet("this", GameValueExt(this), true);

        for (int i = 0; i < type->_userTypeActions.Size(); i++)
        {
            const UserTypeAction& act = type->_userTypeActions[i];
            if (relPos.Distance2(act.modelPosition) > Square(act.radius))
            {
                continue;
            }
            if (!gstate->EvaluateBool(act.condition))
            {
                continue;
            }

            actions.Add(ATUserType, this, 1.4 - 0.001 * i, i, true);
        }
    }

    if (unit->GetVehicle() == this && CommanderUnit() == unit && !unit->IsInCargo())
    {
        // lights on / off
        if (unit->IsPlayer() && _reflectors.Size() > 0)
        {
            if (IsPilotLight())
            {
                actions.Add(ATLightOff, this, 0.3);
            }
            else
            {
                if (GScene->MainLight()->NightEffect() > 0)
                {
                    actions.Add(ATLightOn, this, 0.3);
                }
            }
        }
        if (EnableWeaponManipulation())
        {
            // secondary weapons
            int primary = -1;
            bool isPrimaryWeapon = false;
            int primaryMask = IsHandGunSelected() ? MaskSlotHandGun : MaskSlotPrimary;
            for (int i = 0; i < NMagazineSlots(); i++)
            {
                if (i == _currentWeapon)
                {
                    continue;
                }
                const MagazineSlot& slot = GetMagazineSlot(i);
                const MuzzleType* muzzle = slot._muzzle;
                if (muzzle->_primary)
                {
                    if (primary < 0)
                    {
                        primary = i;
                        isPrimaryWeapon = (slot._weapon->_weaponType & primaryMask) != 0;
                    }
                    else if (!isPrimaryWeapon && (slot._weapon->_weaponType & primaryMask) != 0)
                    {
                        primary = i;
                        isPrimaryWeapon = true;
                    }
                }
                else
                {
                    if (!slot._muzzle->_showEmpty && EmptySlot(slot))
                    {
                        continue;
                    }
                    const WeaponModeType* mode = GetWeaponMode(i);
                    if (mode && mode->_useAction)
                    {
                        // actions.Add(ATUseWeapon, this, 0.5 - 0.01 * i, i);
                    }
                    else
                    {
                        actions.Add(ATSwitchWeapon, this, 0.5 - 0.01 * i, i);
                    }
                }
            }
            // primary weapon
            if (_currentWeapon >= 0)
            {
                const MagazineSlot& slot = GetMagazineSlot(_currentWeapon);
                const MuzzleType* muzzle = slot._muzzle;
                if (!muzzle->_primary && primary >= 0)
                {
                    // search for primary weapon
                    actions.Add(ATSwitchWeapon, this, 0.51, primary);
                }
            }
            // use magazines
            AUTO_STATIC_ARRAY(MagazineType*, usedTypes, 32);
            for (int i = 0; i < _magazines.Size(); i++)
            {
                Magazine* magazine = _magazines[i];
                if (magazine && magazine->_ammo > 0 && magazine->_type->_useAction)
                {
                    // check if this magazine type wasn't processed already
                    bool found = false;
                    for (int j = 0; j < usedTypes.Size(); j++)
                    {
                        if (magazine->_type == usedTypes[j])
                        {
                            found = true;
                            break;
                        }
                    }

                    if (!found)
                    {
                        actions.Add(ATUseMagazine, this, 0.519 - 0.0001 * i, magazine->_creator, false, true,
                                    magazine->_id);
                        usedTypes.Add(magazine->_type);
                    }
                }
            }
            // magazines
            const MuzzleType* currentMuzzle = nullptr;
            if (_currentWeapon >= 0)
            {
                currentMuzzle = GetMagazineSlot(_currentWeapon)._muzzle;
                GetReloadActions(this, actions, _currentWeapon, currentMuzzle);
            }
            // background reloading
            int iSlot = 0;
            for (int w = 0; w < _weapons.Size(); w++)
            {
                const WeaponType* weapon = _weapons[w];
                for (int m = 0; m < weapon->_muzzles.Size(); m++)
                {
                    const MuzzleType* muzzle = weapon->_muzzles[m];
                    if (muzzle != currentMuzzle && muzzle->_backgroundReload)
                    {
                        GetReloadActions(this, actions, iSlot, muzzle);
                    }
                    iSlot += muzzle->_nModes;
                }
            }
        }
    }
}

/*!
    \param text displayed text of action
    \param script this script will be performed when action is activated
    \return id of action
*/
int EntityAI::AddUserAction(RString text, RString script)
{
    int index = _userActions.Add();
    UserActionDescription& action = _userActions[index];
    action.id = _nextUserActionId++;
    action.text = text;
    action.script = script;
    return action.id;
}

/*!
    \param id of action
*/
void EntityAI::RemoveUserAction(int id)
{
    for (int i = 0; i < _userActions.Size(); i++)
    {
        if (_userActions[i].id == id)
        {
            _userActions.Delete(i);
            return;
        }
    }
}

/*!
    \param id of action
*/
const UserActionDescription* EntityAI::FindUserAction(int id) const
{
    for (int i = 0; i < _userActions.Size(); i++)
    {
        if (_userActions[i].id == id)
        {
            return &_userActions[i];
        }
    }
    return nullptr;
}

#define ENTITY_EVENT_NAME(type, prefix, name) Foundation::EnumName(prefix##name, #name),

template <>
const Foundation::EnumName* Foundation::GetEnumNames(EntityEvent)
{
    static const Foundation::EnumName EntityEventNames[] = {ENTITY_EVENT_ENUM(EntityEvent, EE, ENTITY_EVENT_NAME)
                                                                Foundation::EnumName()};
    return EntityEventNames;
}

template <>
EntityEvent Foundation::GetEnumCount(EntityEvent)
{
    return NEntityEvent;
}

#undef ENTITY_EVENT_NAME

int EntityAI::AddEventHandler(EntityEvent event, RString expression)
{
    return _eventHandlers[event].Add(expression);
}
void EntityAI::RemoveEventHandler(EntityEvent event, int handle)
{
    _eventHandlers[event].Delete(handle);
}
void EntityAI::ClearEventHandlers(EntityEvent event)
{
    _eventHandlers[event].Clear();
}

const AutoArray<RString>& EntityAI::GetEventHandlers(EntityEvent event) const
{
    return _eventHandlers[event];
}

class GameArrayTypeRef
{
    const GameArrayType& _ref;

  public:
    GameArrayTypeRef(const GameArrayType& ref) : _ref(ref) {}
    operator const GameArrayType&() const { return _ref; }
    operator GameValue() const { return GameValue(_ref); }
};

bool EntityAI::IsEventHandler(EntityEvent event) const
{
    const AutoArray<RString>& handlers = GetEventHandlers(event);
    if (handlers.Size() > 0)
    {
        return true;
    }
    RString handler = GetType()->_eventHandlers[event];
    return handler.GetLength() > 0;
}

void EntityAI::OnEvent(EntityEvent event, const GameValue& pars)
{
    const AutoArray<RString>& handlers = GetEventHandlers(event);
    for (int i = 0; i < handlers.Size(); i++)
    {
        GameVarSpace local;
        GameState* gstate = GWorld->GetGameState();
        gstate->BeginContext(&local);
        gstate->VarSetLocal("_this", pars, true);
        RString handler = handlers[i];
        gstate->Execute(handler);
        gstate->EndContext();
    }
    {
        // execute type based event handler
        RString handler = GetType()->_eventHandlers[event];
        GameVarSpace local;
        GameState* gstate = GWorld->GetGameState();
        gstate->BeginContext(&local);
        gstate->VarSetLocal("_this", pars, true);
        gstate->Execute(handler);
        gstate->EndContext();
    }
}

void EntityAI::OnEvent(EntityEvent event, bool par1)
{
    if (!IsEventHandler(event))
    {
        return;
    }
    GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
    GameArrayType& arguments = value;
    arguments.Add(GameValueExt(this));
    arguments.Add(GameValue(par1));
    OnEvent(event, value);
}

void EntityAI::OnEvent(EntityEvent event, RString par1)
{
    if (!IsEventHandler(event))
    {
        return;
    }
    GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
    GameArrayType& arguments = value;
    arguments.Add(GameValueExt(this));
    arguments.Add(GameValue(par1));
    OnEvent(event, value);
}

void EntityAI::OnEvent(EntityEvent event, RString par1, EntityAI* par2)
{
    if (!IsEventHandler(event))
    {
        return;
    }
    GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
    GameArrayType& arguments = value;
    arguments.Add(GameValueExt(this));
    arguments.Add(GameValue(par1));
    arguments.Add(GameValueExt(par2));
    OnEvent(event, value);
}

void EntityAI::OnEvent(EntityEvent event, RString par1, float par2)
{
    if (!IsEventHandler(event))
    {
        return;
    }
    GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
    GameArrayType& arguments = value;
    arguments.Add(GameValueExt(this));
    arguments.Add(GameValue(par1));
    arguments.Add(GameValue(par2));
    OnEvent(event, value);
}

void EntityAI::OnEvent(EntityEvent event)
{
    if (!IsEventHandler(event))
    {
        return;
    }
    GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
    GameArrayType& arguments = value;
    arguments.Add(GameValueExt(this));
    OnEvent(event, value);
}

void EntityAI::OnEvent(EntityEvent event, EntityAI* par1, float par2)
{
    if (!IsEventHandler(event))
    {
        return;
    }
    GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
    GameArrayType& arguments = value;
    arguments.Add(GameValueExt(this));
    arguments.Add(GameValueExt(par1));
    arguments.Add(GameValue(par2));
    OnEvent(event, value);
}

void EntityAI::OnEvent(EntityEvent event, EntityAI* par1)
{
    if (!IsEventHandler(event))
    {
        return;
    }
    GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
    GameArrayType& arguments = value;
    arguments.Add(GameValueExt(this));
    arguments.Add(GameValueExt(par1));
    OnEvent(event, value);
}

const float ManLockRadius = 1.0;

void EntityAI::PerformLock()
{
    if (_locked && !_tempLocked)
    {
        float manRadius = _lockedSoldier ? ManLockRadius : floatMax(4, _lockedRadius * 0.6);
        _locker->LockPosition(_lockedBeg, _lockedRadius, _lockedSoldier, GetShape()->GeometrySphere());
        _locker->LockPositionMan(_lockedBeg, manRadius);
        _tempLocked = true;
    }
}

void EntityAI::PerformUnlock()
{
    if (_locked && _tempLocked)
    {
        float manRadius = _lockedSoldier ? ManLockRadius : floatMax(4, _lockedRadius * 0.6);
        _locker->UnlockPositionMan(_lockedBeg, manRadius);
        _locker->UnlockPosition(_lockedBeg, _lockedRadius, _lockedSoldier);
        _tempLocked = false;
    }
}

void EntityAI::LockPosition()
{
    if (_static)
    {
        return; // no lock for buildings ...
    }
    // helper functions for lock/unlock
    // lock current position and area around it
    float radius = CollisionSize() * 1.5 + 2.5;
    saturateMax(radius, 4);
    _locked = true;
    // make some shift aside - we prefer having right side free
    Vector3Val lockPos = AimingPosition();
    if (_tempLocked)
    {
        // check if already locked - no change
        float dist2 = (lockPos - _lockedBeg).SquareSizeXZ();
        if (dist2 < Square(0.25f))
        {
            return;
        }
        PerformUnlock();
    }
    // this also helps getting in (drivers gets in from the left)
    _lockedBeg = lockPos;
    _lockedRadius = radius;
    _lockedSoldier = CommanderUnit() ? CommanderUnit()->GetPerson() == this : false;
    PerformLock();
}

void EntityAI::UnlockPosition()
{
    // lock some area around vehicle
    PerformUnlock();
    _locked = false;
}

/* \note
This function is currently never used
*/

bool EntityAI::HasPriorityOver(EntityAI* who) const
{
    if (!who->CommanderUnit())
    {
        return false; // he cannot dodge
    }
    if (!CommanderUnit())
    {
        return true; // I cannot dodge
    }
    // first rule: if I am at his back and he is in front of me, he has priority
    bool iAmInFrontOfHim = who->PositionWorldToModel(Position()).Z() > 0;
    bool heIsInFrontOfMe = PositionWorldToModel(who->Position()).Z() > 0;
    // he has priority
    if (iAmInFrontOfHim && !heIsInFrontOfMe)
    {
        return true;
    }
    if (!iAmInFrontOfHim && heIsInFrontOfMe)
    {
        return false;
    }
    // uncertain: check priority from the right
    return false;
}

void EntityAI::AvoidCollision(float deltaT, float& speedWanted, float& headChange)
{
    AIUnit* unit = PilotUnit();
    if (!unit)
    {
        return;
    }

    const float neutralAside = 0;

    AISubgroup* mySubgrp = unit->GetSubgroup();
    _avoidAsideWanted = neutralAside;

    if (mySubgrp && mySubgrp->GetMode() == AISubgroup::DirectGo)
    {
        return;
    }
    // if we are stopped, do not try to avoid
    const float maxSpeedStopped = 0.05;
    if (fabs(speedWanted) <= maxSpeedStopped)
    {
        return;
    }
    // avoid collisions
    float mySize = CollisionSize(); // assume vehicle is not round
    float mySpeedSize = fabs(ModelSpeed()[2]);
    float maxSpeed = GetType()->GetMaxSpeedMs();
    float myBrakeDist = Square(mySpeedSize) * 0.2;
    float myBrakeTime = mySpeedSize * 0.05;
    float maxDist = floatMax(GetPrecision() * 4, 5) + myBrakeDist * 1.3;
    // check if we are on collision course
    VehicleCollisionBuffer ret;
    float gapFactor = Interpolativ(mySpeedSize, maxSpeed * 0.5, maxSpeed, 0.5, 1);
    float gap = mySize * gapFactor;
    float maxTime = 1.5 + myBrakeTime * 1.3;

    GLOB_LAND->PredictCollision(ret, this, maxTime, gap, maxDist);
    if (ret.Size() <= 0)
    {
        return;
    }

    bool iAmMan = unit->IsSoldier();
    bool iAmHeavy = GetMass() > 5000;

    AIGroup* myGroup = unit->GetGroup();
    AICenter* myCenter = myGroup ? myGroup->GetCenter() : nullptr;

    // precalculate
    float invMaxSpeed = 1 / maxSpeed;

    saturate(speedWanted, -maxSpeed, maxSpeed);
    Vector3 mySpeed = Direction() * speedWanted;

    float maxAvoid = GetPrecision() * 0.5;
    saturate(maxAvoid, 1, 4); // 4m is enough to avoid on any road with any vehicle
    float wantAvoid = 0;

    for (int i = 0; i < ret.Size(); i++)
    {
        const VehicleCollision& info = ret[i];
        const EntityAI* who = info.who;

        // something is near
        // some vehicle
        // determine who should slow down
        // if who is in front of us, slow down to his speed
        // if we are heave and he is enemy soldier, ignore him
        if (iAmHeavy)
        {
            AIUnit* whoUnit = who->CommanderUnit();
            if (whoUnit)
            {
                AIGroup* whoGroup = whoUnit->GetGroup();
                if (whoGroup)
                {
                    AICenter* whoCenter = whoGroup->GetCenter();
                    if (myCenter->IsEnemy(whoCenter->GetSide()))
                    {
                        continue;
                    }
                }
            }
        }

#if _ENABLE_CHEATS
        if (CHECK_DIAG(DECombat))
        {
            Ref<Object> obj = new ObjectColored(GScene->Preloaded(SphereModel), -1);
            obj->SetPosition(info.pos);
            Color color(1, 1, 0, 0.3);
            obj->SetScale(info.distance < gap ? 1 : 0.3);
            obj->SetConstantColor(PackedColor(color));
            GLandscape->ShowObject(obj);
        }
#endif

        if (info.distance < gap)
        {
            // check relative speed and position

#if DIAG_COL
            if (this == GWorld->CameraOn())
            {
                LOG_DEBUG(AI, "{} vs {}", (const char*)GetDebugName(), (const char*)who->GetDebugName());
            }
#endif

            Vector3Val v = mySpeed - who->Speed();
            Vector3Val r = who->Position() - Position();

            // projection of v to r is how much we are getting nearer
            Vector3 nearerV = v.Project(r);
            // nearer is oriented relative speed size
            float nearer = nearerV * r.Normalized();

            // actual object distance
            float distance = info.distance;
            float isNear;
            if (distance < gap)
            {
                isNear = Interpolativ(distance, 0, gap, -maxSpeed * 0.5, -maxSpeed * 0.1);
            }
            else
            {
                isNear = Interpolativ(distance, gap, gap + myBrakeDist, -maxSpeed * 0.1, maxSpeed);
            }

            float dirFactor = nearer * invMaxSpeed;

#if DIAG_COL
            if (this == GWorld->CameraOn())
            {
                LOG_DEBUG(AI, "  nearer {:.3f}, dirFactor {:.3f}", nearer, dirFactor);
            }
#endif
            if (dirFactor <= -0.1)
            {
#if DIAG_COL
                if (this == GWorld->CameraOn())
                {
                    LOG_DEBUG(AI, "  going away");
                }
#endif
                continue; // safe: going away
            }

            if (Airborne())
            {
                // check if obstacle is dangerous to airborne vehicle
                float maxY = who->Position().Y() + who->GetShape()->Max().Y();
                if (maxY < Position().Y() - 10)
                {
                    continue;
                }
            }

            if (who->Static())
            {
                float limSpeed = isNear;
                // static object - should be considered in path planner
                saturateMax(limSpeed, maxSpeed * 0.5);
                saturate(speedWanted, -limSpeed, +limSpeed);

#if DIAG_COL
                if (this == GWorld->CameraOn())
                {
                    LOG_DEBUG(AI, "  Obj {:.1f}, idist {:.1f}, dist {:.1f}, gap {:.1f}, brakeDist {:.1f}",
                              speedWanted * 3.6, info.distance, distance, gap, myBrakeDist);
                }
#endif
            }
            else
            {
                Vector3Val hisSpeedR = DirectionWorldToModel(who->Speed());
                float hisSpeedRZ = hisSpeedR.Z();

                // check collision on predicted position

                float t = info.time;
                Vector3 myPos = Position() + t * Speed();
                Vector3 whoPos = who->Position() + t * who->Speed();

                FrameBase myTrans, whoTrans;
                myTrans.SetTransform(Transform());
                myTrans.SetPosition(myPos);

                whoTrans.SetTransform(who->Transform());
                whoTrans.SetPosition(whoPos);
                // enlarge slightly - be carefull
                myTrans.SetScale(1.1);
                whoTrans.SetScale(1.1);

                bool dangerous = true;
                AIUnit* whoUnit = who->CommanderUnit();
                if (whoUnit && !whoUnit->IsFreeSoldier())
                {
                    // do not check collision with men
                    // assume collision will happen
                    CollisionBuffer objCol;
                    Intersect(objCol, const_cast<EntityAI*>(who), myTrans, whoTrans);
                    dangerous = (objCol.Size() > 0);
                }

                bool sameSubgroup = false;
                if (whoUnit)
                {
                    AISubgroup* whoSubgroup = whoUnit->GetSubgroup();
                    sameSubgroup = (whoSubgroup == mySubgrp);
                }

#if _ENABLE_CHEATS
                if (CHECK_DIAG(DECombat))
                {
                    Ref<Object> obj = new ObjectColored(GScene->Preloaded(SphereModel), -1);
                    float scale = (maxTime - info.time) / maxTime * 2;
                    saturate(scale, 0.2, 2);
                    Color color;
                    obj->SetPosition(info.pos);
                    float danger = -hisSpeedRZ * (1.0 / 10);
                    saturate(danger, 0, 1);
                    color = Color(1, 0, dangerous) * danger + Color(0, 1, dangerous) * (1 - danger);
                    obj->SetScale(scale);
                    obj->SetConstantColor(PackedColor(color));
                    GLandscape->ShowObject(obj);

                    GScene->DrawCollisionStar(myPos, 0.2);
                    GScene->DrawCollisionStar(whoPos, 0.2);
                }
#endif

                // check direction of target
                bool overtaken = false;
                bool overtaking = false;
                float whoSpeedD = who->ObjectSpeed() * Direction();
                float mySpeedD = Speed() * Direction();

                if (!sameSubgroup)
                {
                    // no overtaking/avoiding in same group
                    float avoidThis = (who->CollisionSize() + CollisionSize()) * 0.6;
                    Vector3 whoRelPos = PositionWorldToModel(who->Position());
                    if (fabs(whoRelPos.X()) > 20 && fabs(whoRelPos.X()) > whoRelPos.Z() * 0.5)
                    {
                        // aside of our direction - do not avoid
                        avoidThis = 0;
                    }
                    else if (Direction() * who->Direction() < -0.2)
                    {
                        // opposite direction - both should avoid
                    }
                    else if (whoRelPos.Z() < 0 && whoRelPos.X() < 0)
                    {
                        // he's overtaking us
                        overtaken = true;
                    }
                    else if (whoRelPos.Z() > 0 && whoRelPos.X() > 0)
                    {
                        // we're overtaking him
                        overtaking = true;
                    }
                    else if (mySpeedD > 5)
                    {
                        // I am moving forward
                        overtaking = whoSpeedD < mySpeedD;
                        overtaken = !overtaking;
                    }

                    if (!dangerous)
                    {
                        avoidThis *= 0.5;
                        saturate(avoidThis, -maxAvoid * 0.5, +maxAvoid * 0.5);
#if DIAG_COL
                        if (this == GWorld->CameraOn())
                        {
                            LOG_DEBUG(AI, "  not dangerous");
                        }
#endif
                    }

                    if (overtaking)
                    {
                        // overtake - same direction
                        if (wantAvoid <= 0)
                        {
                            wantAvoid -= avoidThis;
                        }
#if DIAG_COL
                        if (this == GWorld->CameraOn())
                        {
                            LOG_DEBUG(AI, "  overtaking {:.2f}", wantAvoid);
                        }
#endif
                    }
                    else
                    {
                        // avoid - different direction
                        if (wantAvoid >= 0)
                        {
                            wantAvoid += avoidThis;
                        }
#if DIAG_COL
                        if (this == GWorld->CameraOn())
                        {
                            LOG_DEBUG(AI, "  overtaken {:.2f}", wantAvoid);
                        }
#endif
                    }
                } // if( mySubgrp!=whoSubgrp )

                // check if he is behind us

                Vector3 whoRelPos = PositionWorldToModel(who->Position());
                if (whoRelPos.Z() < 0)
                {
                    // he's behind us
                    // do not brake - it would be only worse
                    if (speedWanted > 0)
                    {
#if DIAG_COL
                        if (this == GWorld->CameraOn())
                        {
                            LOG_DEBUG(AI, "  behind us");
                        }
#endif
                        continue;
                    }
                }
                else if (hisSpeedRZ > maxSpeed * 0.1)
                {
                    // he is is front of us - going forward
                    // we can limit speed to his speed
                    float curDist = who->Position().Distance(Position());
                    float maxNear = mySize * 2 + myBrakeDist * 0.6;
                    float curNear = curDist / maxNear - 1;
                    if (!dangerous)
                    {
                        // he is not dangerous - move
                        saturateMax(curNear, 0.1 * maxSpeed);
                    }
                    saturateMin(isNear, curNear);
                }

                if (who->Speed().SquareSize() < Square(2))
                {
                    // he is stopped - we have to move
                    if (!sameSubgroup)
                    {
                        saturateMax(isNear, 0.1 * maxSpeed);
                    }
                }

                {
                    float limSpeed = isNear + hisSpeedRZ;
                    float carelessSpeed = hisSpeedRZ + maxSpeed * 0.3;
                    limSpeed = Interpolativ(dirFactor, 0, 0.1, carelessSpeed, limSpeed);
                    // moving vehicle very near - slow down
                    if (!whoUnit)
                    {
                        // vehicle empty - should be considered in path planner
                        limSpeed = carelessSpeed;
                    }
                    else
                    {
                        float limSpeedMin = 0;
                        if (iAmMan && whoUnit->IsSoldier())
                        {
                            // man vs. man: never stop
                            limSpeedMin = 1;
                        }
                        else if (who->IsOnRoadMoving(3) && IsOnRoadMoving(3))
                        {
                            // if both are moving and on road, there is no need to stop
                            if (overtaking)
                            {
                                limSpeedMin = hisSpeedRZ + maxSpeed * 0.2;
                            }
                            else
                            {
                                limSpeedMin = dangerous ? 6 : 12;
                            }
                            // if we are heading same direction
                            // we will overtake and should not brake under his speed + some reserve
                        }
                        else
                        {
                            // stop if neccessary
                            limSpeedMin = dangerous ? 0 : 6;
                        }
                        saturateMax(limSpeed, limSpeedMin);
                    }

                    // keep braking for some time
                    if (Glob.time > _avoidSpeedTime || _avoidSpeed > limSpeed)
                    {
                        _avoidSpeedTime = Glob.time + 1;
                        _avoidSpeed = limSpeed;
#if DIAG_COL
                        if (this == GWorld->CameraOn())
                        {
                            LOG_DEBUG(AI, "  limit speed {:.1f}", limSpeed);
                        }
#endif
                    }
                    saturate(speedWanted, -limSpeed, +limSpeed);
                }

#if DIAG_COL
                if (this == GWorld->CameraOn())
                {
                    LOG_DEBUG(AI, "  Col {:.1f} dist {:.1f}, gap {:.1f}, brakeDist {:.1f}, dir {:.2f}",
                              speedWanted * 3.6, distance, gap, myBrakeDist, dirFactor);
                }
#endif
            }
        } // if( info.distance<gap )
        else
        {
#if DIAG_COL
            if (this == GWorld->CameraOn())
            {
                LOG_DEBUG(AI, "{} vs {} ignored ({:.2f}>{:.2f})", (const char*)GetDebugName(),
                          (const char*)who->GetDebugName(), info.distance, gap);
            }
#endif
        }
    } // for(i)

    saturate(wantAvoid, -maxAvoid, +maxAvoid);
    _avoidAsideWanted = wantAvoid + neutralAside;

    if (fabs(speedWanted) < maxSpeedStopped || _objectContact)
    {
        CreateFreshPlan();
    }
}

void EntityAI::CreateFreshPlan()
{
    // do not report in DirectGo mode
    AIUnit* unit = PilotUnit();
    if (!unit)
    {
        return;
    }
    if (unit->GetSubgroup()->GetMode() == AISubgroup::DirectGo)
    {
        return;
    }
    Path& path = unit->GetPath();
    if (path.Size() >= 2)
    {
        // check if we have recent path
        if (path.GetSearchTime() < Glob.time - 5)
        {
            // replan
            if (unit->IsSubgroupLeader())
            {
                unit->SendAnswer(AI::StepTimeOut);
            }
            else
            {
                unit->ForceReplan();
            }
        }
    }
}

Vector3 EntityAI::SteerPoint(float spdTime, float costTime)
{
    // calculate point on trajectory in time (relative in sec)
    // estimate position after time
    AIUnit* unit = PilotUnit();
    if (!unit)
    {
        return Position();
    }
    const Path& path = unit->GetPath();
    // no path - no steering
    if (path.Size() < 2)
    {
        return Position() + Direction();
    }
    Vector3Val sPos = Position() + Speed() * spdTime + 0.5 * spdTime * spdTime * _acceleration;

    float cost = path.CostAtPos(sPos) + costTime;

    Vector3Val pos = path.PosAtCost(cost, Position());

    return pos;
}

float EntityAI::GetPathCost(const GeographyInfo& info, float dist) const
{
    float cost = GetFieldCost(info) * GetCost(info);
    if (cost > GET_UNACCESSIBLE)
    {
        cost = GetType()->GetMinCost() * 2;
    }
    return cost * dist;
}

void EntityAI::FillPathCost(Path& path) const
{
    if (path.Size() <= 0)
    {
        return;
    }

    Vector3 lastPos = path[0]._pos;
    path[0]._cost = 0;

    float sumCost = 0;
    for (int i = 1; i < path.Size(); i++)
    {
        Vector3 pos = path[i]._pos;
        float dist = pos.Distance(lastPos);

        // update cost info if necessary
        int cx = toIntFloor(lastPos.X() * InvLandGrid);
        int cz = toIntFloor(lastPos.Z() * InvLandGrid);
        GeographyInfo geogr = GLOB_LAND->GetGeography(cx, cz);

        float cost = GetPathCost(geogr, dist);

        sumCost += cost;

        path[i]._cost = sumCost;

        lastPos = pos;
    }
}

float EntityAI::VisibleMovement() const
{
    float camouflage = GetType()->_camouflage;
    float vis = camouflage;

    saturateMax(vis, _shootVisible); // firing target is better visible
    float rSpeed = fabs(ModelSpeed().Z()) * 3;
    if (rSpeed > vis * GetRadius())
    { // moving target is better visible
        float relSpeed = rSpeed / GetRadius();
        saturateMin(relSpeed, 8);
        saturateMax(vis, relSpeed * camouflage);
    }
    return vis;
}

float EntityAI::VisibleLights() const
{
    return 0;
}

float EntityAI::Audible() const
{
    float aud = EngineIsOn() ? 3 : 0.1;
    float rSpeed = fabs(ModelSpeed().Z());
    if (rSpeed > GetRadius() * 0.5)
    { // moving target is better visible
        float relSpeed = rSpeed * 0.3 / GetRadius();
        saturateMin(relSpeed, 2);
        aud += relSpeed;
    }
    aud *= GetType()->GetAudible();
    saturateMax(aud, _shootAudible); // firing target is better audible
    return aud;
}

float EntityAI::GetHidden() const
{
    return 1;
}

float EntityAI::VisibleFire() const
{
    return _shootVisible;
}
float EntityAI::AudibleFire() const
{
    return _shootAudible;
}

TargetType* EntityAI::FiredAt() const
{
    return _shootTarget;
}

float EntityAI::GetExplosives() const
{
    return GetAmmoHit() * 0.015f; // assume in-place explosion is much less effective
}

inline float CalcHitDammage(float distance2, float valRange2)
{
    if (distance2 <= valRange2)
    {
        return 1;
    }
    else
    {
        return valRange2 * valRange2 / (distance2 * distance2);
    }
}

RString EntityAI::HitpointName(int i) const
{
    const HitPointList& hitpoints = GetType()->GetHitPoints();
    if (i >= hitpoints.Size())
    {
        return RString();
    }
    if (i < 0)
    {
        return RString();
    }
    int sel = hitpoints[i]->GetSelection();
    LODShape* lShape = GetShape();
    if (!lShape)
    {
        return RString();
    }

    // FIX: hitpoint with no selection - avoid crash
    if (sel < 0)
    {
        LOG_ERROR(AI, "{}: Hitpoint {} is in no selection", (const char*)lShape->GetName(), i);
        return RString();
    }

    Shape* hits = lShape->HitpointsLevel();
    if (!hits)
    {
        return RString();
    }
    const NamedSelection& nsel = hits->NamedSel(sel);
    return nsel.GetName();
}

} // namespace Poseidon
