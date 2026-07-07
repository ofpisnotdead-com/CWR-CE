#pragma once

// The default CloudSync scope shared by every trigger-point call site
// (launch pull, quit push, mission-save push, profile-creation push) so it's
// defined in exactly one place. Kept separate from CloudSync.hpp, which
// stays path-agnostic.

#include <Poseidon/Core/CloudSync/CloudSync.hpp>

#include <vector>

namespace Poseidon::CloudSync
{

// Per issue #73: profiles/saves (GamePaths::UserDir()+"Users/") and the
// shared editor missions (GamePaths::UserContentDir()+"missions/"+
// "MPMissions/"). Deliberately excludes GamePaths::CacheDir() (regenerable)
// and Mods/Workshop (re-downloadable/large) -- see GamePaths.hpp.
std::vector<SyncPair> DefaultSyncPairs();

// Opportunistic, fire-and-forget push using DefaultSyncPairs() -- for call
// sites (mission save, profile creation) that want a change visible on other
// devices sooner than the next quit, without blocking the caller or owning a
// SyncWorker themselves. No-ops when IsAvailable() is false. Failures are
// silent here (the quit-time push is the authoritative, logged one) -- a
// prior in-flight PushInBackground() call is abandoned (cancel + detach,
// same as any other SyncWorker::Start()) rather than queued.
void PushInBackground();

} // namespace Poseidon::CloudSync
