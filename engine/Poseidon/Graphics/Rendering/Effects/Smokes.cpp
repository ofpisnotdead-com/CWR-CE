
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Graphics/Rendering/Effects/Smokes.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/AI/VehicleAI.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Random/randomGen.hpp>
#include <Poseidon/World/Entities/Weapons/Weapons.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Game/Commands/GameStateExt.hpp>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/BoolArray.hpp>
#include <Poseidon/Foundation/Containers/StreamArray.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>

using namespace Poseidon;
namespace Poseidon
{
using Poseidon::Foundation::Time;

#define DEBUG_NAME(x) (x) ? (const char*)(x)->GetDebugName() : "<null>"

DEFINE_FAST_ALLOCATOR(Smoke)

static VehicleNonAIType* SmokeType()
{
    static Ref<VehicleNonAIType> type;
    if (type)
    {
        return type;
    }
    type = VehicleTypes.New("smoke");
    return type;
}

Smoke::Smoke(LODShapeWithShadow* shape, VehicleNonAIType* type, float duration, float loopedDuration)
    : Vehicle(shape, type, -1), _animation(0), _animationSpeed(1 / duration), _invisible(false), _firstLoop(true),
      _timeToLive(loopedDuration ? loopedDuration : 0), _alpha(1.0), _fadeValue(0), _fadeIn(0), _fadeInTime(0),
      _fadeInInv(1000), _fadeOut(0), _fadeOutTime(0), _fadeOutInv(1000)
{
    Object::_type = TypeTempVehicle;
    _constantColor = PackedColorRGB(_constantColor, 0);
    SetSimulationPrecision(1.0 / 5);
}

Smoke::~Smoke() = default;

SmokeSource::SmokeSource(LODShapeWithShadow* shape, float density, float size)
    : CloudletSource(shape, 0.3 / density), _density(density), _size(size), _sourceSize(size), _inOutDensity(1),
      _timeToLive(30), _color(HWhite), _in(1), _inTime(0), _inInv(1), _out(1), _outTime(1), _outInv(1),
      _speed(Vector3(0, 1.5 + size * 2, 0))
{
    CloudletSource::SetSize(size);
}

const float MinGeneralize = 1.0;

} // namespace Poseidon
bool EnableVisualEffects(const Poseidon::Foundation::Vector3P& effPos, SimulationImportance prec);
bool GetPos(Poseidon::Foundation::Vector3P&, const class GameValue&);
namespace Poseidon
{

bool SmokeSource::Simulate(Vector3Par pos, Vector3Par speed, float deltaT, SimulationImportance prec)
{
    if (_inTime > 0)
    {
        _inTime -= deltaT;
        _inOutDensity = 1 - _inTime * _inInv;
    }
    else if (_timeToLive > 0)
    {
        _timeToLive -= deltaT;
        _inOutDensity = 1;
    }
    else if (_outTime > 0)
    {
        _outTime -= deltaT;
        _inOutDensity = _outTime * _outInv;
    }
    else
    {
        return true;
    }
    if (EnableVisualEffects(pos, prec))
    {
        _nextTime -= deltaT;
        while (_nextTime <= 0)
        {
            const Camera& camera = *GLOB_SCENE->GetCamera();
            float dist = camera.Position().Distance(pos);
            float invZoom = camera.Left();

            _generalize = dist * invZoom * GScene->GetSmokeGeneralization();

            saturate(_generalize, MinGeneralize, 3);
            _nextTime += _interval * _generalize * _generalize;
            float size05 = 0.5 * _sourceSize;
            Vector3Val windSpeed = GLandscape->GetWind() * 0.5;
            Vector3 speed(GRandGen.RandomValue() * (1 + _sourceSize) - (0.5 + size05) + _speed[0],
                          GRandGen.RandomValue() - 0.5 + _speed[1],
                          GRandGen.RandomValue() * (1 + _sourceSize) - (0.5 + size05) + _speed[2]);
            Vector3 offset((GRandGen.RandomValue() * 2 - 1) * _sourceSize,
                           (GRandGen.RandomValue() * 2 - 1) * _sourceSize,
                           (GRandGen.RandomValue() * 2 - 1) * _sourceSize);
            CloudletSource::SetSize((GRandGen.RandomValue() + 2) * _size * 2);
            CloudletSource::SetAlpha(_inOutDensity * _density * 0.7);
            //  simulate cloudlet source
            Cloudlet* cloudlet = Drop(pos + offset, speed);
            cloudlet->SetSpeed(speed + windSpeed);
            GLOB_WORLD->AddCloudlet(cloudlet);
        }
    }
    return false;
}

LSError Smoke::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar))
    PARAM_CHECK(ar.Serialize("animation", _animation, 1))
    PARAM_CHECK(ar.Serialize("animationSpeed", _animationSpeed, 1))
    PARAM_CHECK(ar.Serialize("timeToLive", _timeToLive, 1))
    PARAM_CHECK(ar.Serialize("firstLoop", _firstLoop, 1))
    PARAM_CHECK(ar.Serialize("invisible", _invisible, 1))
    PARAM_CHECK(ar.Serialize("alpha", _alpha, 1))
    PARAM_CHECK(ar.Serialize("fadeValue", _fadeValue, 1))
    PARAM_CHECK(ar.Serialize("fadeIn", _fadeIn, 1))
    PARAM_CHECK(ar.Serialize("fadeInTime", _fadeInTime, 1))
    PARAM_CHECK(ar.Serialize("fadeOut", _fadeOut, 1))
    PARAM_CHECK(ar.Serialize("fadeOutTime", _fadeOutTime, 1))
    if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassFirst)
    {
        _fadeInInv = 1.0f / _fadeIn;
        _fadeOutInv = 1.0f / _fadeOut;
    }
    return LSOK;
}

void SmokeSource::Load(const ParamEntry& cls)
{
    base::Load(cls);

    _density = cls >> "density";
    _size = cls >> "cloudletSize";
    _sourceSize = cls >> "size";
    _timeToLive = cls >> "timeToLive";
    float in = cls >> "in";
    SetIn(in);
    float out = cls >> "out";
    SetOut(out);
    _speed[1] = cls >> "initYSpeed";
}

LSError SmokeSource::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar))
    PARAM_CHECK(ar.Serialize("color", _color, 1))
    PARAM_CHECK(ar.Serialize("speed", _speed, 1))
    PARAM_CHECK(ar.Serialize("density", _density, 1))
    PARAM_CHECK(ar.Serialize("size", _size, 1))
    PARAM_CHECK(ar.Serialize("sourceSize", _sourceSize, 1, 0))
    PARAM_CHECK(ar.Serialize("inOutDensity", _inOutDensity, 1, 1))
    PARAM_CHECK(ar.Serialize("timeToLive", _timeToLive, 1))
    PARAM_CHECK(ar.Serialize("in", _in, 1))
    PARAM_CHECK(ar.Serialize("inTime", _inTime, 1))
    PARAM_CHECK(ar.Serialize("out", _out, 1))
    PARAM_CHECK(ar.Serialize("outTime", _outTime, 1))
    if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassFirst)
    {
        _inInv = 1.0f / _in;
        _outInv = 1.0f / _out;
    }

    return LSOK;
}

DEFINE_FAST_ALLOCATOR(SmokeSourceVehicle)
DEFINE_CASTING(SmokeSourceVehicle)

SmokeSourceVehicle::SmokeSourceVehicle(LODShapeWithShadow* shape, float density, float size, EntityAI* owner)
    : Vehicle(nullptr, VehicleTypes.New("SmokeSource"), -1), _darkFire(GLOB_SCENE->Preloaded(CloudletBasic), 0.05),
      _minExplosionFactor(0.5), _maxExplosionFactor(0.75), _exploded(false), _lightTime(0), _owner(owner),
      SmokeSource(shape, density, size)
{
    Object::_type = TypeTempVehicle;
    SetSimulationPrecision(_interval);
    _fire.Load(Pars >> "CfgCloudlets" >> "Explosion");
    _fire.SetSize(size);
    _darkFire.SetSize(size);
    _darkFire.SetColor(Color(0.15, 0.15, 0.10));
}

void SmokeSourceVehicle::Sound(bool inside, float deltaT) {}

void SmokeSourceVehicle::UnloadSound() {}

#pragma warning(disable : 4723)

void SmokeSourceVehicle::Explode(Time time)
{
    if (_explosionTime > time + 30)
    {
        _explosionTime = time;
    }
    SimulateExplosion();
}

bool SmokeSourceVehicle::ExplosionFinished() const
{
    if (!_exploded)
    {
        return false;
    }
    if (_fire.Active() || _darkFire.Active())
    {
        return false;
    }
    return true;
}

LSError SmokeSourceVehicle::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar))
    PARAM_CHECK(SmokeSource::Serialize(ar))
    PARAM_CHECK(ar.Serialize("_lightTime", _lightTime, 1))
    PARAM_CHECK(ar.SerializeRef("_owner", _owner, 1))
    PARAM_CHECK(ar.Serialize("_fire", _fire, 1))
    PARAM_CHECK(ar.Serialize("_darkFire", _darkFire, 1))
    PARAM_CHECK(ar.Serialize("_minExplosionFactor", _minExplosionFactor, 1, 0.5))
    PARAM_CHECK(ar.Serialize("_maxExplosionFactor", _maxExplosionFactor, 1, 0.75))
    PARAM_CHECK(ar.Serialize("_exploded", _exploded, 1))
    PARAM_CHECK(ar.Serialize("_explosionTime", _explosionTime, 1, TIME_MIN));

    return LSOK;
}

static const Color ExploColor(1, 1, 1);
static const Color ExploAmbient(0.2, 0.2, 0.2);

static const Color FireColor(1, 0.8, 0);
static const Color FireAmbient(0.2, 0.2, 0.2);

void SmokeSourceVehicle::SimulateExplosion()
{
    if (!_exploded && _explosionTime <= Glob.time)
    {
        _exploded = true;
        VehicleNonAIType* vType = VehicleTypes.New("FuelExplosion");
        AmmoType* aType = dynamic_cast<AmmoType*>(vType);
        if (!aType)
        {
            Fail("No explosion type");
            return;
        }
        AmmoType type = *aType;
        if (GetObject())
        {
            float gauss = GRandGen.RandomValue() + GRandGen.RandomValue();
            float randomFactor = gauss * (_maxExplosionFactor - _minExplosionFactor) + _minExplosionFactor;

            float hit = GetObject()->GetExplosives() * randomFactor;
            float indirectRatio = type.indirectHit / type.hit;
            type.hit = hit;
            type.indirectHit = indirectRatio * hit;
        }
        if (type.hit > 50)
        {
            // change fire and darkFire parameters based on type.hit value
            float fireSize = type.hit * 0.001;
            float fireTime = type.hit * 0.002;
            saturate(fireSize, 0.25, 1.25);
            saturate(fireTime, 0.25, 1.25);
            _fire.SetSize(fireSize);
            _darkFire.SetSize(fireSize);
            _fire.Start(fireTime);
            _darkFire.Start(fireTime);
            float rndTime = GRandGen.RandomValue() * 0.4 + 0.8;
            SetSourceTimes(0, 40 * rndTime, 40 * rndTime);
            // sound of explosion
            const ParamEntry& par = Pars >> "CfgDestroy" >> "EngineHit";
            SoundPars soundPars;
            GetValue(soundPars, par >> "sound");
            float rndFreq = GRandGen.RandomValue() * 0.1 + 0.95;
            IWave* sound = GSoundScene->OpenAndPlayOnce(soundPars.name, Position(), VZero, soundPars.vol,
                                                        soundPars.freq * rndFreq);
            if (sound)
            {
                GSoundScene->SimulateSpeedOfSound(sound);
                GSoundScene->AddSound(sound);
            }
            PoseidonAssert(_owner);
            if (IsLocal())
            {
                GLOB_LAND->ExplosionDammage(_owner, nullptr, GetObject(), Position(), VUp, &type);
            }
        }
    }
}

void SmokeSourceVehicle::Simulate(float deltaT, SimulationImportance prec)
{
    SimulateExplosion();
    bool canDelete = false;
    if (SmokeSource::Simulate(Position(), Speed(), deltaT, prec))
    {
        canDelete = true;
    }
    if (_fire.Active() || _darkFire.Active())
    {
        _fire.Simulate(Position(), Speed(), 0.8, deltaT);
        _darkFire.Simulate(Position(), Speed(), 0.8, deltaT);
    }
    else if (_exploded)
    {
        if (canDelete)
        {
            _delete = true;
        }
    }

    if (_fire.Active() && prec <= SimulateInvisibleNear && (ENGINE_CONFIG.lights & LIGHT_EXPLO))
    {
        if (!_light)
        {
            _light = new LightPoint(HBlack, HBlack);
            GLOB_SCENE->AddLight(_light);
        }
    }
    if (_light)
    {
        _lightTime += deltaT * 1.0;
        float intensity = (1 - fabs(_lightTime - 1)) * _size;
        saturateMax(intensity, 0);
        _light->SetPosition(Position());
        _light->SetDiffuse(ExploColor * intensity);
        _light->SetAmbient(ExploAmbient * intensity);
        if (_lightTime >= 2.0)
        {
            _light.Free();
        }
    }
}

DEFINE_FAST_ALLOCATOR(SmokeSourceOnVehicle)

SmokeSourceOnVehicle::SmokeSourceOnVehicle(LODShapeWithShadow* shape, float density, float size, EntityAI* owner,
                                           Object* vehicle, Vector3Par position)
    : SmokeSourceVehicle(shape, density, size, owner), AttachedOnVehicle(vehicle, position, Vector3(0, 0, 1))
{
    _explosionTime = TIME_MAX;
}

void SmokeSourceOnVehicle::UpdatePosition()
{
    if (_vehicle != nullptr)
    {
        Matrix4 toWorld = _vehicle->WorldTransform();
        Matrix4 transf;
        transf.SetPosition(toWorld.FastTransform(_pos));
        transf.SetDirectionAndUp(toWorld.Rotate(_dir), toWorld.DirectionUp());
        Move(transf);
    }
}

LSError SmokeSourceOnVehicle::Serialize(ParamArchive& ar)
{
    // Note: PARAM_CHECK(AttachedOnVehicle::Serialize(ar))
    PARAM_CHECK(SmokeSourceVehicle::Serialize(ar))

    return LSOK;
}

DEFINE_FAST_ALLOCATOR(ObjectDestructed)

const float DestroyerTimeToLive = 2;

ObjectDestructed::ObjectDestructed(LODShapeWithShadow* shape)
    : Vehicle(shape, VehicleTypes.New("ObjectDestructed"), -1), _destroy(nullptr), _anim(0),
      _speed(1.0 / DestroyerTimeToLive), _dust(GLOB_SCENE->Preloaded(CloudletBasic), 2.0, 0.5)
{
    _soundPars.name = RString(nullptr);
    // dust parameters
    _dust.SetClimbRate(-0.5, -1, 1);
    _dust.SetSpeed(Vector3(0, 0.2, 0));
    _dust.SetColor(Color(0.51, 0.46, 0.33) * 0.5);
    float timeToLive = DestroyerTimeToLive;
    _dust.SetSourceTimes(timeToLive * 0.4, timeToLive * 0.2, timeToLive * 0.4);
    _dust.SetFades(0.8, 0.2, 1.5);
    _dust.SetTimes(0.5, 1.0);
}

ObjectDestructed::ObjectDestructed(Object* destroy, const SoundPars& soundPars, float timeToLive, float size)
    : Vehicle(nullptr, VehicleTypes.New("ObjectDestructed"), -1), _destroy(destroy), _anim(0), _speed(1 / timeToLive),
      _dust(GLOB_SCENE->Preloaded(CloudletBasic), 2.0, destroy->GetShape()->GeometrySphere() * 0.2),
      _soundPars(soundPars)
{
    if (_soundPars.name.GetLength() > 0)
    {
        IWave* sound = GSoundScene->OpenAndPlayOnce(_soundPars.name, destroy->Position(), destroy->ObjectSpeed());
        if (sound)
        {
            _sound = sound;
            GSoundScene->SimulateSpeedOfSound(sound);
            GSoundScene->AddSound(sound);
        }
    }
    // dust parameters
    _dust.SetClimbRate(-0.5, -1, 1);
    _dust.SetSpeed(Vector3(0, 0.2, 0));
    _dust.SetColor(Color(0.51, 0.46, 0.33) * 0.5);
    _dust.SetSourceTimes(timeToLive * 0.4, timeToLive * 0.2, timeToLive * 0.4);
    _dust.SetFades(0.8, 0.2, 1.5);
    _dust.SetTimes(0.5, 1.0);
}

void ObjectDestructed::Simulate(float deltaT, SimulationImportance prec)
{
    if (!EnableVisualEffects(prec))
    {
        _anim = 1;
    }
    if (!_destroy || !_destroy->GetShape())
    {
        _delete = true;
        return;
    }
    _anim += _speed * deltaT * 2;
    if (_anim >= 1)
    {
        _anim = 1;
        _delete = true;
    }
    // check object destruction class
    // apply special destruction for trees
    _destroy->SetDestroyed(_anim);
    _dust.Simulate(Position(), Speed(), deltaT, prec);
}

void ObjectDestructed::Sound(bool inside, float deltaT)
{
    if (_sound)
    {
        _sound->SetVolume(_soundPars.vol, _soundPars.freq); // volume, frequency
        _sound->SetPosition(Position(), Speed());
    }
}

void ObjectDestructed::UnloadSound() {}

LSError ObjectDestructed::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar))
    PARAM_CHECK(ar.Serialize("soundPars", _soundPars, 1))
    PARAM_CHECK(ar.SerializeRef("destroy", _destroy, 1))
    PARAM_CHECK(ar.Serialize("anim", _anim, 1))
    PARAM_CHECK(ar.Serialize("destSpeed", _speed, 1))
    PARAM_CHECK(ar.Serialize("Dust", _dust, 1))
    return LSOK;
}

NetworkMessageType ObjectDestructed::GetNMType(NetworkMessageClass cls) const
{
    switch (cls)
    {
        case NMCCreate:
            return NMTCreateObjectDestructed;
        case NMCUpdateDammage:
            return NMTNone;
        default:
            return base::GetNMType(cls);
    }
}

#define CREATE_OBJECT_DESTRUCTED_MSG(XX) \
	XX(OLink<Object>, destroy, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Destroying object"), IdxTransferRef) \
	XX(float, timeToLive, NDTFloat, NCTNone, DEFVALUE(float, 2), DOC_MSG("Time to live"), IdxTransfer) \
	XX(RString, soundName, NDTString, NCTNone, DEFVALUE(RString, RString(nullptr)), DOC_MSG("ID of played sound"), IdxTransfer) \
	XX(float, soundVolume, NDTFloat, NCTNone, DEFVALUE(float, 1), DOC_MSG("Volume of played sound"), IdxTransfer) \
	XX(float, soundFrequency, NDTFloat, NCTNone, DEFVALUE(float, 1), DOC_MSG("Pitch of played sound"), IdxTransfer)

DECLARE_NET_INDICES_EX(CreateObjectDestructed, CreateVehicle, CREATE_OBJECT_DESTRUCTED_MSG)
DEFINE_NET_INDICES_EX(CreateObjectDestructed, CreateVehicle, CREATE_OBJECT_DESTRUCTED_MSG)

} // namespace Poseidon

DEFINE_GET_INDICES(CreateObjectDestructed)

namespace Poseidon
{

NetworkMessageFormat& ObjectDestructed::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    switch (cls)
    {
        case NMCCreate:
            base::CreateFormat(cls, format);
            CREATE_OBJECT_DESTRUCTED_MSG(MSG_FORMAT)
            break;
        default:
            base::CreateFormat(cls, format);
            break;
    }
    return format;
}

TMError ObjectDestructed::TransferMsg(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCCreate:
            if (ctx.IsSending())
            {
                TMCHECK(base::TransferMsg(ctx))

                PoseidonAssert(dynamic_cast<const IndicesCreateObjectDestructed*>(ctx.GetIndices()))
                    const IndicesCreateObjectDestructed* indices =
                        static_cast<const IndicesCreateObjectDestructed*>(ctx.GetIndices());

                ITRANSF_REF(destroy)
                float timeToLive = 1.0 / _speed;
                TMCHECK(ctx.IdxTransfer(indices->timeToLive, timeToLive))
                TMCHECK(ctx.IdxTransfer(indices->soundName, _soundPars.name))
                TMCHECK(ctx.IdxTransfer(indices->soundVolume, _soundPars.vol))
                TMCHECK(ctx.IdxTransfer(indices->soundFrequency, _soundPars.freq))
            }
            break;
        default:
            TMCHECK(base::TransferMsg(ctx))
            break;
    }
    return TMOK;
}

ObjectDestructed* ObjectDestructed::CreateObject(NetworkMessageContext& ctx)
{
    base* veh = base::CreateObject(ctx);
    ObjectDestructed* destroyer = dyn_cast<ObjectDestructed>(veh);
    if (!destroyer)
    {
        return nullptr;
    }

    PoseidonAssert(dynamic_cast<const IndicesCreateObjectDestructed*>(ctx.GetIndices()))
        const IndicesCreateObjectDestructed* indices =
            static_cast<const IndicesCreateObjectDestructed*>(ctx.GetIndices());

    float timeToLive;
    if (ctx.IdxTransferRef(indices->destroy, destroyer->_destroy) != TMOK)
    {
        return nullptr;
    }
    if (ctx.IdxTransfer(indices->timeToLive, timeToLive) != TMOK)
    {
        return nullptr;
    }
    if (ctx.IdxTransfer(indices->soundName, destroyer->_soundPars.name) != TMOK)
    {
        return nullptr;
    }
    if (ctx.IdxTransfer(indices->soundVolume, destroyer->_soundPars.vol) != TMOK)
    {
        return nullptr;
    }
    if (ctx.IdxTransfer(indices->soundFrequency, destroyer->_soundPars.freq) != TMOK)
    {
        return nullptr;
    }

    destroyer->_speed = 1.0 / timeToLive;
    destroyer->_dust.SetSourceTimes(timeToLive * 0.4, timeToLive * 0.2, timeToLive * 0.4);
    if (destroyer->_soundPars.name.GetLength() > 0)
    {
        IWave* sound =
            GSoundScene->OpenAndPlayOnce(destroyer->_soundPars.name, destroyer->Position(), destroyer->Speed());
        if (sound)
        {
            destroyer->_sound = sound;
            GSoundScene->SimulateSpeedOfSound(sound);
            GSoundScene->AddSound(sound);
        }
    }
    return destroyer;
}

void Cloudlet::Draw(int level, ClipFlags clipFlags, const FrameBase& frame)
{
#if !ALPHA_SPLIT
    PoseidonAssert(level != LOD_INVISIBLE);
    DrawDecal(level, clipFlags, frame);
#endif
}

#if ALPHA_SPLIT
void Cloudlet::DrawAlpha(int level, ClipFlags clipFlags)
{
    PoseidonAssert(level != LOD_INVISIBLE);
    DrawDecal(clipFlags, pos);
}
#endif

void Smoke::Simulate(float deltaT, SimulationImportance prec)
{
    _animation += deltaT * _animationSpeed;
    while (_animation >= 1.0)
    {
        _animation -= 1.0, _firstLoop = false;
    }
    if (_fadeInTime > 0)
    {
        _fadeValue = 1 - _fadeInTime * _fadeInInv;
        _fadeInTime -= deltaT;
    }
    else if (_timeToLive > 0)
    {
        _fadeValue = 1.0;
        _timeToLive -= deltaT;
    }
    else if (_fadeOutTime > 0)
    {
        _fadeValue = _fadeOutTime * _fadeOutInv;
        _fadeOutTime -= deltaT;
        _firstLoop = false; // if fading is valid, we never wait for loop
    }
    else
    {
        if (_firstLoop)
        {
            _fadeValue = 1;
        }
        else
        {
            _fadeValue = 0;
            _invisible = true;
            _delete = true;
        }
    }
    int alpha255 = toIntFloor(_alpha * _fadeValue * 255);
    if (alpha255 > 255)
    {
        alpha255 = 255;
    }
    if (alpha255 < 0)
    {
        alpha255 = 0;
    }
    _constantColor = PackedColorRGB(_constantColor, alpha255);
}

bool Smoke::IsAnimated(int level) const
{
    return true;
}
bool Smoke::IsAnimatedShadow(int level) const
{
    return true;
}

void Smoke::Animate(int level)
{
    // animate all animated textures
    Shape* shape = _shape->Level(level);
    if (!shape)
    {
        return;
    }
    if (shape->NFaces() <= 0)
    {
        return;
    }
    // normal smoke is single-LOD single face object
    Poly& face = shape->Face(shape->BeginFaces());
    if (face.Special() & ::IsAnimated)
    {
        face.AnimateTexture(_animation);
    }
}

void Smoke::Deanimate(int level)
{
    // unanimated smoke is nonsense and should never exist
}

LSError CloudletTItem::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.Serialize("maxT", maxT, 1, 0))
    PARAM_CHECK(ar.Serialize("color", color, 1, Color(HBlack)))
    return LSOK;
}

int CloudletTTable::Add(float maxT, Color color)
{
    int index = base::Add();
    Set(index).maxT = maxT;
    Set(index).color = color;
    return index;
}

Color CloudletTTable::GetColor(float t) const
{
    // linear interpolation in table
    if (Size() == 0)
    {
        Log("Empty CloudletTTable");
        return HWhite;
    }

    if (t <= Get(0).maxT)
    {
        return Get(0).color;
    }
    int index = -1;
    for (int i = 1; i < Size(); i++)
    {
        if (t <= Get(i).maxT)
        {
            index = i;
            break;
        }
    }
    if (index < 0)
    {
        return Get(Size() - 1).color;
    }
    float t0 = Get(index - 1).maxT;
    float t1 = Get(index).maxT;
    float coef = (t - t0) / (t1 - t0);
    return Get(index - 1).color * (1.0 - coef) + Get(index).color * coef;
}

DEFINE_FAST_ALLOCATOR(Cloudlet)

namespace
{
void ApplyCloudletShapeSpecials(LODShapeWithShadow* shape)
{
    if (!shape)
        return;

    shape->OrSpecial(ClampU | ClampV | NoZWrite | IsAlpha | IsAlphaFog | NoShadow | IsColored | IsAlphaOrdered);
}
} // namespace

// basic element from which smoke trails are built
Cloudlet::Cloudlet(LODShapeWithShadow* shape, float duration, float loopedDuration)
    : Smoke(shape, SmokeType(), duration, loopedDuration), _size(1), _growSize(0), _growUpTime(0), _growUpInv(1),
      _accY(-0.8), _minYSpeed(0), _maxYSpeed(5), _xSpeed0(0), _zSpeed0(0), _xFriction(0), _zFriction(0), _t(0), _dt(0),
      _cloudletColor(HWhite)
{
    ApplyCloudletShapeSpecials(shape);
}

SimulationImportance Cloudlet::WorstImportance() const
{
    return SimulateVisibleFar;
}

float Cloudlet::CloudletClippingCoef() const
{
    return 10;
}

SimulationImportance Cloudlet::BestImportance() const
{
    return SimulateVisibleFar;
}

void Cloudlet::Simulate(float deltaT, SimulationImportance prec)
{
    if (_growUpTime > 0)
    {
        _growSize = 1 - _growUpTime * _growUpInv;
        _growUpTime -= deltaT;
    }
    else
    {
        _growSize = 1;
    }
    // simulate acceleration
    _speed[1] += _accY * deltaT;
    Friction(_speed[0], _xFriction, 0, deltaT);
    Friction(_speed[2], _zFriction, 0, deltaT);
    Limit(_speed[1], _minYSpeed, _maxYSpeed);
    SetOrientScaleOnly(_growSize * _size);
    SetPosition(Position() + _speed * deltaT);

    _t += _dt * deltaT;
    _constantColor = PackedColor(_cloudletColor * _cloudletTTable->GetColor(_t));

    Smoke::Simulate(deltaT, prec);
}

void Cloudlet::SetTemperature(float t, float dt, CloudletTTable* table)
{
    _t = t;
    _dt = dt;
    _cloudletTTable = table;
}

CloudletSource::CloudletSource(LODShapeWithShadow* shape, float interval)
    : // cloudlet generator
      _cloudletShape(shape), _interval(interval), _nextTime(0),

      // cloudlett parameters
      _cloudletDuration(1.0), _cloudletAnimPeriod(3.0), _cloudletSize(1), _cloudletAlpha(1), _cloudletGrowUp(1.0),
      _cloudletFadeIn(0.5), _cloudletFadeOut(2.0), _cloudletSpeed(VZero), _cloudletColor(HWhite), _cloudletInitT(0),
      _cloudletDeltaT(0),

      _generalize(1.0),

      _lastPositionValid(false)
{
    SetClimbRate(0, -10, +10);
    _cloudletTTable = new CloudletTTable();
    _cloudletTTable->Resize(1);
    _cloudletTTable->Set(0).maxT = 10000;
    _cloudletTTable->Set(0).color = HWhite;
}

Cloudlet* CloudletSource::Drop(Vector3Par pos, Vector3Par speed)
{
    Cloudlet* cloudlet = new Cloudlet(_cloudletShape, _cloudletAnimPeriod, _cloudletDuration);
    cloudlet->SetFades(_cloudletFadeIn, _cloudletFadeOut);
    cloudlet->SetGrowUp(_cloudletGrowUp, _cloudletSize * _generalize);
    cloudlet->SetAlpha(_cloudletAlpha);
    cloudlet->SetSpeed(_cloudletSpeed + speed);
    cloudlet->SetClimbRate(_cloudletAccY, _cloudletMinYSpeed, _cloudletMaxYSpeed);
    Color colorA0 = _cloudletColor;
    colorA0.SetA(0);
    cloudlet->SetColor(PackedColor(colorA0));
    float delta = _cloudletDeltaT * (0.7 + 0.6 * GRandGen.RandomValue());
    cloudlet->SetTemperature(_cloudletInitT, delta, _cloudletTTable);
    cloudlet->SetPosition(pos);
    return cloudlet;
}

void CloudletSource::Simulate(Vector3Par pos, Vector3Par speed, float deltaT)
{
    _nextTime -= deltaT;
    if (_nextTime <= 0)
    {
        _nextTime = _interval;
        Cloudlet* cloudlet = Drop(pos, speed);
        GLOB_WORLD->AddCloudlet(cloudlet);
    }
}

void CloudletSource::Load(const ParamEntry& cls)
{
    _interval = cls >> "interval";
    _cloudletDuration = cls >> "cloudletDuration";
    _cloudletAnimPeriod = cls >> "cloudletAnimPeriod";
    _cloudletSize = cls >> "cloudletSize";
    _cloudletAlpha = cls >> "cloudletAlpha";
    _cloudletGrowUp = cls >> "cloudletGrowUp";
    _cloudletFadeIn = cls >> "cloudletFadeIn";
    _cloudletFadeOut = cls >> "cloudletFadeOut";
    _cloudletAccY = cls >> "cloudletAccY";
    _cloudletMinYSpeed = cls >> "cloudletMinYSpeed";
    _cloudletMaxYSpeed = cls >> "cloudletMaxYSpeed";
    _cloudletColor = ::GetColor(cls >> "cloudletColor");
    _cloudletInitT = cls >> "initT";
    _cloudletDeltaT = cls >> "deltaT";
    const ParamEntry& table = cls >> "Table";
    int n = table.GetEntryCount();
    _cloudletTTable->Resize(n);
    for (int i = 0; i < n; i++)
    {
        const ParamEntry& item = table.GetEntry(i);
        _cloudletTTable->Set(i).maxT = item >> "maxT";
        _cloudletTTable->Set(i).color = ::GetColor(item >> "color");
    }
    _cloudletShape = Shapes.New(GetShapeName(cls >> "cloudletShape"), false, false);
    ApplyCloudletShapeSpecials(_cloudletShape);
}

LSError CloudletSource::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.Serialize("interval", _interval, 1))
    PARAM_CHECK(ar.Serialize("nextTime", _nextTime, 1))

    // parameters of cloudlets
    if (ar.IsSaving())
    {
        RString name;
        if (_cloudletShape)
        {
            name = _cloudletShape->Name();
        }
        PARAM_CHECK(ar.Serialize("cloudletShape", name, 1))
    }
    else if (ar.GetPass() == ParamArchive::PassFirst)
    {
        RString name;
        PARAM_CHECK(ar.Serialize("cloudletShape", name, 1))
        if (name.GetLength() > 0)
        {
            _cloudletShape = Shapes.New(name, false, true);
        }
        else
        {
            _cloudletShape = nullptr;
        }
    }

    if (ar.IsSaving())
    {
        PARAM_CHECK(ar.Serialize("cloudletTTable", *_cloudletTTable, 1))
    }
    else if (ar.GetPass() == ParamArchive::PassFirst)
    {
        _cloudletTTable = new CloudletTTable;
        PARAM_CHECK(ar.Serialize("cloudletTTable", *_cloudletTTable, 1))
    }

    PARAM_CHECK(ar.Serialize("cloudletDuration", _cloudletDuration, 1))
    PARAM_CHECK(ar.Serialize("cloudletAnimPeriod", _cloudletAnimPeriod, 1))
    PARAM_CHECK(ar.Serialize("cloudletSize", _cloudletSize, 1))
    PARAM_CHECK(ar.Serialize("cloudletAlpha", _cloudletAlpha, 1))
    PARAM_CHECK(ar.Serialize("cloudletGrowUp", _cloudletGrowUp, 1))
    PARAM_CHECK(ar.Serialize("cloudletFadeIn", _cloudletFadeIn, 1))
    PARAM_CHECK(ar.Serialize("cloudletFadeOut", _cloudletFadeOut, 1))
    PARAM_CHECK(ar.Serialize("cloudletAccY", _cloudletAccY, 1))
    PARAM_CHECK(ar.Serialize("cloudletMinYSpeed", _cloudletMinYSpeed, 1))
    PARAM_CHECK(ar.Serialize("cloudletMaxYSpeed", _cloudletMaxYSpeed, 1))
    PARAM_CHECK(ar.Serialize("cloudletColor", _cloudletColor, 1))
    PARAM_CHECK(ar.Serialize("cloudletSpeed", _cloudletSpeed, 1))
    PARAM_CHECK(ar.Serialize("generalize", _generalize, 1))
    PARAM_CHECK(ar.Serialize("cloudletInitT", _cloudletInitT, 1, 0))
    PARAM_CHECK(ar.Serialize("cloudletDeltaT", _cloudletDeltaT, 1, 0))

    /* Note: ?? serialize
    Vector3 _lastPosition;
    bool _lastPositionValid;
    */
    return LSOK;
}

void DustSource::Init()
{
    SetClimbRate(-2, -3, 3);
    SetColor(Color(0.51, 0.46, 0.33));
    SetFades(0.2, 0.2, 1);
    SetTimes(0.5, 1.0);
    SetAlpha(0.6);
    SetSize(0.5);
    _generalizeFactor = 1;
    _maxGeneralize = 3;
    _windCoef = 0.5;
}

DustSource::DustSource(LODShapeWithShadow* shape, float interval) : CloudletSource(shape, interval)
{
    Init();
}

DustSource::DustSource(float interval)
    : CloudletSource(GLOB_SCENE->Preloaded(CloudletBasic), interval), _maxGeneralize(3)
{
    Init();
}

void DustSource::Load(const ParamEntry& cls)
{
    base::Load(cls);
    _size = cls >> "size";
    _sourceSize = cls >> "sourceSize";
}

LSError DustSource::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar))
    PARAM_CHECK(ar.Serialize("size", _size, 1))
    PARAM_CHECK(ar.Serialize("sourceSize", _sourceSize, 1))
    PARAM_CHECK(ar.Serialize("maxGeneralize", _maxGeneralize, 1))
    PARAM_CHECK(ar.Serialize("windCoef", _windCoef, 1))
    return LSOK;
}

WaterSource::WaterSource(LODShapeWithShadow* shape, float interval) : DustSource(shape, interval)
{
    SetClimbRate(-3, -0.1, 2);
    SetColor(HWhite);
    SetFades(0.5, 0.2, 1.3);
    SetTimes(0.5, 1.0);
    SetSize(0.1);
}

WaterSource::WaterSource(float interval) : DustSource(GLOB_SCENE->Preloaded(CloudletWater), interval)
{
    SetClimbRate(-3, -0.1, 2);
    SetColor(HWhite);
    SetFades(0.5, 0.2, 1.3);
    SetTimes(0.5, 1.0);
    SetSize(0.1);
}

void ExhaustSource::Init()
{
    SetClimbRate(-2, -2, 2);
    Color color(0.2, 0.2, 0.5);
    SetColor(color);
    base::SetSize(0.3, 0.1);
    SetFades(0.1, 0.2, 1);
    SetTimes(0.1, 1.0);
}

ExhaustSource::ExhaustSource(LODShapeWithShadow* shape, float interval) : base(shape, interval)
{
    Init();
}

ExhaustSource::ExhaustSource(float interval) : base(GLOB_SCENE->Preloaded(CloudletBasic), interval)
{
    Init();
}

void ExhaustSource::SetSize(float size)
{
    base::SetSize(0.3 * size, 0.1 * size);
}

void DustSource::Simulate(Vector3Par pos, Vector3Par speed, float density, float deltaT)
{
    if (density < 0.01)
    {
        return; // no dust
    }
    _nextTime -= deltaT;
    while (_nextTime <= 0)
    {
        const Camera& camera = *GLOB_SCENE->GetCamera();
        float dist = camera.Position().Distance(pos);
        float invZoom = camera.Left();

        _generalize = dist * invZoom * GScene->GetSmokeGeneralization();

        saturate(_generalize, MinGeneralize, _maxGeneralize);
        _nextTime += _interval * _generalize * _generalize;
        const float size2 = 2 * _size;
        const float size05 = 0.5 * _size;
        Vector3Val windSpeed = GLandscape->GetWind() * _windCoef;
        Vector3 cSpeed((GRandGen.RandomValue() * (1 + _size) - (0.5 + size05)) * 2,
                       GRandGen.RandomValue() * 1 + size2 * 1,
                       (GRandGen.RandomValue() * (1 + _size) - (0.5 + size05)) * 2);
        Vector3 offset((GRandGen.RandomValue() * 2 - 1) * _sourceSize, (GRandGen.RandomValue() * 2 - 1) * _sourceSize,
                       (GRandGen.RandomValue() * 2 - 1) * _sourceSize);
        float cSize = floatMax(_size, _size * 3 * density);
        CloudletSource::SetSize((GRandGen.RandomValue() + 2) * cSize);
        // simulate cloudlet source
        Cloudlet* cloudlet = Drop(pos + offset, cSpeed * density * 0.5 + windSpeed + speed);
        cloudlet->SetClimbRate(_cloudletAccY, _cloudletMinYSpeed, _cloudletMaxYSpeed);
        cloudlet->SetSideSpeed(0, 0, 1.5, 1.5);
        GLOB_WORLD->AddCloudlet(cloudlet);
    }
}

void WeaponCloudsSource::Init()
{
    _generalizeFactor = 2;
    SetSize(0.2, 0.2);
    SetClimbRate(+0.4, +0.2, +0.8);
    SetFades(0.1, 0.01, 0.3);
    SetTimes(0.3, 1.0);
}

WeaponCloudsSource::WeaponCloudsSource(LODShapeWithShadow* shape, float interval)
    : DustSource(shape, interval), _timeToLive(-1)
{
    Init();
}
WeaponCloudsSource::WeaponCloudsSource(float interval)
    : DustSource(GLOB_SCENE->Preloaded(CloudletBasic), interval), _timeToLive(-1)
{
    Init();
}

void WeaponCloudsSource::Start(float time)
{
    _timeToLive = time;
}

void WeaponCloudsSource::Simulate(Vector3Par pos, Vector3Par speed, float density, float deltaT)
{
    if (_timeToLive < 0)
    {
        return;
    }
    _timeToLive -= deltaT;

    if (density < 0.01)
    {
        return; // no dust
    }
    _nextTime -= deltaT;
    while (_nextTime <= 0)
    {
        const Camera& camera = *GLOB_SCENE->GetCamera();
        float dist = camera.Position().Distance(pos);
        float invZoom = camera.Left();

        _generalize = dist * invZoom * GScene->GetSmokeGeneralization();

        saturate(_generalize, MinGeneralize, _maxGeneralize);
        _nextTime += _interval * _generalize * _generalize;

        const float size = GetSize();
        const float size2 = 2 * size;
        const float size05 = 0.5 * size;
        Vector3 cSpeed((GRandGen.RandomValue() * (1 + size) - (0.5 + size05)) * 2,
                       GRandGen.RandomValue() * 1 + size2 * 1,
                       (GRandGen.RandomValue() * (1 + size) - (0.5 + size05)) * 2);
        const float sourceSize = GetSourceSize();
        Vector3 offset((GRandGen.RandomValue() * 2 - 1) * sourceSize, (GRandGen.RandomValue() * 2 - 1) * sourceSize,
                       (GRandGen.RandomValue() * 2 - 1) * sourceSize);
        float cSize = floatMax(size, size * 3 * density);
        CloudletSource::SetSize((GRandGen.RandomValue() + 2) * cSize);
        // simulate cloudlet source
        Cloudlet* cloudlet = Drop(pos + offset, speed);

        float newSize = GetSize() * _generalize * (0.5 + 1.0 * GRandGen.RandomValue());
        cloudlet->SetGrowUp(_cloudletGrowUp, newSize);
        Vector3 dir = cloudlet->Position() - pos;
        saturateMax(dir[1], 0);

        float speed2 = 4.0 * dir.Size();
        saturateMin(speed2, 25);
        dir.Normalize();

        cloudlet->SetSpeed(speed + speed2 * dir);

        cloudlet->SetClimbRate(_cloudletAccY * density * 1.5, _cloudletMinYSpeed, _cloudletMaxYSpeed);
        cloudlet->SetSideSpeed(0, 0, 1.5, 1.5);
        GLOB_WORLD->AddCloudlet(cloudlet);
    }
}

LSError WeaponCloudsSource::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar))
    PARAM_CHECK(ar.Serialize("timeToLive", _timeToLive, 1))
    return LSOK;
}

WeaponLightSource::WeaponLightSource()
{
    Init();
}

void WeaponLightSource::Init()
{
    _timeToLight = -1;
}

void WeaponLightSource::Start(float time, float intensity, bool mGun)
{
    float lightTime = time * 3;
    _timeToLight = lightTime;
    _invTotalTimeToLight = 1 / lightTime;
    _lightIntensity = intensity;
    _lightMGun = mGun;
}

bool WeaponLightSource::Active() const
{
    return _timeToLight >= 0 || _light;
}

void WeaponLightSource::Simulate(Vector3Par pos, float deltaT)
{
    if (_timeToLight < 0)
    {
        _light.Free();
    }
    else
    {
        _timeToLight -= deltaT;
        // simulate light
        if (ENGINE_CONFIG.lights & LIGHT_EXPLO)
        {
            if (!_light)
            {
                _light = new LightPoint(FireColor, FireAmbient);
                GLOB_SCENE->AddLight(_light);
            }
            if (_light)
            {
                float animation = _timeToLight * _invTotalTimeToLight;
                float intensity = _lightIntensity;
                if (_lightMGun)
                {
                    intensity *= GRandGen.PlusMinus(0.8, 0.2);
                }
                else
                {
                    intensity *= (0.5 - fabs(animation - 0.5)) * 2.0 * _lightIntensity;
                }

                saturateMax(intensity, 0);
                _light->SetPosition(pos);
                _light->SetBrightness(intensity);
            }
        }
    }
}

LSError WeaponLightSource::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.Serialize("timeToLight", _timeToLight, 1))
    PARAM_CHECK(ar.Serialize("invTotalTimeToLight", _invTotalTimeToLight, 1))
    PARAM_CHECK(ar.Serialize("lightIntensity", _lightIntensity, 1))

    return LSOK;
}

WeaponFireSource::WeaponFireSource(float interval) : WeaponCloudsSource(GLOB_SCENE->Preloaded(CloudletFire), interval)
{
    Init();
}

WeaponFireSource::WeaponFireSource(LODShapeWithShadow* shape, float interval) : WeaponCloudsSource(shape, interval)
{
    Init();
}

void WeaponFireSource::Init()
{
    _generalizeFactor = 1;
    SetSize(0.5, 0.5);
    SetClimbRate(+0.4, +0.2, +0.8);
    SetFades(0.2, 0.01, 0.5);
    SetTimes(0.2, 1.0);
}

LSError WeaponFireSource::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar))
    PARAM_CHECK(base2::Serialize(ar))
    return LSOK;
}

void WeaponFireSource::Simulate(Vector3Par pos, Vector3Par speed, float density, float deltaT)
{
    // simulate light
    base2::Simulate(pos, deltaT);
    // simulate clouds
    base::Simulate(pos, speed, density, deltaT);
}

void WeaponFireSource::Start(float time, float intensity, bool lightMGun)
{
    base2::Start(time, intensity, lightMGun);
    base::Start(time);
}

bool WeaponFireSource::Active() const
{
    return _timeToLive >= 0 || base2::Active();
}

DEFINE_FAST_ALLOCATOR(Crater)
DEFINE_CASTING(Crater)

Crater::Crater(LODShapeWithShadow* shape, VehicleNonAIType* type, float timeToLive, float size, bool smoke, bool blood,
               bool water)
    : Smoke(shape, type, timeToLive, timeToLive)
{
    Init(timeToLive, size, smoke, blood, water);
}

void Crater::Init(float timeToLive, float size, bool smoke, bool blood, bool water)
{
    _isSmoke = smoke;
    _isWater = water;
    _isBlood = blood && ENGINE_CONFIG.blood;
    _size = size;

    // set crater parameters
    SetFades(0.1, timeToLive * 0.6);
    if (_isBlood)
    {
        // blood
        _dustTimeToLive = 0.1;
        _dust.Load(Pars >> "CfgCloudlets" >> "CraterBlood");

        float angle = 2.0 * H_PI * GRandGen.RandomValue();
        Vector3 dir(sin(angle), 0, cos(angle));

        _dust.SetSpeed(-VUp * 0.5 + dir * 0.5);
        _dust.SetWindCoef(0);
    }
    else if (_isWater)
    {
        // water
        _dustTimeToLive = 0.3;
        _dust.Load(Pars >> "CfgCloudlets" >> "CraterWater");
        _dust.SetSpeed(3.0 * VUp);
        _dust.SetWindCoef(0.1);
    }
    else if (_isSmoke)
    {
        // big explosion
        float timeSize = floatMin(size * 2, timeToLive);
        saturate(timeSize, 0.2, 1.0);
        _dustTimeToLive = timeSize;
        _dust.Load(Pars >> "CfgCloudlets" >> "CraterDustBig");
        _dust.SetSpeed(VUp * 10 * size);
        _dust.SetSize(size, size * 1.5);
        // set smoke parameters
        float cloudletSize = size;
        saturate(cloudletSize, 0.5, 1.2);
        _smoke1.Load(Pars >> "CfgCloudlets" >> "CraterSmoke1");
        _smoke1.SetSize(2.0 * cloudletSize, 1.5 * size);
        _smoke1.SetColor(_smoke1.GetColor() * GRandGen.RandomValue());
        _smoke2.Load(Pars >> "CfgCloudlets" >> "CraterSmoke2");
        _smoke2.SetSize(2.0 * cloudletSize, 1.5 * size);
        _smoke2.SetColor(_smoke2.GetColor() * (0.6 + 0.4 * GRandGen.RandomValue()));
        _smoke3.Load(Pars >> "CfgCloudlets" >> "CraterSmoke3");
        _smoke3.SetSize(1.0 * cloudletSize, 1.5 * size);
        _smoke3.SetColor(_smoke3.GetColor() * (0.6 + 0.4 * GRandGen.RandomValue()));
    }
    else
    {
        // small explosion
        float timeSize = floatMin(size * 100, timeToLive);
        saturate(timeSize, 0.2, 2.0);

        _dustTimeToLive = 0.1 * timeSize;

        _dust.Load(Pars >> "CfgCloudlets" >> "CraterDustSmall");
        _dust.SetFades(0.6 * timeSize, 0, 0.6 * timeSize);
        _dust.SetTimes(0.2 * timeSize, 1.0 * timeSize);

        _dust.SetSpeed(VUp * 2 * timeSize);
        _dust.SetClimbRate(-3.8, -20 * timeSize, 10 * timeSize);
        _dust.SetSize(20 * size, 2 * size);
    }
}

void Crater::Simulate(float deltaT, SimulationImportance prec)
{
    // consider: server-side simulation?
    if (prec > SimulateVisibleFar)
    {
        _delete = true;
        return;
    }
    if (_isSmoke)
    {
        _smoke1.Simulate(Position(), Speed(), deltaT, prec);
        _smoke2.Simulate(Position(), Speed(), deltaT, prec);
        _smoke3.Simulate(Position(), Speed(), deltaT, prec);
    }
    if (_dustTimeToLive > 0)
    {
        const float scale = 0.8;
        Vector3 up((GRandGen.RandomValue() * 6 * scale - 3 * scale) * _size, 10 * scale * _size,
                   (GRandGen.RandomValue() * 6 * scale - 3 * scale) * _size);
        float density = floatMax(_size, 0.02);
        _dustTimeToLive -= deltaT;
        if (!_isSmoke)
        {
            _dust.Simulate(Position(), Speed() + up, density, deltaT);
        }
    }
    Smoke::Simulate(deltaT, prec);
}

LSError Crater::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar))
    PARAM_CHECK(ar.Serialize("isSmoke", _isSmoke, 1, false))
    PARAM_CHECK(ar.Serialize("isBlood", _isBlood, 1, false))
    PARAM_CHECK(ar.Serialize("isWater", _isWater, 1, false))
    PARAM_CHECK(ar.Serialize("smoke", _smoke1, 1))
    PARAM_CHECK(ar.Serialize("smoke2", _smoke2, 1))
    PARAM_CHECK(ar.Serialize("smoke3", _smoke3, 1))
    PARAM_CHECK(ar.Serialize("dust", _dust, 1))
    PARAM_CHECK(ar.Serialize("size", _size, 1))
    PARAM_CHECK(ar.Serialize("dustTimeToLive", _dustTimeToLive, 1))
    return LSOK;
}

NetworkMessageType Crater::GetNMType(NetworkMessageClass cls) const
{
    switch (cls)
    {
        case NMCCreate:
            return NMTCreateCrater;
        case NMCUpdateDammage:
        case NMCUpdateGeneric:
            return NMTNone;
        default:
            return base::GetNMType(cls);
    }
}

#define CREATE_CRATER_MSG(XX) \
	XX(float, timeToLive, NDTFloat, NCTNone, DEFVALUE(float, 20), DOC_MSG("Time to live"), IdxTransfer) \
	XX(float, size, NDTFloat, NCTNone, DEFVALUE(float, 1), DOC_MSG("Size"), IdxTransfer) \
	XX(bool, isSmoke, NDTBool, NCTNone, DEFVALUE(bool, true), DOC_MSG("Source of smoke"), IdxTransfer) \
	XX(bool, isBlood, NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Source of blood"), IdxTransfer) \
	XX(bool, isWater, NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Source of water"), IdxTransfer) \
	XX(float, alpha, NDTFloat, NCTNone, DEFVALUE(float, 1), DOC_MSG("Transparency"), IdxTransfer)

DECLARE_NET_INDICES_EX(CreateCrater, CreateVehicle, CREATE_CRATER_MSG)
DEFINE_NET_INDICES_EX(CreateCrater, CreateVehicle, CREATE_CRATER_MSG)

} // namespace Poseidon

DEFINE_GET_INDICES(CreateCrater)

namespace Poseidon
{

NetworkMessageFormat& Crater::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    switch (cls)
    {
        case NMCCreate:
            base::CreateFormat(cls, format);
            CREATE_CRATER_MSG(MSG_FORMAT)
            break;
        default:
            base::CreateFormat(cls, format);
            break;
    }
    return format;
}

TMError Crater::TransferMsg(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCCreate:
            if (ctx.IsSending())
            {
                TMCHECK(base::TransferMsg(ctx))

                PoseidonAssert(dynamic_cast<const IndicesCreateCrater*>(ctx.GetIndices()))
                    const IndicesCreateCrater* indices = static_cast<const IndicesCreateCrater*>(ctx.GetIndices());

                ITRANSF(timeToLive)
                ITRANSF(size)
                ITRANSF(isSmoke)
                ITRANSF(isBlood)
                ITRANSF(isWater)
                ITRANSF(alpha)
            }
            break;
        default:
            TMCHECK(base::TransferMsg(ctx))
            break;
    }
    return TMOK;
}

Crater* Crater::CreateObject(NetworkMessageContext& ctx)
{
    Vehicle* veh = base::CreateObject(ctx);
    Crater* crater = dyn_cast<Crater>(veh);
    if (!crater)
    {
        return nullptr;
    }

    PoseidonAssert(dynamic_cast<const IndicesCreateCrater*>(ctx.GetIndices())) const IndicesCreateCrater* indices =
        static_cast<const IndicesCreateCrater*>(ctx.GetIndices());

    float timeToLive, size, alpha;
    bool smoke, blood, water;
    if (ctx.IdxTransfer(indices->timeToLive, timeToLive) != TMOK)
    {
        return nullptr;
    }
    if (ctx.IdxTransfer(indices->size, size) != TMOK)
    {
        return nullptr;
    }
    if (ctx.IdxTransfer(indices->isSmoke, smoke) != TMOK)
    {
        return nullptr;
    }
    if (ctx.IdxTransfer(indices->isBlood, blood) != TMOK)
    {
        return nullptr;
    }
    if (ctx.IdxTransfer(indices->isWater, water) != TMOK)
    {
        return nullptr;
    }
    if (ctx.IdxTransfer(indices->alpha, alpha) != TMOK)
    {
        return nullptr;
    }

    crater->SetTimeToLive(timeToLive);
    crater->_animationSpeed = 1.0 / timeToLive;
    crater->SetAlpha(alpha);
    crater->Init(timeToLive, size, smoke, blood, water);
    return crater;
}

DEFINE_FAST_ALLOCATOR(CraterOnVehicle)
DEFINE_CASTING(CraterOnVehicle)

CraterOnVehicle::CraterOnVehicle(LODShapeWithShadow* shape, VehicleNonAIType* type, float timeToLive, float size,
                                 Object* vehicle, Vector3Par position, Vector3Par direction)
    : Crater(shape, type, timeToLive, size, false, dyn_cast<VehicleWithBrain>(vehicle) != nullptr),
      AttachedOnVehicle(vehicle, position, direction)
{
    if (_vehicle != nullptr)
    {
        Matrix4 toWorld = _vehicle->WorldTransform();
        Matrix4 transf;
        transf.SetPosition(toWorld.FastTransform(_pos));
        transf.SetDirectionAndUp(toWorld.Rotate(_dir), toWorld.DirectionUp());
        Vehicle::SetTransform(transf);
    }
}

// used for serialization only
CraterOnVehicle::CraterOnVehicle(LODShapeWithShadow* shape, VehicleNonAIType* type)
    : Crater(shape, type), AttachedOnVehicle(nullptr, VZero, VForward)
{
    if (_vehicle != nullptr)
    {
        Matrix4 toWorld = _vehicle->WorldTransform();
        Matrix4 transf;
        transf.SetPosition(toWorld.FastTransform(_pos));
        transf.SetDirectionAndUp(toWorld.Rotate(_dir), toWorld.DirectionUp());
        Vehicle::SetTransform(transf);
    }
}

void CraterOnVehicle::UpdatePosition()
{
    if (_vehicle != nullptr)
    {
        Matrix4 toWorld = _vehicle->WorldTransform();
        Matrix4 transf;
        transf.SetPosition(toWorld.FastTransform(_pos));
        transf.SetDirectionAndUp(toWorld.Rotate(_dir), toWorld.DirectionUp());
        Move(transf);
    }
}

LSError CraterOnVehicle::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(AttachedOnVehicle::Serialize(ar))
    PARAM_CHECK(Crater::Serialize(ar))
    return LSOK;
}

NetworkMessageType CraterOnVehicle::GetNMType(NetworkMessageClass cls) const
{
    switch (cls)
    {
        case NMCCreate:
            return NMTCreateCraterOnVehicle;
        default:
            return base::GetNMType(cls);
    }
}

#define CREATE_CRATER_ON_VEHICLE_MSG(XX) \
	XX(OLink<Object>, vehicle, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Attached vehicle (where this crater is)"), IdxTransferRef) \
	XX(Vector3, pos, NDTVector, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Position on vehicle"), IdxTransfer) \
	XX(Vector3, dir, NDTVector, NCTNone, DEFVALUE(Vector3, VUp), DOC_MSG("Orientation on vehicle"), IdxTransfer)

DECLARE_NET_INDICES_EX(CreateCraterOnVehicle, CreateCrater, CREATE_CRATER_ON_VEHICLE_MSG)
DEFINE_NET_INDICES_EX(CreateCraterOnVehicle, CreateCrater, CREATE_CRATER_ON_VEHICLE_MSG)

} // namespace Poseidon

DEFINE_GET_INDICES(CreateCraterOnVehicle)

namespace Poseidon
{

NetworkMessageFormat& CraterOnVehicle::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    switch (cls)
    {
        case NMCCreate:
            base::CreateFormat(cls, format);
            CREATE_CRATER_ON_VEHICLE_MSG(MSG_FORMAT)
            break;
        default:
            base::CreateFormat(cls, format);
            break;
    }
    return format;
}

TMError CraterOnVehicle::TransferMsg(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCCreate:
            if (ctx.IsSending())
            {
                TMCHECK(base::TransferMsg(ctx))
            }
            {
                PoseidonAssert(dynamic_cast<const IndicesCreateCraterOnVehicle*>(ctx.GetIndices()))
                    const IndicesCreateCraterOnVehicle* indices =
                        static_cast<const IndicesCreateCraterOnVehicle*>(ctx.GetIndices());

                ITRANSF_REF(vehicle)
                ITRANSF(pos)
                ITRANSF(dir)
            }
            break;
        default:
            TMCHECK(base::TransferMsg(ctx))
            break;
    }
    return TMOK;
}

CraterOnVehicle* CraterOnVehicle::CreateObject(NetworkMessageContext& ctx)
{
    base* veh = base::CreateObject(ctx);
    CraterOnVehicle* crater = dyn_cast<CraterOnVehicle>(veh);
    if (!crater)
    {
        return nullptr;
    }

    if (crater->TransferMsg(ctx) != TMOK)
    {
        return nullptr;
    }
    return crater;
}

DEFINE_FAST_ALLOCATOR(Slop)
DEFINE_CASTING(Slop)

Slop::Slop(LODShapeWithShadow* shape, VehicleNonAIType* type, const Matrix4& trans, float timeToLive, float size)
    : Smoke(shape, type, timeToLive, timeToLive)
{
    _growSize = 0;
    _origTransform = trans;

    Init(timeToLive, size);
}

void Slop::Init(float timeToLive, float size)
{
    float fadeIn = 0;
    float fadeOut = 0.25 * timeToLive;
    SetFades(fadeIn, fadeOut);

    float growUp = 0.11 * timeToLive;
    SetGrowUp(growUp, size);

    SetTimeToLive(timeToLive - fadeIn - fadeOut);
}

void Slop::Simulate(float deltaT, SimulationImportance prec)
{
    _animation += deltaT * _animationSpeed;
    if (_animation >= 1.0)
    {
        _animation = 1.0;
    }

    if (_growUpTime > 0)
    {
        _growSize = 1 - _growUpTime * _growUpInv;
        _growUpTime -= deltaT;
    }
    else
    {
        _growSize = 1;
    }

    float scale = _growSize * _size;
    saturateMax(scale, 0.1);

    Matrix4 trans;
    trans.SetOrientation(_origTransform.Orientation() * scale);
    trans.SetPosition(_origTransform.Position());
    SetTransform(trans);

    if (_fadeInTime > 0)
    {
        _fadeValue = 1 - _fadeInTime * _fadeInInv;
        _fadeInTime -= deltaT;
    }
    else if (_timeToLive > 0)
    {
        _fadeValue = 1.0;
        _timeToLive -= deltaT;
    }
    else if (_fadeOutTime > 0)
    {
        _fadeValue = _fadeOutTime * _fadeOutInv;
        _fadeOutTime -= deltaT;
        _firstLoop = false; // if fading is valid, we never wait for loop
    }
    else
    {
        if (_firstLoop)
        {
            _fadeValue = 1;
        }
        else
        {
            _fadeValue = 0;
            _invisible = true;
            _delete = true;
        }
    }
    int alpha255 = toIntFloor(_alpha * _fadeValue * 255);
    saturate(alpha255, 0, 255);
    _constantColor = PackedColorRGB(_constantColor, alpha255);

    Entity::Simulate(deltaT, prec);
}

DEFINE_FAST_ALLOCATOR(Explosion)

// Lazy initialization to avoid SIOF
static const RStringB& GetExplosionName()
{
    static const RStringB ExplosionName = "Explosion";
    return ExplosionName;
}

Explosion::Explosion(LODShapeWithShadow* shape, Vehicle* owner, float duration)
    : Vehicle(nullptr, VehicleTypes.New(GetExplosionName()), -1), _soundDone(false), _exSize(duration), _owner(owner)
{
    SetSimulationPrecision(1.0 / 5);
    float coef = (1.0 / 0.6) * _exSize;
    float invCoef = 1.0 / coef;
    _fire.Load(Pars >> "CfgCloudlets" >> "Explosion");
    _fire.SetSize(_fire.GetSize(), coef * _fire.GetSourceSize());
    _fire.SetInterval(Square(invCoef) * _fire.GetInterval());
    _fire.Start(duration * 0.05);
    _minLightTime = 0.2;
}

Explosion::~Explosion() = default;

void Explosion::Simulate(float deltaT, SimulationImportance prec)
{
    _minLightTime -= deltaT;

    if (prec > SimulateInvisibleNear)
    {
        _delete = true;
    }
    else
    {
        if (_fire.Active())
        {
            _fire.Simulate(Position(), Speed(), _exSize * 0.7, deltaT);
        }
        if (!_fire.Active() && _minLightTime <= 0)
        {
            _delete = true;
        }
    }

    if (_delete || !_fire.Active())
    {
        if (GLOB_WORLD->CameraOn() == this && _owner != nullptr)
        {
            GLOB_WORLD->SwitchCameraTo(_owner, GLOB_WORLD->GetCameraType());
        }
    }

    if ((_fire.Active() || _minLightTime > 0) && prec <= SimulateInvisibleNear && (ENGINE_CONFIG.lights & LIGHT_EXPLO))
    {
        if (!_light)
        {
            _light = new LightPoint(HBlack, HBlack);
            GLOB_SCENE->AddLight(_light);
        }
        if (_light)
        {
            float intensity = 1.0;
            saturateMax(intensity, 0);
            _light->SetPosition(Position());
            _light->SetDiffuse(ExploColor * intensity);
            _light->SetAmbient(ExploAmbient * intensity);
            _light->SetBrightness(10 * _exSize);
        }
    }
    else
    {
        _light.Free();
    }
}

void Explosion::Sound(bool inside, float deltaT) {}

LSError Explosion::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar))
    PARAM_CHECK(ar.Serialize("_fire", _fire, 1))
    PARAM_CHECK(ar.Serialize("_soundDone", _soundDone, 1))
    /* Note: ?? serialize
    float _exSize;
    Ref<LightPoint> _light;
    OLink<Vehicle> _owner; // who owned the shot
    */
    return LSOK;
}

NetworkMessageType Explosion::GetNMType(NetworkMessageClass cls) const
{
    switch (cls)
    {
        case NMCCreate:
            return NMTCreateExplosion;
        case NMCUpdateGeneric:
        case NMCUpdateDammage:
            return NMTNone;
        default:
            return base::GetNMType(cls);
    }
}

#define CREATE_EXPLOSION_MSG(XX) \
	XX(OLink<Vehicle>, owner, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Who is responsible for explosion"), IdxTransferRef) \
	XX(float, duration, NDTFloat, NCTNone, DEFVALUE(float, 1), DOC_MSG("Duration of explosion"), IdxTransfer)

DECLARE_NET_INDICES_EX(CreateExplosion, CreateVehicle, CREATE_EXPLOSION_MSG)
DEFINE_NET_INDICES_EX(CreateExplosion, CreateVehicle, CREATE_EXPLOSION_MSG)

} // namespace Poseidon

DEFINE_GET_INDICES(CreateExplosion)

namespace Poseidon
{

NetworkMessageFormat& Explosion::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    switch (cls)
    {
        case NMCCreate:
            base::CreateFormat(cls, format);
            CREATE_EXPLOSION_MSG(MSG_FORMAT)
            break;
        default:
            base::CreateFormat(cls, format);
            break;
    }
    return format;
}

TMError Explosion::TransferMsg(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCCreate:
            if (ctx.IsSending())
            {
                TMCHECK(base::TransferMsg(ctx))

                PoseidonAssert(dynamic_cast<const IndicesCreateExplosion*>(ctx.GetIndices()))
                    const IndicesCreateExplosion* indices =
                        static_cast<const IndicesCreateExplosion*>(ctx.GetIndices());

                ITRANSF_REF(owner)
                TMCHECK(ctx.IdxTransfer(indices->duration, _exSize))
            }
            break;
        default:
            TMCHECK(base::TransferMsg(ctx))
            break;
    }
    return TMOK;
}

Explosion* Explosion::CreateObject(NetworkMessageContext& ctx)
{
    base* veh = base::CreateObject(ctx);
    Explosion* explosion = dyn_cast<Explosion>(veh);
    if (!explosion)
    {
        return nullptr;
    }

    PoseidonAssert(dynamic_cast<const IndicesCreateExplosion*>(ctx.GetIndices()))
        const IndicesCreateExplosion* indices = static_cast<const IndicesCreateExplosion*>(ctx.GetIndices());

    float duration;
    if (ctx.IdxTransferRef(indices->owner, explosion->_owner) != TMOK)
    {
        return nullptr;
    }
    if (ctx.IdxTransfer(indices->duration, duration) != TMOK)
    {
        return nullptr;
    }
    explosion->_exSize = duration;
    float invDuration = 1.0 / duration;
    explosion->_fire.SetSize(explosion->_fire.GetSize(), duration * explosion->_fire.GetSourceSize());
    explosion->_fire.SetInterval(Square(invDuration) * explosion->_fire.GetInterval());
    explosion->_fire.Start(duration * 0.05);
    return explosion;
}

RString GetString(const ParamEntry& cls, const char* name);

DEFINE_FAST_ALLOCATOR(CParticle)

CParticle::CParticle(LODShapeWithShadow* Shape, RString AnimationName, EParticleType PType, float TimerPeriod,
                     float LifeTime, Vector3Par Position, Vector3Par MoveVelocity, float RotationVelocity, float Weight,
                     float Volume, float Rubbing, AutoArray<float> Size, AutoArray<Color> PColor,
                     AutoArray<float> AnimationPhase, float RandomDirectionPeriod, float RandomDirectionIntensity,
                     RString OnTimerScript, RString BeforeDestroyScript)
    : Vehicle(Shape, SmokeType(), -1)
{
    // Create random orientation
    Matrix3 RX;
    RX.SetRotationX(((float)rand() / RAND_MAX) * 2.0f * H_PI);
    Matrix3 RY;
    RY.SetRotationY(((float)rand() / RAND_MAX) * 2.0f * H_PI);
    Matrix3 RZ;
    RZ.SetRotationZ(((float)rand() / RAND_MAX) * 2.0f * H_PI);
    Matrix3 Orientation = RZ * RY * RX;

    // Create random axis to rotate around
    Vector3 Dir(((float)rand()) / RAND_MAX, ((float)rand()) / RAND_MAX, ((float)rand()) / RAND_MAX);
    Vector3 Up;
    // The unlikely case - all 3 scalars are set to 0 - we will take a x axis (or anything else)
    if ((Dir[0] == 0.0f) && (Dir[1] == 0.0f) && (Dir[2] == 0.0f))
    {
        Dir[0] = 1.0f;
        Up = Vector3(0.0f, 1.0f, 0.0f);
    }
    else
    {
        Up = Dir.CrossProduct(Vector3(1.0f, 0.0f, 0.0f));
        // The unlikely case - Up lies on the x axis - we will use y axis for the scalar product (or anything else)
        if ((Up[0] == 0.0f) && (Up[1] == 0.0f) && (Up[2] == 0.0f))
        {
            Up = Dir.CrossProduct(Vector3(0.0f, 1.0f, 0.0f));
        }
    }
    Matrix3 RotOrientation0;
    RotOrientation0.SetDirectionAndUp(Dir, Up);
    _InvRotOrientation0 = RotOrientation0.InverseRotation();
    _RotOrientation0_m_Orientation = RotOrientation0 * Orientation;

    // Assign values
    _Type = PType;
    _TimerPeriod = TimerPeriod;
    _Lifetime = LifeTime;
    _Position = Position;
    _MoveVelocity = MoveVelocity;
    _RotationVelocity = RotationVelocity;
    _Weight = Weight;
    _Volume = Volume;
    _Rubbing = Rubbing;
    _Size = Size;
    _Color = PColor;
    _AnimationPhase = AnimationPhase;
    _RandomDirectionPeriod = RandomDirectionPeriod;
    _RandomDirectionIntensity = RandomDirectionIntensity;
    _OnTimerScript = OnTimerScript;
    _BeforeDestroyScript = BeforeDestroyScript;

    // Animation
    if (AnimationName.GetLength() > 0)
    {
        _Skeleton = new Skeleton();
        AnimationRTName Name;
        Name.name = GetAnimationName(AnimationName);
        Name.skeleton = _Skeleton;
        _Animation = new AnimationRT(Name, false);
        _Animation->Prepare(_shape, _Skeleton, _Weights, false);
    }

    // Alpha setting
    const int cloudletSpec = ClampU | ClampV | NoZWrite | IsAlpha | IsAlphaFog | NoShadow | IsColored;
    Shape->OrSpecial(cloudletSpec);

    // Initialize rest of the values
    _Age = 0.0f;
    _Rotation = 0.0f;
    _LastOnTimerScriptCalling = 0.0f;
}

CParticle::~CParticle() = default;

void CParticle::Simulate(float deltaT, SimulationImportance prec)
{
    float ShrinkedAge = _Age / _Lifetime;

    // --- Update age and return in case it exceeded lifetime. ---
    _Age += deltaT;
    if (_Age > _Lifetime)
    {
        // BeforeDestroy script calling
        if (_BeforeDestroyScript != RString(""))
        {
            // Create parameters of the script
            GameArrayType GA(3);
            GA.Resize(3);
            GA[0] = _Position[0];
            GA[1] = _Position[2];
            GA[2] = _Position[1] - GLandscape->SurfaceYAboveWater(_Position.X(), _Position.Z());
            GameValue GV(GA);
            // Create a instance of the script and call it
            Script* script = new Script(_BeforeDestroyScript, GV);
            GWorld->AddScript(script);
        }
        SetDelete();
        return;
    }

    // --- OnTimer script calling ---
    float Age_Minus_TimerPeriod = _Age - _TimerPeriod;
    while (_LastOnTimerScriptCalling < Age_Minus_TimerPeriod)
    {
        if (_OnTimerScript != RString(""))
        {
            // Create parameters of the script
            GameArrayType GA(3);
            GA.Resize(3);
            GA[0] = _Position[0];
            GA[1] = _Position[2];
            GA[2] = _Position[1] - GLandscape->SurfaceYAboveWater(_Position.X(), _Position.Z());
            GameValue GV(GA);
            // Create a instance of the script and call it
            Script* script = new Script(_OnTimerScript, GV);
            GWorld->AddScript(script);
        }
        _LastOnTimerScriptCalling += _TimerPeriod;
    }

    // --- Update position ---
    Vector3 MoveVelocity = _MoveVelocity;
    // Gravity
    MoveVelocity += Vector3(0.0f, -9.81f * deltaT, 0.0f);
    // Wind and rubbing
    Vector3 WindVelocity = GLandscape->GetWind();
    Vector3 WindMoveVelocity = (WindVelocity - _MoveVelocity);
    MoveVelocity += ((WindMoveVelocity * WindMoveVelocity.Size() * _Rubbing) / (_Weight + 1)) * deltaT;
    // Lift vector using lift force
    MoveVelocity += Vector3(0.0f, (_Volume * 9.81f * 1.275 / _Weight) * deltaT, 0.0f);
    // Random value
    if (((float)rand() / RAND_MAX) < (deltaT / _RandomDirectionPeriod))
    {
        float RIntensity = _RandomDirectionIntensity;
        MoveVelocity[0] += ((float)rand() / RAND_MAX) * RIntensity * 2 - RIntensity;
        MoveVelocity[1] += ((float)rand() / RAND_MAX) * RIntensity * 2 - RIntensity;
        MoveVelocity[2] += ((float)rand() / RAND_MAX) * RIntensity * 2 - RIntensity;
    }
    // Update position
    _Position += MoveVelocity * deltaT;
    _MoveVelocity = MoveVelocity;

    // --- Update orientation ---
    float RotationVelocity = _RotationVelocity;
    // Rubbing and weight
    RotationVelocity += (-RotationVelocity * _Rubbing / (_Weight + 1)) * deltaT;
    // Update rotation
    _Rotation += RotationVelocity * deltaT;
    _RotationVelocity = RotationVelocity;
    // Rotation
    Matrix3 RotZ;
    RotZ.SetRotationZ(_Rotation * 2 * H_PI);
    Matrix3 Orientation = _RotOrientation0_m_Orientation * RotZ * _InvRotOrientation0;

    // Size
    float AuxFloat;
    AA_GETVALUELINEAR(_Size, ShrinkedAge, AuxFloat);

    // --- Apply all transformations ---
    Matrix4 Trans;
    Trans.SetIdentity();
    Trans.SetOrientation(Orientation * AuxFloat);
    Trans.SetPosition(_Position);
    SetTransform(Trans);

    // --- Set color ---
    Color AuxColor;
    AA_GETVALUELINEAR(_Color, ShrinkedAge, AuxColor);
    _constantColor = PackedColor(AuxColor);
}

bool CParticle::IsAnimated(int level) const
{
    return true;
}

bool CParticle::IsAnimatedShadow(int level) const
{
    return true;
}

void CParticle::Animate(int level)
{
    // Get phase
    float Phase;
    AA_GETVALUELINEAR(_AnimationPhase, _Age / _Lifetime, Phase);
    // animate all animated textures
    Shape* shape = _shape->Level(level);
    if (!shape)
    {
        return;
    }
    if (shape->NFaces() <= 0)
    {
        return;
    }
    // normal smoke is single-LOD single face object
    Offset beg = shape->BeginFaces();
    Offset end = shape->EndFaces();
    for (Offset o = beg; o < end; shape->NextFace(o))
    {
        Poly& face = shape->Face(o);
        if (face.Special() & ::IsAnimated)
        {
            face.AnimateTexture(Phase);
        }
    }

    if (_Animation)
    {
        _Animation->Apply(_Weights, _shape, level, Phase);
    }
}

void CParticle::Deanimate(int level)
{
    // unanimated smoke is nonsense and should never exist

    if (_Animation)
    {
        _Animation->Apply(_Weights, _shape, level, 0);
    }
}

void CParticle::Draw(int level, ClipFlags clipFlags, const FrameBase& frame)
{
    PoseidonAssert(level != LOD_INVISIBLE);
    switch (_Type)
    {
        case PT_Billboard:
            DrawDecal(level, clipFlags, frame);
            break;
        case PT_SpaceObject:
            base::Draw(level, clipFlags, frame);
            break;
    }
}

bool GetVector3(Vector3& ret, GameValuePar oper)
{
    const GameArrayType& Array = oper;
    if (Array.Size() != 3)
    {
        return false;
    }
    if (Array[0].GetType() != GameScalar || Array[1].GetType() != GameScalar || Array[2].GetType() != GameScalar)
    {
        return false;
    }
    ret[0] = Array[0];
    ret[1] = Array[2];
    ret[2] = Array[1];
    return true;
}

bool GetColor(Color& ret, GameValuePar oper)
{
    const GameArrayType& Array = oper;
    if (Array.Size() != 4)
    {
        return false;
    }
    if (Array[0].GetType() != GameScalar || Array[1].GetType() != GameScalar || Array[2].GetType() != GameScalar ||
        Array[3].GetType() != GameScalar)
    {
        return false;
    }
    ret = ColorP(Array[0], Array[1], Array[2], Array[3]);
    return true;
}

bool IsParticleDropArgumentCountSupported(int count)
{
    return count == 18 || count == 19;
}

GameValue ParticleDrop(const GameState* state, GameValuePar oper1)
{
    // GetPos is the global ::GetPos (declared at file scope above).

    const GameArrayType& Array = oper1;

    // In case number of parameters doesn't match required number.
    if (!IsParticleDropArgumentCountSupported(Array.Size()))
    {
        state->SetError(EvalDim, Array.Size(), 19);
        return GameValue();
    }

    // Match the required types of parameters and get its values

    // ShapeName
    if (Array[0].GetType() != GameString)
    {
        state->TypeError(GameString, Array[0].GetType());
        return GameValue();
    }

    // AnimationName
    if (Array[1].GetType() != GameString)
    {
        state->TypeError(GameString, Array[1].GetType());
        return GameValue();
    }

    // _Type
    if (Array[2].GetType() != GameString)
    {
        state->TypeError(GameString, Array[2].GetType());
        return GameValue();
    }
    EParticleType ParticleType;
    RString STRPT = Array[2];
    if (STRPT == RString("Billboard"))
    {
        ParticleType = PT_Billboard;
    }
    else if (STRPT == RString("SpaceObject"))
    {
        ParticleType = PT_SpaceObject;
    }
    else
    {
        state->SetError(EvalGen);
        return GameValue();
    }

    // _TimerPeriod
    if (Array[3].GetType() != GameScalar)
    {
        state->TypeError(GameScalar, Array[3].GetType());
        return GameValue();
    }

    // _LifeTime
    if (Array[4].GetType() != GameScalar)
    {
        state->TypeError(GameScalar, Array[4].GetType());
        return GameValue();
    }

    // _MoveVelocity
    if (Array[6].GetType() != GameArray)
    {
        state->TypeError(GameArray, Array[6].GetType());
        return GameValue();
    }
    Vector3 MoveVelocity;
    if (!GetVector3(MoveVelocity, Array[6]))
    {
        state->SetError(EvalGen);
        return GameValue();
    }

    // _RotationVelocity
    if (Array[7].GetType() != GameScalar)
    {
        state->TypeError(GameScalar, Array[7].GetType());
        return GameValue();
    }

    // _Weight
    if (Array[8].GetType() != GameScalar)
    {
        state->TypeError(GameScalar, Array[8].GetType());
        return GameValue();
    }

    // _Volume
    if (Array[9].GetType() != GameScalar)
    {
        state->TypeError(GameScalar, Array[9].GetType());
        return GameValue();
    }

    // _Rubbing
    if (Array[10].GetType() != GameScalar)
    {
        state->TypeError(GameScalar, Array[10].GetType());
        return GameValue();
    }

    // _Size
    if (Array[11].GetType() != GameArray)
    {
        state->TypeError(GameArray, Array[11].GetType());
        return GameValue();
    }
    const GameArrayType& ArraySize = Array[11];
    AutoArray<float> Size(ArraySize.Size());
    Size.Resize(ArraySize.Size());
    for (int i = 0; i < ArraySize.Size(); i++)
    {
        if (ArraySize[i].GetType() != GameScalar)
        {
            state->TypeError(GameScalar, ArraySize[i].GetType());
            return GameValue();
        }
        Size[i] = ArraySize[i];
    }

    // _Color
    if (Array[12].GetType() != GameArray)
    {
        state->TypeError(GameArray, Array[12].GetType());
        return GameValue();
    }
    const GameArrayType& ArrayColor = Array[12];
    AutoArray<Color> PColor(ArrayColor.Size());
    PColor.Resize(ArrayColor.Size());
    for (int i = 0; i < ArrayColor.Size(); i++)
    {
        if (ArrayColor[i].GetType() != GameArray)
        {
            state->TypeError(GameArray, ArrayColor[i].GetType());
            return GameValue();
        }
        Color AuxColor;
        if (!GetColor(AuxColor, ArrayColor[i]))
        {
            state->SetError(EvalGen);
            return GameValue();
        }
        PColor[i] = AuxColor;
    }

    // _AnimationPhase
    if (Array[13].GetType() != GameArray)
    {
        state->TypeError(GameArray, Array[13].GetType());
        return GameValue();
    }
    const GameArrayType& ArrayAnimationPhase = Array[13];
    AutoArray<float> AnimationPhase(ArrayAnimationPhase.Size());
    AnimationPhase.Resize(ArrayAnimationPhase.Size());
    for (int i = 0; i < ArrayAnimationPhase.Size(); i++)
    {
        if (ArrayAnimationPhase[i].GetType() != GameScalar)
        {
            state->TypeError(GameScalar, ArrayAnimationPhase[i].GetType());
            return GameValue();
        }
        AnimationPhase[i] = ArrayAnimationPhase[i];
    }

    // _RandomDirectionPeriod
    if (Array[14].GetType() != GameScalar)
    {
        state->TypeError(GameScalar, Array[14].GetType());
        return GameValue();
    }

    // _RandomDirectionIntensity
    if (Array[15].GetType() != GameScalar)
    {
        state->TypeError(GameScalar, Array[15].GetType());
        return GameValue();
    }

    // OnTimer
    if (Array[16].GetType() != GameString)
    {
        state->TypeError(GameString, Array[16].GetType());
        return GameValue();
    }

    // BeforeDestroy
    if (Array[17].GetType() != GameString)
    {
        state->TypeError(GameString, Array[17].GetType());
        return GameValue();
    }

    // Object
    Vector3 Position;
    if (Array.Size() == 19 && Array[18].GetType() == GameObject)
    {
        const Object* pObject = static_cast<GameDataObject*>(Array[18].GetData())->GetObject();
        if (pObject == nullptr)
        {
            return GameValue();
        }
        if (Array[5].GetType() == GameArray)
        {
            if (!GetVector3(Position, Array[5]))
            {
                state->SetError(EvalGen);
                return GameValue();
            }
        }
        else if (Array[5].GetType() == GameString)
        {
            RString sPosition;
            sPosition = Array[5];
            Position = pObject->GetShape()->MemoryPoint(sPosition);
        }
        else
        {
            state->TypeError(GameArray, Array[5].GetType());
            return GameValue();
        }
        Position = pObject->PositionModelToWorld(Position);
        MoveVelocity = pObject->DirectionModelToWorld(MoveVelocity);
    }
    else
    {
        // In case there is no object assigned and position reference is not an array...
        if (Array[5].GetType() != GameArray)
        {
            state->TypeError(GameArray, Array[5].GetType());
            return GameValue();
        }
        else
        {
            if (!GetVector3(Position, Array[5]))
            {
                state->SetError(EvalGen);
                return GameValue();
            }
        }
    }

    LODShapeWithShadow* shape = Shapes.New(GetShapeName(Array[0]), false, false);
    if (shape)
    {
        CParticle* Particle = new CParticle(shape, Array[1], ParticleType, Array[3], Array[4], Position, MoveVelocity,
                                            Array[7], Array[8], Array[9], Array[10], Size, PColor, AnimationPhase,
                                            Array[14], Array[15], Array[16], Array[17]);
        GLOB_WORLD->AddCloudlet(Particle);
    }

    return GameValue();
}
} // namespace Poseidon

SmokeSourceVehicle::~SmokeSourceVehicle() {}
