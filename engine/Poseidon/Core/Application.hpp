#pragma once
#define POSEIDON_APPLICATION_HPP

#include <memory>
#include <string>
#include <Poseidon/Foundation/Logging/Logging.hpp> // Need full definition for unique_ptr<LoggingSystem>
#include <Poseidon/Core/Config/Configuration.hpp>  // Need full definition for unique_ptr<ConfigurationSystem>

// Forward declarations (global scope — types defined outside Core/)
namespace Poseidon
{
class Engine;
}
using Poseidon::Engine;
namespace Poseidon
{
struct GraphicsEngineParams;
}
using Poseidon::GraphicsEngineParams;

// Application display name; defined in Application.cpp.
extern const char* AppName;

// Forward declarations for Windows types (Linux gets these from PoseidonBase platform headers)
#ifdef _WIN32
struct HINSTANCE__;
struct HWND__;
typedef HINSTANCE__* HINSTANCE;
typedef HWND__* HWND;
#else
#include <Poseidon/Foundation/platform.hpp>
#endif

namespace Poseidon
{

class ProgressSystem;

class Application
{
  public:
    enum Type
    {
        CLIENT,          // Full game client with GUI
        DEDICATED_SERVER // Headless server
    };

    Application(Type type);
    virtual ~Application();

    // Main entry point - returns exit code
    virtual int Run(const char* commandLine) = 0;

    Type GetType() const { return m_type; }
    bool IsClient() const { return m_type == CLIENT; }
    bool IsServer() const { return m_type == DEDICATED_SERVER; }

    // Subsystem access
    ProgressSystem& GetProgressSystem();
    Foundation::LoggingSystem& GetLoggingSystem();
    ConfigurationSystem& GetConfig();

    // Global instance access (set by concrete Application in Run())
    static Application* s_instance;
    static Application& Instance() { return *s_instance; }

    // Application state (for window focus, pause, etc.)
    bool IsAppActive() const { return m_appActive; }
    bool IsAppPaused() const; // Checks GApp->m_keepFocus, active, paused, iconic

    // True while the player is actively controlling a unit in a running mission — the
    // in-game UI is live, so keyboard combos like Alt (freelook) + F4 (select unit 4)
    // are valid game shortcuts. False in menus, briefing, the Esc dialog, and loading.
    bool IsInGameplay() const;

    // Windows handles (public for now - backwards compatibility)
    HINSTANCE m_hInstance = 0;
    HWND m_hwnd = 0;

    // Quit/close state (public for backwards compatibility shim)
    bool m_validateQuit = false;          // User really wants to quit (not accidental ESC)
    bool m_closeRequest = false;          // Close has been requested
    int m_exitCode = 0;                   // Exit code for process termination
    bool m_cleanTestEndRequested = false; // triEndTest initiated shutdown before any failure did

    // Application state (public for backwards compatibility shim)
    bool m_appActive = false;
    bool m_appPaused = false;
    bool m_appIconic = false;

    // Rendering state (public for backwards compatibility shim)
    bool m_keepFocus = false;   // Keep focus when window loses focus
    bool m_canRender = false;   // Everything is initialized, rendering enabled
    bool m_forceRender = false; // Draw frame as soon as possible
    // Deferred in-process re-mount request (set by triRemount / dev panel). Serviced
    // at a safe between-frames point by AppIdle — a reload tears down the world, so it
    // must not run mid-frame while scripts/draw are using it (the rebuilt world's first
    // Simulate hits a null sensor list in SensorList::CheckPos). All callers route
    // through RequestRemount so the reload lands here.
    bool m_remountRequested = false;
    bool m_remountHasModPath = false; // false → reload with the current mod set
    std::string m_remountModPath;     // explicit mod set when m_remountHasModPath ("" = base game)
    bool m_remountFailed = false;     // a Remount rolled back; the menu shows a message + clears this

    // Queue an in-process re-mount for the next AppIdle (the only safe point). Both are safe
    // to call mid-frame / from an ImGui handler — they only set state; the teardown+reload
    // runs later at the top of AppIdle, before any simulate/draw.
    void RequestRemount() // reload with the currently mounted mod set
    {
        m_remountHasModPath = false;
        m_remountRequested = true;
    }
    void RequestRemountWithMods(const char* modPath) // modPath = semicolon-separated set, "" = base game
    {
        m_remountModPath = modPath ? modPath : "";
        m_remountHasModPath = true;
        m_remountRequested = true;
    }

    // Graphics engine creation hook — apps override to provide real backends.
    // Default returns Dummy (headless). Game overrides with DX9/GL selection.
    virtual Engine* CreateGraphicsEngine(const GraphicsEngineParams& params);

    // In-process reload of all game content with the current mod set (keeps the
    // window/device alive). Lets the dev panel and tooling trigger a re-mount
    // without going through a concrete app type. Default no-op; Game overrides.
    // Returns false if reload is not possible (e.g. a mission is active).
    virtual bool ReloadGameContent() { return false; }

    // Same in-process re-mount, but swaps the mod set first. `modPath` is a
    // semicolon-separated list of absolute mod-folder paths (empty string = base
    // game only). Lets the dev panel apply a user-picked mod selection without
    // depending on the concrete app type. Default no-op; Game overrides.
    virtual bool ReloadGameContentWithMods(const char* /*modPath*/) { return false; }

    // True only in the demo build (GameDemoApplication overrides it). The demo and
    // full game share one compiled engine, so demo-specific behaviour keys off this
    // runtime flag rather than a compile switch: the menu cutscene world
    // (UseDemoWorld / GetMenuInitWorld) and the "-demo" version tag (GetVersionTag).
    virtual bool IsDemo() const { return false; }

    // The demo runs its menu background cutscene on the demo island
    // (CfgWorlds::demoWorld) rather than the full intro island. Read via
    // GetMenuInitWorld().
    virtual bool UseDemoWorld() const { return IsDemo(); }

  protected:
    // Lifecycle hooks
    virtual bool InitializeMemorySystem() = 0;
    virtual bool ParseCommandLine(const char* commandLine) = 0;
    virtual bool ReadConfiguration() = 0;
    virtual bool InitializeSubsystems() = 0;
    virtual void RunMainLoop() = 0;
    virtual void ShutdownSubsystems() = 0;

  private:
    Type m_type;
    std::unique_ptr<ProgressSystem> m_progressSystem; // Owned by Application, injected into global accessor
    std::unique_ptr<Foundation::LoggingSystem> m_loggingSystem;
    std::unique_ptr<ConfigurationSystem> m_configSystem;
};

// A window close request (the OS turns Alt+F4 into one on Windows) is honoured unless
// Alt is held during active gameplay, where Alt+F4 is a real in-game shortcut, not a
// quit. The title-bar X / taskbar close / menu Quit carry no Alt and always close.
inline bool ShouldHonorWindowClose(bool altDown, bool inGameplay)
{
    return !(altDown && inGameplay);
}

// Runtime gate that feeds ShouldHonorWindowClose. A world with enabled UI is gameplay only
// after loading/startup progress has finished and only outside the main-menu intro scene.
inline bool ShouldReportInGameplayForWindowClose(bool hasWorld, bool introMode, bool uiEnabled,
                                                 bool startupProgressActive)
{
    return hasWorld && !introMode && uiEnabled && !startupProgressActive;
}

inline int ResolveMultiplayerAutoTestExitCode(int exitCode, bool missionReachedPlay, bool cleanTestEndRequested)
{
    // Auto-assign uses 2 while waiting for play. Only an explicit clean harness end may
    // consume that sentinel; real failure codes remain authoritative.
    constexpr int pendingMissionExitCode = 2;
    if (exitCode == pendingMissionExitCode && missionReachedPlay && cleanTestEndRequested)
        return 0;
    return exitCode;
}

inline int ResolveMultiplayerAutomationExitCode(int exitCode, bool shutdownAlreadyRequested, bool successfulOutcome)
{
    // Auto-assign starts at 2 while waiting for play. A new outcome may replace that
    // sentinel, but a path reached after shutdown was requested must preserve its result.
    if (shutdownAlreadyRequested)
        return exitCode;
    return successfulOutcome ? 0 : 2;
}

// Global application pointer (like Unreal's GEngine, CryEngine's gEnv)
extern Application* GApp;
} // namespace Poseidon

// Global-scope alias
using Poseidon::GApp;
