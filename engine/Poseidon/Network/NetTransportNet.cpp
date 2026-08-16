#include <Poseidon/Network/NetTransportNetDecls.hpp>
#include <Poseidon/Network/NetTransportNetInternal.hpp>
#include <Poseidon/Network/NetTransportClientVoiceInit.hpp>
#include <Poseidon/Network/NetTransportPlayerValidationPolicy.hpp>
#include <Poseidon/Network/NetServerPeerTeardown.hpp>
#include <Poseidon/Network/NetTransportServerVoiceRouting.hpp>
#ifndef _WIN32
#include <arpa/inet.h>
#endif
#ifndef _WIN32
#include <netinet/in.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <cstring>
#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>
#include <Poseidon/Foundation/Common/Global.hpp>
#include <Poseidon/Foundation/Common/NetGlobal.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/Bitmask.hpp>
#include <Poseidon/Foundation/Containers/Maps.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Framework/PoTime.hpp>
#include <Poseidon/Foundation/Memory/CheckMem.hpp>
#include <Poseidon/Foundation/Memory/MemAlloc.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Strings/StrFormat.hpp>
#include <Poseidon/Foundation/Threads/PoCritical.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

using namespace Poseidon;
namespace Poseidon
{

int NetSessionDescriptions::Add()
{
    if (_size >= MAX_SESSIONS)
    {
        return -1;
    }
    return _size++;
}

void NetSessionDescriptions::Delete(int i)
{
    _size--;
    for (int j = i; j < _size; j++)
    {
        _data[j] = _data[j + 1];
    }
}

void NetSessionDescriptions::Clear()
{
    _size = 0;
}

NetStatus enumReceive(NetMessage* msg, NetStatus event, void* data);
NetStatus ctrlReceive(NetMessage* msg, NetStatus event, void* data);
NetStatus clientReceive(NetMessage* msg, NetStatus event, void* data);
NetStatus serverReceive(NetMessage* msg, NetStatus event, void* data);

static PoCriticalSection LockDecl(poolLock, "::poolLock");
// locking of 'pool', 'clientPeer', 'serverPeer'

static RefD<NetPool> pool; // Global network pool.

static RefD<NetPeer> clientPeer; // NetPeer for enumerator and client.

static RefD<NetPeer> serverPeer; // NetPeer for server.

static NetPool* getPool()
// should be called inside of poolLock.enter()
{
    if (!pool)
    {
        pool = new NetPool(new PeerChannelFactoryUDP, nullptr, nullptr);
    }
    return pool;
}

static void destroyPool()
// should be called inside of poolLock.enter()
{
    if (clientPeer)
    {
        if (pool)
        {
            pool->deletePeer(clientPeer);
        }
        else
        {
            clientPeer->close();
        }
        clientPeer = nullptr;
    }
    if (serverPeer)
    {
        if (pool)
        {
            pool->deletePeer(serverPeer);
        }
        else
        {
            serverPeer->close();
        }
        serverPeer = nullptr;
    }
    if (pool)
    {
        pool = nullptr;
    }
}

static void setupPortBitMask(BitMask& mask, bool server)
{
    mask.empty();
    if (!server && IsDedicatedServer())
    {
        mask.on(0);
        return;
    }

    int port = GetNetworkPort() + (server ? 0 : 2);
    int i;
    if (server && ::Poseidon::GetPidFileName().GetLength())
    {
        mask.on(port);
    }
    else
    {
        for (i = 0; i++ < NUM_PORTS_TO_TRY; port += PORT_INTERVAL)
        {
            mask.on(port);
        }
    }
}

static NetPeer* getClientPeer()
// should be called inside of poolLock.enter()
{
    if (!clientPeer)
    {
        BitMask portMask;
        setupPortBitMask(portMask, false);
        clientPeer = getPool()->createPeer(&portMask);
        if (clientPeer)
        {
            NetChannel* ctrl = clientPeer->getBroadcastChannel();
            if (ctrl)
            {
                ctrl->setProcessRoutine(enumReceive);
            }
        }
    }
    return clientPeer;
}

static NetPeer* getServerPeer()
// should be called inside of poolLock.enter()
{
    if (!serverPeer)
    {
        BitMask portMask;
        setupPortBitMask(portMask, true);
        serverPeer = getPool()->createPeer(&portMask);
        if (serverPeer)
        {
            NetChannel* ctrl = serverPeer->getBroadcastChannel();
            if (ctrl)
            {
                ctrl->setProcessRoutine(ctrlReceive);
            }
        }
    }
    return serverPeer;
}

static bool SendRawMagicPacket(NetPeer* peer, NetChannel* channel, unsigned32& serial, int magic, BYTE* buffer,
                               int bufferSize)
{
    if (!peer || !channel || bufferSize < 0)
    {
        return false;
    }
    const int dataSize = static_cast<int>(sizeof(unsigned32)) + bufferSize;
    if (dataSize > channel->maxMessageData())
    {
        return false;
    }
    Ref<NetMessage> msg = NetMessagePool::pool()->newMessage(dataSize, channel);
    if (!msg)
    {
        return false;
    }
    AutoArray<char> payload;
    payload.Resize(dataSize);
    memcpy(payload.Data(), &magic, sizeof(unsigned32));
    if (bufferSize > 0)
    {
        memcpy(payload.Data() + sizeof(unsigned32), buffer, bufferSize);
    }
    msg->setFlags(MSG_ALL_FLAGS, MSG_MAGIC_FLAG);
    msg->setData(reinterpret_cast<unsigned8*>(payload.Data()), payload.Size());
    msg->send();
    ++serial;
    return true;
}

void decodeURLAddress(RString address, RString& ip, int& port)
{
    const char* ptr = strrchr(address, ':');
    if (!ptr)
    {
        ip = address;
        return;
    }
    ip = address.Substring(0, ptr - address);
    port = atoi(ptr + 1);
}

// Actual enumeration instance (can be nullptr).
static NetSessionEnum* _enum = nullptr;

// Actual client instance (can be nullptr).
static NetClient* _client = nullptr;

// NetServer instance to process control messages.
static NetServer* _server = nullptr;

// Sets actual enumeration instance (can be nullptr).
void setEnum(NetSessionEnum* _en)
{
    poolLock.enter();
    _enum = _en;
    if (_en)
    {
        getPool();
        getClientPeer();
    }
    poolLock.leave();
}

// Sets actual client instance (can be nullptr).
void setClient(NetClient* _cl)
{
    poolLock.enter();
    _client = _cl;
    if (_cl)
    {
        getPool();
        getClientPeer();
    }
    poolLock.leave();
}

// Sets actual server instance (can be nullptr).
void setServer(NetServer* _srv)
{
    poolLock.enter();
    _server = _srv;
    if (_srv)
    {
        getPool();
        getServerPeer();
    }
    poolLock.leave();
}

// Checks _enum, _client and _server. If everything is nullptr, destroys the global NetPool!
static void checkPool()
{
    poolLock.enter();
    if (!_enum && !_client && !_server)
    {
        destroyPool();
    }
    poolLock.leave();
}

Ref<NetMessage> mergeMessageList(NetMessage* msg)
{
    if (!msg)
    {
        return nullptr;
    }
    unsigned size = 0;
    NetMessage* ptr = msg;
    NetMessage* last;
    do
    {
        size += ptr->getLength();
        last = ptr;
    } while ((ptr = ptr->next));
    Ref<NetMessage> composite = NetMessagePool::pool()->newMessage(size, msg->getChannel());
    if (!composite)
    {
        return nullptr;
    }
    composite->setFrom(last);
    unsigned8* data = (unsigned8*)composite->getData();
    for (ptr = msg; ptr; ptr = ptr->next)
    {
        memcpy(data, ptr->getData(), ptr->getLength());
        data += ptr->getLength();
    }
    composite->setLength(size);
    return composite;
}

// Regular data received by the NetServer...
NetStatus serverReceive(NetMessage* msg, NetStatus event, void* data)
{
    // thread-safe
    if (!_server || !msg)
    {
        return nsNoMoreCallbacks; // nothing to do
    }
    unsigned len = msg->getLength();
    if (!len)
    {
        return nsNoMoreCallbacks; // message is too small
    }
    struct sockaddr_in dist;
    msg->getDistant(dist);
    unsigned flags = msg->getFlags();
    bool isMagic = (flags & MSG_MAGIC_FLAG) != 0;
    if (isMagic)
    { // magic-messages:
        if (len < 4)
        {
            return nsNoMoreCallbacks; // magic-message is too small
        }
        unsigned32 magic = *(unsigned32*)msg->getData();

#ifdef NET_LOG_CTRL_RECEIVE
#ifdef NET_LOG_BRIEF
        NetLog("Pe(%u):rCtrlG(%u.%u.%u.%u:%u,%u,%x)", getServerPeer()->getPeerId(), (unsigned)IP4(dist),
               (unsigned)IP3(dist), (unsigned)IP2(dist), (unsigned)IP1(dist), (unsigned)PORT(dist), msg->getLength(),
               magic);
#else
        NetLog("Peer(%u)::serverReceive: received control message (from=%u.%u.%u.%u:%u, len=%3u, serial=%4u, "
               "flags=%04x, magic=%x)",
               getServerPeer()->getPeerId(), (unsigned)IP4(dist), (unsigned)IP3(dist), (unsigned)IP2(dist),
               (unsigned)IP1(dist), (unsigned)PORT(dist), msg->getLength(), msg->getSerial(), flags, magic);
#endif
#endif

        switch (magic)
        {
            case MAGIC_DESTROY_PLAYER:
            {                        // the player itself is disconnecting..
                _server->enterUsr(); // due to 'channelToPlayer()'
                int player = _server->channelToPlayer(msg->getChannel());
                if (player < 0)
                {
                    RptF("No player found for channel %u - MAGIC_DESTROY_PLAYER message ignored",
                         (unsigned)(uintptr_t)msg->getChannel());
                }
                else
                {
                    _server->finishDestroyPlayer(player);
                }
                _server->leaveUsr();
                break; // don't need to lock this..
            }
            default:
            {
                _server->enterUsr();
                int player = _server->channelToPlayer(msg->getChannel());
                _server->leaveUsr();
                if (player >= 0 && len >= sizeof(unsigned32))
                {
                    _server->enterRcv();
                    int index = _server->rawMagicReceived.Add();
                    RawMagicMessage& item = _server->rawMagicReceived[index];
                    item.from = player;
                    item.magic = magic;
                    item.data.Resize(len - sizeof(unsigned32));
                    if (item.data.Size() > 0)
                    {
                        memcpy(item.data.Data(), (const char*)msg->getData() + sizeof(unsigned32), item.data.Size());
                    }
                    _server->leaveRcv();
                }
                break;
            }
        }

        return nsNoMoreCallbacks; // unknown message => don't care
    }

    // process regular (user) message:
#ifdef NET_LOG_SERVER_RECEIVE
    NetLog(
        "Peer(%u)::serverReceive: received user message (from=%u.%u.%u.%u:%u, len=%3u, serial=%4u, flags=%04x, ID=%x)",
        getServerPeer()->getPeerId(), (unsigned)IP4(dist), (unsigned)IP3(dist), (unsigned)IP2(dist),
        (unsigned)IP1(dist), (unsigned)PORT(dist), len, msg->getSerial(), flags, msg->id);
#endif
    _server->enterRcv();
    // merge partial messages:
    if (flags & MSG_PART_FLAG)
    {
        bool closing = (flags & MSG_CLOSING_FLAG) > 0;
        ChannelSupport* sup = _server->m_support.get(sockaddrKey(dist));
        NET_ERROR(sup);
        if (flags & MSG_URGENT_FLAG)
        {
            if (closing)
            { // the last message-part to be merged..
                if (!sup->m_splitUrgent || !sup->m_lastSplitUrgent)
                { // closing fragment with no opener — drop it, don't deref null
                    _server->leaveRcv();
                    return nsNoMoreCallbacks;
                }
                sup->m_lastSplitUrgent->next = msg;
                msg->next = nullptr;
                msg = mergeMessageList(sup->m_splitUrgent);
                sup->m_splitUrgent = nullptr;
            }
            else
            { // another message-part => remember it!
                if (!sup->m_splitUrgent)
                {
                    sup->m_splitUrgent = msg;
                }
                else
                {
                    sup->m_lastSplitUrgent->next = msg;
                }
                (sup->m_lastSplitUrgent = msg)->next = nullptr;
                _server->leaveRcv();
                return nsNoMoreCallbacks;
            }
        }
        else if (closing)
        { // the last message-part to be merged..
            if (!sup->m_split || !sup->m_lastSplit)
            { // closing fragment with no opener — drop it, don't deref null
                _server->leaveRcv();
                return nsNoMoreCallbacks;
            }
            sup->m_lastSplit->next = msg;
            msg->next = nullptr;
            msg = mergeMessageList(sup->m_split);
            sup->m_split = nullptr;
        }
        else
        { // another message-part => remember it!
            if (!sup->m_split)
            {
                sup->m_split = msg;
            }
            else
            {
                sup->m_lastSplit->next = msg;
            }
            (sup->m_lastSplit = msg)->next = nullptr;
            _server->leaveRcv();
            return nsNoMoreCallbacks;
        }
#ifdef NET_LOG_MERGE
#ifdef NET_LOG_BRIEF
        NetLog("Pe(%u):rMerS(%u,%u,%x,%x)", getServerPeer()->getPeerId(), msg->getLength(), msg->getSerial(),
               (unsigned)msg->getFlags(), msg->id);
#else
        NetLog("Peer(%u)::serverReceive: merging user message (len=%3u, serial=%4u, flags=%04x, ID=%x)",
               getServerPeer()->getPeerId(), msg->getLength(), msg->getSerial(), (unsigned)msg->getFlags(), msg->id);
#endif
#endif
    }

    _server->insertReceived(msg);

    _server->leaveRcv();

    return nsNoMoreCallbacks;
}

// Control (broadcast) data received by the NetServer...
NetStatus ctrlReceive(NetMessage* msg, NetStatus event, void* data)
{
    // thread-critical: NetMessagePool::pool()->newMessage(), NetMessage::send(), strncmp(),
    //                  getServerPeer()->getPool()->createChannel(), AutoArray::Add()
    if (!_server || !msg || msg->getLength() < 4 || !(msg->getFlags() & MSG_MAGIC_FLAG))
    {
        return nsNoMoreCallbacks; // fatal message error
    }

    unsigned32 magic = *(unsigned32*)msg->getData();
    struct sockaddr_in distant;
    msg->getDistant(distant);

#ifdef NET_LOG_CTRL_RECEIVE
#ifdef NET_LOG_BRIEF
    NetLog("Pe(%u):rCtrlB(%u.%u.%u.%u:%u,%u,%x)", getServerPeer()->getPeerId(), (unsigned)IP4(distant),
           (unsigned)IP3(distant), (unsigned)IP2(distant), (unsigned)IP1(distant), (unsigned)PORT(distant),
           msg->getLength(), magic);
#else
    NetLog("Peer(%u)::ctrlReceive: received control message (from=%u.%u.%u.%u:%u, len=%3u, serial=%4u, flags=%04x, "
           "magic=%x)",
           getServerPeer()->getPeerId(), (unsigned)IP4(distant), (unsigned)IP3(distant), (unsigned)IP2(distant),
           (unsigned)IP1(distant), (unsigned)PORT(distant), msg->getLength(), msg->getSerial(),
           (unsigned)msg->getFlags(), magic);
#endif
#endif

    switch (magic)
    {
        case MAGIC_ENUM_REQUEST: // enumeration request
            if (msg->getLength() == sizeof(EnumPacket))
            {
                EnumPacket* ep = (EnumPacket*)msg->getData();
                _server->enterUsr();
                // The reply is larger than the request; rate-limit the reflected
                // reply so the server's reply traffic stays bounded. Checked under usrCs.
                if (ep->magicApplication == _server->magicApp &&
                    _server->_enumReplyBucket.tryConsume((uint32_t)GetTickCount()))
                {
                    Ref<NetMessage> out = NetMessagePool::pool()->newMessage(SESSION_PACKET_SIZE, msg->getChannel());
                    if (out)
                    {
                        out->setDistant(distant);
                        out->setFlags(MSG_ALL_FLAGS, MSG_TO_BCAST_FLAG | MSG_MAGIC_FLAG);
                        _server->session.magic = MAGIC_ENUM_RESPONSE;
                        _server->session.request = msg->getSerial();
                        _server->session.numPlayers = _server->users.card() - 1;
                        out->setData((unsigned8*)&(_server->session), SESSION_PACKET_SIZE);
                        out->send(true); // urgent message
                    }
#ifdef ENUM_LEGACY
                    out = NetMessagePool::pool()->newMessage(SESSION_PACKET_1, msg->getChannel());
                    if (out)
                    {
                        out->setDistant(distant);
                        out->setFlags(MSG_ALL_FLAGS, MSG_TO_BCAST_FLAG | MSG_MAGIC_FLAG);
                        _server->session.magic = MAGIC_ENUM_RESPONSE_LEGACY;
                        out->setData((unsigned8*)&(_server->session), SESSION_PACKET_1);
                        out->send(true); // urgent message
                    }
#endif
                }
                _server->leaveUsr();
            }
            break;

        case MAGIC_CREATE_PLAYER: // create player
            if (msg->getLength() == sizeof(CreatePlayerPacket))
            {
                CreatePlayerPacket* cpp = (CreatePlayerPacket*)msg->getData();
                _server->enterUsr(); // lock the user, session etc. structures for a long time..
                if (cpp->magicApplication != _server->magicApp)
                {
                    _server->leaveUsr();
                    break;
                }
                ConnectResult result =
                    EvaluateNetTransportCreatePlayerRequest(_server->session, _server->_password, *cpp);
                if (result == CRVersion)
                {
                    LOG_INFO(Network,
                             "CreatePlayer rejected by version/mod check: server actual={} required={} mod='{}' "
                             "tag='{}'; client actual={} required={} mod='{}' tag='{}'; equalModRequired={}",
                             _server->session.actualVersion, _server->session.requiredVersion, _server->session.mod,
                             _server->session.versionTag, cpp->actualVersion, cpp->requiredVersion, cpp->mod,
                             cpp->versionTag, _server->session.equalModRequired);
                }
                // create a new player:
                int player = 0; // dummy player ID
                int playerEnd;
                NetChannel* ch = nullptr;
                if (result == CROK)
                {
                    poolLock.enter();
                    NetPeer* peer = getServerPeer();
                    NET_ERROR(peer);
                    RefD<NetChannel> findCh;
                    ch = peer->findChannel(distant);
                    if (ch)
                    { // this channel is being used!
                        player = cpp->uniqueID;
                        playerEnd = player - UNIQUE_TO_TRY;
                        do
                        {
                            if (_server->users.get(player, findCh) && (NetChannel*)findCh == ch)
                            {
                                break;
                            }
                        } while (--player != playerEnd);
                        if (player != playerEnd)
                        { // found => refuse it!
                            poolLock.leave();
                            _server->leaveUsr();
                            break; // duplicate CreatePlayer message => refuse it
                        }
                        // new connection from the same IP:port => kick off the old player
                        _server->finishDestroyPlayer(_server->channelToPlayer(ch));
                        ch = nullptr;
                        poolLock.leave();
                        _server->leaveUsr();
                        SLEEP_MS(DESTROY_WAIT); // waiting till my boss destroys the old player (to be sure)..
                        _server->enterUsr();
                        poolLock.enter();
                    }
                    // creating a new player: first determine its ID..
                    player = cpp->uniqueID;
                    playerEnd = player + UNIQUE_TO_TRY;
                    while (player != playerEnd && _server->users.get(player, findCh))
                    {
                        player++;
                    }
                    if (player == playerEnd ||
                        (_server->session.maxPlayers > 0 &&
                         _server->users.card() + 2 > static_cast<unsigned>(_server->session.maxPlayers)))
                    { // card() returns unsigned
                        result = CRSessionFull;
                    }
                    else
                    { // OK <= new player (at least new FlashPoint run)
                        ch = peer->getPool()->createChannel(distant, peer);
                        if (!ch)
                        {
                            result = CRError; // undetermined error
                            RptF("NetServer: createChannel() failed => cannot insert a new player");
                        }
                        else
                        { // NetChannel is OK
#ifdef NET_LOG_SERVER
#ifdef NET_LOG_BRIEF
                            NetLog("Ch(%u):acc(%u.%u.%u.%u:%u,'%s',%d,%d)", ch->getChannelId(), (unsigned)IP4(distant),
                                   (unsigned)IP3(distant), (unsigned)IP2(distant), (unsigned)IP1(distant),
                                   (unsigned)PORT(distant), cpp->name, cpp->uniqueID, player);
#else
                            NetLog("Channel(%u): new player was accepted (from %u.%u.%u.%u:%u), name = '%s', sentID = "
                                   "%d, validID = %d",
                                   ch->getChannelId(), (unsigned)IP4(distant), (unsigned)IP3(distant),
                                   (unsigned)IP2(distant), (unsigned)IP1(distant), (unsigned)PORT(distant), cpp->name,
                                   cpp->uniqueID, player);
#endif
#endif
                            _server->users.put(player, ch);
                            ChannelSupport sup;
                            sup.m_id = player;
                            _server->m_support.put(sockaddrKey(distant), sup);
                            //_server->logUsers();
                            NET_ERROR(_server->users.checkIntegrity());
                            int index = _server->_createPlayers.Add();
                            CreatePlayerInfo& info = _server->_createPlayers[index];
                            info.player = player;
                            if ((info.botClient = (cpp->botClient != 0)))
                            {
                                _server->botId = player;
                            }
                            strncpy(info.name, cpp->name, sizeof(info.name));
                            info.name[sizeof(info.name) - 1] = (char)0;
                            if (strcmp(cpp->name, info.name))
                            {
                                RptF("NetServer: name of a new player is too long => truncating to '%S'", info.name);
                            }
                            RptF("NetServer: new player (waiting for ProcessPlayers) - session.numPlayers=%d, "
                                 "playerId=%d, bot=%d, name='%s', |users|=%u",
                                 _server->session.numPlayers, info.player, (int)info.botClient, info.name,
                                 _server->users.card());
                            strncpy(info.mod, cpp->mod, sizeof(info.mod));
                            info.mod[sizeof(info.mod) - 1] = (char)0;
                            strncpy(info.versionTag, cpp->versionTag, sizeof(info.versionTag));
                            info.versionTag[sizeof(info.versionTag) - 1] = (char)0;
                            ch->setProcessRoutine(serverReceive);
                        }
                    }
                    poolLock.leave();
                }
                _server->leaveUsr();
                // .. and finally send acknowledgement back to the new user
                AckPlayerPacket app;
                app.magic = MAGIC_ACK_PLAYER;
                app.result = result;
                app.playerNo = player; // for future reconnections..
                Ref<NetMessage> out =
                    NetMessagePool::pool()->newMessage(sizeof(AckPlayerPacket), ch ? ch : msg->getChannel());
                if (out)
                {
                    if (!ch)
                    {
                        out->setDistant(distant);
                    }
                    out->setFlags(MSG_ALL_FLAGS, MSG_MAGIC_FLAG | (ch ? MSG_VIM_FLAG : MSG_FROM_BCAST_FLAG));
                    out->setData((unsigned8*)&app, sizeof(app));
                    out->send(true); // urgent message
                }
                if (ch)
                { // initial hand-shake (to initialize band-width..).
                    ch->checkConnectivity(0);
                }
            }
            break;

        case MAGIC_RECONNECT_PLAYER: // reconnect player
            if (msg->getLength() == sizeof(ReconnectPlayerPacket))
            {
                ReconnectPlayerPacket* rpp = (ReconnectPlayerPacket*)msg->getData();
                _server->enterUsr();
                RefD<NetChannel> ch;
                ConnectResult result = CRError;
                if (_server->users.get(rpp->playerNo, ch) && // player with this ID exists => reconnect it
                    ch->reconnect(distant) == nsOK)
                {
                    result = CROK;
                }
                _server->leaveUsr();
                // .. send either acknowledgement back to the user
                AckPlayerPacket app;
                app.magic = MAGIC_ACK_PLAYER;
                app.result = result;
                app.playerNo = rpp->playerNo;
                Ref<NetMessage> out = NetMessagePool::pool()->newMessage(
                    sizeof(AckPlayerPacket), (result == CROK) ? (NetChannel*)ch : msg->getChannel());
                if (out)
                {
                    if (result != CROK)
                    {
                        out->setDistant(distant);
                    }
                    out->setFlags(MSG_ALL_FLAGS,
                                  MSG_MAGIC_FLAG | ((result == CROK) ? MSG_VIM_FLAG : MSG_FROM_BCAST_FLAG));
                    out->setData((unsigned8*)&app, sizeof(app));
                    out->send(true); // urgent message
                }
                if (result == CROK)
                { // initial hand-shake (to re-initialize band-width..).
                    ch->checkConnectivity(0);
                }
            }
            break;
    }

    return nsNoMoreCallbacks;
}

// Control (broadcast) data received by the enumerator and client...
NetStatus enumReceive(NetMessage* msg, NetStatus event, void* data)
{
    // thread-critical: sprintf(), AutoArray::Add(), strncpy()
    if ((!_enum || !_enum->_running) && !_client || !msg || msg->getLength() < 4 || !(msg->getFlags() & MSG_MAGIC_FLAG))
    {
        return nsNoMoreCallbacks; // fatal message error
    }

    unsigned32 magic = *(unsigned32*)msg->getData();
    struct sockaddr_in distant;
    msg->getDistant(distant);
    unsigned32 ip = ntohl(distant.sin_addr.s_addr);
    unsigned16 port = ntohs(distant.sin_port);

#ifdef NET_LOG_ENUM_RECEIVE
    NetLog("Peer(%u)::enumReceive: received control message (from=%u.%u.%u.%u:%u, len=%3u, serial=%4u, flags=%04x, "
           "magic=%x)",
           getClientPeer()->getPeerId(), (unsigned)IP4(distant), (unsigned)IP3(distant), (unsigned)IP2(distant),
           (unsigned)IP1(distant), (unsigned)PORT(distant), msg->getLength(), msg->getSerial(),
           (unsigned)msg->getFlags(), magic);
#endif

    switch (magic)
    {
        case MAGIC_ENUM_RESPONSE: // enumeration response
        {
            if (msg->getLength() != SESSION_PACKET_2 && msg->getLength() != SESSION_PACKET_3 &&
                msg->getLength() != SESSION_PACKET_4)
            {
                break; // wrong size
            }

            // default mod required:
            char mod[MOD_LENGTH];
            mod[0] = 0;
            char versionTag[VERSION_TAG_LENGTH] = {0};
            bool equalModRequired = false;
            if (msg->getLength() >= SESSION_PACKET_3)
            {
                strncpy(mod, ((SessionPacket*)msg->getData())->mod, MOD_LENGTH);
                mod[MOD_LENGTH - 1] = (char)0;
                equalModRequired = (((SessionPacket*)msg->getData())->equalModRequired & 1) != 0;
            }
            if (msg->getLength() >= SESSION_PACKET_4)
            {
                strncpy(versionTag, ((SessionPacket*)msg->getData())->versionTag, VERSION_TAG_LENGTH);
                versionTag[VERSION_TAG_LENGTH - 1] = (char)0;
            }

            SessionPacket* s = (SessionPacket*)msg->getData();
            {
                _enum->enter();

                // search for existing session record
                int iFound = -1;
                for (int i = 0; i < _enum->_sessions.Size(); i++)
                {
                    if (_enum->_sessions[i].ip == ip && _enum->_sessions[i].port == port)
                    {
                        iFound = i;
                        break;
                    }
                }

                unsigned64 reqTime = msg->getChannel()->getMessageTime(s->request);
                // actual ping time in milliseconds:
                unsigned pingTime = reqTime ? (unsigned)((msg->getTime() - reqTime) / 1000) : 0;

                if (iFound < 0)
                { // not found
                    iFound = _enum->_sessions.Add();
                    if (iFound < 0)
                    { // too much sessions encountered
                        _enum->leave();
                        return nsNoMoreCallbacks;
                    }
                    NetSessionDescription& ndesc = _enum->_sessions[iFound];
                    ndesc.ip = ip;
                    ndesc.port = port;
                    ndesc.pingTime = pingTime;
                    snprintf(ndesc.address, sizeof(ndesc.address), "%s:%u", inet_ntoa(distant.sin_addr),
                             (unsigned)port);
                }

                NetSessionDescription& desc = _enum->_sessions[iFound];
                // s->name is the remote server's wire-supplied name; it must be copied as
                // data, never used as the printf format, so conversion specifiers in the
                // name are shown literally rather than interpreted.
                snprintf(desc.name, sizeof(desc.name), "%s", s->name);
                desc.actualVersion = s->actualVersion;
                desc.requiredVersion = s->requiredVersion;
                strncpy(desc.mission, s->mission, LEN_MISSION_NAME);
                desc.mission[LEN_MISSION_NAME - 1] = (char)0;
                strncpy(desc.mod, mod, MOD_LENGTH);
                desc.mod[MOD_LENGTH - 1] = (char)0;
                strncpy(desc.versionTag, versionTag, VERSION_TAG_LENGTH);
                desc.versionTag[VERSION_TAG_LENGTH - 1] = (char)0;
                desc.equalModRequired = equalModRequired ? 1 : 0;
                desc.gameState = s->gameState;
                desc.maxPlayers = s->maxPlayers;
                desc.numPlayers = s->numPlayers;
                desc.password = s->password;
                desc.lastTime = (unsigned32)(getSystemTime() / 1000);
                desc.pingTime = (3 * desc.pingTime + pingTime + 2) >> 2;

                _enum->leave();
            }
        }
        break;
    }

    return nsNoMoreCallbacks;
}

NetClient::NetClient() : LockInit(sndCs, "NetClient::sndCs", true), LockInit(rcvCs, "NetClient::rcvCs", true)
{
    // Message times:
    lastMsgReported = getSystemTime();
    // NetChannel: not set yet
    channel = nullptr;
    // to be sure:
    received = nullptr;
    split = lastSplit = nullptr;
    splitUrgent = lastSplitUrgent = nullptr;
    sent = nullptr;
    rawMagicSerial = 0x70000000;
    sessionTerminated = false;
    whySessionTerminated = NTROther;
    magicApp = 0;
#ifdef NET_LOG_INFO
    oldLatency = -1;
    oldThroughput = -1;
    printAge = false;
#endif
#ifdef NET_LOG_TRANSP_STAT
    nextStatLog = lastMsgReported;
#endif
    setClient(this);
#ifdef NET_LOG_CLIENT
#ifdef NET_LOG_BRIEF
    NetLog("Pe(%u):cli", getClientPeer()->getPeerId());
#else
    NetLog("Peer(%u): creating NetClient instance", getClientPeer()->getPeerId());
#endif
#endif
}

NetClient::~NetClient()
{
    enterSnd();
    if (!sessionTerminated)
    { // I must notify my server..
        sessionTerminated = true;
        whySessionTerminated = NTRDisconnected;
        // send MAGIC_DESTROY_PLAYER message to server:
        if (channel)
        {
            Ref<NetMessage> out = NetMessagePool::pool()->newMessage(4, channel);
            if (out)
            {
                unsigned32 magic = MAGIC_DESTROY_PLAYER;
                out->setFlags(MSG_ALL_FLAGS, MSG_MAGIC_FLAG);
                out->setData((unsigned8*)&magic, 4);
                out->send(true);
                SLEEP_MS(DESTRUCT_WAIT);
            }
        }
    }

    // Clean up VoN speakers
    for (auto& [ch, spk] : _vonSpeakers)
    {
        if (spk)
            spk->destroy();
    }
    _vonSpeakers.clear();
    _vonSystem.shutdown();

    setClient(nullptr);
    // recycle all pending NetMessages:
    RemoveUserMessages();
    RemoveSendComplete();
    // destroy the NetChannel:
#ifdef NET_LOG_CLIENT
#ifdef NET_LOG_BRIEF
    NetLog("Ch(%u):~cli", channel ? channel->getChannelId() : 0);
#else
    NetLog("Channel(%u): destroying NetClient instance", channel ? channel->getChannelId() : 0);
#endif
#endif
    if (channel)
    {
        NetChannel* old = channel;
        channel = nullptr;
        leaveSnd();
        getPool()->deleteChannel(old);
    }
    else
    {
        leaveSnd();
    }
    checkPool();
    FreeMemory();
}

// VoN 3D sound buffer — delegates position to OpenAL speaker source
class VoNSound3DBuffer : public NetTranspSound3DBuffer
{
  public:
    explicit VoNSound3DBuffer(VoNSpeaker* spk) : _spk(spk) {}

    void SetPosition(float x, float y, float z) override
    {
        if (_spk)
            _spk->setPosition(x, y, z);
    }

  private:
    VoNSpeaker* _spk;
};

bool NetClient::InitVoice()
{
    _vonSystem.initClient();
    auto* c = _vonSystem.client();
    if (!c)
        return false;

    // Setup codec factory
    c->setCodecFactory([]() { return std::make_unique<OpusCodec>(); });

    // Open capture device (16kHz, 1s buffer)
    if (!c->openCapture(nullptr, 16000, 16000))
        LOG_WARN(Network, "VoN: failed to open capture device - transmit disabled");

    // Wire packet sink to send VoN packets over network (non-guaranteed, high priority for low latency)
    c->setPacketSink(
        [this](const std::vector<uint8_t>& pkt)
        {
            DWORD msgID;
            SendMsg(const_cast<BYTE*>(pkt.data()), static_cast<int>(pkt.size()), msgID, NMFHighPriority);
        });

    SetNetTransportClientVoiceSenderId([c]() { return c; }, static_cast<uint32_t>(playerNo));

    LOG_INFO(Network, "VoN client initialized (player={})", playerNo);
    return true;
}

bool NetClient::IsVoicePlaying(int player)
{
    PumpVoiceSpeakers();
    auto found = _vonSpeakers.find(static_cast<uint32_t>(player));
    return found != _vonSpeakers.end() && found->second && found->second->active && found->second->level > 0.001f;
}

bool NetClient::IsVoiceRecording()
{
    auto* c = _vonSystem.client();
    return c && c->isTransmitting();
}

void NetClient::GetVoiceSpeakers(AutoArray<NetVoiceSpeakerInfo, Poseidon::Foundation::MemAllocSA>& speakers)
{
    PumpVoiceSpeakers();
    for (auto& [player, speaker] : _vonSpeakers)
    {
        if (!speaker)
        {
            continue;
        }
        NetVoiceSpeakerInfo info;
        info.player = static_cast<int>(player);
        const auto channel = _vonSpeakerChannels.find(player);
        info.channel = channel != _vonSpeakerChannels.end() ? static_cast<int>(channel->second)
                                                            : static_cast<int>(VoNChatChannel::Global);
        info.active = speaker->active && speaker->level > 0.001f;
        info.level = speaker->level;
        speakers.Add(info);
    }
}

void NetClient::PumpVoiceSpeakers()
{
    auto* c = _vonSystem.client();
    if (!c)
        return;

    DWORD now = GlobalTickCount();
    if (_lastVoicePumpTime != 0 && now - _lastVoicePumpTime < 15)
        return;
    _lastVoicePumpTime = now;

    for (auto& [ch, spk] : _vonSpeakers)
    {
        if (!spk)
            continue;

        const auto channel = _vonSpeakerChannels.find(ch);
        spk->setChannel(channel != _vonSpeakerChannels.end() ? channel->second : VoNChatChannel::Global);
        spk->feed(c, ch);
    }
}

NetTranspSound3DBuffer* NetClient::Create3DSoundBuffer(int player)
{
    auto* c = _vonSystem.client();
    if (!c)
        return nullptr;
    uint32_t ch = static_cast<uint32_t>(player);
    c->createChannel(ch, {});

    // Create OpenAL speaker for this channel
    auto& spk = _vonSpeakers[ch];
    if (!spk)
    {
        spk = std::make_unique<VoNSpeaker>();
        spk->init();
        LOG_INFO(Network, "VoN: created speaker for player {}", player);
    }
    _vonSpeakerChannels[ch] = VoNChatChannel::Direct;
    spk->setChannel(VoNChatChannel::Direct);
    return new VoNSound3DBuffer(spk.get());
}

void NetClient::SetVoiceChannel(int channel)
{
    auto* c = _vonSystem.client();
    if (c)
    {
        c->setChatChannel(NetTransportChatChannelToVoN(channel));
        LOG_DEBUG(Network, "VoN# net SetVoiceChannel game={} von={}", channel,
                  static_cast<int>(NetTransportChatChannelToVoN(channel)));
    }
    else
    {
        LOG_WARN(Network, "VoN# net SetVoiceChannel({}) NO-CLIENT", channel);
    }
}

void NetClient::SetVoiceTransmit(bool on)
{
    auto* c = _vonSystem.client();
    if (c)
    {
        c->setTransmit(on);
    }
    else
    {
        LOG_WARN(Network, "VoN# net SetVoiceTransmit({}) NO-CLIENT", on);
    }
}

int NetClient::SendVoiceTestTone(int frames, int amplitude)
{
    auto* c = _vonSystem.client();
    return c ? c->sendTestTone(frames, amplitude) : 0;
}

int NetClient::GetVoiceTransmitHealth()
{
    auto* c = _vonSystem.client();
    return c ? static_cast<int>(c->transmitHealth()) : static_cast<int>(VoNTransmitHealth::Off);
}

NetStatus clientReceive(NetMessage* msg, NetStatus event, void* data)
{
    // thread-safe
    if (!_client || !msg)
    {
        return nsNoMoreCallbacks; // fatal error
    }
    _client->lastMsgReported = msg->getTime();
    unsigned len = msg->getLength();
    if (!len)
    {
        return nsNoMoreCallbacks; // message is too small
    }
#if defined(NET_LOG_CLIENT_RECEIVE) || defined(NET_LOG_MERGE)
    NetChannel* channel = (NetChannel*)data;
    NET_ERROR(channel);
#endif
    unsigned flags = msg->getFlags();
    bool isMagic = (flags & MSG_MAGIC_FLAG) != 0;
    if (isMagic)
    { // magic-messages:
        if (len < 4)
        {
            return nsNoMoreCallbacks; // magic-message is too small
        }
        unsigned32 magic = *(unsigned32*)msg->getData();
#ifdef NET_LOG_CLIENT_RECEIVE
        NetLog("Channel(%u)::clientReceive: received control message (len=%3u, serial=%4u, flags=%04x, magic=%x)",
               channel->getChannelId(), len, msg->getSerial(), flags, magic);
#endif

        switch (magic)
        {
            case MAGIC_ACK_PLAYER: // AckPlayer (in case of success)
                if (len == sizeof(AckPlayerPacket))
                {
                    AckPlayerPacket* app = (AckPlayerPacket*)msg->getData();
                    const int playerNo = app->playerNo;
                    _client->enterSnd();
                    if (_client->ackPlayer == CRNone)
                    { // refuse duplicate messages
                        _client->playerNo = playerNo;
                        _client->ackPlayer = app->result;
                        SetNetTransportClientVoiceSenderIdIfAccepted(
                            IsNetTransportClientVoiceAckAccepted(static_cast<ConnectResult>(app->result)),
                            [&]() { return _client->_vonSystem.client(); }, static_cast<uint32_t>(playerNo));
                    }
                    _client->leaveSnd();
                }
                break; // don't need to lock this..

            case MAGIC_TERMINATE_SESSION:
            { // Session terminate
                // check additional data (if present)
                NetTerminationReason reason = NTROther;
                if (len >= 2 * sizeof(unsigned32))
                {
                    reason = (NetTerminationReason)((unsigned32*)msg->getData())[1];
                }
                _client->enterSnd();
                _client->sessionTerminated = true;
                _client->whySessionTerminated = reason;
                _client->leaveSnd();
                break;
            }
            default:
                if (len >= sizeof(unsigned32))
                {
                    _client->enterRcv();
                    int index = _client->rawMagicReceived.Add();
                    RawMagicMessage& item = _client->rawMagicReceived[index];
                    item.from = 0;
                    item.magic = magic;
                    item.data.Resize(len - sizeof(unsigned32));
                    if (item.data.Size() > 0)
                    {
                        memcpy(item.data.Data(), (const char*)msg->getData() + sizeof(unsigned32), item.data.Size());
                    }
                    _client->leaveRcv();
                }
                break;
        }

        return nsNoMoreCallbacks;
    }

    // process regular (user) message:
#ifdef NET_LOG_CLIENT_RECEIVE
    NetLog("Channel(%u)::clientReceive: received user message (len=%3u, serial=%4u, flags=%04x, ID=%x)",
           channel->getChannelId(), len, msg->getSerial(), flags, msg->id);
#endif
    _client->enterRcv();
    // merge partial messages:
    if (flags & MSG_PART_FLAG)
    {
        bool closing = (flags & MSG_CLOSING_FLAG) > 0;
        if (flags & MSG_URGENT_FLAG)
        {
            if (closing)
            { // the last message-part to be merged..
                NET_ERROR(_client->splitUrgent.NotNull());
                _client->lastSplitUrgent->next = msg;
                msg->next = nullptr;
                msg = mergeMessageList(_client->splitUrgent);
                _client->splitUrgent = nullptr;
            }
            else
            { // another message-part => remember it!
                if (!_client->splitUrgent)
                {
                    _client->splitUrgent = msg;
                }
                else
                {
                    _client->lastSplitUrgent->next = msg;
                }
                (_client->lastSplitUrgent = msg)->next = nullptr;
                _client->leaveRcv();
                return nsNoMoreCallbacks;
            }
        }
        else if (closing)
        { // the last message-part to be merged..
            NET_ERROR(_client->split.NotNull());
            _client->lastSplit->next = msg;
            msg->next = nullptr;
            msg = mergeMessageList(_client->split);
            _client->split = nullptr;
        }
        else
        { // another message-part => remember it!
            if (!_client->split)
            {
                _client->split = msg;
            }
            else
            {
                _client->lastSplit->next = msg;
            }
            (_client->lastSplit = msg)->next = nullptr;
            _client->leaveRcv();
            return nsNoMoreCallbacks;
        }
#ifdef NET_LOG_MERGE
#ifdef NET_LOG_BRIEF
        NetLog("Ch(%u):rMerC(%u,%u,%x,%x)", channel->getChannelId(), msg->getLength(), msg->getSerial(),
               (unsigned)msg->getFlags(), msg->id);
#else
        NetLog("Channel(%u)::clientReceive: merging user message (len=%3u, serial=%4u, flags=%04x, ID=%x)",
               channel->getChannelId(), msg->getLength(), msg->getSerial(), (unsigned)msg->getFlags(), msg->id);
#endif
#endif
    }

    _client->insertReceived(msg);

    _client->leaveRcv();

    return nsNoMoreCallbacks;
}

ConnectResult NetClient::ReInit()
{
    enterSnd();
    if (!channel)
    { // not connected => error
        leaveSnd();
        return CRError;
    }
    // put together the "ReconnectPlayer" message:
    ReconnectPlayerPacket packet;
    packet.magic = MAGIC_RECONNECT_PLAYER;
    packet.playerNo = playerNo;
    // .. and send it to the server:
    ackPlayer = CRNone;
    // server will receive this message via it's control channel so I must not use VIM flag!
    // I'll send it multiple times if necessary..

    // now I have to wait to server's response..
    unsigned64 now = getSystemTime();
    unsigned64 next = now; // re-send time (init = the 1st try)
    unsigned64 timeout = now + 1000 * ACK_PLAYER_TIMEOUT;
    do
    {
        if (now >= next)
        {
            Ref<NetMessage> msg = NetMessagePool::pool()->newMessage(sizeof(packet), channel);
            if (!msg)
            {
                return CRError;
            }
            msg->setFlags(MSG_ALL_FLAGS, MSG_TO_BCAST_FLAG | MSG_MAGIC_FLAG);
            msg->setData((unsigned8*)&packet, sizeof(packet));
            msg->send(true); // urgent
            next = now + 1000 * CREATE_PLAYER_RESEND;
        }
        leaveSnd();
        SLEEP_MS(NET_CHECK_WAIT);
        enterSnd();
        now = getSystemTime();
    } while (ackPlayer == CRNone && now < timeout);

    if (ackPlayer != CRNone)
    { // initial hand-shake (to re-initialize band-width)
        channel->checkConnectivity(0);
    }

#ifdef NET_LOG_CLIENT
#ifdef NET_LOG_BRIEF
    NetLog("Ch(%u):cli-re(%s,%.3f,%d)", channel->getChannelId(), (ackPlayer == CRNone) ? "failed" : "reconnected",
           1.e-6 * (now - timeout + 1000 * ACK_PLAYER_TIMEOUT), playerNo);
#else
    NetLog("Channel(%u): NetClient::ReInit: %s after %.3f seconds (playerID=%d)", channel->getChannelId(),
           (ackPlayer == CRNone) ? "failed" : "reconnected", 1.e-6 * (now - timeout + 1000 * ACK_PLAYER_TIMEOUT),
           playerNo);
#endif
#endif
    ConnectResult result = (ackPlayer == CRNone) ? CRError : (ConnectResult)ackPlayer;
    leaveSnd();
    return result;
}

ConnectResult NetClient::Init(RString address, RString password, bool botClient, int& port, RString player,
                              MPVersionInfo& versionInfo, int magic)
{
#ifdef NET_LOG_CLIENT
#ifdef NET_LOG_BRIEF
    NetLog("Cli(%s,%d)", address.GetLength() ? (const char*)address : "none", port);
#else
    NetLog("NetClient::Init: address=%s, port=%d", address.GetLength() ? (const char*)address : "none", port);
#endif
#endif
    enterSnd();
    if (channel)
    { // already connected => error
        leaveSnd();
        return CRError;
    }
    magicApp = magic;
    RString ip;
    struct sockaddr_in daddr;
    if (address.GetLength() == 0)
    { // use "localhost"
        char buf[64];
        snprintf(buf, sizeof(buf), "127.0.0.1:%d", port);
        address = buf;
    }
    decodeURLAddress(address, ip, port);
    if (!getHostAddress(daddr, (const char*)ip, port))
    {
        leaveSnd();
        return CRError;
    }
    // now "daddr" contains valid IP address..
    sessionTerminated = false;
    whySessionTerminated = NTROther;

    // create a new NetChannel for client <-> server communication:
    poolLock.enter();
    if (getPool())
    {
        channel = getPool()->createChannel(daddr, getClientPeer());
    }
    if (!channel)
    {
        poolLock.leave();
        leaveSnd();
        return CRError;
    }
    poolLock.leave();

    channel->setProcessRoutine(clientReceive, channel);

    amIBot = botClient;

    // put together the "CreatePlayer" message:
    CreatePlayerPacket packet;
    packet.magic = MAGIC_CREATE_PLAYER;
    packet.magicApplication = magicApp;
    strncpy(packet.name, player, LEN_PLAYER_NAME);
    packet.name[LEN_PLAYER_NAME - 1] = (char)0;
    strncpy(packet.password, password, LEN_PASSWORD_NAME);
    packet.password[LEN_PASSWORD_NAME - 1] = (char)0;
    packet.actualVersion = versionInfo.versionActual;
    packet.requiredVersion = versionInfo.versionRequired;
    strncpy(packet.mod, versionInfo.mod, MOD_LENGTH);
    packet.mod[MOD_LENGTH - 1] = (char)0;
    strncpy(packet.versionTag, versionInfo.versionTag, VERSION_TAG_LENGTH);
    packet.versionTag[VERSION_TAG_LENGTH - 1] = (char)0;
    packet.botClient = botClient ? 1 : 0;
    packet.uniqueID = (int32)(getSystemTime() & 0x7fffffff) + UNIQUE_TO_TRY;
#ifdef NET_LOG_CLIENT
#ifdef NET_LOG_BRIEF
    NetLog("Ch(%u):cli(%u.%u.%u.%u:%u,'%s',%d,%d)", channel->getChannelId(), (unsigned)IP4(daddr), (unsigned)IP3(daddr),
           (unsigned)IP2(daddr), (unsigned)IP1(daddr), (unsigned)PORT(daddr), (const char*)player, botClient ? 1 : 0,
           packet.uniqueID);
#else
    NetLog("Channel(%u): NetClient::Init: server=%u.%u.%u.%u:%u, player='%s', bot=%d, magic=%d, playerID=%d",
           channel->getChannelId(), (unsigned)IP4(daddr), (unsigned)IP3(daddr), (unsigned)IP2(daddr),
           (unsigned)IP1(daddr), (unsigned)PORT(daddr), (const char*)player, botClient ? 1 : 0, magic, packet.uniqueID);
#endif
#endif
    // .. and send it to the server:
    ackPlayer = CRNone;
    // server will receive this message via it's control channel so I must not use VIM flag!
    // I'll send it multiple times if necessary..

    // now I have to wait to server's response..
    unsigned64 now = getSystemTime();
    unsigned64 next = now; // re-send time (init = the 1st try)
    unsigned64 timeout = now + 1000 * ACK_PLAYER_TIMEOUT;
    do
    {
        if (now >= next)
        {
            Ref<NetMessage> msg = NetMessagePool::pool()->newMessage(sizeof(packet), channel);
            if (!msg)
            {
                leaveSnd();
                return CRError;
            }
            msg->setFlags(MSG_ALL_FLAGS, MSG_TO_BCAST_FLAG | MSG_MAGIC_FLAG);
            msg->setData((unsigned8*)&packet, sizeof(packet));
            msg->send(true); // urgent
            next = now + 1000 * CREATE_PLAYER_RESEND;
        }
        leaveSnd();
        SLEEP_MS(NET_CHECK_WAIT);
        enterSnd();
        now = getSystemTime();
    } while (ackPlayer == CRNone && now < timeout);

    if (ackPlayer != CRNone)
    { // initial hand-shake (to initialize band-width)
        channel->checkConnectivity(0);
        struct sockaddr_in distant;
        channel->getDistantAddress(distant);
        port = ntohs(distant.sin_port);
    }

#ifdef NET_LOG_CLIENT
#ifdef NET_LOG_BRIEF
    NetLog("Ch(%u):cli(%s,%.3f,%d)", channel->getChannelId(), (ackPlayer == CRNone) ? "failed" : "connected",
           1.e-6 * (now - timeout + 1000 * ACK_PLAYER_TIMEOUT), playerNo);
#else
    NetLog("Channel(%u): NetClient::Init: %s after %.3f seconds (playerID=%d)", channel->getChannelId(),
           (ackPlayer == CRNone) ? "failed" : "connected", 1.e-6 * (now - timeout + 1000 * ACK_PLAYER_TIMEOUT),
           playerNo);
#endif
#endif
    ConnectResult result = (ackPlayer == CRNone) ? CRError : (ConnectResult)ackPlayer;
    leaveSnd();
    return result;
}

NetStatus clientSendComplete(NetMessage* msg, NetStatus event, void* data)
// data -> NetClient instance
// thread-safe
{
    NetClient* client = (NetClient*)data;
    if (!client || !msg || client->sessionTerminated)
    {
        return nsNoMoreCallbacks; // fatal error
    }
#ifdef NET_LOG_CLIENT_COMPLETE
    NetLog("Channel(%u)::clientSendComplete: status=%d, len=%3u, serial=%4u, flags=%04x, msgId=%x",
           client->channel->getChannelId(), (int)msg->getStatus(), msg->getLength(), msg->getSerial(),
           (unsigned)msg->getFlags(), msg->id);
#endif
    client->enterSnd();
    msg->next = client->sent;
    client->sent = msg;
    client->leaveSnd();
    return nsNoMoreCallbacks;
}

bool NetClient::SendMsg(BYTE* buffer, int bufferSize, DWORD& msgID, NetMsgFlags flags)
{
    bool vim = (flags & NMFGuaranteed) > 0;
    bool urgent = (flags & NMFHighPriority) > 0;
    enterSnd();
    if (!channel || !buffer || bufferSize <= 0)
    {
        leaveSnd();
        return false;
    }
    int maxMessage = channel->maxMessageData();
    int maxGuaranteedPayload =
        maxMessage > NetTransportReliableFragmentPayload ? NetTransportReliableFragmentPayload : maxMessage;
    if (!vim && bufferSize > maxMessage)
    {
        leaveSnd();
#ifdef NET_LOG_MERGE
#ifdef NET_LOG_BRIEF
        NetLog("Ch(%u):tooLargeC(%d,%x)", channel->getChannelId(), bufferSize, (unsigned)flags);
#else
        NetLog("Channel(%u): NetClient::SendMsg: trying to send too large non-guaranteed message (len=%3d, flags=%x)",
               channel->getChannelId(), bufferSize, (unsigned)flags);
#endif
#endif
        RptF("NetClient: trying to send too large non-guaranteed message (%d bytes long)", bufferSize);
        return false;
    }
    Ref<NetMessage> msg;
    if (vim)
    { // guaranteed message
        unsigned fl = MSG_VIM_FLAG | (urgent ? MSG_URGENT_FLAG : 0);
        if (bufferSize > maxGuaranteedPayload)
        { // split too big message:
#ifdef NET_LOG_MERGE
#ifdef NET_LOG_BRIEF
            NetLog("Ch(%u):sMerC(%d,%x)", channel->getChannelId(), bufferSize, (unsigned)flags);
#else
            NetLog("Channel(%u): NetClient::SendMsg: splitting user message (len=%3d, flags=%x)",
                   channel->getChannelId(), bufferSize, (unsigned)flags);
#endif
#endif
            int toSent = bufferSize;
            int packet;
            fl |= MSG_PART_FLAG;
            do
            {
                packet = (toSent > maxGuaranteedPayload) ? maxGuaranteedPayload : toSent;
                toSent -= packet;
                msg = NetMessagePool::pool()->newMessage(packet, channel);
                if (!msg)
                {
                    leaveSnd();
                    return false;
                }
                msg->setFlags(MSG_ALL_FLAGS, fl | (toSent ? 0 : MSG_CLOSING_FLAG));
                msg->setOrderedPrevious();
                msg->setData((unsigned8*)buffer, packet);
                buffer += packet;
                msg->send(urgent);
            } while (toSent);
        }
        else
        { // small message
            msg = NetMessagePool::pool()->newMessage(bufferSize, channel);
            msg->setFlags(MSG_ALL_FLAGS, fl);
            msg->setOrderedPrevious();
            msg->setData((unsigned8*)buffer, bufferSize);
            msg->send(urgent);
        }
    }
    else
    { // common message
        msg = NetMessagePool::pool()->newMessage(bufferSize, channel);
        if (!msg)
        {
            leaveSnd();
            return false;
        }
        msg->setCallback(clientSendComplete, nsOutputSent, this);
        msg->setSendTimeout(SEND_TIMEOUT);
        msg->setData((unsigned8*)buffer, bufferSize);
        msg->send();
    }
    msgID = (DWORD)msg->id;
#ifdef NET_LOG_CLIENT_SEND
    NetLog("Channel(%u): NetClient::SendMsg: len=%3d, flags=%x, msgID=%x, hdr=%08x%08x", channel->getChannelId(),
           bufferSize, (unsigned)flags, msgID, ((unsigned*)msg->getData())[0], ((unsigned*)msg->getData())[1]);
#endif
    leaveSnd();
    return true;
}

bool NetClient::SendRawMagic(int magic, BYTE* buffer, int bufferSize)
{
    enterSnd();
    NetPeer* peer = getClientPeer();
    bool ok = SendRawMagicPacket(peer, channel, rawMagicSerial, magic, buffer, bufferSize);
    leaveSnd();
    return ok;
}

void NetClient::GetSendQueueInfo(int& nMsg, int& nBytes, int& nMsgG, int& nBytesG)
{
    nMsg = nBytes = nMsgG = nBytesG = 0;
}

bool NetClient::GetConnectionInfo(int& latencyMS, int& throughputBPS)
{
    enterSnd();
    if (!channel)
    {
        leaveSnd();
        return false;
    }
    latencyMS = (int)(channel->getLatency() / 1000);
    throughputBPS = (int)channel->getOutputBandWidth();
    ;
#ifdef NET_LOG_INFO
    if (latencyMS != oldLatency || throughputBPS != oldThroughput)
    {
        NetLog("Channel(%u): NetClient::GetConnectionInfo: %d ms,%d B/s", channel->getChannelId(), latencyMS,
               throughputBPS);
        oldLatency = latencyMS;
        oldThroughput = throughputBPS;
        printAge = true;
    }
#endif
    leaveSnd();
    return true;
}

template <class Type>
Type LoadValue(const ParamEntry& cfg, const char* name, const Type& defVal)
{
    const ParamEntry* entry = cfg.FindEntry(name);
    if (!entry)
    {
        return defVal;
    }
    else
    {
        return *entry;
    }
}

void NetClient::SetNetworkParams(const ParamEntry& cfg)
{
    NetworkParams data;
    LoadNetworkParams(data, cfg);
    enterSnd();
    if (!channel)
    {
        leaveSnd();
        return;
    }
    channel->setNetworkParams(data);
    leaveSnd();
}

float NetClient::GetLastMsgAge()
{
    enterSnd();
    float result = 1.0;
    if (channel)
    {
        unsigned64 last = channel->getLastMessageArrival();
        unsigned64 now = getSystemTime();
        if (now < last)
        {
            LOG_DEBUG(Network, "Negative time delta in NetClient::GetLastMsgAge: now = {:8x}{:08x}, last = {:8x}{:08x}",
                      (unsigned)(now >> 32), (unsigned)(now & 0xffffffff), (unsigned)(last >> 32),
                      (unsigned)(last & 0xffffffff));
            result = 0;
        }
        else
        {
            unsigned64 delta = now - last;
            if (delta > 200000000)
            {
                RptF("Overflow in NetClient::GetLastMsgAge: now = %8x%08x, last = %8x%08x, delta = %.0f",
                     (unsigned)(now >> 32), (unsigned)(now & 0xffffffff), (unsigned)(last >> 32),
                     (unsigned)(last & 0xffffffff), (double)(1.e-6f * delta));
            }
            result = 1.e-6f * (unsigned)delta;
        }
    }
    leaveSnd();
    return result;
}

float NetClient::GetLastMsgAgeReported()
{
    return (1.e-6f * (getSystemTime() - lastMsgReported));
}

void NetClient::LastMsgAgeReported()
{
    lastMsgReported = getSystemTime();
}

bool NetClient::IsSessionTerminated()
{
    enterSnd();
    if (!channel)
    {
        leaveSnd();
        return true;
    }
    // check NetChannel connectivity:
    if (!amIBot && channel->dropped())
    {
        sessionTerminated = true;
        whySessionTerminated = NTRTimeout;
    }
    leaveSnd();
    return sessionTerminated;
}

NetTerminationReason NetClient::GetWhySessionTerminated()
{
    NetTerminationReason reason;
    enterSnd();
    reason = whySessionTerminated;

    leaveSnd();
    return reason;
}

void NetClient::ProcessUserMessages(UserMessageClientCallback* callback, void* context)
{
    if (!callback)
    {
        return;
    }
    Ref<NetMessage> msg;
    enterRcv();
#ifdef NET_LOG_CLIENT_PROCESS
    int n = 0;
    msg = received;
    while (msg)
    {
        n++;
        msg = msg->next;
    }
    if (n)
    {
        NetLog("Channel(%u): NetClient::ProcessUserMessages: processed %d messages:", channel->getChannelId(), n);
        msg = received;
        while (msg)
        {
            NetLog("Channel(%u): Processed: len=%3u, serial=%4u, flags=%04x, msgID=%x, hdr=%08x%08x",
                   channel->getChannelId(), msg->getLength(), msg->getSerial(), (unsigned)msg->getFlags(), msg->id,
                   ((unsigned*)msg->getData())[0], ((unsigned*)msg->getData())[1]);
            msg = msg->next;
        }
    }
#endif
    while (received)
    {
        msg = received;
        received = msg->next;
        msg->next = nullptr;
        leaveRcv();

        // Intercept VoN data packets before they reach the game message handler
        if (vonIsDataPacket(msg->getData(), msg->getLength()))
        {
            auto* c = _vonSystem.client();
            if (c && msg->getLength() >= VoNDataPacket::HEADER_SIZE)
            {
                VoNDataPacket hdr;
                std::memcpy(&hdr, msg->getData(), VoNDataPacket::HEADER_SIZE);
                c->onDataPacket(hdr, reinterpret_cast<const uint8_t*>(msg->getData()) + VoNDataPacket::HEADER_SIZE);
                _vonSpeakerChannels[hdr.channel] = hdr.chatChan;
                LOG_TRACE(Network, "VoN: rx voice pkt ch={} origin={} size={}B", hdr.channel, hdr.origin, hdr.size);

                // Auto-create speaker if we don't have one for this sender
                auto& spk = _vonSpeakers[hdr.channel];
                if (!spk)
                {
                    spk = std::make_unique<VoNSpeaker>();
                    spk->init();
                    LOG_INFO(Network, "VoN: auto-created speaker for channel {}", hdr.channel);
                }
                spk->setChannel(hdr.chatChan);
            }
        }
        else
        {
            (*callback)((char*)msg->getData(), msg->getLength(), context);
        }

        enterRcv();
    }
    leaveRcv();

    // Drive VoN client: capture → encode → send via packet sink
    if (auto* c = _vonSystem.client())
    {
        c->update();
        PumpVoiceSpeakers();
    }
}

void NetClient::insertReceived(NetMessage* msg)
// must be called inside enterRcv()
{
    if (received)
    {
        MsgSerial s = msg->getSerial();
        if (s < received->getSerial())
        {
            msg->next = received;
            received = msg;
        }
        else
        {
            NetMessage* ptr = received;
            while (ptr->next && ptr->next->getSerial() < s)
            {
                ptr = ptr->next;
            }
            msg->next = ptr->next;
            ptr->next = msg;
        }
    }
    else
    {
        msg->next = nullptr;
        received = msg;
    }
}

void NetClient::ProcessRawMagicMessages(RawMagicClientCallback* callback, void* context)
{
    if (!callback)
    {
        return;
    }
    while (true)
    {
        enterRcv();
        if (rawMagicReceived.Size() <= 0)
        {
            leaveRcv();
            break;
        }
        RawMagicMessage item;
        item.from = rawMagicReceived[0].from;
        item.magic = rawMagicReceived[0].magic;
        item.data.Resize(rawMagicReceived[0].data.Size());
        if (item.data.Size() > 0)
        {
            memcpy(item.data.Data(), rawMagicReceived[0].data.Data(), item.data.Size());
        }
        rawMagicReceived.Delete(0);
        leaveRcv();
        (*callback)(item.magic, item.data.Data(), item.data.Size(), context);
    }
}

void NetClient::RemoveUserMessages()
{
    enterRcv();
    Ref<NetMessage> tmp;
    while (received)
    {
        tmp = received->next;
        received->next = nullptr; // don't need this message anymore, somebody will recycle it later
        received = tmp;
    }
    rawMagicReceived.Clear();
    leaveRcv();
}

void NetClient::ProcessSendComplete(SendCompleteCallback* callback, void* context)
{
    if (!callback)
    {
        return;
    }
    enterSnd();
    NetMessage* msg;
#ifdef NET_LOG_CLIENT_COMPLETE
    int n = 0;
    msg = sent;
    while (msg)
    {
        n++;
        msg = msg->next;
    }
    if (n)
    {
        NetLog("Channel(%u): NetClient::ProcessSendComplete: processed %d messages:", channel->getChannelId(), n);
        msg = sent;
        while (msg)
        {
            NetLog("Channel(%u): Processed: ok=%d(%d), len=%3u, serial=%4u, flags=%04x, MsgID=%x, hdr=%08x%08x",
                   channel->getChannelId(), (int)(msg->getStatus() != nsOutputObsolete), (int)msg->getStatus(),
                   msg->getLength(), msg->getSerial(), (unsigned)msg->getFlags(), msg->id,
                   ((unsigned*)msg->getData())[0], ((unsigned*)msg->getData())[1]);
            msg = msg->next;
        }
    }
#endif
    msg = sent;
    while (msg)
    {
        (*callback)((DWORD)msg->id, msg->wasSent(), context);
        msg = msg->next;
    }
    RemoveSendComplete();
    leaveSnd();
}

void NetClient::RemoveSendComplete()
{
    enterSnd();
    Ref<NetMessage> tmp;
    while (sent)
    {
        tmp = sent->next;
        sent->next = nullptr; // don't need this message anymore, somebody will recycle it later
        sent = tmp;
    }
    leaveSnd();
}

#ifdef NET_LOG_TRANSP_STAT
static int getStatisticsCount = 0;
#endif

RString NetClient::GetStatistics()
{
    enterSnd();
    if (!channel)
    {
        leaveSnd();
        return RString("No NetChannel is connected to this NetClient");
    }
    int latencyAve;
    unsigned latencyAct, latencyMin;
    int throughputAve;
    latencyAve = (int)(channel->getLatency(&latencyAct, &latencyMin) / 1000);
    latencyAct /= 1000;
    latencyMin /= 1000;
    EnhancedBWInfo enhanced;
    throughputAve = (int)channel->getOutputBandWidth(&enhanced);
    int nMsg, nBytes, nMsgG, nBytesG;
    channel->getOutputQueueStatistics(nMsg, nBytes, nMsgG, nBytesG);
    ChannelStatistics stat; // internal statistics of the NetChannel
    Zero(stat);
    channel->getInternalStatistics(stat);
    leaveSnd();
    char buf[256];
    bool kbps = (throughputAve < (9 << 17));
    snprintf(buf, sizeof(buf),
             "ping%4dms(%4u,%4u) BW%c%c%c%5d%cb(%4u,%4u,%4u) lost%4.1f%%%%(%3u) queue%4dB(%4d) ackWait%3u(%3.1f,%3.1f)",
             latencyAve, latencyAct, latencyMin, (char)(enhanced.growMode + 'c'), (char)(enhanced.growModePing + 'c'),
             (char)(enhanced.growModeLost + 'c'), kbps ? ((throughputAve + 64) >> 7) : ((throughputAve + 65536) >> 17),
             kbps ? 'K' : 'M', (enhanced.actBW + 64) >> 7, (enhanced.goodBW + 64) >> 7, (enhanced.sentBW + 64) >> 7,
             stat.ackTotal ? (stat.ackLost * 100.0) / stat.ackTotal : 0.0, stat.ackLost, nBytes, nBytesG,
             stat.revisitedNo, 1e-6 * stat.revisitedAveAge, 1e-6 * stat.revisitedMaxAge);
#ifdef NET_LOG_TRANSP_STAT
    unsigned64 now = getSystemTime();
    if ((!amIBot && now >= nextStatLog) || getStatisticsCount > 100)
    {
        NetLog("Channel(%u): NetClient(%d)[%d] - %s", channel->getChannelId(), playerNo, getStatisticsCount, buf);
        getStatisticsCount = 0;
        nextStatLog = now + STAT_LOG_INTERVAL;
    }
    else
        getStatisticsCount++;
#endif
    return RString(buf);
}

unsigned NetClient::FreeMemory()
{
    if (!NetMessagePool::pool())
    {
        return 0;
    }
    unsigned ret = NetMessagePool::pool()->freeMemory();
    SafeMemoryCleanUp();
    return ret;
}

NetServer::NetServer()
    : LockInit(usrCs, "NetServer::usrCs", true), LockInit(rcvCs, "NetServer::rcvCs", true),
      LockInit(sndCs, "NetServer::sndCs", true)
{
    // to be sure:
    enterUsr();
    received = nullptr;
    sent = nullptr;
    rawMagicSerial = 0x71000000;
    session.actualVersion = 0;
    session.requiredVersion = 0;
    session.gameState = 0;
    session.maxPlayers = 0;
    session.numPlayers = 0;
    session.password = false;
    session.port = 0;
    session.name[0] = (char)0;
    session.mission[0] = (char)0;
    session.mod[0] = 0;
    session.versionTag[0] = 0;
    session.equalModRequired = 0;
    botId = 0;
    magicApp = 0;
    // Cap reflected enum replies: 100 burst, 50/s sustained — orders of magnitude
    // above a legitimate browser's one-request-per-refresh, keeping reply traffic
    // bounded.
    _enumReplyBucket.configure(/*ratePerSec*/ 50.0, /*burst*/ 100.0);
#ifdef NET_LOG_INFO
    oldLatency = -1;
    oldThroughput = -1;
#endif
#ifdef NET_LOG_TRANSP_STAT
    nextStatLog = getSystemTime();
    forceLog = inGetConnection = false;
#endif
    setServer(this);
    // create a new NetPeer:
    poolLock.enter();
    sessionPort = getServerPeer() ? getServerPeer()->getPort() : 0;
    poolLock.leave();
    leaveUsr();
#ifdef NET_LOG_SERVER
#ifdef NET_LOG_BRIEF
    NetLog("Pe(%u):srv", getServerPeer() ? getServerPeer()->getPeerId() : 0);
#else
    NetLog("Peer(%u): creating NetServer instance", getServerPeer() ? getServerPeer()->getPeerId() : 0);
#endif
#endif
}

NetServer::~NetServer()
{
    // kick-off all the players (must happen BEFORE closing the peer,
    // so the UDP sender thread can actually transmit the MAGIC_TERMINATE_SESSION messages):
    IteratorState it;
    enterUsr();
    RefD<NetChannel> ch;
    if (users.getFirst(it, ch))
    {
        do
        {
            destroyPlayer(ch, NTRDisconnected);
        } while (users.getNext(it, ch));
    }
    leaveUsr();
    SLEEP_MS(DESTRUCT_WAIT); // waits till all players are gone..

    // Now stop UDP threads and clean up the serverPeer.
    // The serverPeer is global/static, so we need to explicitly clean it up
    // to prevent threads from running after this NetServer instance is destroyed.
    // serverPeer->close() joins the UDP listener, which itself takes poolLock
    // inside ctrlReceive; joining under poolLock deadlocks, so the join runs with
    // poolLock released. serverPeer stays set across the join so a late
    // ctrlReceive resolves the existing peer instead of creating a fresh one.
    poolLock.enter();
    RefD<NetPeer> localServerPeer = serverPeer;
    if (localServerPeer)
    {
        JoinServerPeerWithoutPoolLock(poolLock, [&]() { localServerPeer->close(); });
        if (pool)
        {
            pool->deletePeer(localServerPeer); // Remove from pool
        }
        serverPeer = nullptr; // Reset global so next attempt creates fresh peer
    }
    poolLock.leave();

#ifdef NET_LOG_SERVER
#ifdef NET_LOG_BRIEF
    NetLog("Pe(%u):~srv", getServerPeer()->getPeerId());
#else
    NetLog("Peer(%u): destroying NetServer instance", getServerPeer()->getPeerId());
#endif
#endif
    CancelAllMessages();
    setServer(nullptr);
    // recycle all pending NetMessages:
    RemoveUserMessages();
    RemoveSendComplete();
    // remove other data:
    RemovePlayers();
    RemoveVoicePlayers();
    checkPool();
    FreeMemory();
}

void NetServer::GetSendQueueInfo(int to, int& nMsg, int& nBytes, int& nMsgG, int& nBytesG)
{
    enterUsr();
    RefD<NetChannel> channel;
    if (users.get(to, channel))
    {
        channel->getOutputQueueStatistics(nMsg, nBytes, nMsgG, nBytesG);
    }
    else
    {
        nMsg = nBytes = nMsgG = nBytesG = 0;
    }
    leaveUsr();
}

bool NetServer::GetConnectionInfo(int to, int& latencyMS, int& throughputBPS)
{
    enterUsr();
    RefD<NetChannel> channel;
    if (!users.get(to, channel))
    {
        leaveUsr();
        return false;
    }
    latencyMS = (int)(channel->getLatency() / 1000);
    throughputBPS = (int)channel->getOutputBandWidth();
#ifdef NET_LOG_INFO
    if (latencyMS != oldLatency || throughputBPS != oldThroughput)
    {
        NetLog("Channel(%u): NetServer::GetConnectionInfo(player=%d): %d ms,%d B/s", channel->getChannelId(), to,
               latencyMS, throughputBPS);
        oldLatency = latencyMS;
        oldThroughput = throughputBPS;
    }
#endif
    // check the channel condition:
    bool dropped = (to != botId) && channel->dropped();
    if (dropped)
    {
        finishDestroyPlayer(to);
    }
#ifdef DEDICATED_STAT_LOG
    if (IsDedicatedServer())
    {
        unsigned64 now = getSystemTime();
#ifdef NET_LOG_TRANSP_STAT
        if (true)
        {
            const bool doConsole = (now >= nextConsoleLog);
#else
        if (now >= nextConsoleLog)
        {
            const bool doConsole = true;
#endif
            if (doConsole)
                nextConsoleLog = now + CONSOLE_LOG_INTERVAL;
            IteratorState it;
            if (users.getFirst(it, channel, &to))
                do
                    if (to != botId)
                    {
                        RString stat = GetStatistics(to);
                        if (doConsole)
                            LOG_INFO(Network, "Player {}: {}", to, (const char*)stat);
                    }
                while (users.getNext(it, channel, &to));
        }
    }
#else
#ifdef NET_LOG_TRANSP_STAT
    IteratorState it;
    forceLog = false;
    inGetConnection = true;
    for (users.getFirst(it, channel, &to); channel; users.getNext(it, channel, &to))
        if (to != botId)
            GetStatistics(to);
    inGetConnection = false;
#endif
#endif
    leaveUsr();
    return true;
}

float NetServer::GetLastMsgAgeReliable(int player)
{
    float result = 1.0;

    enterUsr();
    RefD<NetChannel> channel;
    if (users.get(player, channel))
    {
        unsigned64 last = channel->getLastMessageArrival();
        unsigned64 now = getSystemTime();
        if (now < last)
        {
            LOG_DEBUG(Network,
                      "Negative time delta in NetServer::GetLastMsgAgeReliable: now = {:8x}{:08x}, last = {:8x}{:08x}",
                      (unsigned)(now >> 32), (unsigned)(now & 0xffffffff), (unsigned)(last >> 32),
                      (unsigned)(last & 0xffffffff));
            result = 0;
        }
        else
        {
            unsigned64 delta = now - last;
            if (delta > 200000000)
            {
                RptF("Overflow in NetServer::GetLastMsgAgeReliable: now = %8x%08x, last = %8x%08x, delta = %.0f",
                     (unsigned)(now >> 32), (unsigned)(now & 0xffffffff), (unsigned)(last >> 32),
                     (unsigned)(last & 0xffffffff), (1.e-6 * delta));
            }
            result = 1.e-6f * (unsigned)delta;
        }
    }

    leaveUsr();
    return result;
}

void NetServer::SetNetworkParams(const ParamEntry& cfg)
{
    NetworkParams data;
    LoadNetworkParams(data, cfg);
    enterUsr();
    IteratorState it;
    RefD<NetChannel> channel;
    if (!users.getFirst(it, channel))
    {
        leaveUsr();
        return;
    }
    channel->setNetworkParams(data);
    leaveUsr();
}

NetStatus destroyPlayerCallback(NetMessage* msg, NetStatus event, void* data)
// data = player number
{
    poolLock.enter();
    if (_server)
    {
        _server->finishDestroyPlayer((int)(intptr_t)data);
    }
    poolLock.leave();
    return nsNoMoreCallbacks;
}

void NetServer::destroyPlayer(NetChannel* ch, NetTerminationReason reason)
{
    NET_ERROR(ch);
#ifdef NET_LOG_SERVER_KICKOFF
    NetLog("Channel(%u): NetServer::destroyPlayer: player=%d", ch->getChannelId(), channelToPlayer(ch));
#endif
    // send MAGIC_TERMINATE_SESSION message to the client:
    unsigned32 magic[2];
    Ref<NetMessage> out = NetMessagePool::pool()->newMessage(sizeof(magic), ch);
    if (!out)
    {
        return;
    }
    magic[0] = MAGIC_TERMINATE_SESSION;
    magic[1] = reason;
    out->setFlags(MSG_ALL_FLAGS, MSG_MAGIC_FLAG);
    out->setData((unsigned8*)&magic, sizeof(magic));
    out->setCallback(destroyPlayerCallback, nsOutputSent, (void*)(intptr_t)channelToPlayer(ch));
    out->send(true);
}

void NetServer::logUsers()
{
    char usersText[1024];

    IteratorState it;
    int key;
    RefD<NetChannel> channel;
    snprintf(usersText, sizeof(usersText), "%d: ", users.card());
    if (users.getFirst(it, channel, &key))
    {
        do
        {
            sprintf(usersText + strlen(usersText), " %d", key);
        } while (users.getNext(it, channel, &key));
    }
    RptF("NetServer::logUsers %s", usersText);
}

void NetServer::finishDestroyPlayer(int player)
{
    enterUsr();
    RefD<NetChannel> ch;
    if (!users.get(player, ch))
    {
        RptF("NetServer::finishDestroyPlayer(%d): users.get failed", player);
        NET_ERROR(users.checkIntegrity());
        leaveUsr();
        return;
    }
    // delete player from my structures:
    users.remove(player);
    struct sockaddr_in daddr;
    ch->getDistantAddress(daddr);
    m_support.remove(sockaddrKey(daddr));
    NET_ERROR(users.checkIntegrity());
    getPool()->deleteChannel(ch);
    int index;
    for (index = 0; index < _createPlayers.Size(); index++)
    {
        if (_createPlayers[index].player == player)
        {
            RptF("NetServer::finishDestroyPlayer(%d): DESTROY immediately after CREATE, both cancelled", player);
            _createPlayers.Delete(index);
            leaveUsr();
            return;
        }
    }
    index = _deletePlayers.Add();
    DeletePlayerInfo& info = _deletePlayers[index];
    info.player = player;
    RptF("NetServer:finishDestroyPlayer (waiting for ProcessPlayers) - session.numPlayers=%d, playerId=%d, |users|=%u",
         session.numPlayers, info.player, users.card());
    leaveUsr();
}

void NetServer::KickOff(int player, NetTerminationReason reason)
{
    enterUsr();
    RefD<NetChannel> channel;
    if (!users.get(player, channel))
    {
        RptF("NetServer::KickOff: player=%d - !users.get", player);
        leaveUsr();
        return;
    }
#ifdef NET_LOG_SERVER_KICKOFF
    NetLog("Channel(%u): NetServer::KickOff: player=%d", channel->getChannelId(), player);
#endif
    destroyPlayer(channel, reason);
    leaveUsr();
}

bool NetServer::InitVoice()
{
    _vonSystem.initServer();
    auto* srv = _vonSystem.server();
    if (!srv)
        return false;

    // Wire VoN server forwarder to relay voice packets to target players
    srv->setForwarder(
        [this](uint32_t target, const void* data, int size)
        {
            DWORD msgID;
            SendMsg(static_cast<int>(target), const_cast<BYTE*>(reinterpret_cast<const BYTE*>(data)), size, msgID,
                    NMFHighPriority);
        });

    LOG_INFO(Network, "VoN server initialized");
    return true;
}

void NetServer::GetTransmitTargets(int from, AutoArray<int, MemAllocSA>& to)
{
    auto it = _vonTargets.find(from);
    if (it == _vonTargets.end())
        return;
    for (int t : it->second)
        to.Add(t);
}

void NetServer::SetTransmitTargets(int from, AutoArray<int, MemAllocSA>& to, int channel)
{
    std::vector<uint32_t> targets;
    targets.reserve(to.Size());
    std::vector<int>& stored = _vonTargets[from];
    stored.clear();
    stored.reserve(to.Size());
    for (int i = 0; i < to.Size(); ++i)
    {
        stored.push_back(to[i]);
        targets.push_back(static_cast<uint32_t>(to[i]));
    }
    auto* srv = _vonSystem.server();
    if (srv)
        srv->setRouting(static_cast<uint32_t>(from), NetTransportChatChannelToVoN(channel), targets);
    else
        LOG_WARN(Network, "VoN# srv SetTransmitTargets from={} NO-SERVER", from);
    LOG_DEBUG(Network, "VoN# srv SetTransmitTargets from={} gameCh={} vonCh={} targets={}", from, channel,
              static_cast<int>(NetTransportChatChannelToVoN(channel)), to.Size());
}

void NetServer::ProcessVoicePlayers(CreateVoicePlayerCallback* callback, void* context)
{
    // Notify game layer about each connected voice player
    for (auto& [sender, targets] : _vonTargets)
    {
        if (callback)
            callback(sender, context);
    }
}

void NetServer::RemoveVoicePlayers()
{
    auto* srv = _vonSystem.server();
    if (srv)
    {
        for (auto& [sender, targets] : _vonTargets)
            srv->removePlayer(static_cast<uint32_t>(sender));
    }
    _vonTargets.clear();
}

bool NetServer::Init(int port, RString password, char* hostname, int maxPlayers, RString sessionNameInit,
                     RString sessionNameFormat, RString playerName, MPVersionInfo& versionInfo, bool equalModRequired,
                     int magic)
{
    if (!sessionPort)
    {
        if (IsDedicatedServer())
        {
            LOG_ERROR(Network, "Cannot start server on port {}", port);
        }
        return false;
    }
    // initialize session attributes:
    enterUsr();
    _password = password;
    magicApp = magic;
    session.actualVersion = versionInfo.versionActual;
    session.requiredVersion = versionInfo.versionRequired;
    session.equalModRequired = equalModRequired ? 1 : 0;
    session.maxPlayers = maxPlayers;
    session.numPlayers = 0;
    session.password = password.GetLength() > 0;
    session.port = sessionPort;
    char localName[128];
    if (!hostname || !hostname[0])
    { // get more friendly form of localhost
        if (getLocalName(localName, 128))
        {
            hostname = localName;
        }
        else
        {
            hostname = nullptr;
        }
    }
    char buf[512];
    if (hostname)
    {
        if (playerName.GetLength() > 0)
        {
            snprintf(buf, 512, (const char*)sessionNameFormat, (const char*)playerName, hostname);
        }
        else
        {
            strncpy(buf, hostname, 512);
        }
    }
    else
    {
        strncpy(buf, sessionNameInit, 512);
    }
    buf[511] = (char)0;
    strncpy(session.name, buf, sizeof(session.name));
    session.name[sizeof(session.name) - 1] = (char)0;
    sessionName = buf;

    strncpy(session.mod, versionInfo.mod, sizeof(session.mod));
    session.mod[sizeof(session.mod) - 1] = (char)0;

    strncpy(session.versionTag, versionInfo.versionTag, sizeof(session.versionTag));
    session.versionTag[sizeof(session.versionTag) - 1] = (char)0;

    session.mission[0] = 0;
    session.gameState = 0;
#ifdef DEDICATED_STAT_LOG
    nextConsoleLog = getSystemTime();
#endif
#ifdef NET_LOG_SERVER
#ifdef NET_LOG_BRIEF
    NetLog("Pe(%u):srv(%d,'%s','%s','%s',%d)", getServerPeer()->getPeerId(), sessionPort, hostname, session.name,
           (const char*)playerName, magic);
#else
    NetLog("Peer(%u): NetServer::Init: local port=%d, hostname='%s', sessionName='%s', playerName='%s', magic=%d",
           getServerPeer()->getPeerId(), sessionPort, hostname, session.name, (const char*)playerName, magic);
#endif
#endif
    leaveUsr();
    return true;
}

NetStatus serverSendComplete(NetMessage* msg, NetStatus event, void* data)
// data -> NetServer instance
// thread-safe
{
    NetServer* server = (NetServer*)data;
    if (!server || !msg)
    {
        return nsNoMoreCallbacks; // fatal error
    }
#ifdef NET_LOG_SERVER_COMPLETE
    NetLog("Channel(%u)::serverSendComplete: status=%d, len=%3u, serial=%4u, flags=%04x, msgId=%x",
           msg->getChannel()->getChannelId(), (int)msg->getStatus(), msg->getLength(), msg->getSerial(),
           (unsigned)msg->getFlags(), msg->id);
#endif
    server->enterSnd();
    msg->next = server->sent;
    server->sent = msg;
    server->leaveSnd();
    return nsNoMoreCallbacks;
}

bool NetServer::SendMsg(int to, BYTE* buffer, int bufferSize, DWORD& msgID, NetMsgFlags flags)
{
    if (!buffer || bufferSize <= 0)
    {
        RptF("NetServer: trying to send empty message to %d", to);
        return false;
    }
    RefD<NetChannel> channel;
    Ref<NetMessage> msg;
    bool vim = (flags & NMFGuaranteed) > 0;
    bool urgent = (flags & NMFHighPriority) > 0;
    int maxMessage = getServerPeer()->maxMessageData();
    int maxGuaranteedPayload =
        maxMessage > NetTransportReliableFragmentPayload ? NetTransportReliableFragmentPayload : maxMessage;
    if ((!vim || to == DPNID_ALL_PLAYERS_GROUP) && bufferSize > maxMessage)
    {
#ifdef NET_LOG_MERGE
        unsigned chId = 0;
        if (to != DPNID_ALL_PLAYERS_GROUP && users.get(to, channel))
            chId = channel->getChannelId();
#ifdef NET_LOG_BRIEF
        NetLog("Ch(%u):tooLargeS(%d,%d,%x)", chId, to, bufferSize, (unsigned)flags);
#else
        NetLog("Channel(%u): NetServer::SendMsg: trying to send too large non-guaranteed message (to=%d, len=%3d, "
               "flags=%x)",
               chId, to, bufferSize, (unsigned)flags);
#endif
#endif
        RptF("NetServer: trying to send too large non-guaranteed message (%d bytes long)", bufferSize);
        return false;
    }
    unsigned16 fl = 0;
    if (vim)
    {
        fl |= MSG_VIM_FLAG;
    }
    if (urgent)
    {
        fl |= MSG_URGENT_FLAG;
    }

    enterUsr();
    if (to == DPNID_ALL_PLAYERS_GROUP)
    { // broadcast to all players
#ifdef NET_LOG_SERVER_SEND
        NetLog("Peer(%u): NetServer::SendMsg: broadcast to all players", getServerPeer()->getPeerId());
#endif
        IteratorState it;
        if (users.getFirst(it, channel))
        {
            do
            {
                msg = NetMessagePool::pool()->newMessage(bufferSize, channel);
                if (!msg)
                {
                    leaveUsr();
                    RptF("NetServer: pool()->newMessage failed when sending to %d", to);
                    return false;
                }
                msg->setFlags(MSG_ALL_FLAGS, fl);
                if (fl)
                {
                    msg->setOrderedPrevious(); // VIM is set ORDERED automatically
                }
                else
                { // common messages
                    msg->setCallback(serverSendComplete, nsOutputSent, this);
                    msg->setSendTimeout(SEND_TIMEOUT);
                }
                msg->setData((unsigned8*)buffer, bufferSize);
                msg->send(urgent);
#ifdef NET_LOG_SERVER_SEND
                NetLog("Channel(%u): NetServer::SendMsg: to=%d, len=%3d, flags=%x, msgID=%x, hdr=%08x%08x",
                       channel->getChannelId(), to, bufferSize, (unsigned)flags, (unsigned)msg->id,
                       ((unsigned*)msg->getData())[0], ((unsigned*)msg->getData())[1]);
#endif
            } while (users.getNext(it, channel));
        }
    }

    else
    { // single recipient
        if (!users.get(to, channel))
        {
#ifdef NET_LOG_SERVER_SEND
            NetLog("Peer(%u): NetServer::SendMsg: cannot find channel #%d, users.card=%u", getServerPeer()->getPeerId(),
                   to, users.card());
#endif
            RptF("NetServer::SendMsg: cannot find channel #%d, users.card=%u", to, users.card());
            RptF("NetServer: users.get failed when sending to %d", to);
            NET_ERROR(users.checkIntegrity());
            leaveUsr();
            return false;
        }
        if (bufferSize > maxGuaranteedPayload)
        { // split too big message:
#ifdef NET_LOG_MERGE
#ifdef NET_LOG_BRIEF
            NetLog("Ch(%u):sMerS(%d,%d,%x)", channel->getChannelId(), to, bufferSize, (unsigned)flags);
#else
            NetLog("Channel(%u): NetServer::SendMsg: splitting user message (to=%d, len=%3d, flags=%x)",
                   channel->getChannelId(), to, bufferSize, (unsigned)flags);
#endif
#endif
            int toSent = bufferSize;
            int packet;
            fl |= MSG_PART_FLAG;
            do
            {
                packet = (toSent > maxGuaranteedPayload) ? maxGuaranteedPayload : toSent;
                toSent -= packet;
                msg = NetMessagePool::pool()->newMessage(packet, channel);
                if (!msg)
                {
                    leaveSnd();
                    RptF("NetServer: pool()->newMessage failed when sending to %d", to);
                    return false;
                }
                msg->setFlags(MSG_ALL_FLAGS, fl | (toSent ? 0 : MSG_CLOSING_FLAG));
                msg->setOrderedPrevious();
                msg->setData((unsigned8*)buffer, packet);
                buffer += packet;
                msg->send(urgent);
            } while (toSent);
        }
        else
        { // small message
            msg = NetMessagePool::pool()->newMessage(bufferSize, channel);
            if (!msg)
            {
                leaveUsr();
                RptF("NetServer: pool()->newMessage failed when sending to %d", to);
                return false;
            }
            msg->setFlags(MSG_ALL_FLAGS, fl);
            if (fl)
            {
                msg->setOrderedPrevious(); // VIM is set ORDERED automatically
            }
            else
            { // common messages
                msg->setCallback(serverSendComplete, nsOutputSent, this);
                msg->setSendTimeout(SEND_TIMEOUT);
            }
            msg->setData((unsigned8*)buffer, bufferSize);
            msg->send(urgent);
        }
#ifdef NET_LOG_SERVER_SEND
        NetLog("Channel(%u): NetServer::SendMsg: to=%d, len=%3d, flags=%x, msgID=%x, hdr=%08x%08x",
               channel->getChannelId(), to, bufferSize, (unsigned)flags, (unsigned)msg->id,
               ((unsigned*)msg->getData())[0], ((unsigned*)msg->getData())[1]);
#endif
    }

    msgID = (DWORD)msg->id;
    leaveUsr();
    return true;
}

bool NetServer::SendRawMagic(int to, int magic, BYTE* buffer, int bufferSize)
{
    enterUsr();
    RefD<NetChannel> channel;
    bool found = users.get(to, channel);
    NetPeer* peer = getServerPeer();
    bool ok = found && SendRawMagicPacket(peer, channel, rawMagicSerial, magic, buffer, bufferSize);
    leaveUsr();
    return ok;
}

void NetServer::CancelAllMessages()
{
    poolLock.enter();
    if (getServerPeer())
    {
        getServerPeer()->cancelAllMessages();
    }
    poolLock.leave();
}

void NetServer::UpdateSessionDescription(int state, RString mission)
{
    enterUsr();
    session.gameState = state;
    strncpy(session.mission, mission, LEN_MISSION_NAME);
    session.mission[LEN_MISSION_NAME - 1] = (char)0;
    leaveUsr();
}

bool NetServer::GetURL(char* address, DWORD addressLen)
{
    poolLock.enter();
    bool result = (getServerPeer() != nullptr);
    if (result)
    {
        NET_ERROR(address);
        struct sockaddr_in local;
        getLocalAddress(local, sessionPort);
        snprintf(address, addressLen, "%u.%u.%u.%u:%u", (unsigned)IP4(local), (unsigned)IP3(local),
                 (unsigned)IP2(local), (unsigned)IP1(local), (unsigned)sessionPort);
    }
    poolLock.leave();
    return result;
}

int NetServer::channelToPlayer(NetChannel* ch)
{
    if (!ch)
    {
        return -1;
    }
    IteratorState it;
    int i;
    enterUsr();
    RefD<NetChannel> itch;
    if (users.getFirst(it, itch, &i))
    {
        do
        {
            if ((NetChannel*)itch == ch)
            {
                break;
            }
        } while (users.getNext(it, itch, &i));
    }
    if (!itch)
    {
        i = -1;
    }
    leaveUsr();
    return i;
}

RString NetServer::GetPlayerHostIP(int player)
{
    char buffer[24] = {};
    if (!GetPlayerHostIP(player, buffer, sizeof(buffer)))
    {
        return RString();
    }
    return RString(buffer);
}

bool NetServer::GetPlayerHostIP(int player, char* buffer, int bufferSize)
{
    enterUsr();
    RefD<NetChannel> ch;
    const bool found = users.get(player, ch);
    leaveUsr();
    if (!buffer || bufferSize <= 0)
    {
        return false;
    }
    buffer[0] = 0;
    if (!found || !ch)
    {
        return false;
    }
    struct sockaddr_in distant;
    ((NetChannel*)ch)->getDistantAddress(distant);
    snprintf(buffer, bufferSize, "%u.%u.%u.%u", (unsigned)IP4(distant), (unsigned)IP3(distant), (unsigned)IP2(distant),
             (unsigned)IP1(distant));
    buffer[bufferSize - 1] = 0;
    return true;
}

void NetServer::ProcessUserMessages(UserMessageServerCallback* callback, void* context)
{
    if (!callback)
    {
        return;
    }
    Ref<NetMessage> msg;
    enterRcv();
#ifdef NET_LOG_SERVER_PROCESS
    int n = 0;
    msg = received;
    while (msg)
    {
        n++;
        msg = msg->next;
    }
    if (n)
    {
        NetLog("Peer(%u): NetServer::ProcessUserMessages: processed %d messages:", getServerPeer()->getPeerId(), n);
        msg = received;
        while (msg)
        {
            NetLog(
                "Channel(%u): NetServer::ProcessUserMessages: len=%3u, serial=%4u, flags=%04x, msgID=%x, hdr=%08x%08x",
                msg->getChannel()->getChannelId(), msg->getLength(), msg->getSerial(), (unsigned)msg->getFlags(),
                msg->id, ((unsigned*)msg->getData())[0], ((unsigned*)msg->getData())[1]);
            msg = msg->next;
        }
    }
#endif
    while (received)
    {
        msg = received;
        received = msg->next;
        msg->next = nullptr;
        // process the "msg" message:
        enterUsr();
        int player = channelToPlayer(msg->getChannel());
        leaveUsr();
        leaveRcv();
        if (player == -1)
        {
            RptF("No player found for channel %u - message ignored", (unsigned)(uintptr_t)msg->getChannel());
        }
        else if (vonIsDataPacket(msg->getData(), msg->getLength()))
        {
            // VoN voice data — route through VoN server to targets
            auto* srv = _vonSystem.server();
            if (srv)
            {
                srv->onDataPacket(msg->getData(), msg->getLength());
                LOG_TRACE(Network, "VoN srv: routed voice pkt ({}B) from player={}", msg->getLength(), player);
            }
        }
        else
        {
            (*callback)(player, (char*)msg->getData(), msg->getLength(), context);
        }
        enterRcv();
    }
    leaveRcv();
}

void NetServer::ProcessRawMagicMessages(RawMagicServerCallback* callback, void* context)
{
    if (!callback)
    {
        return;
    }
    while (true)
    {
        enterRcv();
        if (rawMagicReceived.Size() <= 0)
        {
            leaveRcv();
            break;
        }
        RawMagicMessage item;
        item.from = rawMagicReceived[0].from;
        item.magic = rawMagicReceived[0].magic;
        item.data.Resize(rawMagicReceived[0].data.Size());
        if (item.data.Size() > 0)
        {
            memcpy(item.data.Data(), rawMagicReceived[0].data.Data(), item.data.Size());
        }
        rawMagicReceived.Delete(0);
        leaveRcv();
        (*callback)(item.from, item.magic, item.data.Data(), item.data.Size(), context);
    }
}

void NetServer::insertReceived(NetMessage* msg)
// must be called inside enterRcv()
{
    if (received)
    {
        MsgSerial s = msg->getSerial();
        if (s < received->getSerial())
        {
            msg->next = received;
            received = msg;
        }
        else
        {
            NetMessage* ptr = received;
            while (ptr->next && ptr->next->getSerial() < s)
            {
                ptr = ptr->next;
            }
            msg->next = ptr->next;
            ptr->next = msg;
        }
    }
    else
    {
        msg->next = nullptr;
        received = msg;
    }
}

void NetServer::RemoveUserMessages()
{
    enterRcv();
    Ref<NetMessage> tmp;
    while (received)
    {
        tmp = received->next;
        received->next = nullptr; // don't need this message anymore, somebody will recycle it later
        received = tmp;
    }
    rawMagicReceived.Clear();
    leaveRcv();
}

void NetServer::ProcessSendComplete(SendCompleteCallback* callback, void* context)
{
    if (!callback)
    {
        return;
    }
    enterSnd();
    NetMessage* msg;
#ifdef NET_LOG_SERVER_COMPLETE
    int n = 0;
    msg = sent;
    while (msg)
    {
        n++;
        msg = msg->next;
    }
    if (n)
    {
        NetLog("Peer(%u): NetServer::ProcessSendComplete: processed %d messages:", getServerPeer()->getPeerId(), n);
        msg = sent;
        while (msg)
        {
            NetLog("Channel(%u): NetServer::ProcessSendComplete: ok=%d(%d), len=%4u, serial=%3u, flags=%04x, MsgID=%x, "
                   "hdr=%08x%08x",
                   msg->getChannel()->getChannelId(), (int)(msg->getStatus() != nsOutputObsolete),
                   (int)msg->getStatus(), msg->getLength(), msg->getSerial(), (unsigned)msg->getFlags(), msg->id,
                   ((unsigned*)msg->getData())[0], ((unsigned*)msg->getData())[1]);
            msg = msg->next;
        }
    }
#endif
    msg = sent;
    while (msg)
    {
        (*callback)((DWORD)msg->id, msg->wasSent(), context);
        msg = msg->next;
    }
    RemoveSendComplete();
    leaveSnd();
}

void NetServer::RemoveSendComplete()
{
    enterSnd();
    Ref<NetMessage> tmp;
    while (sent)
    {
        tmp = sent->next;
        sent->next = nullptr; // don't need this message anymore, somebody will recycle it later
        sent = tmp;
    }
    leaveSnd();
}

void NetServer::ProcessPlayers(CreatePlayerCallback* callbackCreate, DeletePlayerCallback* callbackDelete,
                               void* context)
{
    int i;
    enterUsr();
    if (_deletePlayers.Size() || _createPlayers.Size())
    {
        RptF("NetServer::ProcessPlayers(): users.card=%u, session.numPlayers=%d, created=%d, deleted=%d", users.card(),
             session.numPlayers, _createPlayers.Size(), _deletePlayers.Size());
    }
    for (i = 0; i < _deletePlayers.Size(); i++)
    {
        DeletePlayerInfo& info = _deletePlayers[i];
        callbackDelete(info.player, context);
        session.numPlayers--;
    }
    if (session.numPlayers < 0)
    {
        session.numPlayers = 0;
    }
    _deletePlayers.Clear();
    for (i = 0; i < _createPlayers.Size(); i++)
    {
        CreatePlayerInfo& info = _createPlayers[i];
        callbackCreate(info.player, info.botClient, info.name, info.mod, context);
        session.numPlayers++;
    }
    _createPlayers.Clear();
    leaveUsr();
}

void NetServer::RemovePlayers()
{
    enterUsr();
    _createPlayers.Clear();
    _deletePlayers.Clear();
    leaveUsr();
}

RString NetServer::GetStatistics(int player)
{
    enterUsr();
    RefD<NetChannel> channel;
    if (!users.get(player, channel))
    {
        leaveUsr();
        return Format("Unknown player ID = %d", player);
    }
    int latencyAve;
    unsigned latencyAct, latencyMin;
    int throughputAve;
    latencyAve = (int)(channel->getLatency(&latencyAct, &latencyMin) / 1000);
    latencyAct /= 1000;
    latencyMin /= 1000;
    EnhancedBWInfo enhanced;
    throughputAve = (int)channel->getOutputBandWidth(&enhanced);
    int nMsg, nBytes, nMsgG, nBytesG;
    channel->getOutputQueueStatistics(nMsg, nBytes, nMsgG, nBytesG);
    ChannelStatistics stat; // internal statistics of the NetChannel
    Zero(stat);
    channel->getInternalStatistics(stat);
    leaveUsr();
    char buf[256];
    bool kbps = (throughputAve < (9 << 17));
    snprintf(buf, sizeof(buf),
             "ping%4dms(%4u,%4u) BW%c%c%c%5d%cb(%4u,%4u,%4u) lost%4.1f%%%%(%3u) queue%4dB(%4d) ackWait%3u(%3.1f,%3.1f)",
             latencyAve, latencyAct, latencyMin, (char)(enhanced.growMode + 'c'), (char)(enhanced.growModePing + 'c'),
             (char)(enhanced.growModeLost + 'c'), kbps ? ((throughputAve + 64) >> 7) : ((throughputAve + 65536) >> 17),
             kbps ? 'K' : 'M', (enhanced.actBW + 64) >> 7, (enhanced.goodBW + 64) >> 7, (enhanced.sentBW + 64) >> 7,
             stat.ackTotal ? (stat.ackLost * 100.0) / stat.ackTotal : 0.0, stat.ackLost, nBytes, nBytesG,
             stat.revisitedNo, 1e-6 * stat.revisitedAveAge, 1e-6 * stat.revisitedMaxAge);
#ifdef NET_LOG_TRANSP_STAT
    unsigned64 now = getSystemTime();
    if (player != botId && (inGetConnection && forceLog || now >= nextStatLog))
    {
        forceLog = true;
        NetLog("Channel(%u): NetServer(%d)[%d] - %s", channel->getChannelId(), player, getStatisticsCount, buf);
        getStatisticsCount = 0;
        if (now >= nextStatLog)
            nextStatLog = now + STAT_LOG_INTERVAL;
    }
    else
        getStatisticsCount++;
#endif
    return RString(buf);
}

unsigned NetServer::FreeMemory()
{
    if (!NetMessagePool::pool())
    {
        return 0;
    }
    unsigned ret = NetMessagePool::pool()->freeMemory();
    SafeMemoryCleanUp();
    return ret;
}

// Namespaced wrappers over the file-static peer/pool accessors, so the split-out
// NetSessionEnum translation unit can reach them without duplicating the backing
// state (declared in NetTransportNetInternal.hpp).
namespace NetTpInternal
{
NetPeer* getClientPeer()
{
    return ::Poseidon::getClientPeer();
}
void checkPool()
{
    ::Poseidon::checkPool();
}
} // namespace NetTpInternal

// Single explicit instantiation backing the extern template declarations in
// NetTransportNetDecls.hpp (kept in one TU so the header is multi-include-safe).
namespace Foundation
{
template struct ExplicitMapTraits<unsigned64, ChannelSupport>;
template class ExplicitMap<unsigned64, ChannelSupport, true, MemAllocSafe>;
} // namespace Foundation

} // namespace Poseidon

// Transport factory functions (declared in NetTransport.hpp). Defined here, in one
// TU, so NetTransportNetDecls.hpp can be included from several translation units.
NetTranspSessionEnum* CreateNetSessionEnum(const ParamEntry& cfg)
{
    NetworkParams data;
    LoadNetworkParams(data, cfg);
    NetChannelBasic::setGlobalNetworkParams(data);
    return new NetSessionEnum;
}

NetTranspClient* CreateNetClient(const ParamEntry& cfg)
{
    NetworkParams data;
    LoadNetworkParams(data, cfg);
    NetChannelBasic::setGlobalNetworkParams(data);
    return new NetClient;
}

NetTranspServer* CreateNetServer(const ParamEntry& cfg)
{
    NetworkParams data;
    LoadNetworkParams(data, cfg);
    NetChannelBasic::setGlobalNetworkParams(data);
    return new NetServer;
}
