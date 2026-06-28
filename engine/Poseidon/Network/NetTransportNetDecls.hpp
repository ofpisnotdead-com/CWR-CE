#pragma once
#include <Poseidon/Foundation/PoseidonPCH.hpp>
#include <Poseidon/Network/NetTransport.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/Core/Application.hpp>

#include <Poseidon/Foundation/Memory/MemHeap.hpp>
#include <Poseidon/Foundation/Strings/StrFormat.hpp>
#include <Poseidon/Network/Legacy/netpch.hpp>
#include <Poseidon/Network/RateLimit.hpp>
#include <Poseidon/Audio/Voice/VonApp.hpp>
#include <Poseidon/Audio/Voice/VonSpeaker.hpp>
#include <Poseidon/Audio/Voice/OpusCodec.hpp>
#include <Poseidon/Audio/Voice/VonNet.hpp>
#include <Poseidon/Network/Legacy/PeerFactory.hpp>
#include <Poseidon/Network/Legacy/NetPeer.hpp>
#include <Poseidon/Network/Legacy/NetChannel.hpp>

#ifndef DPNID_ALL_PLAYERS_GROUP
#define DPNID_ALL_PLAYERS_GROUP 0
#endif

#define MAX_SESSIONS 16
#define LEN_SESSION_NAME 256
#define LEN_MISSION_NAME 40

#define NUM_PORTS_TO_TRY 15
#define PORT_INTERVAL 12
#define UNIQUE_TO_TRY 12

#define LEN_PLAYER_NAME 40
#define LEN_PASSWORD_NAME 40

#undef DEDICATED_STAT_LOG

#ifdef DEDICATED_STAT_LOG
#define CONSOLE_LOG_INTERVAL 2000000
#endif

#include <Poseidon/Network/NetworkConfig.hpp>

using Poseidon::Foundation::ExplicitMap;
using Poseidon::Foundation::getSystemTime;
using Poseidon::Foundation::IteratorState;
using Poseidon::Foundation::MemAllocSA;
using Poseidon::Foundation::MemAllocSafe;
using Poseidon::Foundation::nsNoMoreCallbacks;
using Poseidon::Foundation::nsOK;
using Poseidon::Foundation::nsOutputSent;
using Poseidon::Foundation::PoCriticalSection;
using Poseidon::Foundation::SafeMemoryCleanUp;

namespace Poseidon
{
extern RString GetPidFileName();
}

namespace Poseidon
{

#ifdef NET_LOG_TRANSP_STAT
#define STAT_LOG_INTERVAL 80000
#endif

// Maximum enumeration message age (in milliseconds).
#define MAX_ENUM_AGE 15000
// Minimum enumeration retry time (in milliseconds).
#define MIN_ENUM_RETRY 4000
// Maximum time to wait for AckPlayer response (in milliseconds).
#define ACK_PLAYER_TIMEOUT 8000
// Re-send time for MAGIC_CREATE_PLAYER and MAGIC_RECONNECT_PLAYER (in milliseconds).
#define CREATE_PLAYER_RESEND 2000
// Time interval to check network response (NetClient::Init(), in milliseconds).
#define NET_CHECK_WAIT 100
// Time to wait in network destructors (for last message departure, in milliseconds).
#define DESTRUCT_WAIT 500
// Time to wait for the higher-layer to process player-delete operation (in milliseconds).
#define DESTROY_WAIT 100
// Send-timeout for common messages (in micro-seconds).
#define SEND_TIMEOUT 800000

// Tag for messages starting with 32-bit "magic" number.
#define MSG_MAGIC_FLAG 0x0001

// Define this symbol if both legacy and new enumeration responses will be sent.
// #define ENUM_LEGACY

// Magic numbers, little-endian on the wire.

// Enumeration request (client:broadcast -> server:broadcast).
#define MAGIC_ENUM_REQUEST 0xeee191ae
// Enumeration response (server:broadcast -> client:broadcast).
#define MAGIC_ENUM_RESPONSE 0xfff1e8ac
// Enumeration response - legacy version (server:broadcast -> client:broadcast).
#define MAGIC_ENUM_RESPONSE_LEGACY 0xfff4fe12
// CreatePlayer request (client:normal -> server:broadcast). Not VIM. Duplicate messages should be ignored.
#define MAGIC_CREATE_PLAYER 0xccca1e12
// AckPlayer response (server:both -> client:normal). I need some acknowledgement for broadcast sender (in far future).
#define MAGIC_ACK_PLAYER 0xaaa51a7e
// ReconnectPlayer request (client:normal -> server:broadcast).
#define MAGIC_RECONNECT_PLAYER 0x111044ec
// Terminate the session (server:normal -> clients:normal). Not VIM.
#define MAGIC_TERMINATE_SESSION 0x777814a1
// DestroyPlayer message (client:normal -> server:normal). Not VIM.
#define MAGIC_DESTROY_PLAYER 0xddd15072

#pragma pack(push, netPackets, 1)

// Universal packet starting with 32-bit magic number.
struct MagicPacket
{
    // Magic number (MAGIC_*).
    unsigned32 magic;

} PACKED;

// Enumeration request (will be sent through network).
struct EnumPacket : public MagicPacket
{
    // Application-specific magic number.
    int32 magicApplication;

} PACKED;

// Session description packet (will be sent through network).
struct SessionPacket : public MagicPacket
{
    // Name of sesion.
    char name[LEN_SESSION_NAME];

    // Current version of application on host.
    int32 actualVersion;

    // Required version of application on host.
    int32 requiredVersion;

    // Name of running mission on host.
    char mission[LEN_MISSION_NAME];

    // State of game on host.
    int32 gameState;

    // Maximal number of players + 2 (or 0 if not restricted)
    int32 maxPlayers;

    // Password is required.
    int16 password;

    // Receiving port.
    int16 port;

    // Actual number of players.
    int32 numPlayers;

    // Serial number of enumeration request.
    MsgSerial request;

    // list of mods
    char mod[MOD_LENGTH];

    // equal mod are required for clients
    int16 equalModRequired;

    /// build version tag (must match to join)
    char versionTag[VERSION_TAG_LENGTH];

} PACKED;

const size_t SESSION_PACKET_1 = offsetof(SessionPacket, numPlayers); // LEGACY_SESSION_PACKET
const size_t SESSION_PACKET_2 = offsetof(SessionPacket, mod);
const size_t SESSION_PACKET_3 = offsetof(SessionPacket, versionTag);
const size_t SESSION_PACKET_4 = sizeof(SessionPacket);
const size_t SESSION_PACKET_SIZE = SESSION_PACKET_4;

// Description of single session (will be stored within enumeration object).
struct NetSessionDescription : public SessionPacket
{
    // Full address (string format).
    char address[32];

    // IP adress in network format.
    unsigned32 ip;

    // Last update time in milliseconds.
    unsigned32 lastTime;

    // Actual ping (RTT) time.
    unsigned32 pingTime;

} PACKED;

// Create player packet (will be sent from NetTranspClient to NetTranspServer).
struct CreatePlayerPacket : public MagicPacket
{
    // Application-specific magic number.
    int32 magicApplication;

    char name[LEN_PLAYER_NAME];

    char password[LEN_PASSWORD_NAME];

    char mod[MOD_LENGTH];

    int32 actualVersion;

    int32 requiredVersion;

    int16 botClient;

    int32 uniqueID; // Unique player ID (derived from system time and acknowledged by the server).

    char versionTag[VERSION_TAG_LENGTH];

} PACKED;

// Server response to CreatePlayer message (AckPlayer)
struct AckPlayerPacket : public MagicPacket
{
    // Connection result (enum ConnectResult)
    int32 result;

    // Player number (for future reconnection).
    int32 playerNo;

} PACKED;

// Reconnect player packet (sent from NetTranspClient to NetTranspServer).
struct ReconnectPlayerPacket : public MagicPacket
{
    // Player number (from first player acknowledgement).
    int32 playerNo;

} PACKED;

#pragma pack(pop, netPackets)

// Container for list of sessions.
class NetSessionDescriptions
{
  protected:
    // Current number of contained sessions.
    int _size;

    // Sessions.
    NetSessionDescription _data[MAX_SESSIONS];

  public:
    NetSessionDescriptions() { _size = 0; }

    ~NetSessionDescriptions() { Clear(); }

    // Return current number of contained sessions
    int Size() { return _size; }

    // Add a new session; returns its index, or -1 if the container is full.
    int Add();

    // Delete one session.
    void Delete(int i);

    // Delete all sessions.
    void Clear();

    // R/W access to single session.
    NetSessionDescription& operator[](int i) { return _data[i]; }

    // R/O access to single session.
    const NetSessionDescription& operator[](int i) const { return _data[i]; }
};

// Net implementation of NetTranspSessionEnum class
class NetSessionEnum : public NetTranspSessionEnum
{
  protected:
    // List of enumerated sessions.
    NetSessionDescriptions _sessions;

    // Critical section for _sessions.
    PoCriticalSection _cs;

    // Enter the critical section.
    inline void enter() { _cs.enter(); }

    // Leave the critical section.
    inline void leave() { _cs.leave(); }

    // Enumeration is running.
    bool _running;

    // Magic number for application separation.
    int32 magicApp;

    // Send a batch of requests to the given host. br is an existing, configured
    // broadcast channel; addr is the host IP (network endian); port is the first
    // port to try (host endian).
    void sendRequest(NetChannel* br, struct sockaddr_in& addr, unsigned16 port);

    // True if the address still needs requesting (for enumeration retries).
    // addr is the host IP (network endian); port the first port to try (host endian).
    bool needsRequest(struct sockaddr_in& addr, unsigned16 port);

    friend NetStatus enumReceive(NetMessage* msg, NetStatus event, void* data);

  public:
    NetSessionEnum();

    ~NetSessionEnum() override;

    // Initializes enumeration data.
    bool Init(int magic = 0) override;

    // Releases enumeration data.
    virtual void Done();

    RString IPToGUID(RString ip, int port) override;

    // Session enumeration is running?
    bool RunningEnumHosts() override { return _running; }

    // Start enumerating remote sessions. ip is a distant IP to try (nullptr for
    // broadcast only); port the distant port; hosts an array of distant hosts to
    // try (nullptr if none).
    bool StartEnumHosts(RString ip, int port, AutoArray<RemoteHostAddress>* hosts) override;

    // Cancel session enumeration.
    void StopEnumHosts() override;

    // Number of actually enumerated sessions.
    int NSessions() override;

    // Retrieve all enumerated sessions.
    void GetSessions(AutoArray<SessionInfo>& sessions) override;
};

// Interface for network transport client class.
class NetClient : public NetTranspClient
{
  protected:
    // Last reported message time in seconds.
    unsigned64 lastMsgReported;

    // Associated communication channel (peer-to-peer point).
    Ref<NetChannel> channel;

    // AckPlayer result (CRNone if not finished yet).
    int ackPlayer;

    // PlayerNo (playerID on the server - for reconnection purposes).
    int playerNo;

    // Am I bot-client?
    bool amIBot;

    // The session was terminated (by the server or a channel drop).
    bool sessionTerminated;

    // Why the session was terminated.
    NetTerminationReason whySessionTerminated;

    // Linked list of received messages (can be nullptr). Sorted by serial number.
    Ref<NetMessage> received;

    // Inserts a new received message into 'received' sorted list..
    void insertReceived(NetMessage* msg);

    // Linked list of received message-parts to be merged.
    Ref<NetMessage> split;

    // -> last received split message-part.
    NetMessage* lastSplit;

    // Linked list of received message-parts to be merged.
    Ref<NetMessage> splitUrgent;

    // -> last received split message-part.
    NetMessage* lastSplitUrgent;

    // Critical section for received.
    PoCriticalSection rcvCs;

    // Enter the critical section for received messages.
    inline void enterRcv() { rcvCs.enter(); }

    // Leave the critical section for received messages.
    inline void leaveRcv() { rcvCs.leave(); }

    // Linked list of sent messages (can be nullptr).
    Ref<NetMessage> sent;

    // Critical section for sent.
    PoCriticalSection sndCs;

    // Enter the critical section for sent messages.
    inline void enterSnd() { sndCs.enter(); }

    // Leave the critical section for sent messages.
    inline void leaveSnd() { sndCs.leave(); }

    // Magic number for application separation.
    int32 magicApp;

    friend NetStatus clientReceive(NetMessage* msg, NetStatus event, void* data);
    friend NetStatus enumReceive(NetMessage* msg, NetStatus event, void* data);
    friend NetStatus clientSendComplete(NetMessage* msg, NetStatus event, void* data);

#ifdef NET_LOG_INFO
    int oldLatency;
    int oldThroughput;
    bool printAge;
#endif

#ifdef NET_LOG_TRANSP_STAT
    unsigned64 nextStatLog;
#endif

    VoNSystem _vonSystem;
    std::unordered_map<int, bool> _vonPlaying; // player → playing state
    std::unordered_map<uint32_t, std::unique_ptr<VoNSpeaker>> _vonSpeakers;

  public:
    // Does virtually no initialization.
    NetClient();

    ~NetClient() override;

    // Initialize the client. address is "server.domain.tld:port"; port is the
    // remote port and receives the actual port used; versionInfo is the remote
    // session version; magic separates different applications.
    ConnectResult Init(RString address, RString password, bool botClient, int& port, RString player,
                       MPVersionInfo& versionInfo, int magic = 0) override;

    // Reconnect the client after an IP:port change (re-dial).
    virtual ConnectResult ReInit();

    // Voice client init (dummy).
    bool InitVoice() override;
    bool IsVoicePlaying(int player) override;
    bool IsVoiceRecording() override;
    NetTranspSound3DBuffer* Create3DSoundBuffer(int player) override;
    void SetVoiceChannel(int channel) override;
    void SetVoiceTransmit(bool on) override;
    int SendVoiceTestTone(int frames, int amplitude) override;

    // Sends the given user (raw) message to server.
    bool SendMsg(BYTE* buffer, int bufferSize, DWORD& msgID, NetMsgFlags flags) override;

    // Gets actual send-queue statistics.
    void GetSendQueueInfo(int& nMsg, int& nBytes, int& nMsgG, int& nBytesG) override;

    // Gets actual I/O channel statistics.
    bool GetConnectionInfo(int& latencyMS, int& throughputBPS) override;

    // Sets internal params for underlying NetChannel object.
    void SetNetworkParams(const ParamEntry& cfg) override;

    // Last received message's age in seconds.
    float GetLastMsgAge() override;

    // Last received message's age in seconds (used to eliminate "disconnect cheat")
    float GetLastMsgAgeReliable() override { return GetLastMsgAge(); }

    // Last reported message's age in seconds.
    float GetLastMsgAgeReported() override;

    // Remember time of reported message.
    void LastMsgAgeReported() override;

    bool IsSessionTerminated() override;

    NetTerminationReason GetWhySessionTerminated() override;

    // Processes received user messages.
    void ProcessUserMessages(UserMessageClientCallback* callback, void* context) override;

    // Removes all received user messages.
    void RemoveUserMessages() override;

    // Processes sent (and acknowledged) user messages.
    void ProcessSendComplete(SendCompleteCallback* callback, void* context) override;

    // Removes all sent messages.
    void RemoveSendComplete() override;

    RString GetStatistics() override;

    unsigned FreeMemory() override;
};

// Empty key value (internal).
namespace Foundation
{
template <>
inline int ExplicitMapTraits<int, RefD<NetChannel>>::keyNull = 0;

// For removed values (internal).
template <>
inline int ExplicitMapTraits<int, RefD<NetChannel>>::zombie = -1;
} // namespace Foundation

class ChannelSupport
{
  public:
    // Player (channel) id.
    int m_id;

    // Linked list of received message-parts to be merged.
    Ref<NetMessage> m_split;

    // -> last received split message-part.
    NetMessage* m_lastSplit;

    // Linked list of received urgent message-parts to be merged.
    Ref<NetMessage> m_splitUrgent;

    // -> last received urgent split message-part.
    NetMessage* m_lastSplitUrgent;

    ChannelSupport()
    {
        m_id = 0;
        m_lastSplit = m_lastSplitUrgent = nullptr;
    }

    ChannelSupport(const ChannelSupport& from)
    {
        m_id = from.m_id;
        m_split = from.m_split;
        m_lastSplit = from.m_lastSplit;
        m_splitUrgent = from.m_splitUrgent;
        m_lastSplitUrgent = from.m_lastSplitUrgent;
    }

    bool operator==(const ChannelSupport& sec) const { return (m_id == sec.m_id); }
};

namespace Foundation
{
template <>
inline unsigned64 ExplicitMapTraits<unsigned64, ChannelSupport>::keyNull = (unsigned64)-1;
template <>
inline unsigned64 ExplicitMapTraits<unsigned64, ChannelSupport>::zombie = (unsigned64)-2;
template <>
inline ChannelSupport ExplicitMapTraits<unsigned64, ChannelSupport>::null = ChannelSupport();

// Declared here, instantiated once in NetTransportNet.cpp, so this header stays safe
// to include from more than one translation unit.
extern template struct ExplicitMapTraits<unsigned64, ChannelSupport>;
extern template class ExplicitMap<unsigned64, ChannelSupport, true, MemAllocSafe>;
} // namespace Foundation

// Socket implementation for network transport server class.
class NetServer : public NetTranspServer
{
  protected:
    // Session password
    RString _password;

    // Session info in network format.
    SessionPacket session;

    // Critical section for user map, session info, added & deleted users.
    PoCriticalSection usrCs;

    // Enter the critical section for user map.
    inline void enterUsr() { usrCs.enter(); }

    // Leave the critical section for user map.
    inline void leaveUsr() { usrCs.leave(); }

    // Port on which the session is running.
    unsigned16 sessionPort;

    // Name of session (contains name of player and IP address of host).
    RString sessionName;

    // Linked list of received messages (can be nullptr). Sorted by serial number.
    Ref<NetMessage> received;

    // Inserts a new received message into 'received' sorted list..
    void insertReceived(NetMessage* msg);

    // Split lists for individual players, etc.
    ExplicitMap<unsigned64, ChannelSupport, true, MemAllocSafe> m_support;

    // Critical section for received.
    PoCriticalSection rcvCs;

    // Enter the critical section for received messages.
    inline void enterRcv() { rcvCs.enter(); }

    // Leave the critical section for received messages.
    inline void leaveRcv() { rcvCs.leave(); }

    // Linked list of sent messages (can be nullptr).
    Ref<NetMessage> sent;

    // Critical section for sent.
    PoCriticalSection sndCs;

    // Enter the critical section for sent messages.
    inline void enterSnd() { sndCs.enter(); }

    // Leave the critical section for sent messages.
    inline void leaveSnd() { sndCs.leave(); }

    // Mapping from user ID# to NetChannels.
    ExplicitMap<int, RefD<NetChannel>, true, MemAllocSafe> users;

    // ID of 'bot' client or 0.
    int botId;

    // Info about new players.
    AutoArray<CreatePlayerInfo, MemAllocSafe> _createPlayers;

    // Info about removed players.
    AutoArray<DeletePlayerInfo, MemAllocSafe> _deletePlayers;

    // Magic number for application separation.
    int32 magicApp;

    // Rate limiter for reflected enum replies, so the server's reply traffic stays
    // bounded. Accessed only under usrCs.
    RateLimit::TokenBucket _enumReplyBucket;

    // Map a NetChannel to its player ID, or -1 if not found.
    int channelToPlayer(NetChannel* ch);

    friend NetStatus ctrlReceive(NetMessage* msg, NetStatus event, void* data);
    friend NetStatus serverReceive(NetMessage* msg, NetStatus event, void* data);
    friend NetStatus destroyPlayerCallback(NetMessage* msg, NetStatus event, void* data);
    friend NetStatus serverSendComplete(NetMessage* msg, NetStatus event, void* data);

    // Complete player-destroy sequence (with notification message to the player).
    void destroyPlayer(NetChannel* ch, NetTerminationReason reason);

    // Final player-destroying (w/o notification message to the player).
    void finishDestroyPlayer(int player);

    // diagnostics - log current state of "users" variable
    void logUsers();

#ifdef NET_LOG_INFO
    int oldLatency;
    int oldThroughput;
#endif

#ifdef DEDICATED_STAT_LOG
    unsigned64 nextConsoleLog;
#endif

#ifdef NET_LOG_TRANSP_STAT
    unsigned64 nextStatLog;
    bool forceLog;
    bool inGetConnection;
#endif

    VoNSystem _vonSystem;
    std::unordered_map<int, std::vector<int>> _vonTargets; // sender → targets

  public:
    // Does virtually no initialization.
    NetServer();

    ~NetServer() override;

    // Initialize the server. port is the local port to work with.
    bool Init(int port, RString password, char* hostname, int maxPlayers, RString sessionNameInit,
              RString sessionNameFormat, RString playerName, MPVersionInfo& versionInfo, bool equalModRequired,
              int magic = 0) override;

    // Voice server init (dummy).
    bool InitVoice() override;
    void ProcessVoicePlayers(CreateVoicePlayerCallback* callback, void* context) override;
    void RemoveVoicePlayers() override;
    void GetTransmitTargets(int from, AutoArray<int, MemAllocSA>& to) override;
    void SetTransmitTargets(int from, AutoArray<int, MemAllocSA>& to, int channel) override;

    int GetSessionPort() override { return sessionPort; }

    RString GetSessionName() override { return sessionName; }

    bool SendMsg(int to, BYTE* buffer, int bufferSize, DWORD& msgID, NetMsgFlags flags) override;

    void CancelAllMessages() override;

    void GetSendQueueInfo(int to, int& nMsg, int& nBytes, int& nMsgG, int& nBytesG) override;

    bool GetConnectionInfo(int to, int& latencyMS, int& throughputBPS) override;

    // Last received message's age in seconds (used to eliminate "disconnect cheat")
    float GetLastMsgAgeReliable(int player) override;

    // Sets internal params for underlying NetChannel object.
    void SetNetworkParams(const ParamEntry& cfg) override;

    void UpdateSessionDescription(int state, RString mission) override;

    bool GetURL(char* address, DWORD addressLen) override;

    void KickOff(int player, NetTerminationReason reason) override;
    RString GetPlayerHostIP(int player) override;

    void ProcessUserMessages(UserMessageServerCallback* callback, void* context) override;

    void RemoveUserMessages() override;

    void ProcessSendComplete(SendCompleteCallback* callback, void* context) override;

    void RemoveSendComplete() override;

    void ProcessPlayers(CreatePlayerCallback* callbackCreate, DeletePlayerCallback* callbackDelete,
                        void* context) override;

    void RemovePlayers() override;

    RString GetStatistics(int player) override;

    unsigned FreeMemory() override;
};

static void LoadNetworkParams(NetworkParams& data, const ParamEntry& cfg)
{
    data = defaultNetworkParams;
    if (cfg.FindEntry("sockets"))
    {
        const ParamEntry& c = cfg >> "sockets";

#define GET_PAR(x)       \
    if (c.FindEntry(#x)) \
    data.x = c >> #x
#define GET_PAR_U(x)     \
    if (c.FindEntry(#x)) \
    data.x = (int)(c >> #x)

        GET_PAR(winsockVersion);
        GET_PAR_U(rcvBufSize);
        GET_PAR_U(maxPacketSize);
        GET_PAR_U(dropGap);
        GET_PAR_U(ackTimeoutA); // Multiplicative coefficient for actual ack-timeout computation.
        GET_PAR_U(ackTimeoutB); // Additive coefficient for actual ack-timeout computation (in microseconds).
        GET_PAR_U(ackRedundancy);
        GET_PAR_U(initBandwidth);
        GET_PAR_U(minBandwidth);
        GET_PAR_U(maxBandwidth);
        GET_PAR_U(minActivity);
        GET_PAR_U(initLatency);
        GET_PAR_U(minLatencyUpdate);
        GET_PAR(minLatencyMul);
        GET_PAR_U(minLatencyAdd);
        GET_PAR(goodAckBandFade);
        GET_PAR_U(outWindow);
        GET_PAR_U(ackWindow);
        GET_PAR_U(maxChannelBitMask);
        GET_PAR(lostLatencyMul);
        GET_PAR(lostLatencyAdd);
        GET_PAR_U(maxOutputAckMask);
        GET_PAR_U(minAckHistory);
        GET_PAR(maxDropouts);
        GET_PAR(midDropouts);
        GET_PAR(okDropouts);
        GET_PAR(minDropouts);
        GET_PAR(latencyOverMul);
        GET_PAR(latencyOverAdd);
        GET_PAR(latencyWorseMul);
        GET_PAR(latencyWorseAdd);
        GET_PAR(latencyOkMul);
        GET_PAR(latencyOkAdd);
        GET_PAR(latencyBestMul);
        GET_PAR(latencyBestAdd);
        GET_PAR(maxBandOverGood);
        GET_PAR(safeMaxBandOverGood);

        const int nGrowEntries = 2 * MAX_GROW_STATE + 2;
        const ParamEntry* gcc = c.FindEntry("grow");
        if (gcc && gcc->GetSize() == nGrowEntries * 2)
        {
            const ParamEntry& gc = *gcc;
            for (int i = 0; i < nGrowEntries; i++)
            {
                data.grow[i].mul = gc[i * 2];
                data.grow[i].add = gc[i * 2 + 1];
            }
        }
#undef GET_PAR
#undef GET_PAR_U
    }
#ifdef NET_LOG
    static bool printFirst = true;
    if (printFirst)
    {
        printFirst = false;
        char buf[264] = "Par: ";
        data.printParams1(buf + 5);
        NetLog(buf);
        data.printParams2(buf + 5);
        NetLog(buf);
        data.printParams3(buf + 5);
        NetLog(buf);
    }
#endif
}

// CreateNetSessionEnum / CreateNetClient / CreateNetServer are declared in
// NetTransport.hpp and defined once in NetTransportNet.cpp, so this header stays
// safe to include from more than one translation unit.

} // namespace Poseidon
