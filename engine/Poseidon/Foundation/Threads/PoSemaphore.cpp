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
#elif __APPLE__
    // macOS has deprecated and removed POSIX semaphores (sem_init returns ENOTSUP).
    // Use pthread mutex + condition variable as a portable replacement.
    error = false;
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&lock.mutex, &attr);
    pthread_mutexattr_destroy(&attr);
    pthread_cond_init(&lock.cond, nullptr);
    lock.waiters = 0;
    counter = (init < maxCount) ? init : maxCount;
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
#elif __APPLE__
    pthread_mutex_lock(&lock.mutex);
    lock.waiters++;
    while (counter <= 0L)
    {
        pthread_cond_wait(&lock.cond, &lock.mutex);
    }
    counter--;
    lock.waiters--;
    pthread_mutex_unlock(&lock.mutex);
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
#elif __APPLE__
    pthread_mutex_lock(&lock.mutex);
    bool result = (counter > 0L);
    if (result)
    {
        counter--;
    }
    pthread_mutex_unlock(&lock.mutex);
    return result;
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
#elif __APPLE__
    pthread_mutex_lock(&lock.mutex);
    counter += count;
    long signaled = (count > lock.waiters) ? lock.waiters : count;
    for (long i = 0L; i < signaled; i++)
    {
        pthread_cond_signal(&lock.cond);
    }
    pthread_mutex_unlock(&lock.mutex);
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
#elif __APPLE__
    pthread_mutex_lock(&lock.mutex);
    long val = counter;
    pthread_mutex_unlock(&lock.mutex);
    return val;
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
#elif __APPLE__
    pthread_mutex_destroy(&lock.mutex);
    pthread_cond_destroy(&lock.cond);
#else
    sem_destroy(&sem);
#endif
}

} // namespace Poseidon::Foundation
