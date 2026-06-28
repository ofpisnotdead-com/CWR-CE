#pragma once

namespace Poseidon
{

inline bool ShouldTransferNetworkTankTurretState(bool sending, bool localCrewControlsTurret)
{
    return sending || !localCrewControlsTurret;
}

template <class Matrix3T, class Matrix4T>
Matrix3T BuildCommanderTankTurretStabilizationFrame(bool commanderTurretOnMainTurret, const Matrix3T& newBodyFrame,
                                                    const Matrix4T& mainTurretTransform)
{
    return commanderTurretOnMainTurret ? newBodyFrame * mainTurretTransform.Orientation() : newBodyFrame;
}

template <class Matrix3T>
const Matrix3T& SelectCommanderTankTurretOldStabilizationFrame(bool commanderTurretOnMainTurret,
                                                               const Matrix3T& oldBodyFrame,
                                                               const Matrix3T& oldMainTurretFrame)
{
    return commanderTurretOnMainTurret ? oldMainTurretFrame : oldBodyFrame;
}

} // namespace Poseidon
