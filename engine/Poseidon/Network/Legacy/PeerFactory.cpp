#include <Poseidon/Network/Legacy/netpch.hpp>
#include <Poseidon/Network/Legacy/NetPeer.hpp>
#include <Poseidon/Network/Legacy/NetChannel.hpp>
#include <Poseidon/Network/Legacy/PeerFactory.hpp>
#ifndef _WIN32
#include <arpa/inet.h>
#endif
#ifndef _WIN32
#include <netinet/in.h>
#endif
#ifndef _WIN32
#include <sys/socket.h>
#endif
#include <Poseidon/Foundation/Common/NetGlobal.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Threads/PoCritical.hpp>
#include <Poseidon/Network/NetworkConfig.hpp>
#include <string.h>

int PeerChannelFactoryUDP::instances = 0;

PeerChannelFactoryUDP::PeerChannelFactoryUDP()
{
    LockRegister(lock, "PeerChannelFactoryUDP");
    if (!instances++)
    {
#ifdef _WIN32
        WSADATA wsaData;
        BYTE major = (BYTE)floor(NetChannelBasic::par.winsockVersion);
        BYTE minor = (BYTE)floor(
            10.0 * (NetChannelBasic::par.winsockVersion - floor(NetChannelBasic::par.winsockVersion)) + 0.5);
        if (WSAStartup(MAKEWORD(major, minor), &wsaData) == SOCKET_ERROR)
        {
#ifdef NET_LOG_PEER_CHANNEL_FACTORY
#ifdef NET_LOG_BRIEF
            NetLog("Fac:start failed with error %d", WSAGetLastError());
#else
            NetLog("PeerChannelFactoryUDP: WSAStartup failed with error %d", WSAGetLastError());
#endif
#endif
            WSACleanup();
        }
#ifdef NET_LOG_CREATE_PEER
#ifdef NET_LOG_BRIEF
        NetLog("Fac:start(%u.%u,%u.%u,'%s','%s',%u,%u)", (unsigned)LOBYTE(wsaData.wVersion),
               (unsigned)HIBYTE(wsaData.wVersion), (unsigned)LOBYTE(wsaData.wHighVersion),
               (unsigned)HIBYTE(wsaData.wHighVersion), wsaData.szDescription, wsaData.szSystemStatus,
               (unsigned)wsaData.iMaxSockets, (unsigned)wsaData.iMaxUdpDg);
#else
        NetLog("PeerChannelFactoryUDP: WSAStartup succeeded - version=%u.%u, highest=%u.%u, descr='%s'",
               (unsigned)LOBYTE(wsaData.wVersion), (unsigned)HIBYTE(wsaData.wVersion),
               (unsigned)LOBYTE(wsaData.wHighVersion), (unsigned)HIBYTE(wsaData.wHighVersion), wsaData.szDescription);
        NetLog("PeerChannelFactoryUDP: WSAStartup - status='%s', maxSockets=%u, maxUdpDg=%u", wsaData.szSystemStatus,
               (unsigned)wsaData.iMaxSockets, (unsigned)wsaData.iMaxUdpDg);
#endif
#endif
#endif
    }
}

NetPeer* PeerChannelFactoryUDP::createPeer(NetPool* pool, BitMask* tryPorts)
{
    NET_ERROR(pool);
    if (!tryPorts)
    {
        tryPorts = pool->getLocalPorts();
    }
    if (!tryPorts)
    {
        return nullptr;
    }
    int port = tryPorts->getFirst();
    if (port == BitMask::END)
    {
        return nullptr;
    }
    Poseidon::Foundation::SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
    {
#ifdef NET_LOG_CREATE_PEER
#ifdef _WIN32
#ifdef NET_LOG_BRIEF
        NetLog("Fac:cp: socket() failed with error %d", WSAGetLastError());
#else
        NetLog("PeerChannelFactoryUDP::createPeer: socket() failed with error %d", WSAGetLastError());
#endif
#else
#ifdef NET_LOG_BRIEF
        NetLog("Fac:cp: socket() failed!");
#else
        NetLog("PeerChannelFactoryUDP::createPeer: socket() failed!");
#endif
#endif
#endif
        return nullptr;
    }
    int tmp = 1;
    if (setsockopt(s, SOL_SOCKET, SO_BROADCAST, (char*)&tmp, sizeof(tmp)) == SOCKET_ERROR)
    {
#ifdef NET_LOG_CREATE_PEER
#ifdef _WIN32
#ifdef NET_LOG_BRIEF
        NetLog("Fac:cp: setsockopt(SO_BROADCAST) failed with error %d", WSAGetLastError());
#else
        NetLog("PeerChannelFactoryUDP::createPeer: setsockopt(SO_BROADCAST) failed with error %d", WSAGetLastError());
#endif
#else
#ifdef NET_LOG_BRIEF
        NetLog("Fac:cp: setsockopt(SO_BROADCAST) failed!");
#else
        NetLog("PeerChannelFactoryUDP::createPeer: setsockopt(SO_BROADCAST) failed!");
#endif
#endif
#endif
        closesocket(s);
        return nullptr;
    }
    tmp = NetChannelBasic::par.rcvBufSize;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, (char*)&tmp, sizeof(tmp));
#ifdef NET_LOG_CREATE_PEER
    unsigned maxMsgSize = 0;
    socklen_t dummy = 4;
#ifdef SO_MAX_MSG_SIZE
    if (getsockopt(s, SOL_SOCKET, SO_MAX_MSG_SIZE, (char*)&maxMsgSize, &dummy))
    {
#ifdef _WIN32
#ifdef NET_LOG_BRIEF
        NetLog("Fac:cp: getsockopt(SO_MAX_MSG_SIZE) failed with error %d", WSAGetLastError());
#else
        NetLog("PeerChannelFactoryUDP::createPeer: getsockopt(SO_MAX_MSG_SIZE) failed with error %d",
               WSAGetLastError());
#endif
#else
#ifdef NET_LOG_BRIEF
        NetLog("Fac:cp: getsockopt(SO_MAX_MSG_SIZE) failed!");
#else
        NetLog("PeerChannelFactoryUDP::createPeer: getsockopt(SO_MAX_MSG_SIZE) failed!");
#endif
#endif
    }
#endif
    int rcvBuf = 0;
    int sndBuf = 0;
    if (getsockopt(s, SOL_SOCKET, SO_RCVBUF, (char*)&rcvBuf, &dummy))
    {
#ifdef _WIN32
#ifdef NET_LOG_BRIEF
        NetLog("Fac:cp: getsockopt(SO_RCVBUF) failed with error %d", WSAGetLastError());
#else
        NetLog("PeerChannelFactoryUDP::createPeer: getsockopt(SO_RCVBUF) failed with error %d", WSAGetLastError());
#endif
#else
#ifdef NET_LOG_BRIEF
        NetLog("Fac:cp: getsockopt(SO_RCVBUF) failed!");
#else
        NetLog("PeerChannelFactoryUDP::createPeer: getsockopt(SO_RCVBUF) failed!");
#endif
#endif
    }
    if (getsockopt(s, SOL_SOCKET, SO_SNDBUF, (char*)&sndBuf, &dummy))
    {
#ifdef _WIN32
#ifdef NET_LOG_BRIEF
        NetLog("Fac:cp: getsockopt(SO_SNDBUF) failed with error %d", WSAGetLastError());
#else
        NetLog("PeerChannelFactoryUDP::createPeer: getsockopt(SO_SNDBUF) failed with error %d", WSAGetLastError());
#endif
#else
#ifdef NET_LOG_BRIEF
        NetLog("Fac:cp: getsockopt(SO_SNDBUF) failed!");
#else
        NetLog("PeerChannelFactoryUDP::createPeer: getsockopt(SO_SNDBUF) failed!");
#endif
#endif
    }
#endif
#if defined(SO_PROTOCOL_INFO) && (defined(NET_LOG_CREATE_PEER) || defined(NET_LOG_PEER_PARAMS))
    WSAPROTOCOL_INFO info;
#endif
#if defined(NET_LOG_PEER_PARAMS) && defined(SO_PROTOCOL_INFO)
    dummy = sizeof(info);
    if (getsockopt(s, SOL_SOCKET, SO_PROTOCOL_INFO, (char*)&info, &dummy))
    {
#ifdef _WIN32
#ifdef NET_LOG_BRIEF
        NetLog("Fac:cp: getsockopt(SO_PROTOCOL_INFO) failed with error %d", WSAGetLastError());
#else
        NetLog("PeerChannelFactoryUDP::createPeer: getsockopt(SO_PROTOCOL_INFO) failed with error %d",
               WSAGetLastError());
#endif
#else
#ifdef NET_LOG_BRIEF
        NetLog("Fac:cp: getsockopt(SO_PROTOCOL_INFO) failed!");
#else
        NetLog("PeerChannelFactoryUDP::createPeer: getsockopt(SO_PROTOCOL_INFO) failed!");
#endif
#endif
    }
#endif
    struct sockaddr_in local;
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    RString bindAddress = GetNetworkBindAddress();
    if (bindAddress.GetLength() > 0 && strcmp((const char*)bindAddress, "0.0.0.0") != 0)
    {
        local.sin_addr.s_addr = inet_addr(bindAddress);
        if (local.sin_addr.s_addr == INADDR_NONE)
        {
            closesocket(s);
#ifdef NET_LOG_CREATE_PEER
            NetLog("PeerChannelFactoryUDP::createPeer: invalid bind address '%s'", (const char*)bindAddress);
#endif
            return nullptr;
        }
    }
    do
    { // try one port number
        local.sin_port = htons(port);
        if (bind(s, (struct sockaddr*)&local, sizeof(local)) != SOCKET_ERROR)
        {
            break;
        }
    } while ((port = tryPorts->getNext(port)) != BitMask::END);
    if (port == BitMask::END)
    {
        closesocket(s);
#ifdef NET_LOG_CREATE_PEER
#ifdef NET_LOG_BRIEF
        NetLog("Fac:cp: no free ports are available!");
#else
        NetLog("PeerChannelFactoryUDP::createPeer: no free ports are available!");
#endif
#endif
        return nullptr;
    }
    // Resolve OS-assigned port when binding to port 0 (auto-assign)
    if (port == 0)
    {
        sockaddr_in bound{};
#ifdef _WIN32
        int addrLen = sizeof(bound);
#else
        socklen_t addrLen = sizeof(bound);
#endif
        if (getsockname(s, reinterpret_cast<struct sockaddr*>(&bound), &addrLen) == 0)
            port = ntohs(bound.sin_port);
    }
#if defined(NET_LOG_PEER_PARAMS) && defined(SO_MAX_MSG_SIZE)
    if (info.dwMessageSize == 1)
    {
        dummy = 4;
        if (getsockopt(s, SOL_SOCKET, SO_MAX_MSG_SIZE, (char*)&maxMsgSize, &dummy))
        {
#ifdef _WIN32
#ifdef NET_LOG_BRIEF
            NetLog("Fac:cp: getsockopt(SO_MAX_MSG_SIZE) failed with error %d", WSAGetLastError());
#else
            NetLog("PeerChannelFactoryUDP::createPeer: getsockopt(SO_MAX_MSG_SIZE) failed with error %d",
                   WSAGetLastError());
#endif
#else
#ifdef NET_LOG_BRIEF
            NetLog("Fac:cp: getsockopt(SO_MAX_MSG_SIZE) failed!");
#else
            NetLog("PeerChannelFactoryUDP::createPeer: getsockopt(SO_MAX_MSG_SIZE) failed!");
#endif
#endif
        }
    }
#endif
#ifdef NET_LOG_CREATE_PEER
#ifdef NET_LOG_BRIEF
    NetLog("Fac:cp(%d,%u,%d,%d)", port, maxMsgSize, rcvBuf, sndBuf);
#else
    NetLog("PeerChannelFactoryUDP::createPeer: using local port=%d, MAX_MSG_SIZE=%u, RCVBUF=%d, SNDBUF=%d", port,
           maxMsgSize, rcvBuf, sndBuf);
#endif
#ifdef XP1_SUPPORT_BROADCAST
#ifdef NET_LOG_BRIEF
    NetLog("Fac:cp(%d,%d,%u,'%s')", (info.dwServiceFlags1 & XP1_SUPPORT_BROADCAST) > 0 ? 1 : 0, info.iVersion,
           (unsigned)info.dwMessageSize, info.szProtocol);
#else
    NetLog("PeerChannelFactoryUDP::createPeer: bcast=%d, version=%d, msgSize=%u, protocol='%s'",
           (info.dwServiceFlags1 & XP1_SUPPORT_BROADCAST) > 0 ? 1 : 0, info.iVersion, (unsigned)info.dwMessageSize,
           info.szProtocol);
#endif
#endif
#endif
    return new NetPeerUDP(s, port, pool);
}

NetChannel* PeerChannelFactoryUDP::createChannel(NetPool* pool, bool control)
{
    return new NetChannelBasic(control);
}

PeerChannelFactoryUDP::~PeerChannelFactoryUDP()
{
    if (--instances <= 0)
    {
        instances = 0;
#ifdef _WIN32
        WSACleanup();
#endif
    }
}
