#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/ProgressSystem.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <Poseidon/Core/Config/Configuration.hpp>
#include <Poseidon/Foundation/Platform/AppConfig.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>               // For GLOB_ENGINE
#include <Poseidon/Graphics/Dummy/GraphicsEngineDummy.hpp> // For default CreateGraphicsEngine
#include <Poseidon/World/World.hpp>                        // For GWorld (IsInGameplay)
#ifdef _WIN32
#include <windows.h>

#endif

// Application name — default for all apps, can be overridden per-app before engine init
const char* AppName = "ARMA:CWA-RE-CE";

namespace Poseidon
{
Application* Application::s_instance = nullptr;

// Global application pointer (like Unreal's GEngine)
Application* GApp = nullptr;

Application::Application(Type type) : m_type(type), m_progressSystem(std::make_unique<ProgressSystem>())
{
    s_instance = this;
    GApp = this;
    SetGProgress(m_progressSystem.get());

#ifdef _WIN32
    m_hInstance = GetModuleHandle(nullptr);
#endif
    m_hwnd = 0;
    m_validateQuit = false;
    m_closeRequest = false;
}

Application::~Application()
{
    SetGProgress(nullptr);
    GApp = nullptr;
    s_instance = nullptr;
}

ProgressSystem& Application::GetProgressSystem()
{
    return *m_progressSystem;
}

Foundation::LoggingSystem& Application::GetLoggingSystem()
{
    if (!m_loggingSystem)
    {
        m_loggingSystem = std::make_unique<Foundation::LoggingSystem>();
    }
    return *m_loggingSystem;
}

ConfigurationSystem& Application::GetConfig()
{
    if (!m_configSystem)
    {
        m_configSystem = std::make_unique<ConfigurationSystem>();
    }
    return *m_configSystem;
}

bool Application::IsAppPaused() const
{
    if (GApp->m_keepFocus)
    {
        return false;
    }
    return !m_appActive || m_appPaused || m_appIconic;
}

bool Application::IsInGameplay() const
{
    // Active gameplay: a mission world is simulating with its HUD live. GModeIntro
    // (main menu) also runs a world with a background cutscene and UIEnabled=true, but
    // that is not player-controlled gameplay. Startup/loading progress can also pump
    // window events after GWorld exists, before the menu/gameplay state is settled; Alt+F4
    // must close there too.
    const bool progressActive = ProgressScript || (GEngine && GetGProgress().Active());
    return ShouldReportInGameplayForWindowClose(GWorld != nullptr, GWorld && GWorld->GetMode() == GModeIntro,
                                                GWorld && GWorld->IsUIEnabled(), progressActive);
}

Engine* Application::CreateGraphicsEngine(const GraphicsEngineParams& /*params*/)
{
    // Default: headless dummy engine (server, tools)
    return CreateEngineDummy();
}

void AppPause(bool f)
{
    if (f)
    {
        GApp->m_appPaused = true;
        if (GApp->m_canRender)
        {
            GLOB_ENGINE->Pause();
        }
    }
    else
    {
        GApp->m_appPaused = false;
    }
}

void DestroyEngine()
{
    if (GLOB_ENGINE)
    {
        delete GLOB_ENGINE, GLOB_ENGINE = nullptr;
    }
}
} // namespace Poseidon
