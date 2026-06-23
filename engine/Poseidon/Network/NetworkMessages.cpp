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

DEFINE_NET_MESSAGE(AskForSetDammage, ASK_FOR_SET_DAMMAGE_MSG)

DEFINE_NET_MESSAGE(AskForGetIn, ASK_FOR_GET_IN_MSG)

DEFINE_NET_MESSAGE(AskForGetOut, ASK_FOR_GET_OUT_MSG)

DEFINE_NET_MESSAGE(AskForChangePosition, ASK_FOR_CHANGE_POSITION_MSG)

DEFINE_NET_MESSAGE(AskForAimWeapon, ASK_FOR_AIM_WEAPON_MSG)

DEFINE_NET_MESSAGE(AskForAimObserver, ASK_FOR_AIM_OBSERVER_MSG)

DEFINE_NET_MESSAGE(AskForSelectWeapon, ASK_FOR_SELECT_WEAPON_MSG)

DEFINE_NET_MESSAGE(AskForAddImpulse, ASK_FOR_ADD_IMPULSE_MSG)

DEFINE_NET_MESSAGE(AskForMoveVector, ASK_FOR_MOVE_VECTOR_MSG)

DEFINE_NET_MESSAGE(AskForMoveMatrix, ASK_FOR_MOVE_MATRIX_MSG)

DEFINE_NET_MESSAGE(AskForJoinGroup, ASK_FOR_JOIN_GROUP_MSG)

DEFINE_NET_MESSAGE(AskForJoinUnits, ASK_FOR_JOIN_UNITS_MSG)

DEFINE_NET_MESSAGE(AskForHideBody, ASK_FOR_HIDE_BODY_MSG)

DEFINE_NET_MESSAGE(ExplosionDammageEffects, EXPLOSION_DAMMAGE_EFFECTS_MSG)

DEFINE_NET_MESSAGE(DeleteObject, DELETE_OBJECT_MSG)

DEFINE_NET_MESSAGE(DeleteCommand, DELETE_COMMAND_MSG)

DEFINE_NET_MESSAGE(AskForAmmo, ASK_FOR_AMMO_MSG)

DEFINE_NET_NESSAGE(FireWeapon, FIRE_WEAPON_MSG)

IndicesUpdateWeapons::IndicesUpdateWeapons()
{
    vehicle = -1;
    IndicesUpdateEntityAIWeapons* GetIndicesUpdateEntityAIWeapons();
    weapons = GetIndicesUpdateEntityAIWeapons();
}

IndicesUpdateWeapons::~IndicesUpdateWeapons()
{
    void DeleteIndicesUpdateEntityAIWeapons(IndicesUpdateEntityAIWeapons * weapons);
    DeleteIndicesUpdateEntityAIWeapons(weapons);
}

void IndicesUpdateWeapons::Scan(NetworkMessageFormatBase* format)
{
    SCAN(vehicle)
    void ScanIndicesUpdateEntityAIWeapons(IndicesUpdateEntityAIWeapons * weapons, NetworkMessageFormatBase * format);
    ScanIndicesUpdateEntityAIWeapons(weapons, format);
}

// Create network message indices for UpdateWeaponsMessage class
NetworkMessageIndices* GetIndicesUpdateWeapons()
{
    return new IndicesUpdateWeapons();
}

NetworkMessageFormat& UpdateWeaponsMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("vehicle", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Vehicle to update"));
    EntityAI::CreateFormatWeapons(format);
    return format;
}

TMError UpdateWeaponsMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesUpdateWeapons*>(ctx.GetIndices()))
    const IndicesUpdateWeapons* indices = static_cast<const IndicesUpdateWeapons*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransferRef(indices->vehicle, vehicle))
    if (vehicle)
        TMCHECK(vehicle->TransferMsgWeapons(ctx, indices->weapons))
    return TMOK;
}

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

DEFINE_NET_MESSAGE(MarkerDelete, MARKER_DELETE_MSG)

IndicesMarkerCreate::IndicesMarkerCreate()
{
    channel = -1;
    sender = -1;
    units = -1;

    IndicesMarker* GetIndicesMarker();
    marker = GetIndicesMarker();
}

IndicesMarkerCreate::~IndicesMarkerCreate()
{
    void DeleteIndicesMarker(IndicesMarker * marker);
    DeleteIndicesMarker(marker);
}

void IndicesMarkerCreate::Scan(NetworkMessageFormatBase* format)
{
    SCAN(channel)
    SCAN(sender)
    SCAN(units)

    void ScanIndicesMarker(IndicesMarker * marker, NetworkMessageFormatBase * format);
    ScanIndicesMarker(marker, format);
}

// Create network message indices for MarkerCreateMessage class
NetworkMessageIndices* GetIndicesMarkerCreate()
{
    return new IndicesMarkerCreate();
}

NetworkMessageFormat& MarkerCreateMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("channel", NDTInteger, NCTSmallSigned, DEFVALUE(int, 0),
               DOC_MSG("Chat channel (who will see the marker)"));
    format.Add("sender", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Sender unit"));
    format.Add("units", NDTRefArray, NCTNone, DEFVALUEREFARRAY, DOC_MSG("List of receiving units"));
    ArcadeMarkerInfo::CreateFormat(format);
    return format;
}

TMError MarkerCreateMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesMarkerCreate*>(ctx.GetIndices()))
    const IndicesMarkerCreate* indices = static_cast<const IndicesMarkerCreate*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransfer(indices->channel, channel))
    TMCHECK(ctx.IdxTransferRef(indices->sender, sender))
    TMCHECK(ctx.IdxTransferRefs(indices->units, units))
    return marker.TransferMsg(ctx, indices->marker);
}

// network message indices for NetworkCommandMessage class
class IndicesNetworkCommand : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int type;
    int content;

    IndicesNetworkCommand();
    NetworkMessageIndices* Clone() const override { return new IndicesNetworkCommand; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesNetworkCommand::IndicesNetworkCommand()
{
    type = -1;
    content = -1;
}

void IndicesNetworkCommand::Scan(NetworkMessageFormatBase* format){SCAN(type) SCAN(content)}

// Create network message indices for NetworkCommandMessage class
NetworkMessageIndices* GetIndicesNetworkCommand()
{
    return new IndicesNetworkCommand();
}

NetworkMessageFormat& NetworkCommandMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("type", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Type of command"));
    format.Add("content", NDTRawData, NCTNone, DEFVALUERAWDATA, DOC_MSG("Parameters of command"));
    return format;
}

TMError NetworkCommandMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesNetworkCommand*>(ctx.GetIndices()))
    const IndicesNetworkCommand* indices = static_cast<const IndicesNetworkCommand*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransfer(indices->type, (int&)type))
    TMCHECK(ctx.IdxTransfer(indices->content, content))
    return TMOK;
}

DEFINE_NET_MESSAGE(IntegrityQuestion, INTEGRITY_QUESTION_MSG)

DEFINE_NET_MESSAGE(IntegrityAnswer, INTEGRITY_ANSWER_MSG)

// network message indices for PlayerStateMessage class
class IndicesPlayerState : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int player;
    int state;

    IndicesPlayerState();
    NetworkMessageIndices* Clone() const override { return new IndicesPlayerState; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesPlayerState::IndicesPlayerState()
{
    player = -1;
    state = -1;
}

void IndicesPlayerState::Scan(NetworkMessageFormatBase* format){SCAN(player) SCAN(state)}

// Create network message indices for PlayerStateMessage class
NetworkMessageIndices* GetIndicesPlayerState()
{
    return new IndicesPlayerState();
}

NetworkMessageFormat& PlayerStateMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("player", NDTInteger, NCTNone, DEFVALUE(int, AI_PLAYER), DOC_MSG("Client (player) ID"));
    format.Add("state", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, NGSNone), DOC_MSG("New state of player"));
    return format;
}

TMError PlayerStateMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesPlayerState*>(ctx.GetIndices()))
    const IndicesPlayerState* indices = static_cast<const IndicesPlayerState*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransfer(indices->player, (int&)player))
    TMCHECK(ctx.IdxTransfer(indices->state, (int&)state))
    return TMOK;
}

DEFINE_NET_MESSAGE(AttachPerson, ATTACH_PERSON_MSG)

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

IndicesSetFlagOwner::IndicesSetFlagOwner()
{
    owner = -1;
    carrier = -1;
}

void IndicesSetFlagOwner::Scan(NetworkMessageFormatBase* format){SCAN(owner) SCAN(carrier)}

// Create network message indices for SetFlagOwnerMessage class
NetworkMessageIndices* GetIndicesSetFlagOwner()
{
    return new IndicesSetFlagOwner();
}

// Create network message indices for SetFlagCarrierMessage class
NetworkMessageIndices* GetIndicesSetFlagCarrier()
{
    return new IndicesSetFlagOwner();
}

NetworkMessageFormat& SetFlagOwnerMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("owner", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Flag owner"));
    format.Add("carrier", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Flag carrier"));
    return format;
}

TMError SetFlagOwnerMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesSetFlagOwner*>(ctx.GetIndices()))
    const IndicesSetFlagOwner* indices = static_cast<const IndicesSetFlagOwner*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransferRef(indices->owner, owner))
    TMCHECK(ctx.IdxTransferRef(indices->carrier, carrier))
    return TMOK;
}

// network message indices for PlayerIdentity class
class IndicesLogin : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int dpnid;
    int playerid;
    int id;
    int name;
    int face;
    int glasses;
    int speaker;
    int pitch;
    int squad;
    int fullname;
    int email;
    int icq;
    int remark;
    int state;
    int version;

    IndicesLogin();
    NetworkMessageIndices* Clone() const override { return new IndicesLogin; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesLogin::IndicesLogin()
{
    dpnid = -1;
    playerid = -1;
    id = -1;
    name = -1;
    face = -1;
    glasses = -1;
    speaker = -1;
    pitch = -1;
    squad = -1;
    fullname = -1;
    email = -1;
    icq = -1;
    remark = -1;
    state = -1;
    version = -1;
}

void IndicesLogin::Scan(NetworkMessageFormatBase* format){
    SCAN(dpnid) SCAN(playerid) SCAN(id) SCAN(name) SCAN(face) SCAN(glasses) SCAN(speaker) SCAN(pitch) SCAN(squad)
        SCAN(fullname) SCAN(email) SCAN(icq) SCAN(remark) SCAN(state) SCAN(version)}

IndicesPlayerUpdate::IndicesPlayerUpdate()
{
    dpnid = -1;
    minPing = -1;
    avgPing = -1;
    maxPing = -1;
    minBandwidth = -1;
    avgBandwidth = -1;
    maxBandwidth = -1;
    desync = -1;
    rights = -1;
}

void IndicesPlayerUpdate::Scan(NetworkMessageFormatBase* format)
{
    SCAN(dpnid);
    SCAN(minPing)
    SCAN(avgPing)
    SCAN(maxPing)
    SCAN(minBandwidth)
    SCAN(avgBandwidth)
    SCAN(maxBandwidth)
    SCAN(desync)
    SCAN(rights)
}

// Create network message indices for IndicesPlayerUpdate class
NetworkMessageIndices* GetIndicesLogin()
{
    return new IndicesLogin();
}

// Create network message indices for IndicesPlayerUpdate class position update
NetworkMessageIndices* GetIndicesPlayerUpdate()
{
    return new IndicesPlayerUpdate();
}

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
            format.Add("dpnid", NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("Client (player) ID"));
            format.Add("minPing", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 10), DOC_MSG("Ping range estimation"),
                       ET_ABS_DIF, ERR_COEF_VALUE_MINOR);
            format.Add("avgPing", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 100), DOC_MSG("Ping range estimation"),
                       ET_ABS_DIF, ERR_COEF_VALUE_MAJOR);
            format.Add("maxPing", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 1000), DOC_MSG("Ping range estimation"),
                       ET_ABS_DIF, ERR_COEF_VALUE_MINOR);
            format.Add("minBandwidth", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 2),
                       DOC_MSG("Bandwidth estimation (in kbps)"), ET_ABS_DIF, ERR_COEF_VALUE_MINOR);
            format.Add("avgBandwidth", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 14),
                       DOC_MSG("Bandwidth estimation (in kbps)"), ET_ABS_DIF, ERR_COEF_VALUE_MINOR);
            format.Add("maxBandwidth", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 28),
                       DOC_MSG("Bandwidth estimation (in kbps)"), ET_ABS_DIF, ERR_COEF_VALUE_MINOR);
            format.Add("desync", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0),
                       DOC_MSG("Current desync level (max. error of unsent messages)"), ET_ABS_DIF,
                       ERR_COEF_VALUE_MAJOR);
            format.Add("rights", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0),
                       DOC_MSG("Special rights of given player"), ET_ABS_DIF, ERR_COEF_VALUE_MAJOR);
            return format;
        default:
            format.Add("dpnid", NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("Client (player) ID"));
            format.Add("playerid", NDTInteger, NCTNone, DEFVALUE(int, 0),
                       DOC_MSG("ID unique in session (shorter than dpnid)"));
            format.Add("id", NDTString, NCTNone, DEFVALUE(RString, ""),
                       DOC_MSG("Unique id of player (derivated from CD key)"));
            format.Add("name", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Nick (short) name of player"));
            format.Add("face", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Selected face"));
            format.Add("glasses", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Selected glasses"));
            format.Add("speaker", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Selected speaker"));
            format.Add("pitch", NDTFloat, NCTNone, DEFVALUE(float, 1.0f), DOC_MSG("Selected voice pitch"));
            format.Add("squad", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("unique id (URL) of squad"));
            format.Add("fullname", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Full name of player"));
            format.Add("email", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("E-mail of player"));
            format.Add("icq", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("ICQ of player"));
            format.Add("remark", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Remark about player"));
            format.Add("state", NDTInteger, NCTNone, DEFVALUE(int, NGSNone),
                       DOC_MSG("State of player's network client"));
            format.Add("version", NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("Version player is using"));
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

// network message indices for SquadIdentity class
class IndicesSquad : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int id;
    int nick;
    int name;
    int email;
    int web;
    int picture;
    int title;

    IndicesSquad();
    NetworkMessageIndices* Clone() const override { return new IndicesSquad; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesSquad::IndicesSquad()
{
    id = -1;
    nick = -1;
    name = -1;
    email = -1;
    web = -1;
    picture = -1;
    title = -1;
}

void IndicesSquad::Scan(NetworkMessageFormatBase* format){SCAN(id) SCAN(nick) SCAN(name) SCAN(email) SCAN(web)
                                                              SCAN(picture) SCAN(title)}

// Create network message indices for SquadIdentity class
NetworkMessageIndices* GetIndicesSquad()
{
    return new IndicesSquad();
}

NetworkMessageFormat& SquadIdentity::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("id", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Unique id of squad (URL of XML page)"));
    format.Add("nick", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Nick (short) name of squad"));
    format.Add("name", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Full name of squad"));
    format.Add("email", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("E-mail of squad administrator"));
    format.Add("web", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Web page of squad"));
    format.Add("picture", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Picture of squad (shown on vehicles)"));
    format.Add("title", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Title of squad (shown on vehicles)"));
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

DEFINE_NET_MESSAGE(ShowGroupDir, SHOW_GROUP_DIR_MSG)

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
