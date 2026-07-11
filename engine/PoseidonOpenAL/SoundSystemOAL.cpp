#include <Poseidon/Foundation/Common/Global.hpp>
#include <PoseidonOpenAL/SoundSystemOAL.hpp>
#include <PoseidonOpenAL/WaveOAL.hpp>
#include <PoseidonOpenAL/EFXPresets.hpp>
#include <PoseidonOpenAL/IosAudioSession.hpp>
#include <Poseidon/Audio/Streaming/WaveLoaders.hpp>
#include <Poseidon/Audio/Streaming/WaveStream.hpp>
#include <Poseidon/Audio/Shared/DX8Reference.hpp>
#include <Poseidon/Audio/Core/WaveLifecycle.hpp>
#include <Poseidon/Audio/AudioFactory.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/TaskPool.hpp>
#include <Poseidon/UI/Settings/AudioConfig.hpp>
#include <PoseidonOpenAL/OpenALRuntime.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/IO/ParamFileExt.hpp>
#include <Poseidon/Audio/Voice/VonCapture.hpp>
#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <Poseidon/Foundation/Memory/CheckMem.hpp>
#include <cmath>
#include <algorithm>
#include <string.h>

// Global config singleton (defined in Foundation/Platform/Globals.cpp).
extern Poseidon::ParamFile Pars;

#include <Poseidon/Foundation/Framework/AppFrame.hpp>

namespace Poseidon
{

using Poseidon::Foundation::IsOutOfMemory;
const ParamEntry* FindMusic(RString name, SoundPars& pars);

namespace
{
const ParamEntry* PreviewConfig()
{
    const ParamEntry* sfx = Pars.FindEntry("CfgSFX");
    return sfx ? sfx->FindEntry("Preview") : nullptr;
}

bool ResolvePreviewMusicTrack(const char* entryName, SoundPars& pars)
{
    const ParamEntry* preview = PreviewConfig();
    const ParamEntry* entry = preview ? preview->FindEntry(entryName) : nullptr;
    if (!entry)
        return false;

    RString trackName = *entry;
    return trackName.GetLength() > 0 && FindMusic(trackName, pars) != nullptr && pars.name.GetLength() > 0;
}

bool ResolvePreviewSoundArray(const char* entryName, SoundPars& pars)
{
    const ParamEntry* preview = PreviewConfig();
    const ParamEntry* entry = preview ? preview->FindEntry(entryName) : nullptr;
    if (!entry || !GetValue(pars, *entry) || pars.name.GetLength() <= 0)
        return false;

    RString raw = (*entry)[0];
    const char* rawPath = raw;
    while (*rawPath == '\\' || *rawPath == '/')
        ++rawPath;

    // CfgSFX preview arrays are global-package samples, not mission-relative
    // sounds. GetSoundName() correctly maps weapon effects into DTA/Sound.pbo,
    // but legacy Preview entries also use "voice\..." and "music\..." for
    // DTA/Voice.pbo and DTA/Music.pbo. Keep those rooted at their own banks
    // instead of letting GetSoundName() rewrite them under "sound\".
    if (_strnicmp(rawPath, "voice\\", 6) == 0 || _strnicmp(rawPath, "voice/", 6) == 0 ||
        _strnicmp(rawPath, "music\\", 6) == 0 || _strnicmp(rawPath, "music/", 6) == 0)
    {
        char normalized[256];
        strncpy(normalized, rawPath, sizeof(normalized) - 1);
        normalized[sizeof(normalized) - 1] = 0;
        for (char* c = normalized; *c; ++c)
            if (*c == '/')
                *c = '\\';
        strlwr(normalized);
        pars.name = normalized;
    }
    return pars.name.GetLength() > 0;
}
} // namespace

SoundSystemOAL::SoundSystemOAL()
{
    // Surface the decoded-PCM cache (64 MB LRU) in the memory-budget panel and
    // let the global pressure path evict it. Priority is low — PCM re-decodes
    // cheaply from disk, so it's a good early eviction candidate.
    _pcmProbe.Register(
        "Audio.PcmCache", 0.05f, [this] { return _pcmCache.GetStats().bytes; },
        [] { return Audio::PcmCache::kMaxTotalBytes; }, [this] { return _pcmCache.GetStats().entries; },
        [this](size_t bytes) { return _pcmCache.Evict(bytes); });
}

SoundSystemOAL::~SoundSystemOAL()
{
    // Join the streaming-pump thread before tearing anything down: it issues AL
    // calls against _context and walks _waves, so it must be gone before the
    // context/device are destroyed and before the waves are stopped/cleared.
    StopStreamThread();

    _soundReady = false;
    TerminatePreview();
    DeinitEFX();

    // Stop all waves before destroying context
    for (auto* wave : _waves)
    {
        if (wave)
            wave->DoStop();
    }
    _waves.clear();

    if (_context)
    {
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(static_cast<ALCcontext*>(_context));
        _context = nullptr;
    }
    if (_device)
    {
        alcCloseDevice(static_cast<ALCdevice*>(_device));
        _device = nullptr;
    }
    LOG_INFO(Audio, "System shutdown");
}

bool SoundSystemOAL::Init()
{
    _device = alcOpenDevice(nullptr);
    if (!_device)
    {
        LOG_ERROR(Audio, "alcOpenDevice failed");
        return false;
    }
    _context = alcCreateContext(static_cast<ALCdevice*>(_device), nullptr);
    if (!_context)
    {
        LOG_ERROR(Audio, "alcCreateContext failed");
        alcCloseDevice(static_cast<ALCdevice*>(_device));
        _device = nullptr;
        return false;
    }
    alcMakeContextCurrent(static_cast<ALCcontext*>(_context));

#ifdef POSEIDON_TARGET_IOS
    if (!ConfigureIOSAudioSessionForPlayback())
        return false;
#endif

    // Check EFX extension availability (DX8 parity: soundDX8.cpp:2108-2114)
    _canEAX = alcIsExtensionPresent(static_cast<ALCdevice*>(_device), "ALC_EXT_EFX") == ALC_TRUE;
    LOG_INFO(Audio, "EFX extension: {}", _canEAX ? "available" : "not available");

    // Set distance model for 3D audio
    alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);
    alDopplerFactor(1.0f);

    // Default listener orientation: looking down -Z, up +Y
    float orientation[6] = {0.f, 0.f, -1.f, 0.f, 1.f, 0.f};
    alListener3f(AL_POSITION, 0.f, 0.f, 0.f);
    alListener3f(AL_VELOCITY, 0.f, 0.f, 0.f);
    alListenerfv(AL_ORIENTATION, orientation);

    // Default volumes matching DX8 reference (soundDX8.cpp:2000-2002)
    SetSpeechVolume(5.f);
    SetWaveVolume(5.f);
    SetCDVolume(5.f);

    LoadConfig();

    const char* deviceName = alcGetString(static_cast<ALCdevice*>(_device), ALC_DEVICE_SPECIFIER);
    LOG_INFO(Audio, "Init OK: {}", deviceName ? deviceName : "unknown");

    _soundReady = true;
    _doPreview = false;

    StartStreamThread();

    return true;
}

void SoundSystemOAL::StartStreamThread()
{
    if (_streamThread.joinable())
        return;
    _streamStop = false;
    _streamThread = std::thread([this] { StreamPumpLoop(); });
}

void SoundSystemOAL::StopStreamThread()
{
    if (!_streamThread.joinable())
        return;
    {
        std::lock_guard<std::mutex> lk(_streamWakeMutex);
        _streamStop = true;
    }
    _streamWake.notify_all();
    _streamThread.join();
}

void SoundSystemOAL::StreamPumpLoop()
{
    // Service streamed sources every ~10 ms, independent of the render frame.
    // 4x100 ms buffer ring => a reliable 10 ms cadence keeps it full with deep
    // headroom; the work no-ops when no OGG sources are live.
    for (;;)
    {
        {
            std::unique_lock<std::mutex> lk(_streamWakeMutex);
            _streamWake.wait_for(lk, std::chrono::milliseconds(10), [this] { return _streamStop; });
            if (_streamStop)
                return;
        }
        // Pass 1 (under the lock): recycle/upload buffers for every streamed wave.
        // _waves holds non-owning raw pointers and waves can sit at refcount 0
        // (CreateWave returns 0 until the SoundScene wraps it), so we do NOT take
        // an owning ref — instead the lock keeps the registry stable and blocks
        // ~WaveOAL (which also takes _audioMutex). Collect the waves that want a
        // decode; PumpDrainUpload has latched their decodeInFlight so ~WaveOAL
        // will wait for the decode below rather than free them mid-read.
        std::vector<WaveOAL*> toDecode;
        {
            std::lock_guard<std::recursive_mutex> lk(_audioMutex);
            if (!_soundReady)
                continue;
            for (auto* wave : _waves)
            {
                if (wave && wave->IsStreamed() && wave->PumpDrainUpload())
                    toDecode.push_back(wave);
            }
        }
        // Pass 2 (lock-free): the multi-ms codec read. Each wave here has
        // decodeInFlight latched, so ~WaveOAL spins on it before freeing — the
        // raw pointer stays valid for the duration of DecodeQueuedChunk.
        for (auto* wave : toDecode)
            wave->DecodeQueuedChunk();
    }
}

void SoundSystemOAL::RegisterWave(WaveOAL* wave)
{
    std::lock_guard<std::recursive_mutex> lk(_audioMutex);
    if (wave)
        _waves.push_back(wave);
}

void SoundSystemOAL::UnregisterWave(WaveOAL* wave)
{
    std::lock_guard<std::recursive_mutex> lk(_audioMutex);
    auto it = std::find(_waves.begin(), _waves.end(), wave);
    if (it != _waves.end())
        _waves.erase(it);
}

float SoundSystemOAL::GetWaveDuration(const char* filename)
{
    // SoundLoadFile dispatches by extension (.ogg/.wav, else .wss); WSSLoadFile alone returned
    // null for .ogg, so any OGG duration query (speech/dialog clips, soundLength) read as 0.
    WaveStream* s = SoundLoadFile(filename);
    if (!s)
        return 0.f;
    WAVEFORMATEX fmt;
    s->GetFormat(fmt);
    float dur = fmt.nAvgBytesPerSec > 0
                    ? static_cast<float>(s->GetUncompressedSize()) / static_cast<float>(fmt.nAvgBytesPerSec)
                    : 0.f;
    s->Release();
    return dur;
}

IWave* SoundSystemOAL::CreateEmptyWave(float duration)
{
    return new WaveOAL(this, duration);
}

IWave* SoundSystemOAL::CreateWave(const char* filename, bool is3D, bool /*prealloc*/)
{
    if (!_soundReady)
        return nullptr;
    return new WaveOAL(this, RString(filename), is3D);
}

IWave* SoundSystemOAL::CreateWaveFromMemory(const void* data, size_t size, const char* ext, bool is3D)
{
    if (!_soundReady)
        return nullptr;
    Ref<WaveStream> stream = SoundLoadMemory(data, size, ext);
    if (!stream)
        return nullptr;
    // Use empty constructor, then manually set stream
    auto* wave = new WaveOAL(this, 0.f);
    wave->_is3D = is3D;
    wave->_createdAs3D = is3D;
    wave->_stream = stream;
    wave->LoadHeader();
    return wave;
}

void SoundSystemOAL::SetListener(Vector3Par pos, Vector3Par vel, Vector3Par dir, Vector3Par up)
{
    _listenerPos = pos;
    _listenerVel = vel;
    _listenerDir = dir;
    _listenerUp = up;

    // Coordinate transform: invert X (left-handed game → right-handed OpenAL)
    // Matching ArmA3 soundOAL.cpp:3840-3845
    alListener3f(AL_POSITION, -pos[0], pos[1], pos[2]);
    alListener3f(AL_VELOCITY, -vel[0], vel[1], vel[2]);

    float orientation[6] = {-dir[0], dir[1], dir[2], -up[0], up[1], up[2]};
    alListenerfv(AL_ORIENTATION, orientation);
}

void SoundSystemOAL::Commit()
{
    if (!_soundReady)
        return;

    std::lock_guard<std::recursive_mutex> lk(_audioMutex);

    for (auto* wave : _waves)
    {
        if (wave && wave->_wantPlaying)
        {
            wave->DoPlay();
            wave->_wantPlaying = false;
        }
    }

    // Per-frame streaming start/restart (deferred first start, underrun recovery)
    // — this part touches play-state (_playing/_state) so it stays on the main
    // thread. The buffer ring itself is kept full by the StreamPumpLoop thread
    // (StreamPumpLoop) so frame stalls no longer starve the queue.
    for (auto* wave : _waves)
    {
        if (wave && wave->IsStreamed())
        {
            wave->UpdateStreamingPlayback();
        }
    }

    ProcessPreview();
}

void SoundSystemOAL::Activate(bool active)
{
    if (!_context)
        return;

    if (active)
        alcProcessContext(static_cast<ALCcontext*>(_context));
    else
        alcSuspendContext(static_cast<ALCcontext*>(_context));
}

// --- EFX (EAX reverb) implementation ---
// 1:1 port of soundDX8.cpp InitEAX/DeinitEAX/DoSetEAXEnvironment

bool SoundSystemOAL::InitEFX()
{
    if (!_canEAX)
        return false;

    // Create auxiliary effect slot (DX8: _eaxFakeListener buffer + IKsPropertySet)
    alGenAuxiliaryEffectSlots(1, &_efxSlot);
    if (alGetError() != AL_NO_ERROR)
    {
        LOG_ERROR(Audio, "EFX: failed to create auxiliary effect slot");
        _efxSlot = 0;
        return false;
    }

    // Create reverb effect
    alGenEffects(1, &_efxEffect);
    if (alGetError() != AL_NO_ERROR)
    {
        LOG_ERROR(Audio, "EFX: failed to create effect");
        alDeleteAuxiliaryEffectSlots(1, &_efxSlot);
        _efxSlot = 0;
        _efxEffect = 0;
        return false;
    }

    // Set effect type to EAX Reverb
    alEffecti(_efxEffect, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB);
    if (alGetError() != AL_NO_ERROR)
    {
        LOG_ERROR(Audio, "EFX: AL_EFFECT_EAXREVERB not supported");
        alDeleteEffects(1, &_efxEffect);
        alDeleteAuxiliaryEffectSlots(1, &_efxSlot);
        _efxSlot = 0;
        _efxEffect = 0;
        return false;
    }

    // Apply initial environment (DX8:2540 — calls DoSetEAXEnvironment after init)
    DoSetEAXEnvironment();

    LOG_INFO(Audio, "EFX: EAX reverb initialized (slot={}, effect={})", _efxSlot, _efxEffect);
    return true;
}

void SoundSystemOAL::DeinitEFX()
{
    if (_efxSlot)
    {
        // Detach effect from slot (DX8:2550 — sets ROOM to -10000)
        alAuxiliaryEffectSloti(_efxSlot, AL_EFFECTSLOT_EFFECT, AL_EFFECT_NULL);
    }
    if (_efxEffect)
    {
        alDeleteEffects(1, &_efxEffect);
        _efxEffect = 0;
    }
    if (_efxSlot)
    {
        alDeleteAuxiliaryEffectSlots(1, &_efxSlot);
        _efxSlot = 0;
    }
}

static void ApplyEFXProperties(ALuint efxEffect, ALuint efxSlot, const EFXEAXREVERBPROPERTIES& preset)
{
    alEffectf(efxEffect, AL_EAXREVERB_DENSITY, preset.flDensity);
    alEffectf(efxEffect, AL_EAXREVERB_DIFFUSION, preset.flDiffusion);
    alEffectf(efxEffect, AL_EAXREVERB_GAIN, preset.flGain);
    alEffectf(efxEffect, AL_EAXREVERB_GAINHF, preset.flGainHF);
    alEffectf(efxEffect, AL_EAXREVERB_GAINLF, preset.flGainLF);
    alEffectf(efxEffect, AL_EAXREVERB_DECAY_TIME, preset.flDecayTime);
    alEffectf(efxEffect, AL_EAXREVERB_DECAY_HFRATIO, preset.flDecayHFRatio);
    alEffectf(efxEffect, AL_EAXREVERB_DECAY_LFRATIO, preset.flDecayLFRatio);
    alEffectf(efxEffect, AL_EAXREVERB_REFLECTIONS_GAIN, preset.flReflectionsGain);
    alEffectf(efxEffect, AL_EAXREVERB_REFLECTIONS_DELAY, preset.flReflectionsDelay);
    alEffectfv(efxEffect, AL_EAXREVERB_REFLECTIONS_PAN, preset.flReflectionsPan);
    alEffectf(efxEffect, AL_EAXREVERB_LATE_REVERB_GAIN, preset.flLateReverbGain);
    alEffectf(efxEffect, AL_EAXREVERB_LATE_REVERB_DELAY, preset.flLateReverbDelay);
    alEffectfv(efxEffect, AL_EAXREVERB_LATE_REVERB_PAN, preset.flLateReverbPan);
    alEffectf(efxEffect, AL_EAXREVERB_ECHO_TIME, preset.flEchoTime);
    alEffectf(efxEffect, AL_EAXREVERB_ECHO_DEPTH, preset.flEchoDepth);
    alEffectf(efxEffect, AL_EAXREVERB_MODULATION_TIME, preset.flModulationTime);
    alEffectf(efxEffect, AL_EAXREVERB_MODULATION_DEPTH, preset.flModulationDepth);
    alEffectf(efxEffect, AL_EAXREVERB_AIR_ABSORPTION_GAINHF, preset.flAirAbsorptionGainHF);
    alEffectf(efxEffect, AL_EAXREVERB_HFREFERENCE, preset.flHFReference);
    alEffectf(efxEffect, AL_EAXREVERB_LFREFERENCE, preset.flLFReference);
    alEffectf(efxEffect, AL_EAXREVERB_ROOM_ROLLOFF_FACTOR, preset.flRoomRolloffFactor);
    alEffecti(efxEffect, AL_EAXREVERB_DECAY_HFLIMIT, preset.iDecayHFLimit);

    alAuxiliaryEffectSloti(efxSlot, AL_EFFECTSLOT_EFFECT, efxEffect);
}

void SoundSystemOAL::ApplyEFXPreset(float envSize)
{
    // Select preset from registry based on environment type
    // 1:1 with DX8 DoSetEAXEnvironment switch (soundDX8.cpp:2570-2578)
    const char* presetName = "generic";
    switch (_eaxEnv.type)
    {
        case SEPlain:
            presetName = "plain";
            break;
        case SEForest:
            presetName = "city";
            break; // intentional DX8 parity
        case SECity:
            presetName = "city";
            break;
        case SEMountains:
            presetName = "mountains";
            break;
        case SERoom:
            presetName = "room";
            break;
        default:
            presetName = "generic";
            break;
    }

    const auto* entry = EFX::FindPreset(presetName);
    if (!entry)
        return;

    EFXEAXREVERBPROPERTIES preset = entry->props;
    float presetDefaultSize = entry->defaultSize;

    // Apply size scaling (DX8:2584-2592 — DSPROPERTY_EAXLISTENER_ENVIRONMENTSIZE)
    // EAX 2.0 internally scales decay time proportional to environment size
    float sizeRatio = envSize / presetDefaultSize;
    preset.flDecayTime *= sizeRatio;
    if (preset.flDecayTime < AL_EAXREVERB_MIN_DECAY_TIME)
        preset.flDecayTime = AL_EAXREVERB_MIN_DECAY_TIME;
    if (preset.flDecayTime > AL_EAXREVERB_MAX_DECAY_TIME)
        preset.flDecayTime = AL_EAXREVERB_MAX_DECAY_TIME;

    // Density maps room size perception
    preset.flDensity = envSize / 100.0f;
    if (preset.flDensity < AL_EAXREVERB_MIN_DENSITY)
        preset.flDensity = AL_EAXREVERB_MIN_DENSITY;
    if (preset.flDensity > AL_EAXREVERB_MAX_DENSITY)
        preset.flDensity = AL_EAXREVERB_MAX_DENSITY;

    ApplyEFXProperties(_efxEffect, _efxSlot, preset);
}

void SoundSystemOAL::DoSetEAXEnvironment()
{
    if (!_efxSlot || !_efxEffect)
        return;

    // Clamp size to [2, 100] (DX8:2584-2586)
    float size = _eaxEnv.size;
    if (size < 2.f)
        size = 2.f;
    if (size > 100.f)
        size = 100.f;

    ApplyEFXPreset(size);

    LOG_DEBUG(Audio, "EFX: environment type={} size={}", static_cast<int>(_eaxEnv.type), size);
}

void SoundSystemOAL::SetEnvironment(const SoundEnvironment& env)
{
    // Change detection — 1:1 with DX8 (soundDX8.cpp:2596-2614)
    if (env.type == _eaxEnv.type && fabs(env.size - _eaxEnv.size) < 1 && fabs(env.density - _eaxEnv.density) < 0.05)
    {
        return; // no change
    }
    _eaxEnv = env;

    if (!_eaxEnabled)
        return;

    DoSetEAXEnvironment();
}

bool SoundSystemOAL::EnableEAX(bool val)
{
    std::lock_guard<std::recursive_mutex> lk(_audioMutex);
    // DX8 parity (soundDX8.cpp:1916-1932) minus _hwEnabled check
    if (!_canEAX)
    {
        LOG_DEBUG(Audio, "EnableEAX({}): rejected — EFX not available", val);
        return false;
    }
    if (_eaxEnabled == val)
        return _eaxEnabled;

    _eaxEnabled = val;
    if (_eaxEnabled)
    {
        if (!InitEFX())
            _eaxEnabled = false;
    }
    else
    {
        // Disconnect all sources from the effect slot before destroying it
        for (auto* wave : _waves)
        {
            if (wave)
                wave->DetachEFX();
        }
        DeinitEFX();
    }

    LOG_INFO(Audio, "EAX {}", _eaxEnabled ? "enabled" : "disabled");
    return _eaxEnabled;
}

bool SoundSystemOAL::ApplyEFXByName(const char* presetName, float size)
{
    if (!_eaxEnabled || !_efxSlot || !_efxEffect)
        return false;

    const auto* entry = EFX::FindPreset(presetName);
    if (!entry)
        return false;

    EFXEAXREVERBPROPERTIES preset = entry->props;

    // Size scaling (same as ApplyEFXPreset)
    if (size < 2.f)
        size = 2.f;
    if (size > 100.f)
        size = 100.f;

    float sizeRatio = size / entry->defaultSize;
    preset.flDecayTime *= sizeRatio;
    if (preset.flDecayTime < AL_EAXREVERB_MIN_DECAY_TIME)
        preset.flDecayTime = AL_EAXREVERB_MIN_DECAY_TIME;
    if (preset.flDecayTime > AL_EAXREVERB_MAX_DECAY_TIME)
        preset.flDecayTime = AL_EAXREVERB_MAX_DECAY_TIME;

    preset.flDensity = size / 100.0f;
    if (preset.flDensity < AL_EAXREVERB_MIN_DENSITY)
        preset.flDensity = AL_EAXREVERB_MIN_DENSITY;
    if (preset.flDensity > AL_EAXREVERB_MAX_DENSITY)
        preset.flDensity = AL_EAXREVERB_MAX_DENSITY;

    ApplyEFXProperties(_efxEffect, _efxSlot, preset);
    LOG_DEBUG(Audio, "EFX: applied preset '{}' size={}", presetName, size);
    return true;
}

namespace
{
// Path to the system-global audio.cfg.  Lives directly under the
// user-data directory (NOT the per-profile Users/<name>/ subdir) —
// audio is per-machine, not per-game-profile.
std::string AudioConfigPath()
{
    const std::string& dir = GamePaths::Instance().UserDir();
    // GamePaths guarantees a trailing slash.
    return dir + "audio.cfg";
}

// AudioConfig::Environment impl backed by the live audio backend.
// The output side reads from SoundSystemOAL, the input side from
// VoNCapture (the same enumeration the picker UI uses, so a
// user-selected name will round-trip cleanly).
struct LiveAudioEnv : AudioConfig::Environment
{
    const SoundSystemOAL* sys;
    explicit LiveAudioEnv(const SoundSystemOAL* s) : sys(s) {}
    std::vector<std::string> ListOutputDevices() const override
    {
        return sys ? sys->ListOutputDevices() : std::vector<std::string>{};
    }
    std::vector<std::string> ListInputDevices() const override { return VoNCapture::listDevices(); }
};
} // namespace

void SoundSystemOAL::LoadConfig()
{
    const std::string path = AudioConfigPath();

    AudioConfig cfg;
    if (!cfg.Load(path))
    {
        // Missing or unparseable file → eager-write defaults so the file
        // exists for the user to find and edit.
        cfg.LoadDefaults();
        cfg.Save(path);
        LOG_INFO(Audio, "LoadConfig: created defaults at '{}'", path);
    }

    // Validate against the live device lists.  If anything was reset
    // (volume out of range, output/input device gone), we keep the
    // normalized values in memory but DO NOT write them back — the
    // user might reconnect the device later, and we don't want one
    // boot to silently lose their setting.  Persistence on a normalized
    // field happens when AudioPage::Unmount calls SaveConfig().
    LiveAudioEnv env(this);
    if (cfg.Normalize(env))
        LOG_INFO(Audio, "LoadConfig: normalized invalid fields (not persisted)");

    // Apply to runtime.  UI scale is 0..100 percent; backend scale is
    // 0..10 floats (the IAudioSystem contract).
    SetCDVolume(cfg.musicVolume / 10.0f);
    SetWaveVolume(cfg.effectsVolume / 10.0f);
    SetSpeechVolume(cfg.speechVolume / 10.0f);
    if (cfg.eaxEnabled && _canEAX)
        EnableEAX(true);

    if (!cfg.outputDevice.empty())
        SwitchOutputDevice(cfg.outputDevice.c_str());
    // Input device picker is owned by the AudioPage UI — no runtime
    // VoNCapture instance to seed here.  AudioPage reads cfg.inputDevice
    // when it mounts.
    _selectedInputDevice = cfg.inputDevice;

    LOG_DEBUG(Audio, "LoadConfig: music={}% fx={}% speech={}% eax={} out='{}' in='{}'", cfg.musicVolume,
              cfg.effectsVolume, cfg.speechVolume, _eaxEnabled, cfg.outputDevice, cfg.inputDevice);
}

void SoundSystemOAL::SaveConfig()
{
    if (IsOutOfMemory())
        return;

    AudioConfig cfg;
    cfg.musicVolume = (int)(GetCDVolume() * 10.0f + 0.5f);
    cfg.effectsVolume = (int)(GetWaveVolume() * 10.0f + 0.5f);
    cfg.speechVolume = (int)(GetSpeechVolume() * 10.0f + 0.5f);
    cfg.eaxEnabled = _eaxEnabled;
    cfg.outputDevice = _selectedDevice;
    cfg.inputDevice = _selectedInputDevice;

    const std::string path = AudioConfigPath();
    if (!cfg.Save(path))
        LOG_WARN(Audio, "SaveConfig: failed to write '{}'", path);
    else
        LOG_DEBUG(Audio, "SaveConfig: music={}% fx={}% speech={}% eax={} out='{}' in='{}'", cfg.musicVolume,
                  cfg.effectsVolume, cfg.speechVolume, _eaxEnabled, cfg.outputDevice, cfg.inputDevice);
}

std::vector<std::string> SoundSystemOAL::ListOutputDevices() const
{
    // OpenAL hands back a single buffer of null-terminated strings,
    // double-null at the end.  Prefer ALC_ALL_DEVICES_SPECIFIER (every
    // hardware endpoint) over ALC_DEVICE_SPECIFIER (vendor-classified
    // bucket) on implementations that expose both.
    std::vector<std::string> result;
    const char* list = alcGetString(nullptr, ALC_ALL_DEVICES_SPECIFIER);
    if (!list || !*list)
        list = alcGetString(nullptr, ALC_DEVICE_SPECIFIER);
    if (!list)
        return result;
    while (*list)
    {
        result.emplace_back(list);
        list += result.back().size() + 1;
    }
    return result;
}

std::string SoundSystemOAL::GetCurrentOutputDevice() const
{
    if (!_selectedDevice.empty())
        return _selectedDevice;
    if (_device)
    {
        const char* name = alcGetString(static_cast<ALCdevice*>(_device), ALC_DEVICE_SPECIFIER);
        if (name)
            return name;
    }
    return {};
}

bool SoundSystemOAL::SwitchOutputDevice(const char* name)
{
    // Hold the audio lock across the whole swap: between destroying the old
    // context and binding the new one there is no current context, so the
    // streaming pump must not issue AL calls. It blocks on this lock until the
    // rebind completes, then resumes against the new context.
    std::lock_guard<std::recursive_mutex> lk(_audioMutex);
    std::string requested = (name && *name) ? std::string(name) : std::string();
    if (requested == _selectedDevice && _device)
        return true;

    ALCdevice* newDevice = alcOpenDevice(requested.empty() ? nullptr : requested.c_str());
    if (!newDevice)
    {
        LOG_ERROR(Audio, "SwitchOutputDevice: alcOpenDevice failed for '{}'",
                  requested.empty() ? "<default>" : requested.c_str());
        return false;
    }

    ALCcontext* newContext = alcCreateContext(newDevice, nullptr);
    if (!newContext)
    {
        LOG_ERROR(Audio, "SwitchOutputDevice: alcCreateContext failed");
        alcCloseDevice(newDevice);
        return false;
    }

    // Snapshot volumes — backends typically initialise to 5.f on
    // construction and we don't want a device-switch to silently
    // jolt levels.
    ALCdevice* oldDevice = static_cast<ALCdevice*>(_device);
    ALCcontext* oldContext = static_cast<ALCcontext*>(_context);
    float cd = _cdVolume;
    float wave = _waveVolume;
    float speech = _speechVolume;
    bool wantEAX = _eaxEnabled;

    // Note whether the device preview is currently in flight so we can
    // resume after the context swap.  StartDevicePreview computes the
    // playback offset from a wall-clock anchor (NOT the AL source's
    // AL_SEC_OFFSET) — this stays accurate across the context teardown
    // and rapid clicks no longer collapse the offset to ~0.
    bool wasDevicePreviewing = (_previewDevice.GetRef() != nullptr);

    // Drop the in-flight preview waves while the OLD context is still
    // current so their AL handles get released cleanly.  Note: the
    // wall-clock anchor (_devicePreviewAnchorMs) is intentionally NOT
    // reset here — only TerminatePreview's "true cancel" path resets
    // it.  This is what lets device-cycling stay continuous.
    _previewSpeech.Free();
    _previewEffect.Free();
    _previewMusic.Free();
    _previewCategory.Free();
    _previewDevice.Free();
    _doPreview = false;

    // Unload every wave so its AL source + buffer get released on the
    // current (still-valid) context BEFORE we destroy it.  Without
    // this, the WaveOAL keeps stale ALuints and the next Play() / new
    // wave creation hits AL_INVALID_OPERATION (manifest as
    // "Buffer error: …" on every subsequent CreateWave).
    for (auto* w : _waves)
        if (w)
            w->Unload();

    if (oldContext)
    {
        alcMakeContextCurrent(nullptr);
    }

    if (alcMakeContextCurrent(newContext) != ALC_TRUE)
    {
        LOG_ERROR(Audio, "SwitchOutputDevice: alcMakeContextCurrent failed");
        if (oldContext)
            alcMakeContextCurrent(oldContext);
        alcDestroyContext(newContext);
        alcCloseDevice(newDevice);
        return false;
    }

    if (oldContext)
        alcDestroyContext(oldContext);
    if (oldDevice)
        alcCloseDevice(oldDevice);

    _device = newDevice;
    _context = newContext;
    alcProcessContext(newContext);

    // EAX availability tracks the new device.
    _canEAX = alcIsExtensionPresent(static_cast<ALCdevice*>(_device), "ALC_EXT_EFX") == ALC_TRUE;

    alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);
    alDopplerFactor(1.0f);

    // audio-invariants A-22 — re-apply the cached listener pose so
    // 3D sources keep their spatial position on the new context.
    // Without this, alListener defaults to origin/identity until the
    // next world.cpp::SetListener tick.
    SetListener(_listenerPos, _listenerVel, _listenerDir, _listenerUp);

    SetCDVolume(cd);
    SetWaveVolume(wave);
    SetSpeechVolume(speech);
    if (wantEAX && _canEAX)
        EnableEAX(true);

    _selectedDevice = std::move(requested);
    LOG_INFO(Audio, "SwitchOutputDevice: switched to '{}'",
             _selectedDevice.empty() ? "<default>" : _selectedDevice.c_str());

    // Resume the device-picker preview on the new context; the sample
    // continues from where the old context stopped.
    if (wasDevicePreviewing)
        StartDevicePreview();
    return true;
}

void SoundSystemOAL::SetCDVolume(float v)
{
    if (v < 0.f)
        v = 0.f;
    if (v > 10.f)
        v = 10.f;
    _cdVolume = v;
    UpdateMixer();
}

void SoundSystemOAL::SetWaveVolume(float v)
{
    if (v < 0.f)
        v = 0.f;
    if (v > 10.f)
        v = 10.f;
    _waveVolume = v;
    UpdateMixer();
}

void SoundSystemOAL::SetSpeechVolume(float v)
{
    if (v < 0.f)
        v = 0.f;
    if (v > 10.f)
        v = 10.f;
    _speechVolume = v;
    UpdateMixer();
}

void SoundSystemOAL::SetVoiceBudgetCounters(int evictedThisFrame, int pausedThisFrame)
{
    _evictedThisFrame = evictedThisFrame;
    _pausedThisFrame = pausedThisFrame;
}

AudioCounters SoundSystemOAL::GetCounters() const
{
    std::lock_guard<std::recursive_mutex> lk(_audioMutex);
    AudioCounters c;
    c.suppressedMusic = static_cast<int>(_suppressedMusic.size());
    c.allocFailures = _allocFailures;
    c.evictedThisFrame = _evictedThisFrame;
    c.pausedThisFrame = _pausedThisFrame;
    for (auto* w : _waves)
    {
        if (!w)
        {
            continue;
        }
        // const_cast: GetCounters is a const probe, but WaveOAL::State() is
        // non-const because IWave's overall API is non-const (IsStopped /
        // IsTerminated need to query AL state).  The snapshot here is
        // observe-only — no FSM transitions trigger.
        WaveOAL* mw = const_cast<WaveOAL*>(w);
        WaveState st = mw->State();
        if (st == WaveState::Playing || st == WaveState::WaitingDeferred)
        {
            if (mw->Get3D())
            {
                ++c.active3D;
            }
            else
            {
                ++c.active2D;
            }
        }
        else if (st == WaveState::Paused)
        {
            ++c.paused;
        }
    }
    return c;
}

void SoundSystemOAL::UpdateMixer()
{
    std::lock_guard<std::recursive_mutex> lk(_audioMutex);
    float waveVolExp = DX8::expVol(_waveVolume);
    float speechVolExp = DX8::expVol(_speechVolume);
    float musicVolExp = DX8::expVol(_cdVolume);

    // Per-wave adjusts: ratio of channel to max (soundDX8.cpp:2977-2979)
    float maxVolExp = (std::max)({musicVolExp, waveVolExp, speechVolExp});
    _volumeAdjustEffect = DX8::mixerAdjust(waveVolExp, maxVolExp);
    _volumeAdjustSpeech = DX8::mixerAdjust(speechVolExp, maxVolExp);
    _volumeAdjustMusic = DX8::mixerAdjust(musicVolExp, maxVolExp);

    // DX8 _mixer->SetWaveVolume(MaxVolume()*0.1) → OAL alListenerf(AL_GAIN, ...)
    float maxRawVol = (std::max)({_waveVolume, _speechVolume, _cdVolume});
    alListenerf(AL_GAIN, DX8::listenerGain(maxRawVol));

    LOG_DEBUG(Audio, "UpdateMixer: wave={} speech={} music={} gain={} (vols {}/{}/{})", _volumeAdjustEffect,
              _volumeAdjustSpeech, _volumeAdjustMusic, DX8::listenerGain(maxRawVol), _waveVolume, _speechVolume,
              _cdVolume);

    for (auto* wave : _waves)
    {
        if (wave)
            wave->SetVolumeAdjust(_volumeAdjustEffect, _volumeAdjustSpeech, _volumeAdjustMusic);
    }
}

static bool PreviewAdvance(IWave* wave, float deltaT)
{
    if (!wave)
        return false;
    if (!wave->IsMuted())
    {
        wave->Play();
    }
    else
    {
        wave->Stop();
        wave->Advance(deltaT);
        LOG_DEBUG(Audio, "PreviewAdvance: muted, advancing by {}", deltaT);
    }
    bool term = wave->IsTerminated();
    if (term)
        LOG_DEBUG(Audio, "PreviewAdvance: terminated, advancing to next stage");
    return term;
}

void SoundSystemOAL::StartPreview()
{
    if (!_enablePreview)
    {
        LOG_DEBUG(Audio, "StartPreview: preview disabled");
        return;
    }
    // Always tear down anything in flight — even mid-speech / mid-effect
    // — so a value change *during* the running preview cuts the previous
    // sequence and restarts cleanly from the top.  The 200ms scheduled
    // delay below absorbs slider-drag bursts: each tick during a drag
    // pushes _previewTime forward, so the chain only kicks in once the
    // user pauses for ~200ms.
    TerminatePreview();
    _doPreview = true;
    _previewTime = GlobalTickCount() + 200;
    LOG_DEBUG(Audio, "StartPreview: scheduled at tick {}", _previewTime);
}

void SoundSystemOAL::TerminatePreview()
{
    _previewSpeech.Free();
    _previewEffect.Free();
    _previewMusic.Free();
    _previewCategory.Free();
    // _previewDevice is also cut so a focus-leave from the Output Device
    // row stops audio.  Reset the anchor — re-focusing and re-clicking
    // starts a brand new wall-clock-tracked session from offset 0.
    _previewDevice.Free();
    _devicePreviewAnchorMs = 0;
    _devicePreviewLengthSec = 0.0f;
    _previewEax.Free();
    _doPreview = false;
    LOG_DEBUG(Audio, "TerminatePreview");
}

void SoundSystemOAL::StartEAXPreview()
{
    if (!_enablePreview)
        return;

    // Cut anything else; this is a focused A/B test where the user
    // toggles EAX on/off.
    _previewSpeech.Free();
    _previewEffect.Free();
    _previewMusic.Free();
    _previewCategory.Free();
    _previewDevice.Free();
    _previewEax.Free();
    _doPreview = false;

    // Use one of the environments the game actually exposes at
    // runtime (SoundSystemOAL::ApplyEFXPreset maps SEPlain / SEForest /
    // SECity / SEMountains / SERoom — that's the full set of reverbs
    // shipping in stock missions).  SERoom is the most audibly distinct
    // from the default outdoor SEPlain so the on/off diff is obvious;
    // toggling EAX off plays the sample dry, which is exactly the
    // contrast we want the user to hear.
    if (_canEAX && _eaxEnabled)
        SetEnvironment(SoundEnvironment{SERoom, 1.9f, 0.5f});

    const ParamEntry& cfg = Pars >> "CfgSFX" >> "Preview" >> "effect";
    RStringB name = cfg[0];
    IWave* wave = CreateWave(name, true, false);
    _previewEax = wave;
    if (!wave)
    {
        LOG_WARN(Audio, "StartEAXPreview: failed to create wave '{}'", static_cast<const char*>(name));
        return;
    }
    wave->SetKind(WaveEffect);
    wave->EnableAccommodation(false);
    wave->SetSticky(true);
    wave->SetVolume((float)cfg[1], cfg[2], true);
    // 3D position close to the listener so the effect's reverb tail
    // is fully audible (the EFX send is per-source distance-attenuated;
    // a sample played from 200 m would barely register).
    wave->SetPosition(_listenerPos + _listenerDir * 8, _listenerVel, true);
    wave->Repeat(1);
    wave->Play();
    LOG_DEBUG(Audio, "StartEAXPreview: playing '{}' (eax={})", static_cast<const char*>(name),
              _eaxEnabled ? "on" : "off");
}

void SoundSystemOAL::SuppressMusicForPreview()
{
    std::lock_guard<std::recursive_mutex> lk(_audioMutex);
    // Idempotent — leaving early protects the saved originals if the
    // Audio screen Mounts twice without an intervening Resume (e.g.
    // a future modal that re-enters Audio).  Re-Suppressing now would
    // overwrite originals with the already-zeroed live volumes.
    if (!_suppressedMusic.empty())
        return;

    for (WaveOAL* w : _waves)
    {
        if (!w)
            continue;
        if (w->GetKind() != WaveMusic)
            continue;
        if (!w->_playing)
            continue;
        _suppressedMusic.push_back(w);
        w->_previewMute = true;
        // Force AL_GAIN=0 immediately so we don't wait one mixer tick
        // for SoundScene::Simulate to call SetVolume → ApplyVolume.
        w->ApplyVolume();
    }
    LOG_DEBUG(Audio, "SuppressMusicForPreview: ducked {} music wave(s)", _suppressedMusic.size());
}

void SoundSystemOAL::ResumeMusicForPreview()
{
    std::lock_guard<std::recursive_mutex> lk(_audioMutex);
    if (_suppressedMusic.empty())
        return;
    for (WaveOAL* wave : _suppressedMusic)
    {
        // The wave may have been deleted while suppressed.  Cross-check
        // against the live registry — UnregisterWave erases from _waves
        // on destruction, so membership there proves the pointer is
        // still valid.
        if (std::find(_waves.begin(), _waves.end(), wave) == _waves.end())
            continue;
        wave->_previewMute = false;
        wave->ApplyVolume();
    }
    LOG_DEBUG(Audio, "ResumeMusicForPreview: restored {} music wave(s)", _suppressedMusic.size());
    _suppressedMusic.clear();
}

void SoundSystemOAL::PreloadWave(const char* name)
{
    if (!name || !*name)
    {
        return;
    }
    // OGG rides the chunked streaming path and never enters the PCM
    // cache — preloading it would decode a full music track for nothing.
    if (const char* ext = strrchr(name, '.'); ext && !stricmp(ext, ".ogg"))
    {
        return;
    }
    if (_pcmCache.Contains(name))
    {
        return;
    }

    auto work = [this, path = std::string(name)]()
    {
        Ref<WaveStream> stream = SoundLoadFile(path.c_str());
        if (!stream)
        {
            LOG_DEBUG(Audio, "PreloadWave: '{}' not found", path.c_str());
            return;
        }
        const int size = stream->GetUncompressedSize();
        if (size <= 0 || static_cast<size_t>(size) > Audio::PcmCache::kMaxEntryBytes)
        {
            return; // oversized waves decode async at play time instead
        }
        auto entry = std::make_shared<Audio::PcmCache::Entry>();
        stream->GetFormat(entry->fmt);
        entry->uncompressedSize = static_cast<unsigned int>(size);
        entry->pcm.assign(static_cast<size_t>(size), 0);
        stream->GetData(entry->pcm.data(), 0, size);
        _pcmCache.Insert(path.c_str(), std::move(entry));
    };

    // The system can be torn down (device switch / shutdown) while a
    // worker holds the lambda; the mission-load call site runs inside
    // the loading screen where that cannot happen, but guard the late
    // case by running synchronously when no pool exists.
    if (auto* pool = GetGlobalTaskPool())
    {
        pool->Submit(work);
    }
    else
    {
        work();
    }
}
void SoundSystemOAL::StartCategoryPreview(WaveKind kind)
{
    if (!_enablePreview)
        return;

    // Stop the legacy 3-stage chain AND the device-picker preview if
    // either is running — the category preview is a different focus
    // mode and should be heard alone.
    _previewSpeech.Free();
    _previewEffect.Free();
    _previewMusic.Free();
    _previewDevice.Free();
    _devicePreviewAnchorMs = 0;
    _devicePreviewLengthSec = 0.0f;
    _doPreview = false;

    // Tear the previous category preview down — even if it's the same
    // kind — so a volume slider drag restarts the wave and the user
    // hears the new level immediately on the next cycle.
    _previewCategory.Free();
    _previewCategoryKind = kind;

    const char* slot = (kind == WaveSpeech) ? "speech" : (kind == WaveMusic) ? "music" : "effect";
    SoundPars pars{};
    if (kind == WaveMusic)
    {
        if (!ResolvePreviewMusicTrack("musicTrack", pars))
            ResolvePreviewSoundArray(slot, pars);
    }
    else
    {
        ResolvePreviewSoundArray(slot, pars);
    }
    RStringB name = pars.name;
    float vol = pars.vol, freq = pars.freq;
    bool is3D = (kind == WaveEffect); // matches legacy chain — only effect is 3D
    IWave* wave = nullptr;
    wave = CreateWave(name, is3D, !is3D);
    _previewCategory = wave;
    if (!wave)
    {
        LOG_WARN(Audio, "StartCategoryPreview({}): failed to create wave '{}'", slot, static_cast<const char*>(name));
        return;
    }
    wave->SetKind(kind);
    wave->EnableAccommodation(false);
    wave->SetSticky(true);
    float baseVol = (kind == WaveMusic) ? 0.5f : 1.0f; // legacy halves music
    wave->SetVolume(vol * baseVol, freq, true);
    if (is3D)
        wave->SetPosition(_listenerPos + _listenerDir * 50, _listenerVel, true);
    // Music preview loops while focused so volume tweaks are
    // immediately audible against a continuous track; speech/effect
    // remain one-shot (their TickPreviewLoop replays them every 5 s).
    wave->Repeat(kind == WaveMusic ? 0 : 1);
    wave->Play();
    ++_categoryPreviewRestartCount; // audio-invariants A-18
    LOG_DEBUG(Audio, "StartCategoryPreview({}): playing '{}'", slot, static_cast<const char*>(name));
}

void SoundSystemOAL::StartDevicePreview()
{
    if (!_enablePreview)
        return;

    if (_previewDevice)
        return; // already playing — nothing to restart
    SoundPars pars{};
    if (!ResolvePreviewMusicTrack("deviceMusicTrack", pars) && !ResolvePreviewMusicTrack("musicTrack", pars))
        ResolvePreviewSoundArray("music", pars);

    RStringB name = pars.name;
    float vol = pars.vol, freq = pars.freq;
    IWave* wave = CreateWave(name, false, true);
    _previewDevice = wave;
    if (!wave)
    {
        LOG_WARN(Audio, "StartDevicePreview: no preview wave found");
        return;
    }
    wave->SetKind(WaveMusic);
    wave->EnableAccommodation(false);
    wave->SetSticky(true);
    wave->SetVolume(vol * 0.5f, freq, true);
    wave->Repeat(0); // 0 = forever; loops while the row is focused

    // First start: anchor wall clock at "now" and remember length.
    // Subsequent starts (after a SwitchOutputDevice teardown) compute
    // the resume offset from (now - anchor) % length so playback head
    // advances naturally through the brief context-swap silence —
    // rapid clicks no longer collapse the offset back to ~0.
    uint32_t now = GlobalTickCount();
    if (_devicePreviewAnchorMs == 0)
    {
        _devicePreviewAnchorMs = now;
        _devicePreviewLengthSec = wave->GetLength();
    }
    // Pure-math helper — audio-invariants A-17.
    const float seekTo = audio::DevicePreviewOffsetSeconds(now, _devicePreviewAnchorMs, _devicePreviewLengthSec);
    // Stash the seek on the wave so DoPlay applies it once the AL
    // source actually exists (Play() defers to next Commit, so
    // alSourcef here would no-op against a 0 source).
    if (auto* w = dynamic_cast<WaveOAL*>(wave))
        w->_pendingSeekSeconds = seekTo;
    wave->Play();

    LOG_DEBUG(Audio, "StartDevicePreview: playing '{}' from offset {:.2f}s (anchor={}ms)",
              static_cast<const char*>(name), seekTo, _devicePreviewAnchorMs);
}

void SoundSystemOAL::ProcessPreview()
{
    if (!_doPreview)
        return;

    uint32_t now = GlobalTickCount();
    if (now < _previewTime)
        return;

    float deltaT = 0.1f;

    if (!_previewSpeech)
    {
        SoundPars pars{};
        ResolvePreviewSoundArray("speech", pars);
        RStringB name = pars.name;
        LOG_DEBUG(Audio, "Preview: loading speech '{}'", static_cast<const char*>(name));
        IWave* wave = CreateWave(name, false, true);
        _previewSpeech = wave;
        if (wave)
        {
            wave->SetKind(WaveSpeech);
            wave->EnableAccommodation(false);
            wave->SetSticky(true);
            wave->SetVolume(pars.vol, pars.freq, true);
            wave->Repeat(1);
            wave->Play();
        }
        else
        {
            LOG_WARN(Audio, "Preview: failed to create speech wave '{}'", static_cast<const char*>(name));
            _doPreview = false;
        }
    }
    else if (PreviewAdvance(_previewSpeech, deltaT) && !_previewEffect)
    {
        SoundPars pars{};
        ResolvePreviewSoundArray("effect", pars);
        RStringB name = pars.name;
        LOG_DEBUG(Audio, "Preview: loading effect '{}'", static_cast<const char*>(name));
        IWave* wave = CreateWave(name, true, false);
        _previewEffect = wave;
        if (wave)
        {
            wave->EnableAccommodation(false);
            wave->SetSticky(true);
            wave->SetVolume(pars.vol, pars.freq, true);
            wave->SetPosition(_listenerPos + _listenerDir * 50, _listenerVel, true);
            wave->Repeat(1);
            wave->Play();
        }
    }
    else if (PreviewAdvance(_previewEffect, deltaT) && !_previewMusic)
    {
        SoundPars pars{};
        if (!ResolvePreviewMusicTrack("musicTrack", pars))
            ResolvePreviewSoundArray("music", pars);
        RStringB name = pars.name;
        LOG_DEBUG(Audio, "Preview: loading music '{}'", static_cast<const char*>(name));
        IWave* wave = CreateWave(name, false, true);
        _previewMusic = wave;
        if (wave)
        {
            wave->SetKind(WaveMusic);
            wave->EnableAccommodation(false);
            wave->SetSticky(true);
            wave->SetVolume(pars.vol * 0.5f, pars.freq, true);
            wave->Repeat(1);
            wave->Play();
        }
    }
    else if (PreviewAdvance(_previewMusic, deltaT))
    {
        LOG_DEBUG(Audio, "Preview: sequence complete");
        _doPreview = false;
        _previewSpeech.Free();
        _previewEffect.Free();
        _previewMusic.Free();
    }
}

IAudioSystem* CreateSoundSystemOAL()
{
#ifdef POSEIDON_TARGET_IOS
    if (!ConfigureIOSAudioSessionForPlayback())
        return nullptr;
#endif

    if (!OpenALRuntime::EnsureLoaded())
        return nullptr;

    auto* sys = new SoundSystemOAL();
    if (!sys->Init())
    {
        delete sys;
        return nullptr;
    }
    return sys;
}

void RegisterOpenALAudioBackend()
{
    static bool registered = false;
    if (registered)
        return;

    AudioFactory::Register(AudioBackendDescriptor{
        "openal",
        "OpenAL audio engine",
        1,
        &CreateSoundSystemOAL,
        &OpenALRuntime::IsAvailable,
    });
    registered = true;
}

} // namespace Poseidon
