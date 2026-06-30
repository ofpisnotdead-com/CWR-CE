#pragma once

#include <Poseidon/Network/Legacy/netpch.hpp>
#include <Poseidon/Network/NetTransportEnumRequest.hpp>
#include <Poseidon/Network/NetTransportLocking.hpp>

#include <type_traits>
#include <utility>

namespace Poseidon
{

template <class T>
T* GetNetTransportRawPointer(T* value)
{
    return value;
}

template <class T>
T* GetNetTransportRawPointer(const RefD<T>& value)
{
    return value.operator->();
}

inline void SetupNetTransportPeerPortMask(BitMask& mask, int networkPort, bool server, bool hasPidFile)
{
    mask.empty();
    if (!server)
    {
        mask.on(0);
        return;
    }

    int port = networkPort;
    if (server && hasPidFile)
    {
        mask.on(port);
        return;
    }

    for (int i = 0; i++ < NetTransportEnumPortsToTry; port += NetTransportEnumPortInterval)
    {
        mask.on(port);
    }
}

template <class PoolRef, class CreatePoolFn>
auto EnsureNetTransportPool(PoolRef& pool, CreatePoolFn&& createPool) -> decltype(GetNetTransportRawPointer(pool))
{
    auto* rawPool = GetNetTransportRawPointer(pool);
    if (!rawPool)
    {
        pool = createPool();
        rawPool = GetNetTransportRawPointer(pool);
    }
    return rawPool;
}

template <class PoolT, class PortMaskSetup>
auto CreateNetTransportPeer(PoolT* pool, NetCallBack* processRoutine, PortMaskSetup&& setupPortMask)
    -> decltype(std::declval<PoolT*>()->createPeer(static_cast<BitMask*>(nullptr)))
{
    if (!pool)
    {
        return nullptr;
    }

    BitMask portMask;
    setupPortMask(portMask);

    auto* peer = pool->createPeer(&portMask);
    if (!peer)
    {
        return nullptr;
    }

    if (auto* ctrl = peer->getBroadcastChannel())
    {
        ctrl->setProcessRoutine(processRoutine);
    }

    return peer;
}

template <class PeerRef, class EnsurePoolFn, class PortMaskSetup>
auto EnsureNetTransportPeer(PeerRef& peer, NetCallBack* processRoutine, EnsurePoolFn&& ensurePool,
                            PortMaskSetup&& setupPortMask) -> decltype(GetNetTransportRawPointer(peer))
{
    auto* rawPeer = GetNetTransportRawPointer(peer);
    if (rawPeer)
    {
        return rawPeer;
    }

    peer = CreateNetTransportPeer(ensurePool(), processRoutine, std::forward<PortMaskSetup>(setupPortMask));
    return GetNetTransportRawPointer(peer);
}

template <class PeerRef, class EnsurePoolFn>
auto EnsureNetTransportPeerWithRole(PeerRef& peer, NetCallBack* processRoutine, EnsurePoolFn&& ensurePool,
                                    int networkPort, bool server,
                                    bool hasPidFile) -> decltype(GetNetTransportRawPointer(peer))
{
    return EnsureNetTransportPeer(peer, processRoutine, std::forward<EnsurePoolFn>(ensurePool), [=](BitMask& portMask)
                                  { SetupNetTransportPeerPortMask(portMask, networkPort, server, hasPidFile); });
}

template <class InstanceT, class EnterFn, class LeaveFn, class InitFn>
void AssignNetTransportInstance(InstanceT*& slot, InstanceT* value, EnterFn&& enterPoolLock, LeaveFn&& leavePoolLock,
                                InitFn&& initializeTransport)
{
    enterPoolLock();
    slot = value;
    if (value)
    {
        initializeTransport();
    }
    leavePoolLock();
}

template <class InstanceT, class EnterFn, class LeaveFn, class EnsurePoolFn, class EnsurePeerFn>
void AssignNetTransportInstanceWithPeer(InstanceT*& slot, InstanceT* value, EnterFn&& enterPoolLock,
                                        LeaveFn&& leavePoolLock, EnsurePoolFn&& ensurePool, EnsurePeerFn&& ensurePeer)
{
    AssignNetTransportInstance(slot, value, std::forward<EnterFn>(enterPoolLock), std::forward<LeaveFn>(leavePoolLock),
                               [&]()
                               {
                                   std::forward<EnsurePoolFn>(ensurePool)();
                                   std::forward<EnsurePeerFn>(ensurePeer)();
                               });
}

template <class EnterFn, class LeaveFn, class PeerGetter, class Action>
decltype(auto) AccessNetTransportPeerLocked(EnterFn&& enterPoolLock, LeaveFn&& leavePoolLock, PeerGetter&& getPeer,
                                            Action&& action)
{
    enterPoolLock();
    using PeerValue = std::invoke_result_t<PeerGetter>;
    if constexpr (std::is_void_v<std::invoke_result_t<Action, PeerValue>>)
    {
        action(getPeer());
        leavePoolLock();
    }
    else
    {
        auto result = action(getPeer());
        leavePoolLock();
        return result;
    }
}

template <class EnterFn, class LeaveFn, class PeerGetter>
unsigned16 ReadNetTransportPeerPortLocked(EnterFn&& enterPoolLock, LeaveFn&& leavePoolLock, PeerGetter&& getPeer)
{
    return AccessNetTransportPeerLocked(std::forward<EnterFn>(enterPoolLock), std::forward<LeaveFn>(leavePoolLock),
                                        std::forward<PeerGetter>(getPeer),
                                        [](auto* peer) { return peer ? peer->getPort() : 0; });
}

template <class EnterFn, class LeaveFn, class PeerGetter>
void CancelNetTransportPeerMessagesLocked(EnterFn&& enterPoolLock, LeaveFn&& leavePoolLock, PeerGetter&& getPeer)
{
    AccessNetTransportPeerLocked(std::forward<EnterFn>(enterPoolLock), std::forward<LeaveFn>(leavePoolLock),
                                 std::forward<PeerGetter>(getPeer),
                                 [](auto* peer)
                                 {
                                     if (peer)
                                     {
                                         peer->cancelAllMessages();
                                     }
                                 });
}

template <class DestroyPoolFn>
void DestroyNetTransportPoolIfUnused(const void* enumerator, const void* client, const void* server,
                                     DestroyPoolFn&& destroyPool)
{
    if (!enumerator && !client && !server)
    {
        destroyPool();
    }
}

template <class EnterFn, class LeaveFn, class DestroyPoolFn>
void DestroyNetTransportPoolIfUnusedLocked(const void* enumerator, const void* client, const void* server,
                                           EnterFn&& enterPoolLock, LeaveFn&& leavePoolLock,
                                           DestroyPoolFn&& destroyPool)
{
    AccessNetTransportLocked(
        std::forward<EnterFn>(enterPoolLock), std::forward<LeaveFn>(leavePoolLock), [&]()
        { DestroyNetTransportPoolIfUnused(enumerator, client, server, std::forward<DestroyPoolFn>(destroyPool)); });
}

template <class PeerRef, class PoolRef>
void DestroyNetTransportPeer(PeerRef& peer, PoolRef pool, bool closeBeforeDelete = false)
{
    if (!peer)
    {
        return;
    }

    auto* rawPeer = GetNetTransportRawPointer(peer);
    auto* rawPool = GetNetTransportRawPointer(pool);
    if (closeBeforeDelete)
    {
        rawPeer->close();
    }

    if (rawPool)
    {
        rawPool->deletePeer(rawPeer);
    }
    else if (!closeBeforeDelete)
    {
        rawPeer->close();
    }

    peer = nullptr;
}

template <class ClientPeerRef, class ServerPeerRef, class PoolRef>
void DestroyNetTransportPeersAndPool(ClientPeerRef& clientPeer, ServerPeerRef& serverPeer, PoolRef& pool)
{
    DestroyNetTransportPeer(clientPeer, pool);
    DestroyNetTransportPeer(serverPeer, pool);
    if (pool)
    {
        pool = nullptr;
    }
}

template <class PeerRef, class PoolRef, class EnterFn, class LeaveFn>
void DestroyNetTransportPeerLocked(PeerRef& peer, PoolRef pool, bool closeBeforeDelete, EnterFn&& enterPoolLock,
                                   LeaveFn&& leavePoolLock)
{
    AccessNetTransportLocked(std::forward<EnterFn>(enterPoolLock), std::forward<LeaveFn>(leavePoolLock),
                             [&]() { DestroyNetTransportPeer(peer, pool, closeBeforeDelete); });
}

} // namespace Poseidon
