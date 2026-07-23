#include <Poseidon/Foundation/Algorithms/Crc.hpp>

using namespace Poseidon;
#include <algorithm>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Network/NetworkImpl.hpp>
#include <Poseidon/Network/WireBounds.hpp>
#include <Poseidon/Dev/Debug/DebugTrap.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Memory/FastAlloc.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/Memtype.h>
#include <Poseidon/Foundation/Types/Pointers.hpp>

using namespace Poseidon::Dev;
using Poseidon::Foundation::Time;
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Core/IdString.hpp>

#include <Poseidon/Foundation/Algorithms/Qsort.hpp>

#include <Poseidon/Foundation/Algorithms/Crc.hpp>
#include <Poseidon/AI/AI.hpp>

#ifdef NDEBUG
#define CHECK_TYPE(a)
#else
#define CHECK_TYPE(a) (a->GetVal())
#endif

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

// Generic template definition — used for types without explicit specialisation (e.g. Time).
// Explicit specialisations for int/float/Vector3/etc. live in NetworkMsg.cpp.
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
int CalculateValueSize(const int& value, NetworkCompressionType compression);
template <>
int CalculateValueSize(const float& value, NetworkCompressionType compression);
template <>
int CalculateValueSize(const Vector3& value, NetworkCompressionType compression);
template <>
int CalculateValueSize(const Matrix3& value, NetworkCompressionType compression);
template <>
int CalculateValueSize(const RString& value, NetworkCompressionType compression);
template <>
int CalculateValueSize(const NetworkId& value, NetworkCompressionType compression);

// Message context

void NetworkMessageContext::PrepareMessage()
{
    NET_ERROR(_format) int n = _format->NItems();
    _msg->values.Resize(n);
    for (int i = 0; i < n; i++)
    {
        const NetworkMessageFormatItem& item = _format->GetItem(i);
        if (item.type == NDTObject)
        {
            _msg->values[i] = RefNetworkDataTyped<NetworkMessage>();
        }
        else if (item.type == NDTObjectArray)
        {
            _msg->values[i] = RefNetworkDataTyped<AutoArray<NetworkMessage>>();
        }
        else
        {
            _msg->values[i] = item.defValue;
        }
    }
}

void NetworkMessageContext::LogMessage(int level, RString indent) const
{
    DiagLogF("%sid (time = %.3f)", (const char*)indent, _msg->time.toFloat());
    if (level < 3)
    {
        return;
    }
    int n = _format->NItems();
    for (int i = 0; i < n; i++)
    {
        const NetworkMessageFormatItem& item = _format->GetItem(i);
        const RefNetworkData& val = _msg->values[i];
        switch (item.type)
        {
            case NDTBool:
            {
                CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<bool>)
                DiagLogF("%s%s = %s", (const char*)indent, (const char*)item.name,
                         valTyped.GetVal() ? "true" : "false");
            }
            break;
            case NDTInteger:
            {
                CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<int>)
                DiagLogF("%s%s = %d", (const char*)indent, (const char*)item.name, valTyped.GetVal());
            }
            break;
            case NDTFloat:
            {
                CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<float>)
                DiagLogF("%s%s = %f", (const char*)indent, (const char*)item.name, valTyped.GetVal());
            }
            break;
            case NDTString:
            {
                CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<RString>)
                DiagLogF("%s%s = %s", (const char*)indent, (const char*)item.name, (const char*)valTyped.GetVal());
            }
            break;
            case NDTRawData:
            {
                CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<AutoArray<char>>)
                DiagLogF("%s%s = %x, size %d", (const char*)indent, (const char*)item.name, valTyped.GetVal().Data(),
                         valTyped.GetVal().Size());
            }
            break;
            case NDTTime:
            {
                CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<Time>)
                DiagLogF("%s%s = %.3f", (const char*)indent, (const char*)item.name, valTyped.GetVal().toFloat());
            }
            break;
            case NDTVector:
            {
                CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<Vector3>)
                DiagLogF("%s%s = {%f, %f, %f}", (const char*)indent, (const char*)item.name, valTyped.GetVal().X(),
                         valTyped.GetVal().Y(), valTyped.GetVal().Z());
            }
            break;
            case NDTMatrix:
            {
                CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<Matrix3>)
                DiagLogF("%s%s = ", (const char*)indent, (const char*)item.name);
                DiagLogF("%s\t%f, %f, %f", (const char*)indent, valTyped.GetVal()(0, 0), valTyped.GetVal()(0, 1),
                         valTyped.GetVal()(0, 2));
                DiagLogF("%s\t%f, %f, %f", (const char*)indent, valTyped.GetVal()(1, 0), valTyped.GetVal()(1, 1),
                         valTyped.GetVal()(1, 2));
                DiagLogF("%s\t%f, %f, %f", (const char*)indent, valTyped.GetVal()(2, 0), valTyped.GetVal()(2, 1),
                         valTyped.GetVal()(2, 2));
            }
            break;
            case NDTIntArray:
            {
                CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<AutoArray<int>>)
                DiagLogF("%s%s:", (const char*)indent, (const char*)item.name);
                const AutoArray<int>& array = valTyped.GetVal();
                int m = array.Size();
                for (int j = 0; j < m; j++)
                {
                    DiagLogF("%s\tItem %d = %d", (const char*)indent, j, array[j]);
                }
            }
            break;
            case NDTFloatArray:
            {
                CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<AutoArray<float>>)
                DiagLogF("%s%s:", (const char*)indent, (const char*)item.name);
                const AutoArray<float>& array = valTyped.GetVal();
                int m = array.Size();
                for (int j = 0; j < m; j++)
                {
                    DiagLogF("%s\tItem %d = %f", (const char*)indent, j, array[j]);
                }
            }
            break;
            case NDTStringArray:
            {
                CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<AutoArray<RString>>)
                DiagLogF("%s%s:", (const char*)indent, (const char*)item.name);
                const AutoArray<RString>& array = valTyped.GetVal();
                int m = array.Size();
                for (int j = 0; j < m; j++)
                {
                    DiagLogF("%s\tItem %d = %s", (const char*)indent, j, (const char*)array[j]);
                }
            }
            break;
            case NDTSentence:
            {
                CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<RadioSentence>)
                DiagLogF("%s%s:", (const char*)indent, (const char*)item.name);
                const RadioSentence& array = valTyped.GetVal();
                int m = array.Size();
                for (int j = 0; j < m; j++)
                {
                    DiagLogF("%s\tItem %d = %s, %.3f", (const char*)indent, j, (const char*)array[j].id,
                             array[j].pauseAfter);
                }
            }
            break;
            case NDTObject:
            {
                // access to message
                CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<NetworkMessage>)
                DiagLogF("%s%s:", (const char*)indent, (const char*)item.name);

                NetworkMessage& msg = const_cast<NetworkMessage&>(valTyped.GetVal());

                // message format
                const RefNetworkData& defVal = item.defValue;
                CHECK_ASSIGN(defValTyped, defVal, const RefNetworkDataTyped<int>);
                NetworkMessageFormatBase* msgFormat = _component->GetFormat((NetworkMessageType)defValTyped.GetVal());

                NetworkMessageContext ctx(&msg, msgFormat, *const_cast<NetworkMessageContext*>(this));
                ctx.LogMessage(level, indent + RString("\t"));
            }
            break;
            case NDTObjectArray:
            {
                // access to message array
                CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<AutoArray<NetworkMessage>>)
                DiagLogF("%s%s:", (const char*)indent, (const char*)item.name);

                const AutoArray<NetworkMessage>& array = valTyped.GetVal();

                // messages format
                const RefNetworkData& defVal = item.defValue;
                CHECK_ASSIGN(defValTyped, defVal, const RefNetworkDataTyped<int>);
                NetworkMessageFormatBase* msgFormat = _component->GetFormat((NetworkMessageType)defValTyped.GetVal());

                int m = array.Size();
                for (int j = 0; j < m; j++)
                {
                    DiagLogF("%s\tItem %d:", (const char*)indent, j);
                    NetworkMessage& msg = const_cast<NetworkMessage&>(array[j]);
                    NetworkMessageContext ctx(&msg, msgFormat, *const_cast<NetworkMessageContext*>(this));
                    ctx.LogMessage(level, indent + RString("\t\t"));
                }
            }
            break;
            case NDTRef:
            {
                CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<NetworkId>)
                DiagLogF("%s%s = %d:%d", (const char*)indent, (const char*)item.name, valTyped.GetVal().creator,
                         valTyped.GetVal().id);
            }
            break;
            case NDTRefArray:
            {
                CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<AutoArray<NetworkId>>)
                DiagLogF("%s%s:", (const char*)indent, (const char*)item.name);
                const AutoArray<NetworkId>& array = valTyped.GetVal();
                int m = array.Size();
                for (int j = 0; j < m; j++)
                {
                    DiagLogF("%s\tItem %d = %d:%d", (const char*)indent, j, array[j].creator, array[j].id);
                }
            }
            break;
            case NDTData:
            {
                DiagLogF("%s%s = <generic data>", (const char*)indent, (const char*)item.name);
            }
            break;
            default:
                RptF("Bad data type %d", item.type);
                break;
        }
    }
}

TMError NetworkMessageContext::IdxTransfer(int index, bool& value)
{
    TRANSFER_FORMAT
    NET_ERROR(item.type == NDTBool);
    TRANSFER(bool)
}

TMError NetworkMessageContext::IdxTransfer(int index, int& value)
{
    TRANSFER_FORMAT
    NET_ERROR(item.type == NDTInteger);
    TRANSFER(int)
}

TMError NetworkMessageContext::IdxTransfer(int index, float& value)
{
    TRANSFER_FORMAT
    NET_ERROR(item.type == NDTFloat);
    TRANSFER(float)
}

TMError NetworkMessageContext::IdxTransfer(int index, RString& value)
{
    TRANSFER_FORMAT
    NET_ERROR(item.type == NDTString);
    TRANSFER(RString)
}

// transfer raw data; optimized memory allocation, as there is no need to
// create a temporary AutoArray<char> buffer

TMError NetworkMessageContext::IdxGetRaw(int index, void*& value, int& size)
{
    NET_ERROR(!_sending);
    TRANSFER_FORMAT
    const RefNetworkData& val = _msg->values[index];
    CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<AutoArray<char>>)
    value = valTyped.GetVal().Data();
    size = valTyped.GetVal().Size();
    return TMOK;
}

TMError NetworkMessageContext::IdxSendRaw(int index, void* value, int size)
{
    NET_ERROR(_sending);
    TRANSFER_FORMAT
    NET_ERROR(item.type == NDTRawData);
    _msg->values[index] = RefNetworkDataTyped<AutoArray<char>>(value, size);
    return TMOK;
}

TMError NetworkMessageContext::IdxTransfer(int index, Time& value)
{
    TRANSFER_FORMAT
    NET_ERROR(item.type == NDTTime);
    TRANSFER(Time)
}

TMError NetworkMessageContext::IdxTransfer(int index, Vector3& value)
{
    TRANSFER_FORMAT
    NET_ERROR(item.type == NDTVector);
    TRANSFER(Vector3)
}

TMError NetworkMessageContext::IdxTransfer(int index, Matrix3& value)
{
    TRANSFER_FORMAT
    NET_ERROR(item.type == NDTMatrix);
    TRANSFER(Matrix3)
}

// See also NetworkMessageContext::IdxTransferRaw for the version with direct
// data access.

TMError NetworkMessageContext::IdxTransfer(int index, RadioSentence& value)
{
    TRANSFER_FORMAT
    NET_ERROR(item.type == NDTSentence);
    TRANSFER(RadioSentence)
}

TMError NetworkMessageContext::IdxGetId(int index, NetworkId& id)
{
    if (_sending)
    {
        return TMGeneric;
    }
    TRANSFER_FORMAT
    NET_ERROR(item.type == NDTRef);
    const RefNetworkData& val = _msg->values[index];
    CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<NetworkId>)
    id = valTyped.GetVal();
    return TMOK;
}

TMError NetworkMessageContext::IdxGetIds(int index, AutoArray<NetworkId>& array)
{
    if (_sending)
    {
        return TMGeneric;
    }
    TRANSFER_FORMAT
    NET_ERROR(item.type == NDTRefArray);
    const RefNetworkData& val = _msg->values[index];
    CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<AutoArray<NetworkId>>)
    array = valTyped.GetVal();
    return TMOK;
}

TMError NetworkMessageContext::IdxTransfer(int index, NetworkDataType& type, NetworkCompressionType& compression,
                                           RefNetworkData& defVal)
{
    TRANSFER_FORMAT
    NET_ERROR(item.type == NDTData);

    if (_sending)
    {
        _msg->values[index] = RefNetworkDataWithFormat(type, compression, defVal);
    }
    else
    {
        const RefNetworkData& val = _msg->values[index];
        CHECK_ASSIGN(valTyped, val, const RefNetworkDataWithFormat)
        type = valTyped->format.type;
        compression = valTyped->format.compression;
        defVal = valTyped->format.defValue;
    }

    return TMOK;
}

// Raw message

void NetworkMessageRaw::Put(bool value, NetworkCompressionType compression)
{
    switch (compression)
    {
        case NCTNone:
        case NCTDefault:
            Write(&value, sizeof(bool));
            break;
        default:
            LOG_ERROR(Network, "Unsupported compression method {}", (int)compression);
            break;
    }
}

void NetworkMessageRaw::Put(char value, NetworkCompressionType compression)
{
    switch (compression)
    {
        case NCTNone:
        case NCTDefault:
            Write(&value, sizeof(char));
            break;
        default:
            LOG_ERROR(Network, "Unsupported compression method {}", (int)compression);
            break;
    }
}

void NetworkMessageRaw::Put(int value, NetworkCompressionType compression)
{
    switch (compression)
    {
        case NCTNone:
            Write(&value, sizeof(int));
            break;
        case NCTSmallUnsigned:
        {
            unsigned int val = value;
            for (;;)
            {
                unsigned char c = val & 0x7f;
                val >>= 7;
                if (val)
                {
                    c |= 0x80;
                    Write(&c, sizeof(unsigned char));
                }
                else
                {
                    // no more bits left
                    Write(&c, sizeof(unsigned char));
                    break;
                }
            }
        }
        break;
        case NCTSmallSigned:
        {
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
                unsigned char c = val & 0x7f;
                val >>= 7;
                if (val)
                {
                    c |= 0x80;
                    Write(&c, sizeof(unsigned char));
                }
                else
                {
                    // no more bits left
                    Write(&c, sizeof(unsigned char));
                    break;
                }
            }
        }
        break;
        default:
            LOG_ERROR(Network, "Unsupported compression method {}", (int)compression);
            break;
    }
}

void NetworkMessageRaw::Put(float value, NetworkCompressionType compression)
{
    switch (compression)
    {
        float min, max, invRange;
#define RANGE(a, b) min = (a), max = (b), invRange = 1.0f / ((b) - (a))
        case NCTFloat0To1:
            RANGE(0, 1);
            goto Compression;
        case NCTFloat0To2:
            RANGE(0, 2);
            goto Compression;
        case NCTFloatM1ToP1:
            RANGE(-1, +1);
            goto Compression;
        case NCTFloatAngle:
            RANGE(-H_PI, +H_PI);
            goto CompressionUnlimited;
        case NCTFloatMostly0To1:
            RANGE(0, 1);
            goto CompressionUnlimited;
#undef RANGE
        Compression:
#if FLOAT_COMPRESSION
        {
            // map min to 0, max to 254
            // note: 254 is used instead of 255, so that 0 with min = max
            // can be represented exactly
            float compressed = (value - min) * invRange;
            int iValue = toInt(compressed * 254);
            saturate(iValue, 0, 254);
            unsigned char cValue = iValue;
            Write(&cValue, sizeof(cValue));
        }
        break;
#endif
        CompressionUnlimited:
#if FLOAT_COMPRESSION
        {
            // map min to -128, max to 127
            float compressed = (value - min) * invRange;
            int iValue = toInt(compressed * 254 - 127);
            Put(iValue, NCTSmallSigned);
        }
        break;
#endif
        case NCTNone:
        case NCTDefault:
            Write(&value, sizeof(value));
            break;
        default:
            LOG_ERROR(Network, "Unsupported compression method {}", (int)compression);
            break;
    }
}

#if _ENABLE_CHEATS
#include <Poseidon/Foundation/Math/Statistics.hpp>

StatisticsByName NetStrStats;
StatisticsByName NetMoveStats;

static class NetStrStatsReportClass
{
  public:
    ~NetStrStatsReportClass()
    {
        NetStrStats.Report();
        NetMoveStats.Report();
    }
} NetStrStatsReport;
#endif

void NetworkMessageRaw::Put(RString value, NetworkCompressionType compression)
{
    switch (compression)
    {
        case NCTStringGeneric: // NCTDefault:
        {
            int id = GetNetIdStrings().GetId(value);
            if (id >= 0)
            {
                Put(id + 1, NCTSmallUnsigned);
            }
            else
            {
                // Table misses are valid: the full string is sent below. Config names
                // are case-insensitive in game data, while this compression table is
                // intentionally case-sensitive, so variants like "handgrenade" must
                // not log errors just because they miss the compact id.
                Put(0, NCTSmallUnsigned);
                Write((const char*)value, value.GetLength() + 1);
#if _ENABLE_CHEATS
                NetStrStats.Count(value);
#endif
            }
            break;
        }
        case NCTStringMove:
        {
            int id = GetNetIdMoves().GetId(value);
            if (id >= 0)
            {
                Put(id + 1, NCTSmallUnsigned);
            }
            else
            {
                Put(0, NCTSmallUnsigned);
                Write((const char*)value, value.GetLength() + 1);
#if _ENABLE_CHEATS
                NetMoveStats.Count(value);
#endif
            }
            break;
        }
        case NCTNone:
            Write((const char*)value, value.GetLength() + 1);
            break;
        default:
            LOG_ERROR(Network, "Unsupported compression method {}", (int)compression);
            break;
    }
}

void NetworkMessageRaw::Put(Time value, NetworkCompressionType compression)
{
    switch (compression)
    {
        case NCTNone:
        case NCTDefault:
            Put(value.toInt(), NCTNone);
            break;
        default:
            LOG_ERROR(Network, "Unsupported compression method {}", (int)compression);
            break;
    }
}

void NetworkMessageRaw::Put(Vector3Par value, NetworkCompressionType compression)
{
    switch (compression)
    {
        case NCTNone:
        case NCTDefault:
            for (int i = 0; i < 3; i++)
            {
                Put(value[i], NCTNone);
            }
            break;
        default:
            LOG_ERROR(Network, "Unsupported compression method {}", (int)compression);
            break;
    }
}

void NetworkMessageRaw::Put(Matrix3Par value, NetworkCompressionType compression)
{
    switch (compression)
    {
        case NCTNone:
        case NCTDefault:
            for (int i = 0; i < 3; i++)
            {
                for (int j = 0; j < 3; j++)
                {
                    Put(value(i, j), NCTNone);
                }
            }
            break;
        case NCTMatrixOrientation:
        {
            EncodedMatrix3 encoded;
            encoded.Encode(value);
            Write(&encoded, sizeof(encoded));
            break;
        }
        default:
            LOG_ERROR(Network, "Unsupported compression method {}", (int)compression);
            break;
    }
}

void NetworkMessageRaw::Put(NetworkId& value, NetworkCompressionType compression)
{
    switch (compression)
    {
        case NCTNone:
        case NCTDefault:
            Put(value.creator, NCTNone);
            Put(value.id, NCTNone);
            break;
        default:
            LOG_ERROR(Network, "Unsupported compression method {}", (int)compression);
            break;
    }
}

bool NetworkMessageRaw::Get(bool& value, NetworkCompressionType compression)
{
    switch (compression)
    {
        case NCTNone:
        case NCTDefault:
        {
            // Read into a byte and normalize: a tampered wire byte (e.g. 136)
            // memcpy'd straight into a bool's storage is an out-of-range bool,
            // and any later load of it is undefined behaviour.
            unsigned char raw = 0;
            if (!Read(&raw, sizeof(raw)))
                return false;
            value = (raw != 0);
            return true;
        }
        default:
            LOG_ERROR(Network, "Unsupported compression method {}", (int)compression);
            return false;
    }
}

bool NetworkMessageRaw::Get(char& value, NetworkCompressionType compression)
{
    switch (compression)
    {
        case NCTNone:
        case NCTDefault:
            return Read(&value, sizeof(value));
        default:
            LOG_ERROR(Network, "Unsupported compression method {}", (int)compression);
            return false;
    }
}

bool NetworkMessageRaw::Get(int& value, NetworkCompressionType compression)
{
    switch (compression)
    {
        case NCTNone:
            return Read(&value, sizeof(value));
        case NCTSmallUnsigned:
        {
            unsigned int val = 0;
            int offset = 0;
            while (true)
            {
                unsigned char c;
                if (!Read(&c, sizeof(unsigned char)))
                {
                    return false;
                }
                // transfer 7 bits ber byte. Bound offset: an oversized varint
                // must not shift past int width (UB), and bits beyond 32 don't
                // fit the decoded value anyway. Unsigned shift avoids overflow.
                if (offset < 32)
                    val |= (unsigned)(c & 0x7f) << offset;
                // check terminator
                if ((c & 0x80) == 0)
                {
                    break;
                }
                offset += 7;
            }
            value = val;
        }
            return true;
        case NCTSmallSigned:
        {
            unsigned int val = 0;
            int offset = 0;
            while (true)
            {
                unsigned char c;
                if (!Read(&c, sizeof(unsigned char)))
                {
                    return false;
                }
                // transfer 7 bits ber byte. Bound offset: an oversized varint
                // must not shift past int width (UB), and bits beyond 32 don't
                // fit the decoded value anyway. Unsigned shift avoids overflow.
                if (offset < 32)
                    val |= (unsigned)(c & 0x7f) << offset;
                // check terminator
                if ((c & 0x80) == 0)
                {
                    break;
                }
                offset += 7;
            }
            if (val & 1)
            {
                value = -(int)(val >> 1);
            }
            else
            {
                value = val >> 1;
            }
        }
            return true;
        default:
            LOG_ERROR(Network, "Unsupported compression method {}", (int)compression);
            return false;
    }
}

bool NetworkMessageRaw::Get(float& value, NetworkCompressionType compression)
{
    switch (compression)
    {
        float min, coef;
#define RANGE(a, b) min = (a), coef = float((b) - (a)) / 254
        case NCTFloat0To1:
            RANGE(0, 1);
            goto Compression;
        case NCTFloat0To2:
            RANGE(0, 2);
            goto Compression;
        case NCTFloatM1ToP1:
            RANGE(-1, +1);
            goto Compression;
        case NCTFloatAngle:
            RANGE(-H_PI, +H_PI);
            goto CompressionUnlimited;
        case NCTFloatMostly0To1:
            RANGE(0, 1);
            goto CompressionUnlimited;
#undef RANGE
        Compression:
#if FLOAT_COMPRESSION
        {
            unsigned char cValue;
            if (!Read(&cValue, sizeof(cValue)))
            {
                return false;
            }
            value = cValue * coef + min;
        }
            return true;
#endif
        CompressionUnlimited:
#if FLOAT_COMPRESSION
        {
            int iValue;
            if (!Get(iValue, NCTSmallSigned))
            {
                return false;
            }
            value = (iValue + 127) * coef + min;
        }
            return true;
#endif
        case NCTNone:
        case NCTDefault:
            return Read(&value, sizeof(float));
        default:
            LOG_ERROR(Network, "Unsupported compression method {}", (int)compression);
            return false;
    }
}

bool NetworkMessageRaw::GetNoCompression(RString& value)
{
    int lastPos = _pos;
    if (_externalBuffer)
    {
        while (lastPos < _externalBufferSize && _externalBuffer[lastPos])
        {
            lastPos++;
        }
        if (lastPos >= _externalBufferSize)
        {
            return false;
        }
        value = _externalBuffer + _pos;
    }
    else
    {
        while (lastPos < _buffer.Size() && _buffer[lastPos])
        {
            lastPos++;
        }
        if (lastPos >= _buffer.Size())
        {
            return false;
        }
        value = _buffer.Data() + _pos;
    }
    _pos = lastPos + 1;
    return true;
}

bool NetworkMessageRaw::Get(RString& value, NetworkCompressionType compression)
{
    switch (compression)
    {
        case NCTNone:
            return GetNoCompression(value);
        case NCTStringGeneric: // NCTDefault:
        {
            int id;
            bool ok = Get(id, NCTSmallUnsigned);
            if (!ok)
            {
                return false;
            }
            if (id > 0)
            {
                value = GetNetIdStrings().GetString(id - 1);
                return true;
            }
            else
            {
                ok = GetNoCompression(value);
                return ok;
            }
        }
        case NCTStringMove:
        {
            int id;
            bool ok = Get(id, NCTSmallUnsigned);
            if (!ok)
            {
                return false;
            }
            if (id > 0)
            {
                value = GetNetIdMoves().GetString(id - 1);
                return true;
            }
            else
            {
                ok = GetNoCompression(value);
                return ok;
            }
        }
        default:
            LOG_ERROR(Network, "Unsupported compression method {}", (int)compression);
            return false;
    }
}

bool NetworkMessageRaw::Get(Time& value, NetworkCompressionType compression)
{
    switch (compression)
    {
        case NCTNone:
        case NCTDefault:
        {
            int intVal;
            if (!Get(intVal, NCTNone))
            {
                return false;
            }
            value = Time(intVal);
            return true;
        }
        default:
            LOG_ERROR(Network, "Unsupported compression method {}", (int)compression);
            return false;
    }
}

bool NetworkMessageRaw::Get(Vector3& value, NetworkCompressionType compression)
{
    switch (compression)
    {
        case NCTNone:
        case NCTDefault:
        {
            for (int i = 0; i < 3; i++)
            {
                if (!Get(value[i], NCTNone))
                {
                    return false;
                }
            }
            return true;
        }
        default:
            LOG_ERROR(Network, "Unsupported compression method {}", (int)compression);
            return false;
    }
}

bool NetworkMessageRaw::Get(Matrix3& value, NetworkCompressionType compression)
{
    switch (compression)
    {
        case NCTNone:
        case NCTDefault:
        {
            for (int i = 0; i < 3; i++)
            {
                for (int j = 0; j < 3; j++)
                {
                    if (!Get(value(i, j), NCTNone))
                    {
                        return false;
                    }
                }
            }
            return true;
        }
        case NCTMatrixOrientation:
        {
            EncodedMatrix3 encoded;
            Read(&encoded, sizeof(encoded));
            encoded.Decode(value);
            return true;
        }
        default:
            LOG_ERROR(Network, "Unsupported compression method {}", (int)compression);
            return false;
    }
}

bool NetworkMessageRaw::Get(NetworkId& value, NetworkCompressionType compression)
{
    switch (compression)
    {
        case NCTNone:
        case NCTDefault:
            if (!Get(value.creator, NCTNone))
            {
                return false;
            }
            return Get(value.id, NCTNone);
        default:
            LOG_ERROR(Network, "Unsupported compression method {}", (int)compression);
            return false;
    }
}

// Network Components

// low level transfer functions

bool NetworkClient::DXSendMsg(int to, NetworkMessageRaw& rawMsg, DWORD& msgID, NetMsgFlags flags)
{
    NET_ERROR(_client);
    NET_ERROR(to == TO_SERVER);

    return _client->SendMsg((BYTE*)rawMsg.GetData(), rawMsg.GetSize(), msgID, flags);
}

bool NetworkServer::DXSendMsg(int to, NetworkMessageRaw& rawMsg, DWORD& msgID, NetMsgFlags dwFlags)
{
    NET_ERROR(_server);

    if (_dedicated)
    {
        _monitorOut += rawMsg.GetSize();
    }

    return _server->SendMsg(to, (BYTE*)rawMsg.GetData(), rawMsg.GetSize(), msgID, dwFlags);
}

#if _ENABLE_CHEATS
extern bool outputLogs;
#endif

int NetworkComponent::CalculateMessageSize(NetworkMessage* msg, NetworkMessageType type)
{
    NET_ERROR(type != NMTMessages);

    NetworkMessageFormatBase* format = GetFormat(type);

    int size = 0;
    size += CalculateValueSize((int)type, NCTSmallUnsigned);
    size += CalculateValueSize(msg->time, NCTNone);
    size += CalculateMsgSize(msg, format);
    return size;
}

DWORD NetworkComponent::SendMsg(int to, NetworkMessage* msg, NetworkMessageType type, NetMsgFlags dwFlags)
{
    if (to == TO_SERVER)
    {
        NetworkServer* server = _parent->GetServer();
        if (server)
        {
            msg->size = 0;
            //			server->OnMessage(GetPlayer(), msg, type);
            server->AddLocalMessage(GetPlayer(), type, msg);
            return 0;
        }
        else if (dwFlags & NMFGuaranteed)
        {
            msg->size = CalculateMessageSize(msg, type);
#if _ENABLE_CHEATS
            StatMsgSent(type, msg->size);
#endif
            if (dwFlags & NMFHighPriority)
            {
                DWORD err = SendMsgRemote(to, msg, type, dwFlags | NMFStatsAlreadyDone);
                if (err == 0xffffffff)
                {
                    return err;
                }
                return 0;
            }
            else
            {
                EnqueueMsg(to, msg, type);
                return 0;
            }
        }
        else
        {
            msg->size = CalculateMessageSize(msg, type);
#if _ENABLE_CHEATS
            StatMsgSent(type, msg->size);
#endif
            EnqueueMsgNonGuaranteed(to, msg, type);
            return 1;
        }
    }
    else
    {
        if (IsLocalClient(to))
        {
            msg->size = 0;
            NetworkClient* client = _parent->GetClient();
            client->AddLocalMessage(GetPlayer(), type, msg);
            return 0;
        }
        else if (dwFlags & NMFGuaranteed)
        {
            msg->size = CalculateMessageSize(msg, type);
#if _ENABLE_CHEATS
            StatMsgSent(type, msg->size);
#endif
            if (dwFlags & NMFHighPriority)
            {
                DWORD err = SendMsgRemote(to, msg, type, dwFlags | NMFStatsAlreadyDone);
                if (err == 0xffffffff)
                {
                    return err;
                }
                return 0;
            }
            else
            {
                EnqueueMsg(to, msg, type);
                return 0;
            }
        }
        else
        {
            msg->size = CalculateMessageSize(msg, type);
#if _ENABLE_CHEATS
            StatMsgSent(type, msg->size);
#endif
            EnqueueMsgNonGuaranteed(to, msg, type);
            return 1;
        }
    }
}

DWORD NetworkComponent::SendMsgRemote(int to, NetworkMessage* msg, NetworkMessageType type, NetMsgFlags dwFlags)
{
    NetworkMessageFormatBase* format = GetFormat(type);
    if (!format)
    {
        return 0xFFFFFFFF;
    }

#if _ENABLE_CHEATS
    if (outputLogs)
    {
        if (dwFlags & NMFHighPriority)
        {
            DiagLogF("%s: send HP message %s to %d", GetDebugName(), NetworkMessageTypeNames[type], to);
            NetworkMessageContext ctx(msg, format, this, to, MSG_RECEIVE);
            ctx.LogMessage(3, "\t");
        }
    }
#endif

#if _ENABLE_CHEATS
    int level = GetDiagLevel(type, !_parent->GetClient() || to != _parent->GetClient()->GetPlayer());
    if (level >= 2)
    {
        DiagLogF("%s: send message %s to %d, flags %d", GetDebugName(), NetworkMessageTypeNames[type], to, dwFlags);
        NetworkMessageContext ctx(msg, format, this, to, MSG_RECEIVE);
        ctx.LogMessage(level, "\t");
    }

    if (outputLogs)
        LOG_DEBUG(Network, "{}: send message {} to {}, flags {}", GetDebugName(), NetworkMessageTypeNames[type], to,
                  dwFlags);
#else
    int level = 0;
#endif

    NetworkMessageRaw rawMsg;

    // Message header
    rawMsg.Put(type, NCTSmallUnsigned);
    rawMsg.Put(msg->time, NCTNone);

    // Message body
    EncodeMsg(rawMsg, msg, format);

    msg->size = rawMsg.GetSize();

#if _ENABLE_CHEATS
    // note: all SendMsgRemote message are already counted in stats
    // we can probably remove this flag
    NET_ERROR((dwFlags & NMFStatsAlreadyDone) != 0);

    if ((dwFlags & NMFStatsAlreadyDone) == 0)
    {
        StatMsgSent(type, msg->size);
    }
#endif

    return SendMsgRaw(to, rawMsg, dwFlags, level);
}

DWORD NetworkComponent::SendMsgQueue(int to, NetworkMessageQueue& queue, int begin, int end, NetMsgFlags dwFlags)
{
    int n = end - begin;

    NET_ERROR(n > 0);
    if (n == 1)
    {
        return SendMsgRemote(to, queue[begin].msg, queue[begin].type, dwFlags | NMFStatsAlreadyDone);
    }

    NetworkMessageType type = NMTMessages;
    Time time = Glob.time;

#if _ENABLE_CHEATS
    bool remote = !_parent->GetClient() || to != _parent->GetClient()->GetPlayer();
    int level = GetDiagLevel(type, remote);
    if (level >= 2)
    {
        DiagLogF("%s: send %d messages to %d", GetDebugName(), n, to);
        for (int i = begin; i < end; i++)
        {
            NetworkMessageType type = queue[i].type;
            int level = GetDiagLevel(type, remote);
            if (level < 2)
                continue;
            DiagLogF("\tMessage %s", NetworkMessageTypeNames[type]);
            NetworkMessageContext ctx(queue[i].msg, GetFormat(type), this, to, MSG_RECEIVE);
            ctx.LogMessage(level, "\t\t");
        }
    }
    else
    {
        for (int i = begin; i < end; i++)
        {
            NetworkMessageType type = queue[i].type;
            int level = GetDiagLevel(queue[i].type, remote);
            if (level < 2)
                continue;
            DiagLogF("%s: send message %s to %d, flags %d", GetDebugName(), NetworkMessageTypeNames[type], to, dwFlags);
            NetworkMessageContext ctx(queue[i].msg, GetFormat(type), this, to, MSG_RECEIVE);
            ctx.LogMessage(level, "\t");
        }
    }

    if (outputLogs)
        LOG_DEBUG(Network, "{}: send messages to {}", GetDebugName(), to);
#else
    int level = 0;
#endif

    NetworkMessageRaw rawMsg;

    // Message header
    rawMsg.Put(type, NCTSmallUnsigned);
    rawMsg.Put(time, NCTNone);

    rawMsg.Put(n, NCTSmallUnsigned);
    for (int i = begin; i < end; i++)
    {
        rawMsg.Put(queue[i].type, NCTSmallUnsigned);
        EncodeMsg(rawMsg, queue[i].msg, GetFormat(queue[i].type));
    }

    return SendMsgRaw(to, rawMsg, dwFlags, level);
}

DWORD NetworkComponent::SendMsgRaw(int to, NetworkMessageRaw& rawMsg, NetMsgFlags dwFlags, int diagLevel)
{
    StatRawMsgSent(to, rawMsg.GetSize());

    static Poseidon::Foundation::CRCCalculator calculator;
    int crc = calculator.CRC(rawMsg.GetData(), rawMsg.GetSize());
    rawMsg.Put(crc, NCTNone);

    GDebugger.PauseCheckingAlive();

    DWORD msgID = 0xFFFFFFFF;
    bool result = DXSendMsg(to, rawMsg, msgID, dwFlags);

    int size = rawMsg.GetSize();

    GDebugger.ResumeCheckingAlive();

    if (diagLevel >= 2)
    {
        DiagLogF("  result = %x, message ID = %x, size = %d", result, msgID, size);
    }
    if (!result)
    {
        const PlayerIdentity* ident = FindIdentity(to);
        LOG_ERROR(Network, "Message not sent - error {:x}, message ID = {:x}, to {} ({})", result, msgID, to,
                  ident ? (const char*)ident->name : "<no>");
        return 0xFFFFFFFF;
    }
    return msgID;
}

void NetworkComponent::EncodeMsgItem(NetworkMessageRaw& dst, NetworkMessage* msg, const RefNetworkData& val,
                                     const NetworkMessageFormatItem& formatItem)
{
    switch (formatItem.type)
    {
        case NDTBool:
        {
            CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<bool>)
            dst.Put(valTyped.GetVal(), formatItem.compression);
        }
        break;
        case NDTInteger:
        {
            CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<int>)
            dst.Put(valTyped.GetVal(), formatItem.compression);
        }
        break;
        case NDTFloat:
        {
            CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<float>)
            dst.Put(valTyped.GetVal(), formatItem.compression);
        }
        break;
        case NDTString:
        {
            CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<RString>)
            dst.Put(valTyped.GetVal(), formatItem.compression);
        }
        break;
        case NDTRawData:
        {
            CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<AutoArray<char>>)
            AutoArray<char>& array = valTyped.GetVal();
            int m = array.Size();
            dst.Put(m, NCTSmallUnsigned);
            for (int j = 0; j < m; j++)
            {
                dst.Put(array[j], formatItem.compression);
            }
        }
        break;
        case NDTTime:
        {
            CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<Time>)
            dst.Put(valTyped.GetVal(), formatItem.compression);
        }
        break;
        case NDTVector:
        {
            CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<Vector3>)
            dst.Put(valTyped.GetVal(), formatItem.compression);
        }
        break;
        case NDTMatrix:
        {
            CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<Matrix3>)
            dst.Put(valTyped.GetVal(), formatItem.compression);
        }
        break;
        case NDTIntArray:
        {
            CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<AutoArray<int>>)
            AutoArray<int>& array = valTyped.GetVal();
            int m = array.Size();
            dst.Put(m, NCTSmallUnsigned);
            for (int j = 0; j < m; j++)
            {
                dst.Put(array[j], formatItem.compression);
            }
        }
        break;
        case NDTFloatArray:
        {
            CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<AutoArray<float>>)
            AutoArray<float>& array = valTyped.GetVal();
            int m = array.Size();
            dst.Put(m, NCTSmallUnsigned);
            for (int j = 0; j < m; j++)
            {
                dst.Put(array[j], formatItem.compression);
            }
        }
        break;
        case NDTStringArray:
        {
            CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<AutoArray<RString>>)
            AutoArray<RString>& array = valTyped.GetVal();
            int m = array.Size();
            dst.Put(m, NCTSmallUnsigned);
            for (int j = 0; j < m; j++)
            {
                dst.Put(array[j], formatItem.compression);
            }
        }
        break;
        case NDTSentence:
        {
            CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<RadioSentence>)
            RadioSentence& array = valTyped.GetVal();
            int m = array.Size();
            dst.Put(m, NCTSmallUnsigned);
            for (int j = 0; j < m; j++)
            {
                dst.Put(array[j].id, formatItem.compression);
                dst.Put(array[j].pauseAfter, formatItem.compression);
            }
        }
        break;
        case NDTObject:
        {
            // access to message
            CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<NetworkMessage>)
            NetworkMessage& submsg = valTyped.GetVal();

            // message format
            const RefNetworkData& defVal = formatItem.defValue;
            CHECK_ASSIGN(defValTyped, defVal, const RefNetworkDataTyped<int>)
            NetworkMessageFormatBase* msgFormat = GetFormat((NetworkMessageType)defValTyped.GetVal());

            // propagate message header into submessage
            submsg.time = msg->time;
            EncodeMsg(dst, &submsg, msgFormat);
        }
        break;
        case NDTObjectArray:
        {
            // access to message array
            CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<AutoArray<NetworkMessage>>)
            AutoArray<NetworkMessage>& array = valTyped.GetVal();

            // messages format
            const RefNetworkData& defVal = formatItem.defValue;
            CHECK_ASSIGN(defValTyped, defVal, const RefNetworkDataTyped<int>)
            NetworkMessageFormatBase* itemFormat = GetFormat((NetworkMessageType)defValTyped.GetVal());

            int m = array.Size();
            dst.Put(m, NCTSmallUnsigned);
            for (int j = 0; j < m; j++)
            {
                // propagate message header into submessages
                array[j].time = msg->time;
                EncodeMsg(dst, &array[j], itemFormat);
            }
        }
        break;
        case NDTRef:
        {
            CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<NetworkId>)
            dst.Put(valTyped.GetVal(), formatItem.compression);
        }
        break;
        case NDTRefArray:
        {
            CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<AutoArray<NetworkId>>)
            AutoArray<NetworkId>& array = valTyped.GetVal();
            int m = array.Size();
            dst.Put(m, NCTSmallUnsigned);
            for (int j = 0; j < m; j++)
            {
                dst.Put(array[j], formatItem.compression);
            }
        }
        break;
        case NDTData:
        {
            CHECK_ASSIGN(valTyped, val, const RefNetworkDataWithFormat)
            NetworkDataType type = valTyped->format.type;
            dst.Put(type, NCTSmallUnsigned);
            dst.Put(valTyped->format.compression, NCTSmallUnsigned);
            if (type == NDTObject || type == NDTObjectArray)
            {
                NetworkMessageFormatItem format;
                format.type = NDTInteger;
                format.compression = NCTSmallUnsigned;
                // name and defValue are not used
                EncodeMsgItem(dst, msg, valTyped->format.defValue, format);
            }
            else
            {
                EncodeMsgItem(dst, msg, valTyped->format.defValue, valTyped->format);
            }
        }
        break;
        default:
            RptF("Bad data type %d", formatItem.type);
            break;
    }
}

void NetworkComponent::EncodeMsg(NetworkMessageRaw& dst, NetworkMessage* msg, NetworkMessageFormatBase* format)
{
    NET_ERROR(format);
    for (int i = 0; i < format->NItems(); i++)
    {
        EncodeMsgItem(dst, msg, msg->values[i], format->GetItem(i));
    }
}

RefNetworkData NetworkComponent::DecodeMsgItem(NetworkMessageRaw& src, NetworkMessage* msg,
                                               NetworkMessageFormatItem& formatItem)
{
    switch (formatItem.type)
    {
        case NDTBool:
        {
            // = false: Get leaves value untouched on an unsupported compression or
            // a short read, and constructing the typed data loads it — an
            // uninitialized bool is an out-of-range value (UB).
            bool value = false;
            src.Get(value, formatItem.compression);
            return RefNetworkDataTyped<bool>(value);
        }
        case NDTInteger:
        {
            int value;
            src.Get(value, formatItem.compression);
            return RefNetworkDataTyped<int>(value);
        }
        case NDTFloat:
        {
            float value;
            src.Get(value, formatItem.compression);
            return RefNetworkDataTyped<float>(value);
        }
        case NDTString:
        {
            RString value;
            src.Get(value, formatItem.compression);
            return RefNetworkDataTyped<RString>(value);
        }
        case NDTRawData:
        {
            RefNetworkDataTyped<AutoArray<char>> value;
            //	RefNetworkDataTyped< AutoArray<char> >();
            AutoArray<char>& array = value.GetVal();
            int m;
            if (!src.GetCount(m, NCTSmallUnsigned))
                return value;
            array.Resize(m);
            for (int j = 0; j < m; j++)
            {
                src.Get(array[j], formatItem.compression);
            }
            return value;
        }
        case NDTTime:
        {
            Time value;
            src.Get(value, formatItem.compression);
            return RefNetworkDataTyped<Time>(value);
        }
        case NDTVector:
        {
            Vector3 value;
            src.Get(value, formatItem.compression);
            return RefNetworkDataTyped<Vector3>(value);
        }
        case NDTMatrix:
        {
            Matrix3 value;
            src.Get(value, formatItem.compression);
            return RefNetworkDataTyped<Matrix3>(value);
        }
        case NDTIntArray:
        {
            RefNetworkDataTyped<AutoArray<int>> value;
            //	RefNetworkDataTyped< AutoArray<int> >();
            AutoArray<int>& array = value.GetVal();
            int m;
            if (!src.GetCount(m, NCTSmallUnsigned))
                return value;
            array.Resize(m);
            for (int j = 0; j < m; j++)
            {
                src.Get(array[j], formatItem.compression);
            }
            return value;
        }
        case NDTFloatArray:
        {
            RefNetworkDataTyped<AutoArray<float>> value;
            //	RefNetworkDataTyped< AutoArray<float> >();
            AutoArray<float>& array = value.GetVal();
            int m;
            if (!src.GetCount(m, NCTSmallUnsigned))
                return value;
            array.Resize(m);
            for (int j = 0; j < m; j++)
            {
                src.Get(array[j], formatItem.compression);
            }
            return value;
        }
        case NDTStringArray:
        {
            RefNetworkDataTyped<AutoArray<RString>> value;
            //	RefNetworkDataTyped< AutoArray<RString> >();
            AutoArray<RString>& array = value.GetVal();
            int m;
            if (!src.GetCount(m, NCTSmallUnsigned))
                return value;
            array.Resize(m);
            for (int j = 0; j < m; j++)
            {
                src.Get(array[j], formatItem.compression);
            }
            return value;
        }
        case NDTSentence:
        {
            RefNetworkDataTyped<RadioSentence> value;
            //	RefNetworkDataTyped<RadioSentence>();
            RadioSentence& array = value.GetVal();
            int m;
            if (!src.GetCount(m, NCTSmallUnsigned))
                return value;
            array.Resize(m);
            for (int j = 0; j < m; j++)
            {
                src.Get(array[j].id, formatItem.compression);
                src.Get(array[j].pauseAfter, formatItem.compression);
            }
            return value;
        }
        case NDTObject:
        {
            // access to message
            RefNetworkDataTyped<NetworkMessage> value;
            // RefNetworkDataTyped<NetworkMessage>();
            NetworkMessage& submsg = value.GetVal();

            // message format
            const RefNetworkData& defVal = formatItem.defValue;
            CHECK_ASSIGN(defValTyped, defVal, const RefNetworkDataTyped<int>)
            NetworkMessageFormatBase* msgFormat = GetFormat((NetworkMessageType)defValTyped.GetVal());

            // propagate message header into submessage
            submsg.time = msg->time;
            DecodeMsg(&submsg, src, msgFormat);

            return value;
        }
        case NDTObjectArray:
        {
            // access to message array
            RefNetworkDataTyped<AutoArray<NetworkMessage>> value;
            AutoArray<NetworkMessage>& array = value.GetVal();

            // messages format
            const RefNetworkData& defVal = formatItem.defValue;
            CHECK_ASSIGN(defValTyped, defVal, const RefNetworkDataTyped<int>)
            NetworkMessageFormatBase* itemFormat = GetFormat((NetworkMessageType)defValTyped.GetVal());

            int m;
            if (!src.GetCount(m, NCTSmallUnsigned))
                return value;
            array.Resize(m);
            for (int j = 0; j < m; j++)
            {
                // propagate message header into submessages
                array[j].time = msg->time;
                DecodeMsg(&array[j], src, itemFormat);
            }

            return value;
        }
        case NDTRef:
        {
            NetworkId value;
            src.Get(value, formatItem.compression);
            return RefNetworkDataTyped<NetworkId>(value);
        }
        case NDTRefArray:
        {
            RefNetworkDataTyped<AutoArray<NetworkId>> value;
            //	RefNetworkDataTyped< AutoArray<NetworkId> >();
            AutoArray<NetworkId>& array = value.GetVal();
            int m;
            if (!src.GetCount(m, NCTSmallUnsigned))
                return value;
            array.Resize(m);
            for (int j = 0; j < m; j++)
            {
                src.Get(array[j], formatItem.compression);
            }
            return value;
        }
        case NDTData:
        {
            // Read into ints and range-check before casting to the enums: a
            // tampered wire value out of range would make `type`/`compression`
            // invalid enum values, and every later load of them is UB.
            int typeI = 0, compI = 0;
            src.Get(typeI, NCTSmallUnsigned);
            src.Get(compI, NCTSmallUnsigned);
            if (typeI < 0 || typeI > NDTData || compI < 0 || compI > NCTMatrixOrientation)
            {
                RptF("Bad data type/compression %d/%d", typeI, compI);
                return RefNetworkDataNull();
            }
            NetworkDataType type = (NetworkDataType)typeI;
            NetworkCompressionType compression = (NetworkCompressionType)compI;
            RefNetworkDataWithFormat value(type, compression, ND_NULL);
            if (type == NDTObject || type == NDTObjectArray)
            {
                NetworkMessageFormatItem format;
                format.type = NDTInteger;
                format.compression = NCTSmallUnsigned;
                // name and defValue are not used
                value->format.defValue = DecodeMsgItem(src, msg, format);
            }
            else
            {
                value->format.defValue = DecodeMsgItem(src, msg, value->format);
            }
            return value;
        }
        default:
            RptF("Bad data type %d", formatItem.type);
            return RefNetworkDataNull();
    }
}

void NetworkComponent::DecodeMsg(NetworkMessage* msg, NetworkMessageRaw& src, NetworkMessageFormatBase* format)
{
    NET_ERROR(format);
    if (!format)
    {
        return;
    }
    int n = format->NItems();
    msg->values.Resize(n);
    for (int i = 0; i < n; i++)
    {
        msg->values[i] = DecodeMsgItem(src, msg, format->GetItem(i));
    }
}

int NetworkComponent::CalculateMsgItemSize(NetworkMessage* msg, const RefNetworkData& val,
                                           const NetworkMessageFormatItem& formatItem)
{
    switch (formatItem.type)
    {
        case NDTInteger:
        {
            CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<int>)
            int value = valTyped.GetVal();
            return CalculateValueSize(value, formatItem.compression);
        }
        case NDTBool:
        case NDTFloat:
        case NDTString:
        case NDTTime:
        case NDTVector:
        case NDTMatrix:
        case NDTRawData:
        case NDTIntArray:
        case NDTFloatArray:
        case NDTStringArray:
        case NDTSentence:
        case NDTRef:
        case NDTRefArray:
            return val.CalculateSize(formatItem.compression, formatItem);
        case NDTObject:
        {
            // access to message
            CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<NetworkMessage>)
            NetworkMessage& submsg = valTyped.GetVal();

            // message format
            const RefNetworkData& defVal = formatItem.defValue;
            CHECK_ASSIGN(defValTyped, defVal, const RefNetworkDataTyped<int>)
            NetworkMessageFormatBase* msgFormat = GetFormat((NetworkMessageType)defValTyped.GetVal());

            return CalculateMsgSize(&submsg, msgFormat);
        }
        case NDTObjectArray:
        {
            // access to message array
            CHECK_ASSIGN(valTyped, val, const RefNetworkDataTyped<AutoArray<NetworkMessage>>)
            AutoArray<NetworkMessage>& array = valTyped.GetVal();

            // messages format
            const RefNetworkData& defVal = formatItem.defValue;
            CHECK_ASSIGN(defValTyped, defVal, const RefNetworkDataTyped<int>)
            NetworkMessageFormatBase* itemFormat = GetFormat((NetworkMessageType)defValTyped.GetVal());

            int size = 0;
            int m = array.Size();
            size += CalculateValueSize(m, NCTSmallUnsigned);
            for (int j = 0; j < m; j++)
            {
                size += CalculateMsgSize(&array[j], itemFormat);
            }
            return size;
        }
        case NDTData:
        {
            CHECK_ASSIGN(valTyped, val, const RefNetworkDataWithFormat)
            NetworkDataType type = valTyped->format.type;
            int size = 0;
            size += CalculateValueSize((int)type, NCTSmallUnsigned);
            size += CalculateValueSize((int)valTyped->format.compression, NCTSmallUnsigned);
            if (type == NDTObject || type == NDTObjectArray)
            {
                NetworkMessageFormatItem format;
                format.type = NDTInteger;
                format.compression = NCTSmallUnsigned;
                // name and defValue are not used
                size += CalculateMsgItemSize(msg, valTyped->format.defValue, format);
            }
            else
            {
                size += CalculateMsgItemSize(msg, valTyped->format.defValue, valTyped->format);
            }
            return size;
        }
        default:
            RptF("Bad data type %d", formatItem.type);
            return 0;
    }
}

int NetworkComponent::CalculateMsgSize(NetworkMessage* msg, NetworkMessageFormatBase* format)
{
    NET_ERROR(format);
    int size = 0;
    int n = format->NItems();
    for (int i = 0; i < n; i++)
    {
        size += CalculateMsgItemSize(msg, msg->values[i], format->GetItem(i));
    }
    return size;
}

#define NDT_DEFINE_ALLOC(type, name, description) \
    template <>                                   \
    DEFINE_FAST_ALLOCATOR(NetworkDataTyped<type>)

NETWORK_DATA_TYPES(NDT_DEFINE_ALLOC)

template <>
DEFINE_FAST_ALLOCATOR(NetworkDataTyped<AutoArray<RStringI>>)
template <>
DEFINE_FAST_ALLOCATOR(NetworkDataTyped<StaticArrayAuto<float>>)
