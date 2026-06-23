#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/World/Entities/Weapons/Shots.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Random/randomGen.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/AI/VehicleAI.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/Entities/Weapons/Weapons.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Graphics/Textures/TexturePreload.hpp>
#include <Poseidon/Graphics/Rendering/Draw/SpecLods.hpp>
#include <Poseidon/World/Scene/ObjLine.hpp>

#include <Poseidon/Foundation/Enums/EnumNames.hpp>

#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/Game/Commands/GameStateExt.hpp>
#include <float.h>
#include <stdint.h>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/StreamArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>

using namespace Poseidon;
namespace Poseidon
{
RString GetMissionDirectory();
}

using Poseidon::Foundation::EnumName;

#if _ENABLE_CHEATS
#define ARROWS 1
#else
#define ARROWS 0
#endif

DEFINE_FAST_ALLOCATOR(Shot)
DEFINE_CASTING(Shot)

Shot::Shot(EntityAI* parent, const AmmoType* type) : base(type->GetShape(), type, -1), _parent(parent)
{
    if (type->GetShape() == nullptr)
    {
        Fail("No shape");
        LOG_DEBUG(Physics, "Type {}", (const char*)type->GetName());
    }
    _timeToLive = 10.0;
    SetSimulationPrecision(type->simulationStep);
}

void Shot::SetParent(EntityAI* parent)
{
    _parent = parent;
}

#ifdef NDEBUG
bool Shot::Invisible() const
{
    return _speed.Distance2(GLOB_SCENE->GetCamera()->Speed()) > Square(400);
}
#endif

void Shot::Sound(bool inside, float deltaT)
{
    const SoundPars& sound = Type()->_soundFly;
    if (!_sound && sound.name.GetLength() > 0)
    {
        _sound = GSoundScene->OpenAndPlay(sound.name, Position(), Speed());
    }
    if (_sound)
    {
        const Camera& camera = *GLOB_SCENE->GetCamera();
        Vector3 posToCamera = camera.Position() - Position();
        float speedCoef = 1;
        _sound->SetVolume(sound.vol * speedCoef, sound.freq);
        _sound->SetPosition(Position(), Speed());
    }
}

void Shot::UnloadSound()
{
    _sound.Free();
}

LSError Shot::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar))
    PARAM_CHECK(ar.SerializeRef("Parent", _parent, 1))
    return LSOK;
}

NetworkMessageType Shot::GetNMType(NetworkMessageClass cls) const
{
    switch (cls)
    {
        case NMCCreate:
            return NMTCreateShot;
        case NMCUpdateGeneric:
        case NMCUpdateDammage:
        case NMCUpdatePosition:
            return NMTNone;
        default:
            return base::GetNMType(cls);
    }
}

#define CREATE_SHOT_MSG(XX) \
	XX(OLink<EntityAI>, parent, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Owner of shot"), IdxTransferRef) \
	XX(float, timeToLive, NDTFloat, NCTNone, DEFVALUE(float, 10), DOC_MSG("Time to live (in seconds)"), IdxTransfer) \
	XX(Vector3, createPos, NDTVector, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Initial position"), IdxTransfer) \
	XX(Vector3, createSpeed, NDTVector, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Initial speed"), IdxTransfer) \
	XX(Matrix3, createOrient, NDTMatrix, NCTMatrixOrientation, DEFVALUE(Matrix3, M3Identity), DOC_MSG("Initial orientation"), IdxTransfer)

DECLARE_NET_INDICES_EX(CreateShot, CreateVehicle, CREATE_SHOT_MSG)
DEFINE_NET_INDICES_EX(CreateShot, CreateVehicle, CREATE_SHOT_MSG)
DEFINE_GET_INDICES(CreateShot)


#define UPDATE_SHOT_MSG(XX)

DECLARE_NET_INDICES_EX(UpdateShot, UpdateVehicle, UPDATE_SHOT_MSG)
DEFINE_NET_INDICES_EX(UpdateShot, UpdateVehicle, UPDATE_SHOT_MSG)
DEFINE_GET_INDICES(UpdateShot)

NetworkMessageFormat& Shot::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    Vector3 temp = VZero;
    Matrix3 tempM = M3Identity;
    switch (cls)
    {
        case NMCCreate:
            base::CreateFormat(cls, format);
            CREATE_SHOT_MSG(MSG_FORMAT)
            break;
        case NMCUpdateGeneric:
            base::CreateFormat(cls, format);
            UPDATE_SHOT_MSG(MSG_FORMAT)
            break;
        default:
            base::CreateFormat(cls, format);
            break;
    }
    return format;
}

Shot* Shot::CreateObject(NetworkMessageContext& ctx)
{
    base* veh = base::CreateObject(ctx);
    Shot* shot = dyn_cast<Shot>(veh);
    if (!shot)
    {
        return nullptr;
    }

    if (shot->TransferMsg(ctx) != TMOK)
    {
        return nullptr;
    }
    return shot;
}

TMError Shot::TransferMsg(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCCreate:
            if (ctx.IsSending())
            {
                TMCHECK(base::TransferMsg(ctx))
            }
            {
                PoseidonAssert(dynamic_cast<const IndicesCreateShot*>(ctx.GetIndices()))
                    const IndicesCreateShot* indices = static_cast<const IndicesCreateShot*>(ctx.GetIndices());

                ITRANSF_REF(parent)
                ITRANSF(timeToLive)
                TMCHECK(ctx.IdxTransfer(indices->createSpeed, _speed))
                if (ctx.IsSending())
                {
                    Vector3 pos = Position();
                    Matrix3 orient = Orientation();
                    TMCHECK(ctx.IdxTransfer(indices->createPos, pos))
                    TMCHECK(ctx.IdxTransfer(indices->createOrient, orient))
                }
                else
                {
                    Vector3 pos;
                    Matrix3 orient;
                    pos.Init();
                    TMCHECK(ctx.IdxTransfer(indices->createPos, pos))
                    TMCHECK(ctx.IdxTransfer(indices->createOrient, orient))
                    SetPosition(pos);
                    SetOrientation(orient);
                }
            }
            break;
        case NMCUpdateGeneric:
            TMCHECK(base::TransferMsg(ctx))
            break;
        default:
            TMCHECK(base::TransferMsg(ctx))
            break;
    }
    return TMOK;
}

float Shot::CalculateError(NetworkMessageContext& ctx)
{
    return base::CalculateError(ctx);
}

float Shot::GetMaxPredictionTime(NetworkMessageContext& ctx) const
{
    // no position updates are sent for shots, so long prediction is necessary
    return 100;
}

template <>
const EnumName* Poseidon::Foundation::GetEnumNames(Missile::EngineState dummy)
{
    static const EnumName EngineStateNames[] = {EnumName(Missile::Init, "INIT"), EnumName(Missile::Thrust, "THRUST"),
                                                EnumName(Missile::Fly, "FLY"), EnumName()};
    return EngineStateNames;
}

template <>
const EnumName* Poseidon::Foundation::GetEnumNames(Missile::LockState dummy)
{
    static const EnumName LockStateNames[] = {EnumName(Missile::Locked, "LOCKED"), EnumName(Missile::Lost, "LOST"),
                                              EnumName()};
    return LockStateNames;
}

LSError Missile::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar))
    PARAM_CHECK(ar.SerializeRef("Target", _target, 1))
    PARAM_CHECK(ar.Serialize("thrust", _thrust, 1))
    PARAM_CHECK(ar.SerializeEnum("engine", _engine, 1))
    PARAM_CHECK(ar.SerializeEnum("lock", _lock, 1))
    return LSOK;
}

NetworkMessageType Missile::GetNMType(NetworkMessageClass cls) const
{
    switch (cls)
    {
        case NMCCreate:
            return NMTCreateShot;
        case NMCUpdateGeneric:
        case NMCUpdateDammage:
            return base::GetNMType(cls);
        case NMCUpdatePosition:
            return Entity::GetNMType(cls);
        default:
            return base::GetNMType(cls);
    }
}

float Missile::CalculateError(NetworkMessageContext& ctx)
{
    return base::CalculateError(ctx);
}

float Missile::GetMaxPredictionTime(NetworkMessageContext& ctx) const
{
    return 100;
}

const float MinExplosion = 0.25;
const float MaxExplosion = 1;

DEFINE_FAST_ALLOCATOR(PipeBomb)
DEFINE_CASTING(PipeBomb)

PipeBomb::PipeBomb(EntityAI* parent, const AmmoType* type) : Shot(parent, type)
{
    _explosion = false;
    _timeToLive = FLT_MAX;
}

void PipeBomb::Simulate(float deltaT, SimulationImportance prec)
{
    _timeToLive -= deltaT;
    if (IsLocal() && (_explosion || _timeToLive < 0))
    {
        if (Type()->explosive)
        {
            float size = Type()->hit * 0.003;
            saturate(size, MinExplosion, MaxExplosion);
            Explosion* explosion = new Explosion(nullptr, _parent, size);
            explosion->SetPosition(Position());
            GLOB_WORLD->AddAnimal(explosion);
            GetNetworkManager().CreateVehicle(explosion, VLTAnimal, "", -1);
        }
        GLandscape->ExplosionDammage(_parent, this, nullptr, Position(), VUp, Type());
        _delete = true;
    }
}

DEFINE_FAST_ALLOCATOR(TimeBomb)
DEFINE_CASTING(TimeBomb)

TimeBomb::TimeBomb(EntityAI* parent, const AmmoType* type) : Shot(parent, type)
{
    _timeToLive = 20;
}

void TimeBomb::Simulate(float deltaT, SimulationImportance prec)
{
    _timeToLive -= deltaT;
    if (IsLocal() && _timeToLive < 0)
    {
        if (Type()->explosive)
        {
            float size = Type()->hit * 0.003;
            saturate(size, MinExplosion, MaxExplosion);
            Explosion* explosion = new Explosion(nullptr, _parent, size);
            explosion->SetPosition(Position());
            GLOB_WORLD->AddAnimal(explosion);
            GetNetworkManager().CreateVehicle(explosion, VLTAnimal, "", -1);
        }
        GLandscape->ExplosionDammage(_parent, this, nullptr, Position(), VUp, Type());
        _delete = true;
    }
}

DEFINE_FAST_ALLOCATOR(Mine)
DEFINE_CASTING(Mine)

Mine::Mine(EntityAI* parent, const AmmoType* type) : Shot(parent, type)
{
    _timeToLive = GRandGen.PlusMinus(0.5, 0.1);
    {
        const ParamEntry& pars = _type->GetParamEntry();
        const ParamEntry* entry = pars.FindEntry("activationTime");
        if (entry)
        {
            const float activationTime = *entry;
            if (activationTime >= 0)
            {
                _timeToLive = GRandGen.PlusMinus(activationTime + 0.2, 0.1);
            }
        }
    }
    _active = true;
}

void Mine::Simulate(float deltaT, SimulationImportance prec)
{
    if (!_active)
    {
        // tilt aside to indicate mine is no longer active
        float bank = DirectionAside().Y();
        float bankWanted = 0.5;
        if (bank >= bankWanted)
        {
            return;
        }
        float delta = bankWanted - bank;
        saturate(delta, -deltaT * 0.5f, +deltaT * 0.5f);
        Matrix3 rotZ(MRotationZ, delta);

        Matrix3 newOrient = Orientation() * rotZ;
        SetOrientation(newOrient);
        return;
    }

    _timeToLive -= deltaT;
    if (IsLocal() && _timeToLive < 0)
    {
        float activationMass = 10000.0;
        {
            const ParamEntry& pars = _type->GetParamEntry();
            const ParamEntry* entry = pars.FindEntry("activationMass");
            if (entry)
            {
                activationMass = *entry;
            }
        }
        float activationDistance = Square(6.0);
        {
            // activationDistance config value is the square of the actual distance
            const ParamEntry& pars = _type->GetParamEntry();
            const ParamEntry* entry = pars.FindEntry("activationDistance");
            if (entry)
            {
                activationDistance = *entry;
            }
        }

        bool found = false;
        for (int i = 0; i < GWorld->NVehicles(); i++)
        {
            Vehicle* veh = GWorld->GetVehicle(i);
            if (!veh)
            {
                continue;
            }
            if (veh->GetMass() < activationMass)
            {
                continue;
            }
            if (Position().Distance2(veh->Position()) > activationDistance)
            {
                continue;
            }
            found = true;
            break;
        }
        if (found)
        {
            if (Type()->explosive)
            {
                float size = Type()->hit * 0.003;
                saturate(size, MinExplosion, MaxExplosion);
                Explosion* explosion = new Explosion(nullptr, _parent, size);
                explosion->SetPosition(Position());
                GLOB_WORLD->AddAnimal(explosion);
                GetNetworkManager().CreateVehicle(explosion, VLTAnimal, "", -1);
            }
            GLandscape->ExplosionDammage(_parent, this, nullptr, Position(), VUp, Type());
            _delete = true;
        }
        else
        {
            _timeToLive = GRandGen.PlusMinus(0.5, 0.1);
        }
    }
}

LSError Mine::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar))
    PARAM_CHECK(ar.Serialize("active", _active, 1, true))
    return LSOK;
}

NetworkMessageType Mine::GetNMType(NetworkMessageClass cls) const
{
    switch (cls)
    {
        case NMCCreate:
            return NMTCreateShot;
        case NMCUpdateGeneric:
            return NMTUpdateMine;
        case NMCUpdateDammage:
        case NMCUpdatePosition:
            return NMTNone;
        default:
            return base::GetNMType(cls);
    }
}

#define UPDATE_MINE_MSG(XX) \
	XX(bool, active, NDTBool, NCTNone, DEFVALUE(bool, true), DOC_MSG("Mine is active (can explode)"), IdxTransfer, ET_NOT_EQUAL, ERR_COEF_MODE)

DECLARE_NET_INDICES_EX_ERR(UpdateMine, UpdateShot, UPDATE_MINE_MSG)
DEFINE_NET_INDICES_EX_ERR(UpdateMine, UpdateShot, UPDATE_MINE_MSG)
DEFINE_GET_INDICES(UpdateMine)

NetworkMessageFormat& Mine::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    switch (cls)
    {
        case NMCUpdateGeneric:
            base::CreateFormat(cls, format);
            UPDATE_MINE_MSG(MSG_FORMAT_ERR)
            break;
        default:
            base::CreateFormat(cls, format);
            break;
    }
    return format;
}

TMError Mine::TransferMsg(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
            TMCHECK(base::TransferMsg(ctx))
            {
                PoseidonAssert(dynamic_cast<const IndicesUpdateMine*>(ctx.GetIndices()))
                    const IndicesUpdateMine* indices = static_cast<const IndicesUpdateMine*>(ctx.GetIndices());
                ITRANSF(active)
            }
            break;
        default:
            TMCHECK(base::TransferMsg(ctx))
            break;
    }
    return TMOK;
}

float Mine::CalculateError(NetworkMessageContext& ctx)
{
    float error = 0;
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
            error += base::CalculateError(ctx);
            {
                PoseidonAssert(dynamic_cast<const IndicesUpdateMine*>(ctx.GetIndices()))
                    const IndicesUpdateMine* indices = static_cast<const IndicesUpdateMine*>(ctx.GetIndices());

                ICALCERR_NEQ(bool, active, ERR_COEF_MODE)
            }
            break;
        default:
            error += base::CalculateError(ctx);
            break;
    }
    return error;
}

DEFINE_FAST_ALLOCATOR(ShotShell)
DEFINE_CASTING(ShotShell)

ShotShell::ShotShell(EntityAI* parent, const AmmoType* type) : base(parent, type)
{
    _initDelay = type->initTime;

    _timeToLive = 20.0;
    _airFriction = -0.0005;

    _coefGravity = 1;

    const ParamEntry& pars = type->GetParamEntry();
    const ParamEntry* entry = pars.FindEntry("coefGravity");
    if (entry)
    {
        _coefGravity = *entry;
    }

    // support assigning timeToLive attribute in config
    // only support edit shell's as in 2.01. Extend this for other simulations if necessary
    {
        const ParamEntry* entry = pars.FindEntry("timeToLive");
        if (entry)
        {
            _timeToLive = *entry;
        }
    }
}

bool ShotShell::Invisible() const
{
    if (_initDelay > 0)
    {
        return true;
    }
    return base::Invisible();
}

void ShotShell::Sound(bool inside, float deltaT)
{
    if (_initDelay > 0)
    {
        return;
    }
    base::Sound(inside, deltaT);
}

void ShotShell::Simulate(float deltaT, SimulationImportance prec)
{
    Point3 position = Position();

    Vector3 accel = _speed * (_speed.Size() * _airFriction);
    accel[1] -= _coefGravity * G_CONST;

    _initDelay -= deltaT;
    if (_initDelay <= 0)
    {
        _timeToLive -= deltaT;
        if (_timeToLive < 0)
        {
            _delete = true;
            return;
        }
    }

    if (_initDelay <= 0)
    {
        position += _speed * deltaT;
    }

    if (deltaT > 0)
    {
        // test collision with objects
        CollisionBuffer collision;
        GLandscape->ObjectCollision(collision, this, _parent, Position(), position, 0);
        if (collision.Size() > 0)
        {
            float minT = 1e10;
            int minI = -1;

            Texture* glass = GPreloadedTextures.New(TextureBlack);
            bool detectGlass = dyn_cast<ShotBullet>(this) != nullptr;

            for (int i = 0; i < collision.Size(); i++)
            {
                CollisionInfo& info = collision[i];
                if (detectGlass && info.texture == glass)
                {
                    continue;
                }
                if (info.object)
                {
                    if (minT > info.under)
                    {
                        minT = info.under, minI = i;
                    }
                }
            }
            if (IsLocal())
            {
                for (int i = 0; i < collision.Size(); i++)
                {
                    CollisionInfo& info = collision[i];
                    if (detectGlass && info.texture != glass)
                    {
                        continue;
                    }
                    // stop on first solid obstacle
                    if (info.under > minT)
                    {
                        continue;
                    }
                    if (!info.object)
                    {
                        continue;
                    }
                    // dammage corresponding component (if any)
                    info.object->DirectLocalHit(info.component, Type()->hit //*info.object->GetInvArmor()
                    );
                }
            }

            if (minI >= 0)
            {
                if (IsLocal())
                {
                    CollisionInfo& info = collision[minI];
                    Point3 pos = info.object->PositionModelToTop(info.pos);

                    if (Type()->hit > 50)
                    {
                        Vector3 ePos = pos;
                        float size = Type()->hit * 0.001;
                        saturate(size, MinExplosion, MaxExplosion);
                        float minY = GLandscape->RoadSurfaceY(ePos[0], ePos[2]) + 2 * size;
                        if (ePos[1] < minY)
                        {
                            ePos[1] = minY;
                        }
                        Explosion* explosion = new Explosion(nullptr, _parent, size);
                        explosion->SetPosition(ePos);
                        GLOB_WORLD->AddAnimal(explosion);
                        GetNetworkManager().CreateVehicle(explosion, VLTAnimal, "", -1);
                    }
                    GLandscape->ExplosionDammage(_parent, this, info.object, pos, info.dirOut, Type());
                }
                _delete = true;
                return;
            }
        }

        Vector3Val lPos = Position();
        Vector3 lDir = position - lPos;
        if (lDir.SquareSize() > 0)
        {
            Vector3 lDirNorm = lDir.Normalized();
            float maxDist = lDir * lDirNorm;

            Vector3 isect;
            float t = GLandscape->IntersectWithGroundOrSea(&isect, lPos, lDirNorm, 0, maxDist * 1.1);
            if (t <= maxDist)
            {
                position = isect;

                if (IsLocal())
                {
                    Vector3 exploPos = position;
                    if (Type()->explosive)
                    {
                        float size = Type()->hit * 0.003;
                        saturate(size, MinExplosion, MaxExplosion);
                        exploPos[1] += 0.5 * size;
                        Explosion* explosion = new Explosion(nullptr, _parent, size);
                        explosion->SetPosition(exploPos);
                        GLOB_WORLD->AddAnimal(explosion);
                        GetNetworkManager().CreateVehicle(explosion, VLTAnimal, "", -1);
                    }
                    GLandscape->ExplosionDammage(_parent, this, nullptr, position, VUp, Type());
                }
                _delete = true;
                return;
            }
        }
    }

    if (_initDelay <= 0)
    {
        _speed += accel * deltaT;
    }

    Move(position);
}

void ShotBullet::Simulate(float deltaT, SimulationImportance prec)
{
    base::Simulate(deltaT, prec);
    SetEnd(Position());
}

void ShotBullet::Sound(bool inside, float deltaT)
{
    base::Sound(inside, deltaT);
}

DEFINE_FAST_ALLOCATOR(Missile)
DEFINE_CASTING(Missile)

Missile::Missile(EntityAI* parent, const AmmoType* type, Object* target)
    : Shot(parent, type),

      _lock(Locked), _engine(Init), _initTime(type->initTime), _thrustTime(type->thrustTime),
      _controlDirectionSet(false), _lightColor(0.7, 0.8, 1.0), _target(target)
//_cloudlets(GLOB_SCENE->Preloaded(CloudletMissile),0.01)
{
    _thrust = type->thrust;
    if (type->manualControl)
    {
        _cloudlets.Load(Pars >> "CfgCloudlets" >> "CloudletsMissileManual");
    }
    else
    {
        _cloudlets.Load(Pars >> "CfgCloudlets" >> "CloudletsMissile");
    }
    if (type->maxControlRange < 1 || !target && !type->manualControl)
    {
        _lock = Lost;
    }
    if (_thrustTime <= 0)
    {
        // free fall bombs should have very high time to live
        _timeToLive = 120;
    }
}

void Missile::SetLight(ColorVal color)
{
    _lightColor = color;
}

void Missile::SetControlDirection(Vector3 dir)
{
    _controlDirection = dir;     // manual missile control
    _controlDirectionSet = true; // manual control activated
}

inline bool FaceIsShining(const Poly& f, const Shape* shape)
{
    for (int v = 0; v < f.N(); v++)
    {
        ClipFlags clip = shape->Clip(f.GetVertex(v));
        if ((clip & ClipUserMask) != static_cast<uint32_t>(MSShining) * ClipUserStep)
        {
            return false;
        }
    }
    return true;
}

void Missile::Animate(int level)
{
    Shape* shape = _shape->Level(level);
    if (_thrustTime <= 0)
    {
        for (Offset o = shape->BeginFaces(); o < shape->EndFaces(); shape->NextFace(o))
        {
            Poly& f = shape->Face(o);
            if (!FaceIsShining(f, shape))
            {
                continue;
            }
            f.OrSpecial(IsHidden);
        }
        for (int i = 0; i < shape->NSections(); i++)
        {
            ShapeSection& sec = shape->GetSection(i);
            if (sec.material == MSShining)
            {
                sec.properties.OrSpecial(IsHidden);
            }
        }
    }
}

void Missile::Deanimate(int level)
{
    Shape* shape = _shape->Level(level);
    if (_thrustTime <= 0)
    {
        for (Offset o = shape->BeginFaces(); o < shape->EndFaces(); shape->NextFace(o))
        {
            Poly& f = shape->Face(o);
            if (!FaceIsShining(f, shape))
            {
                continue;
            }
            f.AndSpecial(!IsHidden);
        }
        for (int i = 0; i < shape->NSections(); i++)
        {
            ShapeSection& sec = shape->GetSection(i);
            if (sec.material == MSShining)
            {
                sec.properties.AndSpecial(~IsHidden);
            }
        }
    }
}

void Missile::Simulate(float deltaT, SimulationImportance prec)
{
    Vector3 force(VZero), torque(VZero);
    Vector3 friction(VZero), torqueFriction(VZero);
    Vector3 pForce(VZero), pCenter(VZero);
    Vector3Val position = Position();
    Vector3Val speed = ModelSpeed();
    float mass = GetMass();
    pForce[0] = speed[0] * speed[0] * speed[0] * 5e-4f + speed[0] * fabs(speed[0]) * 10.0f + speed[0] * 10;
    pForce[1] = speed[1] * speed[1] * speed[1] * 5e-4f + speed[1] * fabs(speed[1]) * 10.0f + speed[1] * 10;
    pForce[2] = speed[2] * speed[2] * speed[2] * 1e-5f + speed[2] * fabs(speed[2]) * 0.01f + speed[2] * 2;
    pForce[0] *= Type()->sideAirFriction;
    pForce[1] *= Type()->sideAirFriction;
    pForce[2] *= Type()->sideAirFriction;
    pForce *= mass * (1.0f / 10);

    bool freeFall = Type()->thrustTime <= 0;

#if ARROWS
    Vector3 wCenter(VFastTransform, ModelToWorld(), GetCenterOfMass());
#endif

    if (freeFall)
    {
        // aerodynamic instability aligns direction with speed
        pForce[0] *= 0.1f;
        pForce[1] *= 0.1f;
        pCenter = Vector3(0, 0, +0.3f);
        torque += pCenter.CrossProduct(pForce);

#if ARROWS
        AddForce(wCenter + DirectionModelToWorld(pCenter), DirectionModelToWorld(-pForce * InvMass()), Color(1, 0, 0));
#endif
    }
    else
    {
#if ARROWS
        AddForce(wCenter, DirectionModelToWorld(-pForce * InvMass()), Color(1, 0, 0));
#endif
    }

    friction += pForce;

    if (freeFall)
    {
        // drag force; only applied to free-fall bombs (late addition — adding to all missiles would change behaviour)
        pForce[0] = speed[0] * fabs(speed[0]) * -0.00033f + speed[0] * -0.005f;
        pForce[1] = speed[1] * fabs(speed[1]) * -0.00033f + speed[1] * -0.005f;
        pForce[2] = 0;
        pForce *= mass;
        force += pForce;

#if ARROWS
        AddForce(Position(), pForce * InvMass(), Color(1, 1, 0));
#endif
    }

    switch (_engine)
    {
        case Init:
            _initTime -= deltaT;
            if (_initTime < 0)
            {
                if (_thrustTime > 0)
                {
                    _engine = Thrust;
                }
                else
                {
                    _engine = Fly;
                }
            }
            break;
        case Thrust:
        {
            Point3 backPos = PositionModelToWorld(Vector3(0, 0, -0.5));
            _thrustTime -= deltaT;
            if (_thrustTime < 0)
            {
                _engine = Fly;
            }
            Vector3 cSpeed = Speed() * 0.1;
            const float maxCSpeed = 30;
            if (cSpeed.SquareSize() > Square(maxCSpeed))
            {
                cSpeed = cSpeed.Normalized() * maxCSpeed;
            }
            _cloudlets.Simulate(backPos, cSpeed, deltaT);

            float fade = 1;
            const AmmoType* type = Type();
            if (4 * _thrustTime < type->thrustTime)
            {
                fade = 4 * _thrustTime / type->thrustTime;
            }

            pForce = Vector3(0, 0, _thrust * fade) * mass;
            force += pForce;

            if (ENGINE_CONFIG.lights & LIGHT_MISSILE)
            {
                if (!_light)
                {
                    _light = new LightPointOnVehicle(GLOB_SCENE->Preloaded(SphereLight), _lightColor, Color(HBlack),
                                                     this, Vector3(0, 0, -0.5));
                    GLOB_SCENE->AddLight(_light);
                }
                if (_light)
                {
                    _light->SetDiffuse(_lightColor * fade);
                }
            }
        }
        break;
        case Fly:
            _light.Free();
            break;
    }

    if (_lock == Locked && _target && !_target->LockPossible(Type()))
    {
        _lock = Lost;
    }
    bool forceExplosion = false;
    if (_lock == Locked)
    {
        Vector3 cmdDir;
        float estT = 0.3;
        if (_controlDirectionSet && !_target)
        {
            cmdDir = _controlDirection - Position();
        }
        else if (_target)
        {
            Vector3 pos = _target->AimingPosition();
            float dist = pos.Distance(position);
            float estSpeed = (Type()->maxSpeed + speed.Z()) * 0.5f;
            float time = dist / floatMax(speed.Z(), estSpeed);
            // lead the target
            pos += time * _target->ObjectSpeed();
            if (freeFall)
            {
                pos[1] += pos.DistanceXZ(position) * 0.2f;
            }
            cmdDir = pos - Position();
            estT = floatMin(0.3f, time);
        }

        {
            Matrix3Val orientation = Orientation();

            float dFactor = Type()->maneuvrability * 0.3f;
            Vector3 rDir;
            Matrix3 estOrientation = orientation;
            if (freeFall)
            {
                Matrix3Val derOrientation = _angVelocity.Tilda() * orientation;
                Matrix3 estOrientation = orientation + derOrientation * estT;
                estOrientation.Orthogonalize();
            }
            Matrix3Val invEstOrientation = estOrientation.InverseRotation();
            Vector3Val rSpeed = invEstOrientation * (Speed() + estT * Acceleration());
            Vector3Val rPos = invEstOrientation * cmdDir;
            if (!freeFall)
            {
                saturate(dFactor, 0.5f, 0.95f);
                rDir = rSpeed.Normalized() * dFactor + VForward * (1 - dFactor);
            }
            else
            {
                saturate(dFactor, 0.1f, 0.5f);
                rDir = rSpeed.Normalized() * dFactor + VForward * (1 - dFactor);
            }

            Vector3 rdn = rDir.Normalized();
            Vector3 rpn = rPos.Normalized();

            float up = (rpn.Y() - rdn.Y()) * 20;
            float left = (rpn.X() - rdn.X()) * 20;

            if (speed[2] < 30)
            {
                up = left = 0; // disable controls when flying slow
            }
            if (Type()->manualControl)
            {
                if (!_parent ||            // controlling vehicle is no longer able to control
                    !_parent->CanFire() || // controlling vehicle too far
                    _parent->Position().Distance2(Position()) >= Square(Type()->maxControlRange))
                {
                    // stop rotation (once only)
                    _angMomentum = VZero;
                    up = left = 0;
                    _lock = Lost;
                }
            }
            else
            {
                if (_target)
                {
                    Vector3 relPos = PositionWorldToModel(_target->Position());
                    if (relPos.Z() < 0 || fabs(relPos.X()) > relPos.Z() * 1.5f || fabs(relPos.Y()) > relPos.Z() * 1.5f)
                    {
                        // target is behind or outside lock cone
                        _angMomentum = VZero;
                        up = left = 0;
                        _lock = Lost;
                    }
                }
            }
            float turn = speed[2] * (1.0 / 50);
            saturate(turn, 0.1, 3);
            float invTurn = 3 / turn;
            up *= invTurn;
            left *= invTurn;

            float maxMan = Type()->maneuvrability * 0.25;
            saturate(up, -maxMan, +maxMan);
            saturate(left, -maxMan, +maxMan);

            Vector3 pCenter = Vector3(0, 0, Type()->maneuvrability * 0.04f);
            Vector3 pForce = mass * turn * Vector3(left, up, 0);
            torque += pCenter.CrossProduct(pForce);

            if (freeFall)
            {
                force += pForce;
            }

#if ARROWS
            AddForce(PositionModelToWorld(pCenter), DirectionModelToWorld(pForce * InvMass()), Color(0, 0, 1));
#endif
        }
    }
    DirectionModelToWorld(friction, friction);
    DirectionModelToWorld(force, force);
    DirectionModelToWorld(torque, torque);

    torqueFriction = _angMomentum * 5.0;

    pForce = Vector3(0, -G_CONST, 0) * mass;
    force += pForce;

    Matrix4 movePos;
    ApplySpeed(movePos, deltaT);
    Frame moveTrans;
    moveTrans.SetTransform(movePos);

    if (IsLocal() && deltaT > 0)
    {
        CollisionBuffer collision;
        GLandscape->ObjectCollision(collision, this, _parent, Position(), moveTrans.Position(), 0);
        if (collision.Size() > 0)
        {
            float minT = 1e10;
            int minI = -1;
            for (int i = 0; i < collision.Size(); i++)
            {
                const CollisionInfo& info = collision[i];
                if (info.object)
                {
                    if (minT > info.under)
                    {
                        minT = info.under, minI = i;
                    }
                }
            }

            if (minI >= 0)
            {
                const CollisionInfo& info = collision[minI];
                Point3 pos = info.object->PositionModelToWorld(info.pos);
                float size = Type()->hit * 0.003;
                saturate(size, MinExplosion, MaxExplosion);
                Explosion* explosion = new Explosion(nullptr, _parent, size);
                pos -= Direction() * 2 * size;
                float minY = GLandscape->RoadSurfaceY(position[0], position[2]) + 3 * size;
                if (pos[1] < minY)
                {
                    pos[1] = minY;
                }
                explosion->SetPosition(pos);
                GLOB_WORLD->AddAnimal(explosion);
                GetNetworkManager().CreateVehicle(explosion, VLTAnimal, "", -1);
                GLandscape->ExplosionDammage(_parent, this, info.object, pos, info.dirOut, Type());
                _delete = true;
#if _ENABLE_CHEATS
                if (this == GWorld->CameraOn() && _parent)
                {
                    GWorld->SwitchCameraTo(_parent, CamInternal);
                }
#endif
                return;
            }
        }
    }

    _timeToLive -= deltaT;

    if (IsLocal())
    {
        if (_timeToLive < 0)
        {
            forceExplosion = true;
        }

        Vector3 lPos = position;
        Vector3 lDir = movePos.Position() - lPos;
        if (lDir.SquareSize() > 0)
        {
            Vector3 lDirNorm = lDir.Normalized();
            float maxDist = lDirNorm * lDir;
            Vector3 isect;
            float t = GLandscape->IntersectWithGroundOrSea(&isect, lPos, lDirNorm, 0, maxDist * 1.1);
            if (t < maxDist)
            {
                lPos = isect;
                forceExplosion = true;
            }
        }

        if (forceExplosion)
        {
            float size = Type()->hit * 0.003;
            saturate(size, MinExplosion, MaxExplosion);
            Explosion* explosion = new Explosion(nullptr, _parent, size);
            explosion->SetPosition(lPos);
            GLOB_WORLD->AddAnimal(explosion);
            GetNetworkManager().CreateVehicle(explosion, VLTAnimal, "", -1);
            GLandscape->ExplosionDammage(_parent, this, nullptr, lPos, VUp, Type());
            _delete = true;
#if _ENABLE_CHEATS
            if (this == GWorld->CameraOn() && _parent)
            {
                GWorld->SwitchCameraTo(_parent, CamInternal);
            }
#endif
            return;
        }
    }

    Move(moveTrans);
    DirectionWorldToModel(_modelSpeed, _speed);

    ApplyForces(deltaT, force, torque, friction, torqueFriction);
}

void Missile::Sound(bool inside, float deltaT)
{
    const SoundPars& sound = Type()->_soundEngine;
    if (_engine == Thrust)
    {
        if (!_soundEngine && sound.name.GetLength() > 0)
        {
            _soundEngine = GSoundScene->OpenAndPlay(sound.name, Position(), Speed());
        }
        if (_soundEngine)
        {
            float coef = _thrust * (1.0 / 800);
            _soundEngine->SetVolume(sound.vol * coef, sound.freq);
            _soundEngine->SetPosition(Position(), Speed());
        }
    }
    else
    {
        _soundEngine.Free();
    }
    base::Sound(inside, deltaT);
}

void Missile::UnloadSound()
{
    _soundEngine.Free();
    base::UnloadSound();
}

Missile::~Missile() = default;

DEFINE_CASTING(IlluminatingShell)
DEFINE_FAST_ALLOCATOR(IlluminatingShell)

#define ILL_TTL 17.0F
#define ILL_EXPL 2.0F

IlluminatingShell::IlluminatingShell(EntityAI* parent, const AmmoType* type)
    : base(parent, type), _lightColor(1.0, 1.0, 1.0)
{
    _timeToLive = ILL_TTL;
    _airFriction = -0.0005;

    const ParamEntry& cls = *type->_par;
    _lightColor = GetColor(cls >> "lightColor");
}

void IlluminatingShell::Simulate(float deltaT, SimulationImportance prec)
{
    base::Simulate(deltaT, prec);
    if (!_light && _timeToLive <= ILL_TTL - ILL_EXPL)
    {
        _light = new LightPointOnVehicle(GLOB_SCENE->Preloaded(SphereLight), _lightColor, Color(HBlack), this,
                                         Vector3(0, 0, -0.5));
        _light->SetBrightness(2);
        GLOB_SCENE->AddLight(_light);

        _airFriction = -0.2;

        RString name = RString("onFlare.sqs");
        if (QIFStreamB::FileExist(Poseidon::GetMissionDirectory() + name))
        {
            GameArrayType color;
            color.Add(_lightColor.R());
            color.Add(_lightColor.G());
            color.Add(_lightColor.B());
            GameArrayType arguments;
            arguments.Add(color);
            arguments.Add(GameValueExt(_parent));

            Script* script = new Script(name, arguments);
            GWorld->AddScript(script);
            GWorld->SimulateScripts();
        }
    }
    float fade = GRandGen.PlusMinus(0.8, 0.2);
    if (_light)
    {
        _light->SetDiffuse(_lightColor * fade);
    }
}

void IlluminatingShell::SetLight(ColorVal color)
{
    _lightColor = color;
}

NetworkMessageType IlluminatingShell::GetNMType(NetworkMessageClass cls) const
{
    switch (cls)
    {
        case NMCCreate:
            return NMTCreateShot;
        case NMCUpdateGeneric:
        case NMCUpdateDammage:
            return base::GetNMType(cls);
        case NMCUpdatePosition:
            return Entity::GetNMType(cls);
        default:
            return base::GetNMType(cls);
    }
}

LSError IlluminatingShell::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar))
    PARAM_CHECK(ar.Serialize("lightColor", _lightColor, 1))
    return LSOK;
}

DEFINE_CASTING(SmokeShell)
DEFINE_FAST_ALLOCATOR(SmokeShell)

#define SMOKE_TTL 60.0F

SmokeShell::SmokeShell(EntityAI* parent, const AmmoType* type) : base(parent, type)
{
    _timeToLive = SMOKE_TTL;
    _airFriction = -0.0005;

    const ParamEntry& cls = *type->_par;
    _smoke.Load(cls >> "Smoke");
    _smoke.SetColor(GetColor(cls >> "smokeColor"));
}

void SmokeShell::Simulate(float deltaT, SimulationImportance prec)
{
    Point3 position = Position();

    Vector3 accel = _speed * (_speed.Size() * _airFriction);
    accel[1] -= G_CONST;

    float surfaceY = GLandscape->SurfaceY(position[0], position[2]);

    _initDelay -= deltaT;
    if (_initDelay <= 0)
    {
        _timeToLive -= deltaT;
        if (_timeToLive < 0)
        {
            _delete = true;
        }

        position += _speed * deltaT;
        if (position[1] > surfaceY + 1e-3)
        {
            _speed += accel * deltaT;
        }
    }

    if (position[1] < surfaceY)
    {
        position[1] = surfaceY;
        _speed = VZero;
    }
    if (deltaT > 0)
    {
        CollisionBuffer collision;
        GLandscape->ObjectCollision(collision, this, _parent, Position(), position, 0);

        int nCol = 0;
        for (int i = 0; i < collision.Size(); i++)
        {
            // info.pos is relative to object
            const CollisionInfo& info = collision[i];
            if (info.object)
            {
                nCol++;
            }
        }
        if (nCol > 0)
        {
            position[1] = surfaceY;
            _speed = VZero;
        }
    }

    Move(position);
    _smoke.Simulate(Position(), Speed(), deltaT, prec);
}

LSError SmokeShell::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar))
    return LSOK;
}

DEFINE_FAST_ALLOCATOR(ShotBullet)
DEFINE_CASTING(ShotBullet)

ShotBullet::ShotBullet(EntityAI* parent, const AmmoType* type) : ShotShell(parent, type)
{
    _timeToLive = 3;
    _shape = GScene->Preloaded(BulletLine);

    _beg = VZero;
    _end = VZero;

    float width = type->hit * (1.0 / 20);
    saturate(width, 0.1, 2);

    PackedColor color = type->_tracerColor;
    if (!USER_CONFIG.IsEnabled(DTTracers))
    {
        color = type->_tracerColorR;
    }
    float a8 = color.A8() * width;
    saturate(a8, 0, 255);
    color.SetA8(toInt(a8));
    SetConstantColor(color);
}

void ShotBullet::SetBeg(Vector3Val beg)
{
    _beg = beg;
}

void ShotBullet::SetEnd(Vector3Val end)
{
    if (_beg.SquareSize() <= 1e-6)
    {
        _beg = end;
    }
    _end = end;
}

void ShotBullet::StartFrame()
{
    if (_end.SquareSize() < 1e-6)
    {
        _end = Position();
    }
    SetBeg(_end);
}

inline void ShotBullet::DoDraw(int level, ClipFlags clipFlags, const FrameBase& pos)
{
    if (_beg.Distance2(_end) < 0.1)
    {
        return;
    }

    DrawLines(level, clipFlags, *this);
}

int ShotBullet::PassNum(int lod)
{
    return 2; // alpha pass
}

bool ShotBullet::IsAnimated(int level) const
{
    return true;
}

bool ShotBullet::IsAnimatedShadow(int level) const
{
    return false;
}

void ShotBullet::Animate(int level)
{
    ObjectLine::SetPos(_shape, PositionWorldToModel(_beg), PositionWorldToModel(_end));
}

void ShotBullet::Deanimate(int level)
{
    ObjectLine::SetPos(_shape, VZero, VForward);
}

void ShotBullet::AnimatedMinMax(int level, Vector3* minMax)
{
    Shape* shape = _shape->Level(level);
    PoseidonAssert(shape->NVertex() == 2);
    Vector3 v0 = shape->Pos(0);
    Vector3 v1 = shape->Pos(1);
    minMax[0] = v0, minMax[1] = v0;
    CheckMinMax(minMax[0], minMax[1], v1);
}
void ShotBullet::AnimatedBSphere(int level, Vector3& bCenter, float& bRadius, bool isAnimated)
{
    Shape* shape = _shape->Level(level);
    PoseidonAssert(shape->NVertex() == 2);
    Vector3 v0 = shape->Pos(0);
    Vector3 v1 = shape->Pos(1);
    bCenter = (v0 + v1) * 0.5f;
    bRadius = v0.Distance(v1) * 0.5f;
}

void ShotBullet::Draw(int level, ClipFlags clipFlags, const FrameBase& pos)
{
#if !ALPHA_SPLIT
    DoDraw(level, clipFlags, pos);
#endif
}

#if ALPHA_SPLIT
void ShotBullet::DrawAlpha(int level, ClipFlags clipFlags, const FrameBase& pos)
{
    DoDraw(level, clipFlags, pos);
}
#endif

namespace Poseidon
{
Shot* NewShot(EntityAI* parent, const AmmoType* type, Object* target)
{
    Shot* v = nullptr;
    type->VehicleAddRef();
    switch (type->_simulation)
    {
        case AmmoShotShell:
            v = new ShotShell(parent, type);
            break;
        case AmmoShotMissile:
            v = new Missile(parent, type, target);
            break;
        case AmmoShotRocket:
            v = new Missile(parent, type, target);
            break;
        case AmmoShotBullet:
            v = new ShotBullet(parent, type);
            break;
        case AmmoShotIlluminating:
            v = new IlluminatingShell(parent, type);
            break;
        case AmmoShotSmoke:
            v = new SmokeShell(parent, type);
            break;
        case AmmoShotTimeBomb:
            v = new TimeBomb(parent, type);
            break;
        case AmmoShotPipeBomb:
            v = new PipeBomb(parent, type);
            break;
        case AmmoShotMine:
            v = new Mine(parent, type);
            break;
        default:
            LOG_ERROR(Physics, "Unsupported ammo type (type name {})", (const char*)type->GetName());
            return nullptr;
    }
    type->VehicleRelease();
    return v;
}
} // namespace Poseidon

EntityAI* Shot::GetOwner() const
{
    return _parent;
}
