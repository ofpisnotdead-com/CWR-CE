#include <Poseidon/Foundation/Platform/VersionNo.h>
#include "GameBase.hpp"
#include <Poseidon/Foundation/Platform/InitBridge.hpp>
#include <Poseidon/Foundation/Platform/PoseidonInit.hpp>
#include <Poseidon/Foundation/Platform/AppConfig.hpp>
#include <Poseidon/Foundation/Platform/GamePaths.hpp>
#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp> // gSoftAssert
#include <Poseidon/Core/Config/Config.hpp>
#include <Poseidon/Dev/Diag/PerfTrace.hpp>
#include <Poseidon/Core/TaskPool.hpp>
#include <Poseidon/Core/CloudSync/CloudSync.hpp>
#include <Poseidon/Core/CloudSync/CloudSyncPaths.hpp>
#include <thread>
#include <chrono>
#include <Poseidon/IO/Streams/QBStream.hpp>          // GUseFileBanks
#include <Poseidon/Foundation/Platform/FPUSetup.hpp> // InitFPU
#include <Poseidon/Foundation/Platform/CrashHandler.hpp>
#include <Poseidon/IO/ParamFile/InitLibraryElement.hpp>
#include <string>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h> // chdir
#endif

namespace Poseidon
{
void ApplyGamePathsToLegacyGlobals();
}

bool GameBase::InitializeMemorySystem()
{
    SetMemorySystemReady(true);

    // Embed version string in binary for identification with tools like 'strings'
    const char* volatile version = "VersionMapID" APP_VERSION_TEXT;
    (void)version;

    // ParamFile registration hooks + Poseidon-layer default callbacks. InitDefaults
    // runs after dynamic init so it wins over any cross-library SIOF clobber.
    InitLibraryElement();
    Poseidon::InitDefaults();

    LOG_DEBUG(Memory, "Memory system initialized");
    return true;
}

bool GameBase::ParseCommandLine(const char* commandLine)
{
    m_commandLine = commandLine;

    if (m_argc > 0 && m_argv)
    {
        AppConfig::Instance().ParseCommandLine(m_argc, m_argv);
    }
#ifdef _WIN32
    else
    {
        // __argc / __argv are MSVC-specific globals available on Windows.
        extern int __argc;
        extern char** __argv;
        AppConfig::Instance().ParseCommandLine(__argc, __argv);
    }
#endif

    // Working directory change (-C) before any path resolution.
    const std::string& workDir = AppConfig::Instance().GetWorkingDirectory();
    if (!workDir.empty())
    {
#ifdef _WIN32
        if (!SetCurrentDirectory(workDir.c_str()))
#else
        if (chdir(workDir.c_str()) != 0)
#endif
        {
            LOG_ERROR(Core, "Failed to change working directory to: {}", workDir);
            m_startupExitCode = 2;
            return false;
        }
    }

    // Logging early (after CLI parsing).
    GetLoggingSystem().InitializeFromConfig(LogDomain());

    if (AppConfig::Instance().HasParseFatalError())
    {
        LOG_ERROR(Core, "Command-line parsing failed: {}", AppConfig::Instance().GetParseFatalError());
        m_startupExitCode = AppConfig::Instance().GetParseFatalExitCode();
        return false;
    }

    // Process-wide TaskPool — owned for the whole process. Audio decode and parallel
    // terrain segment generation both submit to this single instance. Auto-capped at
    // kDefaultMaxTaskThreads (a CWA-era game doesn't scale past it); --max-threads
    // overrides (0 = one per core).
    Poseidon::InitGlobalTaskPool(Poseidon::ResolveTaskPoolThreadCount(AppConfig::Instance().GetMaxThreads(),
                                                                      std::thread::hardware_concurrency()));
    if (auto* pool = Poseidon::GetGlobalTaskPool())
    {
        LOG_INFO(Core, "TaskPool initialised: {} task threads ({} logical cores)", pool->ThreadCount(),
                 std::thread::hardware_concurrency());
    }

    if (const auto& perfTrace = AppConfig::Instance().GetPerfTracePath(); !perfTrace.empty())
    {
        if (Poseidon::Dev::Perf::Trace::Enable(perfTrace.c_str()))
        {
            LOG_INFO(Core, "PerfTrace enabled: writing Chrome trace JSON to {}", perfTrace);
        }
        else
        {
            LOG_ERROR(Core, "PerfTrace: failed to open {} for writing", perfTrace);
        }
    }

    if (!workDir.empty())
        LOG_INFO(Core, "Changed working directory to: {}", workDir);

    const std::string oldPathsRoot = std::filesystem::current_path().string();
    GamePaths::Instance().Initialize("CWR", "ColdWarAssault", "Cold War Assault", AppConfig::Instance().OldPaths(),
                                     oldPathsRoot.c_str());
    Poseidon::ApplyGamePathsToLegacyGlobals();

    // Refresh the crash handler after path setup. Linux uses the user dir for reports; Windows
    // keeps minidumps beside the executable.
    Poseidon::Foundation::InstallCrashHandler(GamePaths::Instance().UserDir().c_str());

    LOG_INFO(Core, "Command-line parsing complete");
    LOG_INFO(Core, "  user_dir:  {}", GamePaths::Instance().UserDir());
    LOG_INFO(Core, "  user_content_dir: {}", GamePaths::Instance().UserContentDir());
    LOG_INFO(Core, "  cache_dir: {}", GamePaths::Instance().CacheDir());
    LOG_INFO(Core, "  temp_dir:  {}", GamePaths::Instance().TempDir());

    // Pull any newer profile/mission state from iCloud before anything reads
    // local disk -- ProfileService::ResolveStartupProfile() runs shortly
    // after this, and local disk is meant to be authoritative, so a fresh
    // launch blocks briefly here rather than risk starting against stale
    // data. No-ops immediately when unavailable (non-Apple, not signed into
    // iCloud, or the entitlement/capability isn't provisioned yet).
    if (Poseidon::CloudSync::IsAvailable())
    {
        LOG_INFO(Core, "CloudSync: pulling from iCloud...");
        Poseidon::SyncWorker worker(Poseidon::CloudSync::MakeAppleSyncOpsEnv());
        worker.Start(Poseidon::CloudSync::DefaultSyncPairs(), Poseidon::SyncDirection::Pull);
        while (worker.Running())
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const Poseidon::SyncSnapshot snap = worker.Poll();
        if (snap.failed)
            LOG_ERROR(Core, "CloudSync: pull failed: {}", snap.error);
        else
            LOG_INFO(Core, "CloudSync: pull complete ({} item(s))", snap.itemCount);
    }

    // Disable caps lock for cheats / dev hotkeys.
    InitCapsLock();
    return true;
}

bool GameBase::ReadConfiguration()
{
    LOG_INFO(Config, "Initializing configuration system...");

    // Config: in-process defaults → CLI overrides.
    AppConfig& appConfig = AppConfig::Instance();
    GetConfig().Initialize(appConfig);

    // Game configuration: language, stringtables, modules, config/resource files.
    const auto& lang = appConfig.GetLanguage();
    const char* language = lang.empty() ? nullptr : lang.c_str();
    if (!GetConfig().InitializeGameConfiguration(language))
    {
        LOG_ERROR(Config, "Failed to initialize game configuration");
        return false;
    }

    LOG_INFO(Config, "Game configuration initialized (stringtables, modules, configs loaded)");

#if CHECK_STRINGTABLE
    extern void CheckStringtable(const char* lang);
    CheckStringtable("English");
#endif

    ApplyConfigurationFlags();

    // MP auto-tests use soft asserts so Debug builds return a process result instead of breaking.
    if (!appConfig.GetMPAssign().empty())
        Poseidon::Foundation::gSoftAssert = true;

    return true;
}

int GameBase::RunBootstrap(int argc, char** argv, const char* commandLine)
{
    s_instance = this;
    RegisterGameModules();

    if (!InitializeMemorySystem())
        return 1;

    RegisterAudioBackends();
    m_argc = argc;
    m_argv = argv;

    if (!ParseCommandLine(commandLine))
        return m_startupExitCode;

    return -1; // continue app-specific boot
}

bool GameBase::InitializeEngineCore()
{
    OnPreEngineInit();

    GUseFileBanks = true;
    ConfigureBankMerge();

    Glob_Init();
    OnGlobInitialized();

    InitMan();
    OnManagersInit();

    Poseidon::Foundation::InitFPU();
    return true;
}
