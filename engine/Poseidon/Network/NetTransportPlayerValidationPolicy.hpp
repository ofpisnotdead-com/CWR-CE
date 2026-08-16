#pragma once

#include <cstring>

#include <Poseidon/Foundation/platform.hpp>
#include <Poseidon/Network/NetTransport.hpp>

namespace Poseidon
{

template <class SessionPacketT, class CreatePlayerPacketT>
bool IsNetTransportCreatePlayerVersionOrModMismatchPolicy(const SessionPacketT& session,
                                                          const CreatePlayerPacketT& request)
{
    return request.actualVersion < session.requiredVersion || session.actualVersion < request.requiredVersion ||
           ((session.equalModRequired & 1) != 0 && stricmp(session.mod, request.mod) != 0) ||
           stricmp(session.versionTag, request.versionTag) != 0;
}

template <class SessionPacketT, class CreatePlayerPacketT>
ConnectResult EvaluateNetTransportCreatePlayerRequest(const SessionPacketT& session, const char* serverPassword,
                                                      const CreatePlayerPacketT& request)
{
    if (IsNetTransportCreatePlayerVersionOrModMismatchPolicy(session, request))
    {
        return CRVersion;
    }

    if (std::strncmp(request.password, serverPassword != nullptr ? serverPassword : "", sizeof(request.password)) != 0)
    {
        return CRPassword;
    }

    return CROK;
}

} // namespace Poseidon
