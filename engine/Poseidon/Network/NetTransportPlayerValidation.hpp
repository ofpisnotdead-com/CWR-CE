#pragma once

#include <Poseidon/Network/NetTransportPlayerValidationPolicy.hpp>
#include <Poseidon/Network/NetTransportProtocol.hpp>

namespace Poseidon
{

bool IsNetTransportCreatePlayerVersionOrModMismatch(const SessionPacket& session, const CreatePlayerPacket& request);

ConnectResult ValidateNetTransportCreatePlayerRequest(const SessionPacket& session, const char* serverPassword,
                                                      const CreatePlayerPacket& request);

} // namespace Poseidon
