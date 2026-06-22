#include <Poseidon/Foundation/Platform/AppConfig.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/BuildInfo.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/ModCollection.hpp>
#include <Poseidon/Core/ModSystem.hpp>
#include <Poseidon/Audio/AudioFactory.hpp>
#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <Poseidon/Foundation/Common/PlatformPaths.hpp>
#include <Poseidon/Foundation/Platform/InitBridge.hpp> // For wrapper functions
#include <Poseidon/Network/NetworkConfig.hpp>
#include <Poseidon/IO/Filesystem/FileOps.hpp>
#include <filesystem>
#include <stdio.h>
#include <string.h>
#include <CLI/Error.hpp>
#include <CLI/Option.hpp>
#include <CLI/Validators.hpp>
#include <algorithm>
#include <exception>
#include <system_error>
#include <Poseidon/Foundation/Strings/RString.hpp>

#ifdef _WIN32
#include <windows.h> // For Sleep
#endif

// Note: CLI11 has static initialization that allocates memory
// This happens before WinMain, so we need to ensure memory system tolerates early allocation
#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>

#include <cctype>
#include <cstdlib>
#include <sstream>
#include <vector>

// Forward declarations for Poseidon-namespaced globals defined in Core/
namespace Poseidon
{
extern RString ServerConfig;
extern void SetPidFileName(const RString& filename);
extern void SetRankingLog(const RString& log);
#ifdef _WIN32
extern bool NoTextures;
#endif
} // namespace Poseidon

// Global-namespace variables referenced from ApplyToLegacyGlobals()
extern bool Benchmark;
extern bool GLogFileOps;
extern bool AutoTest;
extern char LoadFile[512];
extern RString& GetModPathInternal();
#ifdef NET_LOG_COMMAND_LINE
extern bool netLogValid;
#endif

namespace Poseidon::Foundation
{

namespace
{
enum class CliHelpVisibility
{
    Basic,
    Full,
    Dev,
};

enum class CliHelpMode
{
    Basic,
    Full,
    Dev,
};

enum class CliAppRole
{
    Game,
    Server,
};

std::vector<std::string> NormalizeLegacyArguments(int argc, char** argv)
{
    std::vector<std::string> args;
    args.reserve(argc);
    for (int i = 0; i < argc; ++i)
    {
        std::string arg = argv[i] ? argv[i] : "";
        if (arg == "-nosplash")
            arg = "--no-splash";
        else if (arg == "-nomap")
            arg = "--nomap";
        else if (arg == "-oldpaths")
            arg = "--oldpaths";
        else if (arg == "-mod")
            arg = "--mod";
        else if (arg.rfind("-mod=", 0) == 0)
            arg = "--mod=" + arg.substr(5);
        args.emplace_back(std::move(arg));
    }
    return args;
}

std::string ToLowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

CliAppRole DetectAppRole(const std::vector<std::string>& args)
{
    if (args.empty())
        return CliAppRole::Game;

    const std::string exeName = ToLowerAscii(std::filesystem::path(args[0]).filename().string());
    if (exeName.find("server") != std::string::npos)
        return CliAppRole::Server;
    return CliAppRole::Game;
}

bool IsCliArg(const std::string& arg, const char* name)
{
    if (arg == name)
        return true;

#ifdef _WIN32
    if (name[0] == '-' && name[1] == '-' && arg.size() == strlen(name) - 1 && arg[0] == '/')
        return _stricmp(arg.c_str() + 1, name + 2) == 0;
#endif

    return false;
}

CliHelpMode DetectHelpMode(const std::vector<std::string>& args)
{
    bool helpRequested = false;
    bool fullHelpRequested = false;
    bool devRequested = false;

    for (const std::string& arg : args)
    {
        if (IsCliArg(arg, "--help"))
            helpRequested = true;
        else if (IsCliArg(arg, "--help-full"))
            fullHelpRequested = true;
        else if (IsCliArg(arg, "--dev"))
            devRequested = true;
    }

    if ((helpRequested || fullHelpRequested) && devRequested)
        return CliHelpMode::Dev;
    if (fullHelpRequested)
        return CliHelpMode::Full;
    return CliHelpMode::Basic;
}

bool ContainsCliArg(const std::vector<std::string>& args, const char* name)
{
    return std::any_of(args.begin(), args.end(), [name](const std::string& arg) { return IsCliArg(arg, name); });
}

bool IsVisibleInHelp(CliHelpVisibility visibility, CliHelpMode mode)
{
    switch (visibility)
    {
        case CliHelpVisibility::Basic:
            return true;
        case CliHelpVisibility::Full:
            return mode == CliHelpMode::Full || mode == CliHelpMode::Dev;
        case CliHelpVisibility::Dev:
            return mode == CliHelpMode::Dev;
    }
    return true;
}

#ifdef _WIN32
void WriteCliText(DWORD stdHandle, FILE* stream, const std::string& text)
{
    HANDLE handle = GetStdHandle(stdHandle);
    DWORD written = 0;
    DWORD mode = 0;
    if (handle != INVALID_HANDLE_VALUE && handle != nullptr)
    {
        if (GetConsoleMode(handle, &mode))
        {
            if (WriteConsoleA(handle, text.c_str(), static_cast<DWORD>(text.size()), &written, nullptr))
                return;
        }
        else if (WriteFile(handle, text.c_str(), static_cast<DWORD>(text.size()), &written, nullptr))
        {
            return;
        }
    }

    fwrite(text.data(), 1, text.size(), stream);
    fflush(stream);
}
#endif

std::string ResolveCliManagedModsDir()
{
    const char* modsEnv = std::getenv("POSEIDON_MODS_DIR");
    if (modsEnv && modsEnv[0] != '\0')
        return modsEnv;

    if (GamePaths::Instance().IsInitialized() && !GamePaths::Instance().ModsDir().empty())
        return GamePaths::Instance().ModsDir();

    const char* contentEnv = std::getenv("POSEIDON_USER_CONTENT_DIR");
    if (contentEnv && contentEnv[0] != '\0')
        return (std::filesystem::path(contentEnv) / "Mods").string();

    const char* userDirEnv = std::getenv("POSEIDON_USER_DIR");
    if (userDirEnv && userDirEnv[0] != '\0')
        return (std::filesystem::path(userDirEnv) / "content" / "Mods").string();

    return (std::filesystem::path(getUserDocumentsDir("Cold War Assault")) / "Mods").string();
}
} // namespace

AppConfig& AppConfig::Instance()
{
    static AppConfig instance;
    return instance;
}

AppConfig::AppConfig() {}

void AppConfig::ParseCommandLine(int argc, char** argv)
{
    if (_parsed)
    {
        return;
    }
    _parsed = true;

    // Console attachment is handled by entry points (WinMain / main)
    // via attachParentConsole() before Application::Run()

    try
    {
        std::vector<std::string> normalizedArgs = NormalizeLegacyArguments(argc, argv);
        if (BuildInfo::ReleaseBuild && ContainsCliArg(normalizedArgs, "--dev"))
        {
            _parseFatalError = "--dev is not supported in release builds";
            _parseFatalExitCode = 2;
            return;
        }

        const CliHelpMode helpMode = DetectHelpMode(normalizedArgs);
        const CliAppRole appRole = DetectAppRole(normalizedArgs);
        const bool serverRole = appRole == CliAppRole::Server;

        CLI::App app{serverRole ? "Arma: Cold War Assault - Remastered Dedicated Server"
                                : "Arma: Cold War Assault - Remastered"};

        // Disable default help flag so we can reuse -h for --height
        app.set_help_flag("--help,--help-full", "Print help and exit");
        if (serverRole)
            app.usage("[OPTIONS]");
        if (BuildInfo::ReleaseBuild)
            app.footer(serverRole ? "Use --help-full for advanced server options."
                                  : "Use --help-full for advanced user options.");
        else
            app.footer(
                serverRole
                    ? "Use --help-full for advanced server options. Use --help --dev for developer and test options."
                    : "Use --help-full for advanced user options. Use --help --dev for developer and test options.");

        app.allow_windows_style_options();

        // Tolerate unknown options — app-specific CLI (e.g. PoseidonUITest's
        // --start-display / --dump-ui / --record) parses the same argv after
        // AppConfig and picks up what isn't recognized here.
        app.allow_extras();

        auto showGroup = [helpMode](CLI::App* group, CliHelpVisibility visibility, bool roleVisible = true) -> CLI::App*
        {
            if (!roleVisible || !IsVisibleInHelp(visibility, helpMode))
                group->group("");
            return group;
        };

        auto showOption = [helpMode](CLI::Option* option, CliHelpVisibility visibility,
                                     bool roleVisible = true) -> CLI::Option*
        {
            if (!roleVisible || !IsVisibleInHelp(visibility, helpMode))
                option->group("");
            return option;
        };

        auto* displayGroup = app.add_option_group("Display & Graphics", "Window, rendering, and visual options");
        showGroup(displayGroup, CliHelpVisibility::Basic, !serverRole);

        auto* windowOpt =
            displayGroup->add_flag("--window,!--fullscreen", _windowMode, "Run in windowed mode instead of fullscreen");

        auto* displayModeOpt =
            displayGroup
                ->add_option("--display-mode", _displayMode, "Display mode: windowed, borderless (default), exclusive")
                ->check(CLI::IsMember({"windowed", "borderless", "exclusive"}));

        auto* widthOpt = displayGroup->add_option("--width,-w", _windowWidth, "Window width in pixels")
                             ->check(CLI::Range(320, 7680));

        auto* heightOpt = displayGroup->add_option("--height,-h", _windowHeight, "Window height in pixels")
                              ->check(CLI::Range(240, 4320));

        bool showSplash = true;
        displayGroup->add_flag("--splash,!--no-splash", showSplash, "Show splash screens on startup");
        bool showMenuScene = true;
        displayGroup->add_flag("--menu-scene,!--no-menu-scene", showMenuScene, "Show 3D background scene in menu");

        showOption(
            displayGroup->add_option("--render", _renderBackend, "Graphics backend: dummy, gl33, auto (default: gl33)")
                ->check(CLI::IsMember({"dummy", "gl33", "auto"})),
            CliHelpVisibility::Full);

        showOption(displayGroup->add_flag("--tl,--hw-tl", _enableHWTL,
                                          "Enable hardware transform & lighting (T&L, default on)"),
                   CliHelpVisibility::Full);

        showOption(
            displayGroup->add_flag("--sw-tl,--no-hw-tl", _disableHWTL, "Disable hardware T&L, use software path"),
            CliHelpVisibility::Full);

        showOption(displayGroup->add_flag("--twomon,--dual-monitor", _dualMonitor,
                                          "Dual monitor mode (legacy flag, no effect)"),
                   CliHelpVisibility::Full);

        displayGroup->add_flag("--fps,--show-fps", _showFps, "Show FPS overlay on screen");

        showOption(displayGroup->add_flag("--notex,--no-textures", _noTextures, "Disable textures (debug mode)"),
                   CliHelpVisibility::Dev);

        displayGroup->add_flag("--no-mouse-grab", _noMouseGrab, "Disable mouse grab (cursor confinement and hiding)");

        showOption(
            displayGroup->add_flag("--old-fonts,--no-freetype", _noFreeType, "Use bitmap fonts instead of FreeType"),
            CliHelpVisibility::Full);

        auto* multiplayerGroup = app.add_option_group("Multiplayer", "Network and server options");

        showOption(multiplayerGroup->add_flag("--host", _createServer, "Create a multiplayer host server"),
                   CliHelpVisibility::Basic, !serverRole);

        // The dedicated server is its own binary (PoseidonServer); the game client is not
        // a server, so there is no --server flag here.

        std::string serverConfigStr;
        showOption(multiplayerGroup->add_option("--config", serverConfigStr, "Server configuration file path"),
                   serverRole ? CliHelpVisibility::Basic : CliHelpVisibility::Full);

        std::string pidFileStr;
        showOption(multiplayerGroup->add_option("--pid", pidFileStr,
                                                "PID file path (for dedicated server process management)"),
                   serverRole ? CliHelpVisibility::Basic : CliHelpVisibility::Full, serverRole);

        std::string connectIPStr;
        showOption(multiplayerGroup->add_option("--connect", connectIPStr, "IP address to connect to"),
                   CliHelpVisibility::Basic, !serverRole);

        std::string masterServerStr;
        showOption(multiplayerGroup->add_option("--master-server", masterServerStr,
                                                "Master server / server directory host override"),
                   serverRole ? CliHelpVisibility::Basic : CliHelpVisibility::Full);
        multiplayerGroup->add_flag("--private,--lan", _privateServer,
                                   "Disable master-server publishing/listing for LAN/private servers");

        multiplayerGroup->add_option("--port", _networkPort, "Network port (default: 1985)")
            ->check(CLI::Range(1, 65535));

        showOption(multiplayerGroup
                       ->add_option("--connect-port", _connectPort,
                                    "Remote connect port (0 = use --port, for proxied topologies)")
                       ->check(CLI::Range(0, 65535)),
                   CliHelpVisibility::Full, !serverRole);

        std::string advertiseAddressStr;
        showOption(multiplayerGroup->add_option("--advertise-address", advertiseAddressStr,
                                                "Public address advertised to the master server"),
                   serverRole ? CliHelpVisibility::Basic : CliHelpVisibility::Full, serverRole);

        std::string passwordStr;
        multiplayerGroup->add_option("--password", passwordStr, "Server password");

        std::string playerNameStr;
        showOption(multiplayerGroup->add_option("--name", playerNameStr, "Player name"), CliHelpVisibility::Basic,
                   !serverRole);

        std::string rankingLogStr;
        showOption(
            multiplayerGroup->add_option("--ranking", rankingLogStr, "Legacy ranking upload endpoint (host:port)"),
            CliHelpVisibility::Full, serverRole);

        showOption(multiplayerGroup
                       ->add_option("--mp-auto-start", _mpAutoStart,
                                    "Auto-start mission when N human players are ready (server, use with --simulate)")
                       ->check(CLI::Range(0, 64)),
                   CliHelpVisibility::Dev);
        showOption(multiplayerGroup->add_option("--mp-assign", _mpAssign,
                                                "Auto-assign to SIDE:SLOT (e.g. WEST:1, EAST:2, RES:1, CIV:3)"),
                   CliHelpVisibility::Dev);
        showOption(multiplayerGroup->add_flag("--force-jip", _forceJIP,
                                              "Force Join In Progress on all missions (server, for testing)"),
                   CliHelpVisibility::Dev);

        bool showBanner = true;
        showOption(multiplayerGroup->add_flag("--banner,!--no-banner", showBanner,
                                              "Show the ASCII-art banner on dedicated server startup"),
                   serverRole ? CliHelpVisibility::Basic : CliHelpVisibility::Full, serverRole);

        auto* audioGroup = app.add_option_group("Audio", "Sound and audio backend options");
        showGroup(audioGroup, CliHelpVisibility::Basic, !serverRole);

        audioGroup->add_flag("--nosound,--no-sound", _noSound, "Disable all sound");

        const std::string audioHelp = "Audio backend (auto, " + AudioFactory::DescribeRegisteredCodes() + ")";
        audioGroup->add_option("--audio", _audioBackend, audioHelp);

        auto* configGroup = app.add_option_group("Configuration", "Config files and mods");

        std::string modPathsStr;
        configGroup->add_option("--mod", modPathsStr, "Mod directory paths (semicolon-separated; legacy alias: -mod)");

        // Optional base directory for relative --mod names. Lets mods be referenced
        // by name (e.g. --mods-dir packages/mods --mod @fixturemod) so test/CI
        // invocations carry no machine-specific absolute path.
        std::string modsDirStr;
        configGroup->add_option("--mods-dir", modsDirStr,
                                "Base directory for relative --mod names + the local mods the MODS screen scans");

        // Where the MODS screen scans for / installs downloaded (workshop) mods.
        // Separate from --mods-dir so a mod's source is preserved by its folder.
        std::string workshopDirStr;
        showOption(configGroup->add_option("--workshop-dir", workshopDirStr,
                                           "Directory for downloaded (workshop) mods (default <UserContent>/Workshop)"),
                   CliHelpVisibility::Basic);

        showOption(configGroup->add_option("--lang", _language,
                                           "Language override (English, French, German, Czech, Polish, Russian, etc.)"),
                   CliHelpVisibility::Basic);
        showOption(configGroup->add_flag("--encryption-required", _requireEncryptedAddons,
                                         "Require addon banks to declare encryption metadata"),
                   serverRole ? CliHelpVisibility::Basic : CliHelpVisibility::Full);

        configGroup
            ->add_option("--work-dir,-C", _workingDirectory,
                         "Working directory (game data directory with DTA/, Worlds/, etc.)")
            ->check(CLI::ExistingDirectory);

        auto* legacyGroup = app.add_option_group("Legacy Compatibility",
                                                 "Accepted OFP/CWA-style aliases: -nosplash, -mod, -nomap, -oldpaths");

        legacyGroup->add_flag("--oldpaths", _oldPaths,
                              "Use game-folder paths for profiles, config, Mods, Missions, and MPMissions "
                              "(legacy alias: -oldpaths)");

        showOption(legacyGroup->add_flag("--nomap", _noMap,
                                         "Accept the legacy no-map flag (legacy alias: -nomap; currently no effect)"),
                   CliHelpVisibility::Dev);

        auto* initGroup = app.add_option_group("Initialization", "Startup and initialization options");
        showGroup(initGroup, serverRole ? CliHelpVisibility::Dev : CliHelpVisibility::Full);

        showOption(initGroup->add_flag("--check", _checkInit, "Initialize subsystems and exit (for smoke tests)"),
                   CliHelpVisibility::Dev);

        showOption(initGroup->add_flag("--remount-selftest", _remountSelfTest,
                                       "Boot, perform one in-process re-mount, then exit (re-mount smoke test)"),
                   CliHelpVisibility::Dev);

        showOption(initGroup->add_flag("--mod-cycle-selftest", _modCycleSelfTest,
                                       "Boot, then re-mount bare -> with --mod -> bare, asserting the mod's banks "
                                       "mount and fully unmount, then exit (mod load/unload smoke test)"),
                   CliHelpVisibility::Dev);

        showOption(initGroup->add_flag("--reload-clean-selftest", _reloadCleanSelfTest,
                                       "Boot, re-mount several times, and assert the loaded-content metrics (shape "
                                       "cache, vehicle types, heap) return to a stable baseline each reload, then exit "
                                       "(reload-leaves-clean-state smoke test)"),
                   CliHelpVisibility::Dev);

        showOption(initGroup->add_flag("--remount-sim-selftest", _remountSimSelfTest,
                                       "Boot, run the menu intro for a while, re-mount, then keep pumping real "
                                       "World::Simulate frames on the re-mounted world (the path interactive reload "
                                       "actually exercises), asserting the sensor list survives, then exit"),
                   CliHelpVisibility::Dev);

        showOption(initGroup->add_flag("--remount-fail-selftest", _remountFailSelfTest,
                                       "Boot, run the intro, then force a failed re-mount (as a bad / unsupported mod "
                                       "would) and keep pumping frames - asserts the failed re-mount rolls back to a "
                                       "live, rendering world instead of crashing on freed terrain, then exit"),
                   CliHelpVisibility::Dev);

        showOption(initGroup->add_flag("--error-resilience-selftest", _errorResilienceSelfTest,
                                       "Boot, raise a simulated critical error (ErrorMessage), and assert the process "
                                       "survives instead of exit(1) - proves --no-strict keeps the player build alive "
                                       "(run with --no-strict); under --strict the error stays fatal"),
                   CliHelpVisibility::Dev);

        showOption(initGroup->add_flag("--audit-cfgvehicles-models", _auditCfgVehiclesModels,
                                       "Log ERROR for every editor-visible CfgVehicles class whose model is missing "
                                       "after mounted addon/config resolution; use with --strict to fail CI after the "
                                       "full audit is logged"),
                   CliHelpVisibility::Dev);

        showOption(initGroup->add_flag("--focus", _keepFocus, "Keep focus when window loses focus"),
                   CliHelpVisibility::Full, !serverRole);

        showOption(initGroup->add_flag("--strict,!--no-strict", _strict,
                                       "Treat any ERROR-level log (incl. SQF/SQS script errors) as fatal: request a "
                                       "clean shutdown with a non-zero exit code. Default on in Debug/RelWithDebInfo "
                                       "builds"),
                   CliHelpVisibility::Dev);

        showOption(initGroup->add_flag("--noland,--no-landscape", _noLandscape, "Disable landscape rendering"),
                   CliHelpVisibility::Dev);
        showOption(initGroup->add_flag("--no-terrain-cache", _noTerrainCache,
                                       "Disable terrain segment caching (regenerate every frame)"),
                   CliHelpVisibility::Dev);
        showOption(initGroup->add_flag("--render-frame-log", _renderFrameLog,
                                       "Log per-frame render-frame stats (pass/draw counts) every ~60 frames"),
                   CliHelpVisibility::Dev);

        showOption(initGroup->add_flag("--piii", _enablePIII, "Enable Pentium III optimizations (flush-to-zero mode)"),
                   CliHelpVisibility::Dev);

        auto* perfGroup = app.add_option_group("Performance", "Memory and CPU resource limits");
        showGroup(perfGroup, serverRole ? CliHelpVisibility::Basic : CliHelpVisibility::Full);

        perfGroup
            ->add_option("--maxmem", _maxMemMB,
                         "Process memory hard cap in MB (soft = 75% of it). Overrides the auto/config "
                         "limit. 0 = leave to config/auto.")
            ->check(CLI::Range(0, 1024 * 1024));

        perfGroup
            ->add_option("--max-threads", _maxThreads,
                         "TaskPool worker cap. Default auto = capped at 8; 0 = uncapped (one per "
                         "core); >0 = explicit count.")
            ->check(CLI::Range(0, 1024));

        auto* debugGroup = app.add_option_group("Debug & Testing", "Development and testing options");

        showOption(debugGroup->add_flag("--benchmark", _benchmark, "Benchmark mode"), CliHelpVisibility::Dev);

        if (!BuildInfo::ReleaseBuild)
        {
            debugGroup->add_flag(
                "--dev", _devMode,
                serverRole ? "Enable developer and test-oriented command-line options"
                           : "Enable the dev panel (Ctrl+` toggles; Cheats/Game/Console/Profile/Memory/Font tabs)");
        }

        showOption(debugGroup->add_option("--vd", _viewDistanceOverride, "Override view distance (bypass 5000 clamp)")
                       ->check(CLI::Range(100.0f, 100000.0f)),
                   CliHelpVisibility::Dev);

        showOption(debugGroup->add_flag("--logfiles,--log-files", _logFileOps, "Enable file operation logging"),
                   CliHelpVisibility::Dev);

        showOption(debugGroup->add_flag("--netlog,--net-log", _netLog, "Enable network logging"),
                   CliHelpVisibility::Dev);
        showOption(debugGroup->add_flag("--write-mpreport,--after-action-report", _writeMPReport,
                                        "Write MP after-action report to a timestamped file in UserDir"),
                   CliHelpVisibility::Dev);

        showOption(debugGroup->add_flag("--autotest,--auto-test", _autoTest, "Auto test mode"), CliHelpVisibility::Dev);

        showOption(
            debugGroup
                ->add_option("--confirm-revert-timeout", _confirmRevertTimeoutSeconds,
                             "Override the Display-confirm-revert modal timeout in seconds (0 = engine default "
                             "15s). Lets integration tests exercise the auto-revert path without paying the full "
                             "real-time wait.")
                ->check(CLI::Range(0.0f, 60.0f)),
            CliHelpVisibility::Dev);

        showOption(debugGroup
                       ->add_option("--timeout", _appTimeoutSeconds,
                                    "Auto-exit after N seconds (0 = disabled, useful for app smoke runs).")
                       ->check(CLI::Range(0.0f, 3600.0f)),
                   CliHelpVisibility::Dev);

        showOption(debugGroup->add_option("--auto-keys", _autoKeys,
                                          "Inject key events at specific frames (format: "
                                          "frame:scancode,frame:scancode,...)"),
                   CliHelpVisibility::Dev);
        showOption(debugGroup->add_option("--auto-screenshot", _autoScreenshot,
                                          "Capture screenshot at frame and exit (format: frame:path)"),
                   CliHelpVisibility::Dev);
        showOption(debugGroup->add_option("--rdc-trigger", _rdcTrigger,
                                          "RenderDoc trigger: capture next swap when frame/time is reached "
                                          "(format: '60' = frame 60, '2s' = 2 seconds elapsed). No-op unless game "
                                          "launched from RenderDoc UI."),
                   CliHelpVisibility::Dev);
        showOption(debugGroup->add_option("--screenshot-delay", _screenshotDelay,
                                          "Gameplay frames to wait before screenshot capture (default: 10)"),
                   CliHelpVisibility::Dev);
        showOption(debugGroup->add_option("--ui-test", _uiTest, "Run UI test scenario and exit (e.g., 'exit')"),
                   CliHelpVisibility::Dev);

        showOption(debugGroup
                       ->add_option("--harness", _harnessPort,
                                    "Enable TCP harness server (0 = auto-assign port, >0 = explicit port)")
                       ->check(CLI::Range(0, 65535)),
                   CliHelpVisibility::Dev);

        showOption(debugGroup->add_option("--test-mission,--test", _testMissionPath,
                                          "Run mission folder or mission.sqm directly and exit"),
                   CliHelpVisibility::Dev);

        showOption(
            debugGroup
                ->add_option("--test-type", _testType, "Test type: autotest (default) or screenshot (capture and exit)")
                ->check(CLI::IsMember({"autotest", "screenshot"})),
            CliHelpVisibility::Dev);

        showOption(
            debugGroup
                ->add_option("--simulate", _simulateMissionPath, "Headless mission simulation with given mission path")
                ->check(CLI::ExistingPath),
            CliHelpVisibility::Dev);

        showOption(
            debugGroup
                ->add_option("--duration", _simulateDuration, "Simulation duration in seconds (0 = until endGame)")
                ->check(CLI::NonNegativeNumber),
            CliHelpVisibility::Dev);

        showOption(debugGroup->add_option("--stats", _statsInterval, "Log world stats every N seconds (simulate mode)")
                       ->check(CLI::PositiveNumber),
                   CliHelpVisibility::Dev);

        showOption(debugGroup
                       ->add_option("--time-scale", _timeScale,
                                    "Time acceleration multiplier for simulate mode (1-16, default 1)")
                       ->check(CLI::Range(1, 16)),
                   CliHelpVisibility::Dev);

        auto* loggingGroup = app.add_option_group("Logging", "Log output and verbosity options");
        showGroup(loggingGroup, serverRole ? CliHelpVisibility::Basic : CliHelpVisibility::Full);

        loggingGroup
            ->add_option("--log-level", _logLevel,
                         "Log level: trace, debug, info (default), warn, error, critical, off")
            ->check(CLI::IsMember({"trace", "debug", "info", "warn", "warning", "error", "err", "critical", "off"}));

        showOption(loggingGroup->add_option("--log-categories", _logCategories,
                                            "Filter log output by categories (comma-separated): CORE,CONFIG,MEMORY,"
                                            "NETWORK. Empty = all categories."),
                   serverRole ? CliHelpVisibility::Basic : CliHelpVisibility::Full);
        showOption(
            loggingGroup->add_flag("--legacy-logs", _legacyLogs, "Show verbose legacy bridge logs (hidden by default)"),
            CliHelpVisibility::Full);

        showOption(
            loggingGroup
                ->add_option("--log-format", _logFormat,
                             "Log output format: text (default, human-readable), jsonl (one JSON object per line)")
                ->check(CLI::IsMember({"text", "jsonl"})),
            serverRole ? CliHelpVisibility::Basic : CliHelpVisibility::Full);
        showOption(loggingGroup->add_option("--app-tag", _appTag,
                                            "App identifier prefix in log lines (e.g. 'server', 'game-01'). "
                                            "Max 10 chars, padded for alignment."),
                   serverRole ? CliHelpVisibility::Basic : CliHelpVisibility::Full);
        loggingGroup->add_option("--log-file", _logFile, "Write log output to a file in addition to console");
        showOption(loggingGroup->add_option("--perf-trace", _perfTracePath,
                                            "Write newline-delimited JSON of every ScopedTimer event to this path. "
                                            "Reads in DuckDB via read_json_auto for tabular analysis; convert to "
                                            "Chrome trace via scripts/perf-analyze.ps1 -ToChromeTrace to load in "
                                            "https://ui.perfetto.dev/."),
                   CliHelpVisibility::Dev);

        // Viewer mode (single-model preview, modder/artist tool).
        // Loads one P3D + RTM with the live game render path. Skips
        // splash and main menu. Mouse-orbit camera, animation scrubber,
        // F5 hot-reload of textures, on-screen help (toggle with `?`).
        // Reusable for the menu laptop, campaign book, mission book,
        // briefing notebook — same code, different --model / --anim.
        auto* viewerGroup = app.add_option_group("Viewer", "Standalone model + animation viewer");
        showGroup(viewerGroup, CliHelpVisibility::Full, !serverRole);
        viewerGroup->add_flag("--viewer", _viewerMode, "Run in viewer mode (skip splash + menu, load --model/--anim)");
        viewerGroup->add_option("--model,-m", _viewerModelPath, "P3D model file to load")->check(CLI::ExistingFile);
        viewerGroup->add_option("--anim,-a", _viewerAnimPath, "RTM animation file to bind to the model")
            ->check(CLI::ExistingFile);
        viewerGroup->add_option("--anim-speed", _viewerAnimSpeed, "Animation playback speed (1.0 = real-time)")
            ->check(CLI::Range(0.0f, 10.0f));
        viewerGroup->add_option("--anim-loop", _viewerAnimLoop, "Animation loop mode")
            ->check(CLI::IsMember({"none", "repeat", "ping-pong"}));
        viewerGroup->add_flag("--loose-textures", _looseTextures,
                              "Accept .png / .tga next to expected .paa (auto-on under --viewer)");
        viewerGroup->add_flag("--no-help", _viewerNoHelp, "Start with the on-screen help overlay hidden");

        // --screenshot is shared between viewer mode (capture-and-exit
        // for the kit script) and --test-mission screenshot test type.
        // Lives at app level, not in either subgroup.
        showOption(app.add_option("--screenshot,-s", _screenshotPath, "Capture a screenshot to this path then exit"),
                   CliHelpVisibility::Full, !serverRole);

        // Positional mission arg — unchecked so an unknown option value
        // (e.g. app-specific --start-display NAME) doesn't trip the
        // CLI::ExistingFile validator and fail the whole parse.
        showOption(app.add_option("mission", _missionFile, "Mission file to load (.pbo or .sqm)"),
                   CliHelpVisibility::Basic, !serverRole);

        try
        {
            std::vector<const char*> normalizedArgv;
            normalizedArgv.reserve(normalizedArgs.size());
            for (const std::string& arg : normalizedArgs)
                normalizedArgv.push_back(arg.c_str());

            app.parse(static_cast<int>(normalizedArgv.size()), normalizedArgv.data());

            // Track which options were explicitly provided
            _windowFlagExplicit = windowOpt->count() > 0;
            _widthExplicit = widthOpt->count() > 0;
            _heightExplicit = heightOpt->count() > 0;
            _displayModeExplicit = displayModeOpt->count() > 0;

            // --window flag overrides display mode
            if (_windowMode && !_displayModeExplicit)
                _displayMode = "windowed";
            // --display-mode overrides --window flag
            if (_displayModeExplicit)
                _windowMode = (_displayMode == "windowed");

            // Apply boolean inversions after parse
            _noSplash = !showSplash;
            _noBanner = !showBanner;
            _noMenuScene = !showMenuScene;

            // --simulate <path> implies simulate mode + test mission
            if (!_simulateMissionPath.empty())
            {
                _simulateMode = true;
                // Make path absolute before any chdir (-C) changes the CWD
                _testMissionPath = std::filesystem::absolute(_simulateMissionPath).string();
            }

            // Make test-mission path absolute before any chdir (-C) changes the CWD
            if (!_testMissionPath.empty() && _simulateMissionPath.empty())
                _testMissionPath = std::filesystem::absolute(_testMissionPath).string();

            // Make viewer paths absolute before any chdir (-C) changes the CWD
            if (!_viewerModelPath.empty())
                _viewerModelPath = std::filesystem::absolute(_viewerModelPath).string();
            if (!_viewerAnimPath.empty())
                _viewerAnimPath = std::filesystem::absolute(_viewerAnimPath).string();
            if (!_screenshotPath.empty())
                _screenshotPath = std::filesystem::absolute(_screenshotPath).string();

            // --viewer implies skipping splash + menu, and enables loose textures
            // so artists can drop .png/.tga next to the expected .paa.  We also
            // skip landscape loading because the viewer renders a single model
            // in an empty world — no terrain WRP required (worlds\intro.wrp
            // isn't part of every package, e.g. UITest only ships data3d/).
            if (_viewerMode)
            {
                _noSplash = true;
                _noMenuScene = true;
                _looseTextures = true;
                _noLandscape = true;
            }

            // Make log-file path absolute before any chdir (-C) changes the CWD
            if (!_logFile.empty())
                _logFile = std::filesystem::absolute(_logFile).string();
            if (!_perfTracePath.empty())
                _perfTracePath = std::filesystem::absolute(_perfTracePath).string();

            // Copy the std::string CLI buffers into the RString members.
            if (!serverConfigStr.empty())
            {
                _serverConfig = RString(serverConfigStr.c_str());
            }
            if (!pidFileStr.empty())
            {
                _pidFile = RString(pidFileStr.c_str());
            }
            if (!connectIPStr.empty())
            {
                _connectIP = RString(connectIPStr.c_str());
            }
            if (!masterServerStr.empty())
            {
                _masterServer = RString(masterServerStr.c_str());
            }
            if (!advertiseAddressStr.empty())
            {
                _advertiseAddress = RString(advertiseAddressStr.c_str());
            }
            if (!passwordStr.empty())
            {
                _password = RString(passwordStr.c_str());
            }
            if (!playerNameStr.empty())
            {
                _playerName = RString(playerNameStr.c_str());
            }
            if (!rankingLogStr.empty())
            {
                _rankingLog = RString(rankingLogStr.c_str());
            }
            // Resolve --mods-dir to an absolute, normalized base independent of
            // whether --mod was also given: the MODS screen scans it for local
            // @<mod> folders. Resolved before any -C chdir (CWD-relative).
            if (!modsDirStr.empty())
            {
                std::error_code mec;
                std::filesystem::path p =
                    std::filesystem::weakly_canonical(std::filesystem::absolute(modsDirStr, mec), mec);
                if (p.empty())
                    p = std::filesystem::absolute(modsDirStr, mec);
                _modsDir = RString(p.string().c_str());
            }
            // Same absolute/normalized resolution for the workshop (downloaded) root.
            if (!workshopDirStr.empty())
            {
                std::error_code wec;
                std::filesystem::path p =
                    std::filesystem::weakly_canonical(std::filesystem::absolute(workshopDirStr, wec), wec);
                if (p.empty())
                    p = std::filesystem::absolute(workshopDirStr, wec);
                _workshopDir = RString(p.string().c_str());
            }
            if (!modPathsStr.empty())
            {
                // Resolve each mod entry before any -C chdir changes the CWD.
                // Relative entries use --mods-dir when supplied, otherwise the
                // current process directory, the -C game directory, then the managed user Mods directory.
                // Missing entries are fatal: silently mounting a typo or an empty
                // folder hides the real problem and leaves the game running vanilla.
                ModPathResolveRequest resolveRequest;
                resolveRequest.modPaths = modPathsStr;
                resolveRequest.explicitModsDir =
                    _modsDir.GetLength() > 0 ? std::string((const char*)_modsDir) : std::string();
                resolveRequest.currentDir = std::filesystem::current_path().string();
                if (!_workingDirectory.empty())
                    resolveRequest.gameDir = std::filesystem::absolute(_workingDirectory).string();
                resolveRequest.managedModsDir = ResolveCliManagedModsDir();
                const ModPathResolveResult resolvedMods = ResolveModPathList(resolveRequest);

                if (!resolvedMods.Ok())
                {
                    std::string msg = "Invalid --mod value:";
                    for (const std::string& error : resolvedMods.errors)
                        msg += "\n  " + error;
                    throw std::runtime_error(msg);
                }

                _modPaths = RString(resolvedMods.mountPath.c_str());
            }
        }
        catch (const CLI::CallForHelp&)
        {
            // --help was requested - print help and exit cleanly
#ifdef _WIN32
            std::string helpText = "\n" + app.help() + "\n";
            WriteCliText(STD_OUTPUT_HANDLE, stdout, helpText);
            Sleep(50);
            FreeConsole();
#else
            fprintf(stdout, "\n%s\n", app.help().c_str());
#endif
            std::exit(0);
        }
        catch (const CLI::CallForVersion&)
        {
            // --version was requested
#ifdef _WIN32
            std::string version = "\nArma: Cold War Assault - Remastered v3.0\n";
            WriteCliText(STD_OUTPUT_HANDLE, stdout, version);
            Sleep(50);
            FreeConsole();
#else
            fprintf(stdout, "\nArma: Cold War Assault - Remastered v3.0\n");
#endif
            std::exit(0);
        }
    }
    catch (const CLI::ParseError& e)
    {
        // CLI11 throws ParseError for --help, --version, and actual errors
        // The exit code tells us if it's success (0 for --help) or error (!=0)
        int exitCode = e.get_exit_code();

        // Output to stderr for errors
        if (exitCode != 0)
        {
            const char* usageHint = BuildInfo::ReleaseBuild
                                        ? "Use --help for basic usage or --help-full for advanced usage"
                                        : "Use --help for basic usage, --help-full for advanced usage, or --help --dev "
                                          "for developer options";
#ifdef _WIN32
            std::string errorMsg = "\nCommand-line error: " + std::string(e.what()) + "\n\n" + usageHint + "\n\n";
            WriteCliText(STD_ERROR_HANDLE, stderr, errorMsg);
#else
            fprintf(stderr, "\nCommand-line error: %s\n\n%s\n\n", e.what(), usageHint);
#endif
        }

#ifdef _WIN32
        Sleep(100);    // Brief delay to ensure output is flushed
        FreeConsole(); // Detach from console before exiting
#endif

        std::exit(exitCode);
    }
    catch (const std::exception& e)
    {
        // Any other exception during setup or parsing. Do not print directly here:
        // GameBase initializes the normal logger after parsing and reports this
        // through the same log sinks as every other startup failure.
        _parseFatalError = e.what();
        _parseFatalExitCode = 2;
        return;
    }
    catch (...)
    {
        _parseFatalError = "Unknown exception during command-line parsing";
        _parseFatalExitCode = 1;
        return;
    }

    ApplyToLegacyGlobals();

    // The `res` folder is the default mod when it exists — matching OFP:Resistance
    // and Cold War Assault, which baked _MOD_PATH_DEFAULT "RES" into the original
    // engine. Gated on presence (unlike the original's unconditional default) so a
    // vanilla install advertises no phantom `res` over the MP wire. Detected and
    // resolved case-insensitively (Linux is case-sensitive and the folder may be
    // res/Res/RES) relative to the working dir (-C) — the same pre-chdir,
    // real-cased resolution the bank loader needs for --mod entries above. res is
    // listed first so --mod entries take priority over it, and it over the base
    // game (the original `RES;<user>` ordering). Applies to client and dedicated
    // server alike (both original configs baked RES).
    RString effectiveMods = _modPaths;
    {
        std::error_code rec;
        std::filesystem::path resCandidate =
            _workingDirectory.empty() ? std::filesystem::path("res") : std::filesystem::path(_workingDirectory) / "res";
        const std::string resAbs = std::filesystem::absolute(resCandidate, rec).string();
        char resReal[1024];
        if (ResolveFilePath(resAbs.c_str(), resReal, sizeof(resReal)))
        {
            const RString resMod(resReal);
            effectiveMods = effectiveMods.GetLength() > 0 ? resMod + RString(";") + effectiveMods : resMod;
        }
    }
    if (effectiveMods.GetLength() > 0)
    {
        Poseidon::ModSystem::SetModPath(effectiveMods);
    }
}

void AppConfig::ApplyToLegacyGlobals()
{
    // Mirror the parsed config into the global variables the rest of the engine reads
    // (the extern declarations above match the globals defined in Core/).

    // Display & Graphics
    ENGINE_CONFIG.useWindow = _windowMode;
    ENGINE_CONFIG.displayMode = _displayMode;
    ENGINE_CONFIG.noSplash = _noSplash;
    ENGINE_CONFIG.noMenuScene = _noMenuScene;

    ENGINE_CONFIG.enablePIII = _enablePIII;
    ENGINE_CONFIG.enableHWTL = !_disableHWTL;
    ENGINE_CONFIG.showFps = _showFps ? 1 : 0;
#ifdef _WIN32
    Poseidon::NoTextures = _noTextures;
#endif

    // Window dimensions (use wrappers to avoid incomplete type issues)
    // Apply if explicitly set on CLI, regardless of window mode
    if (_widthExplicit || _heightExplicit)
    {
        if (_widthExplicit)
            ::Glob_SetWantW(_windowWidth);
        if (_heightExplicit)
            ::Glob_SetWantH(_windowHeight);
    }

    // Multiplayer
    ENGINE_CONFIG.doCreateServer = _createServer;
    Poseidon::ServerConfig = _serverConfig;
    ClientIP = _connectIP;

    // Network settings
    SetNetworkPort(_networkPort);
    if (_connectPort > 0)
    {
        SetNetworkConnectPort(_connectPort);
    }
    if (_advertiseAddress.GetLength() > 0)
    {
        SetNetworkAdvertiseAddress(_advertiseAddress);
    }
    if (_privateServer)
    {
        SetNetworkMasterServer(RString());
    }
    else if (_masterServer.GetLength() > 0)
    {
        SetNetworkMasterServer(_masterServer);
    }
    if (_password.GetLength() > 0)
    {
        SetNetworkPassword(_password);
    }
    if (_pidFile.GetLength() > 0)
    {
        Poseidon::SetPidFileName(_pidFile);
    }
    if (_rankingLog.GetLength() > 0)
    {
        Poseidon::SetRankingLog(_rankingLog);
    }

    // Player name (use wrapper to avoid incomplete type issues)
    if (_playerName.GetLength() > 0)
    {
        ::Glob_SetPlayerName(_playerName);
    }

    // Audio
    ENGINE_CONFIG.noSound = _noSound;
    if (!_audioBackend.empty())
    {
        ENGINE_CONFIG.requestedAudioBackend = _audioBackend;
    }

    // FlashpointCfg path is set by ApplyGamePathsToLegacyGlobals() — not here.
    // Sync CLI --mod path to legacy ModPath so EnumModDirectories includes it for bank loading
    if (_modPaths.GetLength() > 0)
    {
        RString& legacyModPath = ::GetModPathInternal();
        if (legacyModPath.GetLength() > 0)
            legacyModPath = _modPaths + RString(";") + legacyModPath;
        else
            legacyModPath = _modPaths;
    }

    // Initialization flags
    ENGINE_CONFIG.checkInitAndExit = _checkInit;
    GApp->m_keepFocus = _keepFocus;
    ENGINE_CONFIG.noLandscape = _noLandscape;
    ENGINE_CONFIG.noTerrainCache = _noTerrainCache;

    // Debug & Testing
    ::Benchmark = _benchmark;
    ::GLogFileOps = _logFileOps;
#ifdef NET_LOG_COMMAND_LINE
    if (_netLog)
        ::netLogValid = true;
#endif
    ::AutoTest = _autoTest;

    // Mission file
    if (!_missionFile.empty())
    {
        strncpy(::LoadFile, _missionFile.c_str(), sizeof(::LoadFile) - 1);
        ::LoadFile[sizeof(::LoadFile) - 1] = 0;
    }
}

} // namespace Poseidon::Foundation
