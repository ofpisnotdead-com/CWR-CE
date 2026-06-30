#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Core/Application.hpp>

#include <Poseidon/World/Entities/Vehicles/Parachute.hpp>
#include <Poseidon/World/Entities/Weapons/Shots.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>
#include <Poseidon/AI/AI.hpp>

#include <Random/randomGen.hpp>
#include <Poseidon/Graphics/Rendering/Draw/SpecLods.hpp>

#include <Poseidon/Network/Network.hpp>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/BankArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>

#if _ENABLE_CHEATS
#define ARROWS 1
#else
#define ARROWS 0
#endif

namespace Poseidon
{
Parachute::Parachute(VehicleType* name, Person* pilot)
    : base(name, pilot),

      _backRotor(0), _backRotorWanted(0),

      _openState(0),

      _turbulence(VZero), _lastTurbulenceTime(Glob.time)

{
    _head.SetPars("Air");
    _head.Init(Type()->_pilotPos - Vector3(0, 0.2, 0), Type()->_pilotPos, this);
    SetSimulationPrecision(1.0 / 15);
}

Parachute::~Parachute() = default;

ParachuteType::ParachuteType(const ParamEntry* param) : base(param)
{
    _scopeLevel = 1;

    _pilotPos = VZero;
}

void ParachuteType::Load(const ParamEntry& par)
{
    base::Load(par);
}

void ParachuteType::InitShape()
{
    _scopeLevel = 2;
    base::InitShape();

    int level;
    level = _shape->FindLevel(VIEW_PILOT);
    if (level >= 0)
    {
        _shape->LevelOpaque(level)->MakeCockpit();
        _pilotPos = _shape->LevelOpaque(level)->NamedPosition("pilot");
    }

    if (_pilotPos.SquareSize() < 0.1)
    {
        _pilotPos = _shape->MemoryPoint("pilot");
    }

    _skeleton = Skeletons.New("Parachute");
    _weights = new WeightInfo;

    AnimationRTName name;

    name.skeleton = _skeleton;
    name.name = "anim\\opening_para.rtm";

    _open = new AnimationRT(name);
    _open->Prepare(_shape, _skeleton, *_weights, false);
    _open->AddPreloadCount();
    _open->IntroduceStep();

    name.name = "anim\\opened_para_stat.rtm";
    _drop = new AnimationRT(name);
    _drop->Prepare(_shape, _skeleton, *_weights, false);
    _drop->IntroduceStep();
    _drop->AddPreloadCount();
}

void ParachuteType::DeinitShape()
{
    if (_open)
    {
        _open->ReleasePreloadCount();
    }
    if (_drop)
    {
        _drop->ReleasePreloadCount();
    }
    _open.Free();
    _drop.Free();
    base::DeinitShape();
}

static Vector3 BodyFriction(Vector3Val speed, float open)
{
    // something between quadratic and linear
    float openFactor = open * open * 0.75 + open * 0.25;
    Vector3 friction;
    friction.Init();
    float linCoef = openFactor * 0.95 + 0.05;
    float quadCoef = openFactor * 0.95 + 0.05;
    friction[0] = speed[0] * fabs(speed[0]) * 10 * quadCoef + speed[0] * 30 * linCoef;
    friction[1] = speed[1] * fabs(speed[1]) * 30 * quadCoef + speed[1] * 100 * linCoef;
    friction[2] = speed[2] * fabs(speed[2]) * 10 * quadCoef + speed[2] * 30 * linCoef;
    return friction;
}

#define FAST_COEF (1.0 / 25) // use fast/slow simulation mode

void Parachute::GetActions(UIActions& actions, AIUnit* unit, bool now)
{
    // no actions while in parachute
}

bool Parachute::IsAway(float factor)
{
    // parachute is never away
    return false;
}

void Parachute::Simulate(float deltaT, SimulationImportance prec)
{
    _isDead = IsDammageDestroyed();

    Vector3Val speed = ModelSpeed();
    Vector3 force(VZero), friction(VZero);
    Vector3 torque(VZero), torqueFriction(VZero);

    // world space center of mass
    Vector3 wCenter(VFastTransform, ModelToWorld(), GetCenterOfMass());

    Vector3 pForce(VZero);  // partial force
    Vector3 pCenter(VZero); // partial force application point

    float delta;

    if (!_isStopped)
    {
        // change back rotor force
        delta = _backRotorWanted - _backRotor;
        Limit(delta, -2 * deltaT, 2 * deltaT);
        _backRotor += delta;
        Limit(_backRotor, -1, +1);

        // apply aerodynamics of the main rotor

        float backRadius = _shape->BoundingSphere() * 0.95;
        // when moving fast, side bank causes torque
        float sizeFactor = backRadius * (1.0 / 12);
        pForce = Vector3(_backRotor * 12500, 0, 0) * (GetMass() * (1.0 / 3000) * sizeFactor);
        pCenter = Vector3(0, 0, -backRadius);
        torque += pCenter.CrossProduct(pForce);
#if ARROWS
        AddForce(DirectionModelToWorld(pCenter) + wCenter, DirectionModelToWorld(pForce) * InvMass());
#endif

        // convert forces to world coordinates

        DirectionModelToWorld(torque, torque);
        DirectionModelToWorld(force, force);

        // angular velocity causes also some angular friction
        // this should be simulated as torque
        torqueFriction = _angMomentum * 4;

        // calculate new position
        Matrix4 movePos;
        ApplySpeed(movePos, deltaT);
        Frame moveTrans;
        moveTrans.SetTransform(movePos);

        // model space turbulence calculation

        if (Glob.time > _lastTurbulenceTime + 2)
        {
            _lastTurbulenceTime = Glob.time;
            const float maxXT = 3;
            const float maxYT = 1;
            const float maxZT = 3;
            float tx = (GRandGen.RandomValue() - 0.5) * (maxXT * 2);
            float ty = (GRandGen.RandomValue() - 0.5) * (maxYT * 2);
            float tz = (GRandGen.RandomValue() - 0.5) * (maxZT * 2);
            _turbulence = Vector3(tx, ty, tz);
        }

        // body air friction
        float open = _openState - 1;
        saturate(open, 0, 1);

        Vector3 wind = GLandscape->GetWind() + _turbulence;
        Vector3 airSpeed = speed - DirectionWorldToModel(wind);

        friction = BodyFriction(airSpeed, open);
        pCenter = Vector3(0, 5, 0);
        pForce = friction;
        torque -= pCenter.CrossProduct(pForce);

#if ARROWS
        AddForce(DirectionModelToWorld(pCenter) + wCenter,
                 DirectionModelToWorld(pForce) * InvMass() - friction * InvMass(), PackedColor(Color(0.5, 0, 0)));
#endif

        DirectionModelToWorld(friction, friction);

        // gravity - no torque
        pForce = Vector3(0, -1, 0) * (GetMass() * G_CONST);
        force += pForce;
#if ARROWS
        AddForce(wCenter, pForce * InvMass());
#endif

        _objectContact = false;
        _landContact = false;
        _waterContact = false;

        // recalculate COM to reflect change of position
        wCenter.SetFastTransform(moveTrans.ModelToWorld(), GetCenterOfMass());
        if (deltaT > 0)
        {
            Vector3 totForce(VZero);

            // check collision on new position

            float crashSpeed = 0;

            if (prec <= SimulateVisibleFar && IsLocal())
            {
                CollisionBuffer collision;
                GLOB_LAND->ObjectCollision(collision, this, moveTrans);
#define MAX_IN 0.4
#define MAX_IN_FORCE 0.1
#define MAX_IN_FRICTION 0.4

                for (int i = 0; i < collision.Size(); i++)
                {
                    _objectContact = true;
                    // info.pos is relative to object
                    CollisionInfo& info = collision[i];
                    if (info.object)
                    {
                        float cFactor = 1;
                        if (info.object->GetMass() < 50)
                        {
                            continue;
                        }
                        if (info.object->GetType() == Primary)
                        {
                            cFactor = 1;
                        }
                        else
                        {
                            cFactor = info.object->GetMass() * GetInvMass();
                            saturate(cFactor, 0, 5);
                        }
                        if (cFactor > 0.05)
                        {
                            /*
                            LOG_DEBUG(Physics,
                                "%s: heli colision %.3f",
                                (const char *)info.object->GetDebugName(),
                                cFactor
                            );
                            */
                            Vector3Val pos = info.object->PositionModelToWorld(info.pos);
                            Vector3Val dirOut = info.object->DirectionModelToWorld(info.dirOut);
                            // create a force pushing "out" of the collision
                            float forceIn = floatMin(info.under, MAX_IN_FORCE);
                            Vector3 pForce = dirOut * GetMass() * 40 * forceIn;
                            // apply proportional part of force in place of impact
                            pCenter = pos - wCenter;
                            totForce += pForce;
                            torque += pCenter.CrossProduct(pForce);

                            Vector3Val objSpeed = info.object->ObjectSpeed();
                            Vector3 colSpeed = _speed - objSpeed;
                            saturateMax(crashSpeed, colSpeed.Size());

                            // if info.under is bigger than MAX_IN, move out
                            if (info.under > MAX_IN)
                            {
                                Point3 newPos = moveTrans.Position();
                                float moveOut = info.under - MAX_IN;
                                newPos += dirOut * moveOut * 0.1;
                                moveTrans.SetPosition(newPos);
                                // limit speed
                            }

                            // second is "land friction" - causing no momentum

                            float frictionIn = floatMin(info.under, MAX_IN_FRICTION);
                            pForce[0] = fSign(speed[0]) * 20000;
                            pForce[1] = speed[1] * fabs(speed[1]) * 1000 + speed[1] * 8000 + fSign(speed[1]) * 10000;
                            pForce[2] = speed[2] * fabs(speed[2]) * 150 + speed[2] * 250 + fSign(speed[2]) * 2000;

                            pForce = DirectionModelToWorld(pForce) * GetMass() * (4.0 / 10000) * frictionIn;
#if ARROWS
                            AddForce(wCenter + pCenter, -pForce * InvMass());
#endif
                            friction += pForce;
                            torqueFriction += _angMomentum * 0.15;
                        }
                    }
                }
            } // if( object collisions enabled )

            GroundCollisionBuffer gCollision;
            GLOB_LAND->GroundCollision(gCollision, this, moveTrans, 0.05, 0.3, false);

            if (gCollision.Size() > 0)
            {
                Vector3 gFriction(VZero);
                float maxUnder = 0;
#define MAX_UNDER 0.2
#define MAX_UNDER_FORCE 0.1
                for (int i = 0; i < gCollision.Size(); i++)
                {
                    // info.pos is world space
                    UndergroundInfo& info = gCollision[i];
                    if (info.under < 0)
                    {
                        continue;
                    }
                    float under;
                    if (info.type == GroundWater)
                    {
                        under = info.under * 0.001;
                        _waterContact = true;
                    }
                    else
                    {
                        _landContact = true;
                        if (maxUnder < info.under)
                        {
                            maxUnder = info.under;
                        }
                        under = floatMin(info.under, MAX_UNDER_FORCE);
                    }
                    // one is ground "pushing" everything out - causing some momentum
                    Vector3 dirOut = Vector3(0, info.dZ, 1).CrossProduct(Vector3(1, info.dX, 0)).Normalized();
                    pForce = dirOut * GetMass() * 40.0 * under;
                    pCenter = info.pos - wCenter;
                    torque += pCenter.CrossProduct(pForce);
                    // to do: analyze ground reaction force
                    totForce += pForce;

#if ARROWS
                    AddForce(wCenter + pCenter, pForce * under * InvMass());
#endif

                    // second is "land friction" - causing momentum
                    pForce[0] = speed[0] * 5000 + fSign(speed[0]) * 10000;
                    pForce[1] = speed[1] * fabs(speed[1]) * 1000 + speed[1] * 8000 + fSign(speed[1]) * 10000;
                    pForce[2] = speed[2] * fabs(speed[2]) * 150 + speed[2] * 250 + fSign(speed[2]) * 5000;

                    pForce = DirectionModelToWorld(pForce) * GetMass() * (1.0 / 10000);
#if ARROWS
                    AddForce(wCenter + pCenter, -pForce * InvMass());
#endif
                    friction += pForce;

                    // torque applied if speed is big enough
                    if (fabs(speed[0]) < 1)
                    {
                        pForce[0] = 0;
                    }
                    if (fabs(speed[1]) < 1)
                    {
                        pForce[1] = 0;
                    }
                    if (fabs(speed[2]) < 1)
                    {
                        pForce[2] = 0;
                    }
                    torque -= pCenter.CrossProduct(pForce); // sub: it is friction

                    torqueFriction += _angMomentum * info.under * 3;
                }
                saturateMax(crashSpeed, fabs(_speed[1]) + _speed.SizeXZ() * 0.3);
                if (maxUnder > MAX_UNDER)
                {
                    // it is neccessary to move object immediatelly
                    Point3 newPos = moveTrans.Position();
                    float moveUp = maxUnder - MAX_UNDER;
                    newPos[1] += moveUp;
                    moveTrans.SetPosition(newPos);

                    if (_speed.SquareSize() > 1)
                    {
                        _speed.Normalize();
                    }
                }
            }

            if (crashSpeed > 8.5)
            {
                float crash = (crashSpeed - 8.5) * 0.6;
                if (Glob.time > _disableDammageUntil)
                {
                    // crash boom bang state - impact speed too high
                    _doCrash = CrashLand;
                    if (_objectContact)
                    {
                        _doCrash = CrashObject;
                    }
                    if (_waterContact)
                    {
                        _doCrash = CrashWater;
                    }
                    _crashVolume = crash;

                    CrashDammage(crash * 10);

                    LOG_DEBUG(Physics, "para crash {:.3f}, speed {:.1f}", crash, crashSpeed);

                    _disableDammageUntil = Glob.time + 1;
                }
            }

            force += totForce;
        }

        bool stopCondition = false;
        if (_landContact && !_waterContact && !_objectContact)
        {
            // apply static friction
            float maxSpeed = Square(0.7);
            if (!Driver())
            {
                maxSpeed = Square(1.2);
            }
            if (_speed.SquareSize() < maxSpeed && _angVelocity.SquareSize() < maxSpeed * 0.3)
            {
                stopCondition = true;
            }
        }
        if (stopCondition)
        {
            StopDetected();
        }
        else
        {
            IsMoved();
        }

        // apply all forces
        ApplyForces(deltaT, force, torque, friction, torqueFriction);

        // simulate head position
        // calculate how pilot's head is moved is world space between the frames
        // new vehicle position is in moveTrans
        // old is in Transform()
        if (prec <= SimulateCamera)
        {
            _head.Move(deltaT, moveTrans, *this);
        }

        Move(moveTrans);
        DirectionWorldToModel(_modelSpeed, _speed);
    }

    base::Simulate(deltaT, prec);

    if (_landContact || _waterContact)
    {
        if (DriverBrain())
        {
            DriverBrain()->ProcessGetOut(false);
        }
        else
        {
            if (IsLocal())
            {
                SetDelete();
            }
        }
    }
    else
    {
        if (_openState < 1)
        {
            _openState += deltaT * 0.7;
        }
        else
        {
            _openState += deltaT * 0.2;
        }
        // do not close until touched ground
        if (_openState >= 2)
        {
            _openState = 2;
        }
    }
}

const float FlyLevel = 150;

float ParachuteAuto::MakeAirborne()
{
    _landContact = false;
    _objectContact = false;
    _openState = 2;
    _speed = Vector3(0, -7, 0);
    return FlyLevel;
}

bool Parachute::IsPossibleToGetIn() const
{
    return !_landContact;
}

bool Parachute::IsAbleToMove() const
{
    return !_landContact;
}

bool Parachute::Airborne() const
{
    return !_landContact;
}

float Parachute::GetEngineVol(float& freq) const
{
    freq = 1;
    return 0;
}

float Parachute::GetEnvironVol(float& freq) const
{
    freq = 1;
    return _speed.Size() / Type()->GetMaxSpeedMs();
}

void Parachute::Sound(bool inside, float deltaT)
{
    base::Sound(inside, deltaT);
}

void Parachute::UnloadSound()
{
    base::UnloadSound();
}

bool Parachute::HasFlares(CameraType camType) const
{
    return camType != CamInternal && camType != CamGunner;
}

Matrix4 Parachute::InsideCamera(CameraType camType) const
{
    Matrix4 transf;
    if (GetProxyCamera(transf, camType))
    {
        return transf;
    }

    Vector3 pos = _head.Position();
    Matrix4 transform(MTranslation, pos);
    Vector3 up = _head.Position() - _head.Neck();
    up = up + VUp;
    transform.SetUpAndAside(up, VAside);
    return transform;
}

int Parachute::InsideLOD(CameraType camType) const
{
    int level = -1;
    if (level < 0)
    {
        level = GetShape()->FindSpecLevel(VIEW_PILOT);
    }
    if (level < 0)
    {
        level = 0;
    }
    return level;
}

void Parachute::InitVirtual(CameraType camType, float& heading, float& dive, float& fov) const
{
    base::InitVirtual(camType, heading, dive, fov);
}

void Parachute::LimitVirtual(CameraType camType, float& heading, float& dive, float& fov) const
{
    base::LimitVirtual(camType, heading, dive, fov);
    switch (camType)
    {
        case CamInternal:
            saturate(fov, 0.3, 1.2);
            saturate(heading, -1.8, +1.8);
            saturate(dive, -0.7, +0.3);
            break;
    }
}

bool Parachute::IsAnimated(int level) const
{
    return true;
}
bool Parachute::IsAnimatedShadow(int level) const
{
    return true;
}

void Parachute::Draw(int level, ClipFlags clipFlags, const FrameBase& pos)
{
    base::Draw(level, clipFlags, pos);
}

void Parachute::Animate(int level)
{
    // animate with skeletal animation
    if (_openState <= 2)
    {
        float time = _openState - 1;
        saturate(time, 0, 1);
        Type()->_open->Apply(*Type()->_weights, _shape, level, time);
    }
    else
    {
        float time = _openState - 2;
        saturate(time, 0, 1);
        Type()->_drop->Apply(*Type()->_weights, _shape, level, time);
    }

    base::Animate(level);
}

void Parachute::Deanimate(int level)
{
    base::Deanimate(level);
}

ParachuteAuto::ParachuteAuto(VehicleType* name, Person* pilot)
    : Parachute(name, pilot), _dirCompensate(0.5), _lastAngVelocity(VZero),
      _pilotHelper(true), // keyboard helper activated
      _targetOutOfAim(false)
{
}

void ParachuteAuto::Simulate(float deltaT, SimulationImportance prec)
{
    SimulateUnits(deltaT);

    // get simple aproximations of bank and dive
    // we must consider current angular velocity
    float massCoef = GetMass() * (1.0 / 3000);
    saturate(massCoef, 1, 3);
    float dirEstT = massCoef;
    const Matrix3& orientation = Orientation();

    Vector3Val angAcceleration = (_angVelocity - _lastAngVelocity) * (1 / deltaT);
    Vector3Val avgAngVelocity = _angVelocity + angAcceleration * 0.5 * dirEstT;
    Matrix3Val derOrientation = avgAngVelocity.Tilda() * orientation;
    // Matrix3 derOrientation=_angVelocity.Tilda()*orientation;
    Matrix3Val estOrientation = orientation + derOrientation * dirEstT;

    Vector3Val estDirection = estOrientation.Direction().Normalized();

    // float bank=estOrientation.DirectionAside().Normalized().Y();
    // float dive=estDirection.Y();
    if (_pilotHelper && _driver)
    {
        Vector3 direction = Direction() * (1 - _dirCompensate) + estDirection * _dirCompensate;
        float curHeading = atan2(direction[0], direction[2]);
        float changeHeading = AngleDifference(_pilotHeading, curHeading) * 8;
        Limit(changeHeading, -1, 1);
        // when slow, use back rotor
        _backRotorWanted = -changeHeading * 32;
    }

    // no controls available - no engine power
    _backRotorWanted = 0;

    // perform advanced simulation
    MoveWeapons(deltaT);
    base::Simulate(deltaT, prec);

    _lastAngVelocity = _angVelocity; // helper for prediction
}

bool ParachuteAuto::AimWeapon(int weapon, Vector3Par direction)
{
    return true;
}

bool ParachuteAuto::AimWeapon(int weapon, Target* target)
{
    return true;
}

Matrix4 ParachuteAuto::GunTransform() const
{
    return MIdentity;
}

const float MissileConeDown = 0.00;

Vector3 ParachuteAuto::GetWeaponDirection(int weapon) const
{
    return Direction();
}

float ParachuteAuto::GetAimed(int weapon, Target* target) const
{
    return 1;
}

void ParachuteAuto::Eject(AIUnit* unit)
{
    base::Eject(unit);
}

void ParachuteAuto::FakePilot(float deltaT) {}

void ParachuteAuto::JoystickPilot(float deltaT)
{
    auto& input = InputSubsystem::Instance();
    _pilotHelper = false; // keyboard helper deactivated

    _backRotorWanted = -input.GetStickRudder();
}

void ParachuteAuto::SuspendedPilot(AIUnit* unit, float deltaT) {}

void ParachuteAuto::KeyboardPilot(AIUnit* unit, float deltaT)
{
    auto& input = InputSubsystem::Instance();
    _dirCompensate = 0; // low heading compensation

    if (input.IsJoystickActive())
    {
        JoystickPilot(deltaT);
        return;
    }

    _pilotHelper = true; // keyboard helper activated

    Vector3Val direction = Direction();
    _pilotHeading = atan2(direction[0], direction[2]);

    _pilotHeading += 0.5 * (input.GetTurnRight() - input.GetTurnLeft());
}

void ParachuteAuto::AvoidGround(float minHeight) {}

bool ParachuteAuto::FireWeapon(int weapon, TargetType* target)
{
    return false;
}

void ParachuteAuto::FireWeaponEffects(int weapon, const Magazine* magazine, EntityAI* target)
{
    base::FireWeaponEffects(weapon, magazine, target);
}

float ParachuteAuto::GetFieldCost(const GeographyInfo& info) const
{
    return 1;
}

float ParachuteAuto::GetCost(const GeographyInfo& geogr) const
{
    float cost = Type()->GetMinCost(); // basic speed
    int grad = geogr.u.gradient;
    PoseidonAssert(grad <= 7);
    static const float gradPenalty[8] = {1.0, 1.02, 1.05, 1.2, 1.3, 1.5, 1.7, 2.0};
    cost *= gradPenalty[grad];
    return cost;
}

float ParachuteAuto::GetCostTurn(int difDir) const
{ // in sec
    if (difDir == 0)
    {
        return 0;
    }
    float aDir = fabs(difDir);
    float cost = aDir * 0.15 + aDir * aDir * 0.02;
    if (difDir < 0)
    {
        return cost * 0.8;
    }
    return cost;
}

float ParachuteAuto::FireInRange(int weapon, float& timeToAim, const Target& target) const
{
    timeToAim = 0;
    return 1;
}

float ParachuteAuto::FireAngleInRange(int weapon, Vector3Par rel) const
{
    // helicopter cannot fire high, can fire slight low
    if (rel.Y() > 0)
    {
        return 0;
    }
    float size2 = rel.SquareSizeXZ();
    float y2 = Square(rel.Y());
    const float maxY = 0.25;
    if (y2 > size2 * Square(maxY))
    {
        return 0;
    }
    // nearly same level
    float invSize = InvSqrt(size2);
    return 1 - rel.Y() * invSize * (1 / maxY);
}

void ParachuteAuto::AIGunner(AIUnit* unit, float deltaT)
{
    PoseidonAssert(unit);

    if (!GetFireTarget())
    {
        return;
    }

    AimWeapon(_currentWeapon, GetFireTarget());

    if (_currentWeapon < 0)
    {
        return;
    }
    if (_fire._firePrepareOnly)
    {
        return;
    }

    // check if weapon is aimed
    if (GetWeaponLoaded(_currentWeapon) && GetAimed(_currentWeapon, GetFireTarget()) >= 0.7 &&
        GetWeaponReady(_currentWeapon, GetFireTarget()))
    {
        if (!GetAIFireEnabled(GetFireTarget()))
        {
            ReportFireReady();
        }
        else
        {
            FireWeapon(_currentWeapon, GetFireTarget()->idExact);
            _fireState = FireDone;
            _fireStateDelay = Glob.time + 5; // leave some time to recover
        }
    }
}

void ParachuteAuto::MoveWeapons(float deltaT) {}

void ParachuteAuto::AIPilot(AIUnit* unit, float deltaT)
{
    PoseidonAssert(unit);
    PoseidonAssert(unit->GetSubgroup());
    if (!unit->GetSubgroup())
    {
        return;
    }
    bool isLeader = unit->IsSubgroupLeader();

    _dirCompensate = 1;

    Vector3 steerPos = SteerPoint(2.0, 4.0);

    Vector3 steerWant = PositionWorldToModel(steerPos);

    float headChange = atan2(steerWant.X(), steerWant.Z());
    float speedWanted = 0;

    if (!_fire._fireTarget || _fire.GetTargetFinished(unit))
    {
        _fire._fireMode = -1;
        _fire._fireTarget = nullptr;
    }

    bool autopilot = false;
    if (unit->GetState() == AIUnit::Stopping || unit->GetState() == AIUnit::Stopped)
    {
        // special handling of stop state
        // landing position is in _stopPositon
        speedWanted = 0;
        if (_landContact && unit->GetState() == AIUnit::Stopping)
        {
            UpdateStopTimeout();
            unit->SendAnswer(AI::StepCompleted);
            // note: Pilot may get out - Brain may be nullptr
            if (unit->IsFreeSoldier())
            {
                return;
            }
        }
    }
    else if (!isLeader)
    {
    }
    else
    {
    }

    PoseidonAssert(unit);

    if (!autopilot)
    {
        float avoidGround = 0.5;
        float speedSize = fabs(ModelSpeed().Z());
        saturateMax(avoidGround, speedSize * 0.35);

        float maxTurn = H_PI;
        saturate(headChange, -maxTurn, +maxTurn);
        float curHeading = atan2(Direction()[0], Direction()[2]);
        _pilotHeading = curHeading + headChange;
    }
}

void ParachuteAuto::DrawDiags()
{
    LODShapeWithShadow* forceArrow = GScene->ForceArrow();

#if 1
    // draw pilot diags
    {
        Matrix3 rotY(MRotationY, -_pilotHeading);
        Vector3 pilotDir = rotY.Direction();
        Ref<Object> arrow = new Object(forceArrow, -1);
        Point3 pos = Position() + Vector3(0, 5, 0);

        float size = 0.6;
        arrow->SetPosition(pos);
        arrow->SetOrient(pilotDir, VUp);
        arrow->SetPosition(arrow->PositionModelToWorld(forceArrow->BoundingCenter() * size));
        arrow->SetScale(size);
        arrow->SetConstantColor(PackedColor(Color(1, 1, 0)));

        GScene->ObjectForDrawing(arrow);
    }
#endif

    base::DrawDiags();
}

RString ParachuteAuto::DiagText() const
{
    return base::DiagText();
}

LSError Parachute::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar))

#define SERIAL(name) PARAM_CHECK(ar.Serialize(#name, _##name, 1))
#define SERIAL_DEF(name, value) PARAM_CHECK(ar.Serialize(#name, _##name, 1, value))

    if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
    {
        SERIAL_DEF(backRotor, 0);
        SERIAL_DEF(backRotorWanted, 0);
    }

    return LSOK;
}

LSError ParachuteAuto::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar))

    return LSOK;
}

NetworkMessageType ParachuteAuto::GetNMType(NetworkMessageClass cls) const
{
    switch (cls)
    {
        case NMCUpdateGeneric:
            return NMTUpdateParachute;
        default:
            return base::GetNMType(cls);
    }
}

#define UPDATE_PARACHUTE_MSG(XX)                                                                               \
    XX(float, backRotorWanted, NDTFloat, NCTFloatM1ToP1, DEFVALUE(float, 0), DOC_MSG("Obsolete"), IdxTransfer, \
       ET_ABS_DIF, ERR_COEF_VALUE_MAJOR)

DECLARE_NET_INDICES_EX_ERR(UpdateParachute, UpdateTransport, UPDATE_PARACHUTE_MSG)
DEFINE_NET_INDICES_EX_ERR(UpdateParachute, UpdateTransport, UPDATE_PARACHUTE_MSG)

} // namespace Poseidon

DEFINE_GET_INDICES(UpdateParachute)

namespace Poseidon
{

NetworkMessageFormat& ParachuteAuto::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    switch (cls)
    {
        case NMCUpdateGeneric:
            base::CreateFormat(cls, format);
            UPDATE_PARACHUTE_MSG(MSG_FORMAT_ERR)
            break;
        default:
            base::CreateFormat(cls, format);
            break;
    }
    return format;
}

TMError ParachuteAuto::TransferMsg(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
            TMCHECK(base::TransferMsg(ctx))
            {
                PoseidonAssert(dynamic_cast<const IndicesUpdateParachute*>(ctx.GetIndices()))
                    const IndicesUpdateParachute* indices =
                        static_cast<const IndicesUpdateParachute*>(ctx.GetIndices());

                ITRANSF(backRotorWanted)
            }
            break;
        default:
            return base::TransferMsg(ctx);
    }
    return TMOK;
}

float ParachuteAuto::CalculateError(NetworkMessageContext& ctx)
{
    float error = 0;
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
            error += base::CalculateError(ctx);
            {
                PoseidonAssert(dynamic_cast<const IndicesUpdateParachute*>(ctx.GetIndices()))
                    const IndicesUpdateParachute* indices =
                        static_cast<const IndicesUpdateParachute*>(ctx.GetIndices());

                ICALCERR_ABSDIF(float, backRotorWanted, ERR_COEF_VALUE_MAJOR)
            }
            break;
        default:
            error += base::CalculateError(ctx);
            break;
    }
    return error;
}

} // namespace Poseidon
