#include <Poseidon/Network/Legacy/netpch.hpp>
#include <Poseidon/Network/Legacy/NetPeer.hpp>
#include <Poseidon/Network/Legacy/NetChannel.hpp>
#include <Poseidon/Network/NetworkConfig.hpp>
#ifndef _WIN32
#include <arpa/inet.h>
#endif
#include <ctype.h>
#ifndef _WIN32
#include <netdb.h>
#endif
#ifndef _WIN32
#include <netinet/in.h>
#endif
#include <string.h>
#ifndef _WIN32
#include <sys/select.h>
#endif
#ifndef _WIN32
#include <sys/socket.h>
#endif
#ifndef _WIN32
#include <sys/time.h>
#endif
#ifndef _WIN32
#include <unistd.h>
#endif
#include <Poseidon/Foundation/Algorithms/Crc32.h>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/PoTime.hpp>
#include <Poseidon/Foundation/Memory/CheckMem.hpp>
#include <Poseidon/Foundation/Threads/PoCritical.hpp>
#include <Poseidon/Foundation/Types/Memtype.h>
#include <Poseidon/Foundation/Types/Pointers.hpp>

using Poseidon::Foundation::crc32;
using Poseidon::Foundation::ITERATOR_NULL;
using Poseidon::Foundation::IteratorState;
using Poseidon::Foundation::nsError;
using Poseidon::Foundation::nsOutputPending;
using Poseidon::Foundation::nsOutputSent;
using Poseidon::Foundation::nsOutputTimeout;
using Poseidon::Foundation::poSetPriority;
using Poseidon::Foundation::poThreadJoin;
using Poseidon::Foundation::safeDelete;
using Poseidon::Foundation::safeNew;

// Maximum incoming-datagram size. For static allocation only (may exceed real packets, never smaller).
const int MAX_IN_DATA = 2000;

// Listener/sender thread timeout in microseconds.
constexpr long TIMEOUT = 5000;

// udpListen() loop passes before a connectivity check (tuned to 2 seconds for channels with no traffic).
constexpr unsigned CHECK_COUNTER = 2000000 / TIMEOUT;

// udpSend() loop passes before NetChannel::tick. Not constexpr: NetChannelBasic::RUN_INTERVAL isn't.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wglobal-constructors"
const unsigned TICK_COUNTER =
    static_cast<unsigned>((4 * NetChannelBasic::RUN_INTERVAL) / TIMEOUT); // RUN_INTERVAL is unsigned64
#pragma clang diagnostic pop

// Define NET_BREAK externally to enable the break-port debug hook.

#ifdef NET_BREAK

const unsigned64 BreakCheckInterval = 2000000;

const char* BreakFileName = "break.txt";

#endif

bool getLocalAddress(struct sockaddr_in& me, unsigned16 port)
{
    me.sin_family = AF_INET;
    char meName[128];
    if (gethostname(meName, 128) == SOCKET_ERROR)
    {
#ifdef NET_LOG_GET_LOCAL_ADDRESS
#ifdef _WIN32
        NetLog("getLocalAddress: gethostname() failed with error: %d", WSAGetLastError());
#else
        NetLog("getLocalAddress: gethostname() failed!");
#endif
#endif
        return false;
    }
#ifdef NET_LOG_GET_LOCAL_ADDRESS
    NetLog("getLocalAddress: local hostname: %s", meName);
#endif
    if (isalpha(meName[0]))
    {
        struct hostent* h = gethostbyname(meName);
        if (!h)
        {
#ifdef NET_LOG_GET_LOCAL_ADDRESS
#ifdef _WIN32
            NetLog("getLocalAddress: gethostbyname(%s) failed with error: %d", meName, WSAGetLastError());
#else
            NetLog("getLocalAddress: gethostbyname(%s) failed!", meName);
#endif
#endif
            return false;
        }
        if (h->h_length < 4)
        {
#ifdef NET_LOG_GET_LOCAL_ADDRESS
            NetLog("getLocalAddress: invalid network address type!");
#endif
            return false;
        }
        memcpy(&(me.sin_addr.s_addr), h->h_addr, 4);
#ifdef NET_LOG_GET_LOCAL_ADDRESS
        NetLog("getLocalAddress: resolved local hostname: %s (%s)", h->h_name, inet_ntoa(me.sin_addr));
#endif
    }
    else
    {
        me.sin_addr.s_addr = inet_addr(meName);
    }
    me.sin_port = htons(port);
#ifdef NET_LOG_GET_LOCAL_ADDRESS
    NetLog("getLocalAddress: local port: %u", (unsigned)port);
#endif
    return true;
}

bool getLocalName(char* name, unsigned len)
{
    if (!name)
    {
        return false;
    }
    if (gethostname(name, len) == SOCKET_ERROR)
    {
#ifdef NET_LOG_GET_LOCAL_NAME
#ifdef _WIN32
        NetLog("getLocalName: gethostname() failed with error: %d", WSAGetLastError());
#else
        NetLog("getLocalName: gethostname() failed!");
#endif
#endif
        return false;
    }
#ifdef NET_LOG_GET_LOCAL_NAME
    NetLog("getLocalName: local hostname: %s", name);
#endif
    if (isalpha(name[0]))
    {
        struct hostent* h = gethostbyname(name);
        if (!h)
        {
#ifdef NET_LOG_GET_LOCAL_NAME
#ifdef _WIN32
            NetLog("getLocalName: gethostbyname(%s) failed with error: %d", name, WSAGetLastError());
#else
            NetLog("getLocalName: gethostbyname(%s) failed!", name);
#endif
#endif
            return false;
        }
        strncpy(name, h->h_name, len);
        name[len - 1] = (char)0;
#ifdef NET_LOG_GET_LOCAL_NAME
        NetLog("getLocalName: resolved local hostname: %s", name);
#endif
    }
    return true;
}

bool getHostAddress(struct sockaddr_in& host, const char* ip, unsigned16 port)
{
    host.sin_family = AF_INET;
    host.sin_port = htons(port);
    if (!ip || !ip[0])
    {
        host.sin_addr.s_addr = INADDR_BROADCAST;
    }
    else
    {
        host.sin_addr.s_addr = inet_addr(ip);
        if (host.sin_addr.s_addr == INADDR_NONE)
        {
            struct hostent* h = gethostbyname(ip);
            if (!h)
            {
                return false;
            }
            host.sin_addr.s_addr = *(unsigned32*)h->h_addr_list[0];
        }
    }
    return true;
}

THREAD_PROC_RETURN THREAD_PROC_MODE udpListen(void* param)
{
    union
    {
        MsgHeader header;
        char data[MAX_IN_DATA]; // fixed buffer to receive message data
    };
    NetPeerUDP* peer = (NetPeerUDP*)param;
    NET_ERROR(peer);
    fd_set set;                             // list of receiving sockets (we're using only the 1st item)
    struct timeval timeout = {0L, TIMEOUT}; // select() timeout (5ms)
    struct sockaddr_in from;                // IP address the message came from
    socklen_t fromLen;
#ifdef NET_LOG_UDP_LISTEN
#ifdef NET_LOG_BRIEF
    NetLog("Pe(%u):list(%u)", peer->getPeerId(), peer->getLocalPort());
#else
    NetLog("Peer(%u)::udpListen: start listening at local port %u", peer->getPeerId(), peer->getLocalPort());
#endif
#endif
    peer->checkCounter = CHECK_COUNTER;
#ifdef NET_LOG_UDP_RECEIVE
    unsigned waitTime = 0;
#endif
#ifdef NET_BREAK
    unsigned64 lastBreakCheck = Poseidon::Foundation::getSystemTime();
    unsigned breakPort = 0; // 0 .. none, 1 .. all, >1 .. specific port
    FILE* breakFile;
#endif

    while (peer->listen)
    { // check it at least each 50ms (according to "TIMEOUT")

#ifdef NET_BREAK
        unsigned64 now = Poseidon::Foundation::getSystemTime();
        if (now > lastBreakCheck + BreakCheckInterval)
        {
            bool wasBreak = (breakPort != 0);
            breakFile = fopen(BreakFileName, "rt");
            breakPort = 0;
            if (breakFile)
            {
                fscanf(breakFile, "%u", &breakPort);
                fclose(breakFile);
            }
#ifdef NET_LOG
            if (wasBreak != (breakPort != 0))
                NetLog("Peer(%u)::udpListen: set breakPort = %u", peer->getPeerId(), breakPort);
#endif
            lastBreakCheck = now;
        }
#endif
        FD_ZERO(&set);
        FD_SET(peer->sock, &set);
#ifndef _WIN32
        timeout.tv_sec = 0;
        timeout.tv_usec = TIMEOUT;
#endif
        int error = select(FD_SETSIZE, &set, nullptr, nullptr, &timeout);

        if (error == 1)
        { // data are ready
            fromLen = sizeof(from);
            error = recvfrom(peer->sock, data, MAX_IN_DATA - 1, 0, (struct sockaddr*)&from, &fromLen);
            if (error != SOCKET_ERROR && error == (int)header.length && error >= MSG_HEADER_LEN && error <= MAX_IN_DATA)
            {
#ifdef NET_BREAK
                if (breakPort == 1 || // drop all packets..
                    breakPort == ntohs(from.sin_port))
                    goto nothing;
#endif
                // CRC check:
                unsigned32 crc = header.crc;
#ifdef NET_STRESS
                if (stressRnd.uniformNumber() >= NET_STRESS_DROP)
                {
#endif
                    header.crc = 0;
#ifdef NET_LOG_UDP_RECEIVE
#ifdef MSG_ID
                    NetLog("Peer(%u)::udpListen: received message "
                           "(from=%u.%u.%u.%u:%u,len=%3d,serial=%4u,flags=%04x,ID=%x), wait=%u ms",
                           peer->getPeerId(), (unsigned)IP4(from), (unsigned)IP3(from), (unsigned)IP2(from),
                           (unsigned)IP1(from), (unsigned)PORT(from), (int)header.length - MSG_HEADER_LEN,
                           (unsigned)header.serial, (unsigned)header.flags, header.id, waitTime);
#else
                    NetLog("Peer(%u)::udpListen: received message (from=%u.%u.%u.%u:%u,len=%3d,serial=%4u,flags=%04x), "
                           "wait=%u ms",
                           peer->getPeerId(), (unsigned)IP4(from), (unsigned)IP3(from), (unsigned)IP2(from),
                           (unsigned)IP1(from), (unsigned)PORT(from), (int)header.length - MSG_HEADER_LEN,
                           (unsigned)header.serial, (unsigned)header.flags, waitTime);
#endif
#endif
#ifdef NET_STRESS
                }
#ifdef NET_LOG_UDP_STRESS
                else
                    NetLog("Peer(%u)::udpListen with NET_STRESS: dropped message "
                           "(from=%u.%u.%u.%u:%u,len=%3d,serial=%4u,flags=%04x)",
                           peer->getPeerId(), (unsigned)IP4(from), (unsigned)IP3(from), (unsigned)IP2(from),
                           (unsigned)IP1(from), (unsigned)PORT(from), (int)header.length - MSG_HEADER_LEN,
                           (unsigned)header.serial, (unsigned)header.flags);
#endif
#endif
                if (crc32(0, (const unsigned8*)&header, header.length) == crc)
                {
                    header.crc = crc;
                    peer->enter();
                    peer->processData(&header, from); // only NetPeer-related processing (statistics)!
                    RefD<NetChannel> ch;
                    peer->chMap.get(sockaddrKey(from), ch);
                    if ((header.flags & MSG_TO_BCAST_FLAG) || !ch)
                    {
                        ch = peer->getBroadcastChannel();
                    }
                    peer->leave();
                    if (ch)
                    {
                        ch->processData(&header, from); // main data-processing routine
                    }
                }
#if defined(NET_LOG_UDP_RECEIVE) && !defined(NET_STRESS)
                else
                    NetLog("Peer(%u)::udpListen: received message has bad CRC!", peer->getPeerId());
#endif
            }
            else
            {
#ifdef _WIN32
                int werror = WSAGetLastError();
#endif
                error = 0;
                socklen_t errLen = sizeof(error);
                getsockopt(peer->sock, SOL_SOCKET, SO_ERROR, (char*)&error, &errLen);
#ifdef _WIN32
                WSASetLastError(0); // to be sure!
#endif
#ifdef NET_LOG_UDP_LISTEN
#ifdef _WIN32
#ifdef NET_LOG_BRIEF
                NetLog("Pe(%u):err-l(%x,%d,%d)", peer->getPeerId(), (unsigned)peer->sock, error, werror);
#else
                NetLog("Peer(%u)::udpListen: error reading data (socket=%x, error=%d, %d)", peer->getPeerId(),
                       (unsigned)peer->sock, error, werror);
#endif
#else
#ifdef NET_LOG_BRIEF
                NetLog("Pe(%u):err-l(%x,%d)", peer->getPeerId(), (unsigned)peer->sock, error);
#else
                NetLog("Peer(%u)::udpListen: error reading data (socket=%x, error=%d)", peer->getPeerId(),
                       (unsigned)peer->sock, error);
#endif
#endif
#endif
#ifdef _WIN32
                if (werror == WSAECONNRESET)
                {
                    peer->reconnect();
#ifdef NET_LOG_UDP_LISTEN
#ifdef NET_LOG_BRIEF
                    NetLog("Pe(%u):rec(%x)", peer->getPeerId(), (unsigned)peer->sock);
#else
                    NetLog("Peer(%u)::udpListen: reconnect() after WSAECONNRESET (socket=%x)", peer->getPeerId(),
                           (unsigned)peer->sock);
#endif
#endif
                }
#endif
            }
#ifdef NET_LOG_UDP_RECEIVE
            waitTime = 0;
#endif
        } // data are ready
#ifdef NET_LOG_UDP_RECEIVE
        else // nothing was received
            waitTime += TIMEOUT / 1000;
#endif
#ifdef NET_BREAK
    nothing:
#endif
        if (peer->checkCounter-- == 0)
        { // connectivity checks on all channels:
            IteratorState it;
            unsigned64 now = Poseidon::Foundation::getSystemTime();
            peer->enter();
            RefD<NetChannel> channel;
            if (peer->chMap.getFirst(it, channel))
            {
                do
                { // check one channel
                    channel->checkConnectivity(now);
                } while (peer->chMap.getNext(it, channel));
            }
            peer->leave();
            peer->checkCounter = CHECK_COUNTER;
        }

    } // while ( peer->listen )

#ifdef NET_LOG_UDP_LISTEN
#ifdef NET_LOG_BRIEF
    NetLog("Pe(%u):stopl(%u)", peer->getPeerId(), (unsigned)peer->getLocalPort());
#else
    NetLog("Peer(%u)::udpListen: stop listening at local port %u", peer->getPeerId(), (unsigned)peer->getLocalPort());
#endif
#endif
    return (THREAD_PROC_RETURN)0;
}

THREAD_PROC_RETURN THREAD_PROC_MODE udpSend(void* param)
{
    NetPeerUDP* peer = (NetPeerUDP*)param;
    NET_ERROR(peer);
    IteratorState origin = ITERATOR_NULL; // origin for cyclic pass through the channels
    RefD<NetChannel> channel;             // channel sending the actual message
#ifdef NET_LOG_UDP_SEND
#ifdef NET_LOG_BRIEF
    NetLog("Pe(%u):send(%u)", peer->getPeerId(), (unsigned)peer->getLocalPort());
#else
    NetLog("Peer(%u)::udpSend: start sending through local port %u", peer->getPeerId(), (unsigned)peer->getLocalPort());
#endif
#endif
    unsigned waitTime = 1;               // 0 inside the "packet-bunch"
    unsigned64 bunchStart;               // "packet-bunch" start time
    unsigned tickCounter = TICK_COUNTER; // periodic NetChannel::tick revocation

    IteratorState it;                 // general-purpose channel-iterator
    DispatcherStatus* data = nullptr; // struct for dispatcher-state collecting

    while (peer->sending)
    { // check this after every send operation

        peer->enter();

        IteratorState robin{}; // general purpose iterator

        if (!data)
        { // 1st-time run => allocate the collection-struct
            data = (DispatcherStatus*)safeNew(peer->initDispatcherStatus(nullptr));
        }

        if (data)
        {
            peer->initDispatcherStatus(data);
            // collection-struct is initialized => do the collection job:
            if (peer->chMap.getFirst(it, channel))
            {
                do
                {
                    channel->nextDispatcherStatus(data);
                } while (peer->chMap.getNext(it, channel));
            }
            channel = peer->getBroadcastChannel();
            if (channel)
            {
                channel->nextDispatcherStatus(data);
            }
        }

        channel = peer->getBroadcastChannel(); // control channel has the highest priority..
        if (!channel || !channel->getPreparedMessage(data))
        {
            robin = origin; // round-robin strategy
            if (peer->chMap.getFirstCyclic(robin, origin, channel))
            {
                // go through all channels:
                while (!channel->getPreparedMessage(data) && peer->chMap.getNextCyclic(robin, origin, channel))
                {
                    ;
                }
            }
        }

        unsigned64 now;

        if (!channel || !channel->prepared)
        { // Note: better message-ready signaling
            peer->leave();
            waitTime += TIMEOUT / 1000;
            SLEEP_MS(TIMEOUT / 1000);
        }

        else
        { // the message (channel->prepared) is ready to send

            origin = robin; // start here next time
            struct sockaddr_in dist;
            if (channel->isControl())
            { // control channel => get distant address from the message
                channel->prepared->getDistant(dist);
            }
            else
            { // common channel => use distant address associated with the channel
                channel->getDistantAddress(dist);
            }

#ifdef NET_LOG_UDP_SENDING
            NetLog("Peer(%u)::udpSend: sending message (to=%u.%u.%u.%u:%u,len=%3d,serial=%4u,flags=%04x,ID=%x), "
                   "wait=%u ms",
                   peer->getPeerId(), (unsigned)IP4(dist), (unsigned)IP3(dist), (unsigned)IP2(dist),
                   (unsigned)IP1(dist), (unsigned)PORT(dist), (int)channel->prepared->getLength(),
                   channel->prepared->getSerial(), (unsigned)channel->prepared->getFlags(), channel->prepared->id,
                   waitTime);
#endif
            if (waitTime)
            { // a new bunch is starting
#ifdef NET_STRESS
                SLEEP_MS(NET_STRESS_LATENCY);
#endif
                now = bunchStart = channel->preSend(0);
            }
            else
            {
                now = channel->preSend(bunchStart);
            }

            waitTime = 0;
            if (peer->sendData(channel->prepared->header, dist) != nsError)
            {
                channel->prepared->status = // sent OK
                    (channel->prepared->status == nsOutputPending) ? nsOutputSent : nsOutputTimeout;
            }
            else
            { // send error
                channel->prepared->status = nsError;
            }

            channel->postSend(); // remember the message for some time..

            // pass over the message (call-back, acknowledgements, resent etc.)
            if (!channel->isControl())
            { // common channel => check its connectivity
                channel->checkConnectivity(now);
            }

            peer->leave();
        }

        if (tickCounter-- == 0)
        { // tick() call on all channels:
            peer->enter();
            if (peer->chMap.getFirst(it, channel))
            {
                do
                { // check one channel
                    channel->tick();
                } while (peer->chMap.getNext(it, channel));
            }
            channel = peer->getBroadcastChannel();
            if (channel)
            {
                channel->tick();
            }
            peer->leave();
            tickCounter = TICK_COUNTER;
        }
    }

    if (data)
    {
        safeDelete(data);
    }

#ifdef NET_LOG_UDP_SEND
#ifdef NET_LOG_BRIEF
    NetLog("Pe(%u):stops(%u)", peer->getPeerId(), (unsigned)peer->getLocalPort());
#else
    NetLog("Peer(%u)::udpSend: stop sending through local port %u", peer->getPeerId(), (unsigned)peer->getLocalPort());
#endif
#endif
    return (THREAD_PROC_RETURN)0;
}

NetPeerUDP::NetPeerUDP(NetPool* _pool) : NetPeer(_pool), chMap(1)
{
    LockRegister(lock, "NetPeerUDP");
    sock = INVALID_SOCKET;
    port = 0;
    listen = sending = reconnecting = false;
}

NetPeerUDP::NetPeerUDP(Poseidon::Foundation::SOCKET _sock, unsigned16 _port, NetPool* _pool)
    : NetPeer(_pool), chMap(2) // space for at least two channels..
{
    LockRegister(lock, "NetPeerUDP");
    enter();
    sock = _sock;
    port = _port;
    listen = sending = true;
    broadcastCh = nullptr;
    if (pool && pool->getFactory())
    { // create a broadcast channel
        broadcastCh = pool->getFactory()->createChannel(pool, true);
        if (broadcastCh)
        {
            struct sockaddr_in distant;
            Zero(distant);
            distant.sin_addr.s_addr = INADDR_BROADCAST;
            broadcastCh->open(this, distant);
        }
    }
    if (sock != INVALID_SOCKET)
    { // prepare asynchronous listener/send threads
#ifdef NET_LOG_PEER
        char buf[256];
#ifdef NET_LOG_BRIEF
        NetLog("Pe(%u):succ(%s)", getPeerId(), getPeerInfo(buf));
#else
        NetLog("Peer(%u)::NetPeerUDP succeeded: %s", getPeerId(), getPeerInfo(buf));
#endif
#endif
        if (poThreadCreate(&listener, 0, &udpListen, this))
        {
            Verify(poSetPriority(listener, 2)); // the highest priority
        }
        else
        {
            listen = false;
        }
        if (poThreadCreate(&sender, 0, &udpSend, this))
        {
            Verify(poSetPriority(sender, 2)); // the highest priority
        }
        else
        {
            listen = false;
        }
    }
    else
    {
        listen = sending = false;
    }
    leave();
}

void NetPeerUDP::getLocalAddress(struct sockaddr_in& local) const
{
    ::getLocalAddress(local, port);
}

unsigned NetPeerUDP::maxMessageData() const
{
    return (NetChannelBasic::par.maxPacketSize - IP_UDP_HEADER - MSG_HEADER_LEN);
}

bool NetPeerUDP::registerChannel(struct sockaddr_in& distant, NetChannel* ch)
{
    if (!ch)
    {
        return false;
    }
    enter();
    bool result = !chMap.present(sockaddrKey(distant));
    if (result)
    {
        chMap.put(ch);
    }
    leave();
    // the new net-channel will receive incoming data automatically!
    return result;
}

void NetPeerUDP::unregisterChannel(NetChannel* ch)
{
    if (!ch)
    {
        return;
    }
    chMap.remove(ch);
}

NetChannel* NetPeerUDP::findChannel(const struct sockaddr_in& distant)
{
    RefD<NetChannel> result;
    chMap.get(sockaddrKey(distant), result);
    return result;
}

void NetPeerUDP::close()
{
    enter();
    // stop sender & listener threads as early as possible:
    bool wasSending = sending;
    sending = false;
    bool wasListen = listen;
    listen = false;
    // close all associated (point-to-point) channels:
    IteratorState iter;
    RefD<NetChannel> ch;
    if (chMap.getFirst(iter, ch))
    {
        do
        {
            ch->close();
        } while (chMap.getNext(iter, ch));
    }
    chMap.reset();
    // close the broadcast channel:
    if (broadcastCh)
    {
        broadcastCh->close();
        broadcastCh = nullptr;
    }
    leave();
    // destroy the sender thread:
    if (wasSending)
        Verify(poThreadJoin(sender, nullptr));
    // destroy the listener thread:
    if (wasListen)
        Verify(poThreadJoin(listener, nullptr));
    enter();
    // close the socket:
    if (sock != INVALID_SOCKET)
    {
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
    leave();
}

void NetPeerUDP::processData(MsgHeader* hdr, const struct sockaddr_in& distant)
{
    // Note: peer statistics
}

void NetPeerUDP::reconnect()
{
    enter();
    if (sock == INVALID_SOCKET)
    {
        leave();
        return;
    }
    reconnecting = true;
    closesocket(sock);
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET)
    {
#ifdef NET_LOG_PEER
#ifdef _WIN32
#ifdef NET_LOG_BRIEF
        NetLog("Pe(%u):rec: socket() failed with error %d", getPeerId(), WSAGetLastError());
#else
        NetLog("Peer(%u)::reconnect: socket() failed with error %d", getPeerId(), WSAGetLastError());
#endif
#else
#ifdef NET_LOG_BRIEF
        NetLog("Pe(%u):rec: socket() failed", getPeerId());
#else
        NetLog("Peer(%u)::reconnect: socket() failed!", getPeerId());
#endif
#endif
#endif
        reconnecting = false;
        leave();
        return;
    }
    int tmp = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char*)&tmp, sizeof(tmp)) == SOCKET_ERROR)
    {
#ifdef NET_LOG_PEER
#ifdef _WIN32
#ifdef NET_LOG_BRIEF
        NetLog("Pe(%u):rec: setsockopt(SO_BROADCAST) failed with error %d", getPeerId(), WSAGetLastError());
#else
        NetLog("Peer(%u)::reconnect: setsockopt(SO_BROADCAST) failed with error %d", getPeerId(), WSAGetLastError());
#endif
#else
#ifdef NET_LOG_BRIEF
        NetLog("Pe(%u):rec: setsockopt(SO_BROADCAST) failed!", getPeerId());
#else
        NetLog("Peer(%u)::reconnect: setsockopt(SO_BROADCAST) failed!", getPeerId());
#endif
#endif
#endif
        closesocket(sock);
        sock = INVALID_SOCKET;
        reconnecting = false;
        leave();
        return;
    }
    tmp = NetChannelBasic::par.rcvBufSize;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&tmp, sizeof(tmp));
    BOOL share = TRUE;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&share, sizeof(share));
    struct sockaddr_in local;
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    RString bindAddress = GetNetworkBindAddress();
    if (bindAddress.GetLength() > 0 && strcmp((const char*)bindAddress, "0.0.0.0") != 0)
    {
        local.sin_addr.s_addr = inet_addr(bindAddress);
    }
    local.sin_port = htons(port);
    if (bind(sock, (struct sockaddr*)&local, sizeof(local)) == SOCKET_ERROR)
    {
#ifdef NET_LOG_PEER
#ifdef _WIN32
#ifdef NET_LOG_BRIEF
        NetLog("Pe(%u):rec: bind() failed with error %d", getPeerId(), WSAGetLastError());
#else
        NetLog("Peer(%u)::reconnect: bind() failed with error %d", getPeerId(), WSAGetLastError());
#endif
#else
#ifdef NET_LOG_BRIEF
        NetLog("Pe(%u):rec: bind() failed!", getPeerId());
#else
        NetLog("Peer(%u)::reconnect: bind() failed!", getPeerId());
#endif
#endif
#endif
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
    reconnecting = false;
    leave();
}

NetStatus NetPeerUDP::sendData(MsgHeader* hdr, struct sockaddr_in distant)
{
    enter();
    if (sock == INVALID_SOCKET)
    {
        leave();
        return nsError;
    }
    NET_ERROR(hdr);
    NET_ERROR(hdr->length);
    hdr->crc = 0;
    hdr->crc = crc32(0, (const unsigned8*)hdr, hdr->length);
#ifdef _WIN32
    int retryCounter = 12;
retry:
#endif
    int result = sendto(sock, (const char*)hdr, hdr->length, 0, (const sockaddr*)&distant, sizeof(distant));
    if (result == SOCKET_ERROR)
    {
#ifdef _WIN32
        int werror = WSAGetLastError();
#endif
        int error = 0;
        socklen_t errLen = sizeof(error);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&error, &errLen);
#ifdef _WIN32
        WSASetLastError(0); // to be sure!
#ifdef NET_LOG_UDP_SEND
#ifdef NET_LOG_BRIEF
        NetLog("Pe(%u):err-s(%d,%d)", getPeerId(), error, werror);
#else
        NetLog("Peer(%u)::sendData: sendto() failed with error: %d, %d", getPeerId(), error, werror);
#endif
#endif
        if (werror == WSAECONNRESET)
        { // connection reset by peer => reconnect it!
            reconnect();
            if (sock == INVALID_SOCKET)
            {
#ifdef NET_LOG_UDP_SEND
#ifdef NET_LOG_BRIEF
                NetLog("Pe(%u):rec-giveup", getPeerId());
#else
                NetLog("Peer(%u)::sendData: reconnect() after WSAECONNRESET failed .. giving up", getPeerId());
#endif
#endif
                leave();
                return nsError;
            }
            else
            {
#ifdef NET_LOG_UDP_SEND
#ifdef NET_LOG_BRIEF
                NetLog("Pe(%u):rec-retry", getPeerId());
#else
                NetLog("Peer(%u)::sendData: reconnect() after WSAECONNRESET succeeded .. retrying", getPeerId());
#endif
#endif
                if (retryCounter--)
                {
                    goto retry;
                }
                else
                {
                    leave();
                    return nsError;
                }
            }
        }
#else
#ifdef NET_LOG_UDP_SEND
#ifdef NET_LOG_BRIEF
        NetLog("Pe(%u):err-s(%d)", getPeerId(), error);
#else
        NetLog("Peer(%u)::sendData: sendto() failed with error: %d", getPeerId(), error);
#endif
#endif
#endif
        leave();
        return nsError;
    }
#ifdef NET_LOG_SEND_DATA
    else
        NetLog("Peer(%u)::sendData: OK sending data (socket=%x, result=%d)", getPeerId(), (unsigned)sock, result);
#endif
    leave();
    return nsOutputSent;
}

void NetPeerUDP::cancelAllMessages()
{
    IteratorState iter;
    enter();
    RefD<NetChannel> ch;
    if (chMap.getFirst(iter, ch))
    {
        do
        {
            ch->cancelAllMessages();
        } while (chMap.getNext(iter, ch));
    }
    leave();
}

unsigned NetPeerUDP::initDispatcherStatus(DispatcherStatus* data)
{
    if (data)
    {
        DispatcherStatusBasic* ds = (DispatcherStatusBasic*)data;
        memset(ds, 0, sizeof(*ds)); // fast solution
        ds->structLen = sizeof(DispatcherStatusBasic);
    }
    return sizeof(DispatcherStatusBasic);
}

NetPeerUDP::~NetPeerUDP()
{
#if defined(NET_LOG_DESTRUCTOR) || defined(NET_LOG_PEER)
#ifdef NET_LOG_BRIEF
    NetLog("Pe(%u):~(%d,%d,%x,%u,%u)", getPeerId(), (int)listen, (int)sending, (unsigned)sock, (unsigned)port,
           chMap.card());
#else
    NetLog("Peer(%u)::~NetPeerUDP: listening=%d, sending=%d, socket=%x, port=%u, |chMap|=%u", getPeerId(), (int)listen,
           (int)sending, (unsigned)sock, (unsigned)port, chMap.card());
#endif
#endif
    close();
}
