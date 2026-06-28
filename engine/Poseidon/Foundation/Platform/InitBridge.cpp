#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Foundation/Platform/InitBridge.hpp> // For function declarations
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/ModSystem.hpp>
#include <Poseidon/Core/Global.hpp> // For Glob
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/UI/Locale/Stringtable/Stringtable.hpp> // For GLanguage macro
#include <Poseidon/Core/Types.hpp>
#include <Poseidon/AI/AI.hpp>              // For AIStats
#include <Poseidon/Core/Config/Config.hpp> // For FlashpointCfg macro
#include <Poseidon/UI/Locale/SupportedLanguages.hpp>
#include <Poseidon/Audio/AudioFactory.hpp>
#include <Poseidon/Audio/SoundScene.hpp>
#include <Poseidon/World/World.hpp>
#include <stdlib.h>
#include <string>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Memory/CheckMem.hpp>
#include <Poseidon/Foundation/Modules/Modules.hpp>
#include <Poseidon/Foundation/Strings/Mbcs.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/platform.hpp>

using Poseidon::ParseConfig;
using Poseidon::ParseRemaster;
using Poseidon::ParseResource;
using Poseidon::ParseStringtable;

using namespace Poseidon;
void InitCapsLock()
{
#if _ENABLE_CHEATS
    extern KeyLights KeyState;
    KeyState.SetCapsLock(false);
#endif
}

// Older config path; prefer ConfigurationSystem::InitializeGameConfiguration.
bool InitLanguageAndConfig()
{
    extern int MaxGroups;

    // Load language from config (runtime, no compile-time language forcing)
    GLanguage = "English";
    ParamFile cfg;
    cfg.Parse(FlashpointCfg);
    ParamEntry* cfgLang = cfg.FindEntry("language");
    if (cfgLang)
        GLanguage = *cfgLang;

    const std::string normalizedLanguage = CfgLib::NormalizeSupportedLanguage((const char*)GLanguage);
    GLanguage = normalizedLanguage.c_str();
    SetLangID(GLanguage);

    // Load stringtables from all mod directories
    DoVerify(ModSystem::EnumDirectories(ParseStringtable, nullptr));

    // Initialize modules (must be after stringtable, before config)
    InitModules();

    LOG_DEBUG(Core, "Before config parsing: Total allocated: {}", Poseidon::Foundation::MemoryUsed() / 1024);

    // Parse config files from all mod directories
    DoVerify(ModSystem::EnumDirectories(ParseConfig, nullptr));

    // Calculate max groups from config
    MaxGroups = (Pars >> "CfgWorlds" >> "GroupNameList" >> "letters").GetSize() *
                (Pars >> "CfgWorlds" >> "GroupColorList" >> "colors").GetSize();

    // Initialize statistics system
    GStats.Init();

    // Parse Remaster-only asset catalog (optional; absent on pure original builds).
    DoVerify(ModSystem::EnumDirectories(ParseRemaster, nullptr));

    // Parse resource files from all mod directories
    DoVerify(ModSystem::EnumDirectories(ParseResource, nullptr));
    LOG_DEBUG(Core, "After config parsing: Total allocated: {}", Poseidon::Foundation::MemoryUsed() / 1024);

    return true;
}

void ApplyConfigurationFlags()
{
    // No GUI — must be a dedicated server.
    if (ENGINE_CONFIG.doCreateDedicatedServer)
    {
        ENGINE_CONFIG.enableHWTL = false;
        ENGINE_CONFIG.gMergeTextures = false;
    }

    if (ENGINE_CONFIG.enablePIII)
    {
        SetFlushToZero();
    }
}

// Sound system initialization wrappers

// IAudioSystem forward declaration is in AudioFactory.hpp (included above)

IAudioSystem* CreateAudioSystem(void* hwnd, bool noSound, bool isDedicatedServer)
{
    bool useDummy = noSound || isDedicatedServer;
    const std::string requestedBackend = ENGINE_CONFIG.requestedAudioBackend;
    const bool explicitSelection = !requestedBackend.empty() && _stricmp(requestedBackend.c_str(), "auto") != 0;
    LOG_INFO(Audio, "Audio backend request: requested='{}' noSound={} dedicated={} available={}",
             requestedBackend.empty() ? "auto" : requestedBackend.c_str(), noSound ? "yes" : "no",
             isDedicatedServer ? "yes" : "no", AudioFactory::DescribeAvailableCodes());
    AudioCreateResult created = AudioFactory::Create(requestedBackend, useDummy);

    if (!explicitSelection)
    {
        if (created.system == nullptr)
        {
            LOG_ERROR(Audio, "Failed to create any audio backend. Registered: {}",
                      AudioFactory::DescribeRegisteredCodes());
            return nullptr;
        }

        if (useDummy)
        {
            LOG_INFO(Audio, "Audio backend: {} (auto-selected: nosound={}, dedicated={})", created.backend.displayName,
                     noSound, isDedicatedServer);
        }
        else
        {
            LOG_INFO(Audio, "Audio backend: {} (default)", created.backend.displayName);
        }
        return created.system;
    }

    if (created.system == nullptr)
    {
        switch (created.status)
        {
            case AudioCreateStatus::UnknownBackend:
                LOG_ERROR(Audio, "Unknown audio backend: '{}'. Available: -audio={}", requestedBackend,
                          AudioFactory::DescribeRegisteredCodes());
                break;
            case AudioCreateStatus::Unavailable:
                LOG_ERROR(Audio, "Requested audio backend '{}' is not available. Available: -audio={}",
                          requestedBackend, AudioFactory::DescribeAvailableCodes());
                break;
            case AudioCreateStatus::CreateFailed:
                LOG_ERROR(Audio, "Failed to create audio backend '{}'. Available: -audio={}", requestedBackend,
                          AudioFactory::DescribeRegisteredCodes());
                break;
            case AudioCreateStatus::Created:
                break;
        }
        exit(1);
    }

    LOG_INFO(Audio, "Audio backend: {} (command-line)", created.backend.displayName);
    return created.system;
}

SoundScene* CreateSoundScene()
{
    return new SoundScene;
}

void World_UnloadSounds(World* world)
{
    if (world)
    {
        world->UnloadSounds();
    }
}

void CleanupSoundSystem()
{
    if (GWorld)
    {
        GWorld->UnloadSounds();
    }
    if (GSoundScene)
    {
        delete GSoundScene;
        GSoundScene = nullptr;
    }
    if (GSoundsys)
    {
        delete GSoundsys;
        GSoundsys = nullptr;
    }
}
