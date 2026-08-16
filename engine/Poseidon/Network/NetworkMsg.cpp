#include <Poseidon/Foundation/Algorithms/Crc.hpp>
#include <algorithm>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Network/NetworkImpl.hpp>
#include <Poseidon/Dev/Debug/DebugTrap.hpp>
#include <stdlib.h>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

using namespace Poseidon::Dev;
using Poseidon::Foundation::Time;
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Core/IdString.hpp>

#include <Poseidon/Foundation/Algorithms/Qsort.hpp>

#include <Poseidon/Foundation/Algorithms/Crc.hpp>
#include <Poseidon/AI/AI.hpp>

// common string table

// note: following strings are obtained from MP report file
// (see NetStrStats)

// note: due to compression used most used string should be listed first

using namespace Poseidon;
static RStringB* GetNetStrings(int& count)
{
    static RStringB NetStrings[] = {
        // weapons and magazines
        "M16",
        "AK74",
        "HandGrenade",
        "M60",
        "M4",
        "Put",
        "Throw",
        "NVGoggles",
        "CarHorn",
        "TruckHorn",

        "AK47",
        "KozliceBall",
        "KozliceShell",
        "M21",
        "GrenadeLauncher",
        "AK47CZ",
        "Kozlice",
        "AK47GrenadeLauncher",

        "SmokeShell",
        "Binocular",
        "Bullet7_6W",
        "Bullet12_7W",
        "Bullet7_6E",
        "Bullet12_7E",
        "Bullet7_6G",
        "Bullet12_7G",

        "BulletBurstW",
        "BulletSingleW",
        "BulletFullAutoW",
        "BulletBurstE",
        "BulletSingleE",
        "BulletFullAutoE",
        "BulletBurstG",
        "BulletSingleG",
        "BulletFullAutoG",

        "ZsuCannon",
        "MachineGun7_6",
        "Wire",
        "PK",
        "AT3Launcher",
        "Heat73",
        "Gun73",
        "AK74SU",
        "Shell73",
        "Heat120",
        "Gun120",
        "Shell120",
        "RPGLauncher",
        "LAWLauncher",
        "StrokeFist",

        "HellfireLauncherCobra",
        "MachineGun30",
        "ZuniLauncher38",

        // vehicle class names
        "SoldierEB",
        "SoldierECrew",
        "SoldierWB",
        "SoldierWCrew",
        "FenceWood",
        "Danger",
        "ObjectDestructed",

        // format (type) messages
        "objectId",
        "objectPosition",
        "objectCreator",
        "vehicle",
        "dammage",
        "canSmoke",
        "isDestroyed",
        "destroyed",
        "targetSide",
        "name",
        "type",
        "magazines",
        "initialUpdate",
        "weapons",
        "currentWeapon",
        "magazineSlots",
        "action",
        "pilotLight",
        "fireTarget",
        "hit",
        "isDead",
        "position",
        "fuelCargo",
        "ammoCargo",
        "infantryAmmoCargo",
        "repairCargo",
        "alloc",
        "actionParam",
        "actionParam2",
        "id",
        "actionParam3",
        "supplying",
    };
    count = sizeof(NetStrings) / sizeof(*NetStrings);
    return NetStrings;
}

static IdStringTable& GetNetIdStrings()
{
    int count = 0;
    RStringB* strings = GetNetStrings(count);
    static IdStringTable NetIdStrings(strings, count);
    return NetIdStrings;
}

// note: due to compression used most used string should be listed first

static RStringB* GetNetMoves(int& count)
{
    static RStringB NetMoves[] = {
        // basic movement
        "stand",
        "combatsprintrf",
        "combatsprintf",
        "combatsprintlf",
        "combat",
        "combatrunr",
        "combatrunl",
        "combatrunb",
        "combatrunf",
        "combatrunrb",
        "crouchrunf",
        "combatrelaxed",
        "combatturnlrelaxed",
        "combatturnrrelaxed",
        "combatrelaxedstill",
        "lyingfastcrawlf",
        "lyingcrawlf",
        "crouch",
        "lying",
        "crouchrunr",
        "combatrunlb",
        "crouchrunl",
        "crouchrunrf",
        "civilrunf",
        "crouchrunlf",
        "combatwalkf",
        "combatrunrf",
        "lyingcrawlrf",
        "crouchrunlb",
        "civil",
        "combatrunlf",
        "crouchrunb",
        "crouchrunrb",
        "lyingcrawllf",
        "civillying",
        "lyingcrawlb",
        "lyingcrawllb",
        "crouchsprintf",
        "combatdeadver2",
        "civillyingcrawlf",
        "lyingcrawlrb",
        "combatrundead",
        "civillyingfastcrawlf",
        "combatturnr",
        "lyingturnl",
        "combatturnl",
        "civilwalkf",
        "lyingdead",
        "combatstillplayer",
        "lyingturnr",
        "combatwalkr",
        "civilstillv1",
        "civilrunrf",
        "combatdeadver3",
        "putdown",
        "binoclying",
        "binoc",
    };
    count = sizeof(NetMoves) / sizeof(*NetMoves);
    return NetMoves;
}

static IdStringTable& GetNetIdMoves()
{
    int count = 0;
    RStringB* strings = GetNetMoves(count);
    static IdStringTable NetIdMoves(strings, count);
    return NetIdMoves;
}

// Message format

namespace Poseidon::Foundation
{
template class Ref<NetworkObject>;
} // namespace Poseidon::Foundation

NetworkMessageFormatBase::NetworkMessageFormatBase() = default;

void NetworkMessageFormatBase::Init(NetworkMessageIndices* indices)
{
    NET_ERROR(indices);
    NET_ERROR(_map.NItems() == 0);

    // create _map
    int n = _items.Size();
    for (int i = 0; i < n; i++)
    {
        const char* name = _items[i].name;

        // avoid duplicity in names
        const NameToIndex& check = _map[name];
        NET_ERROR(_map.IsNull(check));

        _map.Add(NameToIndex(name, i));
    }

    // scan indices
    _indices = indices;
    _indices->Scan(this);
}

void NetworkMessageFormatBase::Clear()
{
    _items.Clear();
    _map.Clear();
}

int NetworkMessageFormatBase::FindIndex(const char* name) const
{
    const NameToIndex& item = _map[name];
    if (_map.NotNull(item))
    {
        return item.index;
    }
    else
    {
        LOG_DEBUG(Network, "Warning: item {} not found", name);
        return -1;
    }
}

// network message indices for NetworkMessageFormatItem class
class IndicesMsgFormatItem : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int name;
    int type;
    int compression;
    int defValue;

    IndicesMsgFormatItem();
    NetworkMessageIndices* Clone() const override { return new IndicesMsgFormatItem; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesMsgFormatItem::IndicesMsgFormatItem()
{
    name = -1;
    type = -1;
    compression = -1;
    defValue = -1;
}

void IndicesMsgFormatItem::Scan(NetworkMessageFormatBase* format){SCAN(name) SCAN(type) SCAN(compression)
                                                                      SCAN(defValue)}

// Create network message indices for NetworkMessageFormatItem class
NetworkMessageIndices* GetIndicesMsgFormatItem()
{
    return new IndicesMsgFormatItem();
}

NetworkMessageFormat& NetworkMessageFormatItem::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("name", NDTString, NCTDefault, ND_NULL, DOC_MSG("Item name"));
    format.Add("type", NDTInteger, NCTSmallUnsigned, ND_NULL, DOC_MSG("Item type"));
    format.Add("compression", NDTInteger, NCTSmallUnsigned, ND_NULL, DOC_MSG("Item compression"));
    format.Add("defValue", NDTData, NCTNone, ND_NULL, DOC_MSG("Default value of item"));
    return format;
}

TMError NetworkMessageFormatItem::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesMsgFormatItem*>(ctx.GetIndices()))
    const IndicesMsgFormatItem* indices = static_cast<const IndicesMsgFormatItem*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransfer(indices->name, name))
    TMCHECK(ctx.IdxTransfer(indices->type, (int&)type))
    TMCHECK(ctx.IdxTransfer(indices->compression, (int&)compression))
    TMCHECK(ctx.IdxTransfer(indices->defValue, type, compression, defValue))
    return TMOK;
}

// network message indices for NetworkMessageFormatBase class
class IndicesMsgFormat : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int index;
    int items;

    IndicesMsgFormat();
    NetworkMessageIndices* Clone() const override { return new IndicesMsgFormat; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesMsgFormat::IndicesMsgFormat()
{
    index = -1;
    items = -1;
}

void IndicesMsgFormat::Scan(NetworkMessageFormatBase* format){SCAN(index) SCAN(items)}

// Create network message indices for NetworkMessageFormatBase class
NetworkMessageIndices* GetIndicesMsgFormat()
{
    return new IndicesMsgFormat();
}

// Return index of field "index" in IndicesMsgFormat
int IndicesMsgFormatGetIndex(const NetworkMessageIndices* ind)
{
    NET_ERROR(dynamic_cast<const IndicesMsgFormat*>(ind))
    const IndicesMsgFormat* indices = static_cast<const IndicesMsgFormat*>(ind);

    return indices->index;
}

NetworkMessageFormat& NetworkMessageFormatBase::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("index", NDTInteger, NCTSmallUnsigned, ND_NULL, DOC_MSG("Index of message type"));
    format.Add("items", NDTObjectArray, NCTNone, DEFVALUE_MSG(NMTMsgFormatItem), DOC_MSG("List of items"));
    return format;
}

TMError NetworkMessageFormatBase::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesMsgFormat*>(ctx.GetIndices()))
    const IndicesMsgFormat* indices = static_cast<const IndicesMsgFormat*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransferArray(indices->items, _items))
    return TMOK;
}

int NetworkMessageFormat::Add(const char* name, NetworkDataType type, NetworkCompressionType compression,
                              const RefNetworkData& defValue, const char* description, NetworkMessageErrorType errType,
                              float errCoef)
{
    // add to items
    int index = _items.Add();
    NetworkMessageFormatItem& item = _items[index];
    item.name = name;
    item.type = type;
    item.compression = compression;
    item.defValue = defValue;

    // add to errors
    DoVerify(_errors.Add() == index);
    NetworkMessageErrorInfo& info = _errors[index];
    info.type = errType;
    info.coef = errCoef;

    // add to descriptions
#if DOCUMENT_MSG_FORMATS
    DoVerify(_descriptions.Add() == index);
    _descriptions[index] = description;
#endif

    return index;
}

int NetworkMessageFormat::Add(const char* name, NetworkDataType type, NetworkCompressionType compression, int defValue,
                              const char* description, NetworkMessageErrorType errType, float errCoef)
{
    NET_ERROR(defValue == 0);
    // add to items
    int index = _items.Add();
    NetworkMessageFormatItem& item = _items[index];
    item.name = name;
    item.type = type;
    item.compression = compression;
    item.defValue = RefNetworkDataNull();

    // add to errors
    DoVerify(_errors.Add() == index);
    NetworkMessageErrorInfo& info = _errors[index];
    info.type = errType;
    info.coef = errCoef;

    // add to descriptions
#if DOCUMENT_MSG_FORMATS
    DoVerify(_descriptions.Add() == index);
    _descriptions[index] = description;
#endif

    return index;
}

NetworkMessageFormat* NetworkMessageFormat::Init(NetworkMessageIndices* indices)
{
    base::Init(indices);

    return this;
}

void NetworkMessageFormat::Clear()
{
    base::Clear();
    _errors.Clear();
}

// val1.GetVal is called to check if we know type
// if not, dynamic_cast will PoseidonAssert

#ifdef NDEBUG
#define CHECK_TYPE(a)
#else
#define CHECK_TYPE(a) (a->GetVal())
#endif

// common case - used for scalar types
template <class Type>
float RefNetworkDataTyped<Type>::CalculateError(NetworkMessageErrorType type, const RefNetworkDataTyped<Type>& with,
                                                NetworkMessageFormatItem& item, float dt) const
{
    const Type& value = GetVal();
    const Type& withValue = with.GetVal();

    switch (type)
    {
        case ET_NOT_EQUAL:
            return value != withValue ? 1 : 0;
        case ET_ABS_DIF:
            return fabs(value - withValue);
        case ET_SQUARE_DIF:
            return Square(value - withValue);
        case ET_DER_DIF:
            return dt * fabs(value - withValue);
        default:
            Fail("Unexpected error type");
            return 0;
    }
}

template <>
float RefNetworkDataTyped<RString>::CalculateError(NetworkMessageErrorType type,
                                                   const RefNetworkDataTyped<RString>& with,
                                                   NetworkMessageFormatItem& item, float dt) const
{
    switch (type)
    {
        case ET_NOT_EQUAL:
            return stricmp(GetVal(), with.GetVal()) != 0 ? 1 : 0;
        default:
            Fail("Unexpected error type");
            return 0;
    }
}

template <>
float RefNetworkDataTyped<Vector3>::CalculateError(NetworkMessageErrorType type,
                                                   const RefNetworkDataTyped<Vector3>& with,
                                                   NetworkMessageFormatItem& item, float dt) const
{
    Vector3Val value = GetVal();
    Vector3Val withValue = with.GetVal();
    switch (type)
    {
        case ET_NOT_EQUAL:
            return value != withValue ? 1 : 0;
        case ET_SQUARE_DIF:
            return value.Distance2(withValue);
        case ET_ABS_DIF:
            return value.Distance(withValue);
        case ET_DER_DIF:
            return dt * value.Distance(withValue);
        default:
            Fail("Unexpected error type");
            return 0;
    }
}

template <>
float RefNetworkDataTyped<Matrix3>::CalculateError(NetworkMessageErrorType type,
                                                   const RefNetworkDataTyped<Matrix3>& with,
                                                   NetworkMessageFormatItem& item, float dt) const
{
    Matrix3Val value = GetVal();
    Matrix3Val withValue = with.GetVal();
    switch (type)
    {
        case ET_SQUARE_DIF:
        {
            float err;
            err = value.DirectionAside().Distance2(withValue.DirectionAside());
            err += value.DirectionUp().Distance2(withValue.DirectionUp());
            err += value.Direction().Distance2(withValue.Direction());
            return err;
        }
        case ET_ABS_DIF:
        {
            float err;
            err = value.DirectionAside().Distance(withValue.DirectionAside());
            err += value.DirectionUp().Distance(withValue.DirectionUp());
            err += value.Direction().Distance(withValue.Direction());
            return err;
        }
        case ET_DER_DIF:
        {
            float err;
            err = value.DirectionAside().Distance(withValue.DirectionAside());
            err += value.DirectionUp().Distance(withValue.DirectionUp());
            err += value.Direction().Distance(withValue.Direction());
            return dt * err;
        }
        default:
            Fail("Unexpected error type");
            return 0;
    }
}

// specialized constructor
template <>
NetworkDataTyped<AutoArray<char>>::NetworkDataTyped(void* val, int size) : value((char*)val, size)
{
}

const int MaxAbsInt16 = 0x4000;
// we want to have 0.5 represented exactly
// we also want to keep some reserve range in case small overflow will occur

inline int EncodeFloat16(float x)
{
    constexpr float networkOrientationTolerance = 1e-3f;
    if (x < -1.0f || x > 1.0f)
    {
        NET_ERROR(std::isfinite(x));
        NET_ERROR(x >= -1.0f - networkOrientationTolerance);
        NET_ERROR(x <= +1.0f + networkOrientationTolerance);
        saturate(x, -1.0f, +1.0f);
    }
    int r = toInt(MaxAbsInt16 * x);
    saturate(r, -MaxAbsInt16, +MaxAbsInt16);
    return r;
}

__forceinline float DecodeFloat16(short x)
{
    const float coef = 1.0 / MaxAbsInt16;
    return x * coef;
}

void EncodedMatrix3::Encode(const Matrix3& m)
{
    // encoding is quite simple
    _01c = EncodeFloat16(m(0, 1));
    _11c = EncodeFloat16(m(1, 1));
    // note: abs. value can be deduced from previous value, sign not
    _21sign = m(2, 1) >= 0 ? 1 : -1;
    _02c = EncodeFloat16(m(0, 2));
    _12c = EncodeFloat16(m(1, 2));
    _22sign = m(2, 2) >= 0 ? 1 : -1;
// verify matrix is orthogonal
#if _DEBUG
    float m21_sq = 1 - m(0, 1) * m(0, 1) - m(1, 1) * m(1, 1);
    float m21 = (m21_sq >= 0 ? sqrt(m21_sq) : 0) * _21sign;
    NET_ERROR(fabs(m21 - m(2, 1)) < 1e-3);
    float m22_sq = 1 - m(0, 2) * m(0, 2) - m(1, 2) * m(1, 2);
    float m22 = (m22_sq >= 0 ? sqrt(m22_sq) : 0) * _22sign;
    NET_ERROR(fabs(m22 - m(2, 2)) < 1e-3);
    Vector3 mAside = m.DirectionUp().CrossProduct(m.Direction());
    NET_ERROR(mAside.Distance2(m.DirectionAside()) < 1e-6);
#endif
}

void EncodedMatrix3::Decode(Matrix3& m) const
{
    // decoding is a little bit more tricky
    // decode col. 1 (up)
    Vector3 up;
    up.Init();
    up[0] = DecodeFloat16(_01c);
    up[1] = DecodeFloat16(_11c);
    float m21_sq = 1 - up[0] * up[0] - up[1] * up[1];
    up[2] = (m21_sq >= 0 ? sqrt(m21_sq) : 0) * _21sign;

    // decode col. 0 (direction)
    Vector3 dir;
    dir.Init();
    dir[0] = DecodeFloat16(_02c);
    dir[1] = DecodeFloat16(_12c);
    float m22_sq = 1 - dir[0] * dir[0] - dir[1] * dir[1];
    dir[2] = (m22_sq >= 0 ? sqrt(m22_sq) : 0) * _22sign;

    m.SetDirectionAndUp(dir, up);
}

void EncodedMatrix3::Decode(Matrix4& m) const
{
    // decoding is a little bit more tricky
    // decode col. 1 (up)
    Vector3 up;
    up.Init();
    up[0] = DecodeFloat16(_01c);
    up[1] = DecodeFloat16(_11c);
    float m21_sq = 1 - up[0] * up[0] - up[1] * up[1];
    up[2] = (m21_sq >= 0 ? sqrt(m21_sq) : 0) * _21sign;

    // decode col. 0 (direction)
    Vector3 dir;
    dir.Init();
    dir[0] = DecodeFloat16(_02c);
    dir[1] = DecodeFloat16(_12c);
    float m22_sq = 1 - dir[0] * dir[0] - dir[1] * dir[1];
    dir[2] = (m22_sq >= 0 ? sqrt(m22_sq) : 0) * _22sign;

    m.SetDirectionAndUp(dir, up);
}

Vector3 EncodedMatrix3::DirectionUp() const
{
    float m01 = DecodeFloat16(_01c);
    float m11 = DecodeFloat16(_11c);

    float m21_sq = 1 - m01 * m01 - m11 * m11;
    float m21 = (m21_sq >= 0 ? sqrt(m21_sq) : 0) * _21sign;
    return Vector3(m01, m11, m21);
}

Vector3 EncodedMatrix3::Direction() const
{
    float m02 = DecodeFloat16(_02c);
    float m12 = DecodeFloat16(_12c);
    float m22_sq = 1 - m02 * m02 - m12 * m12;
    float m22 = (m22_sq >= 0 ? sqrt(m22_sq) : 0) * _22sign;
    return Vector3(m02, m12, m22);
}

template <>
float RefNetworkDataTyped<AutoArray<char>>::CalculateError(NetworkMessageErrorType type,
                                                           const RefNetworkDataTyped<AutoArray<char>>& with,
                                                           NetworkMessageFormatItem& item, float dt) const
{
    const AutoArray<char>& value = GetVal();
    const AutoArray<char>& withValue = with.GetVal();

    switch (type)
    {
        case ET_UPD_ENTITY_POS:
        {
            // check data size
            if (value.Size() != sizeof(NetworkUpdEntityPos))
            {
                return 0;
            }
            if (withValue.Size() != sizeof(NetworkUpdEntityPos))
            {
                return 0;
            }
            const NetworkUpdEntityPos& d1 = *(NetworkUpdEntityPos*)value.Data();
            const NetworkUpdEntityPos& d2 = *(NetworkUpdEntityPos*)withValue.Data();
            float err;
            err = d1.position.Distance(d2.position);

            err += d1.orientation.DirectionUp().Distance(d2.orientation.DirectionUp());
            err += d1.orientation.Direction().Distance(d2.orientation.Direction());

            err += dt * d1.speed.Distance(d2.speed);
            err += dt * d1.angMomentum.Distance(d2.angMomentum);

            return err;
        }
        case ET_UPD_MAN_POS:
        {
            if (value.Size() != sizeof(NetworkUpdManPos))
            {
                return 0;
            }
            if (withValue.Size() != sizeof(NetworkUpdManPos))
            {
                return 0;
            }
            const NetworkUpdManPos& d1 = *(NetworkUpdManPos*)value.Data();
            const NetworkUpdManPos& d2 = *(NetworkUpdManPos*)withValue.Data();
            float err;
            err = fabs(CompareRot8b(d1.gunXRotWantedC, d2.gunXRotWantedC)) * ERR_COEF_VALUE_MINOR;
            err += fabs(CompareRot8b(d1.gunYRotWantedC, d2.gunYRotWantedC)) * ERR_COEF_VALUE_MINOR;
            err += fabs(CompareRot8b(d1.headXRotWantedC, d2.headXRotWantedC)) * ERR_COEF_VALUE_MINOR;
            err += fabs(CompareRot8b(d1.headYRotWantedC, d2.headYRotWantedC)) * ERR_COEF_VALUE_MINOR;
            return err;
        }
        default:
            Fail("Unexpected error type");
            return 0;
    }
}

template <>
float RefNetworkDataTyped<AutoArray<int>>::CalculateError(NetworkMessageErrorType type,
                                                          const RefNetworkDataTyped<AutoArray<int>>& with,
                                                          NetworkMessageFormatItem& item, float dt) const
{
    const AutoArray<int>& value = GetVal();
    const AutoArray<int>& withValue = with.GetVal();

    switch (type)
    {
        case ET_NOT_EQUAL:
        {
            if (value.Size() != withValue.Size())
            {
                return 1;
            }
            for (int i = 0; i < value.Size(); i++)
            {
                if (value[i] != withValue[i])
                {
                    return 1;
                }
            }
            return 0;
        }
        default:
            Fail("Unexpected error type");
            return 0;
    }
}

template <>
float RefNetworkDataTyped<AutoArray<float>>::CalculateError(NetworkMessageErrorType type,
                                                            const RefNetworkDataTyped<AutoArray<float>>& with,
                                                            NetworkMessageFormatItem& item, float dt) const
{
    const AutoArray<float>& value = GetVal();
    const AutoArray<float>& withValue = with.GetVal();

    switch (type)
    {
        case ET_NOT_EQUAL:
        {
            if (value.Size() != withValue.Size())
            {
                return 1;
            }
            for (int i = 0; i < value.Size(); i++)
            {
                if (value[i] != withValue[i])
                {
                    return 1;
                }
            }
            return 0;
        }
        case ET_ABS_DIF:
        {
            int minSize = std::min(value.Size(), withValue.Size());
            float error = value.Size() - minSize + withValue.Size() - minSize;
            for (int i = 0; i < minSize; i++)
            {
                error += fabs(value[i] - withValue[i]);
            }
            return error;
        }
        default:
            Fail("Unexpected error type");
            return 0;
    }
}

template <>
float RefNetworkDataTyped<AutoArray<RString>>::CalculateError(NetworkMessageErrorType type,
                                                              const RefNetworkDataTyped<AutoArray<RString>>& with,
                                                              NetworkMessageFormatItem& item, float dt) const
{
    const AutoArray<RString>& value = GetVal();
    const AutoArray<RString>& withValue = with.GetVal();

    switch (type)
    {
        case ET_NOT_EQUAL:
        {
            if (value.Size() != withValue.Size())
            {
                return 1;
            }
            for (int i = 0; i < value.Size(); i++)
            {
                if (value[i] != withValue[i])
                {
                    return 1;
                }
            }
            return 0;
        }
        default:
            Fail("Unexpected error type");
            return 0;
    }
}

template <>
float RefNetworkDataTyped<RadioSentence>::CalculateError(NetworkMessageErrorType type,
                                                         const RefNetworkDataTyped<RadioSentence>& with,
                                                         NetworkMessageFormatItem& item, float dt) const
{
    Fail("Unexpected error type");
    return 0;
}

template <>
float RefNetworkDataTyped<NetworkMessage>::CalculateError(NetworkMessageErrorType type,
                                                          const RefNetworkDataTyped<NetworkMessage>& with,
                                                          NetworkMessageFormatItem& item, float dt) const
{
    // message format
    CHECK_ASSIGN(formatType, item.defValue, const RefNetworkDataTyped<int>);
    NetworkMessageFormat* format = GMsgFormats[formatType.GetVal()];

    NetworkMessage& msg1 = GetVal();
    NetworkMessage& msg2 = with.GetVal();

    float errValue = 0;
    for (int i = 0; i < format->NItems(); i++)
    {
        const NetworkMessageErrorInfo& info = format->GetErrorInfo(i);
        if (info.type == ET_NONE)
        {
            continue;
        }

        const RefNetworkData& value1 = msg1.values[i];
        const RefNetworkData& value2 = msg2.values[i];
        errValue += info.coef * value1.CalculateError(info.type, value2, format->GetItem(i), dt);
    }

    return errValue;
}

NetworkMessageFormat& MagazineNetworkInfo::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("type", NDTString, NCTDefault, DEFVALUE(RString, ""), DOC_MSG("Magazine type"));
    format.Add("ammo", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Ammo count"));
    format.Add("burstLeft", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0),
               DOC_MSG("How many shots are there in the burst (auto fired)"));
    format.Add("reload", NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Time rest to reload shot"));
    format.Add("reloadMagazine", NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Time rest to reload magazine"));
    format.Add("creator", NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("Network ID of magazine"));
    format.Add("id", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Network ID of magazine"));
    return format;
}

TMError MagazineNetworkInfo::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesMagazine*>(ctx.GetIndices()))
    const IndicesMagazine* indices = static_cast<const IndicesMagazine*>(ctx.GetIndices());

    ITRANSF(type)
    ITRANSF(ammo)
    ITRANSF(burstLeft)
    ITRANSF(reload)
    ITRANSF(reloadMagazine)
    ITRANSF(creator)
    ITRANSF(id)
    return TMOK;
}

TMError MagazineNetworkInfo::TransferMsgSimple(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesMagazine*>(ctx.GetIndices()))
    const IndicesMagazine* indices = static_cast<const IndicesMagazine*>(ctx.GetIndices());

    ITRANSF(ammo)
    ITRANSF(creator)
    ITRANSF(id)

    return TMOK;
}

template <>
float RefNetworkDataTyped<AutoArray<NetworkMessage>>::CalculateError(
    NetworkMessageErrorType type, const RefNetworkDataTyped<AutoArray<NetworkMessage>>& with,
    NetworkMessageFormatItem& item, float dt) const
{
    // non-const value required here, because NetworkMessageContext requires
    // NetworkMessage *, but messages are actually not changed.
    AutoArray<NetworkMessage>& value = GetVal();
    AutoArray<NetworkMessage>& withValue = with.GetVal();

    switch (type)
    {
        case ET_MAGAZINES:
        {
            // magazines
            // RefArray<Magazine> magazines1, magazines2;
            // AutoArray<MagazineNetworkInfo> magazines1, magazines2;
            AUTO_STATIC_ARRAY(MagazineNetworkInfo, magazines1, 32);
            AUTO_STATIC_ARRAY(MagazineNetworkInfo, magazines2, 32);

            // messages format
            NetworkComponent* component = GNetworkManager.GetServer();
            const RefNetworkData& defVal = item.defValue;
            CHECK_ASSIGN(defValTyped, defVal, const RefNetworkDataTyped<int>)
            NetworkMessageFormatBase* formatItem = component->GetFormat((NetworkMessageType)defValTyped.GetVal());

            // transfer 1
            int n = value.Size();
            magazines1.Resize(n);
            for (int i = 0; i < n; i++)
            {
                NetworkMessageContext ctx(&value[i], formatItem, component, 0, false);
                if (magazines1[i].TransferMsgSimple(ctx) != TMOK)
                {
                    return 0;
                }
            }
            // transfer 2
            n = withValue.Size();
            magazines2.Resize(n);
            for (int i = 0; i < n; i++)
            {
                NetworkMessageContext ctx(&withValue[i], formatItem, component, 0, false);
                if (magazines2[i].TransferMsgSimple(ctx) != TMOK)
                {
                    return 0;
                }
            }

            bool changed = magazines1.Size() != magazines2.Size();
            int ammoDiff = 0;
            if (!changed)
            {
                for (int i = 0; i < magazines1.Size(); i++)
                {
                    if (magazines1[i]._creator != magazines2[i]._creator || magazines1[i]._id != magazines2[i]._id)
                    {
                        changed = true;
                        break;
                    }
                    else
                    {
                        ammoDiff += abs(magazines1[i]._ammo - magazines2[i]._ammo);
                    }
                }
            }
            if (changed)
            {
                return ERR_COEF_MODE;
            }
            else
            {
                return ammoDiff * ERR_COEF_VALUE_MINOR;
            }
        }
        default:
            Fail("Unexpected error type");
            return 0;
    }
}

template <>
float RefNetworkDataTyped<NetworkId>::CalculateError(NetworkMessageErrorType type,
                                                     const RefNetworkDataTyped<NetworkId>& with,
                                                     NetworkMessageFormatItem& item, float dt) const
{
    NetworkId value = GetVal();
    NetworkId withValue = with.GetVal();

    switch (type)
    {
        case ET_NOT_EQUAL:
            return value != withValue ? 1 : 0;
        default:
            Fail("Unexpected error type");
            return 0;
    }
}

template <>
float RefNetworkDataTyped<AutoArray<NetworkId>>::CalculateError(NetworkMessageErrorType type,
                                                                const RefNetworkDataTyped<AutoArray<NetworkId>>& with,
                                                                NetworkMessageFormatItem& item, float dt) const
{
    const AutoArray<NetworkId>& value = GetVal();
    const AutoArray<NetworkId>& withValue = with.GetVal();

    switch (type)
    {
        case ET_NOT_EQUAL_COUNT:
        {
            NET_ERROR(value.Size() == withValue.Size());
            int count = std::min(value.Size(), withValue.Size());
            int dif = 0;
            for (int i = 0; i < count; i++)
            {
                if (value[i] != withValue[i])
                {
                    dif++;
                }
            }
            return dif;
        }
        case ET_NOT_CONTAIN_COUNT:
        {
            int dif = 0;
            for (int i = 0; i < withValue.Size(); i++)
            {
                NetworkId ref2 = withValue[i];
                if (ref2.IsNull())
                {
                    continue;
                }
                bool found = false;
                for (int j = 0; j < value.Size(); j++)
                {
                    if (value[j] == ref2)
                    {
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    dif++;
                }
            }
            return dif;
        }
        default:
            Fail("Unexpected error type");
            return 0;
    }
}

float RefNetworkData::CalculateError(NetworkMessageErrorType type, const RefNetworkData& value2,
                                     NetworkMessageFormatItem& item, float dt) const
{
    switch (item.type)
    {
#define ERR_CASE(caseType)                                                                                      \
    {                                                                                                           \
        const RefNetworkDataTyped<caseType>* val1 = static_cast<const RefNetworkDataTyped<caseType>*>(this);    \
        const RefNetworkDataTyped<caseType>* val2 = static_cast<const RefNetworkDataTyped<caseType>*>(&value2); \
        CHECK_TYPE(val1);                                                                                       \
        CHECK_TYPE(val2);                                                                                       \
        return val1->CalculateError(type, *val2, item, dt);                                                     \
    }
        case NDTBool:
            ERR_CASE(bool)
        case NDTInteger:
            ERR_CASE(int)
        case NDTFloat:
            ERR_CASE(float)
        case NDTString:
            ERR_CASE(RString)
        case NDTRawData:
            ERR_CASE(AutoArray<char>)
        case NDTTime:
            ERR_CASE(Time)
        case NDTVector:
            ERR_CASE(Vector3)
        case NDTMatrix:
            ERR_CASE(Matrix3)
        case NDTIntArray:
            ERR_CASE(AutoArray<int>)
        case NDTFloatArray:
            ERR_CASE(AutoArray<float>)
        case NDTStringArray:
            ERR_CASE(AutoArray<RString>)
        case NDTSentence:
            ERR_CASE(RadioSentence)
        case NDTObject:
            ERR_CASE(NetworkMessage)
        case NDTObjectArray:
            ERR_CASE(AutoArray<NetworkMessage>)
        case NDTRef:
            ERR_CASE(NetworkId)
        case NDTRefArray:
            ERR_CASE(AutoArray<NetworkId>)
// case NDTData: ERR_CASE(xxx)
#undef ERR_CASE
        default:
            Fail("Error calculation not implemented");
            return 0;
    }
}

NetworkDataWithFormat::NetworkDataWithFormat(NetworkDataType type, NetworkCompressionType compression,
                                             const RefNetworkData& data)
{
    format.name = "data";
    format.type = type;
    format.compression = compression;
    format.defValue = data;
}

float RefNetworkDataWithFormat::CalculateError(NetworkMessageErrorType type, const RefNetworkDataWithFormat& with,
                                               NetworkMessageFormatItem& item, float dt) const
{
    Fail("Not implemented");
    return 0;
}

int RefNetworkDataWithFormat::CalculateSize(NetworkCompressionType compression,
                                            const NetworkMessageFormatItem& item) const
{
    Fail("Not implemented");
    return 0;
}

// size calculation - simple types
// Calculate size of serialized and compressed value
template <class Type>
int CalculateValueSize(const Type& value, NetworkCompressionType compression)
{
    switch (compression)
    {
        case NCTNone:
        case NCTDefault:
            return sizeof(value);
        default:
            LOG_ERROR(Network, "Unsupported compression method {}", (int)compression);
            return 0;
    }
}

template <>
int CalculateValueSize(const int& value, NetworkCompressionType compression)
{
    switch (compression)
    {
        case NCTNone:
            return sizeof(int);
        case NCTSmallUnsigned:
        {
            int size = 0;
            unsigned int val = value;
            for (;;)
            {
                val >>= 7;
                size++;
                if (!val)
                {
                    return size;
                }
            }
        }
        case NCTSmallSigned:
        {
            int size = 0;
            unsigned int val;
            if (value >= 0)
            {
                val = value << 1;
            }
            else
            {
                val = (-value << 1) | 1;
            }
            for (;;)
            {
                val >>= 7;
                size++;
                if (!val)
                {
                    return size;
                }
            }
        }
        default:
            RptF("Unsupported compression method %d", compression);
            return 0;
    }
}

template <>
int CalculateValueSize(const Vector3& value, NetworkCompressionType compression)
{
    switch (compression)
    {
        case NCTDefault:
        case NCTNone:
            return 3 * sizeof(float);
        default:
            RptF("Unsupported compression method %d", compression);
            return 0;
    }
}

template <>
int CalculateValueSize(const Matrix3& value, NetworkCompressionType compression)
{
    switch (compression)
    {
        case NCTNone:
        case NCTDefault:
            return 9 * sizeof(float);
        case NCTMatrixOrientation:
            return sizeof(EncodedMatrix3);
        default:
            RptF("Unsupported compression method %d", compression);
            return 0;
    }
}

template <>
int CalculateValueSize(const RString& value, NetworkCompressionType compression)
{
    switch (compression)
    {
        case NCTNone:
            return value.GetLength() + 1;
            break;
        case NCTStringGeneric: // NCTDefault
        {
            int id = GetNetIdStrings().GetId(value);
            // 0 will be transmitted to indicate no ID
            // therefore we add 1 to offset zero to 1
            // -1 is not used, so that unsigned compression can be used
            if (id >= 0)
            {
                return CalculateValueSize(id + 1, NCTSmallUnsigned);
            }
            return value.GetLength() + 2;
        }
        case NCTStringMove:
        {
            int id = GetNetIdMoves().GetId(value);
            // 0 will be transmitted to indicate no ID
            // therefore we add 1 to offset zero to 1
            // -1 is not used, so that unsigned compression can be used
            if (id >= 0)
            {
                return CalculateValueSize(id + 1, NCTSmallUnsigned);
            }
            return value.GetLength() + 2;
        }
        default:
            RptF("Unsupported compression method %d", compression);
            return 0;
    }
}

template <>
int CalculateValueSize(const NetworkId& value, NetworkCompressionType compression)
{
    switch (compression)
    {
        case NCTNone:
        case NCTDefault:
            return 2 * sizeof(int);
        default:
            RptF("Unsupported compression method %d", compression);
            return 0;
    }
}

#define FLOAT_COMPRESSION 1

template <>
int CalculateValueSize(const float& value, NetworkCompressionType compression)
{
    switch (compression)
    {
        float min, max, invRange;
#define RANGE(a, b) min = (a), max = (b), invRange = 1.0f / ((b) - (a))
        case NCTFloatMostly0To1:
            RANGE(0, 1);
            goto CompressionUnlimited;
        case NCTFloatAngle:
            RANGE(-H_PI, +H_PI);
            goto CompressionUnlimited;
        case NCTFloat0To2:
        case NCTFloat0To1:
        case NCTFloatM1ToP1:
#if FLOAT_COMPRESSION
            return 1;
#endif

        CompressionUnlimited:
#if FLOAT_COMPRESSION
        {
            float compressed = (value - min) * invRange;
            int iValue = toInt(compressed * 254 + 127);
            return CalculateValueSize(iValue, NCTSmallSigned);
        }
#endif
        case NCTNone:
        case NCTDefault:
            return sizeof(value);
        default:
            LOG_ERROR(Network, "Unsupported compression method {}", (int)compression);
            return 0;
#undef RANGE
    }
}

// size calculation - compound types
template <class Type>
int RefNetworkDataTyped<Type>::CalculateSize(NetworkCompressionType compression,
                                             const NetworkMessageFormatItem& item) const
{
    const Type& value = GetVal();
    return CalculateValueSize(value, compression);
}

template <>
int RefNetworkDataTyped<Time>::CalculateSize(NetworkCompressionType compression,
                                             const NetworkMessageFormatItem& item) const
{
    switch (compression)
    {
        case NCTNone:
        case NCTDefault:
            return sizeof(int);
        default:
            LOG_ERROR(Network, "Unsupported compression method {}", (int)compression);
            return 0;
    }
}

template <>
int RefNetworkDataTyped<AutoArray<char>>::CalculateSize(NetworkCompressionType compression,
                                                        const NetworkMessageFormatItem& item) const
{
    const AutoArray<char>& value = GetVal();
    int size = 0;
    int m = value.Size();
    size += CalculateValueSize(m, NCTSmallUnsigned);
    for (int j = 0; j < m; j++)
    {
        size += CalculateValueSize(value[j], compression);
    }
    return size;
}

template <>
int RefNetworkDataTyped<AutoArray<int>>::CalculateSize(NetworkCompressionType compression,
                                                       const NetworkMessageFormatItem& item) const
{
    const AutoArray<int>& value = GetVal();
    int size = 0;
    int m = value.Size();
    size += CalculateValueSize(m, NCTSmallUnsigned);
    for (int j = 0; j < m; j++)
    {
        size += CalculateValueSize(value[j], compression);
    }
    return size;
}

template <>
int RefNetworkDataTyped<AutoArray<float>>::CalculateSize(NetworkCompressionType compression,
                                                         const NetworkMessageFormatItem& item) const
{
    const AutoArray<float>& value = GetVal();
    int size = 0;
    int m = value.Size();
    size += CalculateValueSize(m, NCTSmallUnsigned);
    for (int j = 0; j < m; j++)
    {
        size += CalculateValueSize(value[j], compression);
    }
    return size;
}

template <>
int RefNetworkDataTyped<AutoArray<RString>>::CalculateSize(NetworkCompressionType compression,
                                                           const NetworkMessageFormatItem& item) const
{
    const AutoArray<RString>& value = GetVal();
    int size = 0;
    int m = value.Size();
    size += CalculateValueSize(m, NCTSmallUnsigned);
    for (int j = 0; j < m; j++)
    {
        size += CalculateValueSize(value[j], compression);
    }
    return size;
}

template <>
int RefNetworkDataTyped<AutoArray<NetworkId>>::CalculateSize(NetworkCompressionType compression,
                                                             const NetworkMessageFormatItem& item) const
{
    const AutoArray<NetworkId>& value = GetVal();
    int size = 0;
    int m = value.Size();
    size += CalculateValueSize(m, NCTSmallUnsigned);
    for (int j = 0; j < m; j++)
    {
        size += CalculateValueSize(value[j], compression);
    }
    return size;
}

template <>
int RefNetworkDataTyped<RadioSentence>::CalculateSize(NetworkCompressionType compression,
                                                      const NetworkMessageFormatItem& item) const
{
    const RadioSentence& value = GetVal();
    int size = 0;
    int m = value.Size();
    size += CalculateValueSize(m, NCTSmallUnsigned);
    for (int j = 0; j < m; j++)
    {
        size += CalculateValueSize(value[j].id, compression);
        size += CalculateValueSize(value[j].pauseAfter, compression);
    }
    return size;
}

int RefNetworkData::CalculateSize(NetworkCompressionType compression, const NetworkMessageFormatItem& item) const
{
    switch (item.type)
    {
#define ERR_CASE(caseType)                                                                                   \
    {                                                                                                        \
        const RefNetworkDataTyped<caseType>* val1 = static_cast<const RefNetworkDataTyped<caseType>*>(this); \
        CHECK_TYPE(val1);                                                                                    \
        return val1->CalculateSize(compression, item);                                                       \
    }
        case NDTBool:
            ERR_CASE(bool)
        case NDTInteger:
            ERR_CASE(int)
        case NDTFloat:
            ERR_CASE(float)
        case NDTString:
            ERR_CASE(RString)
        case NDTRawData:
            ERR_CASE(AutoArray<char>)
        case NDTTime:
            ERR_CASE(Time)
        case NDTVector:
            ERR_CASE(Vector3)
        case NDTMatrix:
            ERR_CASE(Matrix3)
        case NDTIntArray:
            ERR_CASE(AutoArray<int>)
        case NDTFloatArray:
            ERR_CASE(AutoArray<float>)
        case NDTStringArray:
            ERR_CASE(AutoArray<RString>)
        case NDTSentence:
            ERR_CASE(RadioSentence)
        case NDTObject:
            ERR_CASE(NetworkMessage)
        case NDTObjectArray:
            ERR_CASE(AutoArray<NetworkMessage>)
        case NDTRef:
            ERR_CASE(NetworkId)
        case NDTRefArray:
            ERR_CASE(AutoArray<NetworkId>)
// case NDTData: ERR_CASE(xxx)
#undef ERR_CASE
        default:
            Fail("Size calculation not implemented");
            return 0;
    }
}
