#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Network/NetServerPeerTeardown.hpp>

#include <atomic>
#include <chrono>
#include <thread>

using Poseidon::JoinServerPeerWithoutPoolLock;
using Poseidon::Foundation::PoCriticalSection;

// Mirrors NetServer::~NetServer's serverPeer teardown. The peer's UDP listener
// takes poolLock inside ctrlReceive, so joining the peer (peer->close()) while
// holding poolLock deadlocks. JoinServerPeerWithoutPoolLock releases poolLock
// across the join; regressing that discipline reintroduces the deadlock, which
// this test catches via the watchdog below.
TEST_CASE("server peer join releases poolLock so a concurrent ctrlReceive can finish",
          "[network][termination][deadlock]")
{
    using namespace std::chrono_literals;

    // Heap-allocated so a thread still blocked on the lock (only on a regression)
    // never touches a destroyed mutex; leaked on that failure path on purpose.
    auto* poolLock = new PoCriticalSection(true);

    std::atomic<bool> teardownHoldsLock{false};
    std::atomic<bool> contenderStarted{false};
    std::atomic<bool> contenderDone{false};
    std::atomic<bool> joinReturned{false};

    // The teardown thread holds poolLock and then joins the peer via the helper.
    std::thread teardown(
        [&]()
        {
            poolLock->enter();
            teardownHoldsLock.store(true);
            while (!contenderStarted.load())
            {
                std::this_thread::sleep_for(1ms);
            }
            std::this_thread::sleep_for(50ms); // let the contender block on poolLock->enter()

            // The "join" waits for the contender to finish; the contender can only
            // finish once it acquires poolLock, which requires the helper to have
            // released it. If the helper held poolLock here, this would deadlock.
            JoinServerPeerWithoutPoolLock(*poolLock,
                                          [&]()
                                          {
                                              while (!contenderDone.load())
                                              {
                                                  std::this_thread::sleep_for(1ms);
                                              }
                                          });
            poolLock->leave();
            joinReturned.store(true);
        });

    while (!teardownHoldsLock.load())
    {
        std::this_thread::sleep_for(1ms);
    }

    // Mimics a ctrlReceive that must take poolLock to complete a CreatePlayer.
    std::thread contender(
        [&]()
        {
            contenderStarted.store(true);
            poolLock->enter();
            poolLock->leave();
            contenderDone.store(true);
        });

    for (int i = 0; i < 300 && !joinReturned.load(); ++i)
    {
        std::this_thread::sleep_for(10ms);
    }

    const bool ok = joinReturned.load();
    CHECK(ok);

    if (ok)
    {
        teardown.join();
        contender.join();
        CHECK(contenderDone.load());
        delete poolLock;
    }
    else
    {
        // Regression: poolLock was held across the join, so both threads are
        // deadlocked. Detach them and leak poolLock; the process exit reaps them.
        teardown.detach();
        contender.detach();
    }
}
