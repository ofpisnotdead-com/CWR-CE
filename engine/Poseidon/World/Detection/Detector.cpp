
#include <Poseidon/World/Detection/Detector.hpp>
#include <Random/randomGen.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/Entities/Infantry/Person.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/World/World.hpp>

#include <Poseidon/Audio/DynSound.hpp>

#include <Poseidon/AI/ArcadeTemplate.hpp>

#include <Poseidon/Game/Commands/GameStateExt.hpp>

#include <Poseidon/Network/Network.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>

#include <Poseidon/Game/UiActions.hpp>
#include <cmath>
#include <utility>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/platform.hpp>

using namespace Poseidon;
namespace Poseidon
{
const ParamEntry* FindMusic(RString, SoundPars&);
const ParamEntry* FindRscTitle(RString name);
void FindEnvSound(RString name, SoundPars& day, SoundPars& night);
} // namespace Poseidon

namespace Poseidon::Foundation
{
template class Ref<LightPointOnVehicle>;
} // namespace Poseidon::Foundation
DEFINE_CASTING(Detector)

Detector::Detector(EntityType* name, int id) : Vehicle(name->GetShape(), name, id)
{
    const float prec = 0.5;
    _simulationSkipped = GRandGen.RandomValue() * prec;
    SetSimulationPrecision(prec);
    SetType(TypeVehicle);

    _a = 50;
    _b = 50;
    _e = 0;
    _sinAngle = 0;
    _cosAngle = 1;
    _rectangular = false;

    _activationBy = ASANone;
    _activationType = ASATPresent;
    _repeating = true;
    _timeoutMin = 0;
    _timeoutMid = 0;
    _timeoutMax = 0;
    _interruptable = false;

    _action = ASTNone;

    _text = "";

    _expCond = "this";
    _expActiv = "";
    _expDesactiv = "";

    _assignedStatic = -1;
    _assignedVehicle = -1;

    _effects.Init();

    _nextCheck = Glob.time + 2 + GRandGen.RandomValue() * 2;
    _active = false;
    _activeCountdown = false;

    _vehicles = new GameValue();
}

Detector::~Detector() = default;

int Detector::NVehicles() const
{
    if (_vehicles->GetType() != GameArray)
    {
        return 0;
    }
    GameArrayType& vehicles = *_vehicles;
    return vehicles.Size();
}

const EntityAI* Detector::GetVehicle(int i) const
{
    if (_vehicles->GetType() != GameArray)
    {
        return nullptr;
    }

    GameArrayType& vehicles = *_vehicles;
    Object* obj = static_cast<const GameDataObject*>(vehicles[i].GetData())->GetObject();
    return static_cast<const EntityAI*>(obj);
}

EntityAI* Detector::GetVehicle(int i)
{
    if (_vehicles->GetType() != GameArray)
    {
        return nullptr;
    }

    GameArrayType& vehicles = *_vehicles;
    Object* obj = static_cast<const GameDataObject*>(vehicles[i].GetData())->GetObject();
    return static_cast<EntityAI*>(obj);
}

const GameValue& Detector::GetGameValue() const
{
    return *_vehicles;
}

LSError Detector::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar))
    PARAM_CHECK(ar.Serialize("a", _a, 1))
    PARAM_CHECK(ar.Serialize("b", _b, 1))
    PARAM_CHECK(ar.Serialize("sinAngle", _sinAngle, 1))
    PARAM_CHECK(ar.Serialize("cosAngle", _cosAngle, 1, sqrt(1 - Square(_sinAngle))))

    PARAM_CHECK(ar.Serialize("rectangular", _rectangular, 1))
    PARAM_CHECK(ar.SerializeEnum("activationBy", _activationBy, 1))
    PARAM_CHECK(ar.SerializeEnum("activationType", _activationType, 1))
    PARAM_CHECK(ar.Serialize("repeating", _repeating, 1))
    PARAM_CHECK(ar.Serialize("timeoutMin", _timeoutMin, 1))
    PARAM_CHECK(ar.Serialize("timeoutMid", _timeoutMid, 1))
    PARAM_CHECK(ar.Serialize("timeoutMax", _timeoutMax, 1))
    PARAM_CHECK(ar.Serialize("interruptable", _interruptable, 1))
    PARAM_CHECK(ar.SerializeEnum("action", _action, 1))
    PARAM_CHECK(ar.SerializeRef("assignedGroup", _assignedGroup, 1))
    PARAM_CHECK(ar.Serialize("assignedStatic", _assignedStatic, 1))
    PARAM_CHECK(ar.Serialize("assignedVehicle", _assignedVehicle, 1))
    PARAM_CHECK(ar.Serialize("text", _text, 1))
    PARAM_CHECK(ar.Serialize("expCond", _expCond, 1))
    PARAM_CHECK(ar.Serialize("expActiv", _expActiv, 1))
    PARAM_CHECK(ar.Serialize("expDesactiv", _expDesactiv, 1))
    PARAM_CHECK(ar.SerializeArray("synchronizations", _synchronizations, 1))

    ParamArchive arSubcls;
    if (!ar.OpenSubclass("Effects", arSubcls))
    {
        return LSStructure;
    }
    PARAM_CHECK(_effects.WorldSerialize(arSubcls))

    PARAM_CHECK(ar.Serialize("countdown", _countdown, 1))
    PARAM_CHECK(ar.Serialize("active", _active, 1))
    PARAM_CHECK(ar.Serialize("activeCountdown", _activeCountdown, 1))

    if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassFirst)
    {
        _e = sqrt(Square(_a) - Square(_b));
        _nextCheck = Glob.time - 1.0;
    }

    return LSOK;
}

NetworkMessageType Detector::GetNMType(NetworkMessageClass cls) const
{
    switch (cls)
    {
        case NMCCreate:
            return NMTCreateDetector;
        case NMCUpdateGeneric:
            return NMTUpdateDetector;
        default:
            return base::GetNMType(cls);
    }
}

#define CREATE_DETECTOR_MSG(XX)                                                                                        \
    XX(float, a, NDTFloat, NCTNone, DEFVALUE(float, 50), DOC_MSG("Trigger radius"), IdxTransfer)                       \
    XX(float, b, NDTFloat, NCTNone, DEFVALUE(float, 50), DOC_MSG("Trigger radius"), IdxTransfer)                       \
    XX(float, sinAngle, NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Rotation"), IdxTransfer)                       \
    XX(float, cosAngle, NDTFloat, NCTNone, DEFVALUE(float, 1), DOC_MSG("Rotation"), IdxTransfer)                       \
    XX(bool, rectangular, NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Rectangular / elliptic trigger"),          \
       IdxTransfer)                                                                                                    \
    XX(int, activationBy, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, ASANone), DOC_MSG("Who is activating trigger"),  \
       IdxTransfer)                                                                                                    \
    XX(int, activationType, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, ASATPresent),                                  \
       DOC_MSG("How trigger is activated"), IdxTransfer)                                                               \
    XX(bool, repeating, NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Can be activated repeatedly"), IdxTransfer)  \
    XX(float, timeoutMin, NDTFloat, NCTNone, DEFVALUE(float, 50), DOC_MSG("Trigger timeout"), IdxTransfer)             \
    XX(float, timeoutMid, NDTFloat, NCTNone, DEFVALUE(float, 50), DOC_MSG("Trigger timeout"), IdxTransfer)             \
    XX(float, timeoutMax, NDTFloat, NCTNone, DEFVALUE(float, 50), DOC_MSG("Trigger timeout"), IdxTransfer)             \
    XX(bool, interruptable, NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Timeout is interruptable"), IdxTransfer) \
    XX(int, action, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, ASTNone),                                              \
       DOC_MSG("Action performed when trigger is activated"), IdxTransfer)                                             \
    XX(int, assignedStatic, NDTInteger, NCTSmallSigned, DEFVALUE(int, -1), DOC_MSG("Attached static object"),          \
       IdxTransfer)                                                                                                    \
    XX(int, assignedVehicle, NDTInteger, NCTSmallSigned, DEFVALUE(int, -1), DOC_MSG("Attached vehicle"), IdxTransfer)  \
    XX(RString, text, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Trigger description"), IdxTransfer)          \
    XX(RString, expCond, NDTString, NCTNone, DEFVALUE(RString, "this"),                                                \
       DOC_MSG("Condition for activation of trigger"), IdxTransfer)                                                    \
    XX(RString, expActiv, NDTString, NCTNone, DEFVALUE(RString, ""),                                                   \
       DOC_MSG("Statement, processed when trigger is activated"), IdxTransfer)                                         \
    XX(RString, expDesactiv, NDTString, NCTNone, DEFVALUE(RString, ""),                                                \
       DOC_MSG("Statement, processed when trigger is deactivated"), IdxTransfer)                                       \
    XX(AutoArray<int>, synchronizations, NDTIntArray, NCTSmallUnsigned, DEFVALUEINTARRAY,                              \
       DOC_MSG("List of synchronizations"), IdxTransfer)                                                               \
    XX(ArcadeEffects, effects, NDTObject, NCTNone, DEFVALUE_MSG(NMTEffects), DOC_MSG("Camera and title effects"),      \
       IdxTransferObject)

DECLARE_NET_INDICES_EX(CreateDetector, CreateVehicle, CREATE_DETECTOR_MSG)
DEFINE_NET_INDICES_EX(CreateDetector, CreateVehicle, CREATE_DETECTOR_MSG)
DEFINE_GET_INDICES(CreateDetector)

#define UPDATE_DETECTOR_MSG(XX) \
    XX(OLink<AIGroup>, assignedGroup, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Attached group"), IdxTransferRef)

DECLARE_NET_INDICES_EX(UpdateDetector, UpdateVehicle, UPDATE_DETECTOR_MSG)
DEFINE_NET_INDICES_EX(UpdateDetector, UpdateVehicle, UPDATE_DETECTOR_MSG)
DEFINE_GET_INDICES(UpdateDetector)

NetworkMessageFormat& Detector::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    switch (cls)
    {
        case NMCCreate:
            base::CreateFormat(cls, format);
            CREATE_DETECTOR_MSG(MSG_FORMAT)
            break;
        case NMCUpdateGeneric:
            base::CreateFormat(cls, format);
            UPDATE_DETECTOR_MSG(MSG_FORMAT)
            break;
        default:
            base::CreateFormat(cls, format);
            break;
    }
    return format;
}

Detector* Detector::CreateObject(NetworkMessageContext& ctx)
{
    base* veh = base::CreateObject(ctx);
    Detector* det = dyn_cast<Detector>(veh);
    if (!det)
    {
        return nullptr;
    }
    sensorsMap.Add(det);
    det->TransferMsg(ctx);
    return det;
}

void Detector::DestroyObject()
{
    for (int i = 0; i < sensorsMap.Size(); i++)
    {
        if (sensorsMap[i] == this)
        {
            sensorsMap.Delete(i);
            break;
        }
    }
    base::DestroyObject();
}

TMError Detector::TransferMsg(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCCreate:
            if (ctx.IsSending())
            {
                TMCHECK(base::TransferMsg(ctx))
            }
            {
                PoseidonAssert(dynamic_cast<const IndicesCreateDetector*>(ctx.GetIndices()))
                    const IndicesCreateDetector* indices = static_cast<const IndicesCreateDetector*>(ctx.GetIndices());

                ITRANSF(a)
                ITRANSF(b)
                ITRANSF(sinAngle)
                ITRANSF(cosAngle)

                ITRANSF(rectangular)
                ITRANSF_ENUM(activationBy)
                ITRANSF_ENUM(activationType)
                ITRANSF(repeating)
                ITRANSF(timeoutMin)
                ITRANSF(timeoutMid)
                ITRANSF(timeoutMax)
                ITRANSF(interruptable)
                ITRANSF_ENUM(action)
                ITRANSF(assignedStatic)
                ITRANSF(assignedVehicle)
                ITRANSF(text)
                ITRANSF(expCond)
                ITRANSF(expActiv)
                ITRANSF(expDesactiv)
                ITRANSF(synchronizations)
                TMCHECK(ctx.IdxTransferObject(indices->effects, _effects))
                if (!ctx.IsSending())
                {
                    _e = sqrt(Square(_a) - Square(_b));
                    _cosAngle = sqrt(1 - Square(_sinAngle));
                    _nextCheck = Glob.time - 1.0;
                    for (int i = 0; i < _synchronizations.Size(); i++)
                    {
                        int sync = _synchronizations[i];
                        PoseidonAssert(sync >= 0);
                        if (sync >= synchronized.Size())
                        {
                            synchronized.Resize(sync + 1);
                        }
                        synchronized[sync].Add(this);
                    }
                }
            }
            break;
        case NMCUpdateGeneric:
        {
            TMCHECK(base::TransferMsg(ctx))

            PoseidonAssert(dynamic_cast<const IndicesUpdateDetector*>(ctx.GetIndices()))
                const IndicesUpdateDetector* indices = static_cast<const IndicesUpdateDetector*>(ctx.GetIndices());

            ITRANSF_REF(assignedGroup)
        }
        break;
        default:
            TMCHECK(base::TransferMsg(ctx))
            break;
    }
    return TMOK;
}

float Detector::CalculateError(NetworkMessageContext& ctx)
{
    float error = 0;
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
        {
            error += base::CalculateError(ctx);
        }
        break;
        default:
            error += base::CalculateError(ctx);
            break;
    }
    return error;
}

void Detector::SetArea(float a, float b, float angle, bool rectangular)
{
    _a = a;
    _b = b;
    float radAngle = HDegree(angle);
    if (_a < _b)
    {
        swap(_a, _b);
        radAngle += 0.5 * H_PI;
    }
    _e = sqrt(Square(_a) - Square(_b));
    _sinAngle = sin(radAngle);
    _cosAngle = cos(radAngle);
    _rectangular = rectangular;
}

void Detector::SetTriggerType(ArcadeSensorType type)
{
    _action = type;
}

void Detector::SetTimeout(float min, float mid, float max, bool interruptable)
{
    _timeoutMin = min;
    _timeoutMid = mid;
    _timeoutMax = max;
    _interruptable = interruptable;
}

void Detector::SetStatements(RString cond, RString activ, RString desactiv)
{
    _expCond = cond;
    _expActiv = activ;
    _expDesactiv = desactiv;
}

void Detector::AttachVehicle(EntityAI* vehicle)
{
    if (_activationBy == ASAGroup || _activationBy == ASALeader || _activationBy == ASAMember)
    {
        AIUnit* unit = vehicle ? vehicle->CommanderUnit() : nullptr;
        AIGroup* group = unit ? unit->GetGroup() : nullptr;
        _assignedGroup = group;
        _assignedStatic = -1;
        _assignedVehicle = -1;
        if (!group)
        {
            _activationBy = ASANone;
        }
    }
    else
    {
        int id = -1;
        if (vehicle)
        {
            for (int i = 0; i < vehiclesMap.Size(); i++)
            {
                if (vehiclesMap[i] == vehicle)
                {
                    id = i;
                    break;
                }
            }
        }
        _assignedGroup = nullptr;
        _assignedStatic = -1;
        _assignedVehicle = id;
        if (_activationBy == ASAVehicle)
        {
            if (id == -1)
            {
                _activationBy = ASANone;
            }
        }
        else
        {
            if (id >= 0)
            {
                _activationBy = ASAVehicle;
            }
        }
    }
}

void Detector::SetActivation(ArcadeSensorActivation by, ArcadeSensorActivationType type, bool repeating)
{
    if (by == ASAGroup || by == ASALeader || by == ASAMember)
    {
        if (_assignedGroup)
        {
            _activationBy = by;
        }
        else if (_assignedVehicle >= 0)
        {
            EntityAI* veh = vehiclesMap[_assignedVehicle];
            AIUnit* unit = veh ? veh->CommanderUnit() : nullptr;
            AIGroup* grp = unit ? unit->GetGroup() : nullptr;
            if (grp)
            {
                _assignedGroup = grp;
                _assignedStatic = -1;
                _assignedVehicle = -1;
                _activationBy = by;
            }
        }
    }
    else if (by == ASAVehicle)
    {
        if (_assignedVehicle >= 0)
        {
            _activationBy = by;
        }
        else if (_assignedGroup)
        {
            AIUnit* unit = _assignedGroup->Leader();
            EntityAI* veh = unit ? unit->GetVehicle() : nullptr;
            int id = -1;
            if (veh)
            {
                for (int i = 0; i < vehiclesMap.Size(); i++)
                {
                    if (vehiclesMap[i] == veh)
                    {
                        id = i;
                        break;
                    }
                }
            }
            if (id >= 0)
            {
                _assignedGroup = nullptr;
                _assignedStatic = -1;
                _assignedVehicle = id;
                _activationBy = by;
            }
        }
    }
    else
    {
        _assignedGroup = nullptr;
        _assignedStatic = 0;
        _assignedVehicle = 0;
        _activationBy = by;
    }
    _activationType = type;
    _repeating = repeating;
}

void Detector::FromTemplate(const ArcadeSensorInfo& info)
{
    SetArea(info.a, info.b, info.angle, info.rectangular);

    _activationBy = info.activationBy;
    _activationType = info.activationType;
    _repeating = info.repeating;
    _timeoutMin = info.timeoutMin;
    _timeoutMid = info.timeoutMid;
    _timeoutMax = info.timeoutMax;
    _interruptable = info.interruptable;
    _action = info.type;

    if (info.idStatic >= 0)
    {
        AssignStatic(info.idStatic);
    }
    if (info.idVehicle >= 0)
    {
        if (_activationBy == ASAVehicle)
        {
            AssignVehicle(info.idVehicle);
        }
        else
        {
            EntityAI* veh = dyn_cast<EntityAI>(vehiclesMap[info.idVehicle].GetLink());
            AIUnit* unit = veh ? veh->CommanderUnit() : nullptr;
            AIGroup* grp = unit ? unit->GetGroup() : nullptr;
            AssignGroup(grp);
        }
    }

    _text = info.text;
    _expCond = info.expCond;
    _expActiv = info.expActiv;
    _expDesactiv = info.expDesactiv;

    _synchronizations = info.synchronizations;
    _effects = info.effects;
}

void Detector::AssignGroup(AIGroup* group)
{
    _assignedGroup = group;
    _assignedStatic = -1;
    _assignedVehicle = -1;
    PoseidonAssert(_activationBy == ASAGroup || _activationBy == ASALeader || _activationBy == ASAMember);
}

void Detector::AssignStatic(int id)
{
    _assignedGroup = nullptr;
    _assignedStatic = id;
    _assignedVehicle = -1;
    _activationBy = ASAStatic;
}

void Detector::AssignVehicle(int id)
{
    _assignedGroup = nullptr;
    _assignedStatic = -1;
    _assignedVehicle = id;
    PoseidonAssert(_activationBy == ASAVehicle);
}

float Detector::GetCountdown()
{
    return _countdown - Glob.time;
}

void Detector::Simulate(float deltaT, SimulationImportance prec)
{
    if (_activeCountdown)
    {
        if (Glob.time >= _countdown)
        {
            OnActivate(GetActiveVehicle());
            _activeCountdown = false;
        }
    }

    Scan();

    if (_dynSound)
    {
        _dynSound->Simulate(this, deltaT, prec);
    }
    if (_voice)
    {
        if (!_voice->Simulate(deltaT, prec))
        {
            _voice = nullptr;
        }
    }
    if (_sound)
    {
        if (!_sound->Simulate(deltaT, prec))
        {
            _sound = nullptr;
        }
    }

    base::Simulate(deltaT, prec);
}

bool Detector::TestSide(EntityAI* vehicle)
{
    switch (_activationBy)
    {
        case ASAAnybody:
            return true;
        case ASAEast:
            return vehicle->GetTargetSide() == TEast;
        case ASAWest:
            return vehicle->GetTargetSide() == TWest;
        case ASAGuerrila:
            return vehicle->GetTargetSide() == TGuerrila;
        case ASACivilian:
            return vehicle->GetTargetSide() == TCivilian;
        case ASALogic:
            return vehicle->GetTargetSide() == TLogic;
        default:
            Fail("Activation");
            return false;
    }
}

bool Detector::TestSide(const AITargetInfo& target)
{
    TargetSide side = target._destroyed ? TCivilian : target._side;
    switch (_activationBy)
    {
        case ASAAnybody:
            return true;
        case ASAEast:
            return side == TEast;
        case ASAWest:
            return side == TWest;
        case ASAGuerrila:
            return side == TGuerrila;
        case ASACivilian:
            return side == TCivilian;
        case ASALogic:
            return side == TLogic;
        default:
            Fail("Activation");
            return false;
    }
}

AICenter* Detector::GetCenter() const
{
    switch (_activationType)
    {
        case 2: // detected by West
            return GWorld->GetWestCenter();
        case 3: // detected by East
            return GWorld->GetEastCenter();
        case 4: // detected by Guerrila
            return GWorld->GetGuerrilaCenter();
        case 5: // detected by Civilians
            return GWorld->GetCivilianCenter();
    }
    return nullptr;
}

bool Detector::IsInside(Vector3Par pos, Vector3Par f1, Vector3Par f2)
{
    if (_rectangular)
    {
        Vector3 b = Position();
        Vector3 e = f1 - b;
        Vector3 p = pos - b;
        float t = (e * p) / e.SquareSize();
        if (t < -1.0 || t > 1.0)
        {
            return false;
        }

        e = f2 - b;
        t = (e * p) / e.SquareSize();
        return t >= -1.0 && t <= 1.0;
    }
    else
    {
        float p = (pos - f1).SizeXZ() + (pos - f2).SizeXZ();
        return p <= 2.0 * _a;
    }
}

bool Detector::TestVehicle(Vehicle* veh, Vector3Par f1, Vector3Par f2)
{
    if (veh && !veh->IsDammageDestroyed())
    {
        Vector3 pos = veh->WorldTransform().Position();
        return IsInside(pos, f1, f2);
    }
    return false;
}

bool Detector::TestVehicle(AICenter* center, Vehicle* veh, Vector3Par f1, Vector3Par f2)
{
    for (int i = 0; i < center->NTargets(); i++)
    {
        const AITargetInfo& target = center->GetTarget(i);
        if (target._idExact == veh)
        {
            return target.FadingPositionAccuracy() >= 0.1 && IsInside(target._realPos, f1, f2);
        }
    }
    return false;
}

void Detector::Scan()
{
    Vector3 f1, f2;
    if (_rectangular)
    {
        f1 = Position() + Vector3(+_cosAngle * _a, 0, -_sinAngle * _a);
        f2 = Position() + Vector3(+_sinAngle * _b, 0, +_cosAngle * _b);
    }
    else
    {
        Vector3 diff(+_cosAngle * _e, 0, -_sinAngle * _e);
        f1 = Position() - diff;
        f2 = Position() + diff;
    }

    if (_vehicles->GetType() != GameArray)
    {
        GameArrayType array;
        _vehicles = new GameValue(array);
    }

    _vehicles->SetReadOnly(true);
    bool active = false;
    GameArrayType& vehicles = *_vehicles;

    vehicles.Resize(0);
    switch (_activationBy)
    {
        case ASANone:
            break;
        case ASAAlpha:
        case ASABravo:
        case ASACharlie:
        case ASADelta:
        case ASAEcho:
        case ASAFoxtrot:
        case ASAGolf:
        case ASAHotel:
        case ASAIndia:
        case ASAJuliet:
            return;
        case ASAEast:
        case ASAWest:
        case ASAGuerrila:
        case ASACivilian:
        case ASALogic:
        case ASAAnybody:
            if (_activationType <= 1)
            {
                for (int i = 0; i < GLOB_WORLD->NVehicles(); i++)
                {
                    Vehicle* vehicle = GLOB_WORLD->GetVehicle(i);
                    if (vehicle->IsDammageDestroyed())
                    {
                        continue;
                    }
                    EntityAI* veh = dyn_cast<EntityAI>(vehicle);
                    if (!veh)
                    {
                        continue;
                    }
                    if (veh->GetType()->IsKindOf(GLOB_WORLD->Preloaded(VTypeStatic)))
                    {
                        continue;
                    }
                    if (!TestSide(veh))
                    {
                        continue;
                    }
                    Vector3 pos = veh->WorldTransform().Position();
                    if (IsInside(pos, f1, f2))
                    {
                        vehicles.Add(GameValueExt(veh));
                    }
                }
                if (_activationType == 0)
                {
                    active = vehicles.Size() > 0;
                }
                else
                {
                    active = vehicles.Size() == 0;
                }
            }
            else
            {
                AICenter* center = GetCenter();
                if (!center)
                {
                    return;
                }
                for (int i = 0; i < center->NTargets(); i++)
                {
                    const AITargetInfo& target = center->GetTarget(i);
                    if (target._type->IsKindOf(GLOB_WORLD->Preloaded(VTypeStatic)))
                    {
                        continue;
                    }
                    if (!TestSide(target))
                    {
                        continue;
                    }
                    if (target.FadingPositionAccuracy() < 0.1)
                    {
                        continue;
                    }
                    if (IsInside(target._realPos, f1, f2))
                    {
                        if (target._time < Glob.time - 100)
                        {
                            continue;
                        }
                        vehicles.Add(GameValueExt(target._idExact.GetLink()));
                    }
                }
                active = vehicles.Size() > 0;
            }
            break;
        case ASAStatic:
            if (_assignedStatic < 0)
            {
                return;
            }
            {
                Object* obj = GLandscape->FindObject(_assignedStatic);
                if (_activationType <= 1)
                {
                    EntityAI* veh = dyn_cast<EntityAI>(obj);
                    if (TestVehicle(veh, f1, f2))
                    {
                        vehicles.Add(GameValueExt(veh));
                    }
                    if (_activationType == 0)
                    {
                        // present
                        active = vehicles.Size() > 0;
                    }
                    else
                    {
                        // not present
                        active = vehicles.Size() == 0;
                    }
                }
                else
                {
                    if (obj)
                    {
                        AICenter* center = GetCenter();
                        if (!center)
                        {
                            return;
                        }
                        EntityAI* veh = dyn_cast<EntityAI>(obj);
                        if (TestVehicle(center, veh, f1, f2))
                        {
                            vehicles.Add(GameValueExt(veh));
                        }
                    }
                    active = vehicles.Size() > 0;
                }
            }
            break;
        case ASAVehicle:
            if (_assignedVehicle < 0)
            {
                return;
            }
            if (_assignedVehicle >= vehiclesMap.Size())
            {
                return;
            }
            {
                Vehicle* veh = vehiclesMap[_assignedVehicle];
                if (_activationType <= 1)
                {
                    if (TestVehicle(veh, f1, f2))
                    {
                        vehicles.Add(GameValueExt(veh));
                    }
                    if (_activationType == 0)
                    {
                        // present
                        active = vehicles.Size() > 0;
                    }
                    else
                    {
                        // not present
                        active = vehicles.Size() == 0;
                    }
                }
                else
                {
                    if (veh)
                    {
                        AICenter* center = GetCenter();
                        if (!center)
                        {
                            return;
                        }
                        if (TestVehicle(center, veh, f1, f2))
                        {
                            vehicles.Add(GameValueExt(veh));
                        }
                    }
                    active = vehicles.Size() > 0;
                }
            }
            break;
        case ASAGroup:
            if (_activationType <= 1)
            {
                bool ok = true;
                if (_assignedGroup && _assignedGroup->NUnits() > 0)
                {
                    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
                    {
                        AIUnit* unit = _assignedGroup->UnitWithID(i + 1);
                        if (!unit)
                        {
                            continue;
                        }
                        EntityAI* veh = unit->GetVehicle();
                        if (TestVehicle(veh, f1, f2))
                        {
                            bool found = false;
                            for (int j = 0; j < NVehicles(); j++)
                            {
                                if (GetVehicle(j) == veh)
                                {
                                    found = true;
                                    break;
                                }
                            }
                            if (!found)
                            {
                                vehicles.Add(GameValueExt(veh));
                            }
                        }
                        else
                        {
                            ok = false;
                        }
                    }
                }
                else
                {
                    ok = false;
                }
                if (_activationType == 0)
                {
                    active = ok;
                }
                else
                {
                    active = !ok;
                }
            }
            else
            {
                active = true;
                if (_assignedGroup)
                {
                    AICenter* center = GetCenter();
                    if (!center)
                    {
                        return;
                    }
                    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
                    {
                        AIUnit* unit = _assignedGroup->UnitWithID(i + 1);
                        if (!unit)
                        {
                            continue;
                        }
                        EntityAI* veh = unit->GetVehicle();
                        if (TestVehicle(center, veh, f1, f2))
                        {
                            bool found = false;
                            for (int j = 0; j < NVehicles(); j++)
                            {
                                if (GetVehicle(j) == veh)
                                {
                                    found = true;
                                    break;
                                }
                            }
                            if (!found)
                            {
                                vehicles.Add(GameValueExt(veh));
                            }
                        }
                        else
                        {
                            active = false;
                        }
                    }
                }
                else
                {
                    active = false;
                }
            }
            break;
        case ASALeader:
        {
            AIUnit* leader = _assignedGroup ? _assignedGroup->Leader() : nullptr;
            EntityAI* vehLeader = leader ? leader->GetVehicle() : nullptr;
            if (_activationType <= 1)
            {
                if (TestVehicle(vehLeader, f1, f2))
                {
                    vehicles.Add(GameValueExt(vehLeader));
                }
                if (_activationType == 0)
                {
                    active = vehicles.Size() > 0;
                }
                else
                {
                    active = vehicles.Size() == 0;
                }
            }
            else
            {
                if (vehLeader)
                {
                    AICenter* center = GetCenter();
                    if (!center)
                    {
                        return;
                    }
                    if (TestVehicle(center, vehLeader, f1, f2))
                    {
                        vehicles.Add(GameValueExt(vehLeader));
                    }
                }
                active = vehicles.Size() > 0;
            }
        }
        break;
        case ASAMember:
            if (_activationType <= 1)
            {
                if (_assignedGroup)
                {
                    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
                    {
                        AIUnit* unit = _assignedGroup->UnitWithID(i + 1);
                        if (!unit)
                        {
                            continue;
                        }
                        EntityAI* veh = unit->GetVehicle();
                        if (TestVehicle(veh, f1, f2))
                        {
                            bool found = false;
                            for (int j = 0; j < NVehicles(); j++)
                            {
                                if (GetVehicle(j) == veh)
                                {
                                    found = true;
                                    break;
                                }
                            }
                            if (!found)
                            {
                                vehicles.Add(GameValueExt(veh));
                            }
                        }
                    }
                }
                if (_activationType == 0)
                {
                    active = vehicles.Size() > 0;
                }
                else
                {
                    active = vehicles.Size() == 0;
                }
            }
            else
            {
                if (_assignedGroup)
                {
                    AICenter* center = GetCenter();
                    if (!center)
                    {
                        return;
                    }
                    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
                    {
                        AIUnit* unit = _assignedGroup->UnitWithID(i + 1);
                        if (!unit)
                        {
                            continue;
                        }
                        EntityAI* veh = unit->GetVehicle();
                        if (TestVehicle(center, veh, f1, f2))
                        {
                            bool found = false;
                            for (int j = 0; j < NVehicles(); j++)
                            {
                                if (GetVehicle(j) == veh)
                                {
                                    found = true;
                                    break;
                                }
                            }
                            if (!found)
                            {
                                vehicles.Add(GameValueExt(veh));
                            }
                        }
                    }
                }
                active = vehicles.Size() > 0;
            }
            break;
    }

    if (strcmpi(_expCond, "this") != 0)
    {
        GameState* gstate = GWorld->GetGameState();
        gstate->VarSet("this", active, true);
        gstate->VarSet("thisList", GetGameValue(), true);
        active = gstate->EvaluateBool(_expCond);
    }

    if (active && !IsActive())
    {
        if (_timeoutMax < 0.1)
        {
            OnActivate(GetActiveVehicle());
        }
        else if (!_activeCountdown)
        {
            _activeCountdown = true;
            _countdown = Glob.time + GRandGen.Gauss(_timeoutMin, _timeoutMid, _timeoutMax);
        }
    }

    if (!active)
    {
        if (IsActive())
        {
            if (_repeating)
            {
                OnDesactivate();
            }
        }
        else if (_activeCountdown)
        {
            if (_interruptable)
            {
                _activeCountdown = false;
            }
        }
    }
}

void Detector::DoActivate()
{
    if (strcmpi(_expCond, "this") != 0)
    {
        GameState* gstate = GWorld->GetGameState();
        gstate->VarSet("this", true, true);
        if (!gstate->EvaluateBool(_expCond))
        {
            return;
        }
    }

    OnActivate(GetActiveVehicle());
}

Object* Detector::GetActiveVehicle()
{
    for (int i = 0; i < NVehicles(); i++)
    {
        EntityAI* veh = GetVehicle(i);
        if (veh)
        {
            return veh;
        }
    }
    return this;
}

extern SoundPars EnvSoundPars[];
extern SoundPars EnvSoundParsNight[];
const ParamEntry* FindMusic(RString name, SoundPars& pars);

void Detector::OnActivate(Object* obj)
{
    _active = true;

    GameState* gstate = GWorld->GetGameState();
    gstate->VarSet("thisList", GetGameValue(), true);
    gstate->Execute(_expActiv);

    if (_action == ASTSwitch)
    {
        for (int i = 0; i < _synchronizations.Size(); i++)
        {
            int sync = _synchronizations[i];
            SynchronizedItem& item = synchronized[sync];
            for (int j = 0; j < item.groups.Size(); j++)
            {
                SynchronizedGroup& sgrp = item.groups[j];
                AIGroup* grp = sgrp.group;
                if (!grp)
                {
                    continue;
                }
                for (int k = 0; k < grp->NWaypoints(); k++)
                {
                    const ArcadeWaypointInfo& wInfo = grp->GetWaypoint(k);
                    bool found = false;
                    for (int l = 0; l < wInfo.synchronizations.Size(); l++)
                    {
                        if (wInfo.synchronizations[l] == sync)
                        {
                            found = true;
                            break;
                        }
                    }
                    if (found)
                    {
                        if (grp->GetCurrent())
                        {
                            FSM* fsm = grp->GetCurrent()->_fsm;
                            PoseidonAssert(fsm);
                            int& index = fsm->Var(0);
                            index = k + 1;
                            AIGroupContext ctx(grp);
                            ctx._fsm = fsm;
                            ctx._task = const_cast<Mission*>(grp->GetMission());
                            fsm->SetState(1, &ctx);
                        }
                        break;
                    }
                }
            }
        }
    }
    else
    {
        for (int i = 0; i < _synchronizations.Size(); i++)
        {
            synchronized[_synchronizations[i]].SetActive(this, false);
        }
    }

    GameValue result = gstate->Evaluate(_effects.condition);
    AIUnit* player = GWorld->FocusOn();
    if (result.GetType() == GameObject)
    {
        if (!player)
        {
            return;
        }
        Object* obj = static_cast<GameDataObject*>(result.GetData())->GetObject();
        if (player->GetPerson() != obj && player->GetVehicle() != obj)
        {
            return;
        }
    }
    else if (result.GetType() == GameArray)
    {
        if (!player)
        {
            return;
        }
        bool found = false;
        const GameArrayType& array = (GameArrayType&)result;
        for (int i = 0; i < array.Size(); i++)
        {
            const GameValue& item = array[i];
            Object* obj = static_cast<GameDataObject*>(item.GetData())->GetObject();
            if (player->GetPerson() == obj || player->GetVehicle() == obj)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            return;
        }
    }
    else if (result.GetType() & GameBool)
    {
        if (!(bool)result)
        {
            return;
        }
    }
    else
    {
        return;
    }

    if (_effects.cameraEffect.GetLength() > 0)
    {
        if (stricmp(_effects.cameraEffect, "$TERMINATE$") == 0)
        {
            GLOB_WORLD->SetCameraEffect(nullptr);
        }
        else if (obj)
        {
            GLOB_WORLD->SetCameraEffect(CreateCameraEffect(obj, _effects.cameraEffect, _effects.cameraPosition));
        }
    }

    if (stricmp(_effects.sound, "$NONE$") != 0)
    {
        _sound = new SoundObject(_effects.sound, nullptr);
    }

    if (_effects.voice.GetLength() > 0)
    {
        _voice = new SoundObject(_effects.voice, obj);
    }

    if (_effects.soundEnv.GetLength() > 0)
    {
        FindEnvSound(_effects.soundEnv, EnvSoundPars[5], EnvSoundParsNight[5]);
    }

    if (_effects.soundDet.GetLength() > 0)
    {
        _dynSound = new DynSoundObject(_effects.soundDet);
    }

    if (stricmp(_effects.track, "$NONE$") == 0)
    {
    }
    else if (stricmp(_effects.track, "$STOP$") == 0)
    {
        GSoundScene->StopMusicTrack();
    }
    else
    {
        SoundPars sound;
        if (Poseidon::FindMusic(_effects.track, sound))
        {
            GSoundScene->StartMusicTrack(sound);
        }
    }

    switch (_effects.titleType)
    {
        case TitleNone:
            break;
        case TitleObject:
            GLOB_WORLD->SetTitleEffect(
                CreateTitleEffectObj(_effects.titleEffect, Pars >> "CfgTitles" >> _effects.title));
            break;
        case TitleResource:
        {
            const ParamEntry* cls = FindRscTitle(_effects.title);
            if (cls)
            {
                GWorld->SetTitleEffect(CreateTitleEffectRsc(_effects.titleEffect, *cls));
            }
        }
        break;
        case TitleText:
            GLOB_WORLD->SetTitleEffect(CreateTitleEffect(_effects.titleEffect, Localize(_effects.title)));
            break;
    }
}

void Detector::OnDesactivate()
{
    GameState* gstate = GWorld->GetGameState();
    gstate->Execute(_expDesactiv);

    _active = false;
    if (_action != ASTSwitch)
    {
        for (int i = 0; i < _synchronizations.Size(); i++)
        {
            synchronized[_synchronizations[i]].SetActive(this, true);
        }
    }
    if (_dynSound)
    {
        _dynSound = nullptr;
    }
}

DEFINE_CASTING(Flag)

#include <Poseidon/World/Simulation/Cloth/ClothObject.h>

namespace Poseidon
{
RString FindPicture(RString name);
}
namespace Poseidon
{
} // namespace Poseidon

Flag::Flag(EntityType* name, int id) : base(name->GetShape(), name, id)
{
    _fabric.Init(_shape, "latka", nullptr);

    const float prec = 0.1;
    _simulationSkipped = GRandGen.RandomValue() * prec;
    SetSimulationPrecision(prec);
}

void Flag::Init(Matrix4Par pos)
{
    Shape* shape = _shape->Level(0);
    int selIndex = _fabric.GetSelection(0);
    if (selIndex >= 0)
    {
        const NamedSelection& sel = shape->NamedSel(selIndex);

        shape->SaveOriginalPos();
        float minX = +1e10, minY = +1e10;
        float maxX = -1e10, maxY = -1e10;
        for (int i = 0; i < sel.Size(); i++)
        {
            int index = sel[i];
            Vector3Val val = shape->OrigPos(index);
            saturateMin(minX, val.X());
            saturateMin(minY, val.Y());
            saturateMax(maxX, val.X());
            saturateMax(maxY, val.Y());
        }

        float xSize = maxX - minX;
        float ySize = maxY - minY;

        _cloth = new ClothObject();
        const ParamEntry& entry = Pars >> "CfgCloth" >> "flag";
        _cloth->Init(entry, pos, minX, minY, xSize, ySize);
    }
}

Flag::~Flag()
{
    _cloth.Free();
}

void Flag::SetFlagTexture(Texture* texture)
{
    if (_texture != texture)
    {
        _texture = texture;
        if (texture)
        {
            _shape->RegisterTexture(texture, _fabric);
        }
    }
}

void Flag::Draw(int level, ClipFlags clipFlags, const FrameBase& pos)
{
    base::Draw(level, clipFlags, pos);
}

void Flag::FlagSimulate(Matrix4Par pos, float deltaT, SimulationImportance prec)
{
    base::Simulate(deltaT, prec);
    if (_cloth)
    {
        Vector3 wind = GLandscape->GetWind();
        Vector3 inertia = VZero;
        _cloth->Simulate(pos, Speed(), deltaT, wind, inertia, prec);
    }
}

void Flag::FlagAnimate(FrameBase& frame, int level)
{
    if (_cloth)
    {
        Shape* shape = _shape->Level(level);

        int selIndex = _fabric.GetSelection(0);
        if (selIndex >= 0)
        {
            const NamedSelection& sel = shape->NamedSel(selIndex);
            shape->SaveOriginalPos();

            float xSize = _cloth->GetSizeX();
            float ySize = _cloth->GetSizeY();
            float xMin = _cloth->GetMinX();
            float yMin = _cloth->GetMinY();
            float invXSize = 1 / xSize;
            float invYSize = 1 / ySize;

            Matrix4 worldToModel = frame.GetInvTransform();

            for (int i = 0; i < sel.Size(); i++)
            {
                int index = sel[i];
                Vector3Val val = shape->OrigPos(index);

                float xIndexF = (val.X() - xMin) * invXSize;
                float yIndexF = (val.Y() - yMin) * invYSize;

                Vector3 pos = _cloth->GetPosition(xIndexF, yIndexF);
                Vector3 norm = _cloth->GetNormal(xIndexF, yIndexF);

                Vector3 mpos = worldToModel.FastTransform(pos);
                Vector3 mnorm = worldToModel.Rotate(norm);

                shape->SetPos(index) = mpos;
                shape->SetNorm(index) = mnorm;
            }
            shape->InvalidateNormals();
        }
    }

    if (_texture)
    {
        _fabric.SetTexture(_shape, level, _texture);
    }

    base::Animate(level);
}

void Flag::FlagDeanimate(FrameBase& frame, int level)
{
    base::Deanimate(level);
}

DEFINE_CASTING(FlagCarrier)

FlagCarrier::FlagCarrier(VehicleType* type) : base(type)
{
    _shape->AllowAnimation();

    _phase = 0;
    _animSpeed = 0.5;
    _invAnimSpeed = 1.0 / _animSpeed;
    _skeleton = new Skeleton();
    AnimationRTName name;
    name.name = GetAnimationName("stozar.rtm");
    name.skeleton = _skeleton;
    _animation = new AnimationRT(name, false);
    _animation->Prepare(_shape, _skeleton, _weights, false);

    _flagSide = TSideUnknown;
}

void FlagCarrier::AnimateMatrix(Matrix4& mat, int level, int selection) const
{
    const AnimationRTWeights& wgt = _weights[level];

    Shape* sShape = _shape->Level(level);
    const NamedSelection& sel = sShape->NamedSel(selection);
    int point = sel[0];
    PoseidonAssert(sel.Size() > 0);

    _animation->Matrix(mat, _phase, wgt[point]);
}

Texture* FlagCarrier::GetFlagTexture()
{
    return _flagOwner ? nullptr : _flagTexture;
}

Texture* FlagCarrier::GetFlagTextureInternal()
{
    return _flagTexture;
}

void FlagCarrier::SetFlagTexture(RString name)
{
    _flagTexture = nullptr;
    if (name.GetLength() > 0)
    {
        RString fullName = Poseidon::FindPicture(name);
        if (fullName.GetLength() > 0)
        {
            fullName.Lower();
            _flagTexture = GlobLoadTexture(fullName);
        }
    }
    else
    {
        SetFlagOwner(nullptr);
    }
}

bool CheckSupply(EntityAI* vehicle, EntityAI* parent, SupportCheckF check, float limit, bool now);

void FlagCarrier::GetActions(UIActions& actions, AIUnit* unit, bool now)
{
    if (_flagTexture && !_flagOwner && unit && unit->IsFreeSoldier())
    {
        AIGroup* grp = unit->GetGroup();
        AICenter* center = grp ? grp->GetCenter() : nullptr;
        if (center && center->IsEnemy(_flagSide))
        {
            if (CheckSupply(unit->GetPerson(), this, nullptr, 0, now))
            {
                actions.Add(ATTakeFlag, this, 0.99, 0, true, true);
            }
        }
    }
    base::GetActions(actions, unit, now);
}

void FlagCarrier::SetFlagOwner(Person* veh)
{
    if (_flagOwner)
    {
        if (_flagOwner->IsLocal())
        {
            _flagOwner->SetFlagCarrier(nullptr);
        }
        else
        {
            GetNetworkManager().SetFlagCarrier(_flagOwner, nullptr);
        }

#if LOG_FLAG_CHANGES
        RptF("Flags: Local flag %s owner changes: from %s (wanted %s) to %s", (const char*)GetDebugName(),
             _flagOwner ? (const char*)_flagOwner->GetDebugName() : "nullptr",
             _flagOwnerWanted ? (const char*)_flagOwnerWanted->GetDebugName() : "nullptr",
             veh ? (const char*)veh->GetDebugName() : "nullptr");
#endif

        _flagOwnerWanted = _flagOwner = veh;
        _phase = 0;

        if (veh)
        {
            if (veh->IsLocal())
            {
                veh->SetFlagCarrier(this);
            }
            else
            {
                GetNetworkManager().SetFlagCarrier(veh, this);
            }
        }
    }
    else
    {
        if (veh)
        {
#if LOG_FLAG_CHANGES
            RptF("Flags: Local flag %s: wanted owner changes from %s to %s (real owner is nullptr)",
                 (const char*)GetDebugName(),
                 _flagOwnerWanted ? (const char*)_flagOwnerWanted->GetDebugName() : "nullptr",
                 veh ? (const char*)veh->GetDebugName() : "nullptr");
            if (veh)
            {
                RptF("  Detail info about %s:", (const char*)veh->GetDebugName());
                RptF("    Local: %s", veh->IsLocal() ? "YES" : "NO");
                RptF("    Position [%.0f, %.0f]", veh->Position().X(), veh->Position().Z());
                RptF("    Flag position [%.0f, %.0f]", Position().X(), Position().Z());
                RptF("    Distance from flag %.1f", Position().Distance(veh->Position()));
                RptF("  Player: %s", GWorld->PlayerOn() ? (const char*)GWorld->PlayerOn()->GetDebugName() : "nullptr");
            }
#endif
            _flagOwnerWanted = veh;
            _phase = 0;
            _animStart = Glob.time;
        }
    }
}

TargetSide FlagCarrier::GetFlagSide() const
{
    return _flagSide;
}

void FlagCarrier::SetFlagSide(TargetSide side)
{
    _flagSide = side;
}

void FlagCarrier::Simulate(float deltaT, SimulationImportance prec)
{
    base::Simulate(deltaT, prec);

    if (!_flagOwner && _flagOwnerWanted)
    {
        _phase = _animSpeed * (Glob.time - _animStart);
        if (_phase >= 1.0)
        {
            _phase = 1.0;
#if LOG_FLAG_CHANGES
            RptF("Flags: Local flag %s: real owner changes from %s to wanted owner %s", (const char*)GetDebugName(),
                 _flagOwner ? (const char*)_flagOwner->GetDebugName() : "nullptr",
                 _flagOwnerWanted ? (const char*)_flagOwnerWanted->GetDebugName() : "nullptr");
#endif
            _flagOwner = _flagOwnerWanted;
            if (_flagOwner->IsLocal())
            {
                _flagOwner->SetFlagCarrier(this);
            }
            else
            {
                GetNetworkManager().SetFlagCarrier(_flagOwner, this);
            }
        }
    }
}

LSError FlagCarrier::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar))

    PARAM_CHECK(ar.SerializeRef("flagOwner", _flagOwner, 1))
    if (ar.IsSaving())
    {
        RString name = _flagTexture ? _flagTexture->Name() : "";
        PARAM_CHECK(ar.Serialize("flagTexture", name, 1))
    }
    else if (ar.GetPass() == ParamArchive::PassFirst)
    {
        RString name;
        PARAM_CHECK(ar.Serialize("flagTexture", name, 1))
        _flagTexture = name.GetLength() > 0 ? GlobLoadTexture(name) : nullptr;
    }
    PARAM_CHECK(ar.SerializeEnum("flagSide", _flagSide, 1, TSideUnknown))

    return LSOK;
}

NetworkMessageType FlagCarrier::GetNMType(NetworkMessageClass cls) const
{
    switch (cls)
    {
        case NMCUpdateGeneric:
            return NMTUpdateFlag;
        default:
            return base::GetNMType(cls);
    }
}

namespace Poseidon
{

#define UPDATE_FLAG_MSG(XX)                                                                                          \
    XX(OLink<Person>, flagOwner, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Flag owner"), IdxTransferRef, ET_NOT_EQUAL, \
       ERR_COEF_MODE)                                                                                                \
    XX(RString, flagTexture, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Flag texture"), IdxTransfer,        \
       ET_NOT_EQUAL, ERR_COEF_MODE)                                                                                  \
    XX(int, flagSide, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, TSideUnknown), DOC_MSG("Flag side"), IdxTransfer,  \
       ET_NOT_EQUAL, ERR_COEF_MODE)

DECLARE_NET_INDICES_EX_ERR(UpdateFlag, UpdateVehicleSupply, UPDATE_FLAG_MSG)
DEFINE_NET_INDICES_EX_ERR(UpdateFlag, UpdateVehicleSupply, UPDATE_FLAG_MSG)

} // namespace Poseidon

DEFINE_GET_INDICES(UpdateFlag)

namespace Poseidon
{

NetworkMessageFormat& FlagCarrier::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    switch (cls)
    {
        case NMCUpdateGeneric:
            base::CreateFormat(cls, format);
            UPDATE_FLAG_MSG(MSG_FORMAT_ERR)

            break;
        default:
            base::CreateFormat(cls, format);
            break;
    }
    return format;
}

TMError FlagCarrier::TransferMsg(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
            TMCHECK(base::TransferMsg(ctx))
            {
                PoseidonAssert(dynamic_cast<const IndicesUpdateFlag*>(ctx.GetIndices()))
                    const IndicesUpdateFlag* indices = static_cast<const IndicesUpdateFlag*>(ctx.GetIndices());

#if LOG_FLAG_CHANGES
                if (!ctx.IsSending())
                {
                    Person* owner = _flagOwner;
                    ITRANSF_REF(flagOwner);
                    if (_flagOwner != owner)
                        RptF("Flags: Remote flag %s owner changes: from %s to %s (wanted %s unchanged)",
                             (const char*)GetDebugName(), owner ? (const char*)owner->GetDebugName() : "nullptr",
                             _flagOwner ? (const char*)_flagOwner->GetDebugName() : "nullptr",
                             _flagOwnerWanted ? (const char*)_flagOwnerWanted->GetDebugName() : "nullptr");
                }
                else
                    ITRANSF_REF(flagOwner);
#else
                ITRANSF_REF(flagOwner)
#endif
                ITRANSF_ENUM(flagSide)
                if (ctx.IsSending())
                {
                    RString name = _flagTexture ? _flagTexture->Name() : "";
                    TMCHECK(ctx.IdxTransfer(indices->flagTexture, name))
                }
                else
                {
                    RString name;
                    TMCHECK(ctx.IdxTransfer(indices->flagTexture, name))
                    if (name.GetLength() > 0)
                    {
                        _flagTexture = GlobLoadTexture(name);
                    }
                    else
                    {
                        _flagTexture = nullptr;
                    }
                }
            }
            break;
        default:
            return base::TransferMsg(ctx);
    }
    return TMOK;
}

float FlagCarrier::CalculateError(NetworkMessageContext& ctx)
{
    float error = 0;
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
        {
            error += base::CalculateError(ctx);

            PoseidonAssert(dynamic_cast<const IndicesUpdateFlag*>(ctx.GetIndices())) const IndicesUpdateFlag* indices =
                static_cast<const IndicesUpdateFlag*>(ctx.GetIndices());

            ICALCERR_NEQREF(Person, flagOwner, ERR_COEF_MODE)
            RString name = _flagTexture ? _flagTexture->Name() : "";
            ICALCERRE_NEQSTR(flagTexture, name, ERR_COEF_MODE)
            ICALCERR_NEQ(int, flagSide, ERR_COEF_MODE)
        }
        break;
        default:
            error += base::CalculateError(ctx);
            break;
    }
    return error;
}
} // namespace Poseidon
