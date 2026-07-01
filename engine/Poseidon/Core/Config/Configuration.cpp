// Configuration.cpp - Implementation of unified configuration system

#include <Poseidon/Core/Config/Configuration.hpp>
#include <Poseidon/UI/Locale/SupportedLanguages.hpp>
#include <Poseidon/UI/Locale/LanguageRegistry.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/AI/LicensePlateTextTuning.hpp>
#include <Poseidon/Core/RuntimeFlags.hpp>
#include <Poseidon/Core/EngineState.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Core/Config/ConfigSystem.hpp>
#include <Poseidon/Core/ModSystem.hpp>
#include <Poseidon/Core/Application.hpp> // For logging macros
#include <Poseidon/Foundation/Platform/AppConfig.hpp>
#include <Poseidon/Foundation/Platform/InitBridge.hpp>
#include <Poseidon/Network/NetworkConfig.hpp>

// Global-scope includes (must be outside namespace to avoid re-declaring types inside Poseidon)
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/IO/ParamFileExt.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Modules/Modules.hpp>
#include <Poseidon/Foundation/Strings/Mbcs.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>

namespace Poseidon
{
extern AIStats& GetGStats();
}

namespace Poseidon
{
RString GetUserParams();
}

// Global-scope functions referenced via ::
namespace Poseidon
{
using Poseidon::IsMenuOverriddenByMod;
using Poseidon::MergeBaseResourceExtra;
using Poseidon::ParseConfig;
using Poseidon::ParseRemaster;
using Poseidon::ParseResource;
using Poseidon::ParseStringtable;

// Forward declaration of GetPars (defined in ConfigurationHelpers.cpp, same namespace)
ParamClass& GetPars();

ConfigurationSystem::ConfigurationSystem()
    : m_initialized(false), m_runtimeFlags(std::make_unique<RuntimeFlags>()),
      m_engineState(std::make_unique<EngineState>()),
      m_engineConfig(std::make_unique<EngineConfig>(*m_runtimeFlags, *m_engineState)),
      m_userConfig(std::make_unique<UserConfig>())
{
}

ConfigurationSystem::~ConfigurationSystem()
{
    Shutdown();
}

void ConfigurationSystem::Initialize(const AppConfig& cliArgs)
{
    if (m_initialized)
    {
        return;
    }

    LOG_TRACE(Config, "Initializing configuration system...");
    LOG_TRACE(Config, "  Priority order: CLI > Defaults");

    // Load in priority order (lowest to highest)
    LoadDefaults();
    ApplyCommandLine(cliArgs);

    // EngineConfig is a view over ConfigStore's GameConfig — no sync needed.
    // Apply the 3 standalone globals that aren't behind ENGINE_CONFIG.
    ApplyToLegacyGlobConfig();

    m_initialized = true;

    // Log final configuration at DEBUG level
    LOG_DEBUG(Config, "Configuration loaded: {} settings total", m_config.size());
    LOG_INFO(Config, "Configuration system ready");
}

void ConfigurationSystem::Shutdown()
{
    if (m_initialized)
    {
        m_config.clear();
        m_initialized = false;
    }
}

void ConfigurationSystem::SetValue(const char* key, const std::string& value, const char* source)
{
    // Check if we're overriding
    auto it = m_config.find(key);
    if (it != m_config.end())
    {
        LOG_TRACE(Config, "  {} = {} ({}) -> {} ({})", key, it->second.value, it->second.source, value, source);
    }
    else
    {
        LOG_TRACE(Config, "  {} = {} ({})", key, value, source);
    }

    m_config[key] = {value, source};
}

void ConfigurationSystem::LoadDefaults()
{
    LOG_TRACE(Config, "  [1/2] Loading defaults...");

    // EngineConfig already has defaults from its constructor.  Mirror
    // the user-facing keys into the string map so GetInt / GetString
    // continue to work for callers that take the named-key route.
    SetValue("display.width", "800", "default");
    SetValue("display.height", "600", "default");
    SetValue("display.windowed", "false", "default");
    SetValue("display.bpp", "32", "default");
    SetValue("display.refresh", "60", "default");

    SetValue("graphics.lod", "1.0", "default");
    SetValue("graphics.shadows_lod", "0.025", "default");
    SetValue("graphics.terrain_detail", "50.0", "default");
    SetValue("graphics.hw_tnl", "true", "default");

    SetValue("audio.backend", "openal", "default");

    SetValue("language", "English", "default");
    SetValue("network.port", "1985", "default");
    SetValue("network.master_server", DefaultMasterServer, "default");
}

void ConfigurationSystem::ApplyCommandLine(const AppConfig& cliArgs)
{
    LOG_TRACE(Config, "  [2/2] Applying command-line arguments...");

    auto& ec = *m_engineConfig;

    // Runtime flags — only override when CLI flag is actually set
    if (cliArgs.CheckInitAndExit())
        m_runtimeFlags->checkInitAndExit = true;
    if (cliArgs.NoSplash())
        ec.noSplash = true;

    // Display mode: CLI overrides config file only when explicitly provided
    if (cliArgs.IsWindowMode())
    {
        ec.useWindow = true;
        SetValue("display.windowed", "true", "CLI");
    }
    if (cliArgs.IsDisplayModeExplicit())
    {
        ec.displayMode = cliArgs.GetDisplayMode();
        SetValue("display.mode", cliArgs.GetDisplayMode(), "CLI");
    }

    // Derive useWindow from displayMode
    if (ec.displayMode == "windowed")
        ec.useWindow = true;

    if (cliArgs.NoSound())
        ec.noSound = true;
    if (cliArgs.NoLandscape())
        m_runtimeFlags->noLandscape = true;
    if (cliArgs.IsHostServer())
        m_runtimeFlags->hostMultiplayer = true;
    if (cliArgs.EnableHWTL())
    {
        ec.enableHWTL = true;
        SetValue("graphics.hw_tnl", "true", "CLI");
    }
    if (cliArgs.EnablePIII())
        ec.enablePIII = true;
    if (cliArgs.NetLog())
        ec.netLogEnabled = true;

    // Display settings — only when explicitly provided on CLI
    if (cliArgs.IsWidthExplicit())
    {
        ec.wantW = cliArgs.GetWindowWidth();
        SetValue("display.width", std::to_string(cliArgs.GetWindowWidth()), "CLI");
    }
    if (cliArgs.IsHeightExplicit())
    {
        ec.wantH = cliArgs.GetWindowHeight();
        SetValue("display.height", std::to_string(cliArgs.GetWindowHeight()), "CLI");
    }

    // Graphics
    if (cliArgs.NoTextures())
        SetValue("graphics.textures_enabled", "false", "CLI");

    // Audio backend
    const std::string& audioBackend = cliArgs.GetAudioBackend();
    if (!audioBackend.empty())
    {
        ec.requestedAudioBackend = audioBackend;
        SetValue("audio.backend", audioBackend, "CLI");
    }

    // Render backend
    const std::string& renderBackend = cliArgs.GetRenderBackend();
    if (!renderBackend.empty() && renderBackend != "auto")
        SetValue("graphics.backend", renderBackend, "CLI");

    // Network port lives in NetworkConfig (engine/Network); persisted
    // here as a string-map entry that the network init pass reads.
    if (cliArgs.GetNetworkPort() != 1985)
        SetValue("network.port", std::to_string(cliArgs.GetNetworkPort()), "CLI");
    if (cliArgs.IsPrivateServer())
        SetValue("network.master_server", "", "CLI");
    else if (cliArgs.GetMasterServer().GetLength() > 0)
        SetValue("network.master_server", static_cast<const char*>(cliArgs.GetMasterServer()), "CLI");

    LOG_TRACE(Config, "    Command-line overrides applied");
}

int ConfigurationSystem::GetInt(const char* key, int defaultValue) const
{
    auto it = m_config.find(key);
    if (it == m_config.end())
    {
        return defaultValue;
    }
    return atoi(it->second.value.c_str());
}

bool ConfigurationSystem::GetBool(const char* key, bool defaultValue) const
{
    auto it = m_config.find(key);
    if (it == m_config.end())
    {
        return defaultValue;
    }
    const std::string& val = it->second.value;
    return (val == "true" || val == "1" || val == "yes" || val == "on");
}

std::string ConfigurationSystem::GetString(const char* key, const std::string& defaultValue) const
{
    auto it = m_config.find(key);
    if (it == m_config.end())
    {
        return defaultValue;
    }
    return it->second.value;
}

float ConfigurationSystem::GetFloat(const char* key, float defaultValue) const
{
    auto it = m_config.find(key);
    if (it == m_config.end())
    {
        return defaultValue;
    }
    return static_cast<float>(atof(it->second.value.c_str()));
}

bool ConfigurationSystem::HasKey(const char* key) const
{
    return m_config.find(key) != m_config.end();
}

std::string ConfigurationSystem::GetSource(const char* key) const
{
    auto it = m_config.find(key);
    if (it == m_config.end())
    {
        return "not found";
    }
    return it->second.source;
}

bool ConfigurationSystem::InitializeGameConfiguration(const char* language)
{
    LOG_INFO(Config, "Initializing game configuration (language, stringtables, config files)...");

    // Set language
    RString langToUse;
    if (language && language[0] != '\0')
    {
        langToUse = language;
    }
    else
    {
        // Get from config
        std::string configLang = GetString("language", "English");
        langToUse = RString(configLang.c_str());
    }

    std::string normalizedLanguage = CfgLib::NormalizeSupportedLanguage(langToUse.Data());
    Foundation::SetLangID(normalizedLanguage.c_str());
    GLanguage = normalizedLanguage.c_str();
    LOG_INFO(Config, "  Language: {}", normalizedLanguage);

    // Load the base stringtable first, then let mods overlay the keys they
    // actually provide. Older total-conversion mods often ship a partial
    // bin/stringtable.csv; loading only the mod table drops remaster UI keys.
    LOG_TRACE(Config, "  Loading stringtables from mod directories...");
    ParseStringtable("", nullptr);
    ModSystem::EnumDirectories(ParseStringtable, nullptr);

    // Initialize modules (must be after stringtable, before config)
    LOG_TRACE(Config, "  Initializing modules...");
    InitModules();

    // Parse config files from all mod directories.  Result recorded on
    // ConfigSystem — apps without bin/config.* (Tetris) see
    // IsConfigAvailable() == false without ERR noise.
    LOG_TRACE(Config, "  Parsing config.cpp/config.bin from mod directories...");
    const bool configLoaded = ModSystem::EnumDirectories(ParseConfig, nullptr);
    ConfigSystem::Instance().MarkConfigLoaded(configLoaded);

    // Per-game language set/metadata: a CfgLanguages class overrides the built-in defaults. Applied here so
    // it drives the options list, voice metadata, normalization, system-language detection and the codepage
    // for all subsequent stringtable loads (re-mounts, missions, language switches).
    ParamClass& Pars = GetPars();
    if (configLoaded)
    {
        if (const ParamEntry* cfgLangs = Pars.FindEntry("CfgLanguages"))
            CfgLib::LanguageRegistry::Instance().LoadFromConfig(*cfgLangs);
    }

    // Calculate max groups from config (zero when no config is present).
    if (configLoaded)
    {
        MaxGroups = (Pars >> "CfgWorlds" >> "GroupNameList" >> "letters").GetSize() *
                    (Pars >> "CfgWorlds" >> "GroupColorList" >> "colors").GetSize();
    }
    else
    {
        MaxGroups = 0;
    }
    LOG_TRACE(Config, "    MaxGroups: {}", MaxGroups);

    // Initialize statistics system
    Poseidon::GetGStats();
    LOG_TRACE(Config, "    GStats ready");

    // Parse Remaster-only asset catalog (optional; absent on pure original builds).
    LOG_TRACE(Config, "  Parsing remaster.cpp/remaster.bin from mod directories...");
    const bool remasterLoaded = ModSystem::EnumDirectories(ParseRemaster, nullptr);
    ConfigSystem::Instance().MarkRemasterLoaded(remasterLoaded);
    LoadLicensePlateTextTuningFromConfig(remasterLoaded ? Remaster.FindEntry("CfgLicensePlateText") : nullptr);
    if (remasterLoaded && GetSource("network.master_server") != "CLI")
    {
        if (const ParamEntry* cfgNetwork = Remaster.FindEntry("CfgNetwork"))
        {
            if (const ParamEntry* masterServer = cfgNetwork->FindEntry("masterServer"))
            {
                const RString value = masterServer->GetValue();
                if (value.GetLength() > 0)
                {
                    SetValue("network.master_server", value.Data(), "remaster.cpp");
                    SetNetworkMasterServer(value);
                }
            }
        }
    }

    // Parse resource files from all mod directories.
    LOG_TRACE(Config, "  Parsing resource.cpp/resource.bin from mod directories...");
    const bool resourceLoaded = ModSystem::EnumDirectories(ParseResource, nullptr);
    ConfigSystem::Instance().MarkResourceLoaded(resourceLoaded);

    // A community mod that ships its own bin/resource replaces the base menu
    // resource (enumeration stops at the first mod), shadowing the remaster UI
    // additions — leaving the new Options screen empty and the Mods entry gone.
    // Restore them on top of the mod's resource so the remaster UI always works.
    if (IsMenuOverriddenByMod())
        MergeBaseResourceExtra();

    // Load user configuration (difficulty, UI preferences)
    m_userConfig->LoadFromFile(::Poseidon::GetUserParams().Data());

    LOG_INFO(Config, "Game configuration initialized successfully");

    return true;
}

void ConfigurationSystem::ApplyToLegacyGlobConfig()
{
    LOG_TRACE(Config, "Applying ConfigurationSystem values to legacy Config struct...");

    extern void ApplyConfigToGlobConfig(const ConfigurationSystem& config);
    ApplyConfigToGlobConfig(*this);

    LOG_TRACE(Config, "  Legacy Config struct updated from ConfigurationSystem");
}
} // namespace Poseidon
