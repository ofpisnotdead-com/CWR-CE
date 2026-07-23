#pragma once

#include <Poseidon/Foundation/Common/Global.hpp>
#include <Poseidon/Audio/IAudioSystem.hpp>
#include <Poseidon/Audio/Core/AudioState.hpp>
#include <Poseidon/Audio/Streaming/WaveStream.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <PoseidonOpenAL/WaveStreamingBuffers.hpp>
#include <atomic>
#include <vector>


namespace Poseidon
{
class SoundSystemOAL;

class WaveOAL : public IWave
{
    friend class SoundSystemOAL;

  public:
    // Async-load state machine.  Decode reads the stream and produces
    // PCM (dispatched to a TaskPool worker for large assets); Upload
    // runs the AL calls on the main thread, where the OpenAL context is
    // bound.  DoPlay advances a wave through the states each Commit.
    enum class LoadState : int
    {
        NotStarted     = 0, // ctor opened the stream, decode hasn't run
        DecodePending  = 1, // decode in progress (on a worker thread)
        UploadPending  = 2, // PCM ready, awaiting main-thread alBufferData
        Resident       = 3, // alBuffer + alSource live, IsLoaded() == true
        Failed         = 4, // decode or upload failed; _loadError == true
    };

  private:
    SoundSystemOAL* _sys;
    AudioState _state;
    Ref<WaveStream> _stream;

    unsigned int _alBuffer = 0; // ALuint
    unsigned int _alSource = 0; // ALuint

    // `_loadState` is the cross-thread source of truth; `_loaded` /
    // `_loadError` are redundant fast-path flags for call sites that
    // read them on the main thread (a cheap atomic-load shadow).  The
    // PCM staging buffer lives on the WaveOAL between the Decode phase
    // (worker thread) and the Upload phase (main thread); it's moved
    // out and discarded once alBufferData has copied it.
    std::atomic<int>     _loadState{static_cast<int>(LoadState::NotStarted)};
    std::vector<uint8_t> _pendingPcm;

    // Streaming state for OGG sources: they play from the chunk queue
    // instead of the full _pendingPcm buffer, which keeps playback
    // latency at ~5 ms (one-chunk decode) instead of ~200 ms
    // (whole-file decode).
    ::Poseidon::Audio::StreamingBuffers _streaming;

    // Cached WAVEFORMATEX captured by OpenStreaming so the main
    // thread doesn't have to call `_stream->GetFormat` while a
    // worker is mid-`GetData`.  Vorbis's OggVorbis_File is not
    // thread-safe — concurrent ov_info on the main thread while
    // ov_read runs on a worker corrupts the codec state and hangs
    // or crashes (radio_unpause_resumes test caught this under CI).
    WAVEFORMATEX _streamFormat{};

    bool _is3D = false;
    bool _createdAs3D = false; // tracks if wave was created as 3D (DX8: !_only2DEnabled)
    bool _playing = false;
    bool _paused = false;
    bool _wantPlaying = false;
    bool _everPlayed = false;  // FSM: Created vs StoppedReplayable distinction (A-01)
    bool _loaded = false;
    bool _loadError = false;
    // True for OGG sources (any size).  Determines whether playback
    // runs through the chunked streaming path (`OpenStreaming` +
    // `UpdateStreamingPlayback` + queued AL buffers) or the
    // full-decode-then-single-buffer path.  WSS / WAV stay on the
    // latter because they decode fast and the streaming overhead would
    // add a frame of playback latency for sounds that need to be tight
    // (radio chatter, SFX).
    bool _isStreamed = false;

    // "this streamed source is supposed to be playing" — set when
    // DoPlay commits to starting/resuming a streamed wave, cleared by
    // DoStop.  UpdateStreamingPlayback uses it to (re)issue
    // alSourcePlay whenever AL has gone idle but the track has not
    // genuinely ended: recovers a starvation underrun and completes the
    // deferred first start (the first DoPlay frame has no chunk queued
    // yet, so the source can't start until a later Commit fills the ring).
    bool _streamPlayIntent = false;

    float _volume = 1.0f;
    float _accommodation = 1.0f;
    float _volumeAdjust = 1.0f;
    float _gainSet = 0.0f; // cached last-applied AL_GAIN for change detection
    // Last requested pitch ratio (1.0 = native rate).  Stored so DoPlay can
    // re-apply it when the source (re)starts — mirrors 1.99 Wave8's
    // _frequencySet, which the play path reapplies (soundDX8.cpp:1181).
    // Without it a deferred-play source starts at AL's default pitch 1.0 for
    // one frame, then snaps to the real pitch on the next SetVolume — an
    // audible click on (e.g.) vehicle engine start.
    float _pitchRatio = 1.0f;
    float _stopThreshold = FLT_MAX;
    bool _accomEnabled = true;
    // Optional resume-offset applied right after DoPlay starts the AL
    // source.  Used by SoundSystemOAL::StartDevicePreview to seek into
    // the device-picker preview wave on the new context after a
    // SwitchOutputDevice.  Positive seconds; 0 = play from start.
    float _pendingSeekSeconds = 0.0f;
    // Per-wave preview suppression — set by SoundSystemOAL while the
    // Audio screen is mounted so external SetVolume callers (e.g.
    // SoundScene::Simulate writing _musicInternalVolume every frame)
    // can't undo the mute.  ApplyVolume short-circuits to gain 0 when
    // set; the wave keeps playing so its timeline advances.
    bool _previewMute = false;
    WaveKind _kind = WaveEffect;

    Vector3 _position;
    Vector3 _velocity;

    // Queue chaining — mirrors 1.99 DX8 WaveGlobal8::_queue / _queued
    // (soundDX8.cpp:420-498).  When RadioChannel::Say enqueues a follow-
    // up message, it calls `wave->Queue(predecessor)`; predecessor's
    // _queueNext stores `this`, and `this` sets _queued=true to suppress
    // its own playback until predecessor's IsTerminated() poll activates
    // it.  Without this chain, radio messages composed of multiple
    // waves (callsign + content) all start simultaneously.
    Ref<WaveOAL> _queueNext;
    bool _queued = false;
    // Activation is defined as "successor's _queued was cleared", so the
    // idempotency check reads `_queueNext->_queued` directly — no
    // separate tombstone flag to keep in sync.  This also means Restart()
    // doesn't need to remember to reset chain state.

    void LoadHeader();
    void Load();
    // The Load split: `DecodePcm` reads from `_stream` and writes
    // `_pendingPcm` (no AL calls), so it can run on a TaskPool worker;
    // `UploadAlBuffer` consumes `_pendingPcm`, runs alGenBuffers /
    // alBufferData / alGenSources on the main thread (where the OpenAL
    // context is bound), and transitions the wave to
    // `LoadState::Resident`.
    void DecodePcm();
    void UploadAlBuffer();
    // Kick off DecodePcm on a TaskPool worker if the wave is big
    // enough to benefit, otherwise decode synchronously.  Idempotent —
    // repeated calls while a decode is in flight are no-ops.  Called
    // from DoPlay when `_loaded` is false.
    void KickAsyncLoad();
    // Decode one chunkBytes-sized PCM chunk from the current
    // `_streaming.decodeOffsetBytes`, push it onto `_streaming.queue`,
    // advance the offset.  Runs on a TaskPool worker.  Safe to call
    // repeatedly until `_streaming.eofReached` is set.  No AL calls —
    // pure stream→PCM transformation.  Caller is responsible for
    // setting `_streaming.decodeInFlight = false` after the call
    // returns.
    void DecodeOneChunk();
    // Initialize the streaming state on the wave: derive chunkBytes
    // from the format header, pre-populate the freeBuffers list, and
    // allocate the AL buffer ring + source.  Called once before any
    // DecodeOneChunk dispatch.
    void OpenStreaming();
    void Unload();
    void ApplyVolume();
    // Apply the stored _pitchRatio to the AL source via the 1:1 DirectSound
    // pitch mapping.  Called from SetVolume and from DoPlay's start paths so
    // the source begins at the correct pitch (no startup click).
    void ApplyPitch();
    void Apply3D();
    void DoPlay();
    void DoStop();
    // True when a streamed source has gone AL_STOPPED but the track has
    // NOT genuinely ended — a starvation underrun or a not-yet-started
    // deferred play, as opposed to real end-of-stream (eofReached with
    // the ring fully drained).  Lets IsTerminated/IsStopped avoid reaping
    // music on a frame hitch.
    bool IsStreamingUnderrun() const;
    void AttachEFX();  // connect source to EFX reverb slot
    void DetachEFX();  // disconnect source from EFX reverb slot
    // Capture the AL source's current playback offset into _state.curPosition,
    // mirroring DX8 Wave8::StoreCurrentPosition.  Called before alSourceStop /
    // alSourcePause so DoPlay can restore the offset via AL_BYTE_OFFSET on the
    // next Commit — same pattern as Wave8::Play -> SetCurrentPosition(_curPosition).
    void StoreCurrentOffset();
    bool PumpDrainUpload(bool kickDecode);

  public:
    // Diagnostic: live playback offset in seconds.  Reads AL_SEC_OFFSET when
    // the AL source is active, otherwise derives it from the stored
    // _state.curPosition byte offset.  Used by the tri verb
    // triRadioWaveOffset to verify pause/resume preserves position.
    float GetCurrentOffsetSeconds() const override;

    // Async-load observability.  Atomic load — safe to call from any
    // thread (the Commit poller reads this on the main thread while a
    // decode worker writes it from a TaskPool thread).
    LoadState GetLoadState() const
    {
        return static_cast<LoadState>(_loadState.load(std::memory_order_acquire));
    }

    // Streaming-state observability.  Exposed for unit tests that drive
    // `OpenStreaming` + `DecodeOneChunk` directly without involving
    // DoPlay/Commit.  Returns by reference because the streaming state
    // contains atomics that must be read in-place to preserve
    // thread-safety guarantees.
    ::Poseidon::Audio::StreamingBuffers&       StreamingState()       { return _streaming; }
    const ::Poseidon::Audio::StreamingBuffers& StreamingState() const { return _streaming; }

    // Public so unit tests can drive the chunk-by-chunk decode
    // primitives in isolation, without going through DoPlay/Commit.
    void OpenStreamingForTest()   { OpenStreaming(); }
    void DecodeOneChunkForTest()  { DecodeOneChunk(); }

    // Per-frame streaming maintenance.  Called from
    // `SoundSystemOAL::Commit` for every wave with `_isStreamed`.
    // Recycles processed AL buffers, uploads any chunks that the
    // worker has produced, kicks the next decode task if the queue
    // has room and we're not yet at EOF.  Cheap when no work pending.
    bool IsStreamed() const { return _isStreamed; }
    void UpdateStreamingPlayback();
    // streaming pump (SoundSystemOAL::StreamPumpLoop, ~10ms, off the
    // render thread). Split so the slow codec read runs without holding the audio
    // lock and without an owning ref (which would delete a refcount-0 wave):
    //   PumpDrainUpload — jobs (1)(2): recycle played buffers + upload already-
    //     decoded chunks. Caller MUST hold _sys->_audioMutex. Returns true and
    //     latches _streaming.decodeInFlight when a chunk should be decoded.
    //   DecodeQueuedChunk — job (3): decode one chunk lock-free, then clear
    //     decodeInFlight. Safe to call only after PumpDrainUpload returned true;
    //     the wave can't be freed meanwhile because ~WaveOAL waits on decodeInFlight.
    bool PumpDrainUpload();
    void DecodeQueuedChunk();

    // Test-only: the AL_PITCH currently applied to the source (1.0 if no
    // source).  Lets a test prove SetVolume's freq->pitch mapping reaches
    // the real OpenAL source.
    float AppliedPitchForTest() const;

    WaveOAL(SoundSystemOAL* sys, RString name, bool is3D);
    WaveOAL(SoundSystemOAL* sys, float delay);
    ~WaveOAL() override;

    void SetVolumeAdjust(float volEffect, float volSpeech, float volMusic);

    void Queue(IWave* predecessor, int repeat) override;
    void Repeat(int repeat) override;
    void Play() override;
    void Stop() override;
    void Pause() override;
    void Resume() override;
    void LastLoop() override;
    void PlayUntilStopValue(float time) override;
    void SetStopValue(float time) override;

    bool IsWaiting() override;
    void Skip(float deltaT) override;
    void Advance(float deltaT) override;
    float GetLength() const override;
    bool IsStopped() override;
    bool IsTerminated() override;
    void Restart() override;
    WaveState State() override;

    bool IsMuted() const override { return _gainSet <= 1e-20f; }
    float GetVolume() const override { return _volume; }
    void SetVolume(float v, float freq, bool immediate) override;
    float GetFileMaxVolume() const override { return _state.maxVolume; }
    float GetFileAvgVolume() const override { return _state.avgVolume; }

    void SetAccommodation(float a) override
    {
        _accommodation = a;
        ApplyVolume();
    }
    float GetAccommodation() const override { return _accommodation; }
    void EnableAccommodation(bool e) override { _accomEnabled = e; }
    bool AccommodationEnabled() const override { return _accomEnabled; }

    void SetKind(WaveKind k) override;
    WaveKind GetKind() const override { return _kind; }
    float GetVolumeAdjust() const { return _volumeAdjust; }

    void SetPosition(Vector3Par pos, Vector3Par vel, bool) override;
    Vector3 GetPosition() const override { return _position; }
    void Set3D(bool v) override
    {
        _is3D = v;
        Apply3D();
        ApplyVolume(); // DX8: 3D toggle changes volume path (100x boost for 3DOff)
    }
    bool Get3D() const override { return _is3D; }
    float Distance2D() const override { return 1.0f; }
};

} // namespace Poseidon
