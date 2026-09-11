#include <Poseidon/Network/Legacy/SocketUtil.hpp>
#include <Poseidon/Network/Legacy/NetApi.hpp>
#include <Poseidon/Foundation/Common/Global.hpp>
#include <Poseidon/Foundation/Threads/PoThread.hpp>
#include <atomic>
#ifdef _MSC_VER
#pragma once
#endif

#ifndef _NETPEER_H
#define _NETPEER_H

// Fill me with the local network address bound to port. Returns true on success.
bool getLocalAddress(struct sockaddr_in& me, unsigned16 port);

// Retrieve the local host name into name (len bytes). Returns true on success.
bool getLocalName(char* name, unsigned len);

// Fill host from the string IP and port. Returns true on success.
bool getHostAddress(struct sockaddr_in& host, const char* ip, unsigned16 port);

// UDP net-peer: when opened, bound to one UDP port and driving all duplex traffic
// through it ([localIP:port]-[*:*]). Individual links are handled by net-channels.
class NetPeerUDP : public NetPeer
{
  protected:
    // [distantIP:port] -> NetChannel routing table
    ImplicitMap<unsigned64, RefD<NetChannel>, channelKey, true, Poseidon::Foundation::MemAllocSafe> chMap;

    Poseidon::Foundation::SOCKET sock; // shared socket handle

    Poseidon::Foundation::ThreadId listener;
    Poseidon::Foundation::ThreadId sender;

    std::atomic<bool> listen;  // is this port listening?
    std::atomic<bool> sending; // is this port sending?

    friend THREAD_PROC_RETURN THREAD_PROC_MODE udpListen(void* param);
    friend THREAD_PROC_RETURN THREAD_PROC_MODE udpSend(void* param);

    // Reconnect the port after a WSAECONNRESET error.
    void reconnect();

    bool reconnecting;

  public:
    // For service use only.
    NetPeerUDP(NetPool* _pool);

    // _sock must already have a successful bind().
    NetPeerUDP(Poseidon::Foundation::SOCKET _sock, unsigned16 _port, NetPool* _pool);

    void getLocalAddress(struct sockaddr_in& local) const override;

    unsigned maxMessageData() const override;

    // Register a new net-channel. Two channels must not share the same distant address+port.
    bool registerChannel(struct sockaddr_in& distant, NetChannel* ch) override;

    void unregisterChannel(NetChannel* ch) override;

    // Find the net-channel bound to distant; nullptr if none.
    NetChannel* findChannel(const struct sockaddr_in& distant) override;

    void close() override;

    // Process incoming data; called asynchronously by the listener thread. Must not run
    // channel processing — peer statistics only. hdr carries length then data.
    void processData(MsgHeader* hdr, const struct sockaddr_in& distant) override;

    // Send data to distant. distant may be Zero() for broadcast.
    NetStatus sendData(MsgHeader* hdr, struct sockaddr_in distant) override;

    void cancelAllMessages() override;

    // Initialize dispatcher status. If data is nullptr, only the required length is returned.
    unsigned initDispatcherStatus(DispatcherStatus* data) override;

    ~NetPeerUDP() override;
};

#endif
