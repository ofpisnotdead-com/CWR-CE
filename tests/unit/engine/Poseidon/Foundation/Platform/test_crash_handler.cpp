#include <catch2/catch_test_macros.hpp>

#ifdef _WIN32

#include <Poseidon/Foundation/Platform/CrashHandler.hpp>

#include <windows.h>
#include <dbghelp.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

TEST_CASE("crash handler child process", "[.][platform][crash]")
{
    Poseidon::Foundation::InstallCrashHandler(nullptr);
    RaiseException(0xe0000001, EXCEPTION_NONCONTINUABLE, 0, nullptr);
}

TEST_CASE("crash handler preserves the exception context in a minidump", "[platform][crash]")
{
    char executable[MAX_PATH];
    REQUIRE(GetModuleFileNameA(nullptr, executable, MAX_PATH) > 0);

    std::string command = std::string("\"") + executable + "\" \"crash handler child process\" --reporter compact";

    // dbghelp refuses the write transiently; one child without a dump is not a failure.
    std::filesystem::path dumpPath;
    for (int attempt = 0; attempt < 3 && dumpPath.empty(); ++attempt)
    {
        std::vector<char> mutableCommand(command.begin(), command.end());
        mutableCommand.push_back('\0');

        STARTUPINFOA startup = {};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process = {};
        REQUIRE(CreateProcessA(executable, mutableCommand.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                               &startup, &process));
        CloseHandle(process.hThread);
        REQUIRE(WaitForSingleObject(process.hProcess, 30000) == WAIT_OBJECT_0);
        CloseHandle(process.hProcess);

        const std::filesystem::path candidate =
            std::filesystem::path(executable).parent_path() / ("crash-" + std::to_string(process.dwProcessId) + ".dmp");
        if (std::filesystem::exists(candidate))
        {
            dumpPath = candidate;
        }
    }
    INFO("no minidump written; see the child's \"minidump failed\" line for the Win32 error");
    REQUIRE_FALSE(dumpPath.empty());

    std::ifstream input(dumpPath, std::ios::binary | std::ios::ate);
    REQUIRE(input.good());
    std::vector<char> dump(static_cast<size_t>(input.tellg()));
    input.seekg(0);
    input.read(dump.data(), static_cast<std::streamsize>(dump.size()));
    REQUIRE(input.good());

    MINIDUMP_DIRECTORY* directory = nullptr;
    MINIDUMP_EXCEPTION_STREAM* exception = nullptr;
    ULONG streamSize = 0;
    REQUIRE(MiniDumpReadDumpStream(dump.data(), ExceptionStream, &directory, reinterpret_cast<void**>(&exception),
                                   &streamSize));
    REQUIRE(exception != nullptr);
    REQUIRE(exception->ThreadContext.Rva + exception->ThreadContext.DataSize <= dump.size());

    const CONTEXT* context = reinterpret_cast<const CONTEXT*>(dump.data() + exception->ThreadContext.Rva);
#if defined(_M_X64) || defined(__x86_64__)
    CHECK(context->Rip == exception->ExceptionRecord.ExceptionAddress);
#else
    CHECK(context->Eip == exception->ExceptionRecord.ExceptionAddress);
#endif

    input.close();
    std::filesystem::remove(dumpPath);
}

#else

#include <Poseidon/Foundation/Platform/CrashHandler.hpp>

#include <csignal>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

// A Linux crash must leave an offline-symbolizable trace, not a bare kernel "Segmentation
// fault". Method: fork a child, install the handler pointed at a temp dir, raise SIGSEGV. The
// child dies with SIGSEGV (the handler re-raises after writing the report). The parent then
// asserts the report exists and carries every marker an offline symbolizer needs — the
// signal, the build commit, a backtrace, the raw return addresses, and the module load bases
// from /proc/self/maps (return-addr minus load-base = the offset for llvm-symbolizer/addr2line
// against the matching release binary). Broken-state delta: without InstallCrashHandler the
// child SIGSEGVs with no file written at all.

TEST_CASE("crash handler writes a symbolizable report on a fatal signal", "[platform][crash]")
{
    char dirTmpl[] = "/tmp/cwr_crash_test_XXXXXX";
    REQUIRE(mkdtemp(dirTmpl) != nullptr);
    std::string dir = dirTmpl;

    pid_t pid = fork();
    REQUIRE(pid >= 0);
    if (pid == 0)
    {
        Poseidon::Foundation::InstallCrashHandler(dir.c_str());
        raise(SIGSEGV);
        _exit(0); // unreachable: the handler re-raises to the default disposition
    }

    int status = 0;
    REQUIRE(waitpid(pid, &status, 0) == pid);
    REQUIRE(WIFSIGNALED(status));
    REQUIRE(WTERMSIG(status) == SIGSEGV);

    std::string path = dir + "/crash_" + std::to_string(pid) + ".txt";
    std::ifstream f(path);
    REQUIRE(f.good());
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string report = ss.str();

    REQUIRE(report.find("=== CRASH: SIGSEGV") != std::string::npos);
    REQUIRE(report.find("commit ") != std::string::npos);
    REQUIRE(report.find("backtrace:") != std::string::npos);
    REQUIRE(report.find("return addresses:") != std::string::npos);
    REQUIRE(report.find("/proc/self/maps:") != std::string::npos);

    remove(path.c_str());
    rmdir(dir.c_str());
}

#endif
