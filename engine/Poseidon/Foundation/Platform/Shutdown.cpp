#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <Poseidon/Foundation/Platform/AppConfig.hpp>
#include <Poseidon/Core/Config/Config.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/platform.hpp>

#ifdef _WIN32
#include <Poseidon/Foundation/Common/Win.h>
#include <io.h>
#include <process.h>
#else
#include <unistd.h>
#endif
#include <Poseidon/Core/resrc1.h>

#include <Poseidon/Core/Global.hpp>

#include <Poseidon/Core/Application.hpp>

using namespace Poseidon;
extern void SetSkipKeys(bool skip);

extern void AppPause(bool f);
namespace Poseidon
{
extern void DestroyEngine();
}

#include <Poseidon/Core/Game/GameLoop.hpp>

namespace Poseidon
{
extern RString GetPidFileName();
extern bool GetMyPidFile();
} // namespace Poseidon

#include <Poseidon/World/World.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/AI/ArcadeTemplate.hpp>

#include <Poseidon/Dev/Debug/DebugTrap.hpp>

using namespace Poseidon::Dev;

#include <Poseidon/Audio/IAudioSystem.hpp>
#include <Poseidon/Audio/SoundScene.hpp>
#include <Poseidon/Graphics/Rendering/Draw/FontSystem.hpp>
#include <Poseidon/World/Scene/ScenePreloader.hpp>

#include <Poseidon/IO/ParamFile/ParamFile.hpp>

#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Network/NetworkConfig.hpp>

namespace Poseidon
{
struct ::ArcadeTemplate& GetGClipboard();
}

static bool MouseEx = true;

// Win32 HINSTANCE — used by Dev/Debug/DebugWin.cpp for CreateWindowEx
#ifdef _WIN32
HINSTANCE GHInstance = nullptr;
#endif

static bool fMouseAcquired = true;
bool IsMouseAcquired()
{
    return fMouseAcquired;
}
void SetMouseAcquired(bool acquired)
{
    fMouseAcquired = acquired;
}

#if _ENABLE_CHEATS
#include <Poseidon/Input/KeyLights.hpp>

KeyLights KeyState;
#endif

bool Benchmark = false;
bool AutoTest = false;

const char* ErrorString;

#ifdef _WIN32
RECT clientRect;
#else
struct
{
    long left, top, right, bottom;
} clientRect;
#endif

char LoadFile[512];

extern float Height;

ArcadeTemplate& GetGClipboard();

void ManCleanUp();

// Tears down the whole game-data layer (world, scene, sound, banks, configs,
// caches) in the order the engine has always used. When keepEngine is true the
// graphics engine — the SDL window + RHI device — is left alive; that single
// skipped DestroyEngine() is the only difference between a mod re-mount (which
// reloads onto the same window) and a quit-to-desktop.
void UnloadGameData(bool keepEngine)
{
    GDebugger.PauseCheckingAlive();
    if (GEngine)
    {
        GEngine->ReinitCounters();
    }

    GApp->m_canRender = false;
    markersMap.Clear();
    CurrentTemplate.Clear();
    Poseidon::GetGClipboard().Clear();
    if (GEngine)
    {
        GEngine->StopAll();
    }
    if (GWorld)
    {
        delete GWorld, GWorld = nullptr;
    }
    // GScene aliases GWorld->_scene (a value member of World, assigned via GScene = GetScene()).
    // Deleting GWorld already destroyed that Scene; GScene is not separately heap-allocated, so
    // it must only be cleared here — a delete is a double-free + use-after-free of the World
    // (Scene::~Scene re-runs the landscape teardown and SaveConfig() reads the freed World).
    GScene = nullptr;
    if (GEngine)
    {
        GEngine->ClearFontCache();
    }
    FontSystem::Instance().Shutdown();
    // ScenePreloader is a process-lifetime singleton with an _initialized flag;
    // without this Shutdown the next Initialize(newScene) after a re-mount
    // early-returns and leaves the fresh Scene's _preloaded[] all-null. First
    // explosion then dereferences a null CraterShell in Collisions.cpp.
    ScenePreloader::Instance().Shutdown();
    GPreloadedTextures.Clear();
    // Release the sound scene's waves (env sounds, global sounds, music track) while the
    // sound system that owns their OpenAL buffers/sources is still alive. GSoundScene is a
    // global that outlives a re-mount (only CleanupSoundSystem deletes it, and that runs in
    // DestroyEngine, which keepEngine skips). Without this, the waves linger past the
    // delete GSoundsys below; the next World ctor's GSoundScene->Reset() then destroys them
    // and ~WaveOAL calls SoundSystemOAL::UnregisterWave on the freed system (AV in std::find).
    if (GSoundScene)
    {
        GSoundScene->Reset();
    }
    if (GSoundsys)
    {
        delete GSoundsys, GSoundsys = nullptr;
    }
    ClearStringtable();
    ManCleanUp();
    Glob.Clear();
    if (!keepEngine)
    {
        Poseidon::DestroyEngine();
    }
    Pars.Clear();
    Pars.DeleteVariables();
    ExtParsCampaign.Clear();
    ExtParsCampaign.DeleteVariables();
    ExtParsMission.Clear();
    ExtParsMission.DeleteVariables();
    Res.Clear();
    Res.DeleteVariables();
    GetNetworkManager().Done();
}

void DDTerm()
{
    UnloadGameData(/*keepEngine*/ false);
    if (Poseidon::GetMyPidFile())
    {
        DeleteFile((const char*)Poseidon::GetPidFileName());
    }
}
