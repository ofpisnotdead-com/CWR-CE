#include <Poseidon/Foundation/Common/Global.hpp>
#include <PoseidonOpenAL/WaveOAL.hpp>
#include <PoseidonOpenAL/SoundSystemOAL.hpp>
#include <Poseidon/Audio/Streaming/WaveLoaders.hpp>
#include <Poseidon/Audio/Streaming/WaveStream.hpp>
#include <Poseidon/Audio/Core/WaveLogic.hpp>
#include <Poseidon/Audio/Shared/AudioMath.hpp>
#include <Poseidon/Audio/Shared/DX8Reference.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Dev/Diag/ScopedTimer.hpp>
#include <Poseidon/Core/TaskPool.hpp>
#include <PoseidonOpenAL/OpenALRuntime.hpp>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>


namespace Poseidon
{
static ALenum OALFormat(const WAVEFORMATEX& fmt)
{
    if (fmt.nChannels == 1)
        return fmt.wBitsPerSample == 8 ? AL_FORMAT_MONO8 : AL_FORMAT_MONO16;
    return fmt.wBitsPerSample == 8 ? AL_FORMAT_STEREO8 : AL_FORMAT_STEREO16;
}

WaveOAL::WaveOAL(SoundSystemOAL* sys, RString name, bool is3D) : IWave(name), _sys(sys), _is3D(is3D), _createdAs3D(is3D)
{
    // Match DX8 WaveBuffers8::Reset() — initialize _volumeAdjust from current mixer state
    _volumeAdjust = _sys->_volumeAdjustEffect;

    // Phase A measurement: file-open + decoder-init cost (the
    // `SoundLoadFile` → `ov_open_callbacks` path for OGG; the
    // equivalent header parse for WAV/WSS).  Split out from the
    // bigger `WaveOAL::Load` decode so we can see open vs full-decode
    // separately.  Threshold filters voice-line noise.
    const auto _perfOpenStart = ::Poseidon::Dev::Perf::Now();

    const char* nameStr = static_cast<const char*>(name);
    if (!nameStr || !nameStr[0])
    {
        // Empty sound name — a config slot with no sound assigned (common for
        // some weapons/vehicles). Nothing to load; not an error. Logging it trips
        // --strict during mass combat where many such slots are touched.
        _loadError = true;
        return;
    }
    _stream = SoundLoadFile(nameStr);
    if (!_stream)
    {
        _loadError = true;
        LOG_ERROR(Audio, "Load failed: '{}' (file not found or unsupported format)", nameStr);
        return;
    }
    LoadHeader();

    // Phase 3g: route OGG sources through the chunked streaming
    // path.  Case-insensitive .ogg suffix is the cheap test that
    // catches every music + voice OGG asset in the demo and the
    // full game.  WSS / WAV stay on the Phase 3c/3d full-decode
    // path because they're small and decode fast — streaming
    // would add a frame of playback latency without a perceptual
    // win.
    const char* n = static_cast<const char*>(name);
    if (const size_t nlen = n ? std::strlen(n) : 0; nlen >= 4)
    {
        const char* ext = n + nlen - 4;
        if ((ext[0] == '.' || ext[0] == 0) &&
            (ext[1] == 'o' || ext[1] == 'O') &&
            (ext[2] == 'g' || ext[2] == 'G') &&
            (ext[3] == 'g' || ext[3] == 'G'))
        {
            _isStreamed = true;
        }
    }

    _sys->RegisterWave(this);

    const double _perfOpenMs = ::Poseidon::Dev::Perf::ElapsedMs(_perfOpenStart);
    if (_perfOpenMs >= 1.0)
    {
        LOG_DEBUG(Audio, "PERF: WaveOAL::Open {} took {:.2f}ms", static_cast<const char*>(name), _perfOpenMs);
    }
    ::Poseidon::Dev::Perf::EmitTraceEventAsset(Poseidon::Foundation::LogCategory::Audio, "WaveOAL::Open", _perfOpenStart,
                                          static_cast<const char*>(name));
}

WaveOAL::WaveOAL(SoundSystemOAL* sys, float delay) : IWave(GetPauseName()), _sys(sys)
{
    // Match DX8 WaveBuffers8::Reset() — initialize _volumeAdjust from current mixer state
    _volumeAdjust = _sys->_volumeAdjustEffect;

    // Empty wave — delay placeholder.  Mirror 1.99 Wave8(sys, delay)
    // (soundDX8.cpp:2158): negative _curPosition encodes the delay in
    // "ms units" so Skip can count it down via the virtual frequency
    // (1 kHz, 1-byte samples → 1 unit per ms).  Without these the
    // pause never reaches >= 0 and the Queue-chain walk hangs on it.
    _state.frequency = 1000;
    _state.sSize = 1;
    _state.size = 0;
    if (delay > 0.f)
        _state.curPosition = static_cast<int>(delay * -1000.f);
    _sys->RegisterWave(this);
}

WaveOAL::~WaveOAL()
{
    // the streaming pump (StreamPumpLoop) decodes a chunk lock-free,
    // without holding an owning ref on this wave (it can't — a wave may be at
    // refcount 0). Signal it to bail, then WAIT for any in-flight decode to
    // finish before we free anything it touches (_stream / _streaming). The pump
    // clears decodeInFlight the instant DecodeQueuedChunk returns.
    _streaming.aborted.store(true, std::memory_order_release);
    while (_streaming.decodeInFlight.load(std::memory_order_acquire))
    {
        std::this_thread::yield();
    }

    // Serialize the rest of teardown against the pump's drain/upload pass (Pass 1
    // holds _audioMutex), so it can't be mid-walk on us when UnregisterWave runs.
    std::lock_guard<std::recursive_mutex> lk(_sys->_audioMutex);

    _sys->UnregisterWave(this);

    if (_alSource)
    {
        alSourceStop(_alSource);
        // For streamed sources, unqueue everything before deleting.
        if (_isStreamed)
        {
            ALint queued = 0;
            alGetSourcei(_alSource, AL_BUFFERS_QUEUED, &queued);
            while (queued-- > 0)
            {
                ALuint b = 0;
                alSourceUnqueueBuffers(_alSource, 1, &b);
            }
        }
        alDeleteSources(1, &_alSource);
        _alSource = 0;
    }
    if (_alBuffer)
    {
        alDeleteBuffers(1, &_alBuffer);
        _alBuffer = 0;
    }
    // Streamed AL buffer ring.
    if (_streaming.alBuffers[0] != 0)
    {
        alDeleteBuffers(::Poseidon::Audio::StreamingBuffers::kNumBuffers, _streaming.alBuffers);
        for (int i = 0; i < ::Poseidon::Audio::StreamingBuffers::kNumBuffers; ++i)
        {
            _streaming.alBuffers[i] = 0;
        }
    }
}

void WaveOAL::LoadHeader()
{
    if (!_stream || _loadError)
        return;

    WAVEFORMATEX fmt;
    _stream->GetFormat(fmt);

    int size = _stream->GetUncompressedSize();
    if (size <= 0)
    {
        _loadError = true;
        LOG_ERROR(Audio, "Header FAIL: {} (size={})", static_cast<const char*>(Name()), size);
        return;
    }

    _state.size = static_cast<unsigned int>(size);
    _state.frequency = static_cast<long>(fmt.nSamplesPerSec);
    _state.nChannel = fmt.nChannels;
    _state.sSize = fmt.wBitsPerSample / 8;

    if (fmt.nAvgBytesPerSec > 0)
        _state.maxVolume = 1.f;
}

void WaveOAL::DecodePcm()
{
    // Worker-thread-safe (Phase 3d): no AL calls, only stream reads
    // into `_pendingPcm`.  Touches `_stream`, `_state.size`, `_loadError`,
    // and `_loadState` — none of which the main thread mutates while
    // a decode is in flight (Phase 3d enforces that contract via the
    // state machine: DecodePending is the only state where the worker
    // may touch the wave, and the main thread never writes _stream
    // or _pendingPcm in that window).
    if (!_stream || _loaded || _loadError)
    {
        return;
    }

    const auto _perfDecodeStart = ::Poseidon::Dev::Perf::Now();

    _loadState.store(static_cast<int>(LoadState::DecodePending), std::memory_order_release);

    // PCM cache (asset-loader plan Phase B): a manifest-preloaded or
    // previously-decoded wave skips the stream read + decode entirely.
    // Streamed waves (OGG) never participate — they consume the stream
    // chunk-wise and the full-PCM blob would be music-sized.
    if (!_isStreamed && _sys)
    {
        if (auto hit = _sys->_pcmCache.Find(static_cast<const char*>(Name())))
        {
            _pendingPcm.assign(hit->pcm.begin(), hit->pcm.end());
            _loadState.store(static_cast<int>(LoadState::UploadPending), std::memory_order_release);
            ::Poseidon::Dev::Perf::EmitTraceEventAssetNum(Poseidon::Foundation::LogCategory::Audio,
                                                          "WaveOAL::DecodeCacheHit", _perfDecodeStart,
                                                          static_cast<const char*>(Name()), "bytes",
                                                          static_cast<long long>(_state.size));
            return;
        }
    }

    _pendingPcm.assign(static_cast<size_t>(_state.size), 0);
    _stream->GetData(_pendingPcm.data(), 0, static_cast<int>(_state.size));

    if (!_isStreamed && _sys && _state.size <= Audio::PcmCache::kMaxEntryBytes)
    {
        auto entry = std::make_shared<Audio::PcmCache::Entry>();
        _stream->GetFormat(entry->fmt);
        entry->uncompressedSize = _state.size;
        entry->pcm = _pendingPcm;
        _sys->_pcmCache.Insert(static_cast<const char*>(Name()), std::move(entry));
    }

    _loadState.store(static_cast<int>(LoadState::UploadPending), std::memory_order_release);

    // PERF event tags which thread ran the decode — the validation
    // story for Phase 3d is "Decode runs on tid != 1 for large
    // assets".  Without this emission the trace can't tell us
    // whether async dispatch actually engaged.
    ::Poseidon::Dev::Perf::EmitTraceEventAssetNum(Poseidon::Foundation::LogCategory::Audio, "WaveOAL::Decode", _perfDecodeStart,
                                             static_cast<const char*>(Name()), "bytes",
                                             static_cast<long long>(_state.size));
}

void WaveOAL::UploadAlBuffer()
{
    // Main-thread only — touches OpenAL state which is bound to the
    // main thread.  Consumes `_pendingPcm` produced by `DecodePcm`.
    if (_loaded || _loadError)
    {
        return;
    }
    if (_loadState.load(std::memory_order_acquire) != static_cast<int>(LoadState::UploadPending))
    {
        return;
    }

    const auto _perfUploadStart = ::Poseidon::Dev::Perf::Now();

    WAVEFORMATEX fmt;
    _stream->GetFormat(fmt);

    // Clear any stale AL error from earlier in the frame (e.g.
    // SwitchOutputDevice's context teardown can leave one) so our
    // alGenBuffers/alBufferData status check actually reflects this
    // load's outcome.
    alGetError();

    alGenBuffers(1, &_alBuffer);
    alBufferData(_alBuffer, OALFormat(fmt), _pendingPcm.data(), static_cast<ALsizei>(_state.size),
                 static_cast<ALsizei>(fmt.nSamplesPerSec));
    ALenum err = alGetError();
    if (err != AL_NO_ERROR)
    {
        alDeleteBuffers(1, &_alBuffer);
        _alBuffer = 0;
        _loadError = true;
        _loadState.store(static_cast<int>(LoadState::Failed), std::memory_order_release);
        if (_sys)
        {
            ++_sys->_allocFailures;
        }
        LOG_ERROR(Audio, "Buffer error: {} (al=0x{:x} fmt={} ch={} bps={} size={} rate={})",
                  static_cast<const char*>(Name()), err, OALFormat(fmt), fmt.nChannels, fmt.wBitsPerSample, _state.size,
                  fmt.nSamplesPerSec);
        _pendingPcm.clear();
        _pendingPcm.shrink_to_fit();
        return;
    }

    alGenSources(1, &_alSource);
    ALenum srcErr = alGetError();
    if (srcErr != AL_NO_ERROR || _alSource == 0)
    {
        // Source pool exhausted (typically AL_OUT_OF_MEMORY).  Mark
        // the wave as failed-to-load and surface via _allocFailures —
        // audio-invariants A-23 / A-29.
        alDeleteBuffers(1, &_alBuffer);
        _alBuffer = 0;
        _alSource = 0;
        _loadError = true;
        _loadState.store(static_cast<int>(LoadState::Failed), std::memory_order_release);
        if (_sys)
        {
            ++_sys->_allocFailures;
        }
        LOG_WARN(Audio, "alGenSources failed for wave={} (al=0x{:x})", static_cast<const char*>(Name()), srcErr);
        _pendingPcm.clear();
        _pendingPcm.shrink_to_fit();
        return;
    }
    alSourcei(_alSource, AL_BUFFER, static_cast<ALint>(_alBuffer));

    Apply3D();
    ApplyVolume();
    _loaded = true;
    _loadState.store(static_cast<int>(LoadState::Resident), std::memory_order_release);

    // Release the staging PCM buffer — data is now in the AL buffer.
    // Keep `_stream` alive: GL-12 (commit fa99c9f1d) made WaveOAL::Stop
    // unload AL resources and Restart re-Load via the same _stream so
    // cached weapon-shot waves can replay after external Stop().  My
    // Phase 3c split nulled _stream at the end of the old Load(); the
    // rebase onto GL-12 restored the keep-_stream behaviour here.
    _pendingPcm.clear();
    _pendingPcm.shrink_to_fit();

    LOG_DEBUG(Audio, "Loaded: {} ({}Hz {}ch {}s)", static_cast<const char*>(Name()), static_cast<int>(_state.frequency),
              _state.nChannel, GetLength());

    // PERF event for the main-thread upload.  Always tid=1; if this
    // dominates the perf trace's main-thread time then either decode
    // hasn't actually moved off-thread, or alBufferData itself is
    // the long pole.
    ::Poseidon::Dev::Perf::EmitTraceEventAssetNum(Poseidon::Foundation::LogCategory::Audio, "WaveOAL::Upload", _perfUploadStart,
                                             static_cast<const char*>(Name()), "bytes",
                                             static_cast<long long>(_state.size));
}

void WaveOAL::OpenStreaming()
{
    std::lock_guard<std::recursive_mutex> lk(_sys->_audioMutex);
    // Phase 3g: prepare the streaming state for chunked decode +
    // allocate the AL buffer ring + source.  Chunk size is
    // StreamingBuffers::kChunkMs (100 ms) of PCM bytes derived
    // from the format header.  Rounded down to a sample boundary
    // so partial frames never split a chunk.
    if (!_stream || _loadError)
    {
        return;
    }

    // Cache the format up front so UpdateStreamingPlayback never
    // has to call `_stream->GetFormat` from the main thread while
    // a worker is mid-`GetData` — Vorbis's OggVorbis_File is NOT
    // thread-safe, and concurrent ov_info / ov_read on the same
    // file corrupts state.
    _stream->GetFormat(_streamFormat);

    const int sampleStride = _streamFormat.nChannels * (_streamFormat.wBitsPerSample / 8);
    if (sampleStride <= 0 || _streamFormat.nSamplesPerSec <= 0)
    {
        _loadError = true;
        return;
    }
    const int bytesPerSec = static_cast<int>(_streamFormat.nSamplesPerSec) * sampleStride;
    int chunkBytes = (bytesPerSec * ::Poseidon::Audio::StreamingBuffers::kChunkMs) / 1000;
    // Round down to a sample boundary so chunks always end on a
    // sample edge (AL doesn't care, but it keeps decode-resume
    // semantics clean — the next chunk picks up exactly where the
    // previous one ended).
    chunkBytes -= (chunkBytes % sampleStride);
    if (chunkBytes <= 0)
    {
        chunkBytes = sampleStride; // pathologically small but valid
    }

    _streaming.chunkBytes = chunkBytes;
    _streaming.decodeOffsetBytes.store(0, std::memory_order_release);
    _streaming.consumedBytes.store(0, std::memory_order_release);
    _streaming.eofReached.store(false, std::memory_order_release);
    _streaming.decodeInFlight.store(false, std::memory_order_release);
    _streaming.aborted.store(false, std::memory_order_release);
    _streaming.buffersInFlight = 0;
    _streaming.queue.Clear();
    _streaming.freeBuffers.clear();

    // Phase 3g-3: AL resource allocation for the streamed path.
    // alGenBuffers(N) for the ring, alGenSources(1) for the player.
    // Skip if already allocated (re-Open during teardown / re-open
    // races).  Buffer IDs stay valid for the wave's lifetime; they
    // get queued / unqueued + recycled as chunks flow through.
    if (_streaming.alBuffers[0] == 0)
    {
        alGetError();
        alGenBuffers(::Poseidon::Audio::StreamingBuffers::kNumBuffers, _streaming.alBuffers);
        if (alGetError() != AL_NO_ERROR)
        {
            _loadError = true;
            _loadState.store(static_cast<int>(LoadState::Failed), std::memory_order_release);
            LOG_ERROR(Audio, "Stream alGenBuffers failed: {}", static_cast<const char*>(Name()));
            return;
        }
        _streaming.freeBuffers.reserve(::Poseidon::Audio::StreamingBuffers::kNumBuffers);
        for (int i = 0; i < ::Poseidon::Audio::StreamingBuffers::kNumBuffers; ++i)
        {
            _streaming.freeBuffers.push_back(_streaming.alBuffers[i]);
        }
    }
    if (_alSource == 0)
    {
        alGetError();
        alGenSources(1, &_alSource);
        if (alGetError() != AL_NO_ERROR || _alSource == 0)
        {
            _loadError = true;
            _loadState.store(static_cast<int>(LoadState::Failed), std::memory_order_release);
            LOG_ERROR(Audio, "Stream alGenSources failed: {}", static_cast<const char*>(Name()));
            return;
        }
    }
}

bool WaveOAL::PumpDrainUpload()
{
    // pump-thread jobs (1)(2). CALLER HOLDS _sys->_audioMutex (the
    // StreamPumpLoop). Touches only _streaming bookkeeping + the AL buffer queue
    // (never play-state). Returns true (and latches decodeInFlight) when the
    // caller should follow up with DecodeQueuedChunk() outside the lock.
    if (!_isStreamed || !_alSource || _loadError)
    {
        return false;
    }
    if (_streaming.aborted.load(std::memory_order_acquire))
    {
        return false;
    }

    // (1) Drain processed buffers.  Each unqueued buffer's PCM has been fully
    // played; accumulate its size into consumedBytes so GetCurrentOffsetSeconds
    // reports cumulative position (AL_BYTE_OFFSET on a queued source only reports
    // position within the currently-playing buffer).
    ALint processed = 0;
    alGetSourcei(_alSource, AL_BUFFERS_PROCESSED, &processed);
    while (processed-- > 0)
    {
        ALuint buf = 0;
        alSourceUnqueueBuffers(_alSource, 1, &buf);
        if (buf != 0)
        {
            _streaming.freeBuffers.push_back(buf);
            if (_streaming.buffersInFlight > 0)
            {
                --_streaming.buffersInFlight;
            }
            _streaming.consumedBytes.fetch_add(_streaming.chunkBytes, std::memory_order_release);
        }
    }

    // (2) Upload any chunks decoded on a previous tick.
    while (!_streaming.freeBuffers.empty())
    {
        ::Poseidon::Audio::DecodedChunk chunk;
        if (!_streaming.queue.TryPop(chunk))
        {
            break;
        }
        const ALuint buf = _streaming.freeBuffers.back();
        _streaming.freeBuffers.pop_back();
        // Use the cached format from OpenStreaming — DO NOT call `_stream->GetFormat`
        // here: it touches the same OggVorbis_File state as the GetData decode in
        // DecodeQueuedChunk; serialize via the cached format.
        alGetError();
        alBufferData(buf, OALFormat(_streamFormat), chunk.pcm.data(), static_cast<ALsizei>(chunk.pcm.size()),
                     static_cast<ALsizei>(_streamFormat.nSamplesPerSec));
        if (alGetError() != AL_NO_ERROR)
        {
            _streaming.freeBuffers.push_back(buf); // return on failure
            // Abort via the atomic flag rather than the main-thread-only _loadError,
            // since this runs on the pump thread; recovery + IsTerminated honour it.
            _streaming.aborted.store(true, std::memory_order_release);
            LOG_ERROR(Audio, "Stream alBufferData failed: {}", NameCStr());
            return false;
        }
        alSourceQueueBuffers(_alSource, 1, &buf);
        ++_streaming.buffersInFlight;
    }

    // (3) decision: decode another chunk if there's room and content left. Latch
    // decodeInFlight here (under the lock) so the dtor's wait sees it before we
    // release the lock and the caller decodes.
    const bool needMore = !_streaming.eofReached.load(std::memory_order_acquire) &&
                          static_cast<int>(_streaming.queue.Size()) + _streaming.buffersInFlight <
                              ::Poseidon::Audio::StreamingBuffers::kNumBuffers;
    if (!needMore)
    {
        return false;
    }
    bool expected = false;
    return _streaming.decodeInFlight.compare_exchange_strong(expected, true, std::memory_order_acq_rel);
}

void WaveOAL::DecodeQueuedChunk()
{
    // pump-thread job (3): decode one chunk lock-free (the codec read
    // is multi-ms — must not hold _audioMutex). Only called after PumpDrainUpload
    // latched decodeInFlight, which keeps the wave alive (~WaveOAL waits on it).
    // Same lock-free codec model as the former TaskPool worker (atomic
    // decodeInFlight/aborted + the ChunkQueue's own mutex).
    const auto _perfDecodeStart = ::Poseidon::Dev::Perf::Now();
    DecodeOneChunk();
    _streaming.decodeInFlight.store(false, std::memory_order_release);
    ::Poseidon::Dev::Perf::EmitTraceEventAsset(Poseidon::Foundation::LogCategory::Audio, "WaveOAL::StreamChunk",
                                               _perfDecodeStart, NameCStr());
}

void WaveOAL::UpdateStreamingPlayback()
{
    // Main-thread per-frame entry (SoundSystemOAL::Commit + DoPlay). The buffer
    // ring + decode are driven by the StreamPumpLoop thread (PumpDrainUpload +
    // this does only start/restart, which touches play-state (_playing/_state/
    // _paused) and so MUST stay on the main thread — the pump thread never runs it.
    std::lock_guard<std::recursive_mutex> lk(_sys->_audioMutex);

    if (!_isStreamed || !_alSource || _loadError)
    {
        return;
    }
    if (_streaming.aborted.load(std::memory_order_acquire))
    {
        return;
    }

    // (4) Underrun / deferred-start recovery.  If this streamed source is
    // supposed to be playing but AL has gone idle, (re)start it as long as
    // there is queued audio left to play and the track has not genuinely
    // ended.  This recovers a starvation underrun (a frame hitch drained
    // the ring → AL_STOPPED) and completes the deferred first start (the
    // first DoPlay had no chunk queued yet).  The !eofReached guard keeps
    // a real end-of-track stopped so IsTerminated can reap it; mirrors the
    // VoIP speaker's "stopped (underrun) → just restart" recovery.
    if (_streamPlayIntent && !_paused && !_state.terminated &&
        !_streaming.eofReached.load(std::memory_order_acquire))
    {
        ALint alState = AL_INITIAL;
        alGetSourcei(_alSource, AL_SOURCE_STATE, &alState);
        if (alState != AL_PLAYING && alState != AL_PAUSED)
        {
            ALint queued = 0;
            alGetSourcei(_alSource, AL_BUFFERS_QUEUED, &queued);
            if (queued > 0)
            {
                alSourcePlay(_alSource);
                _playing          = true;
                _state.playing    = true;
                _everPlayed       = true;
                _state.terminated = false;
            }
        }
    }
}

void WaveOAL::DecodeOneChunk()
{
    // Phase 3g-2 worker entry point.  Decodes one chunkBytes-sized
    // PCM slice and pushes it onto the queue.  Designed to be called
    // from a TaskPool worker; safe to call again until eofReached.
    // No AL calls (those happen on the main thread in Phase 3g-3's
    // upload-and-queue pass).
    if (_streaming.aborted.load(std::memory_order_acquire))
    {
        return;
    }
    if (!_stream || _loadError || _streaming.eofReached.load(std::memory_order_acquire))
    {
        return;
    }
    if (_streaming.chunkBytes <= 0)
    {
        return;
    }

    int64_t   offset    = _streaming.decodeOffsetBytes.load(std::memory_order_acquire);
    const int totalSize = static_cast<int>(_state.size);

    // Phase 3g-5: looping via worker re-queue.  If we've reached
    // EOF and the wave is set to loop, wrap the offset to 0 instead
    // of flagging EOF — the AL source keeps consuming the queue,
    // and the worker keeps producing chunks indefinitely.  The
    // result is sample-accurate looping by construction (no AL
    // loop-flag re-trigger glitch, no source restart).
    if (totalSize > 0 && offset >= totalSize)
    {
        if (_state.looping)
        {
            offset = 0;
            _streaming.decodeOffsetBytes.store(0, std::memory_order_release);
        }
        else
        {
            _streaming.eofReached.store(true, std::memory_order_release);
            return;
        }
    }

    int wantBytes = _streaming.chunkBytes;
    bool isLast = false;
    if (totalSize > 0 && offset + wantBytes >= totalSize)
    {
        // The last chunk runs short of a full chunkBytes; clip it.
        // For looping waves this is just the wrap chunk — the
        // worker keeps producing after.
        wantBytes = static_cast<int>(totalSize - offset);
        if (!_state.looping)
        {
            isLast = true;
        }
    }

    ::Poseidon::Audio::DecodedChunk chunk;
    chunk.pcm.assign(static_cast<size_t>(wantBytes), 0);
    chunk.streamOffset = offset;
    const int gotBytes = _stream->GetData(chunk.pcm.data(), static_cast<int>(offset), wantBytes);
    if (gotBytes <= 0)
    {
        // Treat zero-byte read as EOF — defensive against decoders
        // that signal end-of-stream by returning 0 rather than
        // matching the GetUncompressedSize header value.  For loops
        // the same wrap-on-EOF logic applies on the next call.
        if (_state.looping)
        {
            _streaming.decodeOffsetBytes.store(0, std::memory_order_release);
        }
        else
        {
            _streaming.eofReached.store(true, std::memory_order_release);
        }
        return;
    }
    if (gotBytes < wantBytes && !_state.looping)
    {
        // Short read at EOF on a non-looping wave — keep what we
        // have, mark it as the last chunk.
        chunk.pcm.resize(static_cast<size_t>(gotBytes));
        isLast = true;
    }
    chunk.isLastChunk = isLast;

    _streaming.decodeOffsetBytes.store(offset + gotBytes, std::memory_order_release);
    if (isLast)
    {
        _streaming.eofReached.store(true, std::memory_order_release);
    }
    _streaming.queue.Push(std::move(chunk));
}

void WaveOAL::KickAsyncLoad()
{
    // Phase 3d: dispatch DecodePcm to a TaskPool worker if the wave
    // is large enough to benefit from async decode, otherwise decode
    // synchronously on the calling (main) thread.  Idempotent —
    // repeated calls while a decode is in flight are no-ops via CAS.
    //
    // The threshold matches the Phase A baseline shape: music tracks
    // (>=10 MB PCM) dominate the main-thread hitch and reliably want
    // async; voice lines and SFX (<1 MB) decode in 1-10 ms each and
    // would suffer a 16-33 ms playback delay if forced through the
    // worker round-trip.  1 MB cleanly separates the two populations.
    constexpr unsigned int kAsyncThresholdBytes = 1u * 1024u * 1024u;

    if (_loaded || _loadError)
    {
        return;
    }

    int expected = static_cast<int>(LoadState::NotStarted);
    if (!_loadState.compare_exchange_strong(expected, static_cast<int>(LoadState::DecodePending),
                                            std::memory_order_acq_rel))
    {
        // Decode already in flight, or wave has finished loading /
        // hit a failure.  Either way, nothing to do here.
        return;
    }

    auto* pool = ::Poseidon::GetGlobalTaskPool();
    if (_state.size < kAsyncThresholdBytes || !pool)
    {
        // Small asset or no worker pool (server / tools / very early
        // startup) — decode synchronously.  DecodePcm sets the state
        // to UploadPending on success, leaving us at the same place
        // as the async path's worker completion.
        LOG_DEBUG(Audio, "WaveOAL::KickAsyncLoad sync-fallback: {} ({} bytes, pool={})",
                  static_cast<const char*>(Name()), _state.size, pool ? "live" : "null");
        DecodePcm();
        return;
    }

    // Async dispatch.  AddRef keeps the wave alive across the decode
    // — TaskPool::Submit takes the lambda by value, so the captured
    // `this` plus the explicit refcount bump guarantees the object
    // outlives the worker even if every other holder drops their Ref
    // before decode completes.
    LOG_DEBUG(Audio, "WaveOAL::KickAsyncLoad async-dispatch: {} ({} bytes)",
              static_cast<const char*>(Name()), _state.size);
    AddRef();
    pool->Submit([this]()
    {
        DecodePcm();
        Release();
    });
}

void WaveOAL::Load()
{
    if (!_stream || _loaded || _loadError)
        return;

    // Phase A measurement.  `WaveOAL::Load` is the dominant cause of
    // the first-touch hitch in the demo intro (full Vorbis decode of
    // a music track lands here synchronously on the main thread).
    // Phase 3c split this into Decode (worker-safe) + Upload (main-
    // thread).  This wrapper preserves the synchronous contract so
    // the lazy-load-on-DoPlay call site keeps working unchanged.
    // Phase 3d will route around this wrapper to dispatch Decode
    // asynchronously while leaving Upload synchronous on Commit.
    const auto _perfLoadStart = ::Poseidon::Dev::Perf::Now();

    DecodePcm();
    if (_loadError)
    {
        return;
    }
    UploadAlBuffer();
    if (_loadError)
    {
        return;
    }

    LOG_DEBUG(Audio, "PERF: WaveOAL::Load {} ({} bytes PCM) took {:.2f}ms", static_cast<const char*>(Name()),
              _state.size, ::Poseidon::Dev::Perf::ElapsedMs(_perfLoadStart));
    ::Poseidon::Dev::Perf::EmitTraceEventAssetNum(Poseidon::Foundation::LogCategory::Audio, "WaveOAL::Load", _perfLoadStart,
                                             static_cast<const char*>(Name()), "bytes",
                                             static_cast<long long>(_state.size));
}

void WaveOAL::Unload()
{
    if (!_loaded)
        return;

    if (_alSource)
    {
        alSourceStop(_alSource);
        alDeleteSources(1, &_alSource);
        _alSource = 0;
    }
    if (_alBuffer)
    {
        alDeleteBuffers(1, &_alBuffer);
        _alBuffer = 0;
    }
    _loaded = false;
    _loadState.store(static_cast<int>(LoadState::NotStarted), std::memory_order_release);
}

void WaveOAL::ApplyVolume()
{
    if (!_alSource)
        return;

    if (_previewMute)
    {
        // Audio screen ducked this wave so its previews aren't fighting
        // background music.  Force gain to 0 even if some other caller
        // (SoundScene::Simulate, mixer update, etc.) just wrote a
        // non-zero _volume / _volumeAdjust.
        if (_gainSet != 0.f)
        {
            alSourcef(_alSource, AL_GAIN, 0.f);
            _gainSet = 0.f;
        }
        return;
    }

    float gain;
    if (_is3D)
    {
        // DX8 path 3: 3D enabled — volume is in MinDistance, buffer gain is just the adjust
        gain = AudioMath::Gain3D(_volumeAdjust);
    }
    else if (_createdAs3D)
    {
        // DX8 path 2: 3D buffer with 3D disabled (Set3D(false) — e.g. inside vehicle)
        float accom = _accomEnabled ? _accommodation : 1.f;
        gain = _volume * accom * _volumeAdjust * 100.f;
        if (gain > 1.f)
            gain = 1.f; // DX8 float2db clamps at 0 dB (= gain 1.0)
        LOG_DEBUG(Audio, "3DOff boost: {} vol={} accom={} adj={}", static_cast<const char*>(Name()), _volume, accom,
                  _volumeAdjust);
    }
    else
    {
        // DX8 path 1: pure 2D — vol * accom * adjust
        gain = AudioMath::Gain2D(_volume, _accommodation, _accomEnabled, _volumeAdjust);
    }

    // Only update OpenAL if gain changed significantly (ArmA3:423)
    static const float minRelDiff = 1e-2f;
    if (_playing)
    {
        if (std::fabs(gain - _gainSet) > minRelDiff * gain)
        {
            alSourcef(_alSource, AL_GAIN, gain);
            // Level-comparison line — same shape as the DX8-ref's per-update
            // `audio play ... volume_db100=` log so A/B runs diff directly.
            LOG_DEBUG(Audio, "audio gain sound={} gain={:.4f} db={:.0f} 3d={} kind={} vol={:.3f} adj={:.3f}",
                      static_cast<const char*>(Name()), gain, gain > 0.f ? 2000.f * std::log10(gain) : -10000.f,
                      _is3D ? 1 : 0, static_cast<int>(_kind), _volume, _volumeAdjust);
        }
    }
    else
    {
        alSourcef(_alSource, AL_GAIN, gain);
    }
    _gainSet = gain;
}

void WaveOAL::Apply3D()
{
    if (!_alSource)
        return;
    if (_is3D)
    {
        alSourcei(_alSource, AL_SOURCE_RELATIVE, AL_FALSE);
        // Coordinate transform: invert X (left-handed → right-handed OpenAL)
        alSource3f(_alSource, AL_POSITION, -_position[0], _position[1], _position[2]);
        alSource3f(_alSource, AL_VELOCITY, -_velocity[0], _velocity[1], _velocity[2]);
        float accom = _accomEnabled ? _accommodation : 1.f;
        float refDist = DX8::referenceDistance(_volume, accom);
        alSourcef(_alSource, AL_REFERENCE_DISTANCE, refDist);
        alSourcef(_alSource, AL_MAX_DISTANCE, DX8::maxDistance(refDist));
        alSourcef(_alSource, AL_ROLLOFF_FACTOR, 1.f);
    }
    else
    {
        alSourcei(_alSource, AL_SOURCE_RELATIVE, AL_TRUE);
        alSource3f(_alSource, AL_POSITION, 0.f, 0.f, 0.f);
        alSource3f(_alSource, AL_VELOCITY, 0.f, 0.f, 0.f);
    }
}

// Deferred play: marks wave for playing on next Commit()
void WaveOAL::Play()
{
    if (_queued)
        return; // Waiting for predecessor in Queue() chain — see WaveOAL.hpp
    if (_state.terminated)
        return;
    if (_playing)
        return; // Already playing — no-op (DX8 behavior)
    _wantPlaying = true;
    _state.wantPlaying = true;
    _everPlayed = true;
}

// Chain a follow-up wave to play AFTER `predecessor` terminates.  Mirrors
// 1.99 WaveGlobal8::Queue (soundDX8.cpp:470): walk the predecessor's
// queue tail, append `this`, mark `this` as _queued so it can't play
// until the predecessor's IsTerminated() poll activates it.
//
// The `repeat` parameter is accepted for source-compat with the 1.99
// API; we don't currently re-trigger waves in the radio path (callers
// always pass repeat=1).
void WaveOAL::Queue(IWave* predecessorBase, int /*repeat*/)
{
    WaveOAL* predecessor = dynamic_cast<WaveOAL*>(predecessorBase);
    if (!predecessor || predecessor == this)
        return;
    // Walk to the end of any existing chain.  If `this` already appears
    // in predecessor's chain (duplicate-enqueue or a Stop+Restart-style
    // re-queue), don't add a second link — just re-arm `_queued` so the
    // gate is fresh.  This also defeats crossed-Queue cycles
    // (A.Queue(B) then B.Queue(A)) which would otherwise close a loop
    // and overflow IsTerminated()'s tail recursion.
    // Walk to the end of any existing chain.  If `this` already appears
    // in predecessor's chain (duplicate-enqueue or a Stop+Restart-style
    // re-queue), don't add a second link — just re-arm `_queued` so the
    // gate is fresh.  This also defeats crossed-Queue cycles
    // (A.Queue(B) then B.Queue(A)) which would otherwise close a loop
    // and overflow IsTerminated()'s tail recursion.
    for (WaveOAL* node = predecessor; node; node = node->_queueNext.GetRef())
    {
        if (node == this)
        {
            _queued = true;
            return;
        }
        if (!node->_queueNext)
        {
            node->_queueNext = this;
            _queued = true;
            return;
        }
    }
}

// Actual playback — called from SoundSystemOAL::Commit()
void WaveOAL::DoPlay()
{
    std::lock_guard<std::recursive_mutex> lk(_sys->_audioMutex);
    if (_state.terminated)
        return;
    if (_playing)
        return;

    // 3D sounds need position set first
    if (_is3D && !_state.posValid)
        return;

    // Delayed start (negative curPosition)
    if (_state.curPosition < 0)
        return;

    if (_loadError)
        return;

    // Phase 3d: async load state machine.  The synchronous `Load()`
    // is gone from the hot path; DoPlay now drives the state machine:
    //   NotStarted     → kick decode (sync for small / async for
    //                    music tracks), wave not playable this frame
    //   DecodePending  → worker is still decoding, wave not playable
    //   UploadPending  → worker finished, run sub-ms alBufferData
    //                    on this thread, then fall through to play
    //   Resident       → existing AL state, proceed to play
    //   Failed         → caller already saw _loadError check above
    //
    // First play of a non-Resident wave produces no audio in the
    // current frame, but the next Commit will retry — small files
    // resolve in the same frame's KickAsyncLoad call (sync decode +
    // Upload), large files (music) resolve within ~13 frames of
    // worker decode (≈220 ms for 25 MB PCM at the Phase A baseline
    // ~11 MB/s decode rate).  The user-visible difference vs the
    // pre-Phase-3d behaviour is a short audio delay at music start
    // instead of a 220 ms main-thread hitch.
    if (!_loaded)
    {
        if (_isStreamed)
        {
            // Phase 3g-3: streamed path.  Open AL resources +
            // dispatch the first chunk decode.  The wave isn't
            // playable this frame; Commit's UpdateStreamingPlayback
            // pass will pump the queue + alSourcePlay once at
            // least one buffer is queued.
            _streamPlayIntent = true; // recover the deferred start in UpdateStreamingPlayback
            if (_streaming.alBuffers[0] == 0)
            {
                OpenStreaming();
                if (_loadError)
                {
                    return;
                }
            }
            _loaded = true; // streamed waves "load" by opening AL state
            _loadState.store(static_cast<int>(LoadState::Resident), std::memory_order_release);
            UpdateStreamingPlayback(); // kick first chunk immediately
            // Source needs to be playing for AL to consume queued
            // buffers; start it now (will silently play silence
            // until UpdateStreamingPlayback uploads the first chunk
            // on the next Commit pass — typically same frame).
            if (_streaming.buffersInFlight > 0)
            {
                Apply3D();
                ApplyVolume();
                ApplyPitch();
                alSourcei(_alSource, AL_LOOPING, AL_FALSE); // worker drives loop, not AL
                alSourcePlay(_alSource);
                _playing = true;
                _everPlayed = true;
                _state.terminated = false;
            }
            return;
        }
        KickAsyncLoad();
        if (GetLoadState() == LoadState::UploadPending)
        {
            UploadAlBuffer();
        }
        if (!_loaded)
        {
            return;
        }
    }

    if (!_alSource)
        return;

    // Phase 3g-4: re-play path for streamed waves (already loaded
    // from a previous Play, then Stopped / Paused).  Prime the
    // queue before alSourcePlay so AL has something to consume —
    // otherwise the source would idle in AL_INITIAL / AL_STOPPED
    // until the next Commit poll runs UpdateStreamingPlayback.
    // Skip the curPosition-based termination check (stale for
    // streamed waves whose timeline is the worker's
    // decodeOffsetBytes, not _state.curPosition) and the
    // AL_LOOPING set (worker drives looping).
    if (_isStreamed)
    {
        _streamPlayIntent = true; // recover underrun / deferred start in UpdateStreamingPlayback
        UpdateStreamingPlayback(); // ensure ≥ 1 buffer queued
        Apply3D();
        ApplyVolume();
        ApplyPitch();
        AttachEFX();
        alSourcei(_alSource, AL_LOOPING, AL_FALSE);
        alSourcePlay(_alSource);
        _playing      = true;
        _everPlayed   = true;
        _state.terminated = false;
        return;
    }

    // Check termination/wrapping
    if (_state.curPosition >= static_cast<int>(_state.size) && _state.size > 0)
    {
        if (WaveLogic::ShouldTerminate(_state.curPosition, _state.size, _state.looping))
        {
            _state.terminated = true;
            _state.curPosition = _state.size;
            OnTerminateOnce();
            return;
        }
        _state.curPosition = WaveLogic::WrapPosition(_state.curPosition, _state.size, _state.looping);
    }

    Apply3D(); // DX8: DoSetPosition called in DoPlay (soundDX8.cpp:1186)
    ApplyVolume();
    ApplyPitch(); // 1.99 applies _frequencySet before play (soundDX8.cpp:1181) — start at correct pitch
    AttachEFX(); // Connect source to EAX reverb slot if enabled
    alSourcei(_alSource, AL_LOOPING, _state.looping ? AL_TRUE : AL_FALSE);
    alSourcePlay(_alSource);
    // Mirror DX8 Wave8::Play (soundDX8.cpp:1223): SetCurrentPosition(_curPosition)
    // BEFORE Play.  OpenAL doesn't accept the seek before the source is
    // playing, so we set it immediately after alSourcePlay — same effect,
    // single rebind of the timeline to the engine-side authoritative offset.
    // This is the resume mechanism that replaces the prior reliance on
    // alSourcePause/alSourcePlay preserving offset (which OpenAL Soft does
    // not guarantee — and in practice rewinds to 0 on the next play).
    if (_state.curPosition > 0 && _state.size > 0)
    {
        alGetError();
        alSourcei(_alSource, AL_BYTE_OFFSET, _state.curPosition);
        alGetError(); // diagnostic; if AL rejects the offset we still play from 0
    }
    if (_pendingSeekSeconds > 0.f)
    {
        float len = GetLength();
        float seek = (len > 0.f) ? std::fmod(_pendingSeekSeconds, len) : 0.f;
        if (seek > 0.f)
        {
            alGetError();
            alSourcef(_alSource, AL_SEC_OFFSET, seek);
            alGetError(); // diagnostic; ignore failure, source still plays
        }
        _pendingSeekSeconds = 0.f;
    }
    _paused = false;
    _playing = true;
    _state.playing = true;
    _state.terminated = false;

    LOG_DEBUG(Audio, "Play: {} (vol={} adj={} {})", static_cast<const char*>(Name()), _volume, _volumeAdjust,
              _state.looping ? "loop" : "once");
    OnPlayOnce();
}

void WaveOAL::DoStop()
{
    std::lock_guard<std::recursive_mutex> lk(_sys->_audioMutex);
    _wantPlaying = false;
    _state.wantPlaying = false;
    // Clear before the `!_playing` early-out: a Stop issued mid deferred
    // start (source not yet AL_PLAYING) must not be overridden by the
    // streaming pump's underrun/start recovery.
    _streamPlayIntent = false;

    // DoStop must always land StoppedReplayable.  Clear _paused
    // before the `!_playing` early-out — without it, a wave that
    // was Paused (and therefore !_playing) would keep _paused set
    // and State() would still report Paused.
    _paused = false;

    if (!_playing)
    {
        return;
    }

    if (_alSource)
    {
        if (_isStreamed)
        {
            // Phase 3g-4: Stop on a streamed source — drain the AL
            // queue, reset the decode state so a subsequent Play()
            // restarts from offset 0.  The in-flight worker (if any)
            // notices `aborted` and bails before pushing more chunks;
            // its AddRef holds the wave alive until it returns.
            alSourceStop(_alSource);
            ALint queued = 0;
            alGetSourcei(_alSource, AL_BUFFERS_QUEUED, &queued);
            while (queued-- > 0)
            {
                ALuint b = 0;
                alSourceUnqueueBuffers(_alSource, 1, &b);
                if (b != 0)
                {
                    _streaming.freeBuffers.push_back(b);
                }
            }
            _streaming.buffersInFlight = 0;
            _streaming.queue.Clear();
            _streaming.decodeOffsetBytes.store(0, std::memory_order_release);
            _streaming.consumedBytes.store(0, std::memory_order_release);
            _streaming.eofReached.store(false, std::memory_order_release);
        }
        else
        {
            // Mirror DX8 Wave8::DoStop (soundDX8.cpp:1538):
            // StoreCurrentPosition() BEFORE Stop.  Lets a subsequent
            // DoPlay seek back to the same byte offset, matching the
            // 1.99 contract where Stop/Play is the resume primitive
            // (not Pause).
            StoreCurrentOffset();
            alSourceStop(_alSource);
        }
    }

    _playing = false;
    _state.playing = false;
}

void WaveOAL::AttachEFX()
{
    // DX8: all HW 3D buffers automatically received EAX listener reverb.
    // In OpenAL EFX, we must explicitly connect each source to the aux slot.
    if (!_alSource || !_sys || !_sys->_eaxEnabled || !_sys->_efxSlot)
        return;

    alSource3i(_alSource, AL_AUXILIARY_SEND_FILTER, static_cast<ALint>(_sys->_efxSlot), 0, AL_FILTER_NULL);
}

void WaveOAL::DetachEFX()
{
    if (!_alSource)
        return;

    alSource3i(_alSource, AL_AUXILIARY_SEND_FILTER, AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL);
}

void WaveOAL::Stop()
{
    DoStop();
    Unload();
}

void WaveOAL::Resume()
{
    // audio-invariants A-01: Paused -> Playing via Resume.  Clears
    // _paused immediately and queues the deferred play so State()
    // reports WaitingDeferred until DoPlay fires (rather than staying
    // Paused through the queue window).
    if (_state.terminated || _loadError)
    {
        return;
    }
    if (_playing)
    {
        return;
    }
    _paused = false;
    _wantPlaying = true;
    _state.wantPlaying = true;
}

void WaveOAL::Pause()
{
    std::lock_guard<std::recursive_mutex> lk(_sys->_audioMutex);
    if (!_playing)
        return;

    if (_alSource)
    {
        if (_isStreamed)
        {
            // Phase 3g-4: alSourcePause on a queued source freezes
            // playback at the exact sample-in-buffer position; the
            // next alSourcePlay resumes from there, no engine-side
            // offset bookkeeping needed.  This is what AL natively
            // does for streamed sources and it works correctly in
            // OpenAL Soft (the rewind-on-Resume bug from Issue #11
            // was specific to single-buffer sources where AL's
            // resume semantics are looser).
            alSourcePause(_alSource);
        }
        else
        {
            // Capture current offset BEFORE pausing — same pattern as
            // DoStop.  OpenAL Soft does preserve playback position
            // across alSourcePause + alSourcePlay on most builds, but
            // we cannot rely on it (the user-visible bug on Issue #11
            // was the radio rewinding to 0 after pause→resume) so we
            // replicate the 1.99 DX8 contract: engine owns the
            // position.
            StoreCurrentOffset();
            alSourcePause(_alSource);
        }
    }

    _playing = false;
    _paused = true;
    _wantPlaying = false;
    _state.wantPlaying = false;
    _state.playing = false;
}

void WaveOAL::StoreCurrentOffset()
{
    if (!_alSource)
        return;

    // Match Wave8::StoreCurrentPosition (soundDX8.cpp:1457): read the
    // hardware/driver-side playback cursor and stash it as the engine's
    // authoritative resume point.  AL_BYTE_OFFSET reports the index into the
    // PCM data exactly the way DSBuffer::GetCurrentPosition did.
    alGetError();
    ALint byteOffset = 0;
    alGetSourcei(_alSource, AL_BYTE_OFFSET, &byteOffset);
    if (alGetError() == AL_NO_ERROR && byteOffset >= 0)
        _state.curPosition = byteOffset;
}

float WaveOAL::GetCurrentOffsetSeconds() const
{
    std::lock_guard<std::recursive_mutex> lk(_sys->_audioMutex);
    // Phase 3g streamed sources: AL_BYTE_OFFSET on a queued source
    // reports position WITHIN the currently-playing buffer only,
    // not the cumulative stream position.  Sum the consumedBytes
    // we accumulate at unqueue time (fully-played buffers) with
    // AL_BYTE_OFFSET (live position within the head buffer) and
    // divide by the cached format's byte rate.  Matches the
    // position-preservation contract that radio_unpause_resumes
    // exercises.
    if (_isStreamed && _alSource)
    {
        ALint byteOffsetInBuffer = 0;
        alGetError();
        alGetSourcei(_alSource, AL_BYTE_OFFSET, &byteOffsetInBuffer);
        if (alGetError() != AL_NO_ERROR || byteOffsetInBuffer < 0)
        {
            byteOffsetInBuffer = 0;
        }
        const int sampleStride = _streamFormat.nChannels * (_streamFormat.wBitsPerSample / 8);
        const int bytesPerSec  = static_cast<int>(_streamFormat.nSamplesPerSec) * sampleStride;
        if (bytesPerSec <= 0)
        {
            return 0.f;
        }
        const int64_t totalBytes = _streaming.consumedBytes.load(std::memory_order_acquire) +
                                   static_cast<int64_t>(byteOffsetInBuffer);
        return static_cast<float>(totalBytes) / static_cast<float>(bytesPerSec);
    }

    // Non-streamed path (Phase 3c/3d single-buffer): live AL cursor
    // is accurate because there's only one buffer to position within.
    if (_alSource)
    {
        float secOffset = 0.f;
        alGetError();
        alGetSourcef(_alSource, AL_SEC_OFFSET, &secOffset);
        if (alGetError() == AL_NO_ERROR)
            return secOffset;
    }
    if (_state.frequency <= 0 || _state.nChannel <= 0 || _state.sSize <= 0)
        return 0.f;
    int bytesPerSec = static_cast<int>(_state.frequency) * _state.nChannel * _state.sSize;
    return bytesPerSec > 0 ? static_cast<float>(_state.curPosition) / static_cast<float>(bytesPerSec) : 0.f;
}

void WaveOAL::Repeat(int repeat)
{
    if (repeat < 2)
    {
        _state.looping = false;
    }
    else
    {
        _state.looping = true;
        if (_playing && _alSource)
        {
            DoStop();
            DoPlay();
        }
    }
    // Phase 3g-5: streamed sources loop by worker re-queue (the
    // worker wraps decodeOffsetBytes at EOF when _state.looping).
    // AL_LOOPING on a queued source has different semantics — AL
    // would attempt to re-trigger the queue, which is wrong; we
    // explicitly keep it AL_FALSE on streamed sources at all times.
    if (_alSource && !_isStreamed)
        alSourcei(_alSource, AL_LOOPING, _state.looping ? AL_TRUE : AL_FALSE);
}

void WaveOAL::LastLoop()
{
    if (!_state.looping)
        return;
    _state.looping = false;
    if (_alSource && !_isStreamed)
        alSourcei(_alSource, AL_LOOPING, AL_FALSE);
}

void WaveOAL::PlayUntilStopValue(float time)
{
    _stopThreshold = time;
    _state.looping = true;
    if (_alSource && !_isStreamed)
        alSourcei(_alSource, AL_LOOPING, AL_TRUE);
}

void WaveOAL::SetStopValue(float time)
{
    if (time > _stopThreshold)
    {
        LOG_WARN(Audio, "SetStopValue terminate: {} (time={} > threshold={})", static_cast<const char*>(Name()), time,
                 _stopThreshold);
        _state.terminated = true;
        Stop();
    }
}

void WaveOAL::Skip(float deltaT)
{
    if (_queued)
        return; // Waiting for predecessor — don't advance virtual cursor either.
    if (_state.frequency <= 0 || _state.sSize <= 0)
        return;

    int skip = WaveLogic::TimeToPosition(deltaT, _state.frequency, _state.sSize);
    _state.curPosition += skip;

    // Delay/pause wave: size == 0 means there's no PCM to play, only a
    // virtual countdown.  Once curPosition reaches 0 the wave is done.
    // Mirrors 1.99 Wave8::Skip's `_size<=0` branch (soundDX8.cpp:1304).
    if (_state.size == 0 && _state.curPosition >= 0)
    {
        Stop();
        _state.terminated = true;
        _state.curPosition = 0;
        return;
    }

    while (_state.curPosition >= static_cast<int>(_state.size))
    {
        if (WaveLogic::ShouldTerminate(_state.curPosition, _state.size, _state.looping))
        {
            Stop();
            _state.terminated = true;
            _state.curPosition = _state.size;
            return;
        }
        _state.curPosition = _state.curPosition - _state.size;
    }
}

void WaveOAL::Advance(float deltaT)
{
    // 1.99 WaveGlobal8::Advance (soundDX8.cpp:467): a wave queued to start with
    // its cursor already at/after the start must not drift — DoPlay seeks the
    // source to _state.curPosition, so advancing it here starts the sound past
    // its attack (a "fade-in" on menu clicks). A negative cursor is a pre-start
    // delay (speed-of-sound / pause placeholder); Skip counts it up toward 0.
    if (_playing || (_wantPlaying && _state.curPosition >= 0))
        return;
    Skip(deltaT);
}

float WaveOAL::GetLength() const
{
    if (_state.frequency <= 0 || _state.nChannel <= 0 || _state.sSize <= 0)
        return 0.f;
    int bytesPerSec = static_cast<int>(_state.frequency) * _state.nChannel * _state.sSize;
    return bytesPerSec > 0 ? static_cast<float>(_state.size) / static_cast<float>(bytesPerSec) : 0.f;
}

bool WaveOAL::IsStreamingUnderrun() const
{
    std::lock_guard<std::recursive_mutex> lk(_sys->_audioMutex);
    // A streamed source that went AL_STOPPED is only finished when the
    // worker has flagged EOF AND the ring has fully drained.  Anything
    // else (more PCM to come, or buffers still queued) is a starvation
    // underrun or a not-yet-started deferred play — recoverable, not the
    // end of the track.
    if (!_isStreamed)
        return false;
    const bool reallyEnded = _streaming.eofReached.load(std::memory_order_acquire) && _streaming.buffersInFlight == 0;
    return !reallyEnded;
}

bool WaveOAL::IsStopped()
{
    // Deferred play: wave wants to play but hasn't started yet
    if (_wantPlaying)
        return false;

    if (_alSource)
    {
        ALint alState = AL_STOPPED;
        alGetSourcei(_alSource, AL_SOURCE_STATE, &alState);

        // Detect natural end of playback.  For a streamed source, a
        // transient underrun / deferred start also reads AL_STOPPED —
        // UpdateStreamingPlayback re-primes and restarts it, so it is
        // NOT stopped.  Only a genuine end-of-stream terminates here.
        if (alState == AL_STOPPED && _playing && !IsStreamingUnderrun())
        {
            _playing = false;
            _state.playing = false;
            _state.terminated = true;
            _streamPlayIntent = false;
            OnTerminateOnce();
        }
    }

    return !_playing;
}

bool WaveOAL::IsTerminated()
{
    // Deferred play: not terminated if about to play
    if (_wantPlaying)
        return false;

    // Detect natural completion → set _state.terminated.  A streamed
    // source that stopped from a starvation underrun or a not-yet-started
    // deferred play also reads AL_STOPPED; UpdateStreamingPlayback restarts
    // it, so it must NOT be reaped here.  Only a genuine end-of-stream
    // terminates.
    if (!_state.terminated && _alSource && _playing)
    {
        ALint alState = AL_STOPPED;
        alGetSourcei(_alSource, AL_SOURCE_STATE, &alState);
        if (alState == AL_STOPPED && !IsStreamingUnderrun())
        {
            _playing = false;
            _state.playing = false;
            _state.terminated = true;
            _streamPlayIntent = false;
            OnTerminateOnce();
        }
    }

    if (_loadError)
        _state.terminated = true;

    if (!_state.terminated)
        return false;

    // I am terminated.  Activate the next wave in the queue chain (1.99
    // WaveGlobal8::AdvanceQueue — soundDX8.cpp:420).  Idempotent: the
    // successor's _queued flag is the source of truth — once cleared,
    // we don't re-activate.  Restart()ing the head also re-arms this
    // path for the next cycle since Restart() may flip the successor
    // back into _queued state externally.
    if (_queueNext && _queueNext->_queued)
    {
        _queueNext->_queued = false;
        _queueNext->Play();
    }

    // Chain isn't fully terminated until every successor terminates too.
    // The head wave's owner (RadioChannel::_saying) holds the chain alive
    // and polls IsTerminated each frame; reporting `false` here keeps the
    // chain coherent until the tail finishes.
    if (_queueNext)
        return _queueNext->IsTerminated();

    return true;
}

bool WaveOAL::IsWaiting()
{
    return WaveLogic::IsWaiting(_state.curPosition);
}

void WaveOAL::Restart()
{
    Stop();
    _state.terminated = false;
    _state.curPosition = 0;
    _everPlayed = true;
    _pendingSeekSeconds = 0.f;
}

WaveState WaveOAL::State()
{
    // Authoritative FSM query per audio-invariants A-01.
    if (_state.terminated || _loadError)
    {
        return WaveState::Terminal;
    }
    if (_paused)
    {
        return WaveState::Paused;
    }
    if (_playing)
    {
        return WaveState::Playing;
    }
    if (_wantPlaying)
    {
        // Play() called; DoPlay deferred to next Commit (load /
        // position / delay pending).
        return WaveState::WaitingDeferred;
    }
    return _everPlayed ? WaveState::StoppedReplayable : WaveState::Created;
}

float WaveOAL::AppliedPitchForTest() const
{
    if (!_alSource)
        return 1.0f;
    ALfloat pitch = 1.0f;
    alGetSourcef(_alSource, AL_PITCH, &pitch);
    return pitch;
}

void WaveOAL::ApplyPitch()
{
    // _pitchRatio is a pitch ratio (1.0 = native rate).  Mimic 1.99
    // Wave8::SetFrequency exactly: map the ratio to the DirectSound
    // absolute-rate clamp [100, 100000] Hz against the sample's native
    // rate, not a naive [0.5, 2.0] ratio clamp — otherwise a low-RPM
    // engine sound is floored at 0.5x and vehicles jump straight to
    // "full rev" with no startup spin-up.
    if (_alSource && _state.frequency > 0 && _pitchRatio > 0.f)
    {
        alSourcef(_alSource, AL_PITCH, DX8::dsoundPitchRatio(_pitchRatio, _state.frequency));
    }
}

void WaveOAL::SetVolume(float v, float freq, bool /*immediate*/)
{
    _volume = v;

    // Remember the requested pitch so DoPlay re-applies it when the source
    // (re)starts — 1.99 stores _frequencySet and reapplies it on play
    // (soundDX8.cpp:1181).  Without this a deferred-play source plays its
    // first frame at AL's default pitch 1.0 and then snaps to the engine
    // pitch on the next SetVolume — the click on vehicle engine start.
    if (freq > 0.f)
        _pitchRatio = freq;
    ApplyPitch();

    ApplyVolume();
    if (_is3D)
        Apply3D();
}

void WaveOAL::SetVolumeAdjust(float volEffect, float volSpeech, float volMusic)
{
    float vol = volEffect;
    if (_kind == WaveMusic)
        vol = volMusic;
    else if (_kind == WaveSpeech)
        vol = volSpeech;
    _volumeAdjust = vol;
    ApplyVolume();
}

// Matching DX8 SetKind (soundDX8.cpp:1745-1753): update _volumeAdjust immediately
void WaveOAL::SetKind(WaveKind k)
{
    _kind = k;
    if (_sys)
    {
        float vol = _sys->_volumeAdjustEffect;
        if (_kind == WaveMusic)
            vol = _sys->_volumeAdjustMusic;
        else if (_kind == WaveSpeech)
            vol = _sys->_volumeAdjustSpeech;
        _volumeAdjust = vol;
    }
    ApplyVolume();
}

void WaveOAL::SetPosition(Vector3Par pos, Vector3Par vel, bool /*immediate*/)
{
    _position = pos;
    _velocity = vel;
    _state.posValid = true;
    Apply3D();
}

} // namespace Poseidon
