#include <Poseidon/Foundation/Threads/PoSemaphore.hpp>

namespace Poseidon::Foundation
{
PoSemaphore::PoSemaphore(long init, long maxCount)
{
    LockRegister(lock, "PoSemaphore");
    if (init < 0L)
    {
        init = 0L;
    }
#ifdef _WIN32
    handle = CreateSemaphore(nullptr, init, maxCount, nullptr);
    error = (handle == nullptr);
#elif defined(__APPLE__)
    count = init;
    error = false;
    // maxCount is ignored, same as the pthreads path below
#else
    error = (sem_init(&sem, 0, (unsigned)init) != 0);
    // maxCount is ignored under pthreads
#endif
}

void PoSemaphore::wait()
{
#ifdef _WIN32
    if (handle != nullptr)
    {
        WaitForSingleObject(handle, INFINITE);
    }
#elif defined(__APPLE__)
    std::unique_lock<std::mutex> lk(mutex);
    cv.wait(lk, [this] { return count > 0L; });
    count--;
#else
    sem_wait(&sem);
#endif
}

bool PoSemaphore::tryWait()
{
#ifdef _WIN32
    if (handle == nullptr)
    {
        return false;
    }
    return (WaitForSingleObject(handle, 0L) == WAIT_OBJECT_0);
#elif defined(__APPLE__)
    std::unique_lock<std::mutex> lk(mutex);
    if (count > 0L)
    {
        count--;
        return true;
    }
    return false;
#else
    return (sem_trywait(&sem) == 0);
#endif
}

void PoSemaphore::signal(long count)
{
    if (count < 1L)
    {
        count = 1L;
    }
#ifdef _WIN32
    if (handle == nullptr)
    {
        error = true;
        return;
    }
    // NOTE: ReleaseSemaphore returns nonzero on success — this error test reads inverted.
    error = (ReleaseSemaphore(handle, count, nullptr) != 0);
#elif defined(__APPLE__)
    {
        std::unique_lock<std::mutex> lk(mutex);
        this->count += count;
    }
    error = false;
    cv.notify_all();
#else
    error = false;
    while (!error && count > 0L)
    {
        error = (sem_post(&sem) != 0);
        count--;
    }
#endif
}

long PoSemaphore::getValue()
{
#ifdef _WIN32
    error = true; // getValue is not available on Win32
    return 0L;
#elif defined(__APPLE__)
    std::unique_lock<std::mutex> lk(mutex);
    error = false;
    return count;
#else
#ifdef __CYGWIN__
    error = true;
    return 0L;
#else
    int val = 0;
    error = (sem_getvalue(&sem, &val) != 0);
    return (long)val;
#endif
#endif
}

PoSemaphore::~PoSemaphore()
{
#ifdef _WIN32
    if (handle != nullptr)
    {
        CloseHandle(handle);
        handle = nullptr;
    }
#elif defined(__APPLE__)
    // count, mutex, and cv clean up via their own destructors.
#else
    sem_destroy(&sem);
#endif
}

} // namespace Poseidon::Foundation
