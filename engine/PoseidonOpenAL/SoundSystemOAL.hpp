#pragma once

#include <Poseidon/Foundation/Common/Global.hpp>
#include <Poseidon/Audio/IAudioSystem.hpp>
#include <Poseidon/Audio/Shared/PcmCache.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Memory/MemFreeReq.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>


namespace Poseidon
{
class WaveOAL;

class SoundSystemOAL : public IAudioSystem
{
    friend class WaveOAL;

    void* _device = nullptr;  // ALCdevice*
    void* _context = nullptr; // ALCcontext*
    bool _soundReady = false; // DX8:2098 — guards Commit/CreateWave until Init succeeds

    std::vector<WaveOAL*> _waves;

    // Streaming-pump thread (fixes "sound jitters under stress").  Streamed
    // sources (OGG music/speech) refill their AL buffer ring from this thread on
    // a fixed cadence instead of the render-frame Commit(), so a frame stall no
    // longer starves the queue -> no underrun stutter.  _audioMutex serializes
    // the pump's walk of _waves + per-wave streaming state against every
    // main-thread method that touches them (register/unregister, DoPlay/DoStop/
    // Pause/OpenStreaming, the wave dtor, GetCounters).  The hot per-frame param
    // path (SetVolume/SetPosition -> alSourcef/3f) stays lock-free: OpenAL Soft
    // already serializes individual al* calls per source.  Recursive so a locked
    // main-thread op (DoPlay) can call UpdateStreamingPlayback, which also locks.
    mutable std::recursive_mutex _audioMutex;
    std::thread _streamThread;
    std::mutex _streamWakeMutex;
    std::condition_variable _streamWake;
    bool _streamStop = false;
    void StreamPumpLoop();
    void StartStreamThread();
    void StopStreamThread();

    float _waveVolume = 5.0f;
    float _speechVolume = 5.0f;
    float _cdVolume = 5.0f;
    bool _hwAccel = false;

    // EAX/EFX state (DX8 parity: soundDX8.hpp:349-351,374-375)
    bool _eaxEnabled = false;
    bool _canEAX = false;
    unsigned int _efxSlot = 0;   // ALuint — auxiliary effect slot
    unsigned int _efxEffect = 0; // ALuint — EAX reverb effect
    SoundEnvironment _eaxEnv{SEPlain, 100.f, 0.5f}; // DX8:2010 default size=100

    float _volumeAdjustEffect = 1.0f;
    float _volumeAdjustSpeech = 1.0f;
    float _volumeAdjustMusic = 1.0f;

    // Decoded-PCM LRU cache — WaveOAL::DecodePcm consults and populates
    // it; PreloadWave fills it from the mission manifest off the main
    // thread.
    Audio::PcmCache _pcmCache;
    // Surfaces the decoded-PCM cache in the memory-budget panel and lets the
    // global pressure path evict it (thread-safe CPU PCM — see PcmCache::Evict).
    Foundation::MemoryDomainProbe _pcmProbe;

    Vector3 _listenerPos;
    Vector3 _listenerVel;
    Vector3 _listenerDir;
    Vector3 _listenerUp;

    // Preview state (matches SoundSystem8 implementation)
    bool _enablePreview = true;
    bool _doPreview = false;
    uint32_t _previewTime = 0;
    Ref<IWave> _previewSpeech;
    Ref<IWave> _previewEffect;
    Ref<IWave> _previewMusic;

    // Per-category preview (Audio screen volume sliders).  Single wave;
    // restarts on category change or volume change.
    Ref<IWave> _previewCategory;
    WaveKind   _previewCategoryKind = WaveEffect;

    // Device-picker preview — long-form sample looped while the Output
    // Device row is focused.  Wall-clock-based offset tracking: we
    // anchor on the first StartDevicePreview, then compute resume
    // offset as (now - anchor) % length on every subsequent restart.
    // This keeps the playback head advancing through context teardowns
    // and rapid device cycling — the user perceives one continuous
    // track instead of "starts 50 ms in" each click.
    Ref<IWave> _previewDevice;
    uint32_t   _devicePreviewAnchorMs = 0;     // GlobalTickCount() at first start
    float      _devicePreviewLengthSec = 0.0f; // cached, set on first wave load

    // EAX-toggle preview — short 3D effect sample played in a dramatic
    // reverb env so the user can A/B wet vs dry on the toggle.
    Ref<IWave> _previewEax;

    // Per-wave gain suppression for the duration of the Audio screen.
    // Each entry is a wave whose _previewMute flag was set; Resume
    // clears the flag.  Empty when not suppressed.
    std::vector<WaveOAL*> _suppressedMusic;

    // Session-cumulative count of AL alloc failures (alBufferData /
    // alGenSources / alGenBuffers)
    // A-23 / A-29.  Incremented at the failing call site so silent
    // exhaustion under load surfaces as a test/log signal.
    int _allocFailures = 0;

    // Session-cumulative count of StartCategoryPreview calls that
    // created a wave (audio-invariants A-18).  AudioPage drives this
    // on category-row focus changes AND on volume-slider drag — each
    // call restarts the preview so the new gain is audible
    // immediately.
    int _categoryPreviewRestartCount = 0;

    // Per-frame voice-budget counters — written by
    // SoundScene::AdvanceAll via SetVoiceBudgetCounters after each
    // audio::ApplyVoiceBudget3D tick.  Surfaced via GetCounters().
    int _evictedThisFrame = 0;
    int _pausedThisFrame = 0;

    void UpdateMixer();
    void ProcessPreview();

    // EFX internals (1:1 port of DX8 InitEAX/DeinitEAX/DoSetEAXEnvironment)
    bool InitEFX();
    void DeinitEFX();
    void DoSetEAXEnvironment();
    void ApplyEFXPreset(float envSize);

  public:
    SoundSystemOAL();
    ~SoundSystemOAL() override;

    bool Init();

    void RegisterWave(WaveOAL* wave);
    void UnregisterWave(WaveOAL* wave);

    float GetWaveDuration(const char* filename) override;
    IWave* CreateEmptyWave(float duration) override;
    IWave* CreateWave(const char* filename, bool is3D = true, bool prealloc = false) override;
    IWave* CreateWaveFromMemory(const void* data, size_t size, const char* ext, bool is3D) override;

    void SetListener(Vector3Par pos, Vector3Par vel, Vector3Par dir, Vector3Par up) override;
    Vector3 GetListenerPosition() const override { return _listenerPos; }
    void Commit() override;
    void Activate(bool active) override;
    void SetEnvironment(const SoundEnvironment& env) override;

    float GetCDVolume() const override { return _cdVolume; }
    void SetCDVolume(float v) override;
    void SetWaveVolume(float v) override;
    float GetWaveVolume() override { return _waveVolume; }
    void SetSpeechVolume(float v) override;
    float GetSpeechVolume() override { return _speechVolume; }

    void StartPreview() override;
    void TerminatePreview() override;
    void StartCategoryPreview(WaveKind kind) override;
    void PreloadWave(const char* name) override;
    int WaveCacheEntries() const override { return static_cast<int>(_pcmCache.GetStats().entries); }
    int WaveCacheHits() const override { return static_cast<int>(_pcmCache.GetStats().hits); }
    void StartDevicePreview() override;
    void StartEAXPreview() override;
    void SuppressMusicForPreview() override;
    void ResumeMusicForPreview() override;
    int  CountSuppressedMusicWaves() const override { return (int)_suppressedMusic.size(); }
    int  CountCategoryPreviewRestarts() const override { return _categoryPreviewRestartCount; }
    AudioCounters GetCounters() const override;
    void SetVoiceBudgetCounters(int evictedThisFrame, int pausedThisFrame) override;
    void FlushBank(QFBank*) override {}

    bool EnableHWAccel(bool v) override;
    bool EnableEAX(bool val) override;
    bool GetEAX() const override { return _eaxEnabled; }
    bool ApplyEFXByName(const char* presetName, float size) override;
    bool GetHWAccel() const override { return false; }

    void LoadConfig() override;
    void SaveConfig() override;

    std::vector<std::string> ListOutputDevices() const override;
    std::string              GetCurrentOutputDevice() const override;
    bool                     SwitchOutputDevice(const char* name) override;

    // Selected mic device name — persisted alongside _selectedDevice
    // in audio.cfg.  Read by AudioPage on mount to seed the picker;
    // updated when the user cycles the picker.
    const std::string& GetSelectedInputDevice() const override { return _selectedInputDevice; }
    void               SetSelectedInputDevice(const std::string& name) override { _selectedInputDevice = name; }

  private:
    // Selected device name; empty means "system default" (passed as
    // nullptr to alcOpenDevice).  Persisted via LoadConfig/SaveConfig
    // alongside the volumes.
    std::string _selectedDevice;
    std::string _selectedInputDevice;
};

IAudioSystem* CreateSoundSystemOAL();

} // namespace Poseidon
