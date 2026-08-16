#include <Poseidon/Network/NetTransportPlayerValidation.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>

namespace Poseidon
{

bool IsNetTransportCreatePlayerVersionOrModMismatch(const SessionPacket& session, const CreatePlayerPacket& request)
{
    return IsNetTransportCreatePlayerVersionOrModMismatchPolicy(session, request);
}

ConnectResult ValidateNetTransportCreatePlayerRequest(const SessionPacket& session, const char* serverPassword,
                                                      const CreatePlayerPacket& request)
{
    const ConnectResult result = EvaluateNetTransportCreatePlayerRequest(session, serverPassword, request);
    if (result == CRVersion)
    {
        LOG_INFO(Network,
                 "CreatePlayer rejected by version/mod check: server actual={} required={} mod='{}' tag='{}'; "
                 "client actual={} required={} mod='{}' tag='{}'; equalModRequired={}",
                 session.actualVersion, session.requiredVersion, session.mod, session.versionTag, request.actualVersion,
                 request.requiredVersion, request.mod, request.versionTag, session.equalModRequired);
    }
    return result;
}

} // namespace Poseidon
