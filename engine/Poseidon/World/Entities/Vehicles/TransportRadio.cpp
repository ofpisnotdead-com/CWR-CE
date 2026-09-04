#include <Poseidon/World/Entities/Vehicles/Transport.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/Entities/Weapons/ProxyWeapon.hpp>
#include <Poseidon/World/Entities/Infantry/Person.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/Game/UiActions.hpp>
#include <Poseidon/UI/Locale/Sentences.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/Foundation/Enums/EnumNames.hpp>
#include <limits.h>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

using namespace Poseidon;
CameraType ValidateCamera(CameraType cam);

namespace Poseidon
{
using Foundation::EnumName;

RadioMessageVFireFailed::RadioMessageVFireFailed(Transport* vehicle)
{
    _vehicle = vehicle;
}

LSError RadioMessageVFireFailed::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(RadioMessage::Serialize(ar))
    PARAM_CHECK(ar.SerializeRef("Vehicle", _vehicle, 1))
    return LSOK;
}

void RadioMessageVFireFailed::Transmitted() {}

AIUnit* RadioMessageVFireFailed::GetSender() const
{
    return _vehicle ? _vehicle->GunnerUnit() : nullptr;
}

NetworkMessageType RadioMessageVFireFailed::GetNMType(NetworkMessageClass cls) const
{
    return NMTMsgVFireFailed;
}

IndicesVMessage::IndicesVMessage()
{
    vehicle = -1;
}

void IndicesVMessage::Scan(NetworkMessageFormatBase* format)
{
    SCAN(vehicle)
}

class IndicesMsgVFireFailed : public IndicesVMessage
{
    typedef IndicesVMessage base;

  public:
    IndicesMsgVFireFailed();
    NetworkMessageIndices* Clone() const override { return new IndicesMsgVFireFailed; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesMsgVFireFailed::IndicesMsgVFireFailed()
{
    vehicle = -1;
}

void IndicesMsgVFireFailed::Scan(NetworkMessageFormatBase* format)
{
    base::Scan(format);
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesMsgVFireFailed()
{
    using namespace Poseidon;
    return new IndicesMsgVFireFailed();
}
namespace Poseidon
{

NetworkMessageFormat& RadioMessageVFireFailed::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("vehicle", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("In vehicle"));
    return format;
}

TMError RadioMessageVFireFailed::TransferMsg(NetworkMessageContext& ctx)
{
    PoseidonAssert(dynamic_cast<const IndicesMsgVFireFailed*>(ctx.GetIndices())) const IndicesMsgVFireFailed* indices =
        static_cast<const IndicesMsgVFireFailed*>(ctx.GetIndices());

    ITRANSF_REF(vehicle)
    return TMOK;
}

void RadioMessageVFireFailed::Send()
{
    if (_vehicle)
    {
        _vehicle->SendFireFailed();
    }
}

const char* RadioMessageVFireFailed::PrepareSentence(SentenceParams& params)
{
    if (!_vehicle)
    {
        return nullptr;
    }
    AIUnit* receiver = _vehicle->CommanderUnit();
    if (!receiver)
    {
        return nullptr;
    }

    const char* sentence = "VehicleFireFailed";
    return sentence;
}

RadioMessageWithTarget::RadioMessageWithTarget(Transport* vehicle, Target* tgt)
{
    _vehicle = vehicle;
    _target = tgt;
}

AIUnit* RadioMessageWithTarget::GetSender() const
{
    return _vehicle ? _vehicle->CommanderUnit() : nullptr;
}

class IndicesMessageWithTarget : public IndicesVMessage
{
    typedef IndicesVMessage base;

  public:
    int target;

    IndicesMessageWithTarget();
    NetworkMessageIndices* Clone() const override { return new IndicesMessageWithTarget; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesMessageWithTarget::IndicesMessageWithTarget()
{
    target = -1;
}

void IndicesMessageWithTarget::Scan(NetworkMessageFormatBase* format)
{
    base::Scan(format);
    SCAN(target)
}

NetworkMessageFormat& RadioMessageWithTarget::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("vehicle", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("In vehicle"));
    format.Add("target", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Target"));
    return format;
}

TMError RadioMessageWithTarget::TransferMsg(NetworkMessageContext& ctx)
{
    PoseidonAssert(dynamic_cast<const IndicesMessageWithTarget*>(ctx.GetIndices()))
        const IndicesMessageWithTarget* indices = static_cast<const IndicesMessageWithTarget*>(ctx.GetIndices());

    ITRANSF_REF(vehicle)

    if (ctx.IsSending())
    {
        EntityAI* target = nullptr;
        if (_target)
        {
            target = _target->idExact;
        }
        TMCHECK(ctx.IdxTransferRef(indices->target, target))
    }
    else
    {
        _target = nullptr;
        EntityAI* target = nullptr;
        TMCHECK(ctx.IdxTransferRef(indices->target, target))
        if (target)
        {
            AIUnit* unit = _vehicle->CommanderUnit();
            AIGroup* grp = unit ? unit->GetGroup() : nullptr;
            if (grp)
            {
                _target = grp->FindTarget(target);
            }
        }
    }

    return TMOK;
}

static bool CheckUnitAlive(AIUnit* unit)
{
    if (!unit)
    {
        return false;
    }
    if (unit->GetLifeState() == AIUnit::LSAlive)
    {
        return true;
    }
    AIGroup* grp = unit->GetGroup();
    // or do hack that in-vehicle units are always reported by the vehicle commander
    if (grp)
    {
        grp->SetReportBeforeTime(unit, Glob.time + 10);
    }
    return false;
}

RadioMessageVTarget::RadioMessageVTarget(Transport* vehicle, Target* tgt) : base(vehicle, tgt) {}

void RadioMessageVTarget::Transmitted()
{
    if (!_vehicle)
    {
        return;
    }
    AIUnit* receiver = _vehicle->GunnerUnit();
    if (!CheckUnitAlive(receiver))
    {
        return;
    }

    _vehicle->ReceivedTarget(_target);
}

LSError RadioMessageWithTarget::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(RadioMessage::Serialize(ar))
    PARAM_CHECK(ar.SerializeRef("Vehicle", _vehicle, 1))
    PARAM_CHECK(ar.SerializeRef("Target", _target, 1))
    return LSOK;
}

NetworkMessageType RadioMessageVTarget::GetNMType(NetworkMessageClass cls) const
{
    return NMTMsgVTarget;
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesMsgVTarget()
{
    using namespace Poseidon;
    return new IndicesMessageWithTarget();
}
namespace Poseidon
{

void RadioMessageVTarget::Send()
{
    if (_vehicle)
    {
        _vehicle->SendTarget(_target);
    }
}

const char* RadioMessageVTarget::PrepareSentence(SentenceParams& params)
{
    if (!_vehicle)
    {
        return nullptr;
    }
    AIUnit* receiver = _vehicle->GunnerUnit();
    if (!receiver)
    {
        return nullptr;
    }

    if (!_target)
    {
        return "VehicleNoTarget";
    }

    const VehicleType* type = _target->type;
    params.AddWord(type->GetNameSound(), type->GetDisplayName());
    params.AddRelativePosition(_target->AimingPosition() - _vehicle->Position());
    return "VehicleTarget";
}

RadioMessageVFire::RadioMessageVFire(Transport* vehicle, Target* tgt) : base(vehicle, tgt) {}

void RadioMessageVFire::Transmitted()
{
    if (!_vehicle)
    {
        return;
    }
    AIUnit* receiver = _vehicle->GunnerUnit();
    if (!CheckUnitAlive(receiver))
    {
        return;
    }

    _vehicle->ReceivedFire(_target);
}

NetworkMessageType RadioMessageVFire::GetNMType(NetworkMessageClass cls) const
{
    return NMTMsgVFire;
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesMsgVFire()
{
    using namespace Poseidon;
    return new IndicesMessageWithTarget();
}
namespace Poseidon
{

void RadioMessageVFire::Send()
{
    if (_vehicle)
    {
        _vehicle->SendFire(_target);
    }
}

const char* RadioMessageVFire::PrepareSentence(SentenceParams& params)
{
    if (!_vehicle)
    {
        return nullptr;
    }
    AIUnit* receiver = _vehicle->GunnerUnit();
    if (!receiver)
    {
        return nullptr;
    }

    const char* sentence = "VehicleFire";
    if (_target)
    {
        // TargetSide side = _target->side;
        const VehicleType* type = _target->type;
        params.AddWord(type->GetNameSound(), type->GetDisplayName());
    }
    else
    {
        params.AddWord("unknown", LocalizeString(IDS_WORD_UNKNOWN));
    }
    return sentence;
}

RadioMessageVMove::RadioMessageVMove(Transport* vehicle, Vector3Par pos)
{
    _vehicle = vehicle;
    _destination = pos;
}

AIUnit* RadioMessageVMove::GetSender() const
{
    return _vehicle ? _vehicle->CommanderUnit() : nullptr;
}

void RadioMessageVMove::Transmitted()
{
    if (!_vehicle)
    {
        return;
    }
    AIUnit* receiver = _vehicle->PilotUnit();
    if (!CheckUnitAlive(receiver))
    {
        return;
    }

    _vehicle->ReceivedMove(_destination);
}

LSError RadioMessageVMove::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(RadioMessage::Serialize(ar))
    PARAM_CHECK(ar.SerializeRef("Vehicle", _vehicle, 1))
    PARAM_CHECK(ar.Serialize("destination", _destination, 1))
    return LSOK;
}

NetworkMessageType RadioMessageVMove::GetNMType(NetworkMessageClass cls) const
{
    return NMTMsgVMove;
}

class IndicesMsgVMove : public IndicesVMessage
{
    typedef IndicesVMessage base;

  public:
    int destination;

    IndicesMsgVMove();
    NetworkMessageIndices* Clone() const override { return new IndicesMsgVMove; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesMsgVMove::IndicesMsgVMove()
{
    destination = -1;
}

void IndicesMsgVMove::Scan(NetworkMessageFormatBase* format)
{
    base::Scan(format);
    SCAN(destination)
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesMsgVMove()
{
    using namespace Poseidon;
    return new IndicesMsgVMove();
}
namespace Poseidon
{

NetworkMessageFormat& RadioMessageVMove::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("vehicle", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("In vehicle"));
    format.Add("destination", NDTVector, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Destination position"));
    return format;
}

TMError RadioMessageVMove::TransferMsg(NetworkMessageContext& ctx)
{
    PoseidonAssert(dynamic_cast<const IndicesMsgVMove*>(ctx.GetIndices())) const IndicesMsgVMove* indices =
        static_cast<const IndicesMsgVMove*>(ctx.GetIndices());

    ITRANSF_REF(vehicle)
    ITRANSF(destination)
    return TMOK;
}

void RadioMessageVMove::Send()
{
    if (_vehicle)
    {
        _vehicle->SendMove(_destination);
    }
}

const char* RadioMessageVMove::PrepareSentence(SentenceParams& params)
{
    if (!_vehicle)
    {
        return nullptr;
    }
    AIUnit* receiver = _vehicle->PilotUnit();
    if (!receiver)
    {
        return nullptr;
    }

    const char* sentence = "VehicleMove";

    Vector3 dir = _destination - _vehicle->Position();
    int azimut = toInt(0.1 * atan2(dir.X(), dir.Z()) * (180 / H_PI));
    if (azimut < 0)
    {
        azimut += 36;
    }
    int dist = toInt(0.1 * dir.SizeXZ());
    params.Add("%02d", azimut);
    params.Add(dist);
    return sentence;
}

RadioMessageVFormation::RadioMessageVFormation(Transport* vehicle)
{
    _vehicle = vehicle;
}

AIUnit* RadioMessageVFormation::GetSender() const
{
    return _vehicle ? _vehicle->CommanderUnit() : nullptr;
}

void RadioMessageVFormation::Transmitted()
{
    if (!_vehicle)
    {
        return;
    }
    AIUnit* receiver = _vehicle->PilotUnit();
    if (!CheckUnitAlive(receiver))
    {
        return;
    }

    _vehicle->ReceivedJoin();
}

LSError RadioMessageVFormation::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(RadioMessage::Serialize(ar))
    PARAM_CHECK(ar.SerializeRef("Vehicle", _vehicle, 1))
    return LSOK;
}

NetworkMessageType RadioMessageVFormation::GetNMType(NetworkMessageClass cls) const
{
    return NMTMsgVFormation;
}

class IndicesMsgVFormation : public IndicesVMessage
{
    typedef IndicesVMessage base;

  public:
    IndicesMsgVFormation();
    NetworkMessageIndices* Clone() const override { return new IndicesMsgVFormation; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesMsgVFormation::IndicesMsgVFormation() = default;

void IndicesMsgVFormation::Scan(NetworkMessageFormatBase* format)
{
    base::Scan(format);
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesMsgVFormation()
{
    using namespace Poseidon;
    return new IndicesMsgVFormation();
}
namespace Poseidon
{

NetworkMessageFormat& RadioMessageVFormation::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("vehicle", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("In vehicle"));
    return format;
}

TMError RadioMessageVFormation::TransferMsg(NetworkMessageContext& ctx)
{
    PoseidonAssert(dynamic_cast<const IndicesMsgVFormation*>(ctx.GetIndices())) const IndicesMsgVFormation* indices =
        static_cast<const IndicesMsgVFormation*>(ctx.GetIndices());

    ITRANSF_REF(vehicle)
    return TMOK;
}

void RadioMessageVFormation::Send()
{
    if (_vehicle)
    {
        _vehicle->SendJoin();
    }
}

const char* RadioMessageVFormation::PrepareSentence(SentenceParams& params)
{
    if (!_vehicle)
    {
        return nullptr;
    }
    AIUnit* receiver = _vehicle->PilotUnit();
    if (!receiver)
    {
        return nullptr;
    }

    const char* sentence = "VehicleJoin";
    return sentence;
}

RadioMessageVCeaseFire::RadioMessageVCeaseFire(Transport* vehicle)
{
    _vehicle = vehicle;
}

AIUnit* RadioMessageVCeaseFire::GetSender() const
{
    return _vehicle ? _vehicle->CommanderUnit() : nullptr;
}

void RadioMessageVCeaseFire::Transmitted()
{
    if (!_vehicle)
    {
        return;
    }
    AIUnit* receiver = _vehicle->GunnerUnit();
    if (!CheckUnitAlive(receiver))
    {
        return;
    }

    _vehicle->ReceivedCeaseFire();
}

LSError RadioMessageVCeaseFire::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(RadioMessage::Serialize(ar))
    PARAM_CHECK(ar.SerializeRef("Vehicle", _vehicle, 1))
    return LSOK;
}

const char* RadioMessageVCeaseFire::PrepareSentence(SentenceParams& params)
{
    if (!_vehicle)
    {
        return nullptr;
    }
    AIUnit* receiver = _vehicle->GunnerUnit();
    if (!receiver)
    {
        return nullptr;
    }

    const char* sentence = "VehicleCeaseFire";
    return sentence;
}

RadioMessageVSimpleCommand::RadioMessageVSimpleCommand(Transport* vehicle, SimpleCommand cmd)
{
    _vehicle = vehicle;
    _cmd = cmd;
}

AIUnit* RadioMessageVSimpleCommand::GetSender() const
{
    return _vehicle ? _vehicle->CommanderUnit() : nullptr;
}

void RadioMessageVSimpleCommand::Transmitted()
{
    if (!_vehicle)
    {
        return;
    }
    AIUnit* receiver = nullptr;
    if (_cmd == SCFire || _cmd == SCCeaseFire || _cmd == SCKeyFire)
    {
        receiver = _vehicle->GunnerUnit();
    }
    else
    {
        receiver = _vehicle->PilotUnit();
    }
    if (!CheckUnitAlive(receiver))
    {
        return;
    }

    _vehicle->ReceivedSimpleCommand(_cmd);
}

template <>
const EnumName* Foundation::GetEnumNames(SimpleCommand dummy)
{
    static const EnumName SimpleCommandNames[] = {
        EnumName(SCForward, "FORWARD"),  EnumName(SCStop, "STOP"),        EnumName(SCBackward, "BACK"),
        EnumName(SCFaster, "FAST"),      EnumName(SCSlower, "SLOW"),      EnumName(SCLeft, "LEFT"),
        EnumName(SCRight, "RIGHT"),      EnumName(SCFire, "FIRE"),        EnumName(SCCeaseFire, "CEASE FIRE"),
        EnumName(SCKeyUp, "KEY UP"),     EnumName(SCKeyDown, "KEY DOWN"), EnumName(SCKeySlow, "KEY SLOW"),
        EnumName(SCKeyFast, "KEY FAST"), EnumName(SCKeyFire, "KEY FIRE"), EnumName()};
    return SimpleCommandNames;
}

LSError RadioMessageVSimpleCommand::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(RadioMessage::Serialize(ar))
    PARAM_CHECK(ar.SerializeRef("Vehicle", _vehicle, 1))
    PARAM_CHECK(ar.SerializeEnum("cmd", _cmd, 1))
    return LSOK;
}

NetworkMessageType RadioMessageVSimpleCommand::GetNMType(NetworkMessageClass cls) const
{
    return NMTMsgVSimpleCommand;
}

class IndicesMsgVSimpleCommand : public IndicesVMessage
{
    typedef IndicesVMessage base;

  public:
    int cmd;

    IndicesMsgVSimpleCommand();
    NetworkMessageIndices* Clone() const override { return new IndicesMsgVSimpleCommand; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesMsgVSimpleCommand::IndicesMsgVSimpleCommand()
{
    cmd = -1;
}

void IndicesMsgVSimpleCommand::Scan(NetworkMessageFormatBase* format)
{
    base::Scan(format);
    SCAN(cmd)
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesMsgVSimpleCommand()
{
    using namespace Poseidon;
    return new IndicesMsgVSimpleCommand();
}
namespace Poseidon
{

NetworkMessageFormat& RadioMessageVSimpleCommand::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("vehicle", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("In vehicle"));
    format.Add("cmd", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Command"));
    return format;
}

TMError RadioMessageVSimpleCommand::TransferMsg(NetworkMessageContext& ctx)
{
    PoseidonAssert(dynamic_cast<const IndicesMsgVSimpleCommand*>(ctx.GetIndices()))
        const IndicesMsgVSimpleCommand* indices = static_cast<const IndicesMsgVSimpleCommand*>(ctx.GetIndices());

    ITRANSF_REF(vehicle)
    ITRANSF_ENUM(cmd)
    return TMOK;
}

void RadioMessageVSimpleCommand::Send()
{
    if (_vehicle)
    {
        _vehicle->SendSimpleCommand(_cmd);
    }
}

const char* RadioMessageVSimpleCommand::PrepareSentence(SentenceParams& params)
{
    if (!_vehicle)
    {
        return nullptr;
    }
    AIUnit* receiver = nullptr;
    if (_cmd == SCFire || _cmd == SCCeaseFire || _cmd == SCKeyFire)
    {
        receiver = _vehicle->GunnerUnit();
    }
    else
    {
        receiver = _vehicle->PilotUnit();
    }
    if (!receiver)
    {
        return nullptr;
    }

    VehicleMoveMode mode = _vehicle->GetMoveMode();
    if (mode == VMMFormation || mode == VMMMove)
    {
        float speed = _vehicle->ModelSpeed().Z();
        if (speed < -0.1)
        {
            mode = VMMBackward;
        }
        else if (speed > 0.1)
        {
            mode = VMMForward;
        }
        else
        {
            mode = VMMStop;
        }
    }

    switch (_cmd)
    {
        case SCKeyUp:
        {
            if (mode == VMMBackward)
            {
                _cmd = SCStop;
            }
            else
            {
                _cmd = SCForward;
            }
        }
        break;
        case SCKeyDown:
        {
            if (mode == VMMSlowForward || mode == VMMForward || mode == VMMFastForward)
            {
                _cmd = SCStop;
            }
            else
            {
                _cmd = SCBackward;
            }
        }
        break;
        case SCKeySlow:
        {
            if (mode == VMMBackward)
            {
                _cmd = SCStop;
            }
            else
            {
                _cmd = SCSlower;
            }
        }
        break;
        case SCKeyFast:
        {
            if (mode == VMMBackward)
            {
                _cmd = SCStop;
            }
            else
            {
                _cmd = SCFaster;
            }
        }
        break;
        case SCKeyFire:
            if (_vehicle->IsFirePrepare())
            {
                _cmd = SCFire;
            }
            else
            {
                _cmd = SCCeaseFire;
            }
            break;
    }

    switch (_cmd)
    {
        case SCForward:
            return "VehicleForward";
        case SCStop:
            return "VehicleStop";
        case SCBackward:
            return "VehicleBackward";
        case SCFaster:
            return "VehicleFaster";
        case SCSlower:
            return "VehicleSlower";
        case SCLeft:
            return "VehicleLeft";
        case SCRight:
            return "VehicleRight";
        case SCFire:
            return "VehicleDirectFire";
        case SCCeaseFire:
            return "VehicleCeaseFire";
    }
    Fail("Simple command");
    return nullptr;
}

RadioMessageVLoad::RadioMessageVLoad(Transport* vehicle, int weapon)
{
    _vehicle = vehicle;
    _weapon = weapon;
}

AIUnit* RadioMessageVLoad::GetSender() const
{
    return _vehicle ? _vehicle->CommanderUnit() : nullptr;
}

LSError RadioMessageVLoad::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(RadioMessage::Serialize(ar))
    PARAM_CHECK(ar.SerializeRef("Vehicle", _vehicle, 1))
    PARAM_CHECK(ar.Serialize("weapon", _weapon, 1))
    return LSOK;
}

NetworkMessageType RadioMessageVLoad::GetNMType(NetworkMessageClass cls) const
{
    return NMTMsgVLoad;
}

class IndicesMsgVLoad : public IndicesVMessage
{
    typedef IndicesVMessage base;

  public:
    int weapon;

    IndicesMsgVLoad();
    NetworkMessageIndices* Clone() const override { return new IndicesMsgVLoad; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesMsgVLoad::IndicesMsgVLoad()
{
    weapon = -1;
}

void IndicesMsgVLoad::Scan(NetworkMessageFormatBase* format)
{
    base::Scan(format);
    SCAN(weapon)
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesMsgVLoad()
{
    using namespace Poseidon;
    return new IndicesMsgVLoad();
}
namespace Poseidon
{

NetworkMessageFormat& RadioMessageVLoad::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("vehicle", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("In vehicle"));
    format.Add("weapon", NDTInteger, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Weapon index"));
    return format;
}

TMError RadioMessageVLoad::TransferMsg(NetworkMessageContext& ctx)
{
    PoseidonAssert(dynamic_cast<const IndicesMsgVLoad*>(ctx.GetIndices())) const IndicesMsgVLoad* indices =
        static_cast<const IndicesMsgVLoad*>(ctx.GetIndices());

    ITRANSF_REF(vehicle)
    ITRANSF(weapon)
    return TMOK;
}

void RadioMessageVLoad::Send()
{
    if (_vehicle)
    {
        _vehicle->SendLoad(_weapon);
    }
}

void RadioMessageVLoad::Transmitted()
{
    if (!_vehicle)
    {
        return;
    }
    AIUnit* receiver = _vehicle->GunnerUnit();
    if (!CheckUnitAlive(receiver))
    {
        return;
    }

    _vehicle->ReceivedLoad(_weapon);
}

const char* RadioMessageVLoad::PrepareSentence(SentenceParams& params)
{
    if (!_vehicle)
    {
        return nullptr;
    }
    AIUnit* receiver = _vehicle->GunnerUnit();
    if (!receiver)
    {
        return nullptr;
    }

    if (_weapon < 0 || _weapon >= _vehicle->NMagazineSlots())
    {
        return nullptr;
    }

    const char* sentence = "VehicleLoad";
    const Magazine* magazine = _vehicle->GetMagazineSlot(_weapon)._magazine;
    if (!magazine)
    {
        return nullptr;
    }
    const MagazineType* type = magazine->_type;
    params.AddWord(type->GetNameSound(), type->GetDisplayName());
    return sentence;
}

RadioMessageVAzimut::RadioMessageVAzimut(Transport* vehicle, float azimut)
{
    _vehicle = vehicle;
    _azimut = azimut;
}

AIUnit* RadioMessageVAzimut::GetSender() const
{
    return _vehicle ? _vehicle->CommanderUnit() : nullptr;
}

LSError RadioMessageVAzimut::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(RadioMessage::Serialize(ar))
    PARAM_CHECK(ar.SerializeRef("Vehicle", _vehicle, 1))
    PARAM_CHECK(ar.Serialize("azimut", _azimut, 1))
    return LSOK;
}

NetworkMessageType RadioMessageVAzimut::GetNMType(NetworkMessageClass cls) const
{
    return NMTMsgVAzimut;
}

class IndicesMsgVAzimut : public IndicesVMessage
{
    typedef IndicesVMessage base;

  public:
    int azimut;

    IndicesMsgVAzimut();
    NetworkMessageIndices* Clone() const override { return new IndicesMsgVAzimut; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesMsgVAzimut::IndicesMsgVAzimut()
{
    azimut = -1;
}

void IndicesMsgVAzimut::Scan(NetworkMessageFormatBase* format)
{
    base::Scan(format);
    SCAN(azimut)
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesMsgVAzimut()
{
    using namespace Poseidon;
    return new IndicesMsgVAzimut();
}
namespace Poseidon
{

NetworkMessageFormat& RadioMessageVAzimut::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("vehicle", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("In vehicle"));
    format.Add("azimut", NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Orientation azimuth"));
    return format;
}

TMError RadioMessageVAzimut::TransferMsg(NetworkMessageContext& ctx)
{
    PoseidonAssert(dynamic_cast<const IndicesMsgVAzimut*>(ctx.GetIndices())) const IndicesMsgVAzimut* indices =
        static_cast<const IndicesMsgVAzimut*>(ctx.GetIndices());

    ITRANSF_REF(vehicle)
    ITRANSF(azimut);
    return TMOK;
}

void RadioMessageVAzimut::Send()
{
    if (_vehicle)
    {
        _vehicle->SendAzimut(_azimut);
    }
}

void RadioMessageVAzimut::Transmitted()
{
    if (!_vehicle)
    {
        return;
    }
    AIUnit* receiver = _vehicle->PilotUnit();
    if (!CheckUnitAlive(receiver))
    {
        return;
    }

    _vehicle->ReceivedAzimut(_azimut);
}

const char* RadioMessageVAzimut::PrepareSentence(SentenceParams& params)
{
    if (!_vehicle)
    {
        return nullptr;
    }
    AIUnit* receiver = _vehicle->PilotUnit();
    if (!receiver)
    {
        return nullptr;
    }

    const char* sentence = "VehicleAzimut";
    int azimut = toInt(_azimut * (4.0 / H_PI));
    switch (azimut)
    {
        case 0:
            params.AddWord("north", LocalizeString(IDS_Q_NORTH));
            break;
        case 1:
            params.AddWord("northEast", LocalizeString(IDS_Q_NORTH_EAST));
            break;
        case 2:
            params.AddWord("east", LocalizeString(IDS_Q_EAST));
            break;
        case 3:
            params.AddWord("southEast", LocalizeString(IDS_Q_SOUTH_EAST));
            break;
        case 4:
            params.AddWord("south", LocalizeString(IDS_Q_SOUTH));
            break;
        case 5:
            params.AddWord("southWest", LocalizeString(IDS_Q_SOUTH_WEST));
            break;
        case 6:
            params.AddWord("west", LocalizeString(IDS_Q_WEST));
            break;
        case 7:
            params.AddWord("northWest", LocalizeString(IDS_Q_NORTH_WEST));
            break;
    }

    return sentence;
}

RadioMessageVStopTurning::RadioMessageVStopTurning(Transport* vehicle, float azimut)
{
    _vehicle = vehicle;
    _azimut = azimut;
}

LSError RadioMessageVStopTurning::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(RadioMessage::Serialize(ar))
    PARAM_CHECK(ar.SerializeRef("Vehicle", _vehicle, 1))
    PARAM_CHECK(ar.Serialize("azimut", _azimut, 1))
    return LSOK;
}

void RadioMessageVStopTurning::Transmitted()
{
    if (!_vehicle)
    {
        return;
    }
    AIUnit* receiver = _vehicle->PilotUnit();
    if (!CheckUnitAlive(receiver))
    {
        return;
    }

    _vehicle->ReceivedStopTurning(_azimut);
}

AIUnit* RadioMessageVStopTurning::GetSender() const
{
    return _vehicle ? _vehicle->CommanderUnit() : nullptr;
}

NetworkMessageType RadioMessageVStopTurning::GetNMType(NetworkMessageClass cls) const
{
    return NMTMsgVStopTurning;
}

class IndicesMsgVStopTurning : public IndicesVMessage
{
    typedef IndicesVMessage base;

  public:
    int azimut;

    IndicesMsgVStopTurning();
    NetworkMessageIndices* Clone() const override { return new IndicesMsgVStopTurning; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesMsgVStopTurning::IndicesMsgVStopTurning()
{
    azimut = -1;
}

void IndicesMsgVStopTurning::Scan(NetworkMessageFormatBase* format)
{
    base::Scan(format);
    SCAN(azimut)
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesMsgVStopTurning()
{
    using namespace Poseidon;
    return new IndicesMsgVStopTurning();
}
namespace Poseidon
{

NetworkMessageFormat& RadioMessageVStopTurning::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("vehicle", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("In vehicle"));
    format.Add("azimut", NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Final azimuth"));
    return format;
}

TMError RadioMessageVStopTurning::TransferMsg(NetworkMessageContext& ctx)
{
    PoseidonAssert(dynamic_cast<const IndicesMsgVStopTurning*>(ctx.GetIndices()))
        const IndicesMsgVStopTurning* indices = static_cast<const IndicesMsgVStopTurning*>(ctx.GetIndices());

    ITRANSF_REF(vehicle)
    ITRANSF(azimut);
    return TMOK;
}

void RadioMessageVStopTurning::Send()
{
    if (_vehicle)
    {
        _vehicle->SendStopTurning(_azimut);
    }
}

const char* RadioMessageVStopTurning::PrepareSentence(SentenceParams& params)
{
    if (!_vehicle)
    {
        return nullptr;
    }
    AIUnit* receiver = _vehicle->PilotUnit();
    if (!receiver)
    {
        return nullptr;
    }

    switch (_vehicle->GetMoveMode())
    {
        default:
        case VMMStop:
            return "VehicleStop";
        case VMMBackward:
            return "VehicleBackward";
        case VMMSlowForward:
            return "VehicleSlower";
        case VMMForward:
            return "VehicleForward";
        case VMMFastForward:
            return "VehicleFaster";
    }
}

void Transport::SendMove(Vector3Par pos)
{
    AIUnit* commander = CommanderUnit();
    if (!commander)
    {
        return;
    }

    AIUnit* receiver = PilotUnit();
    if (!receiver || receiver == commander)
    {
        return;
    }

    // ignore very short moves
    if (pos.Distance2(Position()) < Square(2))
    {
        commander->SetState(AIUnit::Completed);
        return;
    }

    _radio.Transmit(new RadioMessageVMove(this, pos), commander->GetGroup()->GetCenter()->GetLanguage());
}

void Transport::SendTarget(Target* tgt)
{
    AIUnit* commander = CommanderUnit();
    if (!commander)
    {
        return;
    }

    AIUnit* receiver = GunnerUnit();
    if (!receiver || receiver == commander)
    {
        return;
    }

    int index = INT_MAX;
    while (true)
    {
        RadioMessage* msg = _radio.GetPrevMessage(index);
        if (!msg)
        {
            break;
        }
        switch (msg->GetType())
        {
            case RMTVehicleTarget:
            {
                RadioMessageVTarget* msgT = static_cast<RadioMessageVTarget*>(msg);
                msgT->SetTarget(tgt);
                return;
            }
            break;
            case RMTVehicleSimpleCommand:
            {
                RadioMessageVSimpleCommand* msgT = static_cast<RadioMessageVSimpleCommand*>(msg);
                if (msgT->GetCommand() == SCFire)
                {
                    goto SendTargetTransmit;
                }
            }
            break;
        }
    }

    {
        RadioMessage* msg = _radio.GetActualMessage();
        if (msg)
        {
            switch (msg->GetType())
            {
                case RMTVehicleTarget:
                {
                    RadioMessageVTarget* msgT = static_cast<RadioMessageVTarget*>(msg);
                    if (msgT->GetTarget() == tgt)
                    {
                        return;
                    }
                    else
                    {
                        goto SendTargetTransmit;
                    }
                }
                break;
                case RMTVehicleSimpleCommand:
                {
                    RadioMessageVSimpleCommand* msgT = static_cast<RadioMessageVSimpleCommand*>(msg);
                    if (msgT->GetCommand() == SCFire)
                    {
                        goto SendTargetTransmit;
                    }
                }
                break;
            }
        }
    }

    if (tgt == _fire._fireTarget)
    {
        return;
    }

SendTargetTransmit:

    _radio.Transmit(new RadioMessageVTarget(this, tgt), commander->GetGroup()->GetCenter()->GetLanguage());
    _radio.Simulate(0);
}

void Transport::SendFire(Target* tgt)
{
    AIUnit* commander = CommanderUnit();
    if (!commander)
    {
        return;
    }

    AIUnit* receiver = GunnerUnit();
    if (!receiver || receiver == commander)
    {
        return;
    }

    _radio.Transmit(new RadioMessageVFire(this, tgt), commander->GetGroup()->GetCenter()->GetLanguage());
    _radio.Simulate(0);
}

void Transport::SendJoin()
{
    AIUnit* commander = CommanderUnit();
    if (!commander)
    {
        return;
    }

    AIUnit* receiver = PilotUnit();
    if (!receiver || receiver == commander)
    {
        return;
    }

    _radio.Transmit(new RadioMessageVFormation(this), commander->GetGroup()->GetCenter()->GetLanguage());
}

static bool IsSimpleMove(SimpleCommand type)
{
    switch (type)
    {
        case SCForward:
        case SCStop:
        case SCBackward:
        case SCFaster:
        case SCSlower:
        case SCKeyUp:
        case SCKeyDown:
        case SCKeySlow:
        case SCKeyFast:
            return true;
    }
    return false;
}
void Transport::SendSimpleCommand(SimpleCommand cmd)
{
    AIUnit* commander = CommanderUnit();
    if (!commander)
    {
        return;
    }

    AIUnit* receiver = nullptr;
    if (cmd == SCFire || cmd == SCCeaseFire || cmd == SCKeyFire)
    {
        receiver = GunnerUnit();
    }
    else
    {
        receiver = PilotUnit();
    }
    if (!receiver || receiver == commander)
    {
        return;
    }

    if (IsSimpleMove(cmd))
    {
        int index = INT_MAX;
        while (true)
        {
            RadioMessage* msg = _radio.GetPrevMessage(index);
            if (!msg)
            {
                break;
            }
            if (msg->GetType() == RMTVehicleSimpleCommand)
            {
                RadioMessageVSimpleCommand* cmd = static_cast<RadioMessageVSimpleCommand*>(msg);
                if (IsSimpleMove(cmd->GetCommand()))
                {
                    _radio.Cancel(msg);
                }
            }
        }
    }
    _radio.Transmit(new RadioMessageVSimpleCommand(this, cmd), commander->GetGroup()->GetCenter()->GetLanguage());
}

void Transport::SendLoad(int weapon)
{
    AIUnit* commander = CommanderUnit();
    if (!commander)
    {
        return;
    }

    AIUnit* receiver = GunnerUnit();
    if (!receiver || receiver == commander)
    {
        return;
    }

    int index = INT_MAX;
    while (true)
    {
        RadioMessage* msg = _radio.GetPrevMessage(index);
        if (!msg)
        {
            break;
        }
        switch (msg->GetType())
        {
            case RMTVehicleLoad:
            {
                RadioMessageVLoad* msgT = static_cast<RadioMessageVLoad*>(msg);
                msgT->SetWeapon(weapon);
                return;
            }
            break;
            case RMTVehicleSimpleCommand:
            {
                RadioMessageVSimpleCommand* msgT = static_cast<RadioMessageVSimpleCommand*>(msg);
                if (msgT->GetCommand() == SCFire)
                {
                    goto SendLoadTransmit;
                }
            }
            break;
        }
    }

    {
        int actWeapon = _currentWeapon;
        RadioMessage* msg = _radio.GetActualMessage();
        if (msg)
        {
            switch (msg->GetType())
            {
                case RMTVehicleLoad:
                {
                    RadioMessageVLoad* msgT = static_cast<RadioMessageVLoad*>(msg);
                    actWeapon = msgT->GetWeapon();
                }
                break;
                case RMTVehicleSimpleCommand:
                {
                    RadioMessageVSimpleCommand* msgT = static_cast<RadioMessageVSimpleCommand*>(msg);
                    if (msgT->GetCommand() == SCFire)
                    {
                        goto SendLoadTransmit;
                    }
                }
                break;
            }
        }
        if (actWeapon == weapon)
        {
            return;
        }
    }

SendLoadTransmit:
    _radio.Transmit(new RadioMessageVLoad(this, weapon), commander->GetGroup()->GetCenter()->GetLanguage());
}

void Transport::SendAzimut(float azimut)
{
    AIUnit* commander = CommanderUnit();
    if (!commander)
    {
        return;
    }

    AIUnit* receiver = PilotUnit();
    if (!receiver || receiver == commander)
    {
        return;
    }

    _radio.Transmit(new RadioMessageVAzimut(this, azimut), commander->GetGroup()->GetCenter()->GetLanguage());
}

void Transport::SendStopTurning(float azimut)
{
    AIUnit* commander = CommanderUnit();
    if (!commander)
    {
        return;
    }

    AIUnit* receiver = PilotUnit();
    if (!receiver || receiver == commander)
    {
        return;
    }

    int index = INT_MAX;
    bool oneTurnSkipped = false;
    while (true)
    {
        RadioMessage* msg = _radio.GetPrevMessage(index);
        if (!msg)
        {
            break;
        }
        if (msg->GetType() == RMTVehicleStopTurning)
        {
            _radio.Cancel(msg);
        }
        else if (msg->GetType() == RMTVehicleSimpleCommand)
        {
            RadioMessageVSimpleCommand* cmd = static_cast<RadioMessageVSimpleCommand*>(msg);
            if (cmd->GetCommand() == SCLeft || cmd->GetCommand() == SCRight)
            {
                if (!oneTurnSkipped)
                {
                    oneTurnSkipped = true;
                }
                else
                {
                    _radio.Cancel(msg);
                }
            }
        }
    }

    _radio.Transmit(new RadioMessageVStopTurning(this, azimut), commander->GetGroup()->GetCenter()->GetLanguage());
}

void Transport::SendFireFailed()
{
    AIUnit* from = GunnerUnit();
    if (!from)
    {
        return;
    }

    AIUnit* receiver = CommanderUnit();
    if (!receiver || receiver == from)
    {
        return;
    }

    _radio.Transmit(new RadioMessageVFireFailed(this), from->GetGroup()->GetCenter()->GetLanguage());
}

void Transport::ResetStatus()
{
    base::ResetStatus();
}

void Transport::UpdateStopTimeout()
{
    float maxTime = 0;

    for (int i = 0; i < _getinUnits.Size(); i++)
    {
        AIUnit* unit = _getinUnits[i];
        if (!unit)
        {
            continue;
        }

        // Calculate cost from starting field
        int x = toIntFloor(unit->Position().X() * InvLandGrid);
        int z = toIntFloor(unit->Position().Z() * InvLandGrid);
        GeographyInfo geogr = GLOB_LAND->GetGeography(x, z);
        float dist = Position().Distance(unit->Position());
        float time = 2.0 * unit->GetVehicle()->GetCost(geogr) * dist + 30.0;
        saturateMax(maxTime, time);
    }

    _getinTime = Glob.time + maxTime;
}

template <>
const EnumName* Foundation::GetEnumNames(VehicleMoveMode dummy)
{
    static const EnumName MoveModeNames[] = {
        EnumName(VMMFormation, "FORMATION"), EnumName(VMMMove, "MOVE"),
        EnumName(VMMBackward, "BACK"),       EnumName(VMMStop, "STOP"),
        EnumName(VMMSlowForward, "SLOW"),    EnumName(VMMForward, "FORWARD"),
        EnumName(VMMFastForward, "FAST"),    EnumName(),
    };
    return MoveModeNames;
}

template <>
const EnumName* Foundation::GetEnumNames(VehicleTurnMode dummy)
{
    static const EnumName TurnModeNames[] = {EnumName(VTMNone, "NONE"), EnumName(VTMLeft, "LEFT"),
                                             EnumName(VTMRight, "RIGHT"), EnumName(VTMAbs, "ABS"), EnumName()};
    return TurnModeNames;
}

LSError Transport::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar))

    if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
    {
        PARAM_CHECK(ar.SerializeRef("Driver", _driver, 1))
        PARAM_CHECK(ar.SerializeRef("Gunner", _gunner, 1))
        PARAM_CHECK(ar.SerializeRef("Commander", _commander, 1))
        PARAM_CHECK(ar.SerializeRef("EffCommander", _effCommander, 1))
        if (ar.IsSaving())
        {
            RefArray<Person> manCargo;
            int n = _manCargo.Size();
            manCargo.Resize(n);
            for (int i = 0; i < n; i++)
            {
                manCargo[i] = _manCargo[i];
            }
            PARAM_CHECK(ar.SerializeRefs("ManCargo", manCargo, 1))
        }
        else if (ar.GetPass() == ParamArchive::PassSecond)
        {
            RefArray<Person> manCargo;
            PARAM_CHECK(ar.SerializeRefs("ManCargo", manCargo, 1))
            int n = Type()->_maxManCargo;
            _manCargo.Resize(n);
            saturateMin(n, manCargo.Size());
            for (int i = 0; i < n; i++)
            {
                _manCargo.Set(i).man = manCargo[i];
            }

            if (_driver)
            {
                _driver->SetPilotLight(false);
                _driver->SwitchLight(false);
            }
            if (_gunner)
            {
                _gunner->SetPilotLight(false);
                _gunner->SwitchLight(false);
            }
            if (_commander)
            {
                _commander->SetPilotLight(false);
                _commander->SwitchLight(false);
            }
            for (int i = 0; i < n; i++)
            {
                Person* man = _manCargo[i];
                if (man)
                {
                    man->SetPilotLight(false);
                    man->SwitchLight(false);
                }
            }
        }

        PARAM_CHECK(ar.SerializeRef("GroupAssigned", _groupAssigned, 1))
        PARAM_CHECK(ar.SerializeRef("DriverAssigned", _driverAssigned, 1))
        PARAM_CHECK(ar.SerializeRef("GunnerAssigned", _gunnerAssigned, 1))
        PARAM_CHECK(ar.SerializeRef("CommanderAssigned", _commanderAssigned, 1))
        PARAM_CHECK(ar.SerializeRefs("CargoAssigned", _cargoAssigned, 1))

        PARAM_CHECK(ar.SerializeRefs("GetoutUnits", _getoutUnits, 1))
        PARAM_CHECK(ar.SerializeRefs("GetinUnits", _getinUnits, 1))
        PARAM_CHECK(ar.Serialize("getinTime", _getinTime, 1))
        PARAM_CHECK(ar.Serialize("getoutTime", _getoutTime, 1))

        PARAM_CHECK(ar.Serialize("driverPos", _driverPos, 1, VZero))
        PARAM_CHECK(ar.Serialize("dirWanted", _dirWanted, 1, VZero))

        PARAM_CHECK(ar.SerializeEnum("moveMode", _moveMode, 1, VMMFormation))
        PARAM_CHECK(ar.SerializeEnum("turnMode", _turnMode, 1, VTMNone))

        PARAM_CHECK(ar.Serialize("comFireMode", _commanderFire._fireMode, 1, -1))
        PARAM_CHECK(ar.Serialize("comFirePrepareOnly", _commanderFire._firePrepareOnly, 1, true))
        PARAM_CHECK(ar.SerializeRef("comFireTarget", _commanderFire._fireTarget, 1))

        PARAM_CHECK(ar.Serialize("Radio", _radio, 1))

        if (ar.IsSaving() || ar.GetArVersion() >= 10)
            PARAM_CHECK(ar.SerializeEnum("lock", _lock, 1, (LockState)LSDefault))
        else if (ar.GetPass() == ParamArchive::PassFirst)
        {
            bool locked;
            PARAM_CHECK(ar.Serialize("locked", locked, 1, false))
            _lock = locked ? LSDefault : LSLocked;
        }

        PARAM_CHECK(ar.Serialize("manualFire", _manualFire, 1, false))
        PARAM_CHECK(ar.Serialize("turretFrontUntil", _turretFrontUntil, 1, TIME_MIN))
    }
    PARAM_CHECK(ar.Serialize("fuel", _fuel, 1, Type()->GetFuelCapacity()))
    PARAM_CHECK(ar.Serialize("engineOff", _engineOff, 1, false))
    return LSOK;
}

NetworkMessageType Transport::GetNMType(NetworkMessageClass cls) const
{
    switch (cls)
    {
        case NMCUpdateGeneric:
            return NMTUpdateTransport;
        default:
            return base::GetNMType(cls);
    }
}

IndicesUpdateTransport::IndicesUpdateTransport()
{
    commander = -1;
    driver = -1;
    gunner = -1;
    effCommander = -1;
    manCargo = -1;
    comFireTarget = -1;
    lock = -1;
    driverHiddenWanted = -1;
    gunnerHiddenWanted = -1;
    commanderHiddenWanted = -1;
    fuel = -1;
    engineOff = -1;
}

void IndicesUpdateTransport::Scan(NetworkMessageFormatBase* format)
{
    base::Scan(format);

    SCAN(commander)
    SCAN(driver)
    SCAN(gunner)
    SCAN(effCommander)
    SCAN(manCargo)
    SCAN(comFireTarget)
    SCAN(lock)
    SCAN(driverHiddenWanted)
    SCAN(gunnerHiddenWanted)
    SCAN(commanderHiddenWanted)
    SCAN(fuel);
    SCAN(engineOff);
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesUpdateTransport()
{
    using namespace Poseidon;
    return new IndicesUpdateTransport();
}
namespace Poseidon
{

NetworkMessageFormat& Transport::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    switch (cls)
    {
        case NMCUpdateGeneric:
            base::CreateFormat(cls, format);
            format.Add("commander", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Commander (observer) person"), ET_NOT_EQUAL,
                       ERR_COEF_STRUCTURE);
            format.Add("driver", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Driver person"), ET_NOT_EQUAL,
                       ERR_COEF_STRUCTURE);
            format.Add("gunner", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Gunner person"), ET_NOT_EQUAL,
                       ERR_COEF_STRUCTURE);
            format.Add("effCommander", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Currently commanding person"),
                       ET_NOT_EQUAL, ERR_COEF_STRUCTURE);
            format.Add("manCargo", NDTRefArray, NCTNone, DEFVALUEREFARRAY, DOC_MSG("Men in cargo space"),
                       ET_NOT_EQUAL_COUNT, 0.5 * ERR_COEF_STRUCTURE);
            format.Add("comFireTarget", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Fire target, selected by commander"),
                       ET_NOT_EQUAL, ERR_COEF_MODE);
            format.Add("lock", NDTInteger, NCTSmallSigned, DEFVALUE(int, LSDefault), DOC_MSG("Lock state"),
                       ET_NOT_EQUAL, ERR_COEF_MODE);
            format.Add("driverHiddenWanted", NDTFloat, NCTFloat0To1, DEFVALUE(float, 0),
                       DOC_MSG("Wanted position of (tank) driver - inside / outside"), ET_NOT_EQUAL,
                       ERR_COEF_VALUE_MAJOR);
            format.Add("gunnerHiddenWanted", NDTFloat, NCTFloat0To1, DEFVALUE(float, 0),
                       DOC_MSG("Wanted position of (tank) gunner - inside / outside"), ET_NOT_EQUAL,
                       ERR_COEF_VALUE_MAJOR);
            format.Add("commanderHiddenWanted", NDTFloat, NCTFloat0To1, DEFVALUE(float, 0),
                       DOC_MSG("Wanted position of (tank) commander - inside / outside"), ET_NOT_EQUAL,
                       ERR_COEF_VALUE_MAJOR);
            format.Add("fuel", NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Current amount of fuel"), ET_ABS_DIF,
                       ERR_COEF_VALUE_MINOR);
            format.Add("engineOff", NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Engine is off / on"), ET_ABS_DIF,
                       ERR_COEF_MODE);
            break;
        default:
            base::CreateFormat(cls, format);
            break;
    }
    return format;
}

bool IsInVehicle(Person* soldier, Transport* veh)
{
    if (veh->Commander() == soldier)
    {
        return true;
    }
    if (veh->Driver() == soldier)
    {
        return true;
    }
    if (veh->Gunner() == soldier)
    {
        return true;
    }
    for (int i = 0; i < veh->GetManCargo().Size(); i++)
    {
        if (veh->GetManCargo()[i] == soldier)
        {
            return true;
        }
    }
    return false;
}

TMError Transport::TransferMsg(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
            TMCHECK(base::TransferMsg(ctx))
            {
                PoseidonAssert(dynamic_cast<const IndicesUpdateTransport*>(ctx.GetIndices()))
                    const IndicesUpdateTransport* indices =
                        static_cast<const IndicesUpdateTransport*>(ctx.GetIndices());

                if (ctx.IsSending())
                {
                    ITRANSF_REF(commander)
                    ITRANSF_REF(driver)
                    ITRANSF_REF(gunner)
                    ITRANSF_REF(effCommander)
                    RefArray<Person> manCargo;
                    int n = _manCargo.Size();
                    manCargo.Resize(n);
                    for (int i = 0; i < n; i++)
                    {
                        manCargo[i] = _manCargo[i];
                    }
                    TMCHECK(ctx.IdxTransferRefs(indices->manCargo, manCargo))
                    EntityAI* target = nullptr;
                    if (_commanderFire._fireTarget)
                    {
                        target = _commanderFire._fireTarget->idExact;
                    }
                    TMCHECK(ctx.IdxTransferRef(indices->comFireTarget, target));
                }
                else
                {
                    Person* soldier;
                    TMCHECK(ctx.IdxTransferRef(indices->commander, soldier))
                    if (soldier != _commander)
                    {
                        if (soldier && IsInVehicle(soldier, this))
                        {
                            ChangePosition(ATMoveToCommander, soldier);
                        }
                        else
                        {
                            if (_commander)
                            {
                                AIUnit* unit = _commander->Brain();
                                if (unit)
                                {
                                    bool isFocused = (GWorld->FocusOn() == unit);
                                    unit->DoGetOut(this, false);
                                    if (isFocused)
                                    {
                                        GWorld->SwitchCameraTo(unit->GetVehicle(),
                                                               ValidateCamera(GWorld->GetCameraType()));
                                    }
                                }
                                else
                                {
                                    Vector3 pos = GetCommanderGetOutPos(_commander);
                                    Matrix4 transform;
                                    transform.SetPosition(pos);
                                    transform.SetUpAndDirection(VUp, pos - Position());
                                    GetOutCommander(transform);
                                }
                            }
                            if (soldier)
                            {
                                GetInCommander(soldier);
                                if (GWorld->GetRealPlayer() == soldier)
                                {
                                    GWorld->SwitchCameraTo(this, ValidateCamera(GWorld->GetCameraType()));
                                }
                                AIUnit* unit = soldier->Brain();
                                if (unit)
                                {
                                    unit->AssignAsCommander(this);
                                    unit->OrderGetIn(true);
                                }
                            }
                        }
                    }
                    TMCHECK(ctx.IdxTransferRef(indices->driver, soldier))
                    if (soldier != _driver)
                    {
                        if (soldier && IsInVehicle(soldier, this))
                        {
                            ChangePosition(ATMoveToDriver, soldier);
                        }
                        else
                        {
                            if (_driver)
                            {
                                AIUnit* unit = _driver->Brain();
                                if (unit)
                                {
                                    bool isFocused = (GWorld->FocusOn() == unit);
                                    unit->DoGetOut(this, false);
                                    if (isFocused)
                                    {
                                        GWorld->SwitchCameraTo(unit->GetVehicle(),
                                                               ValidateCamera(GWorld->GetCameraType()));
                                    }
                                }
                                else
                                {
                                    Vector3 pos = GetDriverGetOutPos(_driver);
                                    Matrix4 transform;
                                    transform.SetPosition(pos);
                                    transform.SetUpAndDirection(VUp, pos - Position());
                                    GetOutDriver(transform);
                                }
                            }
                            if (soldier)
                            {
                                GetInDriver(soldier);
                                if (GWorld->GetRealPlayer() == soldier)
                                {
                                    GWorld->SwitchCameraTo(this, ValidateCamera(GWorld->GetCameraType()));
                                }
                                AIUnit* unit = soldier->Brain();
                                if (unit)
                                {
                                    unit->AssignAsDriver(this);
                                    unit->OrderGetIn(true);
                                }
                            }
                        }
                    }
                    TMCHECK(ctx.IdxTransferRef(indices->gunner, soldier))
                    if (soldier != _gunner)
                    {
                        if (soldier && IsInVehicle(soldier, this))
                        {
                            ChangePosition(ATMoveToGunner, soldier);
                        }
                        else
                        {
                            if (_gunner)
                            {
                                AIUnit* unit = _gunner->Brain();
                                if (unit)
                                {
                                    bool isFocused = (GWorld->FocusOn() == unit);
                                    unit->DoGetOut(this, false);
                                    if (isFocused)
                                    {
                                        GWorld->SwitchCameraTo(unit->GetVehicle(),
                                                               ValidateCamera(GWorld->GetCameraType()));
                                    }
                                }
                                else
                                {
                                    Vector3 pos = GetGunnerGetOutPos(_gunner);
                                    Matrix4 transform;
                                    transform.SetPosition(pos);
                                    transform.SetUpAndDirection(VUp, pos - Position());
                                    GetOutGunner(transform);
                                }
                            }
                            if (soldier)
                            {
                                GetInGunner(soldier);
                                if (GWorld->GetRealPlayer() == soldier)
                                {
                                    GWorld->SwitchCameraTo(this, ValidateCamera(GWorld->GetCameraType()));
                                }
                                AIUnit* unit = soldier->Brain();
                                if (unit)
                                {
                                    unit->AssignAsGunner(this);
                                    unit->OrderGetIn(true);
                                }
                            }
                        }
                    }
                    RefArray<Person> manCargo;
                    TMCHECK(ctx.IdxTransferRefs(indices->manCargo, manCargo))
                    for (int i = 0; i < _manCargo.Size(); i++)
                    {
                        soldier = i < manCargo.Size() ? manCargo[i] : nullptr;
                        if (soldier != _manCargo[i])
                        {
                            if (_manCargo[i])
                            {
                                AIUnit* unit = _manCargo[i]->Brain();
                                if (unit)
                                {
                                    bool isFocused = (GWorld->FocusOn() == unit);
                                    unit->DoGetOut(this, false);
                                    if (isFocused)
                                    {
                                        GWorld->SwitchCameraTo(unit->GetVehicle(),
                                                               ValidateCamera(GWorld->GetCameraType()));
                                    }
                                }
                                else
                                {
                                    Vector3 pos = GetCargoGetOutPos(_manCargo[i]);
                                    Matrix4 transform;
                                    transform.SetPosition(pos);
                                    transform.SetUpAndDirection(VUp, pos - Position());
                                    GetOutCargo(_manCargo[i], transform);
                                }
                            }
                            if (soldier)
                            {
                                GetInCargo(soldier, true, i);
                                if (GWorld->GetRealPlayer() == soldier)
                                {
                                    GWorld->SwitchCameraTo(this, ValidateCamera(GWorld->GetCameraType()));
                                }
                                AIUnit* unit = soldier->Brain();
                                if (unit)
                                {
                                    unit->AssignAsCargo(this);
                                    unit->OrderGetIn(true);
                                }
                            }
                        }
                    }

                    EntityAI* target = nullptr;
                    _commanderFire._fireTarget = nullptr;
                    TMCHECK(ctx.IdxTransferRef(indices->comFireTarget, target));
                    if (target)
                    {
                        AIUnit* unit = CommanderUnit();
                        AIGroup* grp = unit ? unit->GetGroup() : nullptr;
                        if (grp)
                        {
                            _commanderFire._fireTarget = grp->FindTarget(target);
                        }
                    }
                }
                ITRANSF_ENUM(lock)
                ITRANSF(driverHiddenWanted)
                ITRANSF(gunnerHiddenWanted)
                ITRANSF(commanderHiddenWanted)
                if (ctx.IsSending())
                {
                    ITRANSF(engineOff)
                    ITRANSF(fuel)
                }
                else
                {
                    bool engineOff;
                    float fuel;
                    TMCHECK(ctx.IdxTransfer(indices->engineOff, engineOff))
                    TMCHECK(ctx.IdxTransfer(indices->fuel, fuel))
                    if (engineOff != _engineOff)
                    {
                        _engineOff = engineOff;
                        OnEvent(EEEngine, _engineOff);
                    }
                    if ((_fuel > 0) != (fuel > 0))
                    {
                        OnEvent(EEFuel, fuel > 0);
                    }
                    _fuel = fuel;
                }
            }
            break;
        default:
            return base::TransferMsg(ctx);
    }
    return TMOK;
}

float Transport::CalculateError(NetworkMessageContext& ctx)
{
    float error = 0;
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
        {
            error += base::CalculateError(ctx);

            PoseidonAssert(dynamic_cast<const IndicesUpdateTransport*>(ctx.GetIndices()))
                const IndicesUpdateTransport* indices = static_cast<const IndicesUpdateTransport*>(ctx.GetIndices());

            ICALCERR_NEQREF(Person, driver, ERR_COEF_STRUCTURE)
            ICALCERR_NEQREF(Person, commander, ERR_COEF_STRUCTURE)
            ICALCERR_NEQREF(Person, gunner, ERR_COEF_STRUCTURE)

            RefArray<Person> manCargo;
            if (ctx.IdxTransferRefs(indices->manCargo, manCargo) == TMOK)
            {
                const float coef = 0.5 * ERR_COEF_STRUCTURE;
                for (int i = 0; i < _manCargo.Size(); i++)
                {
                    Person* soldier = i < manCargo.Size() ? manCargo[i] : nullptr;
                    if (soldier != _manCargo[i])
                    {
                        error += coef;
                    }
                }
            }
            EntityAI* target = nullptr;
            if (_commanderFire._fireTarget)
            {
                target = _commanderFire._fireTarget->idExact;
            }
            ICALCERRE_NEQREF(Object, comFireTarget, target, ERR_COEF_MODE)

            ICALCERR_NEQ(int, lock, ERR_COEF_STRUCTURE)
            ICALCERR_ABSDIF(float, driverHiddenWanted, ERR_COEF_VALUE_MAJOR)
            ICALCERR_ABSDIF(float, gunnerHiddenWanted, ERR_COEF_VALUE_MAJOR)
            ICALCERR_ABSDIF(float, commanderHiddenWanted, ERR_COEF_VALUE_MAJOR)
            ICALCERR_ABSDIF(float, fuel, ERR_COEF_VALUE_MINOR)
            ICALCERR_NEQ(bool, engineOff, ERR_COEF_MODE)
        }
        break;
        default:
            error += base::CalculateError(ctx);
            break;
    }
    return error;
}

void Transport::DestroyObject()
{
    if (_commander)
    {
        AIUnit* unit = _commander->Brain();
        if (unit)
        {
            bool isFocused = (GWorld->FocusOn() == unit);
            unit->DoGetOut(this, false);
            if (isFocused)
            {
                GWorld->SwitchCameraTo(unit->GetVehicle(), ValidateCamera(GWorld->GetCameraType()));
            }
        }
        else
        {
            Vector3 pos = GetCommanderGetOutPos(_commander);
            Matrix4 transform;
            transform.SetPosition(pos);
            transform.SetUpAndDirection(VUp, pos - Position());
            GetOutCommander(transform);
        }
    }
    if (_driver)
    {
        AIUnit* unit = _driver->Brain();
        if (unit)
        {
            bool isFocused = (GWorld->FocusOn() == unit);
            unit->DoGetOut(this, false);
            if (isFocused)
            {
                GWorld->SwitchCameraTo(unit->GetVehicle(), ValidateCamera(GWorld->GetCameraType()));
            }
        }
        else
        {
            Vector3 pos = GetDriverGetOutPos(_driver);
            Matrix4 transform;
            transform.SetPosition(pos);
            transform.SetUpAndDirection(VUp, pos - Position());
            GetOutDriver(transform);
        }
    }
    if (_gunner)
    {
        AIUnit* unit = _gunner->Brain();
        if (unit)
        {
            bool isFocused = (GWorld->FocusOn() == unit);
            unit->DoGetOut(this, false);
            if (isFocused)
            {
                GWorld->SwitchCameraTo(unit->GetVehicle(), ValidateCamera(GWorld->GetCameraType()));
            }
        }
        else
        {
            Vector3 pos = GetGunnerGetOutPos(_gunner);
            Matrix4 transform;
            transform.SetPosition(pos);
            transform.SetUpAndDirection(VUp, pos - Position());
            GetOutGunner(transform);
        }
    }
    for (int i = 0; i < _manCargo.Size(); i++)
    {
        if (_manCargo[i])
        {
            AIUnit* unit = _manCargo[i]->Brain();
            if (unit)
            {
                bool isFocused = (GWorld->FocusOn() == unit);
                unit->DoGetOut(this, false);
                if (isFocused)
                {
                    GWorld->SwitchCameraTo(unit->GetVehicle(), ValidateCamera(GWorld->GetCameraType()));
                }
            }
            else
            {
                Vector3 pos = GetCargoGetOutPos(_manCargo[i]);
                Matrix4 transform;
                transform.SetPosition(pos);
                transform.SetUpAndDirection(VUp, pos - Position());
                GetOutCargo(_manCargo[i], transform);
            }
        }
    }

    base::DestroyObject();
}

} // namespace Poseidon
