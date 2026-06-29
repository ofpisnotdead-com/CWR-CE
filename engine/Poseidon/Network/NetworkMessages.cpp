#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/Config.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Core/ModSystem.hpp>
#include <Poseidon/Network/NetworkImpl.hpp>
#include <Poseidon/Core/Global.hpp>
// #include "strIncl.hpp"
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/UI/Locale/Languages.hpp>

#include <Poseidon/AI/ArcadeTemplate.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/Network/XML/Xml.hpp>
#include <Poseidon/Foundation/Strings/StrFormat.hpp>

#include <Poseidon/Foundation/Platform/GamePaths.hpp>
#include <Poseidon/Foundation/Algorithms/Qsort.hpp>

#include <Poseidon/World/Entities/Vehicles/AllAIVehicles.hpp>
#include <Poseidon/World/Entities/Vehicles/SeaGull.hpp>
#include <Poseidon/World/Detection/Detector.hpp>
#include <Poseidon/World/Entities/Weapons/Shots.hpp>

#include <Poseidon/Dev/Debug/DebugTrap.hpp>

#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/IO/PackFiles.hpp>
#include <Poseidon/IO/FileServer.hpp>
#include <Poseidon/IO/Filesystem/FileOps.hpp>

#include <Random/randomGen.hpp>
#include <Poseidon/Game/Commands/GameStateExt.hpp>

#include <Poseidon/Core/Progress.hpp>

#include <Poseidon/Game/UiActions.hpp>

#include <Poseidon/Foundation/Algorithms/Crc.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

#ifdef _WIN32
#include <io.h>
#endif

#include <Poseidon/World/Scene/Camera/Camera.hpp>

#include <Poseidon/Foundation/Strings/Mbcs.hpp>

using Poseidon::Foundation::Time;

// Use private memory heap for system messages
#define USE_PRIVATE_HEAP 1

using namespace Poseidon;

DEFINE_NET_MESSAGE(AskForDammage, ASK_FOR_DAMMAGE_MSG)
DEFINE_GET_INDICES(AskForDammage)

DEFINE_NET_MESSAGE(AskForSetDammage, ASK_FOR_SET_DAMMAGE_MSG)
DEFINE_GET_INDICES(AskForSetDammage)

DEFINE_NET_MESSAGE(AskForGetIn, ASK_FOR_GET_IN_MSG)
DEFINE_GET_INDICES(AskForGetIn)

DEFINE_NET_MESSAGE(AskForGetOut, ASK_FOR_GET_OUT_MSG)
DEFINE_GET_INDICES(AskForGetOut)

DEFINE_NET_MESSAGE(AskForChangePosition, ASK_FOR_CHANGE_POSITION_MSG)
DEFINE_GET_INDICES(AskForChangePosition)

DEFINE_NET_MESSAGE(AskForAimWeapon, ASK_FOR_AIM_WEAPON_MSG)
DEFINE_GET_INDICES(AskForAimWeapon)

DEFINE_NET_MESSAGE(AskForAimObserver, ASK_FOR_AIM_OBSERVER_MSG)
DEFINE_GET_INDICES(AskForAimObserver)

DEFINE_NET_MESSAGE(AskForSelectWeapon, ASK_FOR_SELECT_WEAPON_MSG)
DEFINE_GET_INDICES(AskForSelectWeapon)

DEFINE_NET_MESSAGE(AskForAddImpulse, ASK_FOR_ADD_IMPULSE_MSG)
DEFINE_GET_INDICES(AskForAddImpulse)

DEFINE_NET_MESSAGE(AskForMoveVector, ASK_FOR_MOVE_VECTOR_MSG)
DEFINE_GET_INDICES(AskForMoveVector)

DEFINE_NET_MESSAGE(AskForMoveMatrix, ASK_FOR_MOVE_MATRIX_MSG)
DEFINE_GET_INDICES(AskForMoveMatrix)

DEFINE_NET_MESSAGE(AskForJoinGroup, ASK_FOR_JOIN_GROUP_MSG)
DEFINE_GET_INDICES(AskForJoinGroup)

DEFINE_NET_MESSAGE(AskForJoinUnits, ASK_FOR_JOIN_UNITS_MSG)
DEFINE_GET_INDICES(AskForJoinUnits)

DEFINE_NET_MESSAGE(AskForHideBody, ASK_FOR_HIDE_BODY_MSG)
DEFINE_GET_INDICES(AskForHideBody)

DEFINE_NET_MESSAGE(ExplosionDammageEffects, EXPLOSION_DAMMAGE_EFFECTS_MSG)
DEFINE_GET_INDICES(ExplosionDammageEffects)

DEFINE_NET_MESSAGE(DeleteObject, DELETE_OBJECT_MSG)
DEFINE_GET_INDICES(DeleteObject)

DEFINE_NET_MESSAGE(DeleteCommand, DELETE_COMMAND_MSG)
DEFINE_GET_INDICES(DeleteCommand)

DEFINE_NET_MESSAGE(AskForAmmo, ASK_FOR_AMMO_MSG)
DEFINE_GET_INDICES(AskForAmmo)

DEFINE_NET_MESSAGE(FireWeapon, FIRE_WEAPON_MSG)
DEFINE_GET_INDICES(FireWeapon)

DEFINE_NET_INDICES(UpdateWeapons, UPDATE_WEAPONS_MSG)
DEFINE_GET_INDICES(UpdateWeapons)

NetworkMessageFormat& UpdateWeaponsMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    UPDATE_WEAPONS_MSG(MSG_FORMAT)
    return format;
}

TMError UpdateWeaponsMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesUpdateWeapons*>(ctx.GetIndices()))
    const IndicesUpdateWeapons* indices = static_cast<const IndicesUpdateWeapons*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransferRef(indices->vehicle, vehicle))
    if (vehicle)
    {
        UpdateEntityAIWeaponsMessage weapons(vehicle);
        TMCHECK(ctx.IdxTransferObject(indices->weapons, weapons))
    }
    return TMOK;
}

DEFINE_NET_MESSAGE(AddWeaponCargo, ADD_WEAPON_CARGO_MSG)
DEFINE_GET_INDICES(AddWeaponCargo)

DEFINE_NET_MESSAGE(RemoveWeaponCargo, REMOVE_WEAPON_CARGO_MSG)
DEFINE_GET_INDICES(RemoveWeaponCargo)

DEFINE_NET_MESSAGE(AddMagazineCargo, ADD_MAGAZINE_CARGO_MSG)
DEFINE_GET_INDICES(AddMagazineCargo)

DEFINE_NET_MESSAGE(RemoveMagazineCargo, REMOVE_MAGAZINE_CARGO_MSG)
DEFINE_GET_INDICES(RemoveMagazineCargo)

NetworkMessageType VehicleInitCmd::GetNMType(NetworkMessageClass cls) const
{
    return NMTVehicleInit;
}

// network message indices for VehicleInitCmd class
class IndicesVehicleInit : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int vehicle;
    int init;

    IndicesVehicleInit();
    NetworkMessageIndices* Clone() const override { return new IndicesVehicleInit; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesVehicleInit::IndicesVehicleInit()
{
    vehicle = -1;
    init = -1;
}

void IndicesVehicleInit::Scan(NetworkMessageFormatBase* format){SCAN(vehicle) SCAN(init)}

// Create network message indices for VehicleInitCmd class
NetworkMessageIndices* GetIndicesVehicleInit()
{
    return new IndicesVehicleInit();
}

NetworkMessageFormat& VehicleInitCmd::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("vehicle", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Vehicle which is initialized"));
    format.Add("init", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Initialization statement"));
    return format;
}

TMError VehicleInitCmd::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesVehicleInit*>(ctx.GetIndices()))
    const IndicesVehicleInit* indices = static_cast<const IndicesVehicleInit*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransferRef(indices->vehicle, vehicle))
    TMCHECK(ctx.IdxTransfer(indices->init, init))
    return TMOK;
}

DEFINE_NET_MESSAGE(VehicleDestroyed, VEHICLE_DESTROYED_MSG)
DEFINE_GET_INDICES(VehicleDestroyed)

DEFINE_NET_MESSAGE(MarkerDelete, MARKER_DELETE_MSG)
DEFINE_GET_INDICES(MarkerDelete)

DEFINE_NET_MESSAGE(MarkerCreate, MARKER_CREATE_MSG)
DEFINE_GET_INDICES(MarkerCreate)

DEFINE_NET_MESSAGE(NetworkCommand, NETWORK_COMMAND_MSG)
DEFINE_GET_INDICES(NetworkCommand)

DEFINE_NET_MESSAGE(IntegrityQuestion, INTEGRITY_QUESTION_MSG)
DEFINE_GET_INDICES(IntegrityQuestion)

DEFINE_NET_MESSAGE(IntegrityAnswer, INTEGRITY_ANSWER_MSG)
DEFINE_GET_INDICES(IntegrityAnswer)

DEFINE_NET_MESSAGE(PlayerState, PLAYER_STATE_MSG)
DEFINE_GET_INDICES(PlayerState)

DEFINE_NET_MESSAGE(AttachPerson, ATTACH_PERSON_MSG)
DEFINE_GET_INDICES(AttachPerson)

/*
NetworkMessageFormat &RespawnQueueItem::CreateFormat
(
    NetworkMessageClass cls,
    NetworkMessageFormat &format
)
{
    format.Add("type", NDTString, NCTNone, DEFVALUE(RString, ""));
    format.Add("side", NDTInteger, NCTNone, DEFVALUE(int, TSideUnknown));
    format.Add("id", NDTInteger, NCTNone, DEFVALUE(int, -1));
    format.Add("varname", NDTString, NCTNone, DEFVALUE(RString, ""));
    // info
    format.Add("firstname", NDTString, NCTNone, DEFVALUE(RString, ""));
    format.Add("name", NDTString, NCTNone, DEFVALUE(RString, ""));
    format.Add("rank", NDTInteger, NCTNone, DEFVALUE(int, RankPrivate));
    format.Add("experience", NDTFloat, NCTNone, DEFVALUE(float, 0));

    format.Add("group", NDTRef, NCTNone, DEFVALUENULL);
    format.Add("position", NDTVector, NCTNone, DEFVALUE(Vector3, VZero));
    format.Add("time", NDTTime, NCTNone, DEFVALUE(Time, Time(0)));
    format.Add("player", NDTInteger, NCTNone, DEFVALUE(int, 0));
    return format;
}

TMError RespawnQueueItem::TransferMsg(NetworkMessageContext &ctx)
{
    if (ctx.IsSending())
    {
        RString typeName = type ? type->GetName() : "";
        TMCHECK(ctx.Transfer("type", typeName))
    }
    else
    {
        RString typeName;
        TMCHECK(ctx.Transfer("type", typeName))
        if (typeName.GetLength() > 0)
            type = static_cast<const VehicleType *>(VehicleTypes.New(typeName));
        else
            type = nullptr;
    }
    TMCHECK(ctx.Transfer("side", (int &)side))
    TMCHECK(ctx.Transfer("id", id))
    TMCHECK(ctx.Transfer("varname", varname))

    TMCHECK(ctx.Transfer("firstname", info._firstname))
    TMCHECK(ctx.Transfer("name", info._name))
    TMCHECK(ctx.Transfer("rank", (int &)info._rank))
    TMCHECK(ctx.Transfer("experience", info._experience))

    TMCHECK(ctx.TransferRef("group", group))
    TMCHECK(ctx.Transfer("position", position))
    TMCHECK(ctx.Transfer("time", time))
    TMCHECK(ctx.Transfer("player", player))
    return TMOK;
}
*/

DEFINE_NET_MESSAGE(SetFlagOwner, SET_FLAG_OWNER_MSG)
DEFINE_GET_INDICES(SetFlagOwner)

DEFINE_NET_INDICES_EX(SetFlagCarrier, SetFlagOwner, SET_FLAG_CARRIER_MSG)
DEFINE_GET_INDICES(SetFlagCarrier)

DEFINE_NET_INDICES(Login, LOGIN_MSG)
DEFINE_GET_INDICES(Login)

DEFINE_NET_INDICES(PlayerUpdate, PLAYER_UPDATE_MSG)
DEFINE_GET_INDICES(PlayerUpdate)

PlayerIdentity::PlayerIdentity()
{
    _minPing = 0, _avgPing = 0, _maxPing = 0;
    _minBandwidth = 0, _avgBandwidth = 0, _maxBandwidth = 0;
    _desync = 0;

    _rights = PRNone;

    destroy = false;
    failedLogin = 0;

    kickOffTime = UITIME_MAX;
    kickOffState = KOWait;
}

PlayerIdentity::~PlayerIdentity() = default;

NetworkMessageType PlayerIdentity::GetNMType(NetworkMessageClass cls) const
{
    if (cls == NMCUpdatePosition)
    {
        return NMTPlayerUpdate;
    }
    return NMTLogin;
}

NetworkMessageFormat& PlayerIdentity::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    switch (cls)
    {
        case NMCUpdatePosition:
            PLAYER_UPDATE_MSG(MSG_FORMAT)
            return format;
        default:
            LOGIN_MSG(MSG_FORMAT)
            return format;
    }
}

TMError PlayerIdentity::TransferMsg(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCUpdatePosition:
        {
            NET_ERROR(dynamic_cast<const IndicesPlayerUpdate*>(ctx.GetIndices()))
            const IndicesPlayerUpdate* indices = static_cast<const IndicesPlayerUpdate*>(ctx.GetIndices());

            ITRANSF(minPing);
            ITRANSF(avgPing);
            ITRANSF(maxPing);
            ITRANSF(minBandwidth);
            ITRANSF(avgBandwidth);
            ITRANSF(maxBandwidth);
            ITRANSF(desync);
            ITRANSF(rights);

            return TMOK;
        }
        default:
        {
            NET_ERROR(dynamic_cast<const IndicesLogin*>(ctx.GetIndices()))
            const IndicesLogin* indices = static_cast<const IndicesLogin*>(ctx.GetIndices());

            TMCHECK(ctx.IdxTransfer(indices->dpnid, (int&)dpnid))
            TMCHECK(ctx.IdxTransfer(indices->playerid, (int&)playerid))
            TMCHECK(ctx.IdxTransfer(indices->id, id))
            TMCHECK(ctx.IdxTransfer(indices->name, name))
            TMCHECK(ctx.IdxTransfer(indices->face, face))
            TMCHECK(ctx.IdxTransfer(indices->glasses, glasses))
            TMCHECK(ctx.IdxTransfer(indices->speaker, speaker))
            TMCHECK(ctx.IdxTransfer(indices->pitch, pitch))
            TMCHECK(ctx.IdxTransfer(indices->squad, squadId))
            TMCHECK(ctx.IdxTransfer(indices->fullname, fullname))
            TMCHECK(ctx.IdxTransfer(indices->email, email))
            TMCHECK(ctx.IdxTransfer(indices->icq, icq))
            TMCHECK(ctx.IdxTransfer(indices->remark, remark))
            TMCHECK(ctx.IdxTransfer(indices->state, (int&)state))
            TMCHECK(ctx.IdxTransfer(indices->version, version))

            // Original copy protection CD key check disabled — not applicable to CWR.
            return TMOK;
        }
    }
}

RString PlayerIdentity::GetName() const
{
    if (squad)
    {
        return name + RString(" [") + squad->nick + RString("]");
    }
    else
    {
        return name;
    }
}

DEFINE_NET_MESSAGE(Squad, SQUAD_MSG)
DEFINE_GET_INDICES(Squad)

NetworkMessageFormat& SquadIdentity::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    SQUAD_MSG(MSG_FORMAT)
    return format;
}

TMError SquadIdentity::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesSquad*>(ctx.GetIndices()))
    const IndicesSquad* indices = static_cast<const IndicesSquad*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransfer(indices->id, id))
    TMCHECK(ctx.IdxTransfer(indices->nick, nick))
    TMCHECK(ctx.IdxTransfer(indices->name, name))
    TMCHECK(ctx.IdxTransfer(indices->email, email))
    TMCHECK(ctx.IdxTransfer(indices->web, web))
    TMCHECK(ctx.IdxTransfer(indices->picture, picture))
    TMCHECK(ctx.IdxTransfer(indices->title, title))
    return TMOK;
}

DEFINE_NET_MESSAGE(ShowTarget, SHOW_TARGET_MSG)
DEFINE_GET_INDICES(ShowTarget)

DEFINE_NET_MESSAGE(ShowGroupDir, SHOW_GROUP_DIR_MSG)
DEFINE_GET_INDICES(ShowGroupDir)

// Declare static variable for message format
#define DECLARE_FORMAT(macro, class, name, description, group) \
    static NetworkMessageFormat items##name;                   \
    NetworkMessageIndices* GetIndices##name();

NETWORK_MESSAGE_TYPES(DECLARE_FORMAT)

// Add (create) format to static array of formats
#define FORMAT_SIMPLE(dummy, name) \
    GMsgFormats[curMsgFormat++] = name##Message::CreateFormat(NMCCreate, items##name).Init(GetIndices##name())
// Add (create) format to static array of formats
#define FORMAT_CREATE(type, format) \
    GMsgFormats[curMsgFormat++] = type::CreateFormat(NMCCreate, items##format).Init(GetIndices##format())
// Add (update) format to static array of formats
#define FORMAT_UPDATE(type, format) \
    GMsgFormats[curMsgFormat++] = type::CreateFormat(NMCUpdateGeneric, items##format).Init(GetIndices##format())
// Add (update position) format to static array of formats
#define FORMAT_UPDATE_POSITION(type, format) \
    GMsgFormats[curMsgFormat++] = type::CreateFormat(NMCUpdatePosition, items##format).Init(GetIndices##format())

// Add (update dammage) format to static array of formats
#define FORMAT_UPDATE_DAMMAGE(type, format) \
    GMsgFormats[curMsgFormat++] = type::CreateFormat(NMCUpdateDammage, items##format).Init(GetIndices##format())

// number of registered local (static) message formats
static int curMsgFormat = 0;

// local (static) message formats
NetworkMessageFormat* GMsgFormats[NMTN];

#if DOCUMENT_MSG_FORMATS

#define NDT_DEFINE_ENUM_NAME(type, name, description) #name,

static const char* ItemTypeNames[] = {NETWORK_DATA_TYPES(NDT_DEFINE_ENUM_NAME)};

#define NDT_ENUM_DESCRIPTION(type, name, description) description,

static const char* ItemTypeDescriptions[] = {NETWORK_DATA_TYPES(NDT_ENUM_DESCRIPTION)};

#define NMT_ENUM_DESCRIPTION(macro, class, name, description, group) description,

static const char* MessageTypeDescriptions[] = {NETWORK_MESSAGE_TYPES(NMT_ENUM_DESCRIPTION)};

#define NMT_ENUM_GROUP(macro, class, name, description, group) #group,

static const char* MessageTypeGroups[] = {NETWORK_MESSAGE_TYPES(NMT_ENUM_GROUP)};

#include <Strings/bstring.hpp>

void WriteF(QOStream& out, int indent, const char* format, ...)
{
    va_list arglist;
    va_start(arglist, format);

    for (int i = 0; i < indent; i++)
        out.put('\t');

    BString<512> buffer;
    vsprintf(buffer, format, arglist);
    strcat(buffer, "\r\n");
    out.write(buffer, strlen(buffer));

    va_end(arglist);
}

void DocumentFormat(QOStream& out, int index)
{
    WriteF(out, 2, "<message name=\"%s\" id=\"%d\" group=\"%s\">", NetworkMessageTypeNames[index], index,
           MessageTypeGroups[index]);
    WriteF(out, 2, MessageTypeDescriptions[index]);
    const NetworkMessageFormat& format = *GMsgFormats[index];
    WriteF(out, 3, "<items>");
    for (int i = 0; i < format.NItems(); i++)
    {
        const NetworkMessageFormatItem& item = format.GetItem(i);
        WriteF(out, 4, "<item name=\"%s\" type=\"%s\">", (const char*)item.name, ItemTypeNames[item.type]);
        WriteF(out, 4, format._descriptions[i]);
        WriteF(out, 4, "</item>");
    }
    WriteF(out, 3, "</items>");
    WriteF(out, 2, "</message>");
}
#endif

// Initialization of local (static) message formats

#define NMT_DEFINE_FORMAT(macro, class, name, description, group) macro(class, name);

void InitMsgFormats()
{
    if (curMsgFormat > 0)
    {
        return;
    }
    NETWORK_MESSAGE_TYPES(NMT_DEFINE_FORMAT)
    NET_ERROR(curMsgFormat == NMTN);

#if DOCUMENT_MSG_FORMATS
    QOFStream out("messages.xml");
    WriteF(out, 0, "<?xml version=\"1.0\"?>");
    WriteF(out, 0, "<?xml-stylesheet href=\"messages.xsl\" type=\"text/xsl\"?>");

    WriteF(out, 0, "");
    WriteF(out, 0, "<root>");

    WriteF(out, 1, "<types>");
    for (int i = 0; i < sizeof(ItemTypeNames) / sizeof(*ItemTypeNames); i++)
    {
        WriteF(out, 2, "<type name=\"%s\" id=\"%d\">", ItemTypeNames[i], i);
        WriteF(out, 2, ItemTypeDescriptions[i]);
        WriteF(out, 2, "</type>");
    }
    WriteF(out, 1, "</types>");

    WriteF(out, 1, "<messages>");
    for (int i = 0; i < NMTN; i++)
        DocumentFormat(out, i);
    WriteF(out, 1, "</messages>");

    WriteF(out, 0, "</root>");
    out.close();
#endif
}

// Destroy all local (static) message formats
void DestroyMsgFormats()
{
    for (int i = 0; i < NMTN; i++)
    {
        if (GMsgFormats[i])
        {
            GMsgFormats[i]->Clear();
        }
    }
    // Reset the init guard so the next InitMsgFormats() repopulates the items. Without
    // this, a teardown (e.g. an in-process re-mount) that follows a prior network init
    // (e.g. a server-browser enumeration) leaves the formats cleared but curMsgFormat at
    // NMTN, so InitMsgFormats() early-returns and every message decodes to zero values.
    curMsgFormat = 0;
}
