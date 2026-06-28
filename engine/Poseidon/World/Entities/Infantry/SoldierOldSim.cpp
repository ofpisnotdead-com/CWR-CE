#include <Poseidon/World/Entities/Infantry/SoldierOldCommon.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Network/NetworkCustomAssets.hpp>
#include <limits.h>
#include <stdio.h>
#include <cmath>
#include <Poseidon/Foundation/Algorithms/Qsort.hpp>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>
#include <Random/randomGen.hpp>

namespace Poseidon
{
using namespace Foundation;

bool Man::SimulateAnimations(float& turn, float& moveX, float& moveZ, float deltaT, SimulationImportance prec)
{
    bool change = false;

    if (_primaryMove.id == MoveIdNone)
    {
        // doing nothing: switch to action stand depending on current UpDegree
        _primaryMove = MotionPathItem(GetDefaultMove());
    }

    float maxRunTired = _tired * TiredRunSpeed + (1 - _tired) * FastRunSpeed;

    float speedZ, minSpd, maxSpd;
    GetRelSpeedRange(speedZ, minSpd, maxSpd);

    float adjustTimeCoef = 1.0f;
    if (!QIsManual())
    {
        // now we can adjust the speed
        if (fabs(speedZ) > 1e-6)
        {
            adjustTimeCoef = _walkSpeedWanted / speedZ;
            saturate(adjustTimeCoef, minSpd, maxSpd);
        }
    }
    else
    {
        // limit movement by tired in range <TiredRunSpeed,FastRunSpeed>
        if (fabs(speedZ) > 1e-6)
        {
            if (speedZ > maxRunTired)
            {
                adjustTimeCoef = maxRunTired / speedZ;
                saturate(adjustTimeCoef, minSpd, maxSpd);
            }
        }
    }

    turn = 0;
    moveX = 0;
    moveZ = 0;

    // adjustTimeCoef also determines speed
    change = AdvanceMoveQueue(deltaT, adjustTimeCoef, moveX, moveZ, prec);

    if (_recoil)
    {
        change = true;
    }

    if (_turnToDo)
    {
        turn = -fSign(_turnToDo) * 32 * deltaT;
        if (fabs(turn) >= fabs(_turnToDo))
        {
            turn = -_turnToDo;
            _turnToDo = 0;
        }
    }
    else
    {
        turn = -_turnWanted * deltaT;
    }

    ActionMap* map = Type()->GetActionMap(_primaryMove.id);
    float turnSpeed = map ? map->GetTurnSpeed() : 0;
    saturate(turn, -turnSpeed * deltaT, +turnSpeed * deltaT);

    if (fabs(turn) > 1e-3)
    {
        change = true;
    }

#if _ENABLE_CHEATS
    if (this == GWorld->CameraOn() && CHECK_DIAG(DEAnimation))
    {
        char buf[1024];
        *buf = 0;
#if 1
        ActionMap* map = Type()->GetActionMap(_primaryMove.id);
        sprintf(buf + strlen(buf), " deg %d", map ? map->GetUpDegree() : ManPosStand);
        AnimationRT* pri = Type()->GetAnimation(_primaryMove.id);
        sprintf(buf + strlen(buf), " %s - Prim %s:%.2f, Sec %s:%.2f, PCoef %.2f ",
                pri ? (const char*)pri->Name() : "nullptr", NAME_T(Type(), _primaryMove.id), _primaryTime,
                NAME_T(Type(), _secondaryMove.id), _secondaryTime, _primaryFactor);
        if (_queueMove.Size() > 0)
        {
            MoveId nextMove = _queueMove[0].id;
            sprintf(buf + strlen(buf), "Next %s ", NAME_T(Type(), nextMove));
        }
#else
        sprintf(buf + strlen(buf), "In %d ", InsideLOD(GWorld->GetCameraType()));
#endif

        GlobalShowMessage(200, buf);
    }
#endif

    return change;
}

void Man::BasicSimulationCore(float deltaT, SimulationImportance prec)
{
    _head.Simulate(Type()->_head, deltaT, prec, _isDead);

    if (_isDead)
    {
        _turnWanted = 0;

        ActionMap* map = Type()->GetActionMap(_primaryMove.id);
        // get which action is it (based on action map)
        if (map)
        {
            MoveId moveWanted = map->GetAction(ManActDie);
            if (moveWanted != MoveIdNone)
            {
                Ref<ActionContextDefault> context = new ActionContextDefault;
                context->function = MFDead;
                SetMoveQueue(MotionPathItem(moveWanted, context), prec <= SimulateVisibleFar);
            }
            _forceMove = MotionPathItem((MoveId)MoveIdNone);
            _externalQueue.Clear();
            _externalMove = MotionPathItem((MoveId)MoveIdNone);
        }
        _head.SetMimicMode("dead");
    }
    else
    {
        AIUnit* unit = _brain;
        if (unit)
        {
            CombatMode mode = unit->GetCombatMode();
            if (unit->IsDanger())
            {
                _head.SetMimicMode("danger");
            }
            else if (_whenScreamed < Glob.time && _whenScreamed > Glob.time - 5)
            {
                _head.SetMimicMode("hurt");
            }
            else
            {
                switch (mode)
                {
                    case CMAware:
                        if (FindWeaponType(MaskSlotPrimary | MaskSlotHandGun) >= 0)
                        {
                            _head.SetMimicMode("aware");
                        }
                        else
                        {
                            _head.SetMimicMode("safe");
                        }
                        break;
                    case CMCombat:
                    case CMStealth:
                        _head.SetMimicMode("combat");
                        break;
                    default:
                        _head.SetMimicMode("safe");
                        break;
                }
            }
        }
    }
}

float Man::GetLegPhase() const
{
    return _primaryTime;
}

void Man::BasicSimulation(float deltaT, SimulationImportance prec, float speedFactor)
{
    float turn, moveX, moveZ;

    RefreshMoveQueue(prec <= SimulateVisibleFar);

    bool forceRecalcMatrix = MoveHead(deltaT);
    if (forceRecalcMatrix)
    {
        RecalcGunTransform();
    }
    SimulateAnimations(turn, moveX, moveZ, deltaT * speedFactor, prec);

    BasicSimulationCore(deltaT, prec);
}

void Man::PlaceOnSurface(Matrix4& trans)
{
    // assume landcontact point is always at y=0
    // calculate animated point position
    // place in steady position
    Vector3 pos = trans.Position();
    Matrix3 orient = trans.Orientation();

    pos[1] = GLOB_LAND->RoadSurfaceYAboveWater(pos + VUp * 0.5f);

    trans.SetPosition(pos);
}

void Man::RecalcPositions(const Frame& moveTrans)
{
    _aimingPositionWorld = CalculateAimingPosition(moveTrans);
    _cameraPositionWorld = CalculateCameraPosition(moveTrans);
    // A not-yet-placed soldier (ungrouped, still at the map origin during the MP
    // briefing) momentarily computes aiming/camera positions within ~10m of [0,0,0];
    // it is placed at its mission position once play begins. Transient and harmless, so
    // surface it at debug level instead of asserting (asserting here aborts a strict
    // client before it can leave any infantry mission's briefing).
    if (_aimingPositionWorld.SquareSize() <= Square(10) || _cameraPositionWorld.SquareSize() <= Square(10))
    {
        LOG_DEBUG(World, "soldier '{}' near world origin in RecalcPositions (unplaced)", (const char*)GetDebugName());
    }

    // animate reflectors position
    int level = _shape->FindMemoryLevel();
    if (level >= 0)
    {
        for (int i = 0; i < _reflectors.Size(); i++)
        {
            const ReflectorInfo& info = Type()->_reflectors[i];
            LightReflectorOnVehicle* light = _reflectors[i];
            AttachedOnVehicle* attached = light;
            if (info.positionIndex >= 0)
            {
                Vector3Val position = AnimatePoint(level, info.positionIndex);
                Vector3 direction = VForward;
                if (info.directionIndex >= 0)
                {
                    direction = AnimatePoint(level, info.directionIndex) - position;
                    direction.Normalize();
                }
                attached->SetAttachedPos(position, direction);
            }
        }
    }
}

void Man::OnPositionChanged()
{
    _landContact = false;
    _objectContact = false;
    RecalcPositions(*this);
}

void Man::Simulate(float deltaT, SimulationImportance prec)
{
    if (_ladderBuilding && _ladderIndex >= 0)
    {
        if (IsDammageDestroyed())
        {
            DropLadder(_ladderBuilding, _ladderIndex);
        }
        else
        {
            SimLadderMovement(deltaT, prec);
        }

        base::Simulate(deltaT, prec);
        return;
    }

    float turn, moveX, moveZ;

    bool change = SimulateAnimations(turn, moveX, moveZ, deltaT, prec);

    if (_objectContact)
    {
        change = true;
    }

    BasicSimulationCore(deltaT, prec);

    // if object is frozen, do not simulate it
    if (!CheckPredictionFrozen())
    {
        Vector3 position = Position();

        // simulate interaction with land
        Vector3 friction(VZero), torqueFriction(VZero);
        Vector3 force(VZero), torque(VZero);
        Vector3 pForce(VZero), pCenter(VZero);
        bool freeFall = false;

        float landLegsFactor = 0;
        float bankCorrectFactor = 0;

        Frame moveTrans = *this;

        if (!_landContact && !_objectContact)
        {
            _freeFallUntil = Glob.time + 0.3f;
        }
        if (Glob.time < _freeFallUntil)
        {
            freeFall = true;

            Vector3Val speed = ModelSpeed();
            friction[0] = speed[0] * fabs(speed[0]) * 0.002f * GetMass() + speed[0] * 0.01f * GetMass();
            friction[1] = speed[1] * fabs(speed[1]) * 0.002f * GetMass() + speed[1] * 0.01f * GetMass();
            friction[2] = speed[2] * fabs(speed[2]) * 0.002f * GetMass() + speed[2] * 0.01f * GetMass();

            Matrix4 movePos;
            ApplySpeed(movePos, deltaT);
            moveTrans.SetTransform(movePos);

            pForce[0] = 0;
            pForce[1] = -G_CONST * GetMass();
            pForce[2] = 0;
            force += pForce;

            saturate(_angMomentum[0], -10, +10);
            saturate(_angMomentum[1], -10, +10);
            saturate(_angMomentum[2], -10, +10);
            if (_landContact)
            {
                torqueFriction = _angMomentum * 1;
                friction[0] += fSign(speed[0]) * 100 + speed[0] * 0.5f * GetMass();
                friction[1] += fSign(speed[1]) * 1000 + speed[1] * 5 * GetMass();
                friction[2] += fSign(speed[2]) * 100 + speed[2] * 0.5f * GetMass();
            }
            else
            {
                torqueFriction = _angMomentum * 0.3f;
                float surfaceY = GLOB_LAND->RoadSurfaceY(Position() + VUp * 0.5f);
                if (Position().Y() - surfaceY > 5)
                {
                    pCenter = Vector3(-0.1f, 0.4f, 0.2f);
                    torque += pCenter.CrossProduct(friction);
                }
            }

            friction = DirectionModelToWorld(friction);
            torque = DirectionModelToWorld(torque);

            change = true;
        }
        else if (IsDead())
        {
            if (_whenKilled == Time(0))
            {
                _whenKilled = Glob.time;
                IsMoved();
                _lastMovementTime = Glob.time;
            }
            _speed = VZero;
            _angMomentum = VZero;
            Matrix3 orient;
            float dx, dz;
            GLOB_LAND->RoadSurfaceY(Position() + VUp * 0.5f, &dx, &dz);
            Vector3Val landUp = Vector3(-dx, 1, -dz).Normalized();
            Vector3 toUp = landUp - DirectionUp();
            float maxD = 1 * deltaT;
            saturate(toUp[0], -maxD, +maxD);
            saturate(toUp[1], -maxD, +maxD);
            saturate(toUp[2], -maxD, +maxD);
            orient.SetUpAndDirection(DirectionUp() + toUp, Direction());
            // orient.SetScale(_manScale);

            moveTrans.SetPosition(Position());
            moveTrans.SetOrientation(orient);
        }
        else if (!change)
        {
            // optimize easy case - soldier is static
            _speed = VZero;
            _angMomentum = VZero;
        }
        else
        {
            // correct speedX
            Vector3 xzSpeed;
            float invDeltaT = 1 / deltaT;
            DirectionModelToWorld(xzSpeed, Vector3(moveX, 0, moveZ));
            xzSpeed[1] = 0;
            _speed = xzSpeed * invDeltaT;
            _angMomentum = VZero;
            position += xzSpeed;

            float onLand = ValueTest(&MoveInfo::OnLand);
            Vector3 landUp = VUp;
            landLegsFactor = 1 - onLand;
            if (onLand > 0)
            {
                float dx, dz;
                GLOB_LAND->RoadSurfaceY(position + VUp * 0.5f, &dx, &dz);
                // limit skew to certain extent
                landUp = Vector3(-dx, 1, -dz).Normalized() * onLand + VUp * (1 - onLand);
            }
            bankCorrectFactor = GetLimitGunMovement(); // 1;

            // check current up and turn
            if (fabs(turn) > 1e-6 || landUp.Distance2(DirectionUp()) > 1e-6)
            {
                Matrix3 orient;
                Matrix3 rotate(MRotationY, turn);
                orient.SetUpAndDirection(landUp, rotate * Direction());
                moveTrans.SetOrientation(orient);
            }
            else
            {
                moveTrans.SetOrientation(Orientation());
            }

            moveTrans.SetPosition(position);
        }

        const MoveInfo* priInfo = Type()->GetMoveInfo(_primaryMove.id);
        const MoveInfo* secInfo = Type()->GetMoveInfo(_secondaryMove.id);
        float priDuty = priInfo ? priInfo->GetDuty() : 0;
        float secDuty = secInfo ? secInfo->GetDuty() : 0;

        float duty = priDuty * _primaryFactor + secDuty * (1 - _primaryFactor);

        // personality duration
        float personality = GetInvAbility();
        if (duty < 0)
        {
            personality = 1; // recovery rate is uniform across skill levels
        }
        // "bad" soldier are tired much faster
        _tired += duty * (1.0f / 20) * personality * deltaT;
        saturate(_tired, 0, 1);

        Vector3 wCenter(NoInit);
        moveTrans.PositionModelToWorld(wCenter, GetCenterOfMass());

        float underBias = 0.5f;
        if (!_landContact || freeFall && Speed().Y() > 0.5f)
        {
            underBias = 0;
        }

        bool wasLandContact = _landContact;
        _landContact = false;

        bool steadyBody = false;
        float landDX = 0, landDZ = 0;
        {
            float maxUnder = -underBias;
            if (!_isDead)
            {
                if (!change)
                {
                    // remain static
                    _landContact = true;
                    maxUnder = 0;
                    // landDX?
                }
                else
                {
                    // calculate landcontact point position

                    int level = GetShape()->FindLandContactLevel();
                    DoAssert(level >= 0);

                    _waterDepth = 0;
                    GroundCollisionBuffer retVal;
                    GLOB_LAND->GroundCollision(retVal, this, moveTrans, underBias, 0, true, true);
                    bool waterContact = false;
                    if (retVal.Size() > 0)
                    {
                        for (int i = 0; i < retVal.Size(); i++)
                        {
                            const UndergroundInfo& info = retVal[i];
                            if (info.under < 0)
                            {
                                continue;
                            }
                            if (info.type == GroundSolid)
                            {
                                float under = info.under - underBias;
                                if (maxUnder < under)
                                {
                                    maxUnder = under;
                                    _landContact = true;
                                    landDX = info.dX, landDZ = info.dZ;
                                    if (info.texture)
                                    {
                                        _surfaceSound = info.texture->GetSoundEnv();
                                    }
                                    // LOG_DEBUG(Physics, "ground {:.2f},{:.2f}",landDX,landDZ);
                                }
                            }
                            else if (info.type == GroundWater)
                            {
                                saturateMax(_waterDepth, info.under - underBias);
                                waterContact = true;
                            }
                        }
                    }
                    if (waterContact)
                    {
                        const SurfaceInfo& info = GLandscape->GetWaterSurface();
                        _surfaceSound = info._soundEnv;
                    }
                }
            }
            else
            {
                if (IsDead() && _whenKilled < Glob.time - 5 && _lastMovementTime < Glob.time - 5 && wasLandContact &&
                    _hideBody >= _hideBodyWanted)
                {
                    // simulation suspended - dead body is in steady position
                    _landContact = true;
                    steadyBody = true;
                    maxUnder = 0;
                    if (_hideBody >= 1 && !_brain)
                    {
                        SetDelete();
                    }
                }
                else
                {
                    float delta = _hideBodyWanted - _hideBody;
                    Limit(delta, -0.1f * deltaT, +0.1f * deltaT);
                    _hideBody += delta;

                    float under = underBias - _hideBody;

                    _waterDepth = 0;
                    GroundCollisionBuffer retVal;
                    GLOB_LAND->GroundCollision(retVal, this, moveTrans, under, 0, true, true);
                    for (int i = 0; i < retVal.Size(); i++)
                    {
                        const UndergroundInfo& info = retVal[i];
                        if (info.under < 0)
                        {
                            continue;
                        }
                        if (info.type == GroundSolid)
                        {
                            saturateMax(maxUnder, info.under - underBias);
                            _landContact = true;
                            landDX = info.dX, landDZ = info.dZ;
                            // LOG_DEBUG(Physics, "dead ground {:.2f},{:.2f}",landDX,landDZ);
                        }
                        else if (info.type == GroundWater)
                        {
                            saturateMax(_waterDepth, info.under - underBias);
                        }
                    }
                }
            }

            if (_landContact && Glob.time > _disableDammageUntil)
            {
                float maxCrashSpeed = 10;
                if (_speed.SquareSize() >= Square(maxCrashSpeed))
                {
                    float dammage = (_speed.Size() - maxCrashSpeed) * 0.1f;
                    float armorCoef = GetInvArmor() * 3;
                    dammage *= armorCoef;
                    LocalDammage(nullptr, this, VZero, dammage, 2.0f);
                }
            }
            Vector3 position = moveTrans.Position();
            position[1] += maxUnder;
            moveTrans.SetPosition(position);
        }

        // collision testing
        if (deltaT > 0 && (change || prec <= SimulateVisibleNear))
        {
            Vector3 totForce(VZero);

            float crash = 0;

            _objectContact = false;
            if (QIsManual())
            {
                Vector3 oldPos = AimingPosition();
                Vector3 relAim = PositionWorldToModel(oldPos);
                Vector3 newPos = moveTrans.FastTransform(relAim);
                // check collision on line oldPos / newPos

                // GScene->DrawCollisionStar(oldPos,0.1,PackedColor(Color(0,1,0)));
                // GScene->DrawCollisionStar(newPos,0.1,PackedColor(Color(1,1,0)));
                CollisionBuffer collision;
                if (newPos.Distance2(oldPos) > Square(0.1f))
                {
                    GLandscape->ObjectCollision(collision, this, nullptr, oldPos, newPos, 0.5f, ObjIntersectGeom);
                }
                if (collision.Size() > 0)
                {
                    float minT = 1e10;
                    for (int i = 0; i < collision.Size(); i++)
                    {
                        const CollisionInfo& info = collision[i];
                        Object* obj = info.object;
                        if (!obj)
                        {
                            continue;
                        }
                        if (obj->IsPassable())
                        {
                            continue;
                        }
                        saturateMin(minT, info.under);
#if _ENABLE_CHEATS
                        if (CHECK_DIAG(DECollision))
                        {
                            LOG_DEBUG(Physics, "Component {}", info.component);
                            // draw corresponding component
                            Shape* geom = obj->GetShape()->GeometryLevel();
                            BString<256> compo;
                            sprintf(compo, "Component%02d", info.component);
                            int ns = geom->FindNamedSel(compo);
                            if (ns >= 0)
                            {
                                const NamedSelection& sel = geom->NamedSel(ns);
                                for (int s = 0; s < sel.Size(); s++)
                                {
                                    Vector3Val v = geom->Pos(sel[s]);
                                    Vector3Val w = obj->PositionModelToWorld(v);
                                    GScene->DrawCollisionStar(w, 0.5f, PackedColor(Color(0, 1, 0)));
                                }
                            }
                        }
#endif
                    }
                    if (minT < 1)
                    {
                        // clamp to prevent getting stuck at 0
                        saturateMax(minT, 0.01f);
                        Vector3 col = Position() * (1 - minT) + moveTrans.Position() * minT;

                        moveTrans.SetPosition(col);
#if _ENABLE_CHEATS
                        if (CHECK_DIAG(DECollision))
                        {
                            Vector3 rcol = oldPos * (1 - minT) + newPos * minT;
                            GScene->DrawCollisionStar(col, 0.02f, PackedColor(Color(1, 1, 0)));
                            GScene->DrawCollisionStar(rcol, 0.02f, PackedColor(Color(1, 0.5, 0)));
                            GScene->DrawCollisionStar(oldPos, 0.05f, PackedColor(Color(0.5, 0.5, 0)));
                            GScene->DrawCollisionStar(newPos, 0.05f, PackedColor(Color(0.5, 0.5, 0)));
                        }
                        // check nearest poi
#endif
                    }
                }
            }
            if (prec <= SimulateVisibleFar && !steadyBody)
            {
#define MAX_IN 0.05f
                CollisionBuffer collision;
                bool onlyVehicles = !QIsManual() && !freeFall;
                GLandscape->ObjectCollision(collision, this, moveTrans, onlyVehicles);

                Vector3 offset = VZero;

                int minHierLevel = INT_MAX;

                for (int i = 0; i < collision.Size(); i++)
                {
                    saturateMin(minHierLevel, collision[i].hierLevel);
                }

                for (int i = 0; i < collision.Size(); i++)
                {
                    const CollisionInfo& info = collision[i];
                    // skip proxy collisions when a higher-level collision was found first
                    if (info.hierLevel > minHierLevel)
                    {
                        continue;
                    }
                    if (!info.object)
                    {
                        continue;
                    }
                    if (info.object->IsPassable())
                    {
                        continue;
                    }
                    // if we have no geometry, and it is another soldier, we should not collide
                    if (!HasGeometry() && !freeFall && dyn_cast<Man, Object>(info.object))
                    {
                        continue;
                    }

                    // roadway collision handles ground contacts; skip object collision near road surface
                    if (info.object->GetShape()->FindRoadwayLevel() >= 0)
                    {
                        float under = GLandscape->UnderRoadSurface(info.object, info.pos, 0);
                        if (under > 0 && under < 1.0f)
                        {
                            continue;
                        }
                    }

                    _objectContact = true;
                    _lastObjectContactTime = Glob.time;

                    Point3 pos = info.object->PositionModelToWorld(info.pos);
                    Vector3 dirOut = info.object->DirectionModelToWorld(info.dirOut);

                    float offsetInOutDir = dirOut * offset;
                    saturateMax(offsetInOutDir, 0);
                    float under = info.under - offsetInOutDir;

                    if (under > MAX_IN)
                    {
                        float moveOut = under - MAX_IN;
                        Vector3 offsetAdd = dirOut * moveOut;
                        offset += offsetAdd;

                        Vector3Val objSpeed = info.object->ObjectSpeed();
                        Vector3 colSpeed = _speed - objSpeed;

                        if (info.object->ObjectSpeed().SquareSize() < Square(7))
                        {
                            // limit relative speed to object we crashed into
                            const float maxRelSpeed = 0.5f;
                            if (colSpeed.SquareSize() > Square(maxRelSpeed))
                            {
                                // adapt _speed to match criterion
                                crash += (colSpeed.Size() - maxRelSpeed) * 0.3f;
                                colSpeed.Normalize();
                                colSpeed *= maxRelSpeed;
                            }
                            _speed = objSpeed + colSpeed;
                        }
                    }
                } // for all objects

                if (offset.SquareSize() > Square(0.01f))
                {
                    Matrix4 transform = moveTrans.Transform();
                    Point3 newPos = transform.Position();
                    newPos += offset;

                    transform.SetPosition(newPos);
                    moveTrans.SetTransform(transform);

                    _lastMovementTime = Glob.time;

                    const float crashLimit = 0.3f;
                    float moveSize = offset.Size();
                    if (moveSize > crashLimit)
                    {
                        crash += moveSize - crashLimit;
                    }
                    change = true;
                }
            } // if( object collisions enabled )
            if (_landContact || _objectContact)
            {
                _speed[1] = _speed[0] * landDX + _speed[2] * landDZ;
            }
        }

        if (change)
        {
            float legDX = landDX;
            float legDZ = landDZ;
            saturate(legDX, -0.5f, +0.5f);
            saturate(legDZ, -0.5f, +0.5f);

            Vector3 dxdz(legDX, 0, legDZ);
            DirectionWorldToModel(dxdz, dxdz);

            _legTrans(1, 0) = dxdz.X() * landLegsFactor;
            _legTrans(1, 2) = dxdz.Z() * landLegsFactor;

            // calculate bank correction
            if (QIsManual())
            {
                Vector3 relUp = (WorldToModel().DirectionUp() * bankCorrectFactor + VUp * (1 - bankCorrectFactor));

                _correctBankSin = -relUp.X();
                _correctBankCos = sqrt(1 - _correctBankSin * _correctBankSin);
            }
            else
            {
                _correctBankSin = 0;
                _correctBankCos = 1;
            }
        }

        float impulse2 = _impulseForce.SquareSize();
        if (!_landContact && !_objectContact || impulse2 > Square(GetMass() * 5))
        {
            _lastMovementTime = Glob.time;
            if (impulse2 > Square(GetMass() * 5))
            {
                // LOG_DEBUG(Physics, "impulse {:.2f}",sqrt(impulse2));
                float contact = sqrt(impulse2) * GetInvMass() * (1.0f / 5) - 1;
                float armorCoef = GetInvArmor() * 3;
                contact *= armorCoef;
                if (contact > 0.1f)
                {
                    // LOG_DEBUG(Physics, "Contact {:.2f}",contact);
                    saturateMin(contact, 5);
                    LocalDammage(nullptr, this, VZero, contact, 1.0f);
                }
                if (impulse2 > Square(GetMass() * 60))
                {
                    _impulseForce = _impulseForce.Normalized() * (GetMass() * 60);
                }
            }
            _freeFallUntil = Glob.time + 1.0f;
            freeFall = true;
        }
        if (freeFall)
        {
            ApplyForces(deltaT, force, torque, friction, torqueFriction);
            _lastMovementTime = Glob.time;
        }
        else
        {
            if (_speed.SquareSize() >= Square(0.1f))
            {
                _lastMovementTime = Glob.time;
            }
            _acceleration = VZero;
            _angMomentum = VZero;
            _angVelocity = VZero;
            _impulseForce = VZero;
            _impulseTorque = VZero;
        }

        Move(moveTrans);
        DirectionWorldToModel(_modelSpeed, _speed);

        if (change)
        {
            RecalcPositions(moveTrans);
        }

        if (_waterDepth > 0)
        {
            _waterContact = true;
            float maxSafeDepth = 1.2f;
            if (IsDown())
            {
                maxSafeDepth = 0.6f;
            }
            if (_waterDepth > maxSafeDepth)
            {
                float drown = (_waterDepth - maxSafeDepth) * (1.0f / 0.5f);
                saturateMin(drown, 2);
                LocalDammage(nullptr, this, VZero, 0.05f * deltaT * drown, 1.0f);
            }
        }
        else
        {
            _waterContact = false;
        }
    } // if (!CheckPredictionFrozen())

    if (_mGunClouds.Active() || _mGunFire.Active() || _gunClouds.Active())
    {
        Vector3 gunPos = PositionModelToWorld(GetWeaponPoint(_currentWeapon));
        Vector3 dir = GetWeaponDirection(_currentWeapon);
        _mGunFire.Simulate(gunPos, deltaT);
        _mGunClouds.Simulate(gunPos, Speed() * 0.7f + dir * 5.0f, 0.35f, deltaT);
        _gunClouds.Simulate(gunPos, Speed() * 0.7f + dir * 2.0f, 0.35f, deltaT);
    }

    if (IsLocal())
    {
        if (QIsManual())
        {
            if (LauncherSelected())
            {
                Missile* missile = dyn_cast<Missile, Vehicle>(_lastShot);
                if (missile)
                {
                    Vector3 dir = Position() + GetEyeDirection() * 1000;
                    missile->SetControlDirection(dir);
                }
            }
        }

        if (!LaserSelected())
        {
            _laserTargetOn = false;
        }
    }

    MoveWeapons(deltaT, change);

    base::Simulate(deltaT, prec);
}

void Man::SetFace(RString name, RString player)
{
    if (name.GetLength() <= 0)
    {
        Fail("Face");
        name = "Default";
    }
    RString playerKey = player;
    if (stricmp(name, "custom") == 0 && IsNetworkPlayer())
    {
        playerKey = Poseidon::BuildNetworkPlayerStorageKey(GetRemotePlayer());
    }
    _head.SetFace(Type()->_head, IsWoman(), _shape, name, playerKey);
}

void Man::SetGlasses(RString name)
{
    if (name.GetLength() <= 0)
    {
        Fail("Glasses");
        name = "None";
    }
    _head.SetGlasses(Type()->_head, _shape, name);
}

void Man::AddDefaultWeapons()
{
    AddWeapon("Throw");
    AddWeapon("Put");
    AddWeapon("StrokeFist");
}

void Man::MinimalWeapons()
{
    RemoveAllWeapons();
    RemoveAllMagazines();
    AddDefaultWeapons();
    if (GWorld->FocusOn() && GWorld->FocusOn()->GetVehicle() == this)
    {
        GWorld->UI()->ResetVehicle(this);
    }
}

void Man::ScanNVG()
{
    bool found = false;
    for (int i = 0; i < NWeaponSystems(); i++)
    {
        const WeaponType* w = GetWeaponSystem(i);
        if (!w)
        {
            continue;
        }
        if (!strcmpi(w->GetName(), "NVGoggles"))
        {
            found = true;
        }
    }
    _hasNVG = found;
    if (!_hasNVG)
    {
        _nvg = false;
    }
}

void Man::OnWeaponAdded()
{
    base::OnWeaponAdded();
    ScanNVG();

    if (IsHandGunSelected() && FindWeaponType(MaskSlotHandGun) < 0)
    {
        SelectHandGun(false);
    }
    else if (!IsHandGunSelected() && FindWeaponType(MaskSlotHandGun) >= 0 && FindWeaponType(MaskSlotPrimary) < 0)
    {
        SelectHandGun(true);
    }
}

void Man::OnWeaponRemoved()
{
    ScanNVG();

    if (IsHandGunSelected() && FindWeaponType(MaskSlotHandGun) < 0)
    {
        SelectHandGun(false);
    }
    else if (!IsHandGunSelected() && FindWeaponType(MaskSlotHandGun) >= 0 && FindWeaponType(MaskSlotPrimary) < 0)
    {
        SelectHandGun(true);
    }

    base::OnWeaponRemoved();
}

void Man::OnWeaponChanged()
{
    base::OnWeaponChanged();
    ScanNVG();

    if (IsHandGunSelected() && FindWeaponType(MaskSlotHandGun) < 0)
    {
        SelectHandGun(false);
    }
    else if (!IsHandGunSelected() && FindWeaponType(MaskSlotHandGun) >= 0 && FindWeaponType(MaskSlotPrimary) < 0)
    {
        SelectHandGun(true);
    }
}

void Man::OnDanger()
{
    float reaction = 0.3f;
    if (_lookForwardTimeLeft > reaction)
    {
        _lookForwardTimeLeft = reaction;
    }
}

void Man::Init(Matrix4Par pos)
{
    ReactToDammage();
    ResetMovement(0);
    _aimingPositionWorld = CalculateAimingPosition(pos);
    _cameraPositionWorld = CalculateCameraPosition(pos);
    base::Init(pos);
    ScanNVG();
}

bool Man::IsOnLadder(Building* obj, int ladder) const
{
    return _ladderBuilding == obj && ladder == _ladderIndex;
}

void Man::SimLadderMovement(float deltaT, SimulationImportance prec)
{
    float turn, moveX, moveZ;
    SimulateAnimations(turn, moveX, moveZ, deltaT, prec);

    BasicSimulationCore(deltaT, prec);

    // might drop ladder during SimulateAnimations

    if (!_ladderBuilding)
    {
        return;
    }

    // check ladder positions
    Building* obj = _ladderBuilding;
    const BuildingType* type = obj->Type();
    const Ladder& ladder = type->GetLadder(_ladderIndex);
    // get top/bottom positions
    int mem = obj->GetShape()->FindMemoryLevel();
    Vector3 top = obj->AnimatePoint(mem, ladder._top);
    Vector3 bottom = obj->AnimatePoint(mem, ladder._bottom);

    float speed = 0;
    if (IsLocal())
    {
        if (QIsManual())
        {
            // only up/down checked now
            auto& input = InputSubsystem::Instance();
            speed = (input.GetMoveForward() + input.GetMoveFastForward() + input.GetMoveUp() - input.GetMoveDown() -
                     input.GetMoveBack());
            saturate(speed, -1, +1);
        }
        else
        {
            speed = _ladderAIDir;
        }
    }

    float invDist = (top - bottom).InvSize();
    float relChange = speed * deltaT * invDist;
    _ladderPosition += relChange;
    bool topEnd = _ladderPosition >= 1 && relChange > 0;
    bool bottomEnd = _ladderPosition <= 0 && relChange < 0;
    saturate(_ladderPosition, 0, 1);

    MoveId id = MoveId(MoveIdNone);
    ActionMap* map = Type()->GetActionMap(_primaryMove.id);

    if (!EnableTest(&MoveInfo::OnLadder))
    {
        // LOG_DEBUG(Physics, "Off ladder");
        _ladderBuilding = nullptr;
        _ladderIndex = -1;
        MoveId id = map->GetAction(ManActLadderOffTop);
        if (id != MoveIdNone)
        {
            SetMoveQueue(id, false);
        }
        return;
    }
    if (map)
    {
        if (speed < -0.1f)
        {
            id = map->GetAction(ManActDown);
        }
        else if (speed > 0.1f)
        {
            id = map->GetAction(ManActUp);
        }
        else
        {
            id = map->GetAction(ManActStop);
        }
        if (topEnd)
        {
            id = map->GetAction(ManActLadderOffTop);
        }
        if (bottomEnd)
        {
            id = map->GetAction(ManActLadderOffBottom);
        }
    }
    if (id != MoveIdNone)
    {
        SetMoveQueue(id, false);
    }

    Vector3 aPos = (top - bottom) * _ladderPosition + bottom;
    Frame moveTrans = *this;
    moveTrans.SetPosition(aPos);
    Move(moveTrans);

    RecalcGunTransform();
    RecalcPositions(moveTrans);
}

void Man::CatchLadder(Building* obj, int ladderIndex, bool up)
{
    if (obj)
    {
        const BuildingType* type = obj->Type();
        const Ladder& ladder = type->GetLadder(ladderIndex);
        int mem = obj->GetShape()->FindMemoryLevel();
        Vector3 top = obj->AnimatePoint(mem, ladder._top);
        Vector3 bottom = obj->AnimatePoint(mem, ladder._bottom);
        //		Vector3 ladderPos = up ? bottom : top;
        Vector3 ladderDir = top - bottom;
        Vector3 ladderAside = ladderDir.CrossProduct(VUp).Normalized();
        if (ladderAside.SquareSize() < 0.1f)
        {
            ladderAside = Vector3(1, 0, 0);
            RptF("Ladder in %s is vertical", obj->GetShape()->Name()); // degenerate case: ladder is vertical
        }
        Vector3 ladderForward = VUp.CrossProduct(ladderAside).Normalized();
        Matrix4 trans;
        trans.SetPosition(Position());
        trans.SetUpAndDirection(VUp, ladderForward);
        Move(trans);
    }

    // perform actual catch/drop ladder
    _ladderBuilding = obj;
    _ladderIndex = ladderIndex;
    _ladderPosition = up;
    _ladderAIDir = up ? -1 : +1;
}

void Man::DropLadder(Building* obj, int ladder)
{
    // perform actual catch/drop ladder
    _ladderBuilding = nullptr;
    _ladderIndex = -1;
}

void Man::AutoGuide(float deltaT, bool brake, bool avoid) {}

void Man::Sound(bool inside, float deltaT)
{
    if (_doSoundStep)
    {
        const SoundPars* pars = nullptr;
        RStringB surface = _surfaceSound;
        if (_soundStepOverride.GetLength() > 0)
        {
            surface = _soundStepOverride;
        }

        pars = &Type()->GetEnvSoundExtRandom(surface);
        if (!_soundStep || stricmp(pars->name, _soundStep->Name()) != 0)
        {
            if (pars->name.GetLength() > 0)
            {
                _soundStep = GSoundScene->Open(pars->name);
                // LOG_DEBUG(Physics, "Step {}",(const char *)pars->name);
            }
            else
            {
                _soundStep = nullptr;
            }
        }
        if (_soundStep)
        {
            float volume = GRandGen.RandomValue() * 0.4f + 0.8f;
            float frequency = GRandGen.RandomValue() * 0.2f + 0.9f;
            Vector3 land = Position();
            land[1] = GLandscape->RoadSurfaceY(land + VUp * 0.5f);
            _soundStep->SetVolume(pars->vol * volume, pars->freq * frequency);
            _soundStep->SetPosition(land, VZero);
            _soundStep->Repeat(1);
            _soundStep->Restart();
        }
        _doSoundStep = false;
        _soundStepOverride = Foundation::RStringBEmpty;
    }

    Vector3 absSoundPos = Position();
    if (inside)
    {
        static Vector3 relSoundPos(0, 0.2f, 0.3f);
        absSoundPos = CameraPosition();
        absSoundPos += GScene->GetCamera()->DirectionModelToWorld(relSoundPos);
    }

    if (_isDead)
    {
        _soundBreath = nullptr;
    }
    else
    {
        const SoundPars& pars = Type()->GetMainSound();
        if (!_soundBreath)
        {
            _soundBreath = GSoundScene->OpenAndPlay(pars.name, Position(), Speed());
        }
        if (_soundBreath)
        {
            float vol = pars.vol * _tired;
            float freq = pars.freq * (_tired + 1.0f) * 0.5f;
            _soundBreath->SetVolume(vol, freq);
            _soundBreath->SetPosition(absSoundPos, Speed());
        }
    }

    float envSpeed = _speed.Size() * 0.2f;
    if (envSpeed <= 1e-1 || !inside)
    {
        _soundEnv.Free();
    }
    else
    {
        const SoundPars& pars = Type()->_addSound;
        if (!_soundEnv)
        {
            _soundEnv = GSoundScene->OpenAndPlay(pars.name, AimingPosition(), Speed());
        }
        if (_soundEnv)
        {
            float freq = envSpeed;
            saturate(freq, 0.5f, 1.5f);
            _soundEnv->SetVolume(pars.vol * envSpeed, pars.freq * freq);
            _soundEnv->SetPosition(absSoundPos, Speed());
        }
    }
    base::Sound(inside, deltaT);
}

void Man::UnloadSound()
{
    _soundEnv.Free();
    _soundBreath.Free();
    _soundStep.Free();
    base::UnloadSound();
}

void Man::Destroy(EntityAI* owner, float overkill, float minExp, float maxExp)
{
    base::Destroy(owner, overkill, minExp, maxExp);

    if (_brain)
    {
        KilledBy(owner);
    }
}

void Man::Scream(EntityAI* killer)
{
    if (GetHit(Type()->_headHit) <= 0.9f && (_whenScreamed < Glob.time - 3 || _whenScreamed > Glob.time))
    {
        // LOG_DEBUG(Physics, "Hit {:.3f}",GetHit(Type()->_headHit));
        _whenScreamed = Glob.time;
        const SoundPars& pars = Type()->_hitSound.SelectSound(GRandGen.RandomValue());
        Vector3Val pos = Position();
        float rndFreq = GRandGen.RandomValue() * 0.1f + 0.95f;
        float vol = pars.vol;
        bool isOut = IsInLandscape();
        if (!isOut)
        {
            vol *= 0.2f;
        }
        IWave* sound = GSoundScene->OpenAndPlayOnce(pars.name, pos, VZero, vol, pars.freq * rndFreq);
        if (sound)
        {
            GSoundScene->SimulateSpeedOfSound(sound);
            GSoundScene->AddSound(sound);
        }
        // if we scream, somebody may hear it
        if (isOut && killer)
        {
            GLandscape->Disclose(killer, Position(), 80, true, false);
        }
    }
}

void Man::ShowDammage(int part)
{
    AIUnit* brain = Brain();
    bool wasAlive = brain && brain->GetLifeState() == AIUnit::LSAlive;
    if (wasAlive)
    {
        Scream(nullptr);
    }
    base::ShowDammage(part);
}

void Man::HitBy(EntityAI* owner, float howMuch, RString ammo)
{
    base::HitBy(owner, howMuch, ammo);

    ReactToDammage();
}

RString Man::DiagText() const
{
    const char* weapon = "Prep";
    if (!_fire._firePrepareOnly && _fire._fireMode >= 0)
    {
        const WeaponModeType* mode = GetWeaponMode(_fire._fireMode);
        if (mode)
        {
            weapon = mode->GetDisplayName();
        }
    }
    const char* fire = "Go";
    switch (_fireState)
    {
        case FireInit:
            fire = "Init";
            break;
        case FireDone:
            fire = "Run";
            break;
        case FireAim:
            fire = "Aim";
            break;
        case FireAimed:
            fire = "Fire";
            break;
    }
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s %s %.1f, spdw %.1f ", weapon, fire, Glob.time.Diff(_fireStateDelay),
             _walkSpeedWanted * 3.6);
    return RString(buffer) + base::DiagText();
}

void Man::DrawDiags()
{
    base::DrawDiags();
}

} // namespace Poseidon
