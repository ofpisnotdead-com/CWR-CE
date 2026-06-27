#pragma once

#include <Poseidon/Foundation/Threads/PoThread.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>

#ifdef __APPLE__
#include <condition_variable>
#include <mutex>
#endif

namespace Poseidon::Foundation
{
// Portable simple semaphore.
class PoSemaphore : public RefCountSafe
{
  protected:
#ifdef _WIN32

    HANDLE handle;

#elif defined(__APPLE__)

    // Darwin has no working unnamed POSIX semaphore (sem_init() always fails
    // with ENOSYS). dispatch_semaphore_t was tried as a replacement, but it
    // traps on dispose if its count is below its creation value, which this
    // class's wait/destroy usage doesn't guarantee — so this rolls its own
    // counter instead.
    mutable std::mutex mutex;
    std::condition_variable cv;
    long count;

#else

    sem_t sem;

#endif

  public:
    bool error;

    // init: starting counter (>=0). maxCount: ceiling; error is set if exceeded.
    // Failure reasons are not portable.
    PoSemaphore(long init = 0L, long maxCount = 12);

    // Blocks until the counter is positive. Does not touch error.
    virtual void wait();

    // Non-blocking wait: returns true if the counter was positive, false otherwise. Does not touch error.
    virtual bool tryWait();

    // Adds count to the counter, waking up to count waiters. Sets error if the ceiling is exceeded.
    virtual void signal(long count = 1L);

    virtual long getValue();

    ~PoSemaphore() override;
};

// Semaphore that carries a "titbit" payload with each signal(); consumers receive
// them in FIFO order. TitbitType is the value stored per signal() event.
template <class TitbitType>
class PoSemaphoreTitbit : public PoSemaphore
{
  protected:
    Poseidon::Foundation::AutoArray<TitbitType, MemAllocSafe> tb;

    long size; // titbit array size

    long first; // next titbit to consume

    long last; // slot for the next incoming titbit

    TitbitType defaultTitbit;

    // Enqueue a titbit. No synchronization — the caller must already hold the lock.
    void putTitbit(TitbitType titbit)
    {
        long newLast = last + 1;
        if (newLast >= size)
        {
            newLast = 0L;
        }
        if (newLast != first)
        {
            tb[last] = titbit;
            last = newLast;
        }
    }

    // Dequeue the oldest titbit (undefined if the queue is empty). Locks around the read.
    void getTitbit(TitbitType& titbit)
    {
        enter();
        if (first != last)
        {
            titbit = tb[first++];
            if (first >= size)
            {
                first = 0L;
            }
        }
        leave();
    }

  public:
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — init/maxCount are documented and distinct
    PoSemaphoreTitbit(long init = 0L, long maxCount = 12L) : PoSemaphore(init, maxCount)
    {
        LockRegister(lock, "PoSemaphoreTitbit");
        size = first = last = 0;
        if (maxCount < 1)
        {
            maxCount = 1;
        }
        if (init < 0L)
        {
            init = 0L;
        }
        else if (init > maxCount)
        {
            init = maxCount;
        }
        if (error)
        {
            return;
        }
        tb.Resize(size = maxCount + 12); // room for 10 nested interrupted waits
        long i;
        for (i = 0; i++ < init;)
        {
            putTitbit(defaultTitbit);
        }
    }

    // Blocks until ready; discards the associated titbit.
    void wait() override
    {
        PoSemaphore::wait();
        TitbitType dummy;
        getTitbit(dummy);
    }

    // Blocks until ready; hands the associated titbit back through titbit.
    virtual void wait(TitbitType& titbit)
    {
        PoSemaphore::wait();
        getTitbit(titbit);
    }

    // Non-blocking; on success discards the associated titbit.
    bool tryWait() override
    {
        bool success = PoSemaphore::tryWait();
        if (success)
        {
            TitbitType dummy;
            getTitbit(dummy);
        }
        return success;
    }

    // Non-blocking; on success hands the titbit back (left untouched on failure).
    virtual bool tryWait(TitbitType& titbit)
    {
        bool success = PoSemaphore::tryWait();
        if (success)
        {
            getTitbit(titbit);
        }
        return success;
    }

    // Signal count times using the default titbit (see setDefaultTitbit). Sets error if the ceiling is exceeded.
    void signal(long count = 1L) override
    {
        if (count < 1L)
        {
            count = 1L;
        }
        enter();
        PoSemaphore::signal(count);
        if (!error)
        {
            while (count--)
            {
                putTitbit(defaultTitbit);
            }
        }
        leave();
    }

    // Signal once, carrying titbit to the next consumer. Sets error if the ceiling is exceeded.
    virtual void signal(TitbitType titbit)
    {
        enter();
        PoSemaphore::signal();
        putTitbit(titbit);
        leave();
    }

    // Signal count times, taking one titbit per signal from titbitArray. Sets error if the ceiling is exceeded.
    virtual void signal(long count, TitbitType* titbitArray)
    {
        if (count < 1L)
        {
            count = 1L;
        }
        enter();
        PoSemaphore::signal(count);
        if (!error)
        {
            while (count--)
            {
                putTitbit(*titbitArray++);
            }
        }
        leave();
    }

    // Default titbit used by signal(long).
    virtual void setDefaultTitbit(TitbitType dTitbit) { defaultTitbit = dTitbit; }

    ~PoSemaphoreTitbit() override = default;
};

} // namespace Poseidon::Foundation

