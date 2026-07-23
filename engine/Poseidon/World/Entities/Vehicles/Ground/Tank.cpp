#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Core/Application.hpp>

#include <Poseidon/World/Entities/Vehicles/Ground/Tank.hpp>
#include <Poseidon/World/Entities/Vehicles/Ground/TankNetworkStabilization.hpp>
#include <Poseidon/AI/AI.hpp>

#include <Poseidon/World/Entities/Weapons/Shots.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Dev/Diag/DiagModes.hpp>

#include <Random/randomGen.hpp>

#include <Poseidon/IO/ParamFile/ParamFile.hpp>

#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Game/OperMap.hpp>

#include <Poseidon/Graphics/Rendering/Draw/SpecLods.hpp>
#include <stdio.h>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/RemoveLinks.hpp>

#define ARROWS 0

#pragma warning(disable : 4355)

namespace Poseidon
{
Tank::Tank(VehicleType* name, Person* driver)
    : base(name, driver),

      _thrustL(0), _thrustR(0), _thrustLWanted(0), _thrustRWanted(0),

      _randFrequency(1 - GRandGen.RandomValue() * 0.05f), // do not use same sound frequency

      _track(_shape), _phaseL(0), _phaseR(0),

      _fireDustTimeLeft(0), _invFireDustTimeTotal(1),

      _backwardUsedAsBrake(false), _forwardUsedAsBrake(false), _parkingBrake(false)
{
    _rpm = 0.1f, _rpmWanted = 0.1f;
    // init gear box
    AutoArray<float> gears;
    gears.Add(0);
    gears.Add(1.0f / 7);
    gears.Add(1.0f / 10);
    gears.Add(1.0f / 14);
    gears.Add(1.0f / 19);
    _gearBox.SetGears(gears);

    _mGunClouds.Load((*Type()->_par) >> "MGunClouds");

    _mGunFireFrames = 0;
    _mGunFireTime = UITIME_MIN;
    _mGunFirePhase = 0;

    _gunClouds.Load((*Type()->_par) >> "GunClouds");
    _gunFire.Load((*Type()->_par) >> "GunFire");

    _fireDust.SetSize(1, 2);

    _head.SetPars("Land");
    _head.Init(Type()->_pilotPos - Vector3(0, 0.2f, 0), Type()->_pilotPos, this);
}

float Tank::GetEngineVol(float& freq) const
{
    freq = _randFrequency * _rpm * 1.2f;
    return (fabs(_thrustL) + fabs(_thrustR)) * 0.25f + 0.5f;
}

float Tank::GetEnvironVol(float& freq) const
{
    freq = 1;
    return _speed.SquareSize() / Square(Type()->GetMaxSpeedMs());
}

void Tank::PerformFF(FFEffects& effects)
{
    base::PerformFF(effects);
}

float Tank::GetHitForDisplay(int kind) const
{
    // see InGameUI::DrawTankDirection
    switch (kind)
    {
        case 0:
            return GetHitCont(Type()->_hullHit);
        case 1:
            return GetHitCont(Type()->_engineHit);
        case 2:
            return GetHitCont(Type()->_trackLHit);
        case 3:
            return GetHitCont(Type()->_trackRHit);
        case 4:
            return GetHitCont(Type()->_turretHit);
        case 5:
            return GetHitCont(Type()->_gunHit);
        default:
            return 0;
    }
}

void Tank::Sound(bool inside, float deltaT)
{
    if (_doGearSound && !_gearSound)
    {
        _doGearSound = false;
        IWave* sound = GSoundScene->OpenAndPlayOnce(Type()->_gearSound.name, Position(), Speed());
        _gearSound = sound;
        if (sound)
        {
            GSoundScene->SimulateSpeedOfSound(sound);
            GSoundScene->AddSound(sound);
        }
    }
    if (_gearSound)
    {
        float gearVol = Type()->_gearSound.vol;
        if (inside)
        {
            gearVol *= 0.2f;
        }
        _gearSound->SetVolume(gearVol); // volume, frequency
        _gearSound->SetPosition(Position(), Speed());
    }

    _mainTurret.Sound(Type()->_mainTurret, inside, deltaT, *this, Speed());
    _comTurret.Sound(Type()->_comTurret, inside, deltaT, *this, Speed());
    base::Sound(inside, deltaT);
}

bool Tank::IsTurret(CameraType camType) const
{
    AIUnit* unit = GWorld->FocusOn();
    if (unit)
    {
        if (unit == CommanderUnit())
        {
            // commander needs picture to be able to give commands
            return true;
        }
        if (unit == PilotUnit())
        {
            if (_driverHidden < 0.5f)
            {
                return false;
            }
        }
        else if (unit == GunnerUnit())
        {
            if (_gunnerHidden < 0.5f)
            {
                return false;
            }
        }
        if (unit == ObserverUnit())
        {
            // if (_commanderHidden<0.5) return false;
        }
    }
    return true;
}

bool Tank::HasFlares(CameraType camType) const
{
    if (camType == CamGunner)
    {
        return Type()->_outPilotOnTurret;
    }
    return base::HasFlares(camType);
}

inline float tanSat(float x)
{
    // tan(pi*0.49) == 31.82
    if (x > +H_PI * 0.49f)
    {
        return +32;
    }
    if (x < -H_PI * 0.49f)
    {
        return -32;
    }
    return atan(x);
}

void Tank::LimitCursorHard(CameraType camType, Vector3& dir) const
{
    if (camType == CamGunner)
    {
        // when controlling weapons,
        // limit cursor to maximum angle given by current weapon systems
        // plus screen size angle
        AIUnit* unit = GWorld->FocusOn();
        Person* person = unit ? unit->GetPerson() : nullptr;
        if (person)
        {
            if (person == _commander)
            {
                Vector3 relDir = DirectionWorldToModel(dir);
                float scrAngle = atan(GScene->GetCamera()->Top()) * 0.99f;
                // float scrAngle = 0;
                //  commander turret does not elevate together with main turret
                const TurretType& ttype = Type()->_comTurret;
                float minElev = ttype._minElev;
                float maxElev = ttype._maxElev;
                // normalize direction to plane with distance 1
                float sizeXZ = relDir.SizeXZ();
                float minAlpha = minElev - scrAngle + ttype._neutralXRot;
                float maxAlpha = maxElev + scrAngle + ttype._neutralXRot;

                // convert angular elevation to offset in distance sizeXZ
                // avoid overflow when alpha is out of range -pi/2,+pi/2
                float minY = tanSat(minAlpha) * sizeXZ;
                float maxY = tanSat(maxAlpha) * sizeXZ;
                // float minY = tanSat(minAlpha);
                // float maxY = tanSat(maxAlpha);
                // float minY = minAlpha*sizeXZ;
                // float maxY = maxAlpha*sizeXZ;

                saturate(relDir[1], minY, maxY);
                dir = DirectionModelToWorld(relDir);
            }
            else if (person == _gunner)
            {
                const TurretType& ttype = Type()->_mainTurret;
                Vector3 relDir = DirectionWorldToModel(dir);
                float scrAngle = atan(GScene->GetCamera()->Top()) * 0.99f;
                // float scrAngle = 0;
                //  commander turret does not elevate together with main turret
                float minElev = ttype._minElev;
                float maxElev = ttype._maxElev;
                // normalize direction to plane with distance 1
                float sizeXZ = relDir.SizeXZ();
                float minAlpha = minElev - scrAngle + ttype._neutralXRot;
                float maxAlpha = maxElev + scrAngle + ttype._neutralXRot;

                // convert angular elevation to offset in distance sizeXZ
                // avoid overflow when alpha is out of range -pi/2,+pi/2
                float minY = tanSat(minAlpha) * sizeXZ;
                float maxY = tanSat(maxAlpha) * sizeXZ;
                // float minY = tanSat(minAlpha);
                // float maxY = tanSat(maxAlpha);
                // float minY = minAlpha*sizeXZ;
                // float maxY = maxAlpha*sizeXZ;

                saturate(relDir[1], minY, maxY);
                dir = DirectionModelToWorld(relDir);
            }
            else if (person == _driver)
            {
            }
            else
            {
            }
        }
    }
}

void Tank::LimitVirtual(CameraType camType, float& heading, float& dive, float& fov) const
{
    /*
        switch( camType )
        {
            case CamGunner:
                saturate(fov,0.07f,0.35f);
                base::LimitVirtual(camType,heading,dive,fov);
            break;
        }
    */
    base::LimitVirtual(camType, heading, dive, fov);
}

void Tank::InitVirtual(CameraType camType, float& heading, float& dive, float& fov) const
{
    base::InitVirtual(camType, heading, dive, fov);
    /*
        switch( camType )
        {
            case CamGunner:
                fov=0.3;
            break;
        }
    */
}

void Tank::UnloadSound()
{
    base::UnloadSound();
    _engineSound.Free();
    _mainTurret.UnloadSound();
    _comTurret.UnloadSound();
}

Vector3 Tank::Friction(Vector3Par speed)
{
    Vector3 friction;
    friction.Init();
    friction[0] = speed[0] * fabs(speed[0]) * 50 + speed[0] * 1500 + fSign(speed[0]) * 300;
    friction[1] = speed[1] * fabs(speed[1]) * 50 + speed[1] * 1100 + fSign(speed[1]) * 60;
    friction[2] = speed[2] * fabs(speed[2]) * 15 + speed[2] * 100 + fSign(speed[2]) * 10;
    return friction * GetMass() * (1.0f / 60000);
}

void Tank::MoveWeapons(float deltaT)
{
    // move all turrets
    {
        AIUnit* unit = GunnerUnit();
        if (!unit)
        {
            _mainTurret.Stop(Type()->_mainTurret);
        }
        else
        {
            // check if driver hatch is closed
            // if not, main turret must be locked
            // note: this probably does not apply to some tanks (M113, BMP)
            if (GetHit(Type()->_gunHit) > 0.9f)
            {
                _mainTurret.GunBroken(Type()->_mainTurret);
            }
            if (Type()->HasDriver() && _driverHidden < 0.99f)
            {
                _mainTurret._gunStabilized = false;
                _mainTurret.Aim(Type()->_mainTurret, VForward);
            }
            else if (GetHit(Type()->_turretHit) > 0.9f)
            {
                _mainTurret.TurretBroken(Type()->_mainTurret);
            }
            else
            {
                _mainTurret._gunStabilized = true;
            }
            _mainTurret.MoveWeapons(Type()->_mainTurret, unit, deltaT);
        }
    }
    {
        AIUnit* unit = ObserverUnit();
        if (!unit)
        {
            _comTurret.Stop(Type()->_comTurret);
        }
        else
        {
            if (GetHit(Type()->_turretHit) > 0.9f)
            {
                _comTurret.TurretBroken(Type()->_comTurret);
            }
            else
            {
                _comTurret._gunStabilized = true;
            }
            _comTurret.MoveWeapons(Type()->_comTurret, unit, deltaT);
        }
    }
}

LSError TankWithAI::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar));

    if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
    {
        PARAM_CHECK(_mainTurret.Serialize(ar))
        PARAM_CHECK(_comTurret.Serialize(ar))

        PARAM_CHECK(ar.Serialize("invFireDustTimeTotal", _invFireDustTimeTotal, 1))
        PARAM_CHECK(ar.Serialize("fireDustTimeLeft", _fireDustTimeLeft, 1))

        PARAM_CHECK(ar.Serialize("thrustLWanted", _thrustLWanted, 1, 0))
        PARAM_CHECK(ar.Serialize("thrustRWanted", _thrustRWanted, 1, 0))
        PARAM_CHECK(ar.Serialize("thrustL", _thrustL, 1, 0))
        PARAM_CHECK(ar.Serialize("thrustR", _thrustR, 1, 0))
    }
    return LSOK;
}

bool Tank::IsPossibleToGetIn() const
{
    if (GetHit(Type()->_trackRHit) >= 0.9f)
    {
        return false;
    }
    if (GetHit(Type()->_trackLHit) >= 0.9f)
    {
        return false;
    }
    if (GetHit(Type()->_engineHit) >= 0.9f)
    {
        return false;
    }
    if (GetHit(Type()->_hullHit) >= 0.9f)
    {
        return false;
    }
    return base::IsPossibleToGetIn();
}

bool Tank::IsAbleToMove() const
{
    if (GetHit(Type()->_trackRHit) >= 0.9f)
    {
        return false;
    }
    if (GetHit(Type()->_trackLHit) >= 0.9f)
    {
        return false;
    }
    if (GetHit(Type()->_engineHit) >= 0.9f)
    {
        return false;
    }
    if (GetHit(Type()->_hullHit) >= 0.9f)
    {
        return false;
    }
    return base::IsAbleToMove();
}

bool Tank::IsAbleToFire() const
{
    if (GetHit(Type()->_turretHit) >= 0.9f)
    {
        return false;
    }
    if (GetHit(Type()->_gunHit) >= 0.9f)
    {
        return false;
    }
    return base::IsAbleToFire();
}

void Tank::LandFriction(Vector3& friction, Vector3& torqueFriction, Vector3& torque, bool brakeFriction,
                        Vector3Par fSpeed, Vector3Par speed, Vector3Par pCenter, float coefNPoints, Texture* texture)
{
    Vector3 pForce;
    pForce.Init();
    // second is "land friction" - causing no momentum
    pForce[0] = fSpeed[0] * 2000 + fSign(fSpeed[0]) * 30000;
    pForce[1] = fSpeed[1] * 8000 + fSign(fSpeed[1]) * 5000;
    if (brakeFriction)
    {
        pForce[2] = fSpeed[2] * 200 + fSign(fSpeed[2]) * 40000;
    }
    else
    {
        pForce[2] = fSpeed[2] * 100 + fSign(fSpeed[2]) * 2000;
    }

#if ARROWS
    Vector3 wCenter(VFastTransform, ModelToWorld(), GetCenterOfMass());
#endif

    pForce = DirectionModelToWorld(pForce) * GetMass() * (1.0f / 40000);
#if ARROWS
    AddForce(wCenter + pCenter, -pForce * InvMass(), Color(1, 0, 0));
#endif
    // apply some torque
    friction += pForce * coefNPoints;
    torqueFriction += _angMomentum * 0.12f * coefNPoints;

    // some friction is caused by moving the land aside
    // this applies only to soft surfaces
    float soft = 0;
    if (texture)
    {
        soft = texture->Roughness() * 0.7f;
        // soft*=softFactor;
        saturateMin(soft, 1);
    }
    float landMoved = 0.01f * soft;
    if (brakeFriction)
    {
        landMoved *= 2;
    }
    pForce[0] = speed[0] * 35.0f * landMoved;
    pForce[1] = 0;
    pForce[2] = speed[2] * 6.0f * landMoved;
    pForce = DirectionModelToWorld(pForce) * GetMass();
#if ARROWS
    AddForce(wCenter + pCenter, -pForce * InvMass(), Color(1, 1, 0, 0.5f));
#endif
    friction += pForce * coefNPoints;
}

void Tank::ObjectContact(Frame& moveTrans, Vector3Par wCenter, float deltaT, Vector3& torque, Vector3& friction,
                         Vector3& torqueFriction, Vector3& totForce, float& crash)
{
    Vector3Val speed = ModelSpeed();

    CollisionBuffer collision;
    GLOB_LAND->ObjectCollision(collision, this, moveTrans);
#define MAX_IN 0.2f
#define MAX_IN_FORCE 0.1f
#define MAX_IN_FRICTION 0.2f

    for (int i = 0; i < collision.Size(); i++)
    {
        _objectContact = true;
        // info.pos is relative to object
        CollisionInfo& info = collision[i];
        Object* obj = info.object;
        if (!obj)
        {
            continue;
        }
        if (info.hierLevel > 0)
        {
            continue;
        }
        if (!obj->GetShape())
        {
            continue;
        }
        float cFactor = obj->GetMass() * InvMass();
        if (obj->Static())
        {
            // fixed object - apply fixed collision routines
            // calculate his dammage
            // depending on vehicle speed and mass
            float dFactor = GetMass() * obj->InvMass();
            float dSpeed = _speed.SquareSize() + _angVelocity.SquareSize();
            float dammage = dSpeed * obj->GetInvArmor() * dFactor * 0.2f;
            if (dammage > 0.01f)
            {
                obj->LocalDammage(nullptr, this, VZero, dammage, obj->GetShape()->GeometrySphere());
            }
            if (obj->GetDestructType() == DestructTree || obj->GetDestructType() == DestructTent ||
                obj->GetDestructType() == DestructMan)
            {
                saturate(cFactor, 0.001f, 0.03f);
            }
            else
            {
                saturate(cFactor, 0.001f, 1);
            }
        }
        else
        {
            saturate(cFactor, 0, 10);
        }
        Point3 pos = info.object->PositionModelToWorld(info.pos);
        Vector3 dirOut = info.object->DirectionModelToWorld(info.dirOut);
        // create a force pushing "out" of the collision
        float forceIn = floatMin(info.under, MAX_IN_FORCE);
        Vector3 pForce = dirOut * GetMass() * 40 * forceIn * cFactor;
        // apply proportional part of force in place of impact
        Vector3 pCenter = pos - wCenter;
        if (cFactor > 0.01f)
        {
            totForce += pForce;
            // apply same force to second object
            torque += pCenter.CrossProduct(pForce * 0.5f);
        }

        Vehicle* veh = dyn_cast<Vehicle, Object>(obj);
        if (veh)
        {
            // transfer all my intertia to him?
            Vector3 relDistance = veh->Position() - Position();
            Vector3 relSpeed = veh->Speed() - Speed();
            if (_speed.SquareSize() > Square(3.5f) && relSpeed.SquareSize() > Square(3.5f))
            {
                if (dyn_cast<Person>(veh))
                {
                    // soldier - different dammage calculation
                    float speedTransfer = relSpeed * relDistance * -relDistance.InvSize();
                    saturate(speedTransfer, 0, 0.5f);
                    float limitMass = floatMin(GetMass(), 20000);
                    Vector3 impulse = _speed * limitMass * deltaT * speedTransfer * 0.08f;

                    veh->AddImpulseNetAware(impulse, info.pos.CrossProduct(impulse));
                }
                else
                {
                    float speedTransfer = relSpeed * relDistance * -relDistance.InvSize();
                    saturate(speedTransfer, 0, 0.5f);
                    Vector3 impulse = _speed * GetMass() * deltaT * speedTransfer * 0.02f;

                    veh->AddImpulseNetAware(impulse, info.pos.CrossProduct(impulse));
                }
            }
        }

        if (cFactor < 0.05f)
        {
            continue;
        }

        // if info.under is bigger than MAX_IN, move out
        if (info.under > MAX_IN)
        {
            Matrix4 transform = moveTrans.Transform();
            Vector3 newPos = transform.Position();
            float moveOut = info.under - MAX_IN;
            Vector3 move = dirOut * moveOut * 0.1f;
            newPos += move;
            transform.SetPosition(newPos);
            moveTrans.SetTransform(transform);
            const float crashLimit = 0.3f;
            if (moveOut > crashLimit)
            {
                crash += moveOut - crashLimit;
            }

            Vector3Val objSpeed = info.object->ObjectSpeed();
            Vector3 colSpeed = _speed - objSpeed;

            float potentialGain = move[1] * GetMass();
            float oldKinetic = GetMass() * colSpeed.SquareSize() * 0.5f; // E=0.5*m*v^2
            // kinetic to potential conversion is not 100% effective
            float crashFactor = (moveOut - crashLimit) * 4 + 1.5f;
            saturateMax(crashFactor, 2.5f);
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
                crash += (colSpeed.Size() - maxRelSpeed) * 0.3f;
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

        pForce = DirectionModelToWorld(pForce) * GetMass() * (4.0f / 10000) * frictionIn;
#if ARROWS
        AddForce(wCenter + pCenter, -pForce * InvMass());
#endif
        friction += pForce;
        torqueFriction += _angMomentum * 0.15f;
    }
}

#if _ENABLE_CHEATS
extern bool disableSimpleSim;
#endif

void Tank::StabilizeTurrets(Matrix3Val oldTrans, Matrix3Val newTrans, Matrix3Val oldTurretTrans)
{
    _mainTurret.Stabilize(this, Type()->_mainTurret, oldTrans, newTrans);
    // stabilize with relation to mainTurret
    Matrix3 newTurretTrans = Poseidon::BuildCommanderTankTurretStabilizationFrame(Type()->_comTurretOnMainTurret,
                                                                                  newTrans, TurretTransform());
    const Matrix3& oldComTurretFrame = Poseidon::SelectCommanderTankTurretOldStabilizationFrame(
        Type()->_comTurretOnMainTurret, oldTrans, oldTurretTrans);
    _comTurret.Stabilize(this, Type()->_comTurret, oldComTurretFrame, newTurretTrans);
}

bool Tank::UseSimpleSimulation(SimulationImportance prec) const
{
    if (_isUpsideDown)
    {
        return false;
    }
    if (!_landContact || _waterContact)
    {
        return false;
    }
#if _ENABLE_CHEATS
    if (disableSimpleSim)
        return false;
#endif
    if (prec >= SimulateInvisibleNear)
    {
        return true;
    }
    return prec >= SimulateVisibleNear && Glob.time > _freeFallUntil;
}

void Tank::SimulateOptimized(float deltaT, SimulationImportance prec)
{
    float step = SimulationPrecision();
    // static StatEventRatio ratio("Smple tank sim");
    // ratio.Count(UseSimpleSimulation(prec));

    if (UseSimpleSimulation(prec))
    {
        // when using simple simulation, we can use very big step
        step = 0.2f;
    }

    // variable simulation step, based on distance from camera
    _simulationSkipped += deltaT;
    if (_simulationSkipped >= step)
    {
        Simulate(step, prec);
        _simulationSkipped -= step;
    }
}

void Tank::PlaceOnSurface(Matrix4& trans)
{
    base::PlaceOnSurface(trans);

    Vector3 pos = trans.Position();

    Texture* txt = nullptr;
    float dX, dZ;
    GLandscape->RoadSurfaceYAboveWater(pos, &dX, &dZ, &txt);
    if (txt && GLandscape->GetTexture(0) == txt)
    {
        // estimate water sub-merging
        pos[1] -= 1;
    }
    else
    {
        Vector3 normal(-dX, 1, -dZ);
        normal.Normalize();

        Shape* lcLevel = GetShape()->LandContactLevel();
        if (!lcLevel)
        {
            lcLevel = GetShape()->GeometryLevel();
        }
        Vector3 comDown = GetCenterOfMass();
        comDown[1] = lcLevel->Min().Y();
        Vector3 nPos = trans.FastTransform(comDown);

        // check COM position on the ground
        Vector3 oPos = nPos;
        nPos[1] = GLandscape->RoadSurfaceYAboveWater(nPos, &dX, &dZ);

        float offset = 0.075f;
        float normalizedOffset = offset / normal.Y();
        nPos[1] += normalizedOffset;

        pos[1] += nPos[1] - oPos[1];
    }
    trans.SetPosition(pos);
}

static Vector3 AddFriction(Vector3Val force, Vector3Val friction)
{
    Vector3 res = force + friction;
    if (res[0] * force[0] <= 0)
    {
        res[0] = 0;
    }
    if (res[1] * force[1] <= 0)
    {
        res[1] = 0;
    }
    if (res[2] * force[2] <= 0)
    {
        res[2] = 0;
    }
    return res;
}

void Tank::Simulate(float deltaT, SimulationImportance prec)
{
    if (!_cargoLight && Type()->_cargoLightPos.Size() > 0)
    {
        LODShapeWithShadow* shape = GLOB_SCENE->Preloaded(Marker);
        _cargoLight =
            new LightPointOnVehicle(shape, Color(0, 0, 0, 0), Color(0, 0, 0, 0), this, Type()->_cargoLightPos);
        _cargoLight->LightPoint::Load((*Type()->_par) >> "CargoLight");
        GScene->AddLight(_cargoLight);
    }

    Vector3Val speed = ModelSpeed();
    float speedSize = fabs(speed.Z());
    if (!_engineOff)
    {
        ConsumeFuel(deltaT * (0.01f + _rpm * 0.02f));

        if (_fuel <= 0)
        {
            _engineOff = true, _fuel = 0, _rpmWanted = 0;
        }
        else
        {
            // calculate engine rpm
            _rpmWanted = speedSize * _gearBox.Ratio();
            if (_rpmWanted < 0.3f)
            {
                float avgThrust = (fabs(_thrustR) + fabs(_thrustL)) * 0.5f;
                _rpmWanted = avgThrust * 0.7f + 0.3f;
            }
            saturate(_rpmWanted, 0, 2);
        }
    }
    else
    {
        // engine off
        _rpmWanted = 0;
    }

    if (
        // tank is going to explode
        GetHit(Type()->_engineHit) >= 0.9f || GetHit(Type()->_hullHit) >= 0.9f)
    {
        if (IsLocal() && _explosionTime > Glob.time + 60)
        {
            // set some explosion
            _explosionTime = Glob.time + GRandGen.Gauss(2, 5, 20);
        }
    }

    float delta;
    delta = _rpmWanted - _rpm;
    Limit(delta, -0.5f * deltaT, +0.3f * deltaT);
    _rpm += delta;

    // check actual selection

    Matrix3 oldTurretTrans = Orientation() * TurretTransform().Orientation();
    MoveWeapons(deltaT);

    if (_isDead || _isUpsideDown)
    {
        _engineOff = true, _pilotBrake = true;
    }
    if (_engineOff)
    {
        _thrustLWanted = _thrustRWanted = 0, _pilotBrake = true;
    }

    if (fabs(_thrustLWanted) > 0.1f || fabs(_thrustRWanted) > 0.1f)
    {
        IsMoved();
    }

    // if( _impulseForce.SquareSize()>Square(GetMass()*1e-6f) )
    if (_impulseForce.SquareSize() > Square(GetMass() * 0.005f))
    {
        IsMoved();
        // make this time longer that stop detection
        // so that if vehicle is stable long enough, it goes directly
        // from complete simulation to full stop
        _freeFallUntil = Glob.time + 5.5f;
    }
    if (!_landContact)
    {
        IsMoved();
        _freeFallUntil = Glob.time + 2;
    }
    if (GetStopped())
    {
        // reset impulse - avoid acummulation
        _impulseForce = VZero;
        _impulseTorque = VZero;
    }

    if (EnableVisualEffects(prec))
    {
        if (_gunFire.Active() || _gunClouds.Active() || _mGunClouds.Active() || _mGunFire.Active())
        {
            const TurretType& tur = Type()->_mainTurret;
            Matrix4Val gunTransform = GunTurretTransform();
            Matrix4Val toWorld = Transform() * gunTransform;
            Vector3Val dir = toWorld.Direction();
            Vector3 firePos(VFastTransform, toWorld, tur._pos + 0.6f * tur._dir);
            Vector3 smokePos(VFastTransform, toWorld, tur._pos + 0.6f * tur._dir);
            Vector3 gunPos(VFastTransform, toWorld, Type()->_gunPos);
            _gunFire.Simulate(firePos, Speed() * 0.85f + dir * 20, 0.8f, deltaT);
            _gunClouds.Simulate(smokePos, Speed() * 0.7f + dir * 5, 0.7f, deltaT);
            _mGunClouds.Simulate(gunPos, Speed() * 0.7f + dir * 0.1f, 0.35f, deltaT);
            _mGunFire.Simulate(gunPos, deltaT);
        }
    }

    bool isStatic = Type()->GetFuelCapacity() <= 0;
    if (!GetStopped() && !isStatic && !CheckPredictionFrozen())
    {
        // simulate left/right engine
        delta = _thrustLWanted - _thrustL;
        if (_thrustLWanted * _thrustL <= 0)
        {
            Limit(delta, -2.0f * deltaT, +2.0f * deltaT);
        }
        else
        {
            Limit(delta, -deltaT, +deltaT);
        }
        _thrustL += delta;
        Limit(_thrustL, -1.0f, 1.0f);

        delta = _thrustRWanted - _thrustR;
        if (_thrustRWanted * _thrustR <= 0)
        {
            Limit(delta, -2.0f * deltaT, +2.0f * deltaT);
        }
        else
        {
            Limit(delta, -deltaT, +deltaT);
        }
        _thrustR += delta;
        Limit(_thrustR, -1.0f, 1.0f);

        // calculate all forces, frictions and torques
        Vector3 force(VZero), friction(VZero);
        Vector3 torque(VZero), torqueFriction(VZero);

        Vector3 pForce(NoInit);  // partial force
        Vector3 pCenter(NoInit); // partial force application point

        Vector3 wCenter(VFastTransform, ModelToWorld(), GetCenterOfMass());

        // apply left/right thrust

        bool gearChanged = false;
        if (_engineOff)
        {
            // upside down
            gearChanged = _gearBox.Neutral();
        }
        else
        {
            gearChanged = _gearBox.Change(speedSize);
            if (_landContact || _objectContact || _waterContact)
            {
                // the more we turn, the more power we loose
                float eff = 1 - fabs(_thrustR - _thrustL) * 0.4f;
                saturateMax(eff, 0);
                float invSpeedSize;
                const float coefInvSpeed = 3;
                const float defSpeed = 80.0f; // model tuned at this speed (plain level grass)
                float power = Type()->GetMaxSpeed() * (1 / defSpeed);
                const float minInvSpeed = 2.0f;
                const float invSpeedSizeMax = coefInvSpeed / (minInvSpeed * power);
                if (speedSize < minInvSpeed * power)
                {
                    invSpeedSize = invSpeedSizeMax;
                }
                else
                {
                    invSpeedSize = coefInvSpeed / speedSize;
                }
                float rpmEff = 0.7f;
                const float effC = 3.0f * rpmEff * eff;
                float invSpeedSizeL = speed.Z() * _thrustL < 0.1f ? invSpeedSizeMax : invSpeedSize;
                float invSpeedSizeR = speed.Z() * _thrustR < 0.1f ? invSpeedSizeMax : invSpeedSize;
                invSpeedSizeL *= effC;
                invSpeedSizeR *= effC;
                // water movement is much less efficient
                if (!_landContact && !_objectContact)
                {
                    invSpeedSizeL *= 0.1f;
                    invSpeedSizeR *= 0.1f;
                }
                float turnEnhancer = speedSize * 0.5f * (1 / power);
                turnEnhancer *= CollisionSize() * (1.0f / 3);
                saturate(turnEnhancer, 2, 10);
                float lAccel = _thrustL * invSpeedSizeL * power;
                float rAccel = _thrustR * invSpeedSizeR * power;

                rAccel *= 1 - GetHit(Type()->_engineHit);
                lAccel *= 1 - GetHit(Type()->_engineHit);

                rAccel *= 1 - GetHit(Type()->_trackRHit);
                lAccel *= 1 - GetHit(Type()->_trackLHit);

                pForce = Vector3(0, 0, lAccel * GetMass());
                force += pForce;
                pCenter = Vector3(+7 * turnEnhancer, 0, 0); // relative to the center of mass
                torque += pCenter.CrossProduct(pForce);
#if ARROWS
                AddForce(DirectionModelToWorld(pCenter) + wCenter, DirectionModelToWorld(pForce * InvMass()));
#endif

                pForce = Vector3(0, 0, rAccel * GetMass());
                force += pForce;
                pCenter = Vector3(-7 * turnEnhancer, 0, 0); // relative to the center of mass
                torque += pCenter.CrossProduct(pForce);
#if ARROWS
                AddForce(DirectionModelToWorld(pCenter) + wCenter, DirectionModelToWorld(pForce * InvMass()));
#endif
            }
        }

        if (gearChanged)
        {
            _doGearSound = true;
        }

        float forwardChangePhase = ModelSpeed()[2] * 0.5f;
        const float thrustCoef = 0.5f;
        _phaseL += (_thrustL * thrustCoef + forwardChangePhase) * deltaT;
        if (_phaseL >= 1)
        {
            _phaseL -= 1;
        }
        if (_phaseL < 0)
        {
            _phaseL += 1;
        }

        _phaseR += (_thrustR * thrustCoef + forwardChangePhase) * deltaT;
        if (_phaseR >= 1)
        {
            _phaseR -= 1;
        }
        if (_phaseR < 0)
        {
            _phaseR += 1;
        }

        // convert forces to world coordinates
        DirectionModelToWorld(torque, torque);
        DirectionModelToWorld(force, force);

        // apply gravity
        pForce = Vector3(0, -G_CONST * GetMass(), 0);
        force += pForce;

#if ARROWS
        AddForce(wCenter, pForce * InvMass());
#endif

        torqueFriction = _angMomentum * 1.2f;

        // calculate new position
        Matrix4 movePos;
        ApplySpeed(movePos, deltaT);
        // consider using frame with inverse
        Frame moveTrans;
        moveTrans.SetTransform(movePos);

        // body air friction
        DirectionModelToWorld(friction, Friction(speed));
#if ARROWS
        AddForce(wCenter, friction * InvMass());
#endif

        wCenter.SetFastTransform(moveTrans.ModelToWorld(), GetCenterOfMass());

        Texture* texture = nullptr;

        if (deltaT > 0)
        {
            // check collision on new position
            Vector3 totForce(VZero);

            float crash = 0;
            _objectContact = false;
            if (prec <= SimulateVisibleFar && IsLocal())
            {
                ObjectContact(moveTrans, wCenter, deltaT, torque, friction, torqueFriction, totForce, crash);
            } // if( object collisions enabled )

            float nSpeed = Type()->GetMaxSpeed() * 0.16f; // km/h
            bool brakeFriction = false;
            Vector3 fSpeed;
            if (_landContact && (DirectionUp().Y() <= 0.3f || _pilotBrake))
            {
                // brake friction speed
                brakeFriction = true;
                fSpeed = speed;
            }
            else
            {
                fSpeed = speed - Vector3(0, 0, (_thrustL + _thrustR) * nSpeed);
            }
            float maxUnderWater = 0;
            float softFactor = floatMin(5000 * GetInvMass(), 1.0f);
#define MAX_UNDER_FORCE 0.1f
#define UNDER_OFFSET MAX_UNDER_FORCE

            if (UseSimpleSimulation(prec))
            {
                Shape* lcLevel = GetShape()->LandContactLevel();
                if (!lcLevel)
                {
                    lcLevel = GetShape()->GeometryLevel();
                }

                float dX, dZ;
                // Vector3 comOffset=moveTrans.DirectionModelToWorld(GetCenterOfMass());
                // Vector3 pos=moveTrans.PositionModelToWorld(GetCenterOfMass());

                // check COM-corresponding position on the landcontact level
                Vector3 comDown = GetCenterOfMass();
                comDown[1] = lcLevel->Min().Y();
                Vector3 nPos = moveTrans.FastTransform(comDown);

                // check COM position on the ground
                Vector3 oPos = nPos;

                // calculate surface Y in COM position
                // Vector3 pos=moveTrans.Position();
                // Vector3 nPos = oPos;
                nPos[1] = GLandscape->RoadSurfaceYAboveWater(nPos, &dX, &dZ, &texture);
#if _ENABLE_CHEATS
                if (CHECK_DIAG(DECollision))
                {
                    GScene->DrawCollisionStar(oPos, 0.06f, PackedColor(Color(0.25, 0.25, 0)));
                    GScene->DrawCollisionStar(nPos, 0.1f, PackedColor(Color(0.5, 0.5, 0)));
                }
#endif

                Vector3 normal(-dX, 1, -dZ);
                normal.Normalize();

                // convert to object position
                // check if there is some water

                if (texture && texture == GLandscape->GetTexture(0))
                {
                    // simulate water movement - cannot use simple simulation here?
                    _waterContact = true;
                }
                // offset tuned empirically
                // so that simple and complete simulation do not differ
                float offset = 0.07f;

                // Min().Y() offset should be considered in direction of surface normal
                float normalizedOffset = offset / normal.Y();
                nPos[1] += normalizedOffset;

                // one contact point is always used in this version
                // const float coefNPoints = 9.0f/lcPoints;
                const float coefNPoints = 9.0f;

                Vector3 stabilize = normal - moveTrans.DirectionUp();
                torque += Vector3(0, coefNPoints * 8 * GetMass(), 0).CrossProduct(stabilize);

                if (!_waterContact)
                {
                    Vector3 mPos = moveTrans.Position();
                    mPos[1] += nPos[1] - oPos[1];
                    moveTrans.SetPosition(mPos);
                }

                // there should be force pushing tank out-of-ground
                // vertical component of this force should be 1G
                // first approximation: force size is 1G
                pForce = normal * GetMass() * G_CONST;
                totForce += pForce;

                // calculate friction
                LandFriction(friction, torqueFriction, torque, brakeFriction, fSpeed, speed,
                             Vector3(0, -1, 0), // simpilfied simulation - apply below center of mass
                             coefNPoints, texture);

                // calculate acceleration
                // align speed to copy terrain
                _speed[1] = _speed[0] * dX + _speed[2] * dZ;
            }
            else
            {
                GroundCollisionBuffer gCollision;
                if (prec >= SimulateVisibleNear)
                {
                    GLandscape->GroundCollisionPlane(gCollision, this, moveTrans, UNDER_OFFSET, softFactor);
                }
                else
                {
                    GLandscape->GroundCollision(gCollision, this, moveTrans, UNDER_OFFSET, softFactor);
                }
                _landContact = false;
                _waterContact = false;
#define MAX_UNDER 0.4f

                Shape* lcLevel = GetShape()->LandContactLevel();
                if (!lcLevel)
                {
                    lcLevel = GetShape()->GeometryLevel();
                }
                int lcPoints = lcLevel ? lcLevel->NPoints() : 6;
                saturateMax(lcPoints, gCollision.Size());

                const float coefNPoints = 9.0f / lcPoints;

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
                        // const float coefNPoints=3;
                        // first is water is "pushing" everything up - causing some momentum
                        saturateMax(maxUnderWater, info.under);

                        pForce = Vector3(0, 15000 * info.under * coefNPoints, 0);
                        if (!Type()->_canFloat)
                        {
                            pForce *= 0.3f;
                        }
                        pCenter = info.pos - wCenter;
                        torque += pCenter.CrossProduct(pForce);
                        totForce += pForce;

                        // add stabilizing torque
                        // stabilized means DirectionUp() is (0,1,0)
                        Vector3 stabilize = VUp - moveTrans.DirectionUp();
                        torque += Vector3(0, coefNPoints * 1.8f * GetMass(), 0).CrossProduct(stabilize);

#if ARROWS
                        AddForce(wCenter + pCenter, pForce * InvMass());
#endif

                        // second is "water friction" - causing no momentum
                        pForce[0] = speed[0] * fabs(speed[0]) * 15;
                        pForce[1] = speed[1] * fabs(speed[1]) * 15 + speed[1] * 40;
                        pForce[2] = speed[2] * fabs(speed[2]) * 6;

                        pForce = DirectionModelToWorld(pForce * info.under) * GetMass() * (coefNPoints / 350);
#if ARROWS
                        AddForce(wCenter + pCenter, -pForce * InvMass());
#endif
                        friction += pForce;
                        torqueFriction += _angMomentum * 0.15f;

                        if (_speed.SquareSize() > Square(8))
                        {
                            crash = 2;
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

                        // const float coefNPoints=1.5f;
                        //  one is ground "pushing" everything out - causing some momentum
                        Vector3 dirOut = Vector3(0, info.dZ, 1).CrossProduct(Vector3(1, info.dX, 0)).Normalized();
                        pForce = dirOut * GetMass() * 40.0f * under * coefNPoints;
                        pCenter = info.pos - wCenter;
                        torque += pCenter.CrossProduct(pForce);
                        // to do: analyze ground reaction force
                        totForce += pForce;

#if ARROWS
                        AddForce(wCenter + pCenter, pForce * under * InvMass());
#endif

                        // friction
                        LandFriction(friction, torqueFriction, torque, brakeFriction, fSpeed, speed, pCenter,
                                     coefNPoints, info.texture);
                        // select roughest texture
                        if (!texture || info.texture && info.texture->Roughness() > texture->Roughness())
                        {
                            texture = info.texture;
                        }
                    }
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
                    const float crashLimit = 0.3f;
                    if (moveUp > crashLimit)
                    {
                        crash += moveUp - crashLimit;
                    }
                    float potentialGain = moveUp * GetMass();
                    float oldKinetic = GetMass() * _speed.SquareSize() * 0.5f; // E=0.5*m*v^2
                    // kinetic to potential conversion is not 100% effective
                    float crashFactor = (moveUp - crashLimit) * 4 + 1.5f;
                    saturateMax(crashFactor, 2.5f);
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

            force += totForce;
            float crashTreshold = 30 * GetMass(); // 3G
            float forceCrash = 0;
            if (totForce.SquareSize() > Square(crashTreshold))
            {
                forceCrash = (totForce.Size() - crashTreshold) * InvMass() * (1.0f / 100);
            }
            crash += forceCrash;
            if (crash > 0.1f)
            {
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
                    _crashVolume = crash * 0.5f;
                    if (_doCrash == CrashWater)
                    {
                        crash *= 0.1f;
                    }
                }
            }
            if (!Type()->_canFloat)
            {
                const float maxFord = 1.8f;
                if (maxUnderWater > maxFord)
                {
                    float dammage = (maxUnderWater - maxFord) * 0.5f;
                    saturateMin(dammage, 0.2f);
                    LocalDammage(nullptr, this, VZero, dammage * deltaT, GetRadius());
                }
            }
        }

        if (_objectContact)
        {
            _turretFrontUntil = Glob.time + 30;
        }
        // apply all forces
        // handle special case: brake is on, speed is very low and total forces are low
        Vector3 frictionedForce = AddFriction(force, -friction);
        Vector3 frictionedTorque = AddFriction(torque, -torqueFriction);
        float staticFriction = 0;
        if (_pilotBrake && _landContact)
        {
            staticFriction = GetMass() * G_CONST * 0.5f; // assume brakes can hold 0.5 G
        }
        ApplyForces(deltaT, force, torque, friction, torqueFriction, staticFriction);

        bool stopCondition = false;
        if (_pilotBrake && _landContact && !_waterContact && !_objectContact)
        {
            // apply static friction
            float maxSpeed = Square(0.7f);
            if (!Driver())
            {
                maxSpeed = Square(1.2f);
            }
            if (_speed.SquareSize() < maxSpeed && _angVelocity.SquareSize() < maxSpeed * 0.3f)
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

        // simulate track drawing
        if (EnableVisualEffects(prec))
        {
            if (DirectionUp().Y() >= 0.3f)
            {
                _track.Update(*this, deltaT, !_landContact);
            }
            if (_landContact)
            {
                float dust = texture ? texture->Dustness() * 0.7f : 0.1f;
                // check texture under
                Vector3 lPos = PositionModelToWorld(_track.LeftPos() + Vector3(+0.5f, 0.1f, 0));
                Vector3 rPos = PositionModelToWorld(_track.RightPos() + Vector3(-0.5f, 0.1f, 0));
                float dSoft = floatMax(dust, 0.003f);

                float lDensity = (speedSize * 0.5f + fabs(_thrustL)) * dSoft;
                float rDensity = (speedSize * 0.5f + fabs(_thrustL)) * dSoft;

                saturateMin(lDensity, 1);
                saturateMin(rDensity, 1);
                float dustColor = dSoft * 8;
                saturate(dustColor, 0, 1);
                Color color = Color(0.51f, 0.46f, 0.33f) * dustColor + Color(0.5f, 0.5f, 0.5f) * (1 - dustColor);
                _leftDust.SetColor(color);
                _rightDust.SetColor(color);
                if (lDensity > 0.2f)
                {
                    _leftDust.Simulate(lPos + _speed * 0.2f, Speed() * 0.25f, lDensity, deltaT);
                }
                if (rDensity > 0.2f)
                {
                    _rightDust.Simulate(rPos + _speed * 0.2f, Speed() * 0.25f, rDensity, deltaT);
                }
                if (_fireDustTimeLeft > 0)
                {
                    float phase = 1 - _fireDustTimeLeft * _invFireDustTimeTotal;
                    const float thold = 0.02f;
                    if (phase > thold)
                    {
                        phase = (1 - phase) * (1 / (1 - thold));
                    }
                    else
                    {
                        phase = phase * (1 / thold);
                    }
                    float density = phase * dSoft * 1.5f;
                    saturateMin(density, 1);
                    Vector3 dustPos = (lPos + rPos) * 0.5f + GetWeaponDirection(0) * 6;
                    Vector3 dustSpeed(GRandGen.RandomValue() - 0.5f, GRandGen.RandomValue() * 0.8f + 0.2f,
                                      GRandGen.RandomValue() - 0.5f);
                    _fireDust.SetColor(color);
                    _fireDust.Simulate(dustPos, dustSpeed, density, deltaT);
                    _fireDustTimeLeft -= deltaT;
                }
            }
            if (_waterContact)
            {
                if (Type()->_canFloat)
                {
                    Vector3 lPos = PositionModelToWorld(Vector3(+0.3f, 0, -3.2f));
                    Vector3 rPos = PositionModelToWorld(Vector3(-0.3f, 0, -3.2f));
                    lPos[1] = GLOB_LAND->GetSeaLevel(); // sea level
                    rPos[1] = GLOB_LAND->GetSeaLevel();
                    float dens = floatMin(speedSize * 0.3f, 0.7f);
                    float densL = fabs(_thrustL) + dens;
                    float densR = fabs(_thrustR) + dens;
                    saturateMin(densL, 1);
                    saturateMin(densR, 1);
                    float coefL = _thrustL * 0.3f;
                    float coefR = _thrustR * 0.3f;
                    saturateMax(coefL, 0.05f);
                    saturateMax(coefR, 0.05f);
                    Vector3 spdL = Speed() * 0.1f - DirectionModelToWorld(Vector3(0, 0, 4) * coefL);
                    Vector3 spdR = Speed() * 0.1f - DirectionModelToWorld(Vector3(0, 0, 4) * coefR);
                    //_leftWater.Simulate(lPos+_speed*0.5,spdL,densL,deltaT);
                    //_rightWater.Simulate(rPos+_speed*0.5,spdR,densR,deltaT);
                    _leftWater.Simulate(lPos, spdL, densL, deltaT);
                    _rightWater.Simulate(rPos, spdR, densR, deltaT);
                }
            }
        }

        SimulateExhaust(deltaT, prec);

        // simulate pilot's head movement
        if (prec <= SimulateCamera)
        {
            _head.Move(deltaT, moveTrans, *this);
        }

        StabilizeTurrets(Transform().Orientation(), moveTrans.Orientation(), oldTurretTrans);

        Move(moveTrans);
        DirectionWorldToModel(_modelSpeed, _speed);
    } // if (!GetStopped() && !isStatic)

    base::Simulate(deltaT, prec);
}

inline Matrix4 Tank::TurretTransform() const
{
    int memory = GetShape()->FindMemoryLevel();
    int sel = Type()->_mainTurret._body.GetSelection(memory);
    if (sel >= 0)
    {
        Matrix4 mat = MIdentity;
        AnimateMatrix(mat, memory, sel);
        return mat;
    }
    return MIdentity;
}

Matrix4 Tank::GunTurretTransform() const
{
    int memory = GetShape()->FindMemoryLevel();
    int sel = Type()->_mainTurret._gun.GetSelection(memory);
    if (sel >= 0)
    {
        Matrix4 mat = MIdentity;
        AnimateMatrix(mat, memory, sel);
        return mat;
    }
    return MIdentity;
}

inline Matrix4 Tank::ObsTransform() const
{
    int memory = GetShape()->FindMemoryLevel();
    int sel = Type()->_comTurret._body.GetSelection(memory);
    if (sel >= 0)
    {
        Matrix4 mat = MIdentity;
        AnimateMatrix(mat, memory, sel);
        return mat;
    }
    return MIdentity;
}

Matrix4 Tank::ObsGunTurretTransform() const
{
    int memory = GetShape()->FindMemoryLevel();
    int sel = Type()->_comTurret._gun.GetSelection(memory);
    if (sel >= 0)
    {
        Matrix4 mat = MIdentity;
        AnimateMatrix(mat, memory, sel);
        return mat;
    }
    return MIdentity;
}

bool Tank::AnimateTexture(int level, float phaseL, float phaseR, float speedL, float speedR)
{
    // animate tracks
    Shape* shape = _shape->Level(level);
    if (!shape)
    {
        return false;
    }

    // use _phaseL to modify u,v mapping of the faces
    // we need some textyre to be able to perform konversion to physical
    Type()->_leftOffset.UVOffset(_shape, 0, -_phaseL, level);
    Type()->_rightOffset.UVOffset(_shape, 0, -_phaseR, level);

    return false; // no alpha change
}

bool Tank::IsAnimated(int level) const
{
    return true;
}
bool Tank::IsAnimatedShadow(int level) const
{
    // a stopped (unsimulated) vehicle cannot change pose
    return !ShadowPoseFrozen();
}

void Tank::AnimateMatrix(Matrix4& mat, int level, int selection) const
{
    const TankType* type = Type();
    _comTurret.AnimateMatrix(type->_comTurret, mat, this, level, selection);
    _mainTurret.AnimateMatrix(type->_mainTurret, mat, this, level, selection);
}

Vector3 Tank::AnimatePoint(int level, int index) const
{
    Shape* shape = _shape->Level(level);
    if (!shape)
    {
        return VZero;
    }
    shape->SaveOriginalPos();

    Vector3 pos = shape->OrigPos(index);

    _comTurret.AnimatePoint(Type()->_comTurret, pos, this, level, index);
    _mainTurret.AnimatePoint(Type()->_mainTurret, pos, this, level, index);

    return pos;
}

void Tank::Animate(int level)
{
    Shape* shape = _shape->Level(level);
    if (!shape)
    {
        return;
    }
    // set gun and turret to correct position
    // calculate animation transformation
    // turret transformation
    const TankType* type = Type();

    _mainTurret.Animate(type->_mainTurret, this, level);
    _comTurret.Animate(type->_comTurret, this, level);

    float value = 0.5f * Glob.time.toFloat();
    value = value - toIntFloor(value);
    Type()->_radarIndicator.SetValue(_shape, level, value);
    Vector3 dir = GetWeaponDirection(0);
    value = atan2(dir.X(), dir.Z());
    Type()->_watch.SetTime(_shape, level, Glob.clock);

    // note: turret indicator is always mounted on turret
    if (Type()->_turretIndicator.GetSelection(level) >= 0)
    {
        Matrix4 rot;
        Type()->_turretIndicator.GetRotationForValue(rot, level, value);
        // combine indicator with turret animation
        Matrix4 turRot = _mainTurret.TurretTransform(type->_mainTurret);
        Type()->_turretIndicator.Transform(_shape, rot, level);
    }

    // assume driver is not bound to any turret
    type->_hatchDriver.Open(MIdentity, _shape, level, 1.0f - _driverHidden);
    int sel = type->_hatchCommander.GetSelection(level);
    if (sel >= 0)
    {
        Matrix4 mat = MIdentity;
        AnimateMatrix(mat, level, sel);
        type->_hatchCommander.Open(mat, _shape, level, 1.0f - _commanderHidden);
    }
    sel = type->_hatchGunner.GetSelection(level);
    if (sel >= 0)
    {
        Matrix4 mat = MIdentity;
        AnimateMatrix(mat, level, sel);
        type->_hatchGunner.Open(mat, _shape, level, 1.0f - _gunnerHidden);
    }

    if (_mGunFireFrames > 0 || Glob.uiTime < _mGunFireTime + 0.05f)
    {
        type->_animFire.Unhide(_shape, level);
        type->_animFire.SetPhase(_shape, level, _mGunFirePhase);
        _mGunFireFrames--;
    }
    else
    {
        type->_animFire.Hide(_shape, level);
    }

    base::Animate(level);

    Vector3 min = shape->MinOrig();
    Vector3 max = shape->MaxOrig();
    Vector3 bCenter = shape->BSphereCenterOrig();
    float bRadius = shape->BSphereRadiusOrig();
    // enlarge sure vehicle will fit it
    Vector3 factor(3, 1.2f, 1.2f);
    float sFactor = 2.5f;
    min = bCenter + factor.Modulate(min - bCenter);
    max = bCenter + factor.Modulate(max - bCenter);
    bRadius *= sFactor;
    shape->SetMinMax(min, max, bCenter, bRadius);

    AnimateTexture(level, _phaseL, _phaseR, _thrustL, _thrustR);

    Matrix4 rotL(MRotationX, _phaseL * 2 * H_PI);
    Matrix4 rotR(MRotationX, _phaseR * 2 * H_PI);
    for (int w = 0; w < type->_wheelsRotL.Size(); w++)
    {
        const AnimationWithCenter& anim = type->_wheelsRotL[w];
        anim.Apply(_shape, rotL, level);
    }
    for (int w = 0; w < type->_wheelsRotR.Size(); w++)
    {
        const AnimationWithCenter& anim = type->_wheelsRotR[w];
        anim.Apply(_shape, rotR, level);
    }
    // calculate offsets for all wheels
    typedef AutoArray<AnimationWithCenter> WheelArray;
    const WheelArray* wheelsLR[2] = {&type->_wheelsUpDownL, &type->_wheelsUpDownR};
    const WheelArray* tracksLR[2] = {&type->_tracksUpDownL, &type->_tracksUpDownR};

    for (int lr = 0; lr < 2; lr++)
    {
        const Matrix4& rot = lr ? rotL : rotR;
        const WheelArray* wheels = wheelsLR[lr];
        const WheelArray* tracks = tracksLR[lr];
        for (int w = 0; w < wheels->Size(); w++)
        {
            if (w >= tracks->Size())
            {
                continue;
            }
            const AnimationWithCenter* animW = &wheels->Get(w);
            const AnimationWithCenter* animT = &tracks->Get(w);
            if (!animT || !animW)
            {
                continue;
            }
            Vector3 centerT = animT->Center(level);
            Vector3 centerW = animW->Center(level);
            Vector3 wCenterT = PositionModelToWorld(centerT);
            // add surface randomization
            Vector3Val surfPos = wCenterT;
            float dX, dZ, bump; // dummy return values
            Texture* tex;
            float lY = GLandscape->BumpySurfaceY(surfPos.X(), surfPos.Z(), dX, dZ, tex, 1, bump);
            float radius = 0.1f;
            float dist = lY - surfPos.Y() + radius;
            saturate(dist, -0.05f, +0.3f);
            Matrix4 offset(MTranslation, Vector3(0, dist, 0));
            //

            Matrix4 trans = (offset * Matrix4(MTranslation, centerW) * rot * Matrix4(MTranslation, -centerW));

            animW->Transform(_shape, trans, level);
            animT->Transform(_shape, offset, level);
        }
    }
}

void Tank::Deanimate(int level)
{
    if (!_shape->Level(level))
    {
        return;
    }
    base::Deanimate(level);
    const TankType* type = Type();

    _mainTurret.Deanimate(type->_mainTurret, _shape, level);
    _comTurret.Deanimate(type->_comTurret, _shape, level);

    type->_hatchDriver.Restore(_shape, level);
    type->_hatchCommander.Restore(_shape, level);
    type->_hatchGunner.Restore(_shape, level);
}

RString Tank::DiagText() const
{
    char buf[512];
    float dx, dz;
    Vector3 pos = Position();

    pos[1] = GLandscape->SurfaceYAboveWater(pos[0], pos[2], &dx, &dz);

    float slope = Direction().Z() * dz + Direction().X() * dx;

    snprintf(buf, sizeof(buf), "  Slope %.0f %%, %s %.2f,%.2f", slope * 100, _pilotBrake ? "B " : "E ", _thrustLWanted,
             _thrustRWanted);

    return base::DiagText() + RString(buf);
}

void Tank::Draw(int level, ClipFlags clipFlags, const FrameBase& frame)
{
    if (level == LOD_INVISIBLE)
    {
        return; // invisible LOD
    }
    base::Draw(level, clipFlags, frame);
}

#if ALPHA_SPLIT
void Tank::DrawAlpha(int level, ClipFlags clipFlags, const FrameBase& frame)
{
    base::Draw(level, clipFlags);
}
#endif

void Tank::Eject(AIUnit* unit)
{
    base::Eject(unit);
}

void Tank::FakePilot(float deltaT) {}

void Tank::JoystickPilot(float deltaT)
{
    auto& input = InputSubsystem::Instance();
    _thrustRWanted = _thrustLWanted = input.GetStickForward();

    float stickLeft = input.GetStickLeft();
    _thrustRWanted -= stickLeft;
    _thrustLWanted += stickLeft;

    Limit(_thrustRWanted, -1, 1);
    Limit(_thrustLWanted, -1, 1);

    if (fabs(_thrustLWanted) + fabs(_thrustRWanted) < 0.1f && fabs(_modelSpeed.Z()) < 5.0f)
    {
        _pilotBrake = true;
    }
    else
    {
        _pilotBrake = ModelSpeed()[2] * (_thrustLWanted + _thrustRWanted) < -4;
        if (fabs(_thrustLWanted) + fabs(_thrustRWanted) > 0.1f)
        {
            CancelStop();
            EngineOn();
        }
    }
}

void Tank::SuspendedPilot(AIUnit* unit, float deltaT) {}

void Tank::KeyboardPilot(AIUnit* unit, float deltaT)
{
    auto& input = InputSubsystem::Instance();
    if (input.IsJoystickActive())
    {
        JoystickPilot(deltaT);
        return;
    }

    constexpr InputContext ctx = InputContext::TankDriver;
    float forward = (input.GetAction(ctx, UAMoveForward) - input.GetAction(ctx, UAMoveBack)) * 0.75f;
    forward += input.GetAction(ctx, UAMoveFastForward);
    forward += input.GetAction(ctx, UAMoveSlowForward) * 0.33f;

    if (ModelSpeed()[2] > 2 && forward < -0.5)
    {
        _backwardUsedAsBrake = true;
    }
    else if (forward >= 0)
    {
        _backwardUsedAsBrake = false;
    }

    if (ModelSpeed()[2] < -2 && forward > +0.5)
    {
        _forwardUsedAsBrake = true;
    }
    else if (forward <= 0)
    {
        _forwardUsedAsBrake = false;
    }

    bool brake = _backwardUsedAsBrake || _forwardUsedAsBrake;
    if (brake)
    {
        forward = -ModelSpeed()[2] * 10;
        saturate(forward, -1, 1);
        if (_speed.SquareSize() < 0.2f)
        {
            _parkingBrake = true;
            _backwardUsedAsBrake = _forwardUsedAsBrake = false;
        }
    }
    else
    {
        if (fabs(forward) > 0.25f)
        {
            _parkingBrake = false;
        }
    }

    _thrustRWanted = _thrustLWanted = forward;

    bool internalCamera = IsGunner(GWorld->GetCameraType());
    bool mouseControl = internalCamera && input.IsMouseTurnActive() && !input.IsLookAroundEnabled();
    bool fullTurn = fabs(_thrustLWanted + _thrustRWanted) < 0.01f;

    float estT = mouseControl ? 0.75f : 0.25f;

    if (!fullTurn)
    {
        estT *= 2;
    }

    // estimate heading
    Matrix3Val orientation = Orientation();
    Matrix3Val derOrientation = _angVelocity.Tilda() * orientation;
    Matrix3Val estOrientation = orientation + derOrientation * estT;
    Vector3Val estDirection = estOrientation.Direction();

    float curHeading = atan2(Direction()[0], Direction()[2]);
    float estHeading = atan2(estDirection[0], estDirection[2]);

    float turnWanted = 0;

    if (mouseControl)
    {
        // last input from mouse - use mouse controls
        // _mouseTurnWanted is difference from current heading

        Vector3 relDir(VMultiply, DirWorldToModel(), _mouseDirWanted);
        float mTurnWanted = atan2(relDir.X(), relDir.Z());

        turnWanted = AngleDifference(curHeading + mTurnWanted, estHeading);
    }
    else
    {
        // note: keys give wanted turning speed, not turning acceleration
        float turnKey = input.GetAction(ctx, UATurnRight) - input.GetAction(ctx, UATurnLeft);
        // when moving fast, we want to turn slowly

        float slow = floatMax(0, 1 - fabs(ModelSpeed().Z()) * (1.0f / 20));

        float factor = 1 - slow * 0.5f;
        turnWanted = AngleDifference(curHeading + turnKey * factor, estHeading);
    }

    // special case - tank moving fast and players wants to brake
    // in such situation tank is usually out of control
    // we need to brake first and turn later
    float bFactor = ModelSpeed()[2] * forward;
    // GlobalShowMessage(100,"bFactor %.2f",bFactor);
    if (bFactor < -7)
    {
        saturate(turnWanted, -0.05f, 0.05f);
    }

    _thrustLWanted -= turnWanted * 8;
    _thrustRWanted += turnWanted * 8;

    // if we are goind forward, no track can go backward
    if (_thrustLWanted + _thrustRWanted > 0.01f)
    {
        saturateMax(_thrustLWanted, 0);
        saturateMax(_thrustRWanted, 0);
    }
    else if (_thrustLWanted + _thrustRWanted < -0.01f)
    {
        saturateMin(_thrustLWanted, 0);
        saturateMin(_thrustRWanted, 0);
    }
    Limit(_thrustLWanted, -1, 1);
    Limit(_thrustRWanted, -1, 1);

    if ((fabs(_thrustLWanted) + fabs(_thrustRWanted) < 0.1f || brake || _parkingBrake) && fabs(_modelSpeed.Z()) < 5.0f)
    {
        _pilotBrake = true;
    }
    else
    {
        _pilotBrake = ModelSpeed()[2] * (_thrustLWanted + _thrustRWanted) < -4;
        if (fabs(_thrustLWanted) + fabs(_thrustRWanted) > 0.1f)
        {
            CancelStop();
            EngineOn();
        }
    }
}

#define DIAG_SPEED 0

void TankWithAI::AIPilot(AIUnit* unit, float deltaT)
{
    PoseidonAssert(unit);
    if (!unit)
    {
        return;
    }
    PoseidonAssert(unit->GetSubgroup());
    if (!unit->GetSubgroup())
    {
        return;
    }
    AIUnit* leader = unit->GetSubgroup()->Leader();
    bool isLeaderVehicle = leader && leader->GetVehicleIn() == this;

    Vector3Val speed = ModelSpeed();

    float headChange = 0;
    float speedWanted = 0;
    float turnPredict = 0;

    if (unit->GetState() == AIUnit::Stopping)
    {
        // special handling of stop state
        if (fabs(speed[2]) < 1)
        {
            UpdateStopTimeout();
            unit->SendAnswer(AI::StepCompleted);
        }
        speedWanted = 0;
        headChange = 0;
    }
    else if (unit->GetState() == AIUnit::Stopped)
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

    // estimate inertial orientation change
    Matrix3Val orientation = Orientation();
    Matrix3Val derOrientation = _angVelocity.Tilda() * orientation;
    Matrix3Val estOrientation = orientation + derOrientation;
    Vector3Val estDirection = estOrientation.Direction();
    float estHeading = atan2(estDirection[0], estDirection[2]);

    headChange = AngleDifference(wantedHeading, estHeading);

    {
        float maxSpeed = GetType()->GetMaxSpeedMs();
        float limitSpeed = Interpolativ(fabs(turnPredict), H_PI / 8, H_PI / 4, maxSpeed, 3);
        float limitSpeedC = Interpolativ(fabs(headChange), H_PI / 8, H_PI / 2, maxSpeed, 0);
#if DIAG_SPEED
        if (this == GWorld->CameraOn())
        {
            LOG_DEBUG(Physics, "Turn limit {:.1f} ({:.3f}, turn {:.3f})", limitSpeed, headChange, turnPredict);
        }
#endif

        saturate(speedWanted, -limitSpeed, +limitSpeed);
        saturate(speedWanted, -limitSpeedC, +limitSpeedC);
    }

    if (fabs(speedWanted) > 0.2f)
    {
        EngineOn();
    }
    if (fabs(headChange) > 0.2f)
    {
        EngineOn();
    }

    Vector3 relAccel = DirectionWorldToModel(_acceleration);
    float changeAccel = (speedWanted - speed.Z()) * (1 / 0.5f) - relAccel.Z();
    // some thrust is needed to keep speed
    float isSlow = 1 - fabs(speed.Z()) * (1.0f / 17);
    saturate(isSlow, 0.2f, 1);

    float isLevel = 1 - fabs(Direction()[1] * (1.0f / 0.6f));
    saturate(isLevel, 0.2f, 1);
    saturateMax(isSlow, isLevel); // change thrust slowly on steep surfaces
    changeAccel *= isSlow;
    float thrustOld = (_thrustL + _thrustR) * 0.5f;
    float thrust = thrustOld + changeAccel * 0.33f;
    Limit(thrust, -1, 1);

    const float rotCoef = 5;
    _thrustLWanted = thrust - headChange * rotCoef;
    _thrustRWanted = thrust + headChange * rotCoef;
    Limit(_thrustLWanted, -1, 1);
    Limit(_thrustRWanted, -1, 1);

#if DIAG_SPEED
    if (this == GWorld->CameraOn())
    {
        LOG_DEBUG(Physics, "Thrust {:.1f} L {:.1f} R {:.1f}", thrust, _thrustLWanted, _thrustRWanted);
    }
#endif

    if (fabs(headChange) < 0.05f && fabs(speedWanted) < 0.5f)
    {
        if (fabs(speed[2]) < 0.5f)
        {
            _thrustLWanted = _thrustRWanted = 0;
        }
        _pilotBrake = true;
    }
    else if (fabs(speed[2]) < 5 && fabs(speedWanted) < 0.5f && fabs(headChange) < 0.5f)
    {
        _pilotBrake = true;
    }
    else
    {
        _pilotBrake = false;
    }
}

void TankWithAI::AIGunner(AIUnit* unit, float deltaT)
{
    if (_isDead || _isUpsideDown)
    {
        return;
    }
    base::AIGunner(unit, deltaT);
}

const float MissileUpAngle = 0;

float Tank::GetAimed(int weapon, Target* target) const
{
    return base::GetAimed(weapon, target);
}

float TankWithAI::FireInRange(int weapon, float& timeToAim, const Target& target) const
{
    timeToAim = 0;
    Vector3 relDir = PositionWorldToModel(target.position);
    return FireAngleInRange(weapon, relDir);
}

float TankWithAI::FireAngleInRange(int weapon, Vector3Par rel) const
{
    // all tanks have turret that can be fully turned around (in Y axis)
    float dist2 = rel.SquareSizeXZ();
    float y2 = Square(rel.Y());
    // x>0: atan(x)<x
    // atan(y/sqrt(dist2)) < type->_maxGunElev
    // y/sqrt(dist2) < type->_maxGunElev
    // y < type->_maxGunElev * sqrt(dist2)
    // y2 < type->_maxGunElev^2 * dist2
    // we need to have correct signs
    const TurretType& type = Type()->_mainTurret;
    PoseidonAssert(type._maxElev >= 0);
    PoseidonAssert(type._minElev <= 0);
    float ret = 0;
    if (rel.Y() >= 0)
    {
        // fire up
        if (y2 > dist2 * Square(type._maxElev))
        {
            return 0;
        }
        ret = rel.Y() * InvSqrt(dist2) * (1 / type._maxElev);
    }
    else
    {
        // fire down
        if (y2 > dist2 * Square(type._minElev))
        {
            return 0;
        }
        ret = rel.Y() * InvSqrt(dist2) * (1 / type._minElev);
    }
    return floatMin(1, 6 - ret * 6);
}

void TankWithAI::Simulate(float deltaT, SimulationImportance prec)
{
    // if dammaged or upside down, tank is dead
    _isUpsideDown = DirectionUp().Y() < 0.3f;
    _isDead = IsDammageDestroyed();

    if (!SimulateUnits(deltaT))
    {
        EngineOff();
        _pilotBrake = true;
    }
    base::Simulate(deltaT, prec);
}

} // namespace Poseidon
