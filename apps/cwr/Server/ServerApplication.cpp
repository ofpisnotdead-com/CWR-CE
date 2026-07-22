#include <Poseidon/Foundation/Platform/VersionNo.h>
#include "ServerApplication.hpp"
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Audio/AudioFactory.hpp>
#include <Poseidon/Foundation/Platform/InitBridge.hpp>
#include <Poseidon/Foundation/Platform/FPUSetup.hpp>
#include <Poseidon/Foundation/Platform/PoseidonInit.hpp>
#include <Poseidon/Foundation/Platform/AppConfig.hpp>
#include <Poseidon/Foundation/Platform/GamePaths.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Version.hpp>
#include <Poseidon/Core/Config/Config.hpp>
#include <Poseidon/Core/Profile/ProfileManager.hpp>
#include <Poseidon/Core/resrc1.h>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Graphics/Dummy/GraphicsEngineDummy.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/World/Scene/ScenePreloader.hpp>

#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/IO/ParamFile/InitLibraryElement.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_mouse.h>
#include <cjson/cJSON.h>
#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/ansicolor_sink.h>
#include <spdlog/spdlog.h>
#include <stdlib.h>
#include <cstdio>
#include <ctime>
#include <exception>
#include <string>
#include <vector>
#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/GlobalAlive.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/Memtype.h>
#include <Poseidon/Foundation/platform.hpp>

#ifndef _WIN32
#include <csignal>
#include <chrono>
#include <thread>
#include <unistd.h>

namespace Poseidon::Foundation
{
extern bool interrupted;
void handleInt(int sig);
} // namespace Poseidon::Foundation
#endif

using namespace Poseidon;

#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <Poseidon/Dev/Harness/HarnessServer.hpp>
#include <Poseidon/Dev/Harness/HarnessBuiltins.hpp>
#include <Poseidon/Dev/Harness/HarnessMissionStateTracker.hpp>
#include <Poseidon/Dev/Harness/HarnessPlayerTracker.hpp>
#include <cstring>

#include <memory>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <mimalloc.h>

namespace Poseidon
{
extern Scene* GScene;
}
using Poseidon::GScene;

using namespace Poseidon::Dev;
using Poseidon::CreateEngineDummy;
using Poseidon::GEngine;
namespace Poseidon
{
void ApplyGamePathsToLegacyGlobals();
}

extern SoundScene* CreateSoundScene();

extern void CleanupSimulateMission();

extern void DDTerm();
extern bool CreateDedicatedServer(RString config);

// Server-safe Trident verbs (triEndTest / triAssertNgs[Client]) — registered explicitly so the
// Core-only server gets them without the client command module's GL/UI deps. See GameStateExtServerTest.cpp.
extern void RegisterServerTriCommands();
namespace Poseidon
{
extern bool AppServerLoop();
extern RString ServerConfig;
} // namespace Poseidon

extern void PrintServerStats();

ServerApplication::~ServerApplication() {}

#ifdef _WIN32
int ServerApplication::Run(HINSTANCE hInstance, LPSTR commandLine, int showCmd)
{
    m_hInstance = hInstance;
    m_showCmd = showCmd;

    extern int __argc;
    extern char** __argv;
    int rc = RunBootstrap(__argc, __argv, commandLine);
    if (rc >= 0)
        return rc;

    return RunServerStages();
}
#endif

int ServerApplication::Run(const char* commandLine)
{
    return 0;
}

int ServerApplication::Run(int argc, char** argv)
{
    int rc = RunBootstrap(argc, argv, nullptr);
    if (rc >= 0)
        return rc;

    return RunServerStages();
}

void ServerApplication::RegisterAudioBackends()
{
    Poseidon::RegisterDummyAudioBackend();
    Poseidon::RegisterTextAudioBackend();
}

int ServerApplication::RunServerStages()
{
    // In simulate mode, use soft assertions early (before asset loading).
    if (AppConfig::Instance().IsSimulateMode())
        Poseidon::Foundation::gSoftAssert = true;

    if (!ReadConfiguration())
        return 1;

    // Configuration loading can reset engine flags, so set dedicated-server mode after it.
    ENGINE_CONFIG.doCreateDedicatedServer = true;

    if (!InitializeServerEngine())
        return 1;

    if (!CreateDummyEngine())
        return 1;

    if (!CreateServerConsole())
        return 1;

    if (!InitializeSound())
        return 1;

    // World BEFORE CreateDedicatedServer — the network path calls GWorld->Options().
    if (!InitializeServerWorld())
        return 1;

    // Register server-safe Trident test verbs only for explicit dev/test runs.
    const AppConfig& appConfig = AppConfig::Instance();
    if (appConfig.DevMode() || appConfig.GetHarnessPort() >= 0 || !appConfig.GetTestMissionPath().empty())
        RegisterServerTriCommands();

    if (!InitializeSubsystems())
        return 1;

    GLOB_ENGINE->SetTimeStartGame(GlobalTickCount());

    // -check exits before network setup so background threads do not obscure the result.
    if (ENGINE_CONFIG.checkInitAndExit && !AppConfig::Instance().IsSimulateSmokeCheck())
    {
        LOG_INFO(Core, "Server initialization check complete - exiting");
        _exit(0);
    }

    if (AppConfig::Instance().IsSimulateSmokeCheck())
    {
        LOG_INFO(Core, "Simulate smoke check: initialization complete, continuing into mission startup");
    }

    LOG_INFO(Core, "Creating dedicated server with config: '{}'",
             Poseidon::ServerConfig.GetLength() > 0 ? Poseidon::ServerConfig.Data() : "(none)");
    LOG_INFO(Network, "Initializing network subsystem for dedicated server...");

    if (!CreateDedicatedServer(Poseidon::ServerConfig))
    {
        LOG_ERROR(Core, "Failed to create dedicated server");
        return 1;
    }

    LOG_INFO(Core, "Dedicated server created successfully, entering main loop...");

    GApp->m_canRender = true;
    RunMainLoop();
    return 0;
}

bool ServerApplication::InitializeSubsystems()
{
    if (GScene)
        ScenePreloader::Instance().Initialize(*GScene);

    return true;
}

void ServerApplication::RunMainLoop()
{
    DedicatedServerLoop();
}

void ServerApplication::ShutdownSubsystems() {}

bool ServerApplication::InitializeServerEngine()
{
    return InitializeEngineCore();
}

void ServerApplication::OnPreEngineInit()
{
    // Eager commit and reduced decommit keep allocator contention predictable on server threads.
    mi_option_set(mi_option_eager_commit, 1);
    mi_option_set(mi_option_eager_commit_delay, 1);
}

void ServerApplication::ConfigureBankMerge()
{
    ENGINE_CONFIG.gMergeTextures = false;
}

void ServerApplication::OnGlobInitialized()
{
    // Default player name for server (required for GetUserDirectory).
    extern void Glob_SetPlayerName(const RString& name);
    Glob_SetPlayerName(ProfileManager::kServerProfileName);
}

bool ServerApplication::CreateDummyEngine()
{
    GEngine = CreateEngineDummy();

    return GEngine != nullptr;
}

bool ServerApplication::CreateServerConsole()
{
    try
    {
        std::shared_ptr<spdlog::sinks::stdout_color_sink_mt> console_sink;
        console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::info);
        console_sink->set_pattern("[%H:%M:%S] [%^%l%$] %v");

        std::vector<spdlog::sink_ptr> sinks{console_sink};

        try
        {
            auto now = std::chrono::system_clock::now();
            auto tt = std::chrono::system_clock::to_time_t(now);
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &tt);
#else
            localtime_r(&tt, &tm);
#endif
            char timestamp[32];
            std::snprintf(timestamp, sizeof(timestamp), "%04d%02d%02d_%02d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1,
                          tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

            int port = AppConfig::Instance().GetNetworkPort();
            std::string logPath =
                GamePaths::Instance().UserDir() + "server_" + timestamp + "_" + std::to_string(port) + ".log";

            auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath, true);
            file_sink->set_level(spdlog::level::debug);
            file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
            sinks.push_back(file_sink);
            LOG_INFO(Core, "Server log: {}", logPath);
        }
        catch (const spdlog::spdlog_ex& ex)
        {
            LOG_WARN(Core, "File logging not available: {}", ex.what());
        }

        auto server_logger = std::make_shared<spdlog::logger>("Server", sinks.begin(), sinks.end());
        server_logger->set_level(spdlog::level::debug);
        try
        {
            spdlog::register_logger(server_logger);
        }
        catch (const spdlog::spdlog_ex& ex)
        {
            LOG_WARN(Core, "Failed to register Server logger: {}", ex.what());
        }

        // AppServerLoop requires the app to be active before it processes simulation.
        m_appActive = true;

        if (!AppConfig::Instance().NoBanner())
        {
            LOG_INFO(Core, R"BANNER(
     _    ____  __  __    _
    / \  |  _ \|  \/  |  / \
   / _ \ | |_) | |\/| | / _ \
  / ___ \|  _ <| |  | |/ ___ \
 /_/   \_\_| \_\_|  |_/_/   \_\

   C O L D   W A R   A S S A U L T   -   D E D I C A T E D   S E R V E R
   version {}
)BANNER",
                     (const char*)GetVersionString());
        }
        LOG_INFO(Network, "Dedicated server starting: version {}", (const char*)GetVersionString());
    }
    catch (const std::exception& ex)
    {
        LOG_ERROR(Core, "Exception in CreateServerConsole: {}", ex.what());
        return false;
    }
    catch (...)
    {
        LOG_ERROR(Core, "Unknown exception in CreateServerConsole");
        return false;
    }

    return true;
}

bool ServerApplication::InitializeServerWorld()
{
    LOG_DEBUG(Core, "Creating server world...");
    GWorld = CreateWorld(GEngine, false);

    LOG_DEBUG(Core, "Preloading textures for server...");
    GPreloadedTextures_Preload(true);

    // AI tables need addon config data loaded by world creation.
    LOG_DEBUG(Core, "Initializing AI tables...");
    AI_InitTables();
    GStats_ClearAll();

    LOG_DEBUG(Core, "Creating server landscape...");
    GLandscape = CreateLandscape(GEngine, GWorld, false);

    LOG_DEBUG(Core, "Initializing server world landscape...");
    World_InitLandscape(GWorld, GLandscape);
    I_AM_ALIVE();

    LOG_DEBUG(Core, "Preloading vehicle types...");
    VehicleTypes_Preload();
    I_AM_ALIVE();

    LOG_INFO(Core, "Server world initialized successfully");
    GFileServer_FlushBank();

    InitWorld();
    I_AM_ALIVE();

    return true;
}

bool ServerApplication::InitializeSound()
{
    // Server needs sound system for weapon initialization (GetWaveDuration)
    // but uses dummy backend (no actual audio output)
    LOG_DEBUG(Core, "Initializing server sound system (dummy backend)...");

    extern void CleanupSoundSystem();
    CleanupSoundSystem();

    extern IAudioSystem* CreateAudioSystem(void* hwnd, bool noSound, bool isDedicatedServer);
#ifdef _WIN32
    GSoundsys = CreateAudioSystem(GApp->m_hwnd, false, true); // isDedicatedServer=true
#else
    GSoundsys = CreateAudioSystem(nullptr, false, true);
#endif

    if (!GSoundsys)
    {
        LOG_ERROR(Core, "Failed to create sound system");
        return false;
    }

    GSoundScene = CreateSoundScene();
    if (!GSoundScene)
    {
        LOG_ERROR(Core, "Failed to create sound scene");
        return false;
    }

    LOG_INFO(Core, "Server sound system initialized (dummy backend)");
    return true;
}

void ServerApplication::DedicatedServerLoop()
{
    LOG_INFO(Core, "Dedicated server main loop started");

    int stat1Time = GlobalTickCount();
    (void)stat1Time; // Reserved for stats reporting

    auto& appConfig = AppConfig::Instance();
    bool simulateMode = appConfig.IsSimulateMode();
    int durationSec = appConfig.GetSimulateDuration();
    DWORD startTick = GlobalTickCount();

    // Harness server for external test orchestration (Trident).
    // Server advertises only the queries that make sense headless (no
    // screenshot, no UI interaction, no SQF eval — there's no client
    // scene or game state on the dedicated side).
    std::unique_ptr<HarnessServer> harnessServer;
    int harnessPort = appConfig.GetHarnessPort();
    if (harnessPort >= 0)
    {
        harnessServer = std::make_unique<HarnessServer>();
        if (!harnessServer->Start(harnessPort))
        {
            LOG_ERROR(Core, "Failed to start harness server on port {}", harnessPort);
            harnessServer.reset();
        }
        else
        {
            harnessServer->RegisterCommand(
                {"query",
                 "Query server state (what: players, mission, ngs, connections, master_server_server_detail, "
                 "master_server_mod_detail, master_server_mod_versions, master_server_mod_servers)",
                 {{"what", "string", true}}},
                [](const std::string&, cJSON* root) -> std::string
                {
                    std::string resp = HarnessBuiltins::AnswerNetworkQuery(HarnessProtocol::GetString(root, "what"));
                    if (resp.empty())
                        resp = HarnessBuiltins::AnswerServiceQuery(HarnessProtocol::GetString(root, "what"), root);
                    return resp.empty() ? HarnessProtocol::ErrorResponse("unknown query target") : resp;
                });

            // SQF eval/exec — the dedicated server has a world + GameState, so server-side test
            // verbs (triAssertNgs, triEndTest) run here just as on the client.
            HarnessBuiltins::RegisterSqf(*harnessServer);

            harnessServer->RegisterEvent({"player_joined", "Player connected", {{"dpid", "int"}, {"name", "string"}}});
            harnessServer->RegisterEvent({"player_left", "Player disconnected", {{"dpid", "int"}, {"name", "string"}}});
            harnessServer->RegisterEvent(
                {"mission_state", "Server game state transition", {{"state", "string"}, {"prev", "string"}}});
        }
    }
    HarnessPlayerTracker harnessPlayerTracker;
    HarnessMissionStateTracker harnessMissionStateTracker;

#ifndef _WIN32
    signal(SIGINT, Poseidon::Foundation::handleInt);
#endif

    while (true)
    {
        Poseidon::AppServerLoop();

        if (harnessServer)
        {
            harnessPlayerTracker.Poll(*harnessServer);
            harnessMissionStateTracker.Poll(*harnessServer);
        }

        if (harnessServer)
        {
            if (harnessServer->IsExitRequested())
                m_closeRequest = true;

            HarnessCommand cmd;
            while (harnessServer->PopCommand(cmd))
                harnessServer->ProcessCommand(cmd);
        }

        if (simulateMode && durationSec > 0)
        {
            DWORD elapsed = GlobalTickCount() - startTick;
            if (elapsed >= static_cast<DWORD>(durationSec * 1000))
            {
                LOG_INFO(Mission, "[timeout] Duration {}s exceeded", durationSec);
                GetNetworkManager().Close(); // notify clients before exit
                CleanupSimulateMission();
                Sleep(100);
                _exit(2);
            }
        }

        // Clean exit on close request — simulate-mode endGame/debriefing, or a harness
        // "exit"/triEndTest (IsExitRequested -> m_closeRequest above). Honouring it in
        // non-simulate mode too lets the Trident runner shut the server down at test end.
        if (m_closeRequest)
        {
            LOG_INFO(Core, "Clean exit (code {})", m_exitCode);
            GetNetworkManager().Close(); // notify clients before exit
            if (simulateMode)
                CleanupSimulateMission();
            Sleep(100);
            _exit(m_exitCode);
        }

#ifndef _WIN32
        {
            if (Poseidon::Foundation::interrupted)
                GApp->m_validateQuit = true;
        }
#endif

        if (GApp->m_validateQuit)
        {
#if PROFILE_EXIT
            EnableProfiler();
#endif
            DDTerm();
#if PROFILE_EXIT
            DisableProfiler();
#endif
            if (ENGINE_CONFIG.hideCursor)
                SDL_ShowCursor();
            int ret = 0;
            LOG_INFO(Core, "Exit {}", ret);
            Sleep(500);
            exit(ret);
        }

        Sleep(1); // yield
    }
}
