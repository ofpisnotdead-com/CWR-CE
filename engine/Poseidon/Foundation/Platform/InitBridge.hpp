#pragma once

// Windows types
#ifdef _WIN32
struct HINSTANCE__;
typedef HINSTANCE__* HINSTANCE;
#else
#include <Poseidon/Foundation/platform.hpp>
#endif

// String types
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Strings/Mbcs.hpp>

// Forward declarations for complex types
namespace Poseidon { class KeyLights; }
using Poseidon::KeyLights;
namespace Poseidon { class AIStats; }

// Initialization functions
void SetMemorySystemReady(bool ready);
namespace Poseidon
{
bool ParseStringtable(RStringB dir, void* context);
bool ParseConfig(RStringB dir, void* context);
bool ParseRemaster(RStringB dir, void* context);
bool ParseResource(RStringB dir, void* context);

/// True when a mod's bin/resource replaced the base menu resource (it won the
/// ParseResource enumeration). The main menu reads this to keep + hijack the
/// community addon's custom menu instead of flipping to the remaster menu.
bool IsMenuOverriddenByMod();
/// Merge the base game's resource-extra.cpp (remaster UI additions:
/// RscOptionsShell, RscDisplayMainRemaster, …) on top of Res, restoring resources
/// a mod's bin/resource shadowed.
void MergeBaseResourceExtra();

/// True when a mod's bin/config replaced the base master config (it won the ParseConfig
/// enumeration). Config init reads this to restore the base config-extra a mod's config shadowed.
bool IsConfigOverriddenByMod();
/// Merge the base game's config-extra.cpp (remaster config additions: CfgLanguages) on top of
/// Pars, restoring config a mod's bin/config shadowed.
void MergeBaseConfigExtra();
} // namespace Poseidon
#include <Poseidon/Foundation/Modules/Modules.hpp>
namespace Poseidon { class ParamFile; }
void SetFlushToZero();
void CheckStringtable(const char* lang);

// Graphics engine initialization functions  
void InitMan();
void InitWorld();

// Forward declarations for opaque types (Core doesn't need to know internals)
struct CreateEnginePars;
namespace Poseidon { class World; }
using Poseidon::World;
namespace Poseidon { class Landscape; }
using Poseidon::Landscape;
namespace Poseidon { class Script; }
namespace Poseidon { class Engine; }

// Engine creation functions (D3D9 — defined in GraphicsEngineFactory)

// Wrappers that let Core call graphics/world subsystems without pulling in their full headers.
void AI_InitTables();
void GStats_ClearAll();
void Glob_Init();
int Glob_GetWantW();
int Glob_GetWantH();
int Glob_GetWantBpp();
void Glob_SetWantW(int w);
void Glob_SetWantH(int h);
void Glob_SetPlayerName(const RString& name);
void GPreloadedTextures_Preload(bool b);
World* CreateWorld(::Poseidon::Engine* engine, bool landEditor);
Landscape* CreateLandscape(::Poseidon::Engine* engine, World* world, bool b);
void World_InitLandscape(World* world, Landscape* landscape);
void VehicleTypes_Preload();
int VehicleTypes_AuditEditorVisibleModels();
void GFileServer_FlushBank();
Poseidon::Script* CreateProgressScript(const char* filename);
void ProgressStart_Wrapper(int stringId); // localizes stringId, then calls ProgressStart
int RString_GetLength(const void* rstring);
const void* ClientIP_GetPtr();
::Poseidon::Engine* CreateEngineWithParams(void* hInstance, int showCmd);
// I_AM_ALIVE() is a macro defined in GlobalAlive.hpp; no function declaration needed
void SetProgressScript(Poseidon::Script* script); // ProgressScript is a Ref<Script>, not a raw pointer
void SetGFileBankPrefix(const char* prefix);

// Continuation function from main.cpp
int GameMain_ContinueAfterConfig(HINSTANCE hInst, HINSTANCE hPrev, char* szCmdLine, int sw);

// Main loop and shutdown
void DDTerm();

// Wrapper functions from mainInit.cpp (allow Core to call without full headers)
void InitCapsLock();
bool InitLanguageAndConfig();
void ApplyConfigurationFlags();

// Global variables
// GLanguage is a macro that calls GetLanguage() - see stringtable.hpp
extern const char* FlashpointCfg;
extern int MaxGroups;
// GStats is a macro that calls GetGStats() - see ai.hpp
extern KeyLights KeyState;
// GMergeTextures lives in ENGINE_CONFIG.gMergeTextures, not as a global here.
extern bool netLogValid;

// Graphics engine globals (incomplete types - Core doesn't access internals)
namespace Poseidon
{
extern World* GWorld;
extern Landscape* GLandscape;
}
using Poseidon::GWorld;
using Poseidon::GLandscape;

// Sound system globals
namespace Poseidon
{
class IAudioSystem;
class SoundScene;
extern IAudioSystem* GSoundsys;
extern SoundScene* GSoundScene;
}
using Poseidon::IAudioSystem;
using Poseidon::SoundScene;
using Poseidon::GSoundsys;
using Poseidon::GSoundScene;

// Forward declarations for complex types (minimal to avoid full headers in Core)
// Note: These are intentionally incomplete - Core code should not access internals

// Functions that Core can call
void AI_InitTables(); // distinct symbol from AI::InitTables() to avoid an AI namespace/class conflict
namespace Poseidon { class IFilebankEncryption; }
using Poseidon::IFilebankEncryption;
#ifndef _WIN32
void NetLog(const char* format, ...);
#endif

// Sound system initialization wrappers
IAudioSystem* CreateAudioSystem(void* hwnd, bool noSound, bool isDedicatedServer);
SoundScene* CreateSoundScene();
void World_UnloadSounds(World* world);
void CleanupSoundSystem(); // deletes the sound system here, where its type is complete

// IDS_LOAD_INIT comes from the registered globals in engine/Poseidon/UI/Locale/
// stringtableExt.cpp via STRING(LOAD_INIT).
#include <Poseidon/UI/Locale/StringtableExt.hpp>

