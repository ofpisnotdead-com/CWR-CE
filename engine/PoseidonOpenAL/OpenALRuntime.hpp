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

#include <string>

namespace OpenALRuntime
{
#define OPENAL_RUNTIME_FUNCTIONS(X)                                                                                     \
    X(alAuxiliaryEffectSloti)                                                                                           \
    X(alBufferData)                                                                                                     \
    X(alDeleteAuxiliaryEffectSlots)                                                                                     \
    X(alDeleteBuffers)                                                                                                  \
    X(alDeleteEffects)                                                                                                  \
    X(alDeleteSources)                                                                                                  \
    X(alDistanceModel)                                                                                                  \
    X(alDopplerFactor)                                                                                                  \
    X(alEffectf)                                                                                                        \
    X(alEffectfv)                                                                                                       \
    X(alEffecti)                                                                                                        \
    X(alGenAuxiliaryEffectSlots)                                                                                        \
    X(alGenBuffers)                                                                                                     \
    X(alGenEffects)                                                                                                     \
    X(alGenSources)                                                                                                     \
    X(alGetError)                                                                                                       \
    X(alGetSourcef)                                                                                                     \
    X(alGetSource3f)                                                                                                    \
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
#define RESOLVE_OPENAL_FUNCTION(name)                                                                                   \
    resolved.name = reinterpret_cast<decltype(resolved.name)>(LookupSymbol(#name));                                    \
    if (resolved.name == nullptr)                                                                                       \
    {                                                                                                                   \
        SetError("Missing OpenAL symbol: " #name);                                                                      \
        return false;                                                                                                   \
    }
    OPENAL_RUNTIME_FUNCTIONS(RESOLVE_OPENAL_FUNCTION)
#undef RESOLVE_OPENAL_FUNCTION

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
#define alGetSource3f OpenALRuntime::GetApi().alGetSource3f
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
