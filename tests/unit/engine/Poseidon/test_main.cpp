// Catch2 test main entry point for PoseidonTests
#define CATCH_CONFIG_RUNNER
#include <catch2/catch_session.hpp>

// Required stubs for Poseidon dependencies
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Platform/InitBridge.hpp>

#include <cstdlib>
#include <filesystem>
#include <random>
#include <stdarg.h>
#include <string>
#include <system_error>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/platform.hpp>
#include <spdlog/spdlog.h>

// Early initialization of memory system for tests
// This runs before Catch2's AutoReg constructors (which run during .CRT$XCU)
// We use #pragma init_seg(compiler) to run in .CRT$XCA/.CRT$XCZ (before .CRT$XCU)
#pragma init_seg(compiler)
namespace
{
struct EarlyMemoryInit
{
    EarlyMemoryInit()
    {
        // Enable memory system before any static constructors allocate
        SetMemorySystemReady(true);
    }
} g_earlyMemoryInit;
} // namespace

#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Audio/AudioFactory.hpp>
#include <Poseidon/Audio/Voice/VoiceBackend.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Graphics/Dummy/GraphicsEngineDummy.hpp>
#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <Poseidon/IO/ParamFile/InitLibraryElement.hpp>
#include <Poseidon/Foundation/Platform/InitBridge.hpp>
#include <Poseidon/Foundation/Platform/PoseidonInit.hpp>

using Poseidon::GEngine;

using Poseidon::CreateEngineDummy;

// Minimal test Application that provides configuration system
class TestApplication : public Poseidon::Application
{
  public:
    TestApplication() : Poseidon::Application(Poseidon::Application::CLIENT) {}

    int Run(const char*) override { return 0; } // Not used in tests

  protected:
    bool InitializeMemorySystem() override { return true; }
    bool ParseCommandLine(const char*) override { return true; }
    bool ReadConfiguration() override { return true; }
    bool InitializeSubsystems() override { return true; }
    void RunMainLoop() override {}
    void ShutdownSubsystems() override {}
};

// Stubs for testing
class TestAppFrameFunctions : public Poseidon::Foundation::AppFrameFunctions
{
  public:
    void ErrorMessage(Poseidon::Foundation::ErrorMessageLevel, const char*, va_list) override {}
    void ErrorMessage(const char*, va_list) override {}
    void WarningMessage(const char*, va_list) override {}
    void ShowMessage(int, const char*, va_list) override {}
    DWORD TickCount() override { return 0; }
};

static TestAppFrameFunctions testAppFrame;

namespace
{
void SetEnvVar(const char* name, const std::string& value)
{
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

void ClearEnvVar(const char* name)
{
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

class ScopedPoseidonTestPaths
{
  public:
    ScopedPoseidonTestPaths()
    {
        _oldUser = GetEnvVar("POSEIDON_USER_DIR");
        _oldCache = GetEnvVar("POSEIDON_CACHE_DIR");
        _oldTemp = GetEnvVar("POSEIDON_TEMP_DIR");

        _baseDir = std::filesystem::temp_directory_path() / std::filesystem::path("CWR-poseidontests-user");
        // Seed from `random_device` (per-process) so parallel ctest
        // invocations of this binary don't collide on the same temp
        // dir.  `std::rand()` without srand() returns the same value
        // in every process and made the temp dir process-shared —
        // one process's destructor `remove_all` would race against
        // another's in-flight reads, surfacing as 0xC0000409.
        std::random_device rd;
        _baseDir /= std::to_string(rd());
        _userDir = (_baseDir / "user").string();
        _cacheDir = (_baseDir / "cache").string();
        _tempDir = (_baseDir / "temp").string();

        std::filesystem::create_directories(_userDir);
        std::filesystem::create_directories(_cacheDir);
        std::filesystem::create_directories(_tempDir);

        SetEnvVar("POSEIDON_USER_DIR", _userDir);
        SetEnvVar("POSEIDON_CACHE_DIR", _cacheDir);
        SetEnvVar("POSEIDON_TEMP_DIR", _tempDir);
    }

    ~ScopedPoseidonTestPaths()
    {
        RestoreEnvVar("POSEIDON_USER_DIR", _oldUser);
        RestoreEnvVar("POSEIDON_CACHE_DIR", _oldCache);
        RestoreEnvVar("POSEIDON_TEMP_DIR", _oldTemp);

        std::error_code ec;
        std::filesystem::remove_all(_baseDir, ec);
    }

  private:
    static std::string GetEnvVar(const char* name)
    {
        if (const char* value = std::getenv(name))
            return value;
        return {};
    }

    static void RestoreEnvVar(const char* name, const std::string& value)
    {
        if (value.empty())
            ClearEnvVar(name);
        else
            SetEnvVar(name, value);
    }

    std::filesystem::path _baseDir;
    std::string _userDir;
    std::string _cacheDir;
    std::string _tempDir;
    std::string _oldUser;
    std::string _oldCache;
    std::string _oldTemp;
};
} // namespace

// Minimal main for running Catch2 tests
int main(int argc, char* argv[])
{
    SetEnvVar("POSEIDON_TEST", "1");
    spdlog::default_logger()->set_level(spdlog::level::off);

    // Set the global Poseidon::Foundation::AppFrameFunctions pointer (defined in Poseidon.lib)
    Poseidon::Foundation::CurrentAppFrameFunctions = &testAppFrame;
    SetMemorySystemReady(true);
    Poseidon::Foundation::gSoftAssert = true; // don't break into debugger on non-fatal assertions
    ScopedPoseidonTestPaths testPaths;
    GamePaths::Instance().Initialize("CWR", "ColdWarAssault", GameDirs::ProductName);
    Poseidon::RegisterDummyAudioBackend();
    Poseidon::RegisterTextAudioBackend();
    Poseidon::RegisterOpenALAudioBackend();
    Poseidon::RegisterOpenALVoiceBackend();

    // Initialize ParamFile registration hooks
    InitLibraryElement();
    // Register Poseidon-layer default callback impls — overrides any
    // SIOF clobber from cross-library static-init ordering.  Matches
    // what each production app calls from its own main().
    Poseidon::InitDefaults();
    GEngine = CreateEngineDummy();

    // Create test application to provide GApp and configuration system
    TestApplication testApp;

    int result = Catch::Session().run(argc, argv);

    return result;
}
