#pragma once

#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/Network/NetTransportVoicePlayerQueue.hpp>
#include <Poseidon/Network/NetTransportVoiceRouting.hpp>
#include <Poseidon/Audio/Voice/VonNet.hpp>
#include <unordered_map>
using Poseidon::VoNChatChannel;

namespace Poseidon
{
inline VoNChatChannel NetTransportChatChannelToVoN(int channel)
{
    switch (channel)
    {
        case CCGlobal:
            return VoNChatChannel::Global;
        case CCSide:
            return VoNChatChannel::Side;
        case CCGroup:
            return VoNChatChannel::Group;
        case CCVehicle:
            return VoNChatChannel::Vehicle;
        case CCDirect:
        default:
            return VoNChatChannel::Direct;
    }
}

template <class Allocator>
void ReadNetTransportServerVoiceTargets(const std::unordered_map<int, std::vector<int>>& vonTargets, int from,
                                        AutoArray<int, Allocator>& to)
{
    ReadNetTransportVoiceTargets(vonTargets, from, to);
}

template <class Allocator>
void GetNetTransportServerVoiceTargets(const std::unordered_map<int, std::vector<int>>& vonTargets, int from,
                                       AutoArray<int, Allocator>& to)
{
    ReadNetTransportServerVoiceTargets(vonTargets, from, to);
}

template <class Allocator, class GetServerFn>
void WriteNetTransportServerVoiceTargets(std::unordered_map<int, std::vector<int>>& vonTargets, int from,
                                         const AutoArray<int, Allocator>& to, int channel, GetServerFn&& getServer)
{
    const std::vector<uint32_t> targets = WriteNetTransportVoiceTargets(vonTargets, from, to);
    if (auto* server = getServer())
    {
        server->setRouting(static_cast<uint32_t>(from), NetTransportChatChannelToVoN(channel), targets);
    }
}

template <class Allocator, class GetServerFn, class OnTargetsUpdatedFn>
void SetNetTransportServerVoiceTargets(std::unordered_map<int, std::vector<int>>& vonTargets, int from,
                                       const AutoArray<int, Allocator>& to, int channel, GetServerFn&& getServer,
                                       OnTargetsUpdatedFn&& onTargetsUpdated)
{
    WriteNetTransportServerVoiceTargets(vonTargets, from, to, channel, std::forward<GetServerFn>(getServer));
    std::forward<OnTargetsUpdatedFn>(onTargetsUpdated)(from, to.Size());
}

template <class Allocator, class GetServerFn, class LogTargetsUpdatedFn>
void SetNetTransportServerVoiceTargetsWithLog(std::unordered_map<int, std::vector<int>>& vonTargets, int from,
                                              const AutoArray<int, Allocator>& to, int channel, GetServerFn&& getServer,
                                              LogTargetsUpdatedFn&& logTargetsUpdated)
{
    SetNetTransportServerVoiceTargets(vonTargets, from, to, channel, std::forward<GetServerFn>(getServer),
                                      std::forward<LogTargetsUpdatedFn>(logTargetsUpdated));
}

inline void NotifyNetTransportServerVoicePlayers(const std::unordered_map<int, std::vector<int>>& vonTargets,
                                                 CreateVoicePlayerCallback* callback, void* context)
{
    NotifyNetTransportVoicePlayers(vonTargets, callback, context);
}

inline void ProcessNetTransportServerVoicePlayers(const std::unordered_map<int, std::vector<int>>& vonTargets,
                                                  CreateVoicePlayerCallback* callback, void* context)
{
    NotifyNetTransportServerVoicePlayers(vonTargets, callback, context);
}

template <class GetServerFn>
void ClearNetTransportServerVoicePlayers(std::unordered_map<int, std::vector<int>>& vonTargets, GetServerFn&& getServer)
{
    if (auto* server = getServer())
    {
        ClearNetTransportVoicePlayers(vonTargets,
                                      [server](int sender) { server->removePlayer(static_cast<uint32_t>(sender)); });
        return;
    }

    ClearNetTransportVoicePlayers(vonTargets, [](int) {});
}

template <class GetServerFn>
void RemoveNetTransportServerVoicePlayers(std::unordered_map<int, std::vector<int>>& vonTargets,
                                          GetServerFn&& getServer)
{
    ClearNetTransportServerVoicePlayers(vonTargets, std::forward<GetServerFn>(getServer));
}

template <class GetServerFn>
void RouteNetTransportServerVoicePacket(GetServerFn&& getServer, int player, const void* data, int length)
{
    if (auto* server = getServer())
    {
        server->onDataPacket(data, length);
        LOG_TRACE(Network, "VoN srv: routed voice pkt ({}B) from player={}", length, player);
    }
}

} // namespace Poseidon
