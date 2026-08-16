#include <Poseidon/Foundation/Threads/PoCritical.hpp>
#include <Poseidon/Foundation/Memory/CheckMem.hpp>

namespace Poseidon::Foundation
{

#ifndef _WIN32

#ifdef __APPLE__
pthread_mutex_t mutexInit = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;
#else
pthread_mutex_t mutexInit = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
#endif

#endif

#ifdef LOCK_TRACING

void lockEnter(int id);
void lockLeave(int id);
int registerLock(const char* srcFile, int lineNo, const char* descr);

PoCriticalSection::PoCriticalSection(const char* srcFile, int lineNo, const char* descr)
{
    valid = true;
#ifdef _WIN32
    InitializeCriticalSection(&cs);
#else
    mutex = mutexInit;
#endif
    id = registerLock(srcFile, lineNo, descr);
    error = false;
}

void PoCriticalSection::registerMe(const char* srcFile, int lineNo, const char* descr)
{
    id = registerLock(srcFile, lineNo, descr);
}

#endif // LOCK_TRACING

PoCriticalSection::PoCriticalSection(bool val)
{
    // NOLINTNEXTLINE(bugprone-assignment-in-if-condition) — assign-and-test is intentional
    if ((valid = val))
    {
#ifdef _WIN32
        InitializeCriticalSection(&cs);
#else
        mutex = mutexInit;
#endif
    }
    error = false;
#ifdef LOCK_TRACING
    id = -1;
#endif
}

PoCriticalSection::PoCriticalSection()
{
    valid = true;
#ifdef _WIN32
    InitializeCriticalSection(&cs);
#else
    mutex = mutexInit;
#endif
    error = false;
#ifdef LOCK_TRACING
    id = -1;
#endif
}

void PoCriticalSection::enter() const
{
    if (!valid)
    {
        return;
    }
#ifdef _WIN32
    EnterCriticalSection(&cs);
#ifdef LOCK_TRACING
    lockEnter(id);
#endif
#else
    error = (pthread_mutex_lock(&mutex) != 0);
#endif
}

bool PoCriticalSection::tryEnter() const
{
    if (!valid)
    {
        return true;
    }
#ifdef _WIN32
#ifdef _WINNT
    return (TryEnterCriticalSection(&cs) != 0);
#else
    EnterCriticalSection(&cs); // no TryEnterCriticalSection here — falls back to blocking
    return true;
#endif
#else
    error = false;
    return (pthread_mutex_trylock(&mutex) == 0);
#endif
}

void PoCriticalSection::leave() const
{
    if (!valid)
    {
        return;
    }
#ifdef _WIN32
#ifdef LOCK_TRACING
    lockLeave(id);
#endif
    LeaveCriticalSection(&cs);
#else
    error = (pthread_mutex_unlock(&mutex) != 0);
#endif
}

PoCriticalSection::~PoCriticalSection()
{
    if (!valid)
    {
        return;
    }
#ifdef _WIN32
    DeleteCriticalSection(&cs);
#else
    error = (pthread_mutex_destroy(&mutex) != 0);
#endif
}

void* PoCriticalSection::operator new(size_t size)
{
    return safeNew(size);
}

void* PoCriticalSection::operator new(size_t size, const char* file, int line)
{
    (void)file;
    (void)line;
    return safeNew(size);
}

void PoCriticalSection::operator delete(void* mem)
{
    safeDelete(mem);
}

#ifdef __INTEL_COMPILER

void PoCriticalSection::operator delete(void* mem, const char* file, int line)
{
    safeDelete(mem);
}

#endif

#ifdef LOCK_TRACING

#define MAX_LOCK_INSTANCES 256
#define MAX_LOCK_THREADS 32
#define MAX_LOCK_NESTING 128
#define MAX_LOCK_ITEMS 32768

struct LockInstance
{
    const char* srcFile;

    int lineNo;

    const char* descr;
};

static LockInstance lockDb[MAX_LOCK_INSTANCES];

static int lockDbSize = 0;

int registerLock(const char* srcFile, int lineNo, const char* descr)
{
    int i;
    for (i = 0; i < lockDbSize; i++)
        if (srcFile == lockDb[i].srcFile && lineNo == lockDb[i].lineNo && descr == lockDb[i].descr)
            break;
    if (i < lockDbSize)
        return i;
    if (lockDbSize >= MAX_LOCK_INSTANCES)
        return -1;
    lockDb[lockDbSize].srcFile = srcFile;
    lockDb[lockDbSize].lineNo = lineNo;
    lockDb[lockDbSize].descr = descr;
    lockDbSize++;
#ifdef NET_LOG
    if (lockDbSize == 5)
        for (i = 0; i < 5; i++)
            NetLog("registerLock: id=%3d: '%s' (in '%s' at line %d)", i, lockDb[i].descr ? lockDb[i].descr : "-",
                   lockDb[i].srcFile ? lockDb[i].srcFile : "-", lockDb[i].lineNo);
    else if (lockDbSize > 5)
        NetLog("registerLock: id=%3d: '%s' (in '%s' at line %d)", lockDbSize - 1, descr ? descr : "-",
               srcFile ? srcFile : "-", lineNo);
#endif
    return lockDbSize - 1;
}

struct LockItem
{
    int id;

    int sibling;

    int son;
};

static LockItem lItems[MAX_LOCK_ITEMS];

static int lItemsSize = 0;

int newLockItem()
{
    if (lItemsSize == MAX_LOCK_ITEMS)
        return -1;
    lItems[lItemsSize].sibling = lItems[lItemsSize].son = -1;
    return lItemsSize++;
}

struct ThreadStack
{
    ThreadId tid;

    int stack[MAX_LOCK_NESTING];

    int sp;

    int root;

    void processStack();
};

static ThreadStack stacks[MAX_LOCK_THREADS];

static int stacksSize = 0;

ThreadStack& getThreadStack(ThreadId tid)
{
    int i;
    for (i = 0; i < stacksSize; i++)
        if (stacks[i].tid == tid)
            return stacks[i];
    DoAssert(stacksSize < MAX_LOCK_THREADS);
    stacks[stacksSize].tid = tid;
    stacks[stacksSize].sp = 0;
    stacks[stacksSize].root = -1;
    stacksSize++;
#ifdef NET_LOG
    if (stacksSize == 2)
        for (i = 0; i < 2; i++)
            NetLog("getThreadStack: new thread tid=%u", (unsigned)stacks[i].tid);
    else if (stacksSize > 2)
        NetLog("getThreadStack: new thread tid=%u", (unsigned)tid);
#endif
    return stacks[stacksSize - 1];
}

#ifdef NET_LOG
static FILE* lf = nullptr;
static unsigned flushCount = 0;
#endif

void ThreadStack::processStack()
{
    int i;
    bool newIt = false;
    int* ptr = &root;
    for (i = 0; i < sp; i++)
    {
        // looking for stack[i] in ptr and its siblings:
        while (*ptr >= 0 && lItems[*ptr].id != stack[i])
            ptr = &lItems[*ptr].sibling;
        if (*ptr < 0)
        {
            newIt = true;
            *ptr = newLockItem();
            if (*ptr < 0)
                break;
            lItems[*ptr].id = stack[i];
        }
        ptr = &lItems[*ptr].son;
    }
#ifdef NET_LOG
    if (!newIt)
        return;
    if (!lf)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "lock%04x.txt", ((unsigned)tid) & 0xffff);
        lf = fopen(buf, "wt");
    }
    if (lf)
    {
        fprintf(lf, "%u", (unsigned)tid);
        for (i = 0; i <= sp; i++)
            fprintf(lf, " %d", stack[i]);
        putc('\n', lf);
        if (!(++flushCount & 0xf))
            fflush(lf);
    }
#endif
}

void lockEnter(int id)
{
    ThreadId tid;
    poThreadId(tid);
    ThreadStack& ts = getThreadStack(tid);
    if (ts.sp >= MAX_LOCK_NESTING)
        return;
    ts.stack[ts.sp++] = id;
    ts.processStack();
}

void lockLeave(int id)
{
    ThreadId tid;
    poThreadId(tid);
    ThreadStack& ts = getThreadStack(tid);
    if (ts.sp == 0)
        return;
    if (ts.sp == MAX_LOCK_NESTING && ts.stack[ts.sp - 1] != id)
        return;
    if (ts.stack[ts.sp - 1] != id)
    {
#ifdef NET_LOG
        NetLog("lockLeave: leave() without matching enter() (enter=%d,leave=%d)", ts.stack[ts.sp - 1], id);
#endif
        return;
    }
    ts.sp--;
}

#endif // LOCK_TRACING

} // namespace Poseidon::Foundation
