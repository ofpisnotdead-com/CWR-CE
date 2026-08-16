#ifdef _MSC_VER
#pragma once
#endif
#ifndef POSEIDON_AUDIO_IAUDIOSYSTEM_HPP
#define POSEIDON_AUDIO_IAUDIOSYSTEM_HPP

#include <Poseidon/Core/Types.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/RemoveLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <string>
#include <vector>
namespace Poseidon
{

// Helper function: Get empty name for pause waves
inline const RString& GetPauseName() {
    static const RString PauseName = "";
    return PauseName;
}

// Wave classification for volume control
enum WaveKind
{
	WaveEffect,   // Sound effects (gunshots, explosions, etc.)
	WaveSpeech,   // Voice/radio communications
	WaveMusic     // Background music
};

// Wave lifecycle state
// Backends implement IWave::State() against this enum.  Transition rules:
//   Created          --Play-->        WaitingDeferred | Playing
//   WaitingDeferred  --(loaded/budget)--> Playing
//   Playing          --Pause-->       Paused          (offset preserved)
//   Playing          --Stop-->        StoppedReplayable
//   Playing          --LastLoop / EOF non-loop --> Terminal
//   Paused           --Resume-->      Playing
//   Paused           --Stop-->        StoppedReplayable
//   StoppedReplayable --Play-->        Playing | WaitingDeferred
//   StoppedReplayable --Restart-->     StoppedReplayable (re-primed for Play)
//   Terminal         --Play/Stop/Pause--> Terminal (sticky against those)
//   Terminal         --Restart-->      StoppedReplayable (canonical revival)
enum class WaveState : uint8_t
{
	Created,            // pre-Play, never started
	WaitingDeferred,    // Play() called, awaiting load / budget / position
	Playing,            // active playback
	Paused,             // Pause()'d, offset preserved (Paused != Stop)
	StoppedReplayable,  // Stop()'d, next Play() succeeds
	Terminal            // end-of-life; sticky against Play/Stop/Pause, Restart revives
};

// Callback function type for wave events
typedef void WaveCallback( IWave *wave, RefCount *context );

// Abstract base class for audio wave/sample playback
class IWave: public RefCountWithLinks
{
	RString _name;
	bool _sticky;
	WaveCallback *_onTerminate;
	Ref<RefCount> _onTerminateContext;

	WaveCallback *_onPlay;
	Ref<RefCount> _onPlayContext;

	protected:
	float _stopThreshold; // sound should be stopped if Glob.time>_stopTime

	public:

	RString Name() const {return _name;}
	// Non-copying name accessor. Name() returns RString by value, whose
	// copy does a non-atomic CompactBuffer AddRef on the shared name buffer — a data
	// race when called off the main thread (the streaming pump's diag log/trace).
	// This returns the c_str of the immutable _name without touching its refcount.
	const char* NameCStr() const {return _name;}
	explicit IWave( RString name );
	void SetName( RString name ) {_name=name;}
	
	~IWave() override;
	
	virtual void Queue( IWave *wave, int repeat=1 )= 0; // queue playing
	virtual void Repeat( int repeat=1 )= 0; // set repeat count (1 or infinity)

	virtual void Play() = 0; // start playing
	virtual void Stop() = 0; // stop playing immediatelly
	virtual void Pause() { Stop(); } // pause playback (preserves position if supported)
	virtual void Resume() { Play(); } // resume from pause
	virtual void LastLoop() = 0; // stop playing at the end of the loop

	virtual void PlayUntilStopValue( float time ) = 0; // play until some time
	virtual void SetStopValue( float time ) = 0; // play until some time

	void OnTerminateOnce();
	void OnPlayOnce();

	void SetOnTerminate( WaveCallback *function, RefCount *context )
	{
		_onTerminate = function;
		_onTerminateContext = context;
	}
	void SetOnPlay( WaveCallback *function, RefCount *context )
	{
		_onPlay = function;
		_onPlayContext = context;
	}
	
	virtual bool IsWaiting() = 0;
	virtual void Skip( float deltaT ) = 0; // advance time
	virtual void Advance( float deltaT ) = 0; // skip if stopped
	virtual float GetLength() const = 0; // get wave length in sec

	virtual bool IsStopped() = 0; // query for Stop status
	virtual bool IsTerminated() = 0; // query for termination status
	virtual void Restart() = 0; // start playing again

	// Lifecycle state. Pure virtual —
	// every backend implements its FSM transitions explicitly.  Existing query
	// helpers (IsStopped / IsTerminated / IsWaiting) remain as convenience
	// wrappers over the FSM but State() is authoritative.
	virtual WaveState State() = 0;

	// Diagnostic: current playback offset in seconds.  Default 0 — overriden by
	// WaveOAL (reads AL_SEC_OFFSET) and used by the triRadioWaveOffset tri
	// verb that backs the pause/resume regression test.
	virtual float GetCurrentOffsetSeconds() const { return 0.f; }
	
	// Volume: linear [0..1], 0=silent, 1=full. Gain pipeline:
	// 2D: volume * accommodation * categoryAdjust
	// 3D: categoryAdjust only (distance handles volume via reference distance)
	virtual bool IsMuted() const = 0;
	virtual float GetVolume() const = 0;
	virtual void SetVolume( float volume, float freq=1.0, bool immediate=false )= 0;

	virtual float GetFileMaxVolume() const = 0;
	virtual float GetFileAvgVolume() const = 0;

	// Accommodation: ear-level dynamic range compression multiplier [0..4]
	// Applied by SoundScene based on aggregate loudness. See Shared/AudioMath.hpp.
	virtual void SetAccommodation( float accom=1.0 )= 0;
	virtual float GetAccommodation() const = 0;
	virtual void EnableAccommodation( bool enable ) = 0;
	virtual bool AccommodationEnabled() const = 0;

	virtual void SetKind( WaveKind kind ) = 0;
	virtual WaveKind GetKind() const = 0;
	
	void SetSticky( bool sticky ){_sticky=sticky;}
	bool GetSticky() const {return _sticky;}

	virtual void SetPosition
	(
		Vector3Par pos, Vector3Par vel, bool immediate=false
	)= 0;
	virtual Vector3 GetPosition() const = 0;
	// set if sound should be 3D processed
	// if not, volume is computed based on current source position
	virtual void Set3D( bool is3D ) = 0;
	virtual bool Get3D() const = 0;
	// equvalent distance of 2D sounds for position disabled sounds
	// this is necessary for loudness calculation
	virtual float Distance2D() const = 0;
};

// Per-frame audio diagnostic counters
// Filled by IAudioSystem::GetCounters().  Counters that aren't applicable to a
// particular backend stay 0; the API is uniform so tests / tri verbs / load
// regressions can poll deterministic state without screen-scraping logs.
//
// Use:
//   AudioCounters c = sys->GetCounters();
//   REQUIRE(c.active3D == expected_top_N);
//   REQUIRE(c.evictedThisFrame == 0);
//
// `evictedThisFrame` and `pausedThisFrame` are cleared at the start of each
// SoundScene::AdvanceAll tick (when AdvanceAll is the budget driver); other
// counters reflect the current scene snapshot.  `allocFailures` is monotonic
// across the session.
struct AudioCounters
{
	int active2D = 0;            // currently-playing 2D waves
	int active3D = 0;            // currently-playing 3D waves
	int paused = 0;              // waves currently in Paused state
	int suppressedMusic = 0;     // music waves muted by Audio-screen preview
	int evictedThisFrame = 0;    // 3D waves stopped by budget this AdvanceAll
	int pausedThisFrame = 0;     // waves transitioned to Paused this AdvanceAll
	int allocFailures = 0;       // session-cumulative AL alloc failures (A-23)
};

// Sound environment types for 3D audio effects (reverb, echo, etc.)
enum SoundEnvironmentType
{
	SEPlain,      // normal
	SECity,       // houses around
	SEForest,     // in forest
	SEMountains,  // mountains
	SERoom        // inside building
};

// Sound environment configuration
struct SoundEnvironment
{
	SoundEnvironmentType type;
	float size;    // how large (controls mostly echo time)
	float density; // how dense (echo intensity)
};

// Abstract interface for the audio subsystem
// Manages sound playback, 3D audio, and listener positioning
class IAudioSystem // whole playing system
{
	public:

	virtual float GetWaveDuration( const char *filename ) = 0;

	virtual IWave *CreateEmptyWave( float duration ) = 0; // duration in ms
	
	virtual IWave *CreateWave
	(
		const char *filename, bool is3D=true, bool prealloc=false
	)= 0;

	// Create wave from in-memory file data (WAV/OGG/WSS bytes)
	virtual IWave *CreateWaveFromMemory
	(
		const void *data, size_t size, const char *ext, bool is3D=false
	) { return nullptr; }

	virtual void SetListener
	(
		Vector3Par pos, Vector3Par vel,
		Vector3Par dir, Vector3Par up
	) = 0;
	virtual Vector3 GetListenerPosition() const = 0;
	virtual void Commit() = 0;
	virtual void Activate( bool active ) = 0;
	virtual void SetSimulationRunning( bool running ) {}
	virtual void SetEnvironment( const SoundEnvironment &env ) {}
	virtual bool IsReady() const { return true; }
	virtual ~IAudioSystem(){}

	virtual float GetCDVolume() const = 0;
	virtual void SetCDVolume( float vol ) = 0;

	virtual void SetWaveVolume( float vol ) = 0;
	virtual float GetWaveVolume() = 0;

	virtual void SetSpeechVolume( float vol ) = 0;
	virtual float GetSpeechVolume() = 0;

	virtual void StartPreview() = 0;
	virtual void TerminatePreview() = 0;

	// Per-category preview — Audio screen plays a single category's sample
	// while the corresponding slider is focused so the user hears the
	// volume change in isolation.  On volume change while same category is
	// already previewing, the wave stops and restarts so the new volume
	// takes effect immediately.
	// `kind` selects the wave: WaveMusic / WaveEffect / WaveSpeech.
	// Default no-op so non-OAL backends just don't preview.
	virtual void StartCategoryPreview(WaveKind /*kind*/) {}

	// Session-cumulative count of StartCategoryPreview calls that
	// successfully created a wave — audio-invariants A-18.  Each call
	// restarts the preview (kind change or same-kind volume tweak), so
	// the counter monotonically increases.  Test-only probe; 0 on
	// backends that don't implement category preview.
	virtual int CountCategoryPreviewRestarts() const { return 0; }

	// Mission-manifest preload (asset-loader plan Phase B): decode a wave
	// into the backend's PCM cache off the main thread so the first play
	// skips the file read + decode.  Streamed formats (OGG) and waves over
	// the cache's per-entry cap are ignored.  Default no-op.
	virtual void PreloadWave(const char* /*name*/) {}

	// PCM-cache probes for the preload regression tests.
	virtual int WaveCacheEntries() const { return 0; }
	virtual int WaveCacheHits() const { return 0; }

	// Continuous preview tied to the Output Device picker.  Plays the
	// configured "device" sample looped while focused; SwitchOutputDevice
	// preserves the current playback offset across the device swap so
	// stepping through devices feels continuous instead of restarting the
	// sample on each click.
	virtual void StartDevicePreview() {}

	// Preview tied to the 3D Audio (EAX) toggle.  Applies a dramatic
	// reverb preset (e.g. stone corridor) and plays a 3D effect sample
	// — toggling EAX restarts so the user hears the wet vs dry diff
	// immediately.  No-op on backends that don't expose EFX.
	virtual void StartEAXPreview() {}

	// Mute every currently-playing music-kind wave without stopping it,
	// so the Audio screen's previews aren't fighting menu background
	// music.  The track keeps streaming so its timeline advances; the
	// matching ResumeMusicForPreview restores the saved per-wave gains
	// when the screen unmounts.  No-op on backends that don't track
	// per-wave gain.  Idempotent: a second Suppress while already
	// suppressed is a no-op (re-Suppress would clobber the saved
	// originals with zeros).
	virtual void SuppressMusicForPreview() {}
	virtual void ResumeMusicForPreview() {}

	// Number of waves currently muted by SuppressMusicForPreview.
	// Test-only probe — used by triAssertMusicSuppressed to verify
	// AudioPage Mount/Unmount actually flipped the state.  0 on
	// backends that don't implement the suppression.
	virtual int  CountSuppressedMusicWaves() const { return 0; }

	// Per-frame counters snapshot — see AudioCounters above and
	// with 0 so backends adopt the API incrementally.
	virtual AudioCounters GetCounters() const { return AudioCounters{}; }

	// Report per-frame voice-budget decisions produced by
	// audio::ApplyVoiceBudget3D / ApplyVoice2D, called from
	// SoundScene::AdvanceAll.  Backends store the values and surface
	// them via GetCounters() so tests / crowded-scene diagnostics
	// (audio-invariants A-30) can read deterministic counts without
	// having to call AdvanceAll themselves.  Default no-op so non-OAL
	// backends adopt incrementally.
	virtual void SetVoiceBudgetCounters(int /*evictedThisFrame*/, int /*pausedThisFrame*/) {}

	virtual void FlushBank(QFBank *bank) {}

	// return state after enable
	virtual bool EnableHWAccel(bool val) = 0;
	virtual bool EnableEAX(bool val) = 0;
	// get if feature is enabled
	virtual bool GetEAX() const = 0;
	virtual bool GetHWAccel() const = 0;

	// Apply named EFX preset (from EFXPresets.hpp registry). Returns false if not found/unsupported.
	virtual bool ApplyEFXByName(const char* /*presetName*/, float /*size*/) { return false; }

	virtual void LoadConfig() = 0;
	virtual void SaveConfig() = 0;

	// Output device enumeration / selection.  Empty list and empty
	// current means the backend doesn't expose device choice (Dummy /
	// Capture).  Default implementations return "no choice available"
	// so backends that don't override them simply hide the picker.
	virtual std::vector<std::string> ListOutputDevices() const { return {}; }
	virtual std::string GetCurrentOutputDevice() const { return {}; }
	// Switch to the named device.  Empty / nullptr -> system default.
	// Returns true if the switch happened (or the device was already
	// active); false on failure.  Implementations are expected to
	// preserve volume state and recover currently-playing waves where
	// possible.
	virtual bool SwitchOutputDevice(const char* /*name*/) { return false; }

	// Mic device name persisted alongside the output device choice.
	// The backend doesn't open a capture handle itself — the AudioPage
	// owns the live VoNCapture — but it remembers the last-selected
	// device so audio.cfg can round-trip the choice across restarts.
	virtual const std::string& GetSelectedInputDevice() const
	{
		static const std::string empty;
		return empty;
	}
	virtual void SetSelectedInputDevice(const std::string& /*name*/) {}
};

extern IAudioSystem *GSoundsys;

// Speed of sound constants for 3D audio calculations
const float SpeedOfSound=330;
const float InvSpeedOfSound=1/SpeedOfSound;

} // namespace Poseidon

#endif // POSEIDON_AUDIO_IAUDIOSYSTEM_HPP
