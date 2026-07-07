#include <Poseidon/Core/CloudSync/CloudSyncPaths.hpp>

#include <Poseidon/Foundation/Common/GamePaths.hpp>

namespace Poseidon::CloudSync
{

std::vector<SyncPair> DefaultSyncPairs()
{
    const Foundation::GamePaths& gp = Foundation::GamePaths::Instance();
    return {
        {gp.UserDir() + GameDirs::Users, GameDirs::Users},
        {gp.UserContentDir() + "missions", "missions"},
        {gp.UserContentDir() + GameDirs::MPMissions, GameDirs::MPMissions},
    };
}

void PushInBackground()
{
    if (!IsAvailable())
        return;
    // Static duration so it outlives this call -- Start() abandons (cancel +
    // detach) whichever run, if any, is still in flight from a previous call.
    static SyncWorker worker(MakeAppleSyncOpsEnv());
    worker.Start(DefaultSyncPairs(), SyncDirection::Push);
}

} // namespace Poseidon::CloudSync
