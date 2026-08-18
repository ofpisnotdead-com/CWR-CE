#pragma once

#include <Poseidon/Network/Network.hpp>

namespace Poseidon
{
struct NetworkJIPMessage
{
    NetworkMessageType type = NMTNone;
    Ref<NetworkMessage> msg;
    RString key;
};

constexpr int MaxNetworkJIPMessages = 10000;

bool AddOrReplaceJIPMessage(AutoArray<NetworkJIPMessage>& queue, NetworkMessageType type, Ref<NetworkMessage> msg,
                            const RString& key, int maxMessages = MaxNetworkJIPMessages);
bool RemoveJIPMessage(AutoArray<NetworkJIPMessage>& queue, NetworkMessageType type, const RString& key);
} // namespace Poseidon
