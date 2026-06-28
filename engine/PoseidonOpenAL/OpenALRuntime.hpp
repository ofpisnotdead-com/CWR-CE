#pragma once

#define AL_ALEXT_PROTOTYPES
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/efx.h>

#ifdef _WIN32
#include <Poseidon/Foundation/Common/Win.h>
#else
#include <dlfcn.h>
#endif

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#include <string>

namespace OpenALRuntime
{
// Core AL/ALC entry points every backend (OpenAL Soft, Apple's system
// OpenAL.framework, ...) implements. Missing any of these means there's no
// usable OpenAL at all.
#define OPENAL_CORE_FUNCTIONS(X)                                                                                       \
    X(alBufferData)                                                                                                     \
    X(alDeleteBuffers)                                                                                                  \
    X(alDeleteSources)                                                                                                  \
    X(alDistanceModel)                                                                                                  \
    X(alDopplerFactor)                                                                                                  \
    X(alGenBuffers)                                                                                                     \
    X(alGenSources)                                                                                                     \
    X(alGetError)                                                                                                       \
    X(alGetSourcef)                                                                                                     \
    X(alGetSourcei)                                                                                                     \
    X(alListener3f)                                                                                                     \
    X(alListenerf)                                                                                                      \
    X(alListenerfv)                                                                                                     \
    X(alSource3f)                                                                                                       \
    X(alSource3i)                                                                                                       \
    X(alSourcePause)                                                                                                    \
    X(alSourcePlay)                                                                                                     \
    X(alSourceQueueBuffers)                                                                                             \
    X(alSourceRewind)                                                                                                   \
    X(alSourceStop)                                                                                                     \
    X(alSourceUnqueueBuffers)                                                                                           \
    X(alSourcef)                                                                                                        \
    X(alSourcei)                                                                                                        \
    X(alcCaptureCloseDevice)                                                                                            \
    X(alcCaptureOpenDevice)                                                                                             \
    X(alcCaptureSamples)                                                                                                \
    X(alcCaptureStart)                                                                                                  \
    X(alcCaptureStop)                                                                                                   \
    X(alcCloseDevice)                                                                                                   \
    X(alcCreateContext)                                                                                                 \
    X(alcDestroyContext)                                                                                                \
    X(alcGetIntegerv)                                                                                                   \
    X(alcGetString)                                                                                                     \
    X(alcIsExtensionPresent)                                                                                            \
    X(alcMakeContextCurrent)                                                                                            \
    X(alcOpenDevice)                                                                                                    \
    X(alcProcessContext)                                                                                                \
    X(alcSuspendContext)

// EFX (ALC_EXT_EFX, used for EAX-reverb presets) is an optional extension.
// Apple's system OpenAL.framework (core AL 1.1 only) doesn't export these.
// SoundSystemOAL already gates every call behind a runtime
// alcIsExtensionPresent(ALC_EXT_EFX) check and never touches these pointers
// unless that check passed, so it's safe to leave them null when missing.
#define OPENAL_EFX_FUNCTIONS(X)                                                                                        \
    X(alAuxiliaryEffectSloti)                                                                                           \
    X(alDeleteAuxiliaryEffectSlots)                                                                                     \
    X(alDeleteEffects)                                                                                                  \
    X(alEffectf)                                                                                                        \
    X(alEffectfv)                                                                                                       \
    X(alEffecti)                                                                                                        \
    X(alGenAuxiliaryEffectSlots)                                                                                        \
    X(alGenEffects)

#define OPENAL_RUNTIME_FUNCTIONS(X)                                                                                     \
    OPENAL_CORE_FUNCTIONS(X)                                                                                            \
    OPENAL_EFX_FUNCTIONS(X)

struct Api
{
#define DECLARE_OPENAL_FUNCTION(name) decltype(&::name) name = nullptr;
    OPENAL_RUNTIME_FUNCTIONS(DECLARE_OPENAL_FUNCTION)
#undef DECLARE_OPENAL_FUNCTION
};

namespace detail
{
inline Api& GetApiStorage()
{
    static Api api;
    return api;
}

inline bool& LoadAttempted()
{
    static bool attempted = false;
    return attempted;
}

inline bool& LoadSucceeded()
{
    static bool loaded = false;
    return loaded;
}

inline std::string& LastErrorStorage()
{
    static std::string error;
    return error;
}

inline void*& ModuleHandle()
{
    static void* module = nullptr;
    return module;
}

inline void SetError(const char* message)
{
    LastErrorStorage() = message ? message : "Unknown OpenAL runtime error";
}

inline void ResetApi()
{
    GetApiStorage() = Api{};
}

inline void UnloadModule()
{
    if (ModuleHandle() == nullptr)
        return;
#if defined(__APPLE__) && TARGET_OS_IPHONE
    // iOS: ModuleHandle() is the RTLD_DEFAULT sentinel (see TryLoadModule),
    // not a real dlopen handle -- there's nothing to dlclose.
    ModuleHandle() = nullptr;
    return;
#endif
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(ModuleHandle()));
#else
    dlclose(ModuleHandle());
#endif
    ModuleHandle() = nullptr;
}

inline void* LookupSymbol(const char* name)
{
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(ModuleHandle()), name));
#else
    return dlsym(ModuleHandle(), name);
#endif
}

inline bool TryLoadModule()
{
#ifdef _WIN32
    ModuleHandle() = static_cast<void*>(LoadLibraryA("OpenAL32.dll"));
    if (ModuleHandle() == nullptr)
    {
        SetError("OpenAL32.dll is not available");
        return false;
    }
#elif defined(__APPLE__) && TARGET_OS_IPHONE
    // iOS/Simulator sandboxing prohibits dlopen of anything outside the app
    // bundle, so there's no equivalent of macOS's Homebrew/system-framework
    // probing below. openal-soft is link-time static for this platform (see
    // cmake/vcpkg-triplets/arm64-ios-simulator.cmake) -- its symbols are
    // already part of this binary, so "loading" is just resolving them out
    // of the global symbol table via the RTLD_DEFAULT sentinel handle.
    ModuleHandle() = RTLD_DEFAULT;
#elif defined(__APPLE__)
    // Prefer a real openal-soft if the user has one (Homebrew installs it
    // keg-only since macOS already ships OpenAL.framework), then fall back
    // to Apple's built-in framework, which is always present but core-only
    // (no EFX -- see OPENAL_EFX_FUNCTIONS above).
    static const char* const candidates[] = {
        "libopenal.1.dylib",
        "libopenal.dylib",
        "/opt/homebrew/opt/openal-soft/lib/libopenal.1.dylib",
        "/usr/local/opt/openal-soft/lib/libopenal.1.dylib",
        "/System/Library/Frameworks/OpenAL.framework/OpenAL",
    };
    for (const char* candidate : candidates)
    {
        ModuleHandle() = dlopen(candidate, RTLD_NOW | RTLD_LOCAL);
        if (ModuleHandle() != nullptr)
            break;
    }
    if (ModuleHandle() == nullptr)
    {
        SetError("No OpenAL implementation found (tried openal-soft and the system OpenAL.framework)");
        return false;
    }
#else
    ModuleHandle() = dlopen("libopenal.so.1", RTLD_NOW | RTLD_LOCAL);
    if (ModuleHandle() == nullptr)
        ModuleHandle() = dlopen("libopenal.so", RTLD_NOW | RTLD_LOCAL);
    if (ModuleHandle() == nullptr)
    {
        SetError("libopenal.so is not available");
        return false;
    }
#endif
    return true;
}

inline bool ResolveFunctions()
{
    Api resolved;
#if defined(__APPLE__) && TARGET_OS_IPHONE
#define RESOLVE_REQUIRED_OPENAL_FUNCTION(name) resolved.name = &::name;
    OPENAL_CORE_FUNCTIONS(RESOLVE_REQUIRED_OPENAL_FUNCTION)
#undef RESOLVE_REQUIRED_OPENAL_FUNCTION

#define RESOLVE_OPTIONAL_OPENAL_FUNCTION(name) resolved.name = reinterpret_cast<decltype(resolved.name)>(LookupSymbol(#name));
    OPENAL_EFX_FUNCTIONS(RESOLVE_OPTIONAL_OPENAL_FUNCTION)
#undef RESOLVE_OPTIONAL_OPENAL_FUNCTION
#else
#define RESOLVE_REQUIRED_OPENAL_FUNCTION(name)                                                                         \
    resolved.name = reinterpret_cast<decltype(resolved.name)>(LookupSymbol(#name));                                    \
    if (resolved.name == nullptr)                                                                                       \
    {                                                                                                                   \
        SetError("Missing OpenAL symbol: " #name);                                                                      \
        return false;                                                                                                   \
    }
    OPENAL_CORE_FUNCTIONS(RESOLVE_REQUIRED_OPENAL_FUNCTION)
#undef RESOLVE_REQUIRED_OPENAL_FUNCTION

#define RESOLVE_OPTIONAL_OPENAL_FUNCTION(name)                                                                         \
    resolved.name = reinterpret_cast<decltype(resolved.name)>(LookupSymbol(#name));
    OPENAL_EFX_FUNCTIONS(RESOLVE_OPTIONAL_OPENAL_FUNCTION)
#undef RESOLVE_OPTIONAL_OPENAL_FUNCTION
#endif

    GetApiStorage() = resolved;
    return true;
}
} // namespace detail

inline bool EnsureLoaded()
{
    if (detail::LoadAttempted())
        return detail::LoadSucceeded();

    detail::LoadAttempted() = true;
    if (!detail::TryLoadModule())
        return false;
    if (!detail::ResolveFunctions())
    {
        detail::ResetApi();
        detail::UnloadModule();
        return false;
    }

    detail::LoadSucceeded() = true;
    detail::LastErrorStorage().clear();
    return true;
}

inline bool IsAvailable()
{
    return EnsureLoaded();
}

inline const char* LastError()
{
    return detail::LastErrorStorage().c_str();
}

inline const Api& GetApi()
{
    return detail::GetApiStorage();
}

#undef OPENAL_RUNTIME_FUNCTIONS
} // namespace OpenALRuntime

#define alAuxiliaryEffectSloti OpenALRuntime::GetApi().alAuxiliaryEffectSloti
#define alBufferData OpenALRuntime::GetApi().alBufferData
#define alDeleteAuxiliaryEffectSlots OpenALRuntime::GetApi().alDeleteAuxiliaryEffectSlots
#define alDeleteBuffers OpenALRuntime::GetApi().alDeleteBuffers
#define alDeleteEffects OpenALRuntime::GetApi().alDeleteEffects
#define alDeleteSources OpenALRuntime::GetApi().alDeleteSources
#define alDistanceModel OpenALRuntime::GetApi().alDistanceModel
#define alDopplerFactor OpenALRuntime::GetApi().alDopplerFactor
#define alEffectf OpenALRuntime::GetApi().alEffectf
#define alEffectfv OpenALRuntime::GetApi().alEffectfv
#define alEffecti OpenALRuntime::GetApi().alEffecti
#define alGenAuxiliaryEffectSlots OpenALRuntime::GetApi().alGenAuxiliaryEffectSlots
#define alGenBuffers OpenALRuntime::GetApi().alGenBuffers
#define alGenEffects OpenALRuntime::GetApi().alGenEffects
#define alGenSources OpenALRuntime::GetApi().alGenSources
#define alGetError OpenALRuntime::GetApi().alGetError
#define alGetSourcef OpenALRuntime::GetApi().alGetSourcef
#define alGetSourcei OpenALRuntime::GetApi().alGetSourcei
#define alListener3f OpenALRuntime::GetApi().alListener3f
#define alListenerf OpenALRuntime::GetApi().alListenerf
#define alListenerfv OpenALRuntime::GetApi().alListenerfv
#define alSource3f OpenALRuntime::GetApi().alSource3f
#define alSource3i OpenALRuntime::GetApi().alSource3i
#define alSourcePause OpenALRuntime::GetApi().alSourcePause
#define alSourcePlay OpenALRuntime::GetApi().alSourcePlay
#define alSourceQueueBuffers OpenALRuntime::GetApi().alSourceQueueBuffers
#define alSourceRewind OpenALRuntime::GetApi().alSourceRewind
#define alSourceStop OpenALRuntime::GetApi().alSourceStop
#define alSourceUnqueueBuffers OpenALRuntime::GetApi().alSourceUnqueueBuffers
#define alSourcef OpenALRuntime::GetApi().alSourcef
#define alSourcei OpenALRuntime::GetApi().alSourcei
#define alcCaptureCloseDevice OpenALRuntime::GetApi().alcCaptureCloseDevice
#define alcCaptureOpenDevice OpenALRuntime::GetApi().alcCaptureOpenDevice
#define alcCaptureSamples OpenALRuntime::GetApi().alcCaptureSamples
#define alcCaptureStart OpenALRuntime::GetApi().alcCaptureStart
#define alcCaptureStop OpenALRuntime::GetApi().alcCaptureStop
#define alcCloseDevice OpenALRuntime::GetApi().alcCloseDevice
#define alcCreateContext OpenALRuntime::GetApi().alcCreateContext
#define alcDestroyContext OpenALRuntime::GetApi().alcDestroyContext
#define alcGetIntegerv OpenALRuntime::GetApi().alcGetIntegerv
#define alcGetString OpenALRuntime::GetApi().alcGetString
#define alcIsExtensionPresent OpenALRuntime::GetApi().alcIsExtensionPresent
#define alcMakeContextCurrent OpenALRuntime::GetApi().alcMakeContextCurrent
#define alcOpenDevice OpenALRuntime::GetApi().alcOpenDevice
#define alcProcessContext OpenALRuntime::GetApi().alcProcessContext
#define alcSuspendContext OpenALRuntime::GetApi().alcSuspendContext
