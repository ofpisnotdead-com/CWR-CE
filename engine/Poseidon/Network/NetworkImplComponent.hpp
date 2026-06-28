#pragma once

#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Foundation/Common/Win.h>
#include <Poseidon/Foundation/Memory/MemHeap.hpp>
#include <Poseidon/Core/Application.hpp>

namespace Poseidon { class Command; }

#include <Poseidon/Audio/Speaker.hpp>
#include <Poseidon/AI/Path/ArcadeWaypoint.hpp>

#include <Poseidon/IO/ParamFileExt.hpp>

void DiagLogF(const char* format, ...);

// Magic for distinguish applications
#define MAGIC_APP 0x00000000

#define LOCAL_PLAYER 1
#define TO_SERVER 1

#define MAX_PLAYERS 50

extern const int DiagLevel;
bool DiagFilter(NetworkMessageType type);
extern const char* NetworkMessageTypeNames[];

// Uninitialized camera position
const Vector3 InvalidCamPos(-FLT_MAX, -FLT_MAX, -FLT_MAX);

// Network messages - extracted to NetworkMessages.hpp
#include <Poseidon/Network/NetworkMessages.hpp>

// Network Components

class NetworkManager;

// Info about file currently receiving
struct ReceivingFile
{
    // name of receiving file
    RString fileName;
    // info about which segments already received
    AutoArray<bool> fileSegments;
    // content of receiving file
    AutoArray<char> fileData;
    // total amount of bytes already received (used for statistics)
    int received;
};

#if _ENABLE_CHEATS
// Statistic info about messages of given type
struct NetworkStatisticsItem
{
    // message type
    int message;
    // count of sent messages
    int msgSent;
    // size of sent messages
    int sizeSent;
    // count of received messages
    int msgReceived;
    // size of received messages
    int sizeReceived;

    NetworkStatisticsItem()
    {
        message = 0;
        msgSent = 0;
        sizeSent = 0;
        msgReceived = 0;
        sizeReceived = 0;
    };
};
#endif

// Statistic info about messages physically send / received by players
struct NetworkRawStatisticsItem
{
    // player ID
    int player;
    // count of sent messages
    int msgSent;
    // size of sent messages
    int sizeSent;
    // count of received messages
    int msgReceived;
    // size of received messages
    int sizeReceived;

    NetworkRawStatisticsItem()
    {
        player = 0;
        msgSent = 0;
        sizeSent = 0;
        msgReceived = 0;
        sizeReceived = 0;
    };
};

// Basic network component (client or server)
class NetworkComponent : public INetworkComponent
{
  protected:
    // DirectPlay ID of attached player
    int _player;
    // creating network manager object
    NetworkManager* _parent; // _parent must be valid
    // current state of component (multiplayer game)
    NetworkGameState _state;
    // id of next created network object in this component
    int _nextId;

    // squads of logged players
    RefArray<SquadIdentity> _squads;
    // info about logged players
    AutoArray<PlayerIdentity> _identities;

    // info about current mission
    MissionHeader _missionHeader;
    // role slots
    AutoArray<PlayerRole> _playerRoles;
    // parameter of current mission
    float _param1;
    // parameter of current mission
    float _param2;

    // messages received local (inside process)
    AutoArray<NetworkLocalMessageInfo> _receivedLocalMessages;

    // files currently receiving
    AutoArray<ReceivingFile> _files;

#if _ENABLE_CHEATS
    // Statistics about messages by types
    AutoArray<NetworkStatisticsItem> _statistics;
#endif

    // Statistics about messages by players
    AutoArray<NetworkRawStatisticsItem> _rawStatistics;

  public:
    NetworkComponent(NetworkManager* parent);
    ~NetworkComponent() override;

    // Add message to local received messages
    int AddLocalMessage(int from, NetworkMessageType type, NetworkMessage* msg)
    {
        int index = _receivedLocalMessages.Add();
        _receivedLocalMessages[index].from = from;
        _receivedLocalMessages[index].type = type;
        _receivedLocalMessages[index].msg = msg;

        // if (type == 9)
        //	Log("Message %d, type %d", index, type);
        return index;
    }

    // Return DirectPlay ID of attached player
    int GetPlayer() const { return _player; }
    // Implementation of direct message send through DirectPlay
    virtual bool DXSendMsg(int to, NetworkMessageRaw& rawMsg, DWORD& msgID, NetMsgFlags dwFlags) = 0;

    // Component simulation (process received messages, send update messages etc.)
    virtual void OnSimulate() = 0;
    // Process (user) message
    virtual void OnMessage(int from, NetworkMessage* msg, NetworkMessageType type) = 0;

    // perform regular memory clean-up
    virtual unsigned CleanUpMemory() = 0;

    // Return if given client is loval on server's computer
    virtual bool IsLocalClient(int client) const { return false; }

    // DEDICATED SERVER SUPPORT
    // Called when message is received to calculate transfer rate on dedicated server
    virtual void OnMonitorIn(int size) {}

    // Hi-level send of message
    virtual DWORD SendMsg(int to, NetworkSimpleObject* object, /*int creator, int id,*/
                          NetMsgFlags dwFlags);

    // Return current state of game
    NetworkGameState GetGameState() const { return _state; }
    // Return mission header
    const MissionHeader* GetMissionHeader() const { return &_missionHeader; }
    // Return number of role slots for current mission
    int NPlayerRoles() const { return _playerRoles.Size(); }
    // Return given role slot
    const PlayerRole* GetPlayerRole(int role) const
    {
        if (role >= 0 && role < NPlayerRoles())
            return &_playerRoles[role];
        return nullptr;
    }
    // Find slot for given client
    const PlayerRole* FindPlayerRole(int player) const;
    // Return role slot for local client
    const PlayerRole* GetMyPlayerRole() const { return FindPlayerRole(_player); }
    // Search for identity with given player id
    const PlayerIdentity* FindIdentity(int dpnid) const;
    // Search for identity with given player id
    PlayerIdentity* FindIdentity(int dpnid);
    // Return array of identities
    const AutoArray<PlayerIdentity>* GetIdentities() const { return &_identities; }
    // Retrieves parameters for current mission
    void GetParams(float& param1, float& param2) const
    {
        param1 = _param1;
        param2 = _param2;
    }

    // Return component type ("server" / "client") for debugging purposes
    virtual const char* GetDebugName() const = 0;

    // Decode (and process) message from raw data
    void DecodeMessage(int from, NetworkMessageRaw& src);
    // Calculate size of message after serialization and compression
    int CalculateMessageSize(NetworkMessage* msg, NetworkMessageType type);

    // File transfer
    // Split file into segments and send segments using standard messages.
    void TransferFile(int to, RString dest, RString source);
    // Transfer player's face from server to client
    void TransferFace(int to, int player);
    // Transfer player's custom radio messages from server to client
    void TransferCustomRadio(int to, RString player);

#if _ENABLE_CHEATS
    // Write into statistics info about sent message
    void StatMsgSent(int msg, int size);
    // Write into statistics info about received message
    void StatMsgReceived(int msg, int size);
    // Return statistics about messages by types
    AutoArray<NetworkStatisticsItem>& GetStatistics() { return _statistics; }
#endif

    // Write raw statistics info about sent message
    void StatRawMsgSent(int to, int size);
    // Write raw statistics info about received message
    void StatRawMsgReceived(int from, int size);

  protected:
    // Encode message into raw data
    void EncodeMsg(NetworkMessageRaw& dst, NetworkMessage* msg, NetworkMessageFormatBase* format);
    // Encode single message item into raw data
    void EncodeMsgItem(NetworkMessageRaw& dst, NetworkMessage* msg, const RefNetworkData& val,
                       const NetworkMessageFormatItem& formatItem);
    // Decode message from raw data
    void DecodeMsg(NetworkMessage* msg, NetworkMessageRaw& src, NetworkMessageFormatBase* format);
    // Decode single message item from raw data
    RefNetworkData DecodeMsgItem(NetworkMessageRaw& src, NetworkMessage* msg, NetworkMessageFormatItem& formatItem);

    // Calculate size of message after serialization and compression
    int CalculateMsgSize(NetworkMessage* msg, NetworkMessageFormatBase* format);
    // Calculate size of single message item after serialization and compression
    int CalculateMsgItemSize(NetworkMessage* msg, const RefNetworkData& val,
                             const NetworkMessageFormatItem& formatItem);

    // Process all received messages (local, system and user)
    void ReceiveLocalMessages();

    // Send single network message
    // This function determine, whic type of send will be used - local send, agregation or send using DirectPlay
    DWORD SendMsg(int to, NetworkMessage* msg, NetworkMessageType type, NetMsgFlags dwFlags);
    // Send single network message using DirectPlay
    DWORD SendMsgRemote(int to, NetworkMessage* msg, NetworkMessageType type, NetMsgFlags dwFlags);
    // Send (part of) message queue as single DirectPlay message
    DWORD SendMsgQueue(int to, NetworkMessageQueue& queue, int begin, int end, NetMsgFlags dwFlags);
    // Lo-level data send using DirectPlay
    DWORD SendMsgRaw(int to, NetworkMessageRaw& rawMsg, NetMsgFlags dwFlags, int diagLevel);
    // Send single guaranteed network message to message queue (candidate for agregation)
    virtual void EnqueueMsg(int to, NetworkMessage* msg, NetworkMessageType type) = 0;
    // Send single nonguaranteed network message to message queue (candidate for agregation)
    virtual void EnqueueMsgNonGuaranteed(int to, NetworkMessage* msg, NetworkMessageType type) = 0;

    // Called when file segment is received
    int ReceiveFileSegment(TransferFileMessage& msg);
};

// Info about single type of update of network object
struct NetworkUpdateInfo
{
    // last created update message
    Ref<NetworkMessage> lastCreatedMsg;
    // DirectPlay ID of last created update message
    DWORD lastCreatedMsgId;
    // send time of created update message
    DWORD lastCreatedMsgTime;
    // last sent update message (sent was confirmed by DPN_MSGID_SEND_COMPLETE system message)
    Ref<NetworkMessage> lastSentMsg;
    // message can be canceled (false for initial update message)
    bool canCancel;

    // update structure - pending message send was completed
    bool OnSendComplete(bool ok);
};

// Info about local object on client
struct NetworkLocalObjectInfo //: public RefCount
{
    // ID of object
    NetworkId id;
    // object itself
    OLink<NetworkObject> object;
    // state of updates
    NetworkUpdateInfo updates[NMCUpdateN];
};

// Info about remote object on client
struct NetworkRemoteObjectInfo
{
    // ID of object
    NetworkId id;
    // object itself
    OLink<NetworkObject> object;
};

struct RespawnQueueItem;

// map sounds transferred over network to their unique ids
struct PlaySoundInfo
{
    // unique id of sound
    int creator, id;
    // sound itself
    Link<IWave> wave;
};

// map directly speaing player to object and sound buffer
struct Network3DSoundBuffer
{
    // DirectPlay ID of speaking player
    int player;
    // DirectSound buffer used for speaking player
    SRef<NetTranspSound3DBuffer> buffer;
    // sound source object
    OLink<NetworkObject> object;
};

// Class used for transfer of client info (for example camera position) to server
class ClientInfoObject : public NetworkObject
{
  protected:
    NetworkId _id;

  public:
    Vector3 _cameraPosition;

  public:
    ClientInfoObject() { _cameraPosition = InvalidCamPos; }

    void SetNetworkId(NetworkId& id) override { _id = id; }
    NetworkId GetNetworkId() const override { return _id; }
    Vector3 GetCurrentPosition() const override { return VZero; }
    bool IsLocal() const override { return true; }
    void SetLocal(bool local = true) override {}
    void DestroyObject() override {}

    NetworkMessageType GetNMType(NetworkMessageClass cls = NMCCreate) const override;
    static NetworkMessageFormat& CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format);
    TMError TransferMsg(NetworkMessageContext& ctx) override;
    float CalculateError(NetworkMessageContext& ctx) override;
    static float CalculateErrorCoef(Vector3Par position, Vector3Par cameraPosition) { return 1.0; }
    RString GetDebugName() const override;
};

struct UpdateLocalObjectInfo;

struct BodyInfo
{
    OLink<Person> body;
    Poseidon::Foundation::Time hideTime;
    float value;
};
