#pragma once

#include <Poseidon/Network/Network.hpp>

namespace Poseidon
{

inline NetworkGameState ResolveNetworkManagerServerState(bool simulateMode, bool hasServer,
                                                         NetworkGameState serverState, bool hasClient,
                                                         NetworkGameState clientServerState)
{
    if (hasServer && simulateMode)
    {
        return serverState;
    }

    if (hasClient)
    {
        return clientServerState;
    }

    return NGSNone;
}

inline NetworkGameState ResolveMultiplayerSetupDisplayState(bool isServer, NetworkGameState serverState,
                                                            NetworkGameState clientState)
{
    if (isServer)
    {
        return serverState;
    }

    if (clientState == NGSTransferMission && serverState >= NGSTransferMission && serverState < NGSBriefing)
    {
        return clientState;
    }

    if (serverState < NGSBriefing && clientState >= NGSBriefing)
    {
        return clientState;
    }

    return serverState;
}

} // namespace Poseidon
