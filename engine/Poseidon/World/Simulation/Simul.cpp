#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/World/Entities/Vehicles/Vehicle.hpp>
#include <Poseidon/World/Entities/Infantry/PilotHead.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/World/Simulation/Animation/Animation.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Random/randomGen.hpp>
#include <Poseidon/Game/Commands/GameStateExt.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Graphics/Rendering/Draw/SpecLods.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Graphics/Core/TLVertex.hpp>
#include <Poseidon/Dev/Diag/DiagModes.hpp>

#include <Poseidon/Graphics/Core/Engine.hpp>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/BoolArray.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
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
#include <Poseidon/Foundation/platform.hpp>

#if _ENABLE_CHEATS
#define ARROWS 1
#else
#define ARROWS 0
#endif

#include <Poseidon/Foundation/Math/Statistics.hpp>

using namespace Poseidon;
bool IsDedicatedServer();
bool IsUpdateTransport(NetworkMessageType type);

namespace Poseidon
{
void Entity::Simulate(float deltaT, SimulationImportance prec)
{
    const EntityType* type = GetNonAIType();

    for (int i = 0; i < _animateTexturesTimes.Size(); i++)
    {
        float& phase = _animateTexturesTimes[i];
        phase += deltaT * type->_animateTextures[i].animSpeed;
        while (phase >= 1.0f)
        {
            phase -= 1.0f;
        }
    }
}

void Entity::SimulateOptimized(float deltaT, SimulationImportance prec)
{
    float step = SimulationPrecision();
    _simulationSkipped += deltaT;
    if (_simulationSkipped >= step)
    {
        Simulate(step, prec);
        ApplyRemoteState(step);
        _simulationSkipped -= step;
    }
}
void Entity::SimulateRest(float deltaT, SimulationImportance prec)
{
    _simulationSkipped += deltaT;
    if (_simulationSkipped > 1e-4)
    {
        float step = _simulationSkipped;
        Simulate(step, prec);
        ApplyRemoteState(step);
        _simulationSkipped = 0;
    }
}

void Entity::ApplyRemoteState(float deltaT)
{
    // Local units are authoritative; nothing to correct. Single-player keeps
    // every unit local, so this whole path is inert outside MP clients.
    if (IsLocal() || !_remoteInterp.HasTarget())
    {
        return;
    }
    // Once prediction is frozen the unit must hold still until the next update,
    // not keep chasing an extrapolated ghost.
    if (CheckPredictionFrozen())
    {
        _remoteInterp.Clear();
        return;
    }

    RemoteInterpParams params;
    RemoteInterpResult res = _remoteInterp.Step(deltaT, Position(), _speed, Orientation(), params);
    if (!res.active)
    {
        return;
    }

    Matrix4 trans;
    trans.SetOrientation(res.orientation);
    trans.SetPosition(res.position);
    Move(trans);
    SetSpeed(res.speed);
}

static Vector3 AddFriction(Vector3Val accel, Vector3Val friction)
{
    Vector3 res = accel + friction;
    if (res[0] * accel[0] < 0)
    {
        res[0] = 0;
    }
    if (res[1] * accel[1] < 0)
    {
        res[1] = 0;
    }
    if (res[2] * accel[2] < 0)
    {
        res[2] = 0;
    }
    return res;
}

FFEffects::FFEffects()
{
    engineFreq = engineMag = 0;
    stiffnessX = 0.3f, stiffnessY = 0.3f; // autocentering forces
}

void Entity::PerformFF(FFEffects& effects)
{
    effects = FFEffects();
}

void Entity::ResetFF() {}

EntityType::EntityType(const ParamEntry* param)
{
    _par = const_cast<ParamEntry*>(param);
    _refVehicles = 0;
    _refVehiclesLocked = false;
    _shapeReversed = true;
    _shapeAutoCentered = true;
    _useRoadwayForVehicles = false;
}

void EntityType::Load(const ParamEntry& par)
{
    _accuracy = 1.0;

    _className = par.GetName();
    _simName = par >> "simulation";

    RString model = par >> "model";
    _shapeName = ::GetShapeName(model);

    _shapeReversed = false;
    _shapeAutoCentered = true;
    _shapeAnimated = true;
    _useRoadwayForVehicles = false;

    if (par.FindEntry("reversed"))
    {
        _shapeReversed = par >> "reversed";
    }
    if (par.FindEntry("autocenter"))
    {
        _shapeAutoCentered = par >> "autocenter";
    }
    if (par.FindEntry("animated"))
    {
        _shapeAnimated = par >> "animated";
    }
    if (par.FindEntry("useRoadwayForVehicles"))
    {
        _useRoadwayForVehicles = par >> "useRoadwayForVehicles";
    }

    const ParamClass* cls = dynamic_cast<const ParamClass*>(&par);
    if (cls)
    {
        const char* baseName = cls->GetBaseName();
        if (baseName)
        {
            _parentType = VehicleTypes.New(baseName);
        }
    }
}

EntityType::~EntityType()
{
    VehicleUnlock();
    PoseidonAssert(_refVehicles == 0);
}

bool EntityType::IsKindOf(const EntityType* predecessor) const
{
    const EntityType* cur = this;
    while (cur)
    {
        if (cur == predecessor)
        {
            return true;
        }
        cur = cur->_parentType;
    }
    return false;
}

const ParamEntry& EntityType::GetParamEntry() const
{
    return *_par;
}

void EntityType::InitShape()
{
    if (!_shape)
        return;

    const ParamEntry* array = _par->FindEntry("animateTextures");
    if (array)
    {
        int n = array->GetSize() / 2, index = 0;
        _animateTextures.Realloc(n);
        _animateTextures.Resize(n);
        for (int i = 0; i < n; i++)
        {
            RString name = (*array)[index++];
            _animateTextures[i].animation.Init(_shape, name, nullptr);
            float animTime = (*array)[index++];
            _animateTextures[i].animSpeed = animTime > 0 ? 1.0f / animTime : 0;
        }
    }

    array = _par->FindEntry("Animations");
    if (array)
    {
        int n = array->GetEntryCount();
        _animations.Realloc(n);
        _animations.Resize(n);
        for (int i = 0; i < n; i++)
        {
            _animations[i] = AnimationType::CreateObject(array->GetEntry(i), _shape);
        }
    }
}

void EntityType::DeinitShape() {}

void EntityType::ReloadShape(QIStream& f)
{
    if (_refVehicles > 0)
    {
        DeinitShape();
        _shape->Reload(f, _shapeReversed);
        InitShape();
    }
}

void EntityType::AttachShape(LODShapeWithShadow* shape)
{
    if (_refVehicles != 0)
    {
        LOG_DEBUG(Physics, "Shape {} changed when used.", (const char*)shape->Name());
    }
    _shape = shape;
    InitShape();
    if (_refVehicles <= 0)
    {
        _refVehicles = 1;
    }
}

void EntityType::VehicleAddRef() const
{
    if (_refVehicles++ == 0)
    {
        if (_shapeName[0])
        {
            bool shadow = true;
            _shape = Shapes.New(_shapeName, _shapeReversed, shadow);
            if (!_shape)
            {
                LOG_ERROR(World, "Cannot load vehicle shape '{}'", (const char*)_shapeName);
                return;
            }
            if (!_shapeAutoCentered)
            {
                _shape->SetAutoCenter(false);
                _shape->CalculateBoundingSphere();
            }
            const_cast<EntityType*>(this)->InitShape();
            if (_shapeAnimated)
            {
                _shape->AllowAnimation();
            }
        }
        else
        {
            const_cast<EntityType*>(this)->InitShape();
        }
    }
}

void EntityType::VehicleRelease() const
{
    if (--_refVehicles == 0)
    {
        if (_refVehiclesLocked)
        {
            Fail("Locked vehicle released");
        }
        const_cast<EntityType*>(this)->DeinitShape();
        if (_shape)
        {
            _shape.Free();
        }
    }
}

void EntityType::VehicleLock() const
{
    if (_refVehiclesLocked)
    {
        return;
    }
    if (_refVehicles <= 0)
    {
        return;
    }
    _refVehiclesLocked = true;
    VehicleAddRef();
}
void EntityType::VehicleUnlock() const
{
    if (!_refVehiclesLocked)
    {
        return;
    }
    _refVehiclesLocked = false;
    VehicleRelease();
}

DEFINE_CASTING(Entity)

Entity::Entity(LODShapeWithShadow* shape, const EntityType* type, int id)
    : Object(shape ? shape : type->GetShape(), id), _type(const_cast<EntityType*>(type)), _delete(false),
      _convertToObject(false), _moveOutState(MOIn), _local(true), _prec(SimulateDefault),

      _objectContact(false), _landContact(true), _waterContact(false),

      _constantColor(PackedWhite),

      _simulationPrecision(1.0 / 15), _simulationSkipped(0),

      _invAngInertia(M3Identity), _speed(VZero), _modelSpeed(VZero), _acceleration(VZero), _angVelocity(VZero),
      _angMomentum(VZero),

      _invTransform(MIdentity), _invDirty(false), _impulseForce(VZero), _impulseTorque(VZero),

      _targetSide(TCivilian)
{
    if (!type)
    {
        if (_shape)
        {
            LOG_DEBUG(Physics, "No type, shape {}", (const char*)_shape->Name());
        }
        Fail("No type");
    }
    _type->VehicleAddRef();
    _disableDammageUntil = Glob.time + 2;
    Matrix3Val orientation = Orientation();
    if (_shape)
    {
        _invAngInertia = orientation * _shape->InvInertia() * orientation.InverseScaled();
    }
    else
    {
        _invAngInertia = M3Identity;
    }
    _static = false;
    _destrType = DestructNo;
    Object::_type = TypeVehicle;

    _animateTexturesTimes.Realloc(type->_animateTextures.Size());
    _animateTexturesTimes.Resize(type->_animateTextures.Size());
    for (int i = 0; i < type->_animateTextures.Size(); i++)
    {
        _animateTexturesTimes[i] = 0;
    }

    _animations.Realloc(type->_animations.Size());
    _animations.Resize(type->_animations.Size());
    for (int i = 0; i < type->_animations.Size(); i++)
    {
        _animations[i].SetType(type->_animations[i]);
    }
}

Entity::~Entity()
{
    _type->VehicleRelease();
}

void Entity::SetPosition(Vector3Par pos)
{
    base::SetPosition(pos);
    InvDirty();
}
void Entity::SetTransform(const Matrix4& transform)
{
    base::SetTransform(transform);
    InvDirty();
}
void Entity::SetOrient(const Matrix3& dir)
{
    base::SetOrient(dir);
    InvDirty();
}

void Entity::SetOrient(Vector3Par dir, Vector3Par up)
{
    base::SetOrient(dir, up);
    InvDirty();
}
void Entity::SetOrientScaleOnly(float scale)
{
    base::SetOrientScaleOnly(scale);
    InvDirty();
}

void Entity::CalculateInv() const
{
    _invTransform = CalcInvTransform();
}

PackedColor Entity::GetConstantColor() const
{
#if _ENABLE_CHEATS
    if (CHECK_DIAG(DETransparent))
    {
        return PackedColorRGB(_constantColor, _constantColor.A8() / 2);
    }
#endif
    return _constantColor;
}

void Entity::GetMaterial(TLMaterial& mat, int index) const
{
    Color accom = GEngine->GetAccomodateEye();
    Color ccolor = GetConstantColor();
    ccolor = ccolor * accom;

    CreateMaterial(mat, ccolor, index);
}

void Entity::AddForce(Vector3Par pos, Vector3Par force, Color color)
{
    LODShapeWithShadow* forceArrow = GScene->ForceArrow();
    float size = force.Size() * 0.05;
    if (size > 1)
    {
        size = 1;
    }

    Ref<Object> arrow = new ObjectColored(forceArrow, -1);
    Vector3 aside = force.CrossProduct(VAside);
    Vector3 zAside = force.CrossProduct(VForward);
    if (zAside.SquareSize() > aside.SquareSize())
    {
        aside = zAside;
    }
    arrow->SetPosition(pos);
    arrow->SetOrient(force, aside);
    arrow->SetPosition(arrow->PositionModelToWorld(forceArrow->BoundingCenter() * size));
    arrow->SetScale(size);
    arrow->SetConstantColor(PackedColor(Color(color)));
    GLandscape->ShowObject(arrow);
}

void Entity::OrientationSurface()
{
    Vector3Val pos = Position();
    float dX, dZ;
    float sY = GLOB_LAND->SurfaceY(pos.X(), pos.Z(), &dX, &dZ);
    (void)sY;
    Vector3Val normal = Vector3(0, dZ, 1).CrossProduct(Vector3(1, dX, 0)).Normalized();
    Matrix4 transform = Transform();
    transform.SetUpAndDirection(normal, Direction());
    SetTransform(transform);
}

void Entity::ApplySpeed(Matrix4& result, float deltaT)
{
    Vector3 position = Position();
    position += _speed * deltaT;
    // rotate around the center of mass: translate COM to origin, rotate, translate back
    Vector3Val com = GetCenterOfMass();
    Matrix3 orientation = Orientation();
    Vector3Val translateCOM = orientation * com;
    orientation += _angVelocity.Tilda() * orientation * deltaT;
    orientation.Orthogonalize();
    Vector3 backCOM = orientation * com;
    position += translateCOM - backCOM;

    result.SetPosition(position);
    result.SetOrientation(orientation);
}

void Entity::OnAddImpulse(Vector3Par force, Vector3Par torque) {}

void Entity::AddImpulse(Vector3Par force, Vector3Par torque)
{
    OnAddImpulse(force, torque);
    _impulseForce += force;
    _impulseTorque += torque;
}

#if _ENABLE_CHEATS
#define ENABLE_STATS 0
#endif

#if ENABLE_STATS

static StatisticsByName impulseStats;
#endif

void Entity::AddImpulseNetAware(Vector3Par force, Vector3Par torque)
{
#if ENABLE_STATS
    static Time nextReportTime = Glob.time + 60;
    if (Glob.time > nextReportTime)
    {
        impulseStats.Report(true);
        nextReportTime = Glob.time + 60;
    }
#endif

    // if impulse is very small, do not transmit it
    if (torque.SquareSize() < 0.1f && force.SquareSize() < 0.1f)
    {
#if ENABLE_STATS
        impulseStats.Count("impulse<0.1");
#endif
        return;
    }
#if ENABLE_STATS
    else if (torque.SquareSize() + force.SquareSize() < 1.0f)
    {
        impulseStats.Count("impulse<1.0");
    }
    else if (torque.SquareSize() + force.SquareSize() < 10.0f)
    {
        impulseStats.Count("impulse<10.0");
    }
    else if (torque.SquareSize() + force.SquareSize() < 100.0f)
    {
        impulseStats.Count("impulse<100.0");
    }
    else
    {
        impulseStats.Count("big impulse");
    }
#endif

    if (IsLocal())
    {
        AddImpulse(force, torque);
    }
    else
    {
        GetNetworkManager().AskForAddImpulse(this, force, torque);
    }
}

void Entity::ApplyForces(float deltaT, Vector3Par force, Vector3Par torque, Vector3Par friction,
                         Vector3Par torqueFriction, float staticFric)
{
    Vector3 accel = DirectionWorldToModel(force * GetInvMass());
    Vector3 fricAccel = DirectionWorldToModel(friction * GetInvMass());
    Vector3 oldSpeed = _speed;
    Vector3 speed(VMultiply, DirWorldToModel(), _speed);
    Friction(speed, fricAccel, accel, deltaT);
    DirectionModelToWorld(_speed, speed);
    if (deltaT > 0)
    {
        _acceleration = (_speed - oldSpeed) * (1 / deltaT);
    }
    else
    {
        _acceleration = VZero;
    }
    Friction(_angMomentum, torqueFriction, torque, deltaT);

    float rigid = Rigid();
    _speed += _impulseForce * (GetInvMass() * rigid);
    _angMomentum += _impulseTorque * rigid;

    float staticCanHold = staticFric * GetInvMass() * deltaT;
    if (_speed.SquareSize() < Square(staticCanHold))
    {
        _speed = VZero;
    }
    _impulseTorque = VZero;
    _impulseForce = VZero;

    Matrix3Val orientation0 = Transform().Orientation();
    Matrix3Val invOrientation0 = InvTransform().Orientation();
    if (_shape)
    {
        _invAngInertia = orientation0 * InvInertia() * invOrientation0;
    }
    else
    {
        _invAngInertia = M3Zero;
    }

    _angVelocity = _invAngInertia * _angMomentum;
}

inline float fFric(float x)
{
    x *= 2;
    saturate(x, -1, +1);
    return x;
}

inline Vector3 VFric(Vector3Val x, float factor)
{
    float size2 = x.SquareSize() * Square(factor);
    if (size2 > 1)
    {
        factor *= InvSqrt(size2);
    }
    return x * factor;
}

inline void SaturateAbs(float& v, float m)
{
    if (fabs(v) > fabs(m))
    {
        v = fSign(v) * fabs(m);
    }
}

void SaturateAbs(Vector3& v, const Vector3& m)
{
    SaturateAbs(v[0], m[0]);
    SaturateAbs(v[1], m[1]);
    SaturateAbs(v[2], m[2]);
}

#if _DEBUG
#define LOG_FRIC 0
#endif

void Entity::ApplyForcesAndFriction(float deltaT, Vector3Par force, Vector3Par torque, const FrictionPoint* fric,
                                    int nFric)
{
    float frictionCoef = 10;
    float angularFrictionCoef = 1.0f;

    Vector3 wAccel = force * GetInvMass();
    Vector3 oldSpeed = _speed;

    _angMomentum += torque * deltaT;
    _speed += wAccel * deltaT;

    float rigid = Rigid();

    _speed += _impulseForce * (GetInvMass() * rigid);
    _angMomentum += _impulseTorque * rigid;

    _impulseTorque = VZero;
    _impulseForce = VZero;

    Matrix3Val orientation0 = Transform().Orientation();
    Matrix3Val invOrientation0 = InvTransform().Orientation();
    if (_shape)
    {
        _invAngInertia = orientation0 * InvInertia() * invOrientation0;
    }
    else
    {
        _invAngInertia = M3Zero;
    }

    _angVelocity = _invAngInertia * _angMomentum;

#if 1
    if (nFric > 0)
    {
        AUTO_STATIC_ARRAY(Vector3, pVelChange, 128);
        pVelChange.Resize(nFric);

        Vector3 velChange(VZero);
        Vector3 angMomChange(VZero);
        float mass = GetMass();

        Vector3 wCenter(VFastTransform, ModelToWorld(), GetCenterOfMass());
        float sumFricCoef = 0;
        for (int f = 0; f < nFric; f++)
        {
            const FrictionPoint& fp = fric[f];
            sumFricCoef += fp.frictionCoef;
        }
        // sum may exceed 1 when in water; keep combined force in range
        float invSumFricCoef = sumFricCoef > 1 ? 1.0f / sumFricCoef : 1;

        float angMomFriction = 0;
        for (int f = 0; f < nFric; f++)
        {
            const FrictionPoint& fp = fric[f];
            Vector3 pCenter = fp.pos - wCenter;
            Vector3 radVel = _angVelocity.CrossProduct(pCenter);
            Vector3 actVel = _speed + radVel;
#if ARROWS
            AddForce(wCenter + pCenter, actVel * 2, Color(0, 0, 1, 0.25));
#endif

            Vector3 actVelVert = fp.outDir * (actVel * fp.outDir);
            Vector3 actVelHoriz = actVel - actVelVert;

            float speedFrac = 10 * deltaT;
            saturateMin(speedFrac, 1);

            float frictionCoefNorm = invSumFricCoef * fp.frictionCoef;

            pVelChange[f] = actVelVert * (speedFrac * frictionCoefNorm);

            Vector3 vDelta = sign(actVelHoriz) * frictionCoefNorm * frictionCoef * deltaT;

            SaturateAbs(vDelta, actVelHoriz * floatMin(frictionCoefNorm, 1));

            pVelChange[f] += vDelta;

            angMomFriction += frictionCoefNorm * angularFrictionCoef * deltaT;

#if ARROWS
            AddForce(wCenter + pCenter, -pVelChange[f] * (1.0 / deltaT), Color(1, 0, 0, 0.5));
#endif
        }

        for (int f = 0; f < nFric; f++)
        {
            const FrictionPoint& fp = fric[f];
            Vector3 pCenter = fp.pos - wCenter;

            velChange -= pVelChange[f];
            Vector3 pAngMomChange = pCenter.CrossProduct(pVelChange[f] * mass);
            angMomChange -= pAngMomChange;
        }

#if LOG_FRIC
        Log("_speed %.3f,%.3f,%.3f velChange %.3f,%.3f,%.3f", _speed[0], _speed[1], _speed[2], velChange[0],
            velChange[1], velChange[2]);
#endif

        _speed += velChange;
        if (_speed.SquareSize() > 1e6)
        {
            Log("Rotating fast? %.2f,%.2f,%.2f", _angVelocity[0], _angVelocity[1], _angVelocity[2]);
        }
        _angMomentum += angMomChange;
#if LOG_FRIC
        Log("_angMomentum %.3f,%.3f,%.3f angMomChange %.3f,%.3f,%.3f, fric %.2f", _angMomentum[0], _angMomentum[1],
            _angMomentum[2], angMomChange[0], angMomChange[1], angMomChange[2], angMomFriction);
#endif

        _angMomentum *= 1 - floatMin(angMomFriction, 1);
#if LOG_FRIC
        GlobalShowMessage(100, "AM %5.1f, M %5.1f, S %5.1f", _angMomentum.Size(), _speed.Size() * GetMass(),
                          _angMomentum.Size() + _speed.Size() * GetMass());
#endif
    }
#endif

    if (_shape)
    {
        _invAngInertia = orientation0 * InvInertia() * invOrientation0;
    }
    else
    {
        _invAngInertia = M3Zero;
    }
    _angVelocity = _invAngInertia * _angMomentum;

    if (deltaT > 0)
    {
        _acceleration = (_speed - oldSpeed) * (1 / deltaT);
    }
    else
    {
        _acceleration = VZero;
    }
}

PilotHeadPars::PilotHeadPars()
{
    friction = 10;
    movement = 160;
    maxAmp = 0.05;
    maxSpeed = 3;
    radius = 0.2;
}

PilotHeadPars::PilotHeadPars(const ParamEntry& entry)
{
    friction = entry >> "friction";
    movement = entry >> "movement";
    maxAmp = entry >> "maxAmp";
    maxSpeed = entry >> "maxSpeed";
    radius = entry >> "radius";
}

PilotHead::PilotHead()
{
    _valid = false;
}

void PilotHead::Init(Vector3Par neck, Vector3Par head, const Entity* vehicle)
{
    _neck = neck, _head = head;
    Reset(vehicle);
    _valid = true;
}

void PilotHead::SetPars(const PilotHeadPars& pars)
{
    _pars = pars;
}

void PilotHead::SetPars(const char* name)
{
    _pars = PilotHeadPars(Pars >> "CfgHeads" >> name);
}

void PilotHead::Move(float deltaT, const Frame& newFrame, const Frame& oldFrame)
{
    float invDeltaT = 1 / deltaT;
    Vector3Val oldNeckPos = oldFrame.PositionModelToWorld(_neck);
    Vector3Val newNeckPos = newFrame.PositionModelToWorld(_neck);
    Vector3Val neckSpeed = (newNeckPos - oldNeckPos) * invDeltaT;

    Vector3Val posWEnd = oldFrame.PositionModelToWorld(_pos) + _speed * deltaT;
    Vector3Val headWEnd = newFrame.PositionModelToWorld(_head);

    Matrix4Val invTransform = newFrame.CalcInvTransform();
    Vector3 oSpeed = invTransform.Rotate(_speed - neckSpeed);
    _pos += oSpeed * deltaT;
    saturate(_pos[0], _head[0] - _pars.maxAmp, _head[0] + _pars.maxAmp);
    saturate(_pos[1], _head[1] - _pars.maxAmp, _head[1] + _pars.maxAmp);
    saturate(_pos[2], _head[2] - _pars.maxAmp, _head[2] + _pars.maxAmp);
    Vector3 norm = (_pos - _neck);
    _pos = _neck + norm.Normalized() * _pars.radius;
    Vector3 center = (headWEnd - posWEnd) * _pars.movement;
    Vector3 friction = oSpeed * _pars.friction;
    center = invTransform.Rotate(center);
    Friction(oSpeed, friction, center, deltaT);
    saturate(oSpeed[0], -_pars.maxSpeed, _pars.maxSpeed);
    saturate(oSpeed[1], -_pars.maxSpeed, _pars.maxSpeed);
    saturate(oSpeed[2], -_pars.maxSpeed, _pars.maxSpeed);
    _speed = newFrame.DirectionModelToWorld(oSpeed) + neckSpeed;
}

void Friction(float& speed, float friction, float accel, float deltaT)
{
    speed += accel * deltaT;
    if (speed * friction > 0)
    { // friction must oppose speed
        float oSpeed = speed;
        speed -= friction * deltaT;
        if (oSpeed * speed <= 0)
        {
            speed = 0;
        }
    }
}

void Friction(Vector3& speed, Vector3Par friction, Vector3Par accel, float deltaT)
{
    Friction(speed[0], friction[0], accel[0], deltaT);
    Friction(speed[1], friction[1], accel[1], deltaT);
    Friction(speed[2], friction[2], accel[2], deltaT);
}

AttachedOnVehicle::AttachedOnVehicle(Object* vehicle, Vector3Par pos, Vector3Par dir)
    : _vehicle(vehicle), _pos(pos), _dir(dir)
{
    GLOB_WORLD->AddAttachment(this);
}

AttachedOnVehicle::~AttachedOnVehicle()
{
    GLOB_WORLD->RemoveAttachment(this);
}

void AttachedOnVehicle::UpdatePosition()
{
    if (_vehicle != nullptr)
    {
        float scale = Scale();
        Matrix4 toWorld = _vehicle->WorldTransform();
        SetPosition(toWorld.FastTransform(_pos));
        SetOrient(toWorld.Rotate(_dir), toWorld.DirectionUp());
        SetScale(scale);
    }
}

LSError AttachedOnVehicle::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.SerializeRef("Entity", _vehicle, 1))
    PARAM_CHECK(ar.Serialize("pos", _pos, 1))
    PARAM_CHECK(ar.Serialize("dir", _dir, 1))
    if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassSecond)
    {
        UpdatePosition();
    }
    return LSOK;
}

float Object::CamEffectFOV() const
{
    return 0.7f;
}

bool Object::IsContinuous(CameraType camType) const
{
    return camType == CamGunner || camType == CamGroup;
}

bool Object::IsVirtual(CameraType camType) const
{
    return camType != CamGunner;
}

Vector3 Object::GetCameraDirection(CameraType camType) const
{
    return Direction();
}

bool Object::IsVirtualX(CameraType camType) const
{
    return true;
}

void Object::DetectControlMode() const
{
    static const UserAction moveActions[] = {UAMoveForward,     UAMoveBack, UAMoveFastForward,
                                             UAMoveSlowForward, UAMoveUp,   UAMoveDown};
    static const UserAction turnActions[] = {UAMoveLeft, UAMoveRight, UATurnLeft, UATurnRight};
    static const UserAction cursorActions[] = {UALookLeftDown, UALookDown,   UALookRightDown,
                                               UALookLeft,     UALookCenter, UALookRight,
                                               UALookLeftUp,   UALookUp,     UALookRightUp};
    static const UserAction thrustActions[] = {
        UAMoveUp,
        UAMoveDown,
    };

    const int nMoveActions = sizeof(moveActions) / sizeof(*moveActions);
    const int nTurnActions = sizeof(turnActions) / sizeof(*turnActions);
    const int nCursorActions = sizeof(cursorActions) / sizeof(*cursorActions);
    const int nThrustActions = sizeof(thrustActions) / sizeof(*thrustActions);
    DetectControlModeActions(moveActions, nMoveActions, turnActions, nTurnActions, cursorActions, nCursorActions,
                             thrustActions, nThrustActions);
}

void Object::DetectControlModeActions(const UserAction* moveActions, int nMoveActions, const UserAction* turnActions,
                                      int nTurnActions, const UserAction* cursorActions, int nCursorActions,
                                      const UserAction* thrustActions, int nThrustActions) const
{
    auto& input = InputSubsystem::Instance();
    bool keyMoveActive = false;
    bool keyTurnActive = false;
    bool keyCursorActive = false;
    bool keyThrustActive = false;
    for (int i = 0; i < nMoveActions; i++)
    {
        if (input.GetAction(moveActions[i]))
        {
            keyMoveActive = true;
            break;
        }
    }
    for (int i = 0; i < nTurnActions; i++)
    {
        if (input.GetAction(turnActions[i]))
        {
            keyTurnActive = true;
            break;
        }
    }
    for (int k = 0; k < nCursorActions; k++)
    {
        if (input.GetAction(cursorActions[k]))
        {
            keyCursorActive = true;
            break;
        }
    }
    for (int k = 0; k < nThrustActions; k++)
    {
        if (input.GetAction(thrustActions[k]))
        {
            keyThrustActive = true;
            break;
        }
    }

    if (keyMoveActive)
        input.MarkKeyboardMoveActive();

    if (keyTurnActive)
        input.MarkKeyboardTurnActive();

    if (keyCursorActive)
    {
        input.MarkKeyboardCursorActive();
        input.MarkKeyboardTurnActive();
    }

    if (keyThrustActive)
        input.MarkKeyboardThrustActive();

    {
        static const UserAction moveAxis[] = {UAAxisTurn, UAAxisDive, UAAxisRudder};
        for (int i = 0; i < sizeof(moveAxis) / sizeof(*moveAxis); i++)
        {
            if (input.IsActionBoundToRecentAxis(moveAxis[i]))
            {
                input.MarkJoystickMoveActive();
                break;
            }
        }

        if (input.IsActionBoundToRecentAxis(UAAxisThrust))
            input.MarkJoystickThrustActive();
    }
}

Vector3 Object::ExternalCameraPosition(CameraType camType) const
{
    return Vector3(0, 2, -20);
}

bool Object::HasFlares(CameraType camType) const
{
    return camType != CamInternal;
}

bool Object::IsExternal(CameraType camType) const
{
    return camType == CamGroup;
}

int Object::InsideViewGeomLOD(CameraType camType) const
{
    if (!_shape)
    {
        return LOD_INVISIBLE;
    }
    int inside = InsideLOD(camType);
    int view = LOD_INVISIBLE;
    if (inside != LOD_INVISIBLE && inside >= 0)
    {
        if (_shape->IsSpecLevel(inside, VIEW_PILOT))
        {
            view = _shape->FindViewPilotGeometryLevel();
        }
        else if (_shape->IsSpecLevel(inside, VIEW_GUNNER))
        {
            view = _shape->FindViewGunnerGeometryLevel();
        }
        else if (_shape->IsSpecLevel(inside, VIEW_COMMANDER))
        {
            view = _shape->FindViewCommanderGeometryLevel();
        }
        else if (_shape->IsSpecLevel(inside, VIEW_CARGO))
        {
            view = _shape->FindViewCargoGeometryLevel();
        }
    }
    return view;
}

bool Object::IsGunner(CameraType camType) const
{
    return camType == CamGunner || camType == CamInternal || camType == CamExternal;
}
bool Object::IsTurret(CameraType camType) const
{
    return false;
}
bool Object::ShowAim(int weapon, CameraType camType) const
{
    return true;
}
bool Object::ShowCursor(int weapon, CameraType camType) const
{
    return true;
}

void Object::LimitCursorHard(CameraType camType, Vector3& dir) const {}

void Object::LimitCursor(CameraType camType, Vector3& dir) const {}

void Object::OverrideCursor(CameraType camType, Vector3& dir) const {}

void Object::SimulateHUD(CameraType camType, float deltaT) {}

CursorMode Object::GetCursorRelMode(CameraType camType) const
{
    auto& input = InputSubsystem::Instance();
    if (IsVirtual(camType) && !input.IsLookAroundEnabled() && !input.IsMouseTurnActive())
    {
        return CKeyboard;
    }
    return CMouseAbs;
}

void Object::LimitVirtual(CameraType camType, float& heading, float& dive, float& fov) const
{
    switch (camType)
    {
        case CamInternal:
        case CamExternal:
            saturate(fov, 0.42, 0.85);
            break;
        default:
            saturate(fov, 0.01, 1.5);
            break;
    }
    heading = AngleDifference(heading, 0);
    saturate(dive, -H_PI * 0.3, +H_PI * 0.3);
}

void Object::InitVirtual(CameraType camType, float& heading, float& dive, float& fov) const
{
    switch (camType)
    {
        case CamExternal:
        case CamGroup:
            heading = 0;
            dive = 0.1;
            fov = 0.7;
            break;
        default:
            heading = dive = 0;
            fov = 0.7;
            break;
    }
    if (fabs(heading) > 2 * H_PI)
    {
        heading = fastFmod(heading, 2 * H_PI);
    }
    if (fabs(dive) > 2 * H_PI)
    {
        dive = fastFmod(dive, 2 * H_PI);
    }
}

} // namespace Poseidon
bool EnableVisualEffects(Vector3Par effPos, SimulationImportance prec)
{
    using namespace Poseidon;
    if (IsDedicatedServer())
    {
        return false;
    }
    Vector3 pos = GScene->GetCamera()->Position();
    float dist2 = pos.Distance2(effPos);
    return dist2 < Square(ENGINE_CONFIG.objectsZ);
}
namespace Poseidon
{

bool Entity::EnableVisualEffects(SimulationImportance prec) const
{
    return ::EnableVisualEffects(Position(), prec);
}

SimulationImportance Entity::CalculateImportance(const Vector3* viewerPos, int nViewers) const
{
    if (!IsLocal() && _prec != SimulateDefault)
    {
        return _prec;
    }
    SimulationImportance maxPrec = WorstImportance();
    SimulationImportance minPrec = BestImportance();
    if (maxPrec <= minPrec)
    {
        return maxPrec;
    }

    SimulationImportance ret = SimulateVisibleNear;
    float dist2 = 1e20;
    for (int i = 0; i < nViewers; i++)
    {
        float d2 = viewerPos[i].Distance2(Position());
        saturateMin(dist2, d2);
    }
    float radius = GetRadius();
    if (radius >= 100)
    {
        float dist = floatMax(dist2 * InvSqrt(dist2) - radius, 0);
        dist2 = Square(dist);
    }
    if (dist2 > Square(ENGINE_CONFIG.objectsZ + 500))
    {
        ret = SimulateInvisibleFar;
    }
    else if (dist2 > Square(ENGINE_CONFIG.objectsZ))
    {
        ret = SimulateInvisibleNear;
    }
    else if (dist2 > Square(100))
    {
        ret = SimulateVisibleFar;
    }

    if (ret > maxPrec)
    {
        ret = maxPrec;
    }
    if (ret < minPrec)
    {
        ret = minPrec;
    }
    if (IsLocal())
    {
        const_cast<Entity*>(this)->_prec = ret;
    }
    return ret;
}

void Entity::PlaceOnSurface(Matrix4& trans)
{
    if (!GetShape())
    {
        return;
    }

    float dx, dz;
    Vector3 pos;
    pos = trans.Position();
    pos[1] = GLandscape->RoadSurfaceYAboveWater(pos, &dx, &dz);

    LODShape* shape = GetShape();

    Matrix4 newTransform;
    if ((shape->GetOrHints() & ClipLandKeep | ClipLandOn))
    {
        newTransform.SetUpAndDirection(VUp, trans.Direction());
    }
    else
    {
        Vector3 up(-dx, 1, -dz);
        Matrix4 newTransform;
        newTransform.SetUpAndDirection(up, trans.Direction());
    }
    if (!Static())
    {
        Shape* geom = shape->LandContactLevel();
        if (!geom)
        {
            geom = shape->GeometryLevel();
        }
        if (!geom)
        {
            geom = shape->Level(0);
        }

        if (!geom)
        {
            pos[1] -= shape->Min().Y();
        }
        else
        {
            pos[1] -= geom->Min().Y();
        }
    }
    else
    {
        pos += newTransform.Orientation() * GetShape()->BoundingCenter();
    }
    newTransform.SetPosition(pos);
    trans = newTransform;
}

DEF_RSB(type)
DEF_RSB(shape)
DEF_RSB(id)
DEF_RSB(Transform)
DEF_RSB(speed)
DEF_RSB(angMomentum)

#define TRANS(x) PARAM_CHECK(ar.Serialize(RSB(x), _##x, 1))

#define TRANS_DEF(x, defVal) PARAM_CHECK(ar.Serialize(RSB(x), _##x, 1, defVal))

bool Entity::CastShadow() const
{
    return IS_SHADOW_VEHICLE;
}

float Entity::GetAnimationPhase(RString animation) const
{
    for (int i = 0; i < _animations.Size(); i++)
    {
        if (stricmp(_animations[i].GetName(), animation) == 0)
        {
            return _animations[i].GetPhase();
        }
    }
    return 0.0f;
}

void Entity::SetAnimationPhase(RString animation, float phase)
{
    for (int i = 0; i < _animations.Size(); i++)
    {
        if (stricmp(_animations[i].GetName(), animation) == 0)
        {
            _animations[i].SetPhaseWanted(phase);
            return;
        }
    }
}

bool Entity::IsAnimated(int level) const
{
    if (base::IsAnimated(level))
    {
        return true;
    }
    return _animations.Size() > 0;
}

bool Entity::IsAnimatedShadow(int level) const
{
    if (base::IsAnimatedShadow(level))
    {
        return true;
    }
    return _animations.Size() > 0;
}

void Entity::Animate(int level)
{
    const EntityType* type = GetNonAIType();

    for (int i = 0; i < type->_animateTextures.Size(); i++)
    {
        type->_animateTextures[i].animation.AnimateTexture(_shape, level, _animateTexturesTimes[i]);
    }

    for (int i = 0; i < _animations.Size(); i++)
    {
        const AnimationInstance& anim = _animations[i];
        if (anim.GetSelection(level) >= 0)
        {
            Matrix4 baseAnim = MIdentity;
            AnimateMatrix(baseAnim, level, _animations[i].GetSelection(level));
            _animations[i].Animate(_shape, level, baseAnim);
        }
    }

    base::Animate(level);
}

void Entity::Deanimate(int level)
{
    const EntityType* type = GetNonAIType();

    for (int i = 0; i < _animations.Size(); i++)
    {
        _animations[i].Deanimate(_shape, level);
    }

    for (int i = 0; i < type->_animateTextures.Size(); i++)
    {
        type->_animateTextures[i].animation.AnimateTexture(_shape, level, 0);
    }

    base::Deanimate(level);
}

LSError Entity::Serialize(ParamArchive& ar)
{
    if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
    {
        if (Object::GetType() != Primary)
        {
            if (ar.IsSaving())
            {
                Matrix4& trans = const_cast<Matrix4&>(Transform());
                PARAM_CHECK(ar.Serialize(RSB(Transform), trans, 1))
            }
            else if (ar.GetPass() == ParamArchive::PassFirst)
            {
                Matrix4 trans;
                PARAM_CHECK(ar.Serialize(RSB(Transform), trans, 1))
                SetTransform(trans);
            }
        }
    }

    base::Serialize(ar);
    if (ar.IsSaving())
    {
        RString type = GetName() ? GetName() : "";
        RString shape = _shape ? _shape->Name() : "";
        PARAM_CHECK(ar.Serialize(RSB(type), type, 1))
        PARAM_CHECK(ar.Serialize(RSB(shape), shape, 1))
    }

    if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
    {
        if (Object::GetType() != Primary)
        {
            if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassFirst)
            {
                PARAM_CHECK(ar.Serialize(RSB(id), _id, 1))
                GLandscape->AddToIDCache(this);
            }

            TRANS_DEF(speed, VZero)
            TRANS_DEF(angMomentum, VZero)
            if (ar.IsLoading())
            {
                _invDirty = true;
                DirectionWorldToModel(_modelSpeed, _speed);

                Matrix3Val orientation0 = Transform().Orientation();
                Matrix3Val invOrientation0 = InvTransform().Orientation();
                if (_shape)
                {
                    _invAngInertia = orientation0 * InvInertia() * invOrientation0;
                }
                else
                {
                    _invAngInertia = M3Zero;
                }
                _angVelocity = _invAngInertia * _angMomentum;
                _acceleration = VZero;
                _impulseForce = VZero;
                _impulseTorque = VZero;
            }

            PARAM_CHECK(ar.SerializeRef("hierParent", _hierParent, 1));
        }
        PARAM_CHECK(ar.SerializeEnum("targetSide", _targetSide, 1))
    }

    return LSOK;
}

Entity* Entity::CreateObject(ParamArchive& ar)
{
    RString type, shape;
    if (ar.Serialize("type", type, 1) != LSOK)
    {
        return nullptr;
    }
    if (ar.Serialize("shape", shape, 1) != LSOK)
    {
        return nullptr;
    }
    Entity* veh = NewNonAIVehicle(type, shape, false);
    if (veh && veh->Object::GetType() == Primary)
    {
        veh->SetType(TypeVehicle);
    }
    return veh;
}

NetworkId Entity::GetNetworkId() const
{
    if ((ObjectType)Object::_type == Primary)
    {
        return Object::GetNetworkId();
    }
    else
    {
        return _networkId;
    }
}

NetworkMessageType Entity::GetNMType(NetworkMessageClass cls) const
{
    switch (cls)
    {
        case NMCCreate:
            return NMTCreateVehicle;
        case NMCUpdateGeneric:
            return NMTUpdateVehicle;
        case NMCUpdatePosition:
            return NMTUpdatePositionVehicle;
        default:
            return base::GetNMType(cls);
    }
}

DEFINE_NET_INDICES_EX(CreateVehicle, NetworkObject, CREATE_VEHICLE_MSG)

} // namespace Poseidon

DEFINE_GET_INDICES(CreateVehicle)

namespace Poseidon
{

DEFINE_NET_INDICES_EX_ERR(UpdateVehicle, UpdateObject, UPDATE_VEHICLE_MSG)

} // namespace Poseidon

DEFINE_GET_INDICES(UpdateVehicle)

namespace Poseidon
{

IndicesUpdatePositionVehicle::IndicesUpdatePositionVehicle()
{
    entityUpdPos = -1;
    prec = -1;
}

void IndicesUpdatePositionVehicle::Scan(NetworkMessageFormatBase* format)
{
    base::Scan(format);

    SCAN(entityUpdPos)
    SCAN(prec)
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesUpdatePositionVehicle()
{
    using namespace Poseidon;
    return new IndicesUpdatePositionVehicle();
}
namespace Poseidon
{

NetworkMessageFormat& Entity::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    Vector3 temp = VZero;
    switch (cls)
    {
        case NMCCreate:
            NetworkObject::CreateFormat(cls, format);
            CREATE_VEHICLE_MSG(MSG_FORMAT)
            break;
        case NMCUpdateGeneric:
            base::CreateFormat(cls, format);
            UPDATE_VEHICLE_MSG(MSG_FORMAT_ERR)
            break;
        case NMCUpdatePosition:
            base::CreateFormat(cls, format);
            format.Add("entityUpdPos", NDTRawData, NCTNone, DEFVALUERAWDATA, DOC_MSG("Encoded position"),
                       ET_UPD_ENTITY_POS, 1);
            format.Add("prec", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, SimulateDefault),
                       DOC_MSG("Simulation precision"), ET_NOT_EQUAL, ERR_COEF_VALUE_MAJOR);
            break;
        default:
            base::CreateFormat(cls, format);
            break;
    }
    return format;
}

void Entity::CancelMoveOutInProgress()
{
    DoAssert((MoveOutState)_moveOutState == MOMovingOut);
    _moveOutState = MOIn;
}

void Entity::SetMoveOut(Object* parent)
{
    PoseidonAssert(_moveOutState == MOIn);
    _moveOutState = MOMovingOut, _hierParent = parent;
}
void Entity::SetMoveOutDone(Object* parent)
{
    PoseidonAssert(_moveOutState == MOIn);
    _moveOutState = MOMovedOut, _hierParent = parent;
}

void Entity::SetMoveOutFlag()
{
    _moveOutState = MOMovedOut;
}

Entity* Entity::CreateObject(NetworkMessageContext& ctx)
{
    PoseidonAssert(dynamic_cast<const IndicesCreateVehicle*>(ctx.GetIndices())) const IndicesCreateVehicle* indices =
        static_cast<const IndicesCreateVehicle*>(ctx.GetIndices());

    VehicleListType list;
    if (ctx.IdxTransfer(indices->list, (int&)list) != TMOK)
    {
        return nullptr;
    }
    RString type;
    if (ctx.IdxTransfer(indices->type, type) != TMOK)
    {
        return nullptr;
    }
    RString shape;
    if (ctx.IdxTransfer(indices->shape, shape) != TMOK)
    {
        return nullptr;
    }
    Vector3 position;
    if (ctx.IdxTransfer(indices->position, position) != TMOK)
    {
        return nullptr;
    }

    Entity* veh = NewNonAIVehicle(type, shape, false);
    if (!veh)
    {
        return nullptr;
    }
    Matrix4 trans;
    trans.SetIdentity();
    trans.SetPosition(position);
    veh->Init(trans);
    veh->SetPosition(position);
    if (veh && veh->Object::GetType() == Primary)
    {
        veh->SetType(TypeVehicle);
    }
    switch (list)
    {
        case VLTVehicle:
            GWorld->AddVehicle(veh);
            break;
        case VLTAnimal:
            GWorld->AddAnimal(veh);
            break;
        case VLTBuilding:
            GWorld->AddBuilding(veh);
            break;
        case VLTCloudlet:
            GWorld->AddCloudlet(veh);
            break;
        case VLTFast:
            GWorld->AddFastVehicle(veh);
            break;
        case VLTOut:
            GWorld->AddOutVehicle(veh);
            veh->SetMoveOutFlag();
            break;
        default:
            Fail("Bad list type");
            return nullptr;
    }

    int idVeh;
    if (ctx.IdxTransfer(indices->idVehicle, idVeh) != TMOK)
    {
        return nullptr;
    }
    if (idVeh >= 0)
    {
        if (idVeh >= vehiclesMap.Size())
        {
            vehiclesMap.Resize(idVeh + 1);
        }
        VehicleWithAI* vai = dyn_cast<VehicleWithAI>(veh);
        if (vai)
        {
            vehiclesMap[idVeh] = vai;
        }
        else
        {
            LOG_ERROR(Physics, "NonAI vehicle where AI expected - {}", (const char*)veh->GetDebugName());
        }
    }

    RString name;
    if (ctx.IdxTransfer(indices->name, name) != TMOK)
    {
        return nullptr;
    }
    if (name.GetLength() > 0)
    {
        veh->SetVarName(name);
        GWorld->GetGameState()->VarSet(name, GameValueExt(veh), true);
    }

    NetworkId objectId;
    if (ctx.IdxTransfer(indices->objectCreator, objectId.creator) != TMOK)
    {
        return nullptr;
    }
    if (ctx.IdxTransfer(indices->objectId, objectId.id) != TMOK)
    {
        return nullptr;
    }
    veh->SetNetworkId(objectId);
    veh->SetLocal(false);

    if (dyn_cast<Transport>(veh))
    {
        DoAssert(IsUpdateTransport(veh->GetNMType(NMCUpdateGeneric)));
    }

    return veh;
}

void Entity::DestroyObject()
{
    SetDelete();
}

TMError Entity::TransferMsg(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCCreate:
            TMCHECK(NetworkObject::TransferMsg(ctx))
            if (ctx.IsSending())
            {
                PoseidonAssert(dynamic_cast<const IndicesCreateVehicle*>(ctx.GetIndices()))
                    const IndicesCreateVehicle* indices = static_cast<const IndicesCreateVehicle*>(ctx.GetIndices());

                RString type = GetName() ? GetName() : "";
                TMCHECK(ctx.IdxTransfer(indices->type, type))
                RString shape = _shape ? _shape->Name() : "";
                TMCHECK(ctx.IdxTransfer(indices->shape, shape))
                Vector3& position = const_cast<Vector3&>(Position());
                TMCHECK(ctx.IdxTransfer(indices->position, position))
                TMCHECK(ctx.IdxTransferRef(indices->hierParent, _hierParent))
            }
            break;
        case NMCUpdateGeneric:
            TMCHECK(base::TransferMsg(ctx))
            {
                PoseidonAssert(dynamic_cast<const IndicesUpdateVehicle*>(ctx.GetIndices()))
                    const IndicesUpdateVehicle* indices = static_cast<const IndicesUpdateVehicle*>(ctx.GetIndices());
                ITRANSF_ENUM(targetSide)
                if (ctx.IsSending())
                {
                    AUTO_STATIC_ARRAY(float, animations, 32);
                    animations.Resize(_animations.Size());
                    for (int i = 0; i < _animations.Size(); i++)
                    {
                        animations[i] = _animations[i].GetPhaseWanted();
                    }
                    TMCHECK(ctx.IdxTransfer(indices->animations, animations))
                }
                else
                {
                    AUTO_STATIC_ARRAY(float, animations, 32);
                    TMCHECK(ctx.IdxTransfer(indices->animations, animations))
                    for (int i = 0; i < _animations.Size(); i++)
                    {
                        if (i < animations.Size())
                        {
                            _animations[i].SetPhaseWanted(animations[i]);
                        }
                    }
                }
            }
            break;
        case NMCUpdatePosition:
            TMCHECK(base::TransferMsg(ctx))
            {
                PoseidonAssert(dynamic_cast<const IndicesUpdatePositionVehicle*>(ctx.GetIndices()))
                    const IndicesUpdatePositionVehicle* indices =
                        static_cast<const IndicesUpdatePositionVehicle*>(ctx.GetIndices());
                if (ctx.IsSending())
                {
                    NetworkUpdEntityPos pos;
                    pos.orientation.Encode(Orientation());
                    pos.position = Position();
                    pos.speed = _speed;
                    pos.angMomentum = _angMomentum;
                    TMCHECK(ctx.IdxSendRaw(indices->entityUpdPos, &pos, sizeof(pos)));
                }
                else
                {
                    void* data;
                    int size;
                    TMCHECK(ctx.IdxGetRaw(indices->entityUpdPos, data, size));
                    if (size == sizeof(NetworkUpdEntityPos))
                    {
                        if (IsInLandscape())
                        {
                            // only entities that are really present in the lanscape
                            // may receive updates
                            const NetworkUpdEntityPos& pos = *(NetworkUpdEntityPos*)data;
                            Matrix4 trans;
                            pos.orientation.Decode(trans);
                            trans.SetPosition(pos.position);

                            // Ease a moving remote unit toward the update over
                            // the next sim steps (ApplyRemoteState) instead of
                            // snapping, which reads as a teleport under lag /
                            // throttling. A stationary unit, or any unit with
                            // smoothing disabled, snaps as the original engine did.
                            if (GMPRemoteInterp && !IsLocal() && pos.speed.SquareSize() >= Square(0.1f))
                            {
                                _remoteInterp.SetTarget(pos.position, pos.speed, trans.Orientation());
                                _angMomentum = pos.angMomentum;
                            }
                            else
                            {
                                Move(trans);
                                _speed = pos.speed;
                                _angMomentum = pos.angMomentum;
                                _remoteInterp.Clear();
                            }
                        }
                    }
                    else
                    {
                        Fail("Bad size of NetworkUpdEntityPos field");
                    }
                }
                ITRANSF_ENUM(prec)
            }
            break;
        default:
            TMCHECK(base::TransferMsg(ctx))
            break;
    }
    return TMOK;
}

float Entity::CalculateError(NetworkMessageContext& ctx)
{
    float error = 0;
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
            error += base::CalculateError(ctx);
            {
                PoseidonAssert(dynamic_cast<const IndicesUpdateVehicle*>(ctx.GetIndices()))
                    const IndicesUpdateVehicle* indices = static_cast<const IndicesUpdateVehicle*>(ctx.GetIndices());

                AUTO_STATIC_ARRAY(float, animations, 32);
                if (ctx.IdxTransfer(indices->animations, animations) == TMOK)
                {
                    int size = animations.Size();
                    saturateMin(size, _animations.Size());
                    float animError = 0;
                    for (int i = 0; i < size; i++)
                    {
                        animError += fabs(animations[i] - _animations[i].GetPhaseWanted());
                    }
                    error += animError * ERR_COEF_VALUE_MAJOR;
                }
            }
            break;
        case NMCUpdatePosition:
            error += base::CalculateError(ctx);
            {
                PoseidonAssert(dynamic_cast<const IndicesUpdatePositionVehicle*>(ctx.GetIndices()))
                    const IndicesUpdatePositionVehicle* indices =
                        static_cast<const IndicesUpdatePositionVehicle*>(ctx.GetIndices());
                {
                    AutoArray<char> temp;
                    if (ctx.IdxTransfer(indices->entityUpdPos, temp) == TMOK &&
                        temp.Size() == sizeof(NetworkUpdEntityPos))
                    {
                        NetworkUpdEntityPos& d = *(NetworkUpdEntityPos*)temp.Data();
                        error += d.position.Distance(Position());
                        error += d.orientation.DirectionUp().Distance(Orientation().DirectionUp());
                        error += d.orientation.Direction().Distance(Orientation().Direction());

                        float dt = Glob.time - ctx.GetMsgTime();
                        error += dt * d.speed.Distance(Speed());
                        error += dt * d.angMomentum.Distance(AngMomentum());
                    }
                }
                ICALCERR_NEQ(int, prec, ERR_COEF_VALUE_MAJOR)
            }
            break;
        default:
            break;
    }
    return error;
}

void Entity::ScanContactPoints(ContactArray& contacts, const Frame& moveTrans, SimulationImportance prec, float above,
                               bool ignoreObjects)
{
    _objectContact = false;

    if (!ignoreObjects)
    {
#define MAX_IN 0.2
#define MAX_IN_FORCE 0.1
#define MAX_IN_FRICTION 0.2

        CollisionBuffer collision;
        GLandscape->ObjectCollision(collision, this, moveTrans);

        for (int i = 0; i < collision.Size(); i++)
        {
            const CollisionInfo& info = collision[i];
            Object* obj = info.object;
            if (!obj)
            {
                continue;
            }

            ContactPoint& contact = contacts.Append();
            contact.under = info.under;
            contact.pos = info.object->PositionModelToWorld(info.pos);
            contact.dirOut = info.object->DirectionModelToWorld(info.dirOut);
            contact.type = GroundSolid;
            contact.texture = info.texture;
            contact.obj = info.object;

            _objectContact = true;
        }
    }

    GroundCollisionBuffer gCollision;

    if (prec >= SimulateVisibleFar)
    {
        GLandscape->GroundCollisionPlane(gCollision, this, moveTrans, above, 0, false);
    }
    else
    {
        GLandscape->GroundCollision(gCollision, this, moveTrans, above, 0, false);
    }

    _landContact = false;
    _waterContact = false;
    for (int i = 0; i < gCollision.Size(); i++)
    {
        const UndergroundInfo& info = gCollision[i];
        ContactPoint& contact = contacts.Append();
        contact.under = info.under;
        contact.pos = info.pos;
        contact.dirOut = Vector3(0, info.dZ, 1).CrossProduct(Vector3(1, info.dX, 0)).Normalized();
        contact.type = info.type;
        contact.texture = info.texture;
        contact.obj = nullptr;

        if (contact.type == GroundWater)
        {
            _waterContact = true;
        }
        else
        {
            _landContact = true;
        }
    }
}

void Entity::ConvertContactsToFrictions(const ContactArray& contacts, FrictionArray& frictions, const Frame& moveTrans,
                                        Vector3& offset, Vector3& force, Vector3& torque, float crash,
                                        float maxColSpeed2)
{
    offset = VZero;
    crash = 0;
    maxColSpeed2 = 0;

#define MAX_UNDER 0.2
    const float maxUnderForce = 0.2;

    int outForceFactorCount = 0;
    AUTO_STATIC_ARRAY(float, outForceFactor, 256);
    for (int i = 0; i < contacts.Size(); i++)
    {
        const ContactPoint& info = contacts[i];
        if (info.type == GroundWater)
        {
            continue;
        }
        outForceFactorCount++;
    }
    float outForceFactorSum = 0;
    outForceFactor.Realloc(contacts.Size());
    outForceFactor.Resize(contacts.Size());
    if (outForceFactorCount > 0)
    {
        for (int i = 0; i < contacts.Size(); i++)
        {
            const ContactPoint& info = contacts[i];
            outForceFactor[i] = 0;
            if (info.type == GroundWater)
            {
                continue;
            }
            float factor = info.under;
            saturate(factor, 0, maxUnderForce);
            outForceFactor[i] = factor * (2.0 / maxUnderForce);
            outForceFactorSum += outForceFactor[i];
        }
        // one point submerged to maxUnderForce can push with force 2; normalize above that
        if (outForceFactorSum > 2)
        {
            float invSum = 2 / outForceFactorSum;
            for (int i = 0; i < contacts.Size(); i++)
            {
                outForceFactor[i] *= invSum;
            }
        }
    }

    Vector3 wCenter(VFastTransform, moveTrans.ModelToWorld(), GetCenterOfMass());

#if LOG_FRIC
    float fricFactorSum = 0;
    float accelSum = 0;
#endif
    for (int i = 0; i < contacts.Size(); i++)
    {
        const ContactPoint& info = contacts[i];

        if (info.under < 0)
        {
            continue;
        }

        float offsetInOutDir = info.dirOut * offset;
        saturateMax(offsetInOutDir, 0);
        float under = info.under - offsetInOutDir;
        if (under < 0)
        {
            continue;
        }

        Vector3 pCenter = info.pos - wCenter;

        Vector3Val dirOut = info.dirOut;

        if (info.type == GroundWater)
        {
            float radius = GetRadius();
            float area = radius * radius;
            float areaPerPoint = area / _shape->GeometryLevel()->NPos();
            Vector3 pForce = dirOut * floatMin(under, radius) * areaPerPoint * 10000;

            torque += pCenter.CrossProduct(pForce);
            force += pForce;

            float contactFactor = under * 8 / radius;

            FrictionPoint& fp = frictions.Append();
            fp.frictionCoef = floatMin(contactFactor, 1);
            fp.obj = info.obj;
            fp.outDir = dirOut;
            fp.pos = info.pos;
        }
        else
        {
#if 1
            if (MAX_UNDER < under)
            {
                offset += info.dirOut * (under - MAX_UNDER);
            }
#endif

            under = floatMin(under, maxUnderForce);

            float contactFactor = outForceFactor[i];
            float accelSize = 10 * contactFactor;
            Vector3 pForce = dirOut * GetMass() * accelSize;

#if LOG_FRIC
            accelSum += accelSize;
#endif

            torque += pCenter.CrossProduct(pForce);
            force += pForce;

#if ARROWS
            AddForce(wCenter + pCenter, pForce * InvMass(), Color(1, 1, 0));
#endif

            // add friction point
            FrictionPoint& fp = frictions.Append();
            fp.frictionCoef = floatMin(contactFactor, 1);
            fp.obj = info.obj;
            fp.outDir = dirOut;
            fp.pos = info.pos;
#if LOG_FRIC
            fricFactorSum += fp.frictionCoef;
#endif
        }
    }
#if LOG_FRIC
    Log("fricFactorSum %.2f, accelSum %.2f", fricFactorSum, accelSum);
#endif
}

bool Entity::IsInLandscape() const
{
    return (MoveOutState)_moveOutState == MOIn;
}

Matrix4 Entity::WorldTransform() const
{
    if ((MoveOutState)_moveOutState == MOIn)
    {
        return Transform();
    }
    if (!_hierParent)
    {
        LOG_DEBUG(Physics, "{}: no _hierParent", (const char*)GetDebugName());
        return MIdentity;
    }
    return _hierParent->ProxyWorldTransform(this);
}

Vector3 Entity::WorldSpeed() const
{
    if ((MoveOutState)_moveOutState == MOIn)
    {
        return ObjectSpeed();
    }
    if (!_hierParent)
    {
        LOG_DEBUG(Physics, "{}: no _hierParent", (const char*)GetDebugName());
        return VZero;
    }
    return _hierParent->ObjectSpeed();
}

Matrix4 Entity::WorldInvTransform() const
{
    if ((MoveOutState)_moveOutState == MOIn)
    {
        return GetInvTransform();
    }

    if (!_hierParent)
    {
        LOG_DEBUG(Physics, "{}: no _hierParent", (const char*)GetDebugName());
        return MIdentity;
    }
    return _hierParent->ProxyInvWorldTransform(this);
}

Matrix4 Entity::ProxyWorldTransform(const Object* obj) const
{
    return Transform() * obj->Transform();
}

Matrix4 Entity::ProxyInvWorldTransform(const Object* obj) const
{
    return obj->CalcInvTransform() * GetInvTransform();
}

void Entity::ResetMoveOut()
{
    DoAssert((MoveOutState)_moveOutState == MOMovedOut);
    _moveOutState = MOIn;
}

void Entity::ResetStatus()
{
    base::ResetStatus();

    for (int i = 0; i < _animations.Size(); i++)
    {
        _animations[i].Init();
    }
}

} // namespace Poseidon
