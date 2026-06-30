#include <Poseidon/Core/Application.hpp>

#include <Poseidon/World/Entities/Vehicles/Misc/Ship.hpp>
#include <Poseidon/AI/AI.hpp>

#include <Poseidon/World/Entities/Weapons/Shots.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Random/randomGen.hpp>
#include <Poseidon/Graphics/Rendering/Draw/SpecLods.hpp>

#include <Poseidon/IO/ParamFile/ParamFile.hpp>

#include <Poseidon/Network/Network.hpp>

#include <Poseidon/Foundation/Enums/EnumNames.hpp>
#include <cmath>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

#pragma warning(disable : 4355)
#pragma warning(disable : 4065)

namespace Poseidon
{
using Foundation::EnumName;

#define ARROWS 0

ShipType::ShipType(const ParamEntry* param) : base(param)
{
    _scopeLevel = 1;

    _pilotPos = VZero;
    _gunnerPilotPos = VZero;
    _commanderPilotPos = VZero;
}

void ShipType::Load(const ParamEntry& par)
{
    base::Load(par);

    _turret.Load(par >> "Turret");
}

void ShipType::InitShape()
{
    _scopeLevel = 2;
    base::InitShape();

    const ParamEntry& par = *_par;

    int level;

    // driving wheel and indicators
    _drivingWheel.Init(_shape, "volant", nullptr, "osaVolantKon");
    _toWheelAxis.SetIdentity();
    _indicatorSpeed.Init(_shape, "ukaz_rychlo", nullptr, "osa_rychlo");

    // radar
    _radar.Init(_shape, "radar", nullptr, "osa radaru");

    // camera positions
    level = _shape->FindLevel(VIEW_PILOT);
    if (level >= 0)
    {
        Shape* cockpit = _shape->LevelOpaque(level);
        cockpit->MakeCockpit();

        _pilotPos = cockpit->NamedPosition("pilot");
        _commanderPilotPos = cockpit->NamedPosition("velitel");

        // driving wheel animation
        Vector3 wheelAxisBeg = cockpit->NamedPosition("osaVolantZac");
        Vector3 wheelAxisEnd = cockpit->NamedPosition("osaVolantKon");
        _drivingWheel.SetCenter(wheelAxisEnd);
        _toWheelAxis.SetDirectionAndUp(wheelAxisEnd - wheelAxisBeg, VUp);

        for_each_alpha
        {
            Shape* shape = _shape->Level(level);
            int index = _indicatorSpeed.GetSelection(level);
            if (index >= 0)
            { // tacho: usually right indicator
                // indicators animation
                const NamedSelection& sel = shape->NamedSel(index);
                if (sel.Faces().Size() > 0)
                {
                    const Poly& face = shape->FaceIndexed(sel.Faces()[0]);
                    _toIndicatorSpeedAxis.SetDirectionAndAside(face.GetNormal(*shape), VAside); // transformation
                    _toIndicatorSpeedAxis.SetPosition(VZero);
                }
            }
        }
    }

    level = _shape->FindLevel(VIEW_CARGO);
    if (level >= 0)
    {
        Shape* cockpit = _shape->LevelOpaque(level);
        cockpit->MakeCockpit();
    }

    level = _shape->FindLevel(VIEW_GUNNER);
    if (level >= 0)
    {
        Shape* cockpit = _shape->LevelOpaque(level);
        cockpit->MakeCockpit();

        _gunnerPilotPos = cockpit->NamedPosition("pilot");
    }

    // turret animations
    _turret.InitShape(par >> "Turret", _shape);

    _gunDir = _turret._dir;
    _gunPos = _turret._pos;

    _animFire.Init(_shape, "zasleh", nullptr);
}

Ship::Ship(VehicleType* name, Person* driver)
    : base(name, driver),

      // pilot controls
      _thrustL(0), _thrustR(0), _thrustLWanted(0), _thrustRWanted(0), _lastAngVelocity(VZero),

      _pilotBrake(false),

      _targetOutOfAim(false), _sink(0),

      _randFrequency(1 - GRandGen.RandomValue() * 0.05) // do not use same sound frequency
{
    _head.SetPars("Land");
    _head.Init(Type()->_pilotPos - Vector3(0, 0.2, 0), Type()->_pilotPos, this);

    _leftWater.SetFades(1.0, 0.5, 2.0);
    _rightWater.SetFades(1.0, 0.5, 2.0);
    _leftEngine.SetSize(0.12);
    _rightEngine.SetSize(0.12);
    _leftEngine.SetTimes(1.5, 1);
    _rightEngine.SetTimes(1.5, 1);

    _mGunClouds.Load((*Type()->_par) >> "MGunClouds");

    _mGunFireFrames = 0;
    _mGunFireTime = UITIME_MIN;
    _mGunFirePhase = 0;
}

void Ship::Sound(bool inside, float deltaT)
{
    _turret.Sound(Type()->_turret, inside, deltaT, *this, Speed());
    base::Sound(inside, deltaT);
}

void Ship::UnloadSound()
{
    base::UnloadSound();
    _turret.UnloadSound();
}

float Ship::GetEngineVol(float& freq) const
{
    float thrust = (fabs(_thrustL) + fabs(_thrustR)) * 0.5;
    freq = _randFrequency * 1.2 * (thrust * 0.7 + 0.5);
    return fabs(thrust) * 0.5 + 0.5;
}

float Ship::GetEnvironVol(float& freq) const
{
    freq = 1;
    return _speed.SquareSize() / Square(Type()->GetMaxSpeedMs());
}

Matrix4 Ship::InsideCamera(CameraType camType) const
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

    if (camType == CamGunner)
    {
        return (GunTurretTransform() * Matrix4(MTranslation, Type()->_gunnerPilotPos));
    }
    else
    {
        Vector3 pos = Type()->_pilotPos;

        Matrix4 transform(MTranslation, pos);
        Vector3 up = _head.Position() - _head.Neck();
        up = up + Vector3(0, 3, 0);
        transform.SetUpAndAside(up, VAside);

        return transform;
    }
}

int Ship::InsideLOD(CameraType camType) const
{
    int level = -1;
    if (camType == CamGunner)
    {
        level = GetShape()->FindLevel(VIEW_GUNNER);
    }
    if (level < 0)
    {
        level = GetShape()->FindLevel(VIEW_PILOT);
    }
    return level;
}

bool Ship::IsVirtual(CameraType camType) const
{
    return true;
}

void Ship::LimitVirtual(CameraType camType, float& heading, float& dive, float& fov) const
{
    base::LimitVirtual(camType, heading, dive, fov);
}

void Ship::InitVirtual(CameraType camType, float& heading, float& dive, float& fov) const
{
    base::InitVirtual(camType, heading, dive, fov);
}

Vector3 Ship::DragFriction(Vector3Par speed)
{
    Vector3 friction;
    friction.Init();
    friction[1] = speed[1] * fabs(speed[1]) * 70 + speed[1] * 1100 + fSign(speed[1]) * 20;
    friction[0] = fSign(speed[0]) * 50;
    friction[2] = fSign(speed[2]) * 5;
    return friction * GetMass() * (1.0 / 60000);
}

Vector3 Ship::DragForce(Vector3Par speed)
{
    Vector3 friction;
    friction.Init();
    friction[0] = -speed[0] * fabs(speed[0]) * 3000 - speed[0] * 30000;
    friction[1] = 0;
    friction[2] = -speed[2] * fabs(speed[2]) * 70 - speed[2] * 50;
    return friction * GetMass() * (1.0 / 60000);
}

static const Color ShipLightColor(0.9, 0.8, 0.8);
static const Color ShipLightAmbient(0.1, 0.1, 0.1);

void Ship::MoveWeapons(float deltaT)
{
    // move turret
    AIUnit* unit = GunnerUnit();
    if (!unit)
    {
        _turret.Stop(Type()->_turret);
    }
    else
    {
        if (GetHit(Type()->_gunHit) > 0.9)
        {
            _turret.GunBroken(Type()->_turret);
        }
        else if (GetHit(Type()->_turretHit) > 0.9)
        {
            _turret.TurretBroken(Type()->_turret);
        }
        else
        {
            _turret._gunStabilized = true;
        }
        _turret.MoveWeapons(Type()->_turret, unit, deltaT);
    }
}

void Ship::Simulate(float deltaT, SimulationImportance prec)
{
    if (_isDead)
    {
        SmokeSourceVehicle* smoke = dyn_cast<SmokeSourceVehicle>(GetSmoke());
        if (smoke)
        {
            smoke->Explode();
        }
        NeverDestroy();
    }

    Vector3Val speed = ModelSpeed();
    float speedSize = fabs(speed.Z());

    float delta;

    MoveWeapons(deltaT);

    if (_isDead || _isUpsideDown)
    {
        _engineOff = true;
    }
    if (_engineOff)
    {
        _pilotBrake = true;
    }
    if (_pilotBrake)
    {
        _thrustLWanted = _thrustRWanted = 0;
    }

    const float baseMass = 8584999;
    float sizeCoef = GetShape()->GeometrySphere() * (1.0 / 80);
    bool smallShip = GetMass() < baseMass * 0.1;

    // simulate left/right engine
    delta = _thrustLWanted - _thrustL;
    Limit(delta, -1 * deltaT, +1 * deltaT);
    _thrustL += delta;
    Limit(_thrustL, -1.0, 1.0);

    delta = _thrustRWanted - _thrustR;
    Limit(delta, -1 * deltaT, +1 * deltaT);
    _thrustR += delta;
    Limit(_thrustR, -1.0, 1.0);

    if (!CheckPredictionFrozen())
    {
        // calculate all forces, frictions and torques
        Vector3 force(VZero), friction(VZero);
        Vector3 torque(VZero), torqueFriction(VZero);

        Vector3 pForce(VZero);  // partial force
        Vector3 pCenter(VZero); // partial force application point

        Vector3 wCenter(VFastTransform, ModelToWorld(), GetCenterOfMass());

        // fuel
        if (!_engineOff)
        {
            ConsumeFuel(deltaT * (0.01 + (fabs(_thrustR) + fabs(_thrustL)) * 0.01));
            if (_fuel <= 0)
            {
                _engineOff = true;
            }
        }

        // apply left/right thrust
        if (!_engineOff)
        {
            if (_landContact || _objectContact || _waterContact)
            {
                float power = Type()->GetMaxSpeed() * (1.0 / 50) * 0.13;
                //  water movement is much less efficient
                if (smallShip)
                {
                    power *= 3;
                }
                float lAccel = _thrustL * power;
                float rAccel = _thrustR * power;
                pForce = Vector3(0, 0, lAccel * GetMass());
                force += pForce;
                float turnSize = (smallShip ? 120 : 120) * sizeCoef;
                pCenter = Vector3(+turnSize, 0, 0); // relative to the center of mass
                torque += pCenter.CrossProduct(pForce);
#if ARROWS
                AddForce(DirectionModelToWorld(pCenter) + wCenter, DirectionModelToWorld(pForce * InvMass()),
                         Color(0, 1, 0, 0.5));
#endif

                pForce = Vector3(0, 0, rAccel * GetMass());
                force += pForce;
                pCenter = Vector3(-turnSize, 0, 0); // relative to the center of mass
                torque += pCenter.CrossProduct(pForce);
#if ARROWS
                AddForce(DirectionModelToWorld(pCenter) + wCenter, DirectionModelToWorld(pForce * InvMass()),
                         Color(0, 1, 0, 0.5));
#endif
            }
        }

        // convert forces to world coordinates
        DirectionModelToWorld(torque, torque);
        DirectionModelToWorld(force, force);

        // apply gravity
        pForce = Vector3(0, -G_CONST * GetMass(), 0);
        force += pForce;

#if ARROWS
        AddForce(wCenter, pForce * InvMass(), Color(0, 0, 1, 0.5));
#endif

        torqueFriction = _angMomentum * 0.5;

        if (_isDead && _waterContact && !_landContact)
        {
            _sink += deltaT * 0.33;
            saturateMin(_sink, 50);
            if (_sink > 3)
            {
                // stop smoking - we are under water
                Vehicle* smoke = GetSmoke();
                if (smoke)
                {
                    smoke->SetDelete();
                }
            }
            if (_sink >= _shape->BoundingSphere() * 2)
            {
                SetDelete();
            }
        }
        else if (!_isDead && _sink > 0)
        {
            _sink -= deltaT * 0.33;
            saturateMin(_sink, 0);
        }

        // simulate draconic force
        // (force which causes any movement energy to be transfered to front-back axis)
        // front-back component of friction

        DirectionModelToWorld(pForce, DragForce(speed));

        force += pForce;
#if ARROWS
        AddForce(wCenter, pForce * InvMass(), Color(1, 1, 0, 0.5));
#endif

        Matrix4 movePos;
        ApplySpeed(movePos, deltaT);
        Frame moveTrans;
        moveTrans.SetTransform(movePos);

        // body air friction
        DirectionModelToWorld(friction, DragFriction(speed));
#if ARROWS
        AddForce(wCenter, friction * InvMass(), Color(1, 0, 0, 0.5));
#endif

        wCenter.SetFastTransform(moveTrans.ModelToWorld(), GetCenterOfMass());

        float soft = 0, dust = 0;
        if (deltaT > 0)
        {
            // check collision on new position
            Vector3 totForce(VZero);

            float crash = 0;
#if 1

            CollisionBuffer collision;
            GLOB_LAND->ObjectCollision(collision, this, moveTrans);
            _objectContact = false;
            if (collision.Size() > 0)
            {
#define MAX_IN 0.2
#define MAX_IN_FORCE 0.1
#define MAX_IN_FRICTION 0.2

                _objectContact = true;
                for (int i = 0; i < collision.Size(); i++)
                {
                    // info.pos is relative to object
                    CollisionInfo& info = collision[i];
                    if (info.object)
                    {
                        if (info.object->GetMass() >= 100)
                        {
                            Point3 pos = info.object->PositionModelToWorld(info.pos);
                            Vector3 dirOut = info.object->DirectionModelToWorld(info.dirOut);
                            // create a force pushing "out" of the collision
                            float forceIn = floatMin(info.under, MAX_IN_FORCE);
                            Vector3 pForce = dirOut * GetMass() * 100 * forceIn;
                            // apply proportional part of force in place of impact
                            pCenter = pos - wCenter;
                            totForce += pForce;
                            torque += pCenter.CrossProduct(pForce) * 0.5;

                            // if info.under is bigger than MAX_IN, move out
                            if (info.under > MAX_IN)
                            {
                                Matrix4 transform = moveTrans.Transform();
                                Point3 newPos = transform.Position();
                                float moveOut = info.under - MAX_IN;
                                Vector3 move = dirOut * moveOut * 0.1;
                                newPos += move;
                                transform.SetPosition(newPos);
                                moveTrans.SetTransform(transform);
                                const float crashLimit = 0.3;
                                if (moveOut > crashLimit)
                                {
                                    crash += moveOut - crashLimit;
                                }

                                Vector3Val objSpeed = info.object->ObjectSpeed();
                                Vector3 colSpeed = _speed - objSpeed;

                                float potentialGain = move[1] * GetMass();
                                float oldKinetic = GetMass() * colSpeed.SquareSize() * 0.5; // E=0.5*m*v^2
                                // kinetic to potential conversion is not 100% effective
                                float crashFactor = (moveOut - crashLimit) * 4 + 1.5;
                                saturateMax(crashFactor, 2.5);
                                float newKinetic = oldKinetic - potentialGain * crashFactor;
                                float newSpeedSize2 = newKinetic * InvMass() * 2;
                                if (newSpeedSize2 <= 0 || oldKinetic <= 0)
                                {
                                    colSpeed = VZero;
                                }
                                else
                                {
                                    colSpeed *= sqrt(newSpeedSize2 * colSpeed.InvSquareSize());
                                }
                                // limit relative speed to object we crashed into
                                const float maxRelSpeed = 2;
                                if (colSpeed.SquareSize() > Square(maxRelSpeed))
                                {
                                    // adapt _speed to match criterion
                                    crash += (colSpeed.Size() - maxRelSpeed) * 0.3;
                                    colSpeed.Normalize();
                                    colSpeed *= maxRelSpeed;
                                }
                                _speed = objSpeed + colSpeed;
                            }

                            // second is "land friction" - causing little momentum
                            float frictionIn = floatMin(info.under, MAX_IN_FRICTION);
                            pForce[0] = fSign(speed[0]) * 10000;
                            pForce[1] = speed[1] * fabs(speed[1]) * 1000 + speed[1] * 8000 + fSign(speed[1]) * 10000;
                            pForce[2] = speed[2] * fabs(speed[2]) * 150 + speed[2] * 250 + fSign(speed[2]) * 2000;

                            pForce = DirectionModelToWorld(pForce) * GetMass() * (4.0 / 10000) * frictionIn;
#if ARROWS
                            AddForce(wCenter + pCenter, -pForce * InvMass(), Color(1, 1, 0, 0.5));
#endif
                            friction += pForce;
                            torqueFriction += _angMomentum * 0.3;
                        }
                    }
                }
            }
#endif

            {
                Vector3 upWanted = VUp;
                /**/
                if (smallShip)
                {
                    upWanted[2] = -speed[2] * 0.008;
                    saturate(upWanted[2], -0.3, 0.1);
                    Matrix3 orient;
                    orient.SetUpAndDirection(VUp, Direction());
                    upWanted = orient * upWanted.Normalized();
                }

                // predict direction up in some time
                float dirEstT = 1.0f;
                // Vector3Val angAcceleration=(_angVelocity-_lastAngVelocity)/deltaT;
                // Vector3Val avgAngVelocity=_angVelocity+angAcceleration*0.5*dirEstT;
                // Matrix3Val derOrientation=avgAngVelocity.Tilda()*orientation;
                const Matrix3& orientation = Orientation();
                Matrix3 derOrientation = _angVelocity.Tilda() * orientation;
                Matrix3Val estOrientation = orientation + derOrientation * dirEstT;

                Vector3Val estDirectionUp = estOrientation.DirectionUp().Normalized();
                // Vector3Val estDirectionUp=moveTrans.DirectionUp();

                Vector3 stabilize = upWanted - estDirectionUp;
                saturate(stabilize[0], -0.3, +0.3);
                saturate(stabilize[2], -0.3, +0.3);
                stabilize[1] = 0;

#if ARROWS
                //
                AddForce(wCenter + Vector3(0, 2, 0), upWanted * 10, Color(0, 0, 0, 0.5));
                AddForce(wCenter + Vector3(0, 2, 0), stabilize * 10, Color(0, 1, 1, 0.5));
#endif

                /**/
                GroundCollisionBuffer gCollision;
                float above = -_sink;
                GLandscape->GroundCollision(gCollision, this, moveTrans, above, 1);
                _landContact = false;
                _waterContact = false;
                if (gCollision.Size() > 0)
                {
#define MAX_UNDER 4.0
#define MAX_UNDER_FORCE 2.0

                    Vector3 gFriction(VZero);
                    float maxUnder = 0;
                    for (int i = 0; i < gCollision.Size(); i++)
                    {
                        // info.pos is world space
                        UndergroundInfo& info = gCollision[i];
                        // we consider two forces
                        if (info.under < 0)
                        {
                            continue;
                        }
                        if (info.type == GroundWater)
                        {
                            _waterContact = true;
                            // simulate swimming force
                            const float coefNPoints = 12.0 / 12.0;
                            // first is water is "pushing" everything up - causing some momentum
                            pForce = Vector3(0, (1200000 / baseMass) * GetMass() * info.under * coefNPoints, 0);
                            if (smallShip)
                            {
                                pForce *= 20 + floatMin(floatMax(speed[2], 0) * 6, 100);
                            }
                            pCenter = info.pos - wCenter;
                            torque += pCenter.CrossProduct(pForce * 0.2);
                            totForce += pForce;

#if ARROWS
                            AddForce(wCenter + pCenter, pForce * InvMass(), Color(1, 0, 1, 0.5));
#endif

                            // second is "water friction" - causing no momentum
                            pForce[0] = speed[0] * fabs(speed[0]) * 0.50 * info.under + speed[0] * info.under * 4;
                            pForce[1] = speed[1] * fabs(speed[1]) * 40 + speed[1] * 30;
                            pForce[2] = (fabs(speed[2]) * 0.02 + 0.06) * speed[2] * info.under;

                            if (_pilotBrake)
                            {
                                pForce *= 8;
                            }
                            if (smallShip)
                            {
                                pForce[0] *= 8;
                                pForce *= 200;
                            }

                            pForce = DirectionModelToWorld(pForce * info.under) * GetMass() * (1.0 / 7000);
#if ARROWS
                            AddForce(wCenter + pCenter, -pForce * InvMass(), Color(1, 0, 1, 0.5));
#endif
                            friction += pForce * coefNPoints;
                            if (smallShip)
                            {
                                Vector3 tf = _angMomentum * 0.7 * info.under;
                                tf[1] *= 0.05;
                                torqueFriction += tf;
                            }
                            else
                            {
                                torqueFriction += _angMomentum * 0.001 * info.under;
                            }
                        }
                        else
                        {
                            _landContact = true;
                            if (maxUnder < info.under)
                            {
                                maxUnder = info.under;
                            }
                            float under = floatMin(info.under, MAX_UNDER_FORCE);

                            // const float coefNPoints=12.0/4.0;
                            const float coefNPoints = 12.0 / 12.0;
                            // one is ground "pushing" everything out - causing some momentum
                            Vector3 dirOut = Vector3(0, info.dZ, 1).CrossProduct(Vector3(1, info.dX, 0)).Normalized();
                            pForce = dirOut * GetMass() * 40.0 * under * coefNPoints;
                            pCenter = info.pos - wCenter;
                            torque += pCenter.CrossProduct(pForce);
                            // to do: analyze ground reaction force
                            totForce += pForce;

#if ARROWS
                            AddForce(wCenter + pCenter, pForce * under * InvMass(), Color(1, 0, 1, 0.5));
#endif

                            // second is "land friction" - causing no momentum
                            pForce[0] = speed[0] * 10000 + fSign(speed[0]) * 15000;
                            pForce[1] = speed[1] * 8000 + fSign(speed[1]) * 5000;
                            pForce[2] = speed[2] * 500 + fSign(speed[2]) * 5000;
                            if (smallShip)
                            {
                                pForce *= 2;
                            }

                            pForce = DirectionModelToWorld(pForce) * GetMass() * (1.0 / 40000);
#if ARROWS
                            AddForce(wCenter + pCenter, -pForce * InvMass(), Color(1, 0, 1, 0.5));
#endif
                            friction += pForce * coefNPoints;
                            torqueFriction += _angMomentum * 4 * info.under;

                            // some friction is caused by moving the land aside
                            // this applies only to soft surfaces
                            if (info.texture)
                            {
                                soft = info.texture->Roughness() * 0.7;
                                dust = info.texture->Dustness() * 0.7;
                                saturateMin(soft, 1);
                                saturateMin(dust, 1);
                            }
                            float landMoved = info.under;
                            saturateMin(landMoved, 0.1);
                            pForce[0] = speed[0] * 6500 * landMoved * soft;
                            pForce[1] = 0;
                            pForce[2] = speed[2] * 1500 * landMoved * soft;
                            pForce = DirectionModelToWorld(pForce) * GetMass() * (1.0 / 1000);
#if ARROWS
                            AddForce(wCenter + pCenter, -pForce * InvMass(), Color(1, 0, 1, 0.5));
#endif
                            friction += pForce;
                        }
                    }
                    if (_waterContact && _sink < 2)
                    {
                        const SurfaceInfo& info = GLandscape->GetWaterSurface();
                        soft = info._roughness * 2.5;
                        dust = info._dustness * 2.5;
                        saturateMin(soft, 1);
                        saturateMin(dust, 1);
                    }
                    if (maxUnder > MAX_UNDER)
                    {
                        // it is neccessary to move object immediatelly
                        Matrix4 transform = moveTrans.Transform();
                        Point3 newPos = transform.Position();
                        float moveUp = maxUnder - MAX_UNDER;
                        newPos[1] += moveUp;
                        transform.SetPosition(newPos);
                        moveTrans.SetTransform(transform);
                        // we move up - we have to maintain total energy
                        // what potential energy will gain, kinetic must loose
                        const float crashLimit = 0.3;
                        if (moveUp > crashLimit)
                        {
                            crash += moveUp - crashLimit;
                        }
                        float potentialGain = moveUp * GetMass();
                        float oldKinetic = GetMass() * _speed.SquareSize() * 0.5; // E=0.5*m*v^2
                        // kinetic to potential conversion is not 100% effective
                        float crashFactor = (moveUp - crashLimit) * 4 + 1.5;
                        saturateMax(crashFactor, 2.5);
                        float newKinetic = oldKinetic - potentialGain * crashFactor;
                        float newSpeedSize2 = newKinetic * InvMass() * 2;
                        if (newSpeedSize2 <= 0 || oldKinetic <= 0)
                        {
                            _speed = VZero;
                        }
                        else
                        {
                            _speed *= sqrt(newSpeedSize2 * _speed.InvSquareSize());
                        }
                    }
                }
                if (_waterContact)
                {
                    torque += Vector3(0, 800 * sizeCoef * GetMass(), 0).CrossProduct(stabilize);
                }
            }

            force += totForce;
            float crashTreshold = 30 * GetMass(); // 3G
            float forceCrash = 0;
            if (totForce.SquareSize() > Square(crashTreshold))
            {
                forceCrash = (totForce.Size() - crashTreshold) * InvMass() * (1.0 / 100);
            }
            crash += forceCrash;
            if (crash > 0.1)
            {
                // crash boom bang state - impact speed too high
                _doCrash = CrashWater;
                if (_objectContact)
                {
                    _doCrash = CrashObject;
                }
                else if (_landContact)
                {
                    _doCrash = CrashLand;
                }
                else
                {
                    crash *= 0.1;
                }
                _crashVolume = crash * 0.5;
            }
        }

        _lastAngVelocity = _angVelocity;

        // apply all forces
        ApplyForces(deltaT, force, torque, friction, torqueFriction);

        if (_pilotBrake)
        {
            if ((_landContact || _waterContact) && !_objectContact)
            {
                // apply static friction
                if (_speed.SquareSizeXZ() < 0.2)
                {
                    _speed[0] = _speed[2] = 0;
                }
            }
        }

        // simulate track drawing
        if (EnableVisualEffects(prec) && DirectionUp().Y() >= 0.3)
        {
            if (_waterContact)
            {
                float offset = speed[2];
                saturate(offset, -2, +2);
                Vector3 lPos = PositionModelToWorld(Vector3(+1.4, 0, offset) - _shape->BoundingCenter());
                Vector3 rPos = PositionModelToWorld(Vector3(-1.4, 0, offset) - _shape->BoundingCenter());
                Vector3 lEngPos = PositionModelToWorld(Vector3(+0.7, 0, -4.7) - _shape->BoundingCenter());
                Vector3 rEngPos = PositionModelToWorld(Vector3(-0.7, 0, -4.7) - _shape->BoundingCenter());
                float sea = GLOB_LAND->GetSeaLevel() - 0.1; // sea level
                lPos[1] = rPos[1] = sea;
                lEngPos[1] = rEngPos[1] = sea;
                float dens = floatMin(speedSize * 0.1, 0.7);
                float size = floatMin(speedSize * 0.1, 0.7);
                float densEngL = fabs(_thrustL);
                float densEngR = fabs(_thrustR);
                float side = floatMax(0.1, speed[2] * 0.2);
                Vector3 sideV = DirectionAside() * side;
                Vector3 spdL = Speed() * 0.4 + sideV;
                Vector3 spdR = Speed() * 0.4 - sideV;
                Vector3 spdEngL = Direction() * (-3.0 * _thrustL) + Speed() * 0.2;
                Vector3 spdEngR = Direction() * (-3.0 * _thrustR) + Speed() * 0.2;
                if (dens > 0.1)
                {
                    _leftWater.SetSize(size, 0.1);
                    _rightWater.SetSize(size, 0.1);
                    _leftWater.Simulate(lPos, spdL, dens, deltaT);
                    _rightWater.Simulate(rPos, spdR, dens, deltaT);
                }
                if (densEngL > 0.1)
                {
                    _leftEngine.Simulate(lEngPos, spdEngL, densEngL, deltaT);
                }
                if (densEngR > 0.1)
                {
                    _rightEngine.Simulate(rEngPos, spdEngR, densEngR, deltaT);
                }
            }

            if (_mGunClouds.Active() || _mGunFire.Active())
            {
                Matrix4Val gunTransform = GunTurretTransform();
                Matrix4Val toWorld = Transform() * gunTransform;
                Vector3Val dir = toWorld.Direction();
                Vector3 gunPos(VFastTransform, toWorld, Type()->_gunPos);
                _mGunClouds.Simulate(gunPos, Speed() * 0.7 + dir * 5.0, 0.35, deltaT);
                _mGunFire.Simulate(gunPos, deltaT);
                CancelStop();
            }

            // simulate pilot's head movement
            if (prec <= SimulateCamera)
            {
                _head.Move(deltaT, moveTrans, *this);
            }
        }

        _turret.Stabilize(this, Type()->_turret, Transform().Orientation(), moveTrans.Orientation());

        Move(moveTrans);
        DirectionWorldToModel(_modelSpeed, _speed);
    } // if (!frozen)

    base::Simulate(deltaT, prec);
}

Matrix4 Ship::TurretTransform() const
{
    int memory = GetShape()->FindMemoryLevel();
    int sel = Type()->_turret._body.GetSelection(memory);
    if (sel >= 0)
    {
        Matrix4 mat = MIdentity;
        AnimateMatrix(mat, memory, sel);
        return mat;
    }
    return MIdentity;
}

Matrix4 Ship::GunTurretTransform() const
{
    int memory = GetShape()->FindMemoryLevel();
    int sel = Type()->_turret._gun.GetSelection(memory);
    if (sel >= 0)
    {
        Matrix4 mat = MIdentity;
        AnimateMatrix(mat, memory, sel);
        return mat;
    }
    return MIdentity;
}

void Ship::Draw(int level, ClipFlags clipFlags, const FrameBase& pos)
{
    base::Draw(level, clipFlags, pos);
}

bool Ship::IsAnimated(int level) const
{
    return true;
}
bool Ship::IsAnimatedShadow(int level) const
{
    // a stopped (unsimulated) vehicle cannot change pose
    return !ShadowPoseFrozen();
}

void Ship::AnimateMatrix(Matrix4& mat, int level, int selection) const
{
    _turret.AnimateMatrix(Type()->_turret, mat, this, level, selection);
}

Vector3 Ship::AnimatePoint(int level, int index) const
{
    Shape* shape = _shape->Level(level);
    if (!shape)
    {
        return VZero;
    }
    shape->SaveOriginalPos();

    Vector3 pos = shape->OrigPos(index);

    _turret.AnimatePoint(Type()->_turret, pos, this, level, index);

    return pos;
}

void Ship::Animate(int level)
{
    if (!_shape->Level(level))
    {
        return;
    }

    _turret.Animate(Type()->_turret, this, level);

    Shape* geom = _shape->GeometryLevel();
    geom->CalculateMinMax();

    // driving wheel
    if (Type()->_drivingWheel.GetSelection(level) >= 0)
    {
        float turn = _thrustR - _thrustL;
        Matrix4 turnTrans(MRotationZ, turn * 2);
        Type()->_drivingWheel.Apply(_shape, Type()->_toWheelAxis * turnTrans * Type()->_toWheelAxis.InverseRotation(),
                                    level);
    }

    if (Type()->_indicatorSpeed.GetSelection(level) >= 0)
    {
        Vector3Val speed = ModelSpeed();
        const float maxSpeed = 100 / 3.6;
        float relSpeedSize = fabs(speed[2]) * (1.0 / maxSpeed);
        float angle;
        const float spdFullAngle = HDegree(97);
        const float spdMaxAngle = HDegree(95);
        angle = relSpeedSize * (2 * spdFullAngle) - spdFullAngle;
        saturate(angle, -spdMaxAngle, +spdMaxAngle);
        Matrix4Val fromIndicatorSpeedAxis = Type()->_toIndicatorSpeedAxis.InverseRotation();
        Type()->_indicatorSpeed.Apply(
            _shape, Type()->_toIndicatorSpeedAxis * Matrix4(MRotationZ, angle) * fromIndicatorSpeedAxis, level);
    }

    Type()->_radar.Rotate(_shape, 0.5 * Glob.time.toFloat(), level);

    if (_mGunFireFrames > 0 || Glob.uiTime < _mGunFireTime + 0.05)
    {
        Type()->_animFire.Unhide(_shape, level);
        Type()->_animFire.SetPhase(_shape, level, _mGunFirePhase);
        _mGunFireFrames--;
    }
    else
    {
        Type()->_animFire.Hide(_shape, level);
    }

    base::Animate(level);
}
void Ship::Deanimate(int level)
{
    if (!_shape->Level(level))
    {
        return;
    }

    base::Deanimate(level);

    _turret.Deanimate(Type()->_turret, _shape, level);

    if (Type()->_drivingWheel.GetSelection(level) >= 0)
    {
        Type()->_drivingWheel.Restore(_shape, level);
    }

    if (Type()->_indicatorSpeed.GetSelection(level) >= 0)
    {
        Type()->_indicatorSpeed.Restore(_shape, level);
    }

    Type()->_radar.Restore(_shape, level);
}

bool Ship::AnimateTexture(int level, float phaseL, float phaseR, float speedL, float speedR)
{
    return false; // no alpha change
}

void Ship::Eject(AIUnit* unit)
{
    base::Eject(unit);
}

Vector3 Ship::GetDriverGetInPos(Person* person, Vector3Par upos) const
{
    Vector3 pos = base::GetDriverGetInPos(person, upos);
    AIUnit::FindNearestEmpty(pos, true, person);
    pos[1] = GLandscape->RoadSurfaceYAboveWater(pos[0], pos[2]);
    return pos;
}

Vector3 Ship::GetCommanderGetInPos(Person* person, Vector3Par upos) const
{
    Vector3 pos = base::GetCommanderGetInPos(person, upos);
    AIUnit::FindNearestEmpty(pos, true, person);
    pos[1] = GLandscape->RoadSurfaceYAboveWater(pos[0], pos[2]);
    return pos;
}

Vector3 Ship::GetGunnerGetInPos(Person* person, Vector3Par upos) const
{
    Vector3 pos = base::GetGunnerGetInPos(person, upos);
    AIUnit::FindNearestEmpty(pos, true, person);
    pos[1] = GLandscape->RoadSurfaceYAboveWater(pos[0], pos[2]);
    return pos;
}

Vector3 Ship::GetCargoGetInPos(Person* person, Vector3Par upos) const
{
    Vector3 pos = base::GetCargoGetInPos(person, upos);
    AIUnit::FindNearestEmpty(pos, true, person);
    pos[1] = GLandscape->RoadSurfaceYAboveWater(pos[0], pos[2]);
    return pos;
}

Vector3 Ship::GetCargoGetOutPos(Person* person) const
{
    Vector3 unitPos = person->WorldPosition();
    return GetCargoGetInPos(person, unitPos);
}

void Ship::FakePilot(float deltaT) {}

void Ship::SuspendedPilot(AIUnit* unit, float deltaT) {}

void Ship::KeyboardPilot(AIUnit* unit, float deltaT)
{
    CancelStop();

    auto& input = InputSubsystem::Instance();
    constexpr InputContext ctx = InputContext::ShipDriver;
    float forward = (input.GetAction(ctx, UAMoveForward) - input.GetAction(ctx, UAMoveBack)) * 0.75f;
    forward += input.GetAction(ctx, UAMoveFastForward);
    forward += input.GetAction(ctx, UAMoveSlowForward) * 0.33f;
    _thrustRWanted = _thrustLWanted = forward;

    bool internalCamera = IsGunner(GWorld->GetCameraType());
    bool mouseControl = internalCamera && input.IsMouseTurnActive() && !input.IsLookAroundEnabled();

    float turnWanted;
    if (mouseControl)
    {
        const float estT = 2;
        // estimate heading
        Matrix3Val orientation = Orientation();
        Matrix3Val derOrientation = _angVelocity.Tilda() * orientation;
        Matrix3Val estOrientation = orientation + derOrientation * estT;
        Vector3Val estDirection = estOrientation.Direction();

        float curHeading = atan2(Direction()[0], Direction()[2]);
        float estHeading = atan2(estDirection[0], estDirection[2]);

        Vector3 relDir(VMultiply, DirWorldToModel(), _mouseDirWanted);
        float mTurnWanted = atan2(relDir.X(), relDir.Z());

        turnWanted = AngleDifference(curHeading + mTurnWanted, estHeading);
    }
    else
    {
        turnWanted = input.GetAction(ctx, UATurnRight) - input.GetAction(ctx, UATurnLeft);
    }

    _thrustLWanted -= turnWanted;
    _thrustRWanted += turnWanted;
    Limit(_thrustLWanted, -0.5, 1);
    Limit(_thrustRWanted, -0.5, 1);

    if (fabs(_thrustLWanted) + fabs(_thrustRWanted) < 0.1 && fabs(_modelSpeed.Z()) < 4.0)
    {
        _pilotBrake = true;
    }
    else
    {
        _pilotBrake = false;
        CancelStop();
    }

    if (_engineOff)
    {
        EngineOn();
    }
}

void ShipWithAI::FindStopPosition()
{
    // found place near the cost instead of place with shallow water
    float seaLevel = GLandscape->GetSeaLevel();

    int x = toInt(Position().X() * InvTerrainGrid);
    int z = toInt(Position().Z() * InvTerrainGrid);
    float y = GLandscape->GetHeight(z, x);
    //	if (y >= yMin && y < yMax)
    if (y >= seaLevel)
    {
        _stopPosition = Position();
        return;
    }

    int maxRange = (int)floatMax(floatMax(x, TerrainRange - 1 - x), floatMax(z, TerrainRange - 1 - z));
    // limit search to near vincity
    if (maxRange * TerrainGrid > 500)
    {
        maxRange = (int)(500 / TerrainGrid);
        LOG_DEBUG(Physics, "Limit landing spot search range {}", maxRange);
    }

    for (int range = 1; range <= maxRange; range++)
    {
        for (int i = -range; i < range; i++)
        {
            int xx = x + i;
            int zz = z - range;
            float y = GLandscape->GetHeight(zz, xx);
            if (y >= seaLevel)
            {
                // last field in water
                zz++;
                if (i == -range)
                {
                    xx++;
                }

                _stopPosition[0] = xx * TerrainGrid;
                _stopPosition[2] = zz * TerrainGrid;
                _stopPosition[1] = Position().Y();
                return;
            }
            xx = x + range;
            zz = z + i;
            y = GLandscape->GetHeight(zz, xx);
            if (y >= seaLevel)
            {
                // last field in water
                xx--;
                if (i == -range)
                {
                    zz++;
                }

                _stopPosition[0] = xx * TerrainGrid;
                _stopPosition[2] = zz * TerrainGrid;
                _stopPosition[1] = Position().Y();
                return;
            }
            xx = x - i;
            zz = z + range;
            y = GLandscape->GetHeight(zz, xx);
            if (y >= seaLevel)
            {
                // last field in water
                zz--;
                if (i == -range)
                {
                    xx--;
                }

                _stopPosition[0] = xx * TerrainGrid;
                _stopPosition[2] = zz * TerrainGrid;
                _stopPosition[1] = Position().Y();
                return;
            }
            xx = x - range;
            zz = z - i;
            y = GLandscape->GetHeight(zz, xx);
            if (y >= seaLevel)
            {
                // last field in water
                xx++;
                if (i == -range)
                {
                    zz--;
                }

                _stopPosition[0] = xx * TerrainGrid;
                _stopPosition[2] = zz * TerrainGrid;
                _stopPosition[1] = Position().Y();
                return;
            }
        }
    }

    Fail("No stop position");
}

const float DriverReactionTime = 1.0;

void ShipWithAI::Autopilot(AIUnit* unit, float& speedWanted, float& headChange, float& turnPredict)
{
    switch (_stopState)
    {
        case SSNone:
        {
            Vector3 from = Position() + Speed() * DriverReactionTime;
            FindStopPosition();
            unit->SetWantedPosition(_stopPosition, AIUnit::VehiclePlanned, true);
            _stopState = SSFindPath;
            break;
        }
        case SSFindPath:
            // strategic planning
            {
                unit->CreateStrategicPath(AI::LevelOperative);
                const IAIPathPlanner& planner = unit->GetPlanner();
                if (!planner.IsSearching())
                {
                    unit->CopyPath(planner);
                    _stopState = SSMove;
                }
            }
            break;
        case SSMove:
        {
            // check path position
            const Path& path = unit->GetPath();
            if (path.Size() >= 2)
            {
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

                steerWant = PositionWorldToModel(steerPos);
                headChange = atan2(steerWant.X(), steerWant.Z()) + hcOffset * spdFactor;

                Vector3Val steerPredictRel = PositionWorldToModel(steerPredict);
                turnPredict = atan2(steerPredictRel.X(), steerPredictRel.Z());

                if (_moveMode == VMMBackward)
                {
                    headChange = atan2(-steerWant.X(), -steerWant.Z());
                }
                else
                {
                    headChange = atan2(steerWant.X(), steerWant.Z()) + hcOffset * spdFactor;
                }

                EngineOn();
                //_moveMode=gotoNormal;
                float cost = path.CostAtPos(Position());
                speedWanted = path.SpeedAtCost(cost);

                Vector3Val pos = path.PosAtCost(cost, Position());

                float distPath2 = (Position() - pos).SquareSizeXZ();
                float distEnd2 = (Position() - path.End()).SquareSizeXZ();

                float precision = GetPrecision();
                float tholdDist2 = Square(floatMax(precision * 0.9, 1));
                if (Position().Distance2(pos) > tholdDist2)
                {
                    saturateMax(speedWanted, GetType()->GetMaxSpeedMs() * 0.25);
                }

                if (distEnd2 < Square(precision))
                {
                    // HERE IS DIFFERENCE FROM FORMATION PILOT
                    _stopState = SSStop;
                }
                else if (distPath2 > Square(GetPrecision() * 2))
                {
                    // HERE IS DIFFERENCE FROM FORMATION PILOT
                    _stopState = SSNone;
                    return;
                }
                else if (path.GetMaxIndex() < path.Size())
                {
                    int lastValidIndex = path.GetMaxIndex() - 1;
                    PoseidonAssert(lastValidIndex >= 1);
                    // check if we are in valid region
                    if (cost > path[lastValidIndex]._cost)
                    {
                        // HERE IS DIFFERENCE FROM FORMATION PILOT
                        _stopState = SSNone;
                        return;
                    }
                }
            }
            else
            {
                float finalDist2 = (_stopPosition - Position()).SquareSizeXZ();
                float prec = GetPrecision();
                if (finalDist2 < Square(prec))
                {
                    _stopState = SSStop;
                }
                // path maybe was not planned yet
                else if (path.GetSearchTime() < Glob.time - 5)
                {
                    // HERE IS DIFFERENCE FROM FORMATION PILOT
                    _stopState = SSNone;
                    return;
                }
                speedWanted = 0;
                headChange = 0;
            }

            // strategic target known
            float finalDist2 = (_stopPosition - Position()).SquareSizeXZ();
            float prec = GetPrecision();
            float maxSpeed = GetType()->GetMaxSpeedMs();
            if (finalDist2 < Square(prec * 3))
            {
                if (finalDist2 < Square(prec * 1))
                {
                    speedWanted = 0;
                }
                else
                {
                    float minSpd = maxSpeed * 0.25;
                    saturateMax(minSpd, 1);
                    saturate(speedWanted, -minSpd, +minSpd);
                }
            }

            AIUnit* commander = CommanderUnit();
            if (commander && commander->IsSubgroupLeader())
            {
                AIGroup* group = commander->GetGroup();
                if (!group->GetFlee())
                {
                    saturateMin(speedWanted, _limitSpeed); // move max. by given speed
                }
            }

            if (Glob.time < _avoidSpeedTime)
            {
                saturate(speedWanted, -_avoidSpeed, +_avoidSpeed);
            }
        }
        break;
        case SSStop:
        {
            Vector3Val speed = ModelSpeed();
            if (fabs(speed[2]) < 1)
            {
                UpdateStopTimeout();
                unit->SendAnswer(AI::StepCompleted);
                _stopState = SSNone;
            }
            speedWanted = 0;
            headChange = 0;
        }
        break;
    }
}

void ShipWithAI::AIPilot(AIUnit* unit, float deltaT)
{
    PoseidonAssert(unit);
    PoseidonAssert(unit->GetSubgroup());
    AIUnit* leader = unit->GetSubgroup()->Leader();
    bool isLeaderVehicle = leader && leader->GetVehicleIn() == this;

    Vector3Val speed = ModelSpeed();

    float headChange = 0;
    float speedWanted = 0;
    float turnPredict = 0;

    if (unit->GetState() == AIUnit::Stopping)
    {
        // special handling of stop state
        Autopilot(unit, speedWanted, headChange, turnPredict);
    }
    else
    {
        _stopState = SSNone;
        if (unit->GetState() == AIUnit::Stopped)
        {
            // special handling of stop state
            speedWanted = 0;
            headChange = 0;
        }
        else if (!isLeaderVehicle)
        {
            FormationPilot(speedWanted, headChange, turnPredict);
        }
        else
        { // subgroup leader -
            // if we are near the target we have to operate more precisely
            LeaderPilot(speedWanted, headChange, turnPredict);
        }
    }

#if DIAG_SPEED
    if (this == GWorld->CameraOn())
    {
        LOG_DEBUG(Physics, "Pilot {:.1f}", speedWanted * 3.6);
    }
#endif

    AvoidCollision(deltaT, speedWanted, headChange);

#if DIAG_SPEED
    if (this == GWorld->CameraOn())
    {
        LOG_DEBUG(Physics, "AvoidCollision {:.1f}", speedWanted * 3.6);
    }
#endif

    float curHeading = atan2(Direction()[0], Direction()[2]);
    float wantedHeading = curHeading + headChange;

    float estDirT = 2.0;
    const Matrix3& orientation = Orientation();

    Vector3Val angAcceleration = (_angVelocity - _lastAngVelocity) * (1 / deltaT);
    Vector3Val avgAngVelocity = _angVelocity + angAcceleration * 0.5 * estDirT;
    Matrix3Val derOrientation = avgAngVelocity.Tilda() * orientation;
    Matrix3Val estOrientation = orientation + derOrientation * estDirT;

    Vector3Val estDirection = estOrientation.Direction().Normalized();

    float estHeading = atan2(estDirection[0], estDirection[2]);

    headChange = AngleDifference(wantedHeading, estHeading);

    {
        float maxSpeed = GetType()->GetMaxSpeedMs();
        float limitSpeed = Interpolativ(fabs(turnPredict), H_PI / 16, H_PI / 4, maxSpeed, 3);
        float limitSpeedC = Interpolativ(fabs(headChange), H_PI / 16, H_PI / 4, maxSpeed, 0);
#if DIAG_SPEED
        if (this == GWorld->CameraOn())
        {
            LOG_DEBUG(Physics, "Turn limit {:.1f} ({:.3f}, turn {:.3f})", limitSpeed, headChange, turnPredict);
        }
#endif

        saturate(speedWanted, -limitSpeed, +limitSpeed);
        saturate(speedWanted, -limitSpeedC, +limitSpeedC);
    }

    if (fabs(speedWanted) > 0.5)
    {
        EngineOn();
    }

    Vector3 relAccel = DirectionWorldToModel(_acceleration);
    float changeAccel = (speedWanted - speed.Z()) * (1 / 0.5) - relAccel.Z();
    // some thrust is needed to keep speed
    float isSlow = 1 - fabs(speed.Z()) * (1.0 / 17);
    saturate(isSlow, 0.2, 1);

    changeAccel *= isSlow;
    float thrustOld = (_thrustL + _thrustR) * 0.5f;
    float thrust = thrustOld + changeAccel * 0.33;
    Limit(thrust, -1, 1);

    const float rotCoef = 10;

    float rotOld = _thrustR - _thrustL;
    float rotNew = rotOld + headChange * rotCoef;
    _thrustLWanted = thrust - rotNew;
    _thrustRWanted = thrust + rotNew;
    Limit(_thrustLWanted, -0.5, 1);
    Limit(_thrustRWanted, -0.5, 1);

#if DIAG_SPEED
    if (this == GWorld->CameraOn())
    {
        LOG_DEBUG(Physics, "Thrust {:.1f} L {:.1f} R {:.1f}", thrust, _thrustLWanted, _thrustRWanted);
    }
#endif

    if (fabs(headChange) < 0.2 && fabs(speedWanted) < 0.5)
    {
        if (fabs(speed[2]) < 0.5)
        {
            _thrustLWanted = _thrustRWanted = 0;
        }
        _pilotBrake = true;
    }
    else if (fabs(speed[2]) < 5 && fabs(speedWanted) < 0.5 && fabs(headChange) < 0.5)
    {
        _pilotBrake = true;
    }
    else
    {
        _pilotBrake = false;
    }
}

float Ship::GetAimed(int weapon, Target* target) const
{
    // base::GetAimed is able to handle bullets, shells and guided missiles
    return base::GetAimed(weapon, target);
}

void ShipWithAI::AIGunner(AIUnit* unit, float deltaT)
{
    base::AIGunner(unit, deltaT);
}

float ShipWithAI::FireInRange(int weapon, float& timeToAim, const Target& target) const
{
    timeToAim = 0;
    Vector3 relDir = PositionWorldToModel(target.position);
    return FireAngleInRange(weapon, relDir);
}

void ShipWithAI::Simulate(float deltaT, SimulationImportance prec)
{
    // if dammaged or upside down, tank is dead
    _isUpsideDown = DirectionUp().Y() < 0.3;
    _isDead = IsDammageDestroyed();

    SimulateUnits(deltaT);

    base::Simulate(deltaT, prec);
}

ShipWithAI::ShipWithAI(VehicleType* name, Person* driver) : Ship(name, driver), _stopPosition(VZero)
{
    _stopState = SSNone;
}

ShipWithAI::~ShipWithAI() = default;

bool Ship::FireWeapon(int weapon, TargetType* target)
{
    if (GetNetworkManager().IsControlsPaused())
    {
        return false;
    }
    if (weapon >= NMagazineSlots())
    {
        return false;
    }
    if (!GetWeaponLoaded(weapon))
    {
        return false;
    }
    if (!IsFireEnabled())
    {
        return false;
    }

    const WeaponModeType* mode = GetWeaponMode(weapon);
    if (!mode || !mode->_ammo)
    {
        return false;
    }
    bool fired = false;
    switch (mode->_ammo->_simulation)
    {
        case AmmoShotBullet:
        {
            Matrix4Val shootTrans = GunTurretTransform();
            fired =
                FireMGun(weapon, shootTrans.FastTransform(Type()->_gunPos), shootTrans.Rotate(Type()->_gunDir), target);
        }
        break;
        case AmmoNone:
            break;
        default:
            Fail("Unknown ammo used.");
            break;
    }
    if (fired)
    {
        VehicleWithAI::FireWeapon(weapon, target);
        return true;
    }
    return false;
}

void Ship::FireWeaponEffects(int weapon, const Magazine* magazine, EntityAI* target)
{
    const MagazineSlot& slot = GetMagazineSlot(weapon);
    if (!magazine || slot._magazine != magazine)
    {
        return;
    }

    const WeaponModeType* mode = GetWeaponMode(weapon);
    if (!mode)
    {
        return;
    }
    if (!mode->_ammo)
    {
        return;
    }

    if (EnableVisualEffects(SimulateVisibleNear))
    {
        switch (mode->_ammo->_simulation)
        {
            case AmmoShotBullet:
                _mGunClouds.Start(0.1);
                _mGunFire.Start(0.1, 0.4, true);
                _mGunFireFrames = 1;
                _mGunFireTime = Glob.uiTime;
                int newPhase;
                while ((newPhase = toIntFloor(GRandGen.RandomValue() * 3)) == _mGunFirePhase)
                {
                    ;
                }
                _mGunFirePhase = newPhase;
                break;
            case AmmoNone:
                break;
        }
    }

    base::FireWeaponEffects(weapon, magazine, target);
}

bool Ship::AimWeapon(int weapon, Vector3Par direction)
{
    if (weapon < 0)
    {
        if (NMagazineSlots() <= 0)
        {
            return false;
        }
        weapon = 0;
    }
    SelectWeapon(weapon);
    Vector3 relDir(VMultiply, DirWorldToModel(), direction);
    if (_turret.Aim(Type()->_turret, relDir))
    {
        CancelStop();
    }
    return true;
}

bool Ship::AimWeapon(int weapon, Target* target)
{
    if (weapon < 0)
    {
        if (NMagazineSlots() <= 0)
        {
            return false;
        }
        weapon = 0;
    }
    _fire.SetTarget(CommanderUnit(), target);
    const Magazine* magazine = GetMagazineSlot(weapon)._magazine;
    const MagazineType* aInfo = magazine ? magazine->_type : nullptr;
    Vector3 weaponPos = Type()->_gunPos;
    Vector3 tgtPos = target->AimingPosition();
    // predict his and my movement
    float dist2 = tgtPos.Distance2(Position());
    float time2 = 0;
    if (aInfo)
    {
        time2 = dist2 * Square(aInfo->_invInitSpeed * 1.2);
    }

    float time = sqrt(time2);
    const float minPredTime = 0.25;
    float predTime = floatMax(time + 0.1, minPredTime);
    Vector3 myPos = PositionModelToWorld(weaponPos);
    myPos += Speed() * minPredTime;
    float fall = 0.5 * G_CONST * time2;
    // calculate balistics
    tgtPos[1] += fall; // consider balistics
    if (aInfo)
    {
        Vector3 speedEst = target->speed;
        const float maxSpeedEst = aInfo->_maxLeadSpeed;
        if (speedEst.SquareSize() > Square(maxSpeedEst))
        {
            speedEst = speedEst.Normalized() * maxSpeedEst;
        }
        tgtPos += speedEst * predTime;
    }
    return AimWeapon(weapon, tgtPos - myPos);
}

Vector3 Ship::GetWeaponDirection(int weapon) const
{
    Vector3 dir = Type()->_gunDir;
    return Transform().Rotate(GunTurretTransform().Rotate(dir));
}

Vector3 Ship::GetWeaponCenter(int weapon) const
{
    return _turret.GetCenter(Type()->_turret);
}

float ShipWithAI::GetFieldCost(const GeographyInfo& info) const
{
    return 1;
}

float ShipWithAI::GetCost(const GeographyInfo& geogr) const
{
    float cost = Type()->GetMinCost(); // basic speed is 13 m/s
    // avoid any water
    if (geogr.u.waterDepth < 3)
    {
        if (geogr.u.waterDepth < 2)
        {
            return 1e30;
        }
        cost *= 4; // near shore - be carefull
    }
    // penalty for objects
    cost *= 1 + geogr.u.howManyObjects * 2;
    return cost;
}

float ShipWithAI::GetCostTurn(int difDir) const
{ // in sec
    if (difDir == 0)
    {
        return 0;
    }
    float aDir = fabs(difDir);
    float cost = aDir * 10 + aDir * aDir * 0.5;
    if (difDir < 0)
    {
        return cost * 0.8;
    }
    return cost;
}

void Ship::ResetStatus()
{
    base::ResetStatus();
    _sink = 0;
}

LSError Ship::Serialize(ParamArchive& ar)
{
    SERIAL_BASE
    if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
    {
        SERIAL_BITBOOL(pilotBrake, false)
        SERIAL_BITBOOL(targetOutOfAim, false)
        PARAM_CHECK(_turret.Serialize(ar))
        SERIAL_DEF(thrustL, 0)
        SERIAL_DEF(thrustLWanted, 0)
        SERIAL_DEF(thrustR, 0)
        SERIAL_DEF(thrustRWanted, 0)
        SERIAL_DEF(sink, 0)
    }
    return LSOK;
}

template <>
const EnumName* Foundation::GetEnumNames(ShipWithAI::StopState dummy)
{
    static const EnumName StopStateNames[] = {
        EnumName(ShipWithAI::SSNone, "NONE"), EnumName(ShipWithAI::SSFindPath, "FIND PATH"),
        EnumName(ShipWithAI::SSMove, "MOVE"), EnumName(ShipWithAI::SSStop, "STOP"), EnumName()};
    return StopStateNames;
}

LSError ShipWithAI::Serialize(ParamArchive& ar)
{
    SERIAL_BASE
    if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
    {
        SERIAL_DEF(stopPosition, VZero)
        SERIAL_ENUM(stopState, SSNone)
    }
    return LSOK;
}

NetworkMessageType ShipWithAI::GetNMType(NetworkMessageClass cls) const
{
    switch (cls)
    {
        case NMCUpdateGeneric:
            return NMTUpdateShip;
        case NMCUpdatePosition:
            return NMTUpdatePositionShip;
        default:
            return base::GetNMType(cls);
    }
}

#define UPDATE_SHIP_MSG(XX)                                                                                          \
    XX(bool, pilotBrake, NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("State of brake, wanted by player"),       \
       IdxTransfer, ET_NONE, 0)                                                                                      \
    XX(bool, targetOutOfAim, NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Target is out of aim"), IdxTransfer,  \
       ET_NONE, 0)                                                                                                   \
    XX(float, thrustLWanted, NDTFloat, NCTFloatM1ToP1, DEFVALUE(float, 0), DOC_MSG("Wanted thrust of left engine"),  \
       IdxTransfer, ET_NONE, 0)                                                                                      \
    XX(float, thrustRWanted, NDTFloat, NCTFloatM1ToP1, DEFVALUE(float, 0), DOC_MSG("Wanted thrust of right engine"), \
       IdxTransfer, ET_NONE, 0)                                                                                      \
    XX(Vector3, stopPosition, NDTVector, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Anchor position"), IdxTransfer, \
       ET_NONE, 0)                                                                                                   \
    XX(int, stopState, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, SSNone), DOC_MSG("Anchor state"), IdxTransfer,    \
       ET_NONE, 0)                                                                                                   \
    XX(float, sink, NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Ship is sinked"), IdxTransfer, ET_ABS_DIF,       \
       ERR_COEF_VALUE_MAJOR)

DECLARE_NET_INDICES_EX_ERR(UpdateShip, UpdateTransport, UPDATE_SHIP_MSG)
DEFINE_NET_INDICES_EX_ERR(UpdateShip, UpdateTransport, UPDATE_SHIP_MSG)

} // namespace Poseidon

DEFINE_GET_INDICES(UpdateShip)

namespace Poseidon
{

#define UPDATE_POSITION_SHIP_MSG(XX)                                                                                   \
    XX(Turret, turret, NDTObject, NCTNone, DEFVALUE_MSG(NMTUpdateTurret), DOC_MSG("Turret object"), IdxTransferObject, \
       ET_ABS_DIF, 1)

DECLARE_NET_INDICES_EX_ERR(UpdatePositionShip, UpdatePositionVehicle, UPDATE_POSITION_SHIP_MSG)
DEFINE_NET_INDICES_EX_ERR(UpdatePositionShip, UpdatePositionVehicle, UPDATE_POSITION_SHIP_MSG)

} // namespace Poseidon

DEFINE_GET_INDICES(UpdatePositionShip)

namespace Poseidon
{

NetworkMessageFormat& ShipWithAI::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    switch (cls)
    {
        case NMCUpdateGeneric:
            base::CreateFormat(cls, format);
            UPDATE_SHIP_MSG(MSG_FORMAT_ERR)
            break;
        case NMCUpdatePosition:
            base::CreateFormat(cls, format);
            UPDATE_POSITION_SHIP_MSG(MSG_FORMAT_ERR)
            break;
        default:
            base::CreateFormat(cls, format);
            break;
    }
    return format;
}

TMError ShipWithAI::TransferMsg(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
            TMCHECK(base::TransferMsg(ctx))
            {
                PoseidonAssert(dynamic_cast<const IndicesUpdateShip*>(ctx.GetIndices()))
                    const IndicesUpdateShip* indices = static_cast<const IndicesUpdateShip*>(ctx.GetIndices());

                ITRANSF_BITBOOL(pilotBrake)
                ITRANSF_BITBOOL(targetOutOfAim)
                ITRANSF(thrustLWanted)
                ITRANSF(thrustRWanted)
                ITRANSF(stopPosition)
                ITRANSF_ENUM(stopState)
                ITRANSF(sink)
            }
            break;
        case NMCUpdatePosition:
        {
            PoseidonAssert(dynamic_cast<const IndicesUpdatePositionShip*>(ctx.GetIndices()))
                const IndicesUpdatePositionShip* indices =
                    static_cast<const IndicesUpdatePositionShip*>(ctx.GetIndices());

            Matrix3 oldTrans = Orientation();
            TMCHECK(base::TransferMsg(ctx))
            if (ctx.IsSending() || !(GunnerUnit() && GunnerUnit()->GetPerson()->IsLocal()))
                TMCHECK(ctx.IdxTransferObject(indices->turret, _turret))
            _turret.Stabilize(this, Type()->_turret, oldTrans, Orientation());
        }
        break;
        default:
            return base::TransferMsg(ctx);
    }
    return TMOK;
}

float ShipWithAI::CalculateError(NetworkMessageContext& ctx)
{
    float error = 0;
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
            error += base::CalculateError(ctx);
            {
            }
            break;
        case NMCUpdatePosition:
        {
            error += base::CalculateError(ctx);

            PoseidonAssert(dynamic_cast<const IndicesUpdatePositionShip*>(ctx.GetIndices()))
                const IndicesUpdatePositionShip* indices =
                    static_cast<const IndicesUpdatePositionShip*>(ctx.GetIndices());

            int index = indices->turret;
            if (index >= 0)
            {
                NetworkMessageFormatBase* format = const_cast<NetworkMessageFormatBase*>(ctx.GetFormat());
                NetworkMessageFormatItem& item = format->GetItem(index);
                CHECK_ASSIGN(typeVal, item.defValue, const RefNetworkDataTyped<int>);
                int type = typeVal.GetVal();
                NetworkMessageFormatBase* subformat = ctx.GetComponent()->GetFormat((NetworkMessageType)type);
                if (subformat)
                {
                    const RefNetworkData& val = ctx.GetMessage()->values[index];
                    CHECK_ASSIGN(msgVal, val, const RefNetworkDataTyped<NetworkMessage>);
                    NetworkMessage& submsg = msgVal.GetVal();
                    NetworkMessageContext subctx(&submsg, subformat, ctx);
                    error += _turret.CalculateError(subctx);
                }
            }
        }
        break;
        default:
            error += base::CalculateError(ctx);
            break;
    }
    return error;
}

} // namespace Poseidon
