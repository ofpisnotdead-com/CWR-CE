#pragma once

namespace Poseidon
{
template <typename Role>
inline Role BuildNetworkPlayerRoleAssignmentRequest(const Role& currentRole, int player)
{
    Role info = currentRole;
    info.player = player;
    info.roleLocked = false;
    return info;
}
} // namespace Poseidon
