#pragma once

#include <Poseidon/Foundation/Strings/RString.hpp>
#include <string>

// Forward declaration for CLI11
namespace CLI {
    class App;
}

namespace Poseidon::Foundation
{

/// Centralized application configuration parsed from command-line arguments.
///
/// Usage: AppConfig::Instance().IsWindowMode()
///
/// All getters are const and thread-safe after ParseCommandLine() completes.
class AppConfig {
public:
    static AppConfig& Instance();

    /// Call early in main(), before any subsystem initializes.
    void ParseCommandLine(int argc, char** argv);

    bool HasParseFatalError() const { return _parseFatalExitCode != 0; }
    int GetParseFatalExitCode() const { return _parseFatalExitCode; }
    const std::string& GetParseFatalError() const { return _parseFatalError; }

    /// Apply parsed config values to process-wide globals.
    void ApplyToLegacyGlobals();

    // Display & Graphics

    bool IsWindowMode() const { return _windowMode; }

    /// Check if --window / --fullscreen was explicitly provided on the CLI
    bool IsWindowFlagExplicit() const { return _windowFlagExplicit; }
    
    /// Get display mode string: "windowed", "borderless", "exclusive"
    const std::string& GetDisplayMode() const { return _displayMode; }
    
    /// Check if display mode was explicitly set on command line
    bool IsDisplayModeExplicit() const { return _displayModeExplicit; }
    
    int GetWindowWidth() const { return _windowWidth; }

    int GetWindowHeight() const { return _windowHeight; }
    
    /// Check if width was explicitly set on command line
    bool IsWidthExplicit() const { return _widthExplicit; }
    
    /// Check if height was explicitly set on command line
    bool IsHeightExplicit() const { return _heightExplicit; }
    
    bool NoSplash() const { return _noSplash; }

    /// Check if the ASCII-art dedicated-server startup banner should be skipped
    bool NoBanner() const { return _noBanner; }
    
        const std::string& GetRenderBackend() const { return _renderBackend; }
    
    /// Enable Pentium III optimizations (flush-to-zero mode)
    bool EnablePIII() const { return _enablePIII; }
    
    /// Enable hardware transform & lighting (T&L, default on unless --sw-tl)
    bool EnableHWTL() const { return !_disableHWTL; }
    
        bool DualMonitorMode() const { return _dualMonitor; }
    
    bool NoTextures() const { return _noTextures; }
    
    /// Whether mouse grab (cursor confinement + hiding) is disabled
    bool NoMouseGrab() const { return _noMouseGrab; }

    bool NoFreeType() const { return _noFreeType; }

    // Multiplayer

    bool IsHostServer() const { return _createServer; }

    /// Server configuration file path
    const RString& GetServerConfig() const { return _serverConfig; }
    
    /// PID file path (for dedicated server process management)
    const RString& GetPidFile() const { return _pidFile; }
    
    /// IP address to connect to
    const RString& GetConnectIP() const { return _connectIP; }
    
    /// Network port (0 = use default)
    int GetNetworkPort() const { return _networkPort; }
    
    /// Remote connect port (0 = use GetNetworkPort)
    int GetConnectPort() const { return _connectPort; }

    /// Public address advertised to the master server (empty = detected local address)
    const RString& GetAdvertiseAddress() const { return _advertiseAddress; }

    /// Process memory cap override in MB (0 = not set, use config / auto-derived)
    int GetMaxMemMB() const { return _maxMemMB; }

    /// TaskPool thread cap override (-1 = auto/capped, 0 = uncapped, >0 = explicit)
    int GetMaxThreads() const { return _maxThreads; }

    /// Master server / directory host override
    const RString& GetMasterServer() const { return _masterServer; }

    /// True when master-server publishing/listing is disabled for a LAN/private server.
    bool IsPrivateServer() const { return _privateServer; }

    /// True when this dedicated server should publish itself to the master server.
    bool IsPublicServer() const { return _publicServer; }
    
    /// Server password
    const RString& GetPassword() const { return _password; }
    
    /// Player name
    const RString& GetPlayerName() const { return _playerName; }
    
    /// Ranking log file path
    const RString& GetRankingLog() const { return _rankingLog; }
    
    /// Auto-start server when N human players are ready (--mp-auto-start N, 0=disabled)
    int GetMPAutoStart() const { return _mpAutoStart; }
    
    /// Force JIP on all missions regardless of mission config (--force-jip)
    bool GetForceJIP() const { return _forceJIP; }
    
    /// Auto-assign client to side:slot (--mp-assign WEST:1), empty=disabled
    const std::string& GetMPAssign() const { return _mpAssign; }

    /// Print the exact MP compatibility tuple and exit before opening a window.
    bool PrintMPVersion() const { return _printMPVersion; }
    
    // Audio

    bool NoSound() const { return _noSound; }
    
    const std::string& GetAudioBackend() const { return _audioBackend; }
    
    // Configuration files

    /// Mod directory paths (semicolon-separated)
    const RString& GetModPaths() const { return _modPaths; }

    /// Resolved absolute --mods-dir, or "" when not given. The mods root the MODS
    /// screen scans for local @<mod> folders (else GamePaths::ModsDir()).
    const RString& GetModsDir() const { return _modsDir; }

    /// Resolved absolute --workshop-dir, or "" when not given. The root the MODS
    /// screen scans/installs downloaded mods into (else GamePaths::WorkshopDir()).
    const RString& GetWorkshopDir() const { return _workshopDir; }

    /// Language override (from --lang CLI parameter)
    const std::string& GetLanguage() const { return _language; }

    /// Require addon banks to declare encryption metadata
    bool RequireEncryptedAddons() const { return _requireEncryptedAddons; }

    /// Use legacy game-folder paths for profiles, config, and editor missions.
    bool OldPaths() const { return _oldPaths; }

    // Initialization flags

    bool NoMap() const { return _noMap; }

    bool CheckInitAndExit() const { return _checkInit; }

    /// Boot, perform one in-process re-mount, then exit (re-mount smoke test)
    bool IsRemountSelfTest() const { return _remountSelfTest; }

    /// Boot, re-mount bare -> with --mod -> bare, asserting load/unload, then exit
    bool IsModCycleSelfTest() const { return _modCycleSelfTest; }

    /// Boot, re-mount several times, asserting loaded-content metrics stay at a clean baseline
    bool IsReloadCleanSelfTest() const { return _reloadCleanSelfTest; }

    /// Boot, run the intro, re-mount, then pump World::Simulate frames on the re-mounted world
    bool IsRemountSimSelfTest() const { return _remountSimSelfTest; }

    /// Boot, force a failed re-mount, then pump frames — a bad re-mount must roll back, not crash
    bool IsRemountFailSelfTest() const { return _remountFailSelfTest; }

    /// Boot, raise a simulated critical error (ErrorMessage), and assert the process survives
    /// under --no-strict instead of exit(1) — proves the player build stays alive
    bool IsErrorResilienceSelfTest() const { return _errorResilienceSelfTest; }

    /// Audit editor-visible CfgVehicles model references during boot.
    bool AuditCfgVehiclesModels() const { return _auditCfgVehiclesModels; }

    /// Keep focus when window loses focus
    bool KeepFocus() const { return _keepFocus; }

    /// Strict mode: any ERROR-level log (incl. SQF/SQS script errors) is fatal —
    /// the app requests a clean shutdown with a non-zero exit code. Default on in
    /// Debug only; off in RelWithDebInfo and shipping (the build players run).
    /// Automated tests pass --strict explicitly. --no-strict / --strict overrides.
    bool Strict() const { return _strict; }
    
    bool NoLandscape() const { return _noLandscape; }

    /// Frame validation stats — log per-frame pass/draw counts every
    /// N frames.  Default off so steady-state gameplay logs nothing.
    /// The frame validator (SceneExtractor → BuildFrame →
    /// ValidateFrame) runs regardless; this flag only controls the
    /// summary log line.
    bool RenderFrameLog() const { return _renderFrameLog; }

    
    // Debug & Testing

    bool BenchmarkMode() const { return _benchmark; }

    /// Dev mode (--dev) — gates the Ctrl+` developer panel (Cheats /
    /// Game / Console / Profile / Memory / Font tabs).  In ship builds
    /// the panel never opens when the flag isn't set.
    bool DevMode() const { return _devMode; }

    /// Confirm-revert modal timeout (seconds).  0 = use the engine default
    /// (15s).  Tests set this to a small value via --confirm-revert-timeout
    /// to exercise the auto-revert path without paying the full 15-second
    /// real-time wait.
    float ConfirmRevertTimeoutSeconds() const { return _confirmRevertTimeoutSeconds; }

    /// Generic app auto-exit timeout in seconds. 0 = disabled.
    float AppTimeoutSeconds() const { return _appTimeoutSeconds; }
    
    /// Test mission path (--test-mission)
    const std::string& GetTestMissionPath() const { return _testMissionPath; }

    /// True when --check and --test-mission are combined to run a bounded mission smoke boot.
    bool IsMissionSmokeCheck() const { return _checkInit && !_testMissionPath.empty(); }

    /// True when --check and --simulate are combined to run a bounded MP smoke boot.
    bool IsSimulateSmokeCheck() const { return _checkInit && _simulateMode; }
    
    /// Test type: "autotest" (mission self-terminates) or "screenshot" (capture first frame and exit)
    const std::string& GetTestType() const { return _testType; }
    bool IsScreenshotTest() const { return _testType == "screenshot"; }
    
    /// Stats interval in seconds (--stats N), 0 = disabled
    int GetStatsInterval() const { return _statsInterval; }
    
    /// Time scale for simulate mode (--time-scale N), 1 = realtime
    int GetTimeScale() const { return _timeScale; }
    
    /// Simulate mode (--simulate): headless mission execution without players
    bool IsSimulateMode() const { return _simulateMode; }
    
    /// Simulation duration in seconds (--duration N, 0 = run until endGame or Ctrl+C)
    int GetSimulateDuration() const { return _simulateDuration; }
    
    bool LogFileOps() const { return _logFileOps; }

    bool NetLog() const { return _netLog; }

    /// Write MP after-action report to timestamped file in UserDir
    bool WriteMPReport() const { return _writeMPReport; }

    bool AutoTest() const { return _autoTest; }
    
    /// Auto-keys specification for scripted input injection (--auto-keys "frame:scancode,...")
    const std::string& GetAutoKeys() const { return _autoKeys; }
    
    /// Auto-screenshot specification (--auto-screenshot "frame:path")
    const std::string& GetAutoScreenshot() const { return _autoScreenshot; }

    /// RenderDoc trigger specification (--rdc-trigger "frame|<sec>s")
    /// — fires api->TriggerCapture() on the matching frame, captures
    /// next swap.  No-op if game wasn't launched from RenderDoc.
    const std::string& GetRdcTrigger() const { return _rdcTrigger; }
    
    /// Number of gameplay frames to wait before capturing screenshot (--screenshot-delay N, default 10)
    int GetScreenshotDelay() const { return _screenshotDelay; }
    
    /// UI test scenario name (--ui-test "exit")
    const std::string& GetUITest() const { return _uiTest; }

    /// TCP harness server port (--harness <port>): -1 = disabled, 0 = auto-assign, >0 = explicit
    int GetHarnessPort() const { return _harnessPort; }
    
    // Logging

    /// Get log level (trace, debug, info, warn, error, critical, off)
	const std::string& GetLogLevel() const { return _logLevel; }
	const std::string& GetLogCategories() const { return _logCategories; }
	const std::string& GetLogFormat() const { return _logFormat; }
	const std::string& GetAppTag() const { return _appTag; }
	bool GetLegacyLogs() const { return _legacyLogs; }

	/// Log file path (--log-file), empty = no file logging
	const std::string& GetLogFile() const { return _logFile; }

	/// Chrome trace JSON output path (--perf-trace), empty = disabled.
	/// File loads in https://ui.perfetto.dev/ or chrome://tracing.
	const std::string& GetPerfTracePath() const { return _perfTracePath; }
	
	/// Get working directory (game data directory)
	const std::string& GetWorkingDirectory() const { return _workingDirectory; }

    // Mission file

    /// Mission file to load (positional argument)
    const std::string& GetMissionFile() const { return _missionFile; }
    
    // Viewer mode

    /// True when launched with --viewer (single-model preview mode).
    bool IsViewerMode() const { return _viewerMode; }

    /// P3D model path to load in viewer.
    const std::string& GetViewerModelPath() const { return _viewerModelPath; }

    /// RTM animation path to bind to the viewer model (empty = none).
    const std::string& GetViewerAnimPath() const { return _viewerAnimPath; }

    /// Viewer animation playback speed (1.0 = real-time).
    float GetViewerAnimSpeed() const { return _viewerAnimSpeed; }

    /// Viewer animation loop mode: "none", "repeat", or "ping-pong".
    const std::string& GetViewerAnimLoop() const { return _viewerAnimLoop; }

    /// True if texture loader should fall back to .png / .tga next to expected .paa.
    bool LooseTextures() const { return _looseTextures; }

    /// Viewer starts with on-screen help hidden.
    bool ViewerNoHelp() const { return _viewerNoHelp; }

    /// Screenshot output path (empty = no screenshot)
    const std::string& GetScreenshotPath() const { return _screenshotPath; }

    /// CLI view distance override (--vd N, 0 = use config file value)
    float GetViewDistanceOverride() const { return _viewDistanceOverride; }
    
private:
    AppConfig();
    ~AppConfig() = default;
    
    AppConfig(const AppConfig&) = delete;
    AppConfig& operator=(const AppConfig&) = delete;

    // Display & Graphics
    bool _windowMode = false;
    bool _windowFlagExplicit = false;
    std::string _displayMode = "borderless";
    bool _displayModeExplicit = false;
    int _windowWidth = 800;
    int _windowHeight = 600;
    bool _widthExplicit = false;
    bool _heightExplicit = false;
    bool _noSplash = false;
    bool _noBanner = false;
    bool _noMenuScene = false;
    std::string _renderBackend = "gl33";
    bool _enablePIII = false;
    bool _enableHWTL = false;
    bool _disableHWTL = false;
    bool _showFps = false;
    bool _dualMonitor = false;
    bool _noTextures = false;
    bool _noMouseGrab = false;
    bool _noFreeType = false;

    // Multiplayer
    bool _createServer = false;
    RString _serverConfig;
    RString _pidFile;
    RString _connectIP;
    int _networkPort = 1985;
    int _connectPort = 0;
    RString _bindAddress = "0.0.0.0";
    RString _advertiseAddress;
    int _maxMemMB = 0;
    int _maxThreads = -1;
    bool _privateServer = false;
    bool _publicServer = false;
    RString _masterServer;
    RString _password;
    RString _playerName;
    RString _rankingLog;
    int _mpAutoStart = 0;
    bool _forceJIP = false;
    std::string _mpAssign;
    bool _printMPVersion = false;
    
    // Audio
    bool _noSound = false;
    std::string _audioBackend;
    
    // Configuration files (parsed in Pass 1 - early init)
    RString _modPaths;
    RString _modsDir;     // resolved --mods-dir base ("" if unset)
    RString _workshopDir; // resolved --workshop-dir base ("" if unset)
    std::string _language;
    bool _requireEncryptedAddons = false;
    bool _oldPaths = false;

    // Initialization flags
    bool _noMap = false;
    bool _checkInit = false;
    bool _remountSelfTest = false;
    bool _modCycleSelfTest = false;
    bool _reloadCleanSelfTest = false;
    bool _remountSimSelfTest = false;
    bool _remountFailSelfTest = false;
    bool _errorResilienceSelfTest = false;
    bool _auditCfgVehiclesModels = false;
    bool _keepFocus = false;
    bool _strict = false;
    bool _noLandscape = false;
    bool _noTerrainCache = false;
    bool _renderFrameLog  = false;

    // Debug & Testing
    bool _benchmark = false;
    bool _logFileOps = false;
    bool _netLog = false;
    bool _writeMPReport = false;
    bool _autoTest = false;
    bool _devMode = false;
    float _confirmRevertTimeoutSeconds = 0.f; // 0 → engine default (15s)
    float _appTimeoutSeconds = 0.f;
    std::string _testMissionPath;
    std::string _testType = "autotest";
    std::string _simulateMissionPath;
    bool _simulateMode = false;
    int _simulateDuration = 0;
    int _statsInterval = 0;
    int _timeScale = 1;
    std::string _autoKeys;
    std::string _autoScreenshot;
    std::string _rdcTrigger;
    int _screenshotDelay = 10;
    std::string _uiTest;
    int _harnessPort = -1; // -1 = disabled, 0 = auto-assign, >0 = explicit
    
    // Logging
 	std::string _logLevel = "info";
	std::string _logCategories = "";  // Empty = all categories
	std::string _logFormat = "text"; // Log format: text (default) or jsonl
	std::string _appTag = "";        // App identifier tag for log lines (--app-tag)
	bool _legacyLogs = false;         // Show legacy bridge logs (--legacy-logs)
	std::string _logFile = "";        // Log file path (--log-file), empty = no file
	std::string _perfTracePath = "";  // Chrome trace JSON path (--perf-trace), empty = disabled
    // Mission file
    std::string _missionFile;
    
    // Viewer mode
    bool _viewerMode = false;
    std::string _viewerModelPath;
    std::string _viewerAnimPath;
    float _viewerAnimSpeed = 1.0f;
    std::string _viewerAnimLoop = "ping-pong";
    bool _looseTextures = false;
    bool _viewerNoHelp = false;

    // Screenshot output (shared between --viewer and --test-mission screenshot)
    std::string _screenshotPath;

    // Working directory
    std::string _workingDirectory;  // Game data directory (empty = current directory)

    // View distance override
    float _viewDistanceOverride = 0;
    
    // Flag to ensure ParseCommandLine is only called once
    bool _parsed = false;
    int _parseFatalExitCode = 0;
    std::string _parseFatalError;
};

} // namespace Poseidon::Foundation

using AppConfig = Poseidon::Foundation::AppConfig;
