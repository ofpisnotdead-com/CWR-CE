#pragma once

#include <Poseidon/Foundation/Threads/PoCritical.hpp>

namespace Poseidon
{
// Runs `join` (which blocks until the server peer's UDP threads have stopped)
// with `poolLock` released, then re-acquires it. The peer's listener thread
// takes poolLock inside ctrlReceive, so joining the peer while holding poolLock
// deadlocks. Callers must hold `poolLock` on entry; it is held again on return.
template <class JoinFn>
inline void JoinServerPeerWithoutPoolLock(Foundation::PoCriticalSection& poolLock, JoinFn&& join)
{
    poolLock.leave();
    join();
    poolLock.enter();
}
} // namespace Poseidon
