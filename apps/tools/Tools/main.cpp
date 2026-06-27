#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Audio/AudioFactory.hpp>
#include <Poseidon/Audio/Voice/VoiceBackend.hpp>
#include <Poseidon/Foundation/Platform/InitBridge.hpp>
#include <Poseidon/IO/ParamFile/InitLibraryElement.hpp>
#include <Poseidon/Foundation/Platform/PoseidonInit.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Graphics/Dummy/GraphicsEngineDummy.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <stdarg.h>
#include <CLI/App.hpp>
#include <CLI/Error.hpp>
#include <vector>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/platform.hpp>

#ifdef _MSC_VER
#pragma init_seg(compiler)
#endif
namespace
{
struct EarlyMemoryInit
{
    EarlyMemoryInit() { SetMemorySystemReady(true); }
} g_earlyMemoryInit;
} // namespace

// CLI tools need an app-frame sink before engine initialization.
class ToolAppFrameFunctions : public Poseidon::Foundation::AppFrameFunctions
{
  public:
    void ErrorMessage(Poseidon::Foundation::ErrorMessageLevel, const char*, va_list) override {}
    void ErrorMessage(const char*, va_list) override {}
    void WarningMessage(const char*, va_list) override {}
    void ShowMessage(int, const char*, va_list) override {}
    DWORD TickCount() override { return 0; }
};

static ToolAppFrameFunctions toolAppFrame;

// Command handlers use the engine-global application and configuration system.
class ToolApplication : public Poseidon::Application
{
  public:
    ToolApplication() : Poseidon::Application(Poseidon::Application::CLIENT) {}
    int Run(const char*) override { return 0; }

  protected:
    bool InitializeMemorySystem() override { return true; }
    bool ParseCommandLine(const char*) override { return true; }
    bool ReadConfiguration() override { return true; }
    bool InitializeSubsystems() override { return true; }
    void RunMainLoop() override {}
    void ShutdownSubsystems() override {}
};

#include <CLI/CLI.hpp>
#include <iostream>
#include <string>

#include "commands/VersionCommand.hpp"
#include "commands/ModelCommand.hpp"
#include "commands/ImageCommand.hpp"
#include "commands/PboCommand.hpp"
#include "commands/TerrainCommand.hpp"
#include "commands/FontCommand.hpp"
#include "commands/SoundCommand.hpp"
#include "commands/ConfigCommand.hpp"
#include "commands/StringtableCommand.hpp"
#include "commands/LintCommand.hpp"
#include "commands/ScanCommand.hpp"
#include "commands/MineCommand.hpp"
#include "commands/VonCommand.hpp"
#include "commands/ShadowCommand.hpp"
#ifndef _WIN32
#include "commands/InputCommand.hpp"
#endif

int main(int argc, char** argv)
{
    Poseidon::RegisterDummyAudioBackend();
    Poseidon::RegisterTextAudioBackend();
    Poseidon::RegisterOpenALAudioBackend();
    Poseidon::RegisterOpenALVoiceBackend();

    Poseidon::Foundation::CurrentAppFrameFunctions = &toolAppFrame;
    SetMemorySystemReady(true);
    Poseidon::Foundation::gSoftAssert = true;
    InitLibraryElement();
    Poseidon::InitDefaults();
    GamePaths::Instance().Initialize("CWR", "ColdWarAssault", "Cold War Assault");
    Poseidon::GEngine = Poseidon::CreateEngineDummy();
    static ToolApplication toolApp;

    CLI::App app{"PoseidonTools - Command-line utilities for Arma: Cold War Assault - Remastered"};

    app.set_version_flag("-v,--version", "1.0.0");
    app.require_subcommand(1);

    PoseidonTools::VersionCommand::Setup(app);
    PoseidonTools::ModelCommand::Setup(app);
    PoseidonTools::ImageCommand::Setup(app);
    PoseidonTools::PboCommand::Setup(app);
    PoseidonTools::TerrainCommand::Setup(app);
    PoseidonTools::FontCommand::Setup(app);
    PoseidonTools::SoundCommand::Setup(app);
    PoseidonTools::ConfigCommand::Setup(app);
    PoseidonTools::StringtableCommand::Setup(app);
    PoseidonTools::LintCommand::Setup(app);
    PoseidonTools::ScanCommand::Setup(app);
    PoseidonTools::MineCommand::Setup(app);
    PoseidonTools::VonCommand::Setup(app);
    PoseidonTools::ShadowCommand::Setup(app);
#ifndef _WIN32
    PoseidonTools::InputCommand::Setup(app);
#endif
    try
    {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError& e)
    {
        if (e.get_exit_code() == static_cast<int>(CLI::ExitCodes::RequiredError) &&
            std::string(e.what()).find("subcommand") != std::string::npos)
        {
            for (auto* s : app.get_subcommands())
            {
                if (s->parsed())
                {
                    std::cout << s->help() << std::endl;
                    return 0;
                }
            }
            std::cout << app.help() << std::endl;
            return 0;
        }
        return app.exit(e);
    }
    return 0;
}
