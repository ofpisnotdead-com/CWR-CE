#include <Poseidon/Foundation/Platform/CrashHandler.hpp>
#include <Poseidon/Foundation/Platform/VersionNo.h>

#ifdef _WIN32

#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstring>

namespace Poseidon::Foundation
{
namespace
{
LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep)
{
    static bool reentered = false;
    if (reentered)
        return EXCEPTION_EXECUTE_HANDLER;
    reentered = true;

    HANDLE proc = GetCurrentProcess();
    SymInitialize(proc, nullptr, TRUE);

    DWORD code = ep->ExceptionRecord->ExceptionCode;
    void* addr = ep->ExceptionRecord->ExceptionAddress;
    fprintf(stderr, "\n=== UNHANDLED EXCEPTION 0x%08lX at %p ===\n", code, addr);
    LOG_ERROR(Core, "UNHANDLED EXCEPTION 0x{:08X} at {}", (unsigned)code, addr);

    CONTEXT* ctx = ep->ContextRecord;
    STACKFRAME64 frame = {};
#if defined(_M_X64) || defined(__x86_64__)
    constexpr DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = ctx->Rip;
    frame.AddrFrame.Offset = ctx->Rbp;
    frame.AddrStack.Offset = ctx->Rsp;
#else
    constexpr DWORD machineType = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = ctx->Eip;
    frame.AddrFrame.Offset = ctx->Ebp;
    frame.AddrStack.Offset = ctx->Esp;
#endif
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;

    char symBuf[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO* sym = reinterpret_cast<SYMBOL_INFO*>(symBuf);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;

    for (int i = 0; i < 40; ++i)
    {
        if (!StackWalk64(machineType, proc, GetCurrentThread(), &frame, ctx, nullptr, SymFunctionTableAccess64,
                         SymGetModuleBase64, nullptr))
            break;
        if (!frame.AddrPC.Offset)
            break;

        DWORD64 disp = 0;
        const char* name = "<unknown>";
        if (SymFromAddr(proc, frame.AddrPC.Offset, &disp, sym))
            name = sym->Name;

        IMAGEHLP_MODULE64 mod = {};
        mod.SizeOfStruct = sizeof(mod);
        const char* modName = "?";
        if (SymGetModuleInfo64(proc, frame.AddrPC.Offset, &mod))
            modName = mod.ModuleName;

        fprintf(stderr, "  #%02d  %s!%s+0x%llx  (0x%016llx)\n", i, modName, name, static_cast<unsigned long long>(disp),
                static_cast<unsigned long long>(frame.AddrPC.Offset));
        LOG_ERROR(Core, "  #{:02} {}!{}+0x{:x}  (0x{:016x})", i, modName, name, static_cast<unsigned long long>(disp),
                  static_cast<unsigned long long>(frame.AddrPC.Offset));
    }
    fflush(stderr);

    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (n > 0 && n < MAX_PATH)
    {
        char* last = std::strrchr(path, '\\');
        if (last)
            *last = 0;
        char dmp[MAX_PATH];
        snprintf(dmp, sizeof(dmp), "%s\\crash-%lu.dmp", path, GetCurrentProcessId());
        HANDLE f = CreateFileA(dmp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE)
        {
            MINIDUMP_EXCEPTION_INFORMATION mei = {GetCurrentThreadId(), ep, FALSE};
            MiniDumpWriteDump(proc, GetCurrentProcessId(), f, MiniDumpWithDataSegs, &mei, nullptr, nullptr);
            CloseHandle(f);
            fprintf(stderr, "  minidump: %s\n", dmp);
        }
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
} // namespace

void InstallCrashHandler(const char*)
{
    SetUnhandledExceptionFilter(CrashFilter);
}
} // namespace Poseidon::Foundation

#else // _WIN32

#include <Poseidon/Core/BuildInfo.hpp>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <execinfo.h>
#include <sys/resource.h>
#include <link.h>

namespace Poseidon::Foundation
{
namespace
{
constexpr int kFatalSignals[] = {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS};

char g_crashPath[4096];
char g_buildId[80]; // hex of the main object's NT_GNU_BUILD_ID
volatile sig_atomic_t g_inHandler = 0;
char g_altStack[65536]; // SIGSTKSZ is not a compile-time constant on modern glibc

// All writers below are async-signal-safe: only write()/integer formatting, no malloc/stdio.
void emit(int fd1, int fd2, const char* s)
{
    size_t n = std::strlen(s);
    if (fd1 >= 0)
        (void)!write(fd1, s, n);
    if (fd2 >= 0)
        (void)!write(fd2, s, n);
}

void emitHex(int fd1, int fd2, unsigned long v)
{
    char buf[2 + sizeof(unsigned long) * 2 + 1];
    char* p = buf;
    *p++ = '0';
    *p++ = 'x';
    for (int shift = (int)(sizeof(unsigned long) * 8) - 4; shift >= 0; shift -= 4)
    {
        int nib = (int)((v >> shift) & 0xF);
        *p++ = (char)(nib < 10 ? '0' + nib : 'a' + (nib - 10));
    }
    *p = '\0';
    emit(fd1, fd2, buf);
}

void emitInt(int fd1, int fd2, long v)
{
    char buf[24];
    char* p = buf + sizeof(buf);
    *--p = '\0';
    bool neg = v < 0;
    unsigned long u = neg ? (unsigned long)(-v) : (unsigned long)v;
    if (u == 0)
        *--p = '0';
    while (u)
    {
        *--p = (char)('0' + (u % 10));
        u /= 10;
    }
    if (neg)
        *--p = '-';
    emit(fd1, fd2, p);
}

const char* signalName(int sig)
{
    switch (sig)
    {
        case SIGSEGV:
            return "SIGSEGV";
        case SIGABRT:
            return "SIGABRT";
        case SIGFPE:
            return "SIGFPE";
        case SIGILL:
            return "SIGILL";
        case SIGBUS:
            return "SIGBUS";
        default:
            return "SIGNAL";
    }
}

void copyFileTo(int dstFd, const char* path)
{
    int src = open(path, O_RDONLY);
    if (src < 0)
        return;
    char buf[4096];
    for (;;)
    {
        ssize_t r = read(src, buf, sizeof(buf));
        if (r <= 0)
            break;
        (void)!write(dstFd, buf, (size_t)r);
    }
    close(src);
}

void handler(int sig, siginfo_t* info, void* /*ucontext*/)
{
    if (g_inHandler)
        _exit(128 + sig);
    g_inHandler = 1;

    int fd = open(g_crashPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    emit(fd, STDERR_FILENO, "\n=== CRASH: ");
    emit(fd, STDERR_FILENO, signalName(sig));
    emit(fd, STDERR_FILENO, " at ");
    emitHex(fd, STDERR_FILENO, info ? (unsigned long)info->si_addr : 0);
    emit(fd, STDERR_FILENO, " (pid ");
    emitInt(fd, STDERR_FILENO, (long)getpid());
    emit(fd, STDERR_FILENO, ") ===\nversion " APP_VERSION_TEXT "\ncommit ");
    emit(fd, STDERR_FILENO, Poseidon::BuildInfo::GitSha);
    emit(fd, STDERR_FILENO, "\nbuild-id ");
    emit(fd, STDERR_FILENO, g_buildId[0] ? g_buildId : "unknown");
    emit(fd, STDERR_FILENO, "\n\nbacktrace:\n");

    void* frames[64];
    int n = backtrace(frames, 64);
    backtrace_symbols_fd(frames, n, STDERR_FILENO);
    if (fd >= 0)
        backtrace_symbols_fd(frames, n, fd);

    // Raw return addresses: offline, (addr - module load base from the maps below) gives
    // the file offset to feed llvm-symbolizer/addr2line against the matching release binary.
    emit(fd, STDERR_FILENO, "\nreturn addresses:\n");
    for (int i = 0; i < n; i++)
    {
        emitHex(fd, STDERR_FILENO, (unsigned long)frames[i]);
        emit(fd, STDERR_FILENO, "\n");
    }

    // Module load bases — only to the file; keeps the terminal output short.
    if (fd >= 0)
    {
        emit(fd, -1, "\n/proc/self/maps:\n");
        copyFileTo(fd, "/proc/self/maps");
        close(fd);
    }

    emit(-1, STDERR_FILENO, "\ncrash report written to ");
    emit(-1, STDERR_FILENO, g_crashPath);
    emit(-1, STDERR_FILENO, "\n");

    // SA_RESETHAND restored the default disposition; re-raise so the OS still cores/terminates.
    raise(sig);
}

void captureBuildId()
{
    g_buildId[0] = '\0';
    dl_iterate_phdr(
        [](struct dl_phdr_info* info, size_t, void*) -> int
        {
            for (int i = 0; i < info->dlpi_phnum; i++)
            {
                const ElfW(Phdr) & ph = info->dlpi_phdr[i];
                if (ph.p_type != PT_NOTE)
                    continue;
                auto addr = info->dlpi_addr + ph.p_vaddr;
                ElfW(Addr) off = 0;
                while (off + sizeof(ElfW(Nhdr)) <= ph.p_memsz)
                {
                    const auto* nh = reinterpret_cast<const ElfW(Nhdr)*>(addr + off);
                    ElfW(Addr) nameAligned = (nh->n_namesz + 3) & ~ElfW(Addr)(3);
                    ElfW(Addr) descAligned = (nh->n_descsz + 3) & ~ElfW(Addr)(3);
                    const char* name = reinterpret_cast<const char*>(nh + 1);
                    if (nh->n_type == NT_GNU_BUILD_ID && nh->n_namesz == 4 && std::memcmp(name, "GNU", 4) == 0)
                    {
                        const unsigned char* desc = reinterpret_cast<const unsigned char*>(name + nameAligned);
                        char* w = g_buildId;
                        for (ElfW(Word) b = 0; b < nh->n_descsz && (w - g_buildId) < (long)sizeof(g_buildId) - 2; b++)
                        {
                            static const char hex[] = "0123456789abcdef";
                            *w++ = hex[desc[b] >> 4];
                            *w++ = hex[desc[b] & 0xF];
                        }
                        *w = '\0';
                        return 1; // stop at the main object (first callback)
                    }
                    off += sizeof(ElfW(Nhdr)) + nameAligned + descAligned;
                }
            }
            return 1; // only inspect the main executable (the first entry)
        },
        nullptr);
}
} // namespace

void InstallCrashHandler(const char* crashDir)
{
    if (crashDir && crashDir[0])
    {
        size_t len = std::strlen(crashDir);
        bool sep = len > 0 && crashDir[len - 1] == '/';
        snprintf(g_crashPath, sizeof(g_crashPath), sep ? "%scrash_%d.txt" : "%s/crash_%d.txt", crashDir, (int)getpid());
    }
    else
    {
        snprintf(g_crashPath, sizeof(g_crashPath), "crash_%d.txt", (int)getpid());
    }

    captureBuildId();

    // Warm up the libgcc unwinder so the in-handler backtrace() never dlopen()s (not
    // async-signal-safe). One call here primes it.
    void* warm[4];
    (void)backtrace(warm, 4);

    // Let the OS write a core too (best effort; honours the system's core_pattern).
    rlimit core{RLIM_INFINITY, RLIM_INFINITY};
    setrlimit(RLIMIT_CORE, &core);

    // Alternate stack so a stack-overflow SIGSEGV can still be handled.
    stack_t ss{};
    ss.ss_sp = g_altStack;
    ss.ss_size = sizeof(g_altStack);
    ss.ss_flags = 0;
    sigaltstack(&ss, nullptr);

    struct sigaction sa{};
    sa.sa_sigaction = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    for (int sig : kFatalSignals)
        sigaction(sig, &sa, nullptr);
}
} // namespace Poseidon::Foundation

#endif // _WIN32
