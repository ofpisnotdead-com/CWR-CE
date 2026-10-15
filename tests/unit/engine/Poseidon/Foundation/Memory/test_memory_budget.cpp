#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Foundation/Memory/MemFreeReq.hpp>
#include <Poseidon/Foundation/Memory/MemoryBudget.hpp>

#include <atomic>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>

// Memory budget — process ceiling + per-subsystem accounting/eviction.
//
// Bug class these guard: the allocator was unbounded (only a per-allocation
// 256 MB reject + OS-failure FreeOnDemand). These pin the policy layer:
//  - ProcessMemoryBudget is pure accounting: it tracks process residency and the
//    peak and NEVER refuses or evicts. A refusal would make the engine's unchecked
//    `new` sites write into a null buffer and crash; the ceiling is
//    an eviction trigger (driven off the alloc path by FrameMaintenance), not a
//    wall. `used` stays exact across reserve/reconcile/release.
//  - MemoryFreeOnDemandList reports per-subsystem residency and trims any
//    registrant back to its declared budget.
//
// Broken-state deltas (revert the fix → these fail):
//  - Make Reserve refuse over the ceiling: "never refuses, even over the hard
//    ceiling" sees Reserve return false and `used` left below the request.
//  - Drop the BudgetBytes() gate in EnforceBudgets(): under-budget registrants
//    get trimmed and "leaves under-budget registrant untouched" fails.
//
// The FrameMaintenance eviction path (over-soft trim + over-hard claw-back) is
// pinned at the allocator level in test_jimbo_allocator.cpp.

using Poseidon::Foundation::DeriveDefaultMemoryLimits;
using Poseidon::Foundation::IMemoryFreeOnDemand;
using Poseidon::Foundation::kDefaultHardLimitMB;
using Poseidon::Foundation::kDefaultSoftLimitMB;
using Poseidon::Foundation::kMemHeadroomMB;
using Poseidon::Foundation::MemoryDomainStat;
using Poseidon::Foundation::MemoryFreeOnDemandList;
using Poseidon::Foundation::MemoryLimits;
using Poseidon::Foundation::ProcessMemoryBudget;

// ----------------------------------------------------------------------------
// ProcessMemoryBudget
// ----------------------------------------------------------------------------

TEST_CASE("ProcessMemoryBudget - unlimited by default", "[memory][budget]")
{
    ProcessMemoryBudget b;
    REQUIRE(b.HardLimit() == 0);
    REQUIRE(b.SoftLimit() == 0);

    REQUIRE(b.Reserve(1000));
    REQUIRE(b.Reserve(2000));
    REQUIRE(b.Used() == 3000);
    REQUIRE(b.Peak() == 3000);
    REQUIRE_FALSE(b.OverSoftLimit());

    b.Release(1000);
    REQUIRE(b.Used() == 2000);
    REQUIRE(b.Peak() == 3000); // peak is a high-water mark
}

TEST_CASE("ProcessMemoryBudget - Reconcile adjusts used both ways", "[memory][budget]")
{
    ProcessMemoryBudget b;
    REQUIRE(b.Reserve(1000));

    b.Reconcile(1000, 1024); // backing alloc was larger than requested
    REQUIRE(b.Used() == 1024);
    REQUIRE(b.Peak() == 1024);

    b.Reconcile(1024, 1000); // smaller (hypothetical)
    REQUIRE(b.Used() == 1000);
}

TEST_CASE("ProcessMemoryBudget - Release saturates at zero", "[memory][budget]")
{
    ProcessMemoryBudget b;
    REQUIRE(b.Reserve(500));
    b.Release(900); // more than held — must clamp, never wrap
    REQUIRE(b.Used() == 0);
}

TEST_CASE("ProcessMemoryBudget - soft limit drives OverSoftLimit", "[memory][budget]")
{
    ProcessMemoryBudget b;
    b.SetLimits(/*soft*/ 1000, /*hard*/ 0);

    REQUIRE(b.Reserve(900));
    REQUIRE_FALSE(b.OverSoftLimit());
    REQUIRE(b.Reserve(200)); // soft limit never rejects, only signals
    REQUIRE(b.Used() == 1100);
    REQUIRE(b.OverSoftLimit());
}

TEST_CASE("ProcessMemoryBudget - never refuses, even over the hard ceiling", "[memory][budget]")
{
    ProcessMemoryBudget b;
    b.SetLimits(0, /*hard*/ 1000);

    // The budget must NEVER return false: refusing an allocation would make the
    // engine's many unchecked `new` sites write into a null buffer and crash
    // (terrain VertexTable::AddVertex -> memset). The budget is
    // pure accounting — it never evicts and never refuses. Crossing the ceiling
    // instead drives eviction off the allocation path, once per frame, via the
    // allocator's FrameMaintenance() (see test_jimbo_allocator.cpp). So over-ceiling
    // reservations are simply admitted and `_used` exceeds the cap.
    REQUIRE(b.Reserve(600));
    REQUIRE(b.Reserve(600)); // 1200 > 1000 → still admitted, not refused
    REQUIRE(b.Used() == 1200);
}

TEST_CASE("DeriveDefaultMemoryLimits - fixed cap, reserves OS headroom", "[memory][budget]")
{
    const size_t mb = static_cast<size_t>(1024) * 1024;
    const size_t gib = mb * 1024;
    const size_t headroom = static_cast<size_t>(kMemHeadroomMB) * mb;

    // Unknown RAM, and machines too small to cap without trimming into the game's own
    // working set (<= headroom + 1 GB) → unlimited.
    REQUIRE(DeriveDefaultMemoryLimits(0).hard == 0);
    REQUIRE(DeriveDefaultMemoryLimits(3 * gib).hard == 0);

    // Large machine (64 GB): both clamp to the ABSOLUTE caps, not a fraction of RAM —
    // 80% of 64 GB would be a useless ~50 GB backstop for a game that peaks under 1 GB.
    {
        const MemoryLimits lim = DeriveDefaultMemoryLimits(64 * gib);
        REQUIRE(lim.soft == static_cast<size_t>(kDefaultSoftLimitMB) * mb); // 4 GB cap
        REQUIRE(lim.hard == static_cast<size_t>(kDefaultHardLimitMB) * mb); // 8 GB cap
        REQUIRE(lim.soft < lim.hard);
    }

    // 16 GB (the mainstream target): the cap dominates, leaving far more than the
    // reserve free for the OS.
    {
        const MemoryLimits lim = DeriveDefaultMemoryLimits(16 * gib);
        REQUIRE(lim.hard == static_cast<size_t>(kDefaultHardLimitMB) * mb); // 8 GB cap
        REQUIRE(lim.hard <= 16 * gib - headroom);                           // headroom reserved
    }

    // 8 GB: the cap can't be met, so hard = RAM minus the reserve (6 GB) — the OS
    // keeps its 2 GB. soft stays at its 4 GB ceiling.
    {
        const MemoryLimits lim = DeriveDefaultMemoryLimits(8 * gib);
        REQUIRE(lim.hard == 8 * gib - headroom);
        REQUIRE(lim.soft == static_cast<size_t>(kDefaultSoftLimitMB) * mb);
        REQUIRE(lim.soft < lim.hard);
    }

    // 4 GB: hard = 2 GB (reserve held), soft = 75% of it (1.5 GB) — still well above
    // the game's ~0.6 GB residency, so it never trims in real play.
    //
    // Broken-state delta: drop the headroom reserve (the old 90%-of-RAM clamp) and
    // hard becomes 3.6 GB, leaving the OS only 0.4 GB — the `<= phys - headroom`
    // assertions fail.
    {
        const MemoryLimits lim = DeriveDefaultMemoryLimits(4 * gib);
        REQUIRE(lim.hard == 4 * gib - headroom);
        REQUIRE(lim.hard <= 4 * gib - headroom);
        REQUIRE(lim.soft == lim.hard / 4 * 3);
        REQUIRE(lim.soft < lim.hard);
    }
}

TEST_CASE("ProcessMemoryBudget - concurrent reserve/release stays exact", "[memory][budget][stress]")
{
    ProcessMemoryBudget b; // unlimited — exercise accounting under contention

    constexpr int kThreads = 8;
    constexpr int kIters = 20000;
    constexpr size_t kChunk = 64;

    std::atomic<bool> allReserved{true};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back(
            [&]
            {
                for (int i = 0; i < kIters; ++i)
                {
                    if (!b.Reserve(kChunk))
                        allReserved = false;
                    b.Reconcile(kChunk, kChunk);
                    b.Release(kChunk);
                }
            });
    }
    for (auto& th : threads)
        th.join();

    // Every reserve was balanced by a release — used must return to exactly 0,
    // and peak must have climbed at least one chunk (something was live).
    REQUIRE(allReserved);
    REQUIRE(b.Used() == 0);
    REQUIRE(b.Peak() >= kChunk);
}

TEST_CASE("ProcessMemoryBudget - concurrent reserve/release with a ceiling stays exact", "[memory][budget][stress]")
{
    ProcessMemoryBudget b;
    constexpr size_t kHard = 64 * 1024;
    b.SetLimits(0, kHard);

    constexpr int kThreads = 8;
    constexpr int kIters = 5000;
    constexpr size_t kChunk = 512;

    std::atomic<int> admitted{0};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back(
            [&]
            {
                for (int i = 0; i < kIters; ++i)
                {
                    if (b.Reserve(kChunk))
                    {
                        admitted.fetch_add(1, std::memory_order_relaxed);
                        // Hold briefly, then release so room frees up again.
                        b.Release(kChunk);
                    }
                }
            });
    }
    for (auto& th : threads)
        th.join();

    // Every reserve is admitted (the budget never refuses) and balanced by its
    // release, so accounting returns to exactly zero under contention.
    REQUIRE(b.Used() == 0);
    REQUIRE(admitted.load() == kThreads * kIters);
}

// ----------------------------------------------------------------------------
// MemoryFreeOnDemandList — per-subsystem snapshot + budget enforcement
// ----------------------------------------------------------------------------

namespace
{
// A subsystem that reports residency and trims in fixed chunks, like the real
// FileCache / OperCache registrants.
class FakeSubsystem : public IMemoryFreeOnDemand
{
  public:
    FakeSubsystem(const char* name, size_t held, size_t budget, float priority, size_t chunk = 100)
        : _name(name), _held(held), _budget(budget), _priority(priority), _chunk(chunk)
    {
    }

    size_t Free(size_t amount) override
    {
        size_t freed = 0;
        while (freed < amount && _held > 0)
        {
            const size_t take = _chunk < _held ? _chunk : _held;
            _held -= take;
            freed += take;
        }
        return freed;
    }
    size_t FreeAll() override
    {
        const size_t freed = _held;
        _held = 0;
        return freed;
    }
    float Priority() override { return _priority; }
    const char* DomainName() const override { return _name; }
    size_t HeldBytes() const override { return _held; }
    size_t BudgetBytes() const override { return _budget; }
    size_t HeldItems() const override { return _items; }

    size_t Held() const { return _held; }
    void SetItems(size_t n) { _items = n; }

  private:
    const char* _name;
    size_t _held;
    size_t _budget;
    float _priority;
    size_t _chunk;
    size_t _items = 0;
};
} // namespace

TEST_CASE("MemoryFreeOnDemandList - Snapshot reports per-subsystem residency", "[memory][budget][snapshot]")
{
    MemoryFreeOnDemandList list;
    FakeSubsystem a("FileCache", 5000, 8000, 2.0f);
    FakeSubsystem b("Terrain", 1234, 0, 5.0f);
    b.SetItems(42); // count-only registry (no byte size)
    list.Register(&a);
    list.Register(&b);

    MemoryDomainStat stats[8];
    const int n = list.Snapshot(stats, 8);
    REQUIRE(n == 2);

    // Registry is priority-ordered (lowest first), so FileCache (2.0) precedes
    // Terrain (5.0).
    REQUIRE(std::string(stats[0].name) == "FileCache");
    REQUIRE(stats[0].heldBytes == 5000);
    REQUIRE(stats[0].budgetBytes == 8000);
    REQUIRE(stats[0].heldItems == 0);
    REQUIRE(std::string(stats[1].name) == "Terrain");
    REQUIRE(stats[1].heldBytes == 1234);
    REQUIRE(stats[1].budgetBytes == 0);
    REQUIRE(stats[1].heldItems == 42);
}

TEST_CASE("MemoryFreeOnDemandList - Snapshot respects maxOut", "[memory][budget][snapshot]")
{
    MemoryFreeOnDemandList list;
    FakeSubsystem a("A", 1, 0, 1.0f), b("B", 2, 0, 2.0f), c("C", 3, 0, 3.0f);
    list.Register(&a);
    list.Register(&b);
    list.Register(&c);

    MemoryDomainStat stats[2];
    REQUIRE(list.Snapshot(stats, 2) == 2); // never writes past the array
}

TEST_CASE("MemoryFreeOnDemandList - EnforceBudgets trims only over-budget domains", "[memory][budget][enforce]")
{
    MemoryFreeOnDemandList list;
    FakeSubsystem over("Over", /*held*/ 1000, /*budget*/ 400, 1.0f);
    FakeSubsystem under("Under", /*held*/ 300, /*budget*/ 500, 2.0f);
    FakeSubsystem nobudget("NoBudget", /*held*/ 9999, /*budget*/ 0, 3.0f);
    list.Register(&over);
    list.Register(&under);
    list.Register(&nobudget);

    const size_t freed = list.EnforceBudgets();

    REQUIRE(freed == 600);            // over-budget domain trimmed by held-budget
    REQUIRE(over.Held() == 400);      // trimmed back to its budget
    REQUIRE(under.Held() == 300);     // under budget — untouched
    REQUIRE(nobudget.Held() == 9999); // no declared budget — untouched
}

TEST_CASE("MemoryFreeOnDemandList - EnforceBudgets no-op when all within budget", "[memory][budget][enforce]")
{
    MemoryFreeOnDemandList list;
    FakeSubsystem a("A", 100, 200, 1.0f);
    FakeSubsystem b("B", 0, 0, 2.0f);
    list.Register(&a);
    list.Register(&b);

    REQUIRE(list.EnforceBudgets() == 0);
    REQUIRE(a.Held() == 100);
}

// ----------------------------------------------------------------------------
// MemoryDomainProbe — the adapter that lets a bank join the registry without
// inheriting IMemoryFreeOnDemand. Registers into the global allocator list; the
// stack-scoped probe auto-unlinks (CLTLink dtor) at end of scope.
// ----------------------------------------------------------------------------

using Poseidon::Foundation::MemoryDomainProbe;
using Poseidon::Foundation::MemorySnapshotDomains;

TEST_CASE("MemoryDomainProbe - forwards callbacks + appears in global snapshot", "[memory][budget][probe]")
{
    size_t held = 5000;
    const size_t budget = 8000;
    const size_t items = 12;

    MemoryDomainProbe probe;
    probe.Register(
        "UnitTestProbe", 1.0f, [&] { return held; }, [&] { return budget; }, [&] { return items; },
        [&](size_t n)
        {
            size_t f = n < held ? n : held;
            held -= f;
            return f;
        });

    REQUIRE(std::string(probe.DomainName()) == "UnitTestProbe");
    REQUIRE(probe.HeldBytes() == 5000);
    REQUIRE(probe.BudgetBytes() == 8000);
    REQUIRE(probe.HeldItems() == 12);

    // Free hook routes through the supplied callback.
    REQUIRE(probe.Free(1000) == 1000);
    REQUIRE(held == 4000);

    // Visible in the process-wide snapshot while alive.
    MemoryDomainStat stats[32];
    const int n = MemorySnapshotDomains(stats, 32);
    bool found = false;
    for (int i = 0; i < n; i++)
    {
        if (std::string(stats[i].name) == "UnitTestProbe")
        {
            found = true;
            REQUIRE(stats[i].heldItems == 12);
            REQUIRE(stats[i].budgetBytes == 8000);
        }
    }
    REQUIRE(found);
}

TEST_CASE("MemoryDomainProbe - empty free hook is non-evicting", "[memory][budget][probe]")
{
    MemoryDomainProbe probe;
    probe.Register("NonEvicting", 1.0f, [] { return (size_t)999; }); // bytes only

    REQUIRE(probe.HeldBytes() == 999);
    REQUIRE(probe.BudgetBytes() == 0);
    REQUIRE(probe.HeldItems() == 0);
    REQUIRE(probe.Free(100) == 0); // no free callback → nothing reclaimable
    REQUIRE(probe.FreeAll() == 0);
}
