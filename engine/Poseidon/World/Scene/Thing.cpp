#include <Poseidon/Core/Application.hpp>

#include <Poseidon/World/Scene/Thing.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/Entities/Weapons/Shots.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/Core/Global.hpp>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/BoolArray.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

#include <Random/randomGen.hpp>
#include <Poseidon/World/World.hpp>

namespace Poseidon
{
using namespace Foundation;

#if _DEBUG
#define ARROWS 0
#endif

ThingType::ThingType(const ParamEntry* param) : base(param)
{
    _scopeLevel = 1;
}

ThingType::~ThingType() = default;

void ThingType::Load(const ParamEntry& par)
{
    base::Load(par);
    _submerged = par >> "submerged";
    _submergeSpeed = par >> "submergeSpeed";
    _timeToLive = par >> "timetolive";
    _disappearAtContact = par >> "disappearAtContact";
}

DEFINE_CASTING(Thing)

Thing::Thing(VehicleType* name) : base(name), _doCrash(CrashNone)
{
    _submerged = Type()->_submerged;
    _isCloudlet = false;

    _isStopped = true;
    _objectContact = true;
    _landContact = false;
    SetSimulationPrecision(1.0f / 15);
    _destrType = GetType()->GetDestructType();
}

Vector3 Thing::Friction(Vector3Par speed)
{
    Vector3 friction;
    friction.Init();
    friction[0] = speed[0] * fabs(speed[0]) * 25 + speed[0] * 20 + fSign(speed[0]) * 30;
    friction[1] = speed[1] * fabs(speed[1]) * 25 + speed[1] * 20 + fSign(speed[1]) * 20;
    friction[2] = speed[2] * fabs(speed[2]) * 5 + speed[2] * 20 + fSign(speed[2]) * 10;
    return friction * GetMass() * (1.0 / 1700);
}

void Thing::Simulate(float deltaT, SimulationImportance prec)
{
    _submerged += Type()->_submergeSpeed * deltaT;

    _isUpsideDown = DirectionUp().Y() < 0.3;
    _isDead = IsDammageDestroyed();

    if (_isDead)
    {
        SmokeSourceVehicle* smoke = dyn_cast<SmokeSourceVehicle>(GetSmoke());
        if (smoke)
        {
            float explosionDelay = GRandGen.Gauss(0.2f, 0.5f, 1.5f);
            Time explosionTime = Glob.time + explosionDelay;
            smoke->Explode(explosionTime);
        }
        NeverDestroy();
    }

    base::Simulate(deltaT, prec);

#define SIM_STEP_LIMIT 1

#if SIM_STEP_LIMIT
    float rest = deltaT;
    float simStep = 0.05;
    while (rest > 0)
#endif
    {
#if SIM_STEP_LIMIT
        float deltaT = floatMin(rest, simStep);
        rest -= simStep;
#endif

        Vector3Val speed = ModelSpeed();

        // calculate all forces, frictions and torques
        Vector3 force(VZero), friction(VZero);
        Vector3 torque(VZero), torqueFriction(VZero);

        Vector3 pForce(VZero);  // partial force
        Vector3 pCenter(VZero); // partial force application point

        // simulate left/right engine

        if ((!_landContact || Type()->_submergeSpeed > 0) && !_objectContact)
        {
            // it is not touching anything - we need simulation
            // or it is sumberging
            IsMoved();
        }
        {
            // handle impulse
            float impulse2 = _impulseForce.SquareSize();
            if (impulse2 > Square(GetMass() * 0.001))
            {
                IsMoved();
            }
            if (impulse2 > Square(GetMass() * 3))
            {
                // too strong impulse - dammage
                float contact = sqrt(impulse2) / (GetMass() * 3);
                // contact>0
                saturateMin(contact, 5);
                if (contact > 0.1)
                {
                    float radius = GetRadius();
                    LocalDammage(nullptr, this, VZero, contact * 0.1, radius * 0.3);
                }
            }
        }

        if (GetStopped())
        {
            // reset impulse - avoid cummulation
            _impulseForce = VZero;
            _impulseTorque = VZero;
        }

        if (!_isStopped)
        {
            Vector3 wCenter(VFastTransform, ModelToWorld(), GetCenterOfMass());

            // apply gravity
            pForce = Vector3(0, -G_CONST * GetMass(), 0);
            force += pForce;

#if ARROWS
            AddForce(wCenter, pForce * InvMass(), Color(1, 1, 0, 0.1));
#endif

            // angular velocity causes also some angular friction
            // this should be simulated as torque
            if (_landContact || _objectContact)
            {
                torqueFriction = _angMomentum * 0.2;
            }
            else
            {
                torqueFriction = _angMomentum * 0.05;
            }

            // calculate new position
            Matrix4 movePos;
            ApplySpeed(movePos, deltaT);

            Frame moveTrans;
            moveTrans.SetTransform(movePos);

            // body air friction
            DirectionModelToWorld(friction, Friction(speed));
#if ARROWS
            AddForce(wCenter, friction * InvMass(), Color(0, 1, 1));
#endif

            wCenter.SetFastTransform(moveTrans.ModelToWorld(), GetCenterOfMass());

            AUTO_STATIC_ARRAY(ContactPoint, contacts, 128);

            if (deltaT > 0)
            {
                float above = 0.05 - floatMax(_submerged, 0);

                ScanContactPoints(contacts, moveTrans, prec, above);
            }

            if (deltaT > 0 && contacts.Size() > 0)
            {
                AUTO_STATIC_ARRAY(FrictionPoint, frictions, 128);

                float crash = 0;
                float maxColSpeed2 = 0;

                Vector3 offset;
                ConvertContactsToFrictions(contacts, frictions, moveTrans, offset, force, torque, crash, maxColSpeed2);

#if 1
                if (offset.SquareSize() >= Square(0.01))
                {
                    // move object out immediately
                    Matrix4 transform = moveTrans.Transform();
                    Vector3 newPos = transform.Position();
                    newPos += offset;
                    transform.SetPosition(newPos);
                    moveTrans.SetTransform(transform);
                    // moving up gains potential energy, so kinetic energy must drop
                    const float crashLimit = 0.3;
                    float moveOut = offset.Size();
                    if (moveOut > crashLimit)
                    {
                        crash += moveOut - crashLimit;
                    }
                    // limit speed to avoid getting deeper

                    Vector3 offsetDir = offset.Normalized();
                    float speedOut = offsetDir * _speed;
                    const float minSpeedOut = -0.5;
                    if (speedOut < minSpeedOut)
                    {
                        float addSpeedOut = minSpeedOut - speedOut;
                        _speed += addSpeedOut * offsetDir;
                    }
                }
#endif
                if (crash > 0.1)
                {
                    float speedCrash = maxColSpeed2 * Square(1.0 / 7);
                    if (speedCrash < 0.1)
                    {
                        speedCrash = 0;
                    }
                    if (Glob.time > _disableDammageUntil)
                    {
                        // impact speed too high
                        _doCrash = CrashLand;
                        if (_objectContact)
                        {
                            _doCrash = CrashObject;
                        }
                        if (_waterContact)
                        {
                            _doCrash = CrashWater;
                        }
                        _crashVolume = crash * 0.5;
                        saturateMin(crash, speedCrash);
                        CrashDammage(crash * 4); // 1g -> 5 mm dammage
                    }
                }
                // apply all forces
                ApplyForcesAndFriction(deltaT, force, torque, frictions.Data(), frictions.Size());
            }
            else
            {
                // apply all forces
                ApplyForcesAndFriction(deltaT, force, torque, nullptr, 0);
            }

            bool stopCondition = false;
            if ((_landContact || _objectContact) && !_waterContact)
            {
                // apply static friction
                float maxSpeed = Square(1.0);
                if (_speed.SquareSize() < maxSpeed && _angVelocity.SquareSize() < maxSpeed)
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

            // a cloudlet is not in the landscape, so set its transform directly
            if (_isCloudlet)
            {
                SetTransform(moveTrans);
            }
            else
            {
                Move(moveTrans);
            }
            DirectionWorldToModel(_modelSpeed, _speed);

            // guard against simulation blowing up: delete the object
            if (!Transform().IsFinite())
            {
                LOG_DEBUG(Graphics, "Patch: infinite Thing {}", (const char*)GetDebugName());
                SetOrientation(M3Identity);
            }
            if (_speed.SquareSize() > 1e6)
            {
                LOG_DEBUG(Graphics, "Patch: high speed Thing {}", (const char*)GetDebugName());
                _speed.Normalize();
                _speed *= 0.9e3;
            }
            if (_angMomentum.SquareSize() > 1e6)
            {
                LOG_DEBUG(Graphics, "Patch: high momentum Thing {}, {},{},{}", (const char*)GetDebugName(),
                          _angMomentum[0], _angMomentum[1], _angMomentum[2]);
                _angMomentum.Normalize();
                _angMomentum *= 0.9e3;
            }
        }
    }

#undef SIM_STEP_LIMIT

    if (_landContact && _submerged > 0 && _submerged > GetRadius() * 2)
    {
        SetDelete();
    }
}

void Thing::CrashDammage(float ammount, const Vector3& pos)
{
    ammount *= GetType()->GetInvArmor();
    LocalDammage(nullptr, this, pos, ammount, GetRadius());
}

void Thing::Sound(bool inside, float deltaT)
{
    if (_doCrash != CrashNone && Glob.time > _timeCrash + 3.0)
    {
        _timeCrash = Glob.time;
        const SoundPars* pars = nullptr;
        switch (_doCrash)
        {
            case CrashObject:
                pars = &GetType()->GetCrashSound();
                break;
            case CrashLand:
                pars = &GetType()->GetLandCrashSound();
                break;
            case CrashWater:
                pars = &GetType()->GetWaterCrashSound();
                break;
        }
        if (pars)
        {
            float volume = pars->vol * _crashVolume;
            float freq = pars->freq;
            IWave* sound = GSoundScene->OpenAndPlayOnce(pars->name, Position(), Speed(), volume, freq);
            if (sound)
            {
                GSoundScene->SimulateSpeedOfSound(sound);
                GSoundScene->AddSound(sound);
            }
        }
    }
    _doCrash = CrashNone;
}

void Thing::UnloadSound() {}

void Thing::DrawDiags()
{
    base::DrawDiags();
}

Matrix4 Thing::InsideCamera(CameraType camType) const
{
    return base::InsideCamera(camType);
}

int Thing::InsideLOD(CameraType camType) const
{
    return base::InsideLOD(camType);
}

// no get-in to buildings

bool Thing::IsAnimated(int level) const
{
    return base::IsAnimated(level);
}
bool Thing::IsAnimatedShadow(int level) const
{
    return base::IsAnimatedShadow(level);
}
void Thing::Animate(int level)
{
    base::Animate(level);
}
void Thing::Deanimate(int level)
{
    base::Deanimate(level);
}

DEFINE_CASTING(ThingEffect)

ThingEffect::ThingEffect(VehicleType* name) : base(name)
{
    _objectContact = false;
}

#if 1

DEFINE_FAST_ALLOCATOR(ThingEffectLight)

DEFINE_CASTING(ThingEffectLight)

ThingEffectLight::ThingEffectLight(ThingType* name) : base(name->GetShape(), name, -1), _doCrash(CrashNone)
{
    _submerged = Type()->_submerged;
    _timeToLive = Type()->_timeToLive;

    _isCloudlet = false;

    _objectContact = false;
    _landContact = true;
    SetSimulationPrecision(0.2); // no real limit
}

Vector3 ThingEffectLight::Friction(Vector3Par speed)
{
    Vector3 friction;
    friction.Init();
    friction[0] = speed[0] * fabs(speed[0]) * 25 + speed[0] * 20 + fSign(speed[0]) * 30;
    friction[1] = speed[1] * fabs(speed[1]) * 25 + speed[1] * 20 + fSign(speed[1]) * 20;
    friction[2] = speed[2] * fabs(speed[2]) * 5 + speed[2] * 20 + fSign(speed[2]) * 10;
    return friction * GetMass() * (1.0 / 1700);
}

void ThingEffectLight::Simulate(float deltaT, SimulationImportance prec)
{
    _submerged += Type()->_submergeSpeed * deltaT;
    _timeToLive -= deltaT;

    base::Simulate(deltaT, prec);

#define SIM_STEP_LIMIT 1

#if SIM_STEP_LIMIT
    float rest = deltaT;
    float simStep = 0.05;
    while (rest > 0)
#endif
    {
#if SIM_STEP_LIMIT
        float deltaT = floatMin(rest, simStep);
        rest -= simStep;
#endif

        Vector3Val speed = ModelSpeed();

        // calculate all forces, frictions and torques
        Vector3 force(VZero), friction(VZero);
        Vector3 torque(VZero), torqueFriction(VZero);

        Vector3 pForce(VZero);  // partial force
        Vector3 pCenter(VZero); // partial force application point

        // simulate left/right engine
        Vector3 wCenter(VFastTransform, ModelToWorld(), GetCenterOfMass());

        // apply gravity
        pForce = Vector3(0, -G_CONST * GetMass(), 0);
        force += pForce;

#if ARROWS
        AddForce(wCenter, pForce * InvMass(), Color(1, 1, 0, 0.1));
#endif

        // angular velocity causes also some angular friction
        // this should be simulated as torque
        if (_landContact || _objectContact)
        {
            torqueFriction = _angMomentum * 0.2;
        }
        else
        {
            torqueFriction = _angMomentum * 0.05;
        }

        // calculate new position
        Matrix4 movePos;
        ApplySpeed(movePos, deltaT);

        Frame moveTrans;
        moveTrans.SetTransform(movePos);

        // body air friction
        DirectionModelToWorld(friction, Friction(speed));
#if ARROWS
        AddForce(wCenter, friction * InvMass(), Color(0, 1, 1));
#endif

        wCenter.SetFastTransform(moveTrans.ModelToWorld(), GetCenterOfMass());

        AUTO_STATIC_ARRAY(ContactPoint, contacts, 128);

        if (deltaT > 0)
        {
            float above = 0.05 - floatMax(_submerged, 0);

            ScanContactPoints(contacts, moveTrans, prec, above, true);
        }

        if (deltaT > 0 && contacts.Size() > 0)
        {
            AUTO_STATIC_ARRAY(FrictionPoint, frictions, 128);

            float crash = 0;
            float maxColSpeed2 = 0;

            Vector3 offset;
            ConvertContactsToFrictions(contacts, frictions, moveTrans, offset, force, torque, crash, maxColSpeed2);

#if 1
            if (offset.SquareSize() >= Square(0.01))
            {
                // move object out immediately
                Matrix4 transform = moveTrans.Transform();
                Vector3 newPos = transform.Position();
                newPos += offset;
                transform.SetPosition(newPos);
                moveTrans.SetTransform(transform);
                // moving up gains potential energy, so kinetic energy must drop
                const float crashLimit = 0.3;
                float moveOut = offset.Size();
                if (moveOut > crashLimit)
                {
                    crash += moveOut - crashLimit;
                }
                // limit speed to avoid getting deeper

                Vector3 offsetDir = offset.Normalized();
                float speedOut = offsetDir * _speed;
                const float minSpeedOut = -0.5;
                if (speedOut < minSpeedOut)
                {
                    float addSpeedOut = minSpeedOut - speedOut;
                    _speed += addSpeedOut * offsetDir;
                }
            }
#endif
            if (crash > 0.1)
            {
                float speedCrash = maxColSpeed2 * Square(1.0 / 7);
                if (speedCrash < 0.1)
                {
                    speedCrash = 0;
                }
                if (Glob.time > _disableDammageUntil)
                {
                    // impact speed too high
                    _doCrash = CrashLand;
                    if (_objectContact)
                    {
                        _doCrash = CrashObject;
                    }
                    if (_waterContact)
                    {
                        _doCrash = CrashWater;
                    }
                    _crashVolume = crash * 0.5;
                }
            }
            // apply all forces
            ApplyForcesAndFriction(deltaT, force, torque, frictions.Data(), frictions.Size());
        }
        else
        {
            // apply all forces
            ApplyForcesAndFriction(deltaT, force, torque, nullptr, 0);
        }

        // a cloudlet is not in the landscape, so set its transform directly
        if (_isCloudlet)
        {
            SetTransform(moveTrans);
        }
        else
        {
            Move(moveTrans);
        }
        DirectionWorldToModel(_modelSpeed, _speed);

        if (_speed.SquareSize() > 1e6 || _angMomentum.SquareSize() > 1e6)
        {
            LOG_DEBUG(Graphics, "Patch: high speed object {}", (const char*)GetDebugName());
            SetDelete();
#if SIM_STEP_LIMIT
            break;
#endif
        }
    }

    if (_landContact && _submerged > 0 && _submerged > GetRadius() * 2 || _timeToLive < 0 ||
        Type()->_disappearAtContact && _landContact)
    {
        SetDelete();
    }

    // guard against simulation blowing up: delete the object
    if (!Transform().IsFinite())
    {
        LOG_DEBUG(Graphics, "Patch: infinite object {}", (const char*)GetDebugName());
        SetDelete();
    }
}

void ThingEffectLight::Sound(bool inside, float deltaT)
{
    if (_doCrash != CrashNone && Glob.time > _timeCrash + 3.0)
    {
        _timeCrash = Glob.time;
        const SoundPars* pars = nullptr;
        switch (_doCrash)
        {
            case CrashObject:
                pars = &Type()->GetCrashSound();
                break;
            case CrashLand:
                pars = &Type()->GetLandCrashSound();
                break;
            case CrashWater:
                pars = &Type()->GetWaterCrashSound();
                break;
        }
        if (pars)
        {
            float volume = pars->vol * _crashVolume;
            float freq = pars->freq;
            IWave* sound = GSoundScene->OpenAndPlayOnce(pars->name, Position(), Speed(), volume, freq);
            if (sound)
            {
                GSoundScene->SimulateSpeedOfSound(sound);
                GSoundScene->AddSound(sound);
            }
        }
    }
    _doCrash = CrashNone;
}

void ThingEffectLight::UnloadSound() {}

// no get-in to buildings

bool ThingEffectLight::IsAnimated(int level) const
{
    return false;
}
bool ThingEffectLight::IsAnimatedShadow(int level) const
{
    return false;
}
void ThingEffectLight::Animate(int level) {}
void ThingEffectLight::Deanimate(int level) {}

#endif

struct ThingEffectItem
{
    RStringB _type;
    float _probab;

    ThingEffectItem(RStringB type, float probab) : _type(type), _probab(probab) {}
    ThingEffectItem() : _probab(-1) {}
};

static const ThingEffectItem* GetTEGroundList()
{
    static const ThingEffectItem TEGroundList[] = {ThingEffectItem("FxExploGround1", 0.5),
                                                   ThingEffectItem("FxExploGround2", 10), // any probability rest
                                                   ThingEffectItem()};
    return TEGroundList;
}

static const ThingEffectItem* GetTEArmorList()
{
    static const ThingEffectItem TEArmorList[] = {ThingEffectItem("FxExploArmor1", 0.2),
                                                  ThingEffectItem("FxExploArmor2", 0.2), // any probability rest
                                                  ThingEffectItem("FxExploArmor3", 0.2),
                                                  ThingEffectItem("FxExploArmor4", 0.2), // any probability rest
                                                  ThingEffectItem("FxExploGround1", 0.1),
                                                  ThingEffectItem("FxExploGround2", 10), // any probability rest
                                                  ThingEffectItem()};
    return TEArmorList;
}

static const ThingEffectItem* GetTECartridgeList()
{
    static const ThingEffectItem TECartridgeList[] = {ThingEffectItem("FxCartridge", 10), // any probability rest
                                                      ThingEffectItem()};
    return TECartridgeList;
}

static const ThingEffectItem* SelectThingEffect(const ThingEffectItem* list, float value)
{
    // value 0..1
    while (list->_probab >= 0)
    {
        value -= list->_probab;
        if (value < 0)
        {
            return list;
        }
        list++;
    }
    Fail("No corresponding effect");
    return nullptr;
}

Entity* CreateThingEffect(ThingEffectKind kind,          // kind
                          Matrix4Val pos, Vector3Val vel // position and velocity
)
{
    const ThingEffectItem* list = nullptr;
    switch (kind)
    {
        default:
            Fail("Bad effect kind");
            // fall through
        case TEGround:
            list = GetTEGroundList();
            break;
        case TEArmor:
            list = GetTEArmorList();
            break;
        case TECartridge:
            list = GetTECartridgeList();
            break;
    }
    const ThingEffectItem* item = SelectThingEffect(list, GRandGen.RandomValue());
    if (!item)
    {
        return nullptr;
    }
#if 0
			Ref<EntityAI> veh = NewVehicle(item->_type,RString());
			if (!veh) return veh;
			veh->SetTransform(pos);
			veh->SetSpeed(vel);
			veh->IsMoved();
			Thing *thing = dyn_cast<Thing,EntityAI>(veh);
#else
    Ref<Entity> veh = NewNonAIVehicle(item->_type, RString());
    if (!veh)
    {
        return veh;
    }
    veh->SetTransform(pos);
    veh->SetSpeed(vel);
    ThingEffectLight* thing = dyn_cast<ThingEffectLight, Entity>(veh);
#endif
    if (thing)
    {
        thing->SetCloudlet(true);
        GWorld->AddCloudlet(thing);
    }
    else if (veh)
    {
        GWorld->AddAnimal(veh);
    }
    return veh;
}

Entity* CreateThing(VehicleType* type,             // kind
                    Matrix4Val pos, Vector3Val vel // position and velocity
)
{
    Ref<Entity> veh = NewNonAIVehicle(type->GetName(), RString());
    if (!veh)
    {
        return veh;
    }
    veh->SetTransform(pos);
    veh->SetSpeed(vel);
    ThingEffectLight* thing = dyn_cast<ThingEffectLight, Entity>(veh);
    if (thing)
    {
        thing->SetCloudlet(true);
        GWorld->AddCloudlet(thing);
    }
    else if (veh)
    {
        GWorld->AddAnimal(veh);
    }
    return veh;
}

} // namespace Poseidon
