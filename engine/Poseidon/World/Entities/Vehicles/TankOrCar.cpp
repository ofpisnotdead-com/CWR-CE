
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/Entities/Vehicles/TankOrCar.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Random/randomGen.hpp>

#include <Poseidon/Graphics/Rendering/Draw/SpecLods.hpp>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

namespace Poseidon
{
using namespace Foundation;

TankOrCarType::TankOrCarType(const ParamEntry* param)
    : base(param), _pilotPos(VZero), _outPilotPos(VZero), _outPilotOnTurret(false)
{
}

void TankOrCarType::Load(const ParamEntry& par)
{
    base::Load(par);
    _canFloat = par >> "canFloat";
}

void TankOrCarType::InitShape()
{
    base::InitShape();

    _brakeLights.Init(_shape, "brzdove svetlo", nullptr);

    const ParamEntry& par = *_par;

    _speedIndicator.Init(_shape, par >> "IndicatorSpeed");
    _speedIndicator2.Init(_shape, par >> "IndicatorSpeed2");
    _rpmIndicator.Init(_shape, par >> "IndicatorRPM");

    int level = _shape->FindLevel(VIEW_CARGO);
    if (level >= 0)
    {
        Shape* oShape = _shape->LevelOpaque(level);
        oShape->MakeCockpit();
    }
    level = _shape->FindLevel(VIEW_PILOT);
    if (level >= 0)
    {
        Shape* oShape = _shape->LevelOpaque(level);
        oShape->MakeCockpit();
    }
    level = _shape->FindLevel(0);
    if (level >= 0)
    {
        Shape* oShape = _shape->LevelOpaque(level);
        _outPilotPos = oShape->NamedPosition("pilot");
        int index = oShape->FindNamedSel("otocVez");
        int pIndex = oShape->PointIndex("pilot");
        if (index >= 0 && pIndex >= 0)
        {
            const Selection& turret = oShape->NamedSel(index);
            if (turret.IsSelected(pIndex))
            {
                _outPilotOnTurret = true;
            }
        }
    }

    GetValue(_gearSound, par >> "soundGear");
    _hasExhaust = _shape->MemoryPointExists("vyfuk start");
    Vector3 beg = _shape->MemoryPoint("vyfuk start");
    Vector3 end = _shape->MemoryPoint("vyfuk konec");
    _exhaustPos = end;
    _exhaustDir = (end - beg);
    _exhaustDir.Normalize();

    DEF_HIT_CFG(_shape, _engineHit, par >> "HitEngine", GetArmor());
}

TankOrCar::TankOrCar(VehicleType* name, Person* driver)
    : base(name, driver), _pilotBrake(false), _lastPilotBrake(Glob.time - 60), _freeFallUntil(Glob.time - 60)
{
    float exhaustSize = GetMass() * (1.0 / 16000);
    saturate(exhaustSize, 0.3, 3);
    _exhaust.SetSize(exhaustSize);
}

void TankOrCar::AnimateSpeedIndicator(Matrix4& trans, int level)
{
    trans = MIdentity;
    int selection = Type()->_speedIndicator.GetSelection(level);
    if (selection >= 0)
    {
        AnimateMatrix(trans, level, selection);
    }
}

void TankOrCar::Animate(int level)
{
    if (!_shape->Level(level))
    {
        return;
    }
    float value = fabs(ModelSpeed()[2]);
    Matrix4 speedAnim;
    AnimateSpeedIndicator(speedAnim, level);

    Type()->_speedIndicator.SetValue(_shape, level, value, speedAnim);
    Type()->_speedIndicator2.SetValue(_shape, level, value);
    value = _rpm;
    Type()->_rpmIndicator.SetValue(_shape, level, value);

    // avoid flashing brake lights
    if (_pilotBrake && EngineIsOn())
    {
        _lastPilotBrake = Glob.time;
    }
    if (_lastPilotBrake > Glob.time - 0.4)
    {
        Type()->_brakeLights.Unhide(_shape, level);
    }
    else
    {
        Type()->_brakeLights.Hide(_shape, level);
    }

    base::Animate(level);
}

void TankOrCar::Deanimate(int level)
{
    if (!_shape->Level(level))
    {
        return;
    }
    base::Deanimate(level);
}

void TankOrCar::Simulate(float deltaT, SimulationImportance prec)
{
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
}

float TankOrCar::GetSteerAheadSimul() const
{
    float val = base::GetSteerAheadSimul();
    if (Type()->_canFloat && _waterContact && !_landContact)
    {
        val *= 8;
    }
    return val;
}

float TankOrCar::GetSteerAheadPlan() const
{
    float val = base::GetSteerAheadPlan();
    if (Type()->_canFloat && _waterContact && !_landContact)
    {
        val *= 8;
    }
    return val;
}

float TankOrCar::GetPrecision() const
{
    float val = base::GetPrecision();
    if (Type()->_canFloat && _waterContact)
    {
        val *= 3;
    }
    return val;
}

void TankOrCar::SimulateExhaust(float deltaT, SimulationImportance prec)
{
    if (!EnableVisualEffects(prec))
    {
        return;
    }
    // simulate exhaust smoke
    if (Type()->_hasExhaust && EngineIsOn())
    {
        Vector3 exhaustPos = PositionModelToWorld(Type()->_exhaustPos);
        float moreThrust = ThrustWanted() - Thrust();
        saturate(moreThrust, 0, 0.2);
        float dammage = GetTotalDammage();
        float intensity = Thrust() * 0.3 + moreThrust * 3 + 0.1;
        if (_gearSound)
        {
            intensity = 1; // gear change produces a visible puff
        }
        intensity += dammage * 0.2;
        intensity *= 1 + dammage;
        float alpha = intensity * 0.3;
        float density = intensity * 0.5 + 0.05;
        _exhaust.SetAlpha(alpha);
        Vector3 cSpeed = DirectionModelToWorld(Type()->_exhaustDir);
        cSpeed *= 9 * floatMin(1, _exhaust.GetSize());

        static Color normColor(0.1, 0.2, 0.35);
        _exhaust.SetColor(normColor * (1 - dammage));
        _exhaust.Simulate(exhaustPos + _speed * 0.1, cSpeed + _speed * 0.7, density, deltaT);
    }
}

NetworkMessageType TankOrCar::GetNMType(NetworkMessageClass cls) const
{
    switch (cls)
    {
        case NMCUpdateGeneric:
            return NMTUpdateTankOrCar;
        default:
            return base::GetNMType(cls);
    }
}

DEFINE_NET_INDICES_EX_ERR(UpdateTankOrCar, UpdateTransport, UPDATE_TANK_OR_CAR_MSG)

} // namespace Poseidon

DEFINE_GET_INDICES(UpdateTankOrCar)

namespace Poseidon
{

NetworkMessageFormat& TankOrCar::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    switch (cls)
    {
        case NMCUpdateGeneric:
            base::CreateFormat(cls, format);
            UPDATE_TANK_OR_CAR_MSG(MSG_FORMAT_ERR)
            break;
        default:
            base::CreateFormat(cls, format);
            break;
    }
    return format;
}

TMError TankOrCar::TransferMsg(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
            TMCHECK(base::TransferMsg(ctx))
            {
                PoseidonAssert(dynamic_cast<const IndicesUpdateTankOrCar*>(ctx.GetIndices()))
                    const IndicesUpdateTankOrCar* indices =
                        static_cast<const IndicesUpdateTankOrCar*>(ctx.GetIndices());

                ITRANSF(pilotBrake)
            }
            break;
        default:
            return base::TransferMsg(ctx);
    }
    return TMOK;
}

float TankOrCar::CalculateError(NetworkMessageContext& ctx)
{
    float error = 0;
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
            error += base::CalculateError(ctx);
            {
                PoseidonAssert(dynamic_cast<const IndicesUpdateTankOrCar*>(ctx.GetIndices()))
                    const IndicesUpdateTankOrCar* indices =
                        static_cast<const IndicesUpdateTankOrCar*>(ctx.GetIndices());

                ICALCERR_NEQ(bool, pilotBrake, ERR_COEF_MODE)
            }
            break;
        default:
            error += base::CalculateError(ctx);
            break;
    }
    return error;
}

} // namespace Poseidon
