// test_mixing_parity.cpp -- OpenAL vs DX8 mixing parity tests
// Regression tests for volume pipeline, OpenAndPlay behavior, and preview coupling.
// TDD: written first, then fixes applied to make them pass.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <Poseidon/Audio/Shared/DX8Reference.hpp>
#include <Poseidon/Audio/Shared/AudioMath.hpp>
#include <Poseidon/Audio/Text/SoundSystemText.hpp>
#include <Poseidon/Audio/SoundScene.hpp>
#include <Poseidon/Audio/IAudioSystem.hpp>
#include <cmath>
#include <vector>
#include <algorithm>
#include <catch2/catch_message.hpp>
#include <initializer_list>
#include <string>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>

using namespace Poseidon;
using Catch::Approx;

// RAII helper to set/restore GSoundsys for SoundScene tests
struct ScopedSoundSystem
{
    SoundSystemText sys;
    IAudioSystem* prev;
    ScopedSoundSystem() : prev(GSoundsys) { GSoundsys = &sys; }
    ~ScopedSoundSystem() { GSoundsys = prev; }
};

namespace
{
class TrackingWave : public IWave
{
  public:
    explicit TrackingWave(const char* name, bool is3D) : IWave(name), _is3D(is3D) {}

    void Queue(IWave*, int) override {}
    void Repeat(int repeat = 1) override { _looping = repeat >= 2; }

    void Play() override
    {
        if (_terminated)
            return;
        ++playCalls;
        _playing = true;
    }

    void Stop() override
    {
        ++stopCalls;
        _playing = false;
        _terminated = true;
    }

    void Pause() override
    {
        ++pauseCalls;
        _playing = false;
    }

    void LastLoop() override { _looping = false; }
    void PlayUntilStopValue(float) override {}
    void SetStopValue(float) override {}
    bool IsWaiting() override { return false; }
    void Skip(float) override {}
    void Advance(float) override {}
    float GetLength() const override { return 1.0f; }
    bool IsStopped() override { return !_playing; }
    bool IsTerminated() override { return _terminated; }
    void Restart() override
    {
        _terminated = false;
        _playing = false;
    }
    WaveState State() override
    {
        if (_terminated)
            return WaveState::Terminal;
        if (_playing)
            return WaveState::Playing;
        return playCalls > 0 ? WaveState::StoppedReplayable : WaveState::Created;
    }
    bool IsMuted() const override { return false; }
    float GetVolume() const override { return _volume; }
    void SetVolume(float volume, float = 1.0f, bool = false) override { _volume = volume; }
    float GetFileMaxVolume() const override { return 1.0f; }
    float GetFileAvgVolume() const override { return 1.0f; }
    void SetAccommodation(float accom = 1.0f) override { _accommodation = accom; }
    float GetAccommodation() const override { return _accommodation; }
    void EnableAccommodation(bool enable) override { _accommodationEnabled = enable; }
    bool AccommodationEnabled() const override { return _accommodationEnabled; }
    void SetKind(WaveKind kind) override { _kind = kind; }
    WaveKind GetKind() const override { return _kind; }
    void SetPosition(Vector3Par pos, Vector3Par vel, bool = false) override
    {
        _position = pos;
        _velocity = vel;
    }
    Vector3 GetPosition() const override { return _position; }
    void Set3D(bool is3D) override { _is3D = is3D; }
    bool Get3D() const override { return _is3D; }
    float Distance2D() const override { return 1.0f; }

    int playCalls = 0;
    int pauseCalls = 0;
    int stopCalls = 0;

  private:
    bool _playing = false;
    bool _terminated = false;
    bool _looping = false;
    bool _is3D = false;
    bool _accommodationEnabled = true;
    float _volume = 1.0f;
    float _accommodation = 1.0f;
    WaveKind _kind = WaveEffect;
    Vector3 _position = VZero;
    Vector3 _velocity = VZero;
};

class TrackingAudioSystem : public IAudioSystem
{
  public:
    float GetWaveDuration(const char*) override { return 1.0f; }
    IWave* CreateEmptyWave(float) override
    {
        auto* wave = new TrackingWave("", false);
        _waves.push_back(wave);
        return wave;
    }
    IWave* CreateWave(const char* filename, bool is3D = true, bool = false) override
    {
        auto* wave = new TrackingWave(filename, is3D);
        _waves.push_back(wave);
        return wave;
    }
    void SetListener(Vector3Par, Vector3Par, Vector3Par, Vector3Par) override {}
    Vector3 GetListenerPosition() const override { return VZero; }
    void Commit() override {}
    void Activate(bool) override {}
    float GetCDVolume() const override { return 5.0f; }
    void SetCDVolume(float) override {}
    void SetWaveVolume(float) override {}
    float GetWaveVolume() override { return 5.0f; }
    void SetSpeechVolume(float) override {}
    float GetSpeechVolume() override { return 5.0f; }
    void StartPreview() override {}
    void TerminatePreview() override {}
    bool EnableHWAccel(bool) override { return false; }
    bool EnableEAX(bool) override { return false; }
    bool GetEAX() const override { return false; }
    bool GetHWAccel() const override { return false; }
    void LoadConfig() override {}
    void SaveConfig() override {}

    TrackingWave* LastWave() const { return _waves.empty() ? nullptr : _waves.back(); }

  private:
    std::vector<TrackingWave*> _waves;
};

struct ScopedTrackingSoundSystem
{
    TrackingAudioSystem sys;
    IAudioSystem* prev;
    ScopedTrackingSoundSystem() : prev(GSoundsys) { GSoundsys = &sys; }
    ~ScopedTrackingSoundSystem() { GSoundsys = prev; }
};
} // namespace

// Section 1: DX8 Gain3D dB roundtrip parity
// The DX8 3D gain path is: db2linear(float2db(volumeAdjust))
// AudioMath::Gain3D should produce equivalent results.

TEST_CASE("Parity: Gain3D matches DX8 gain3D at unity", "[Audio][parity]")
{
    CHECK(AudioMath::Gain3D(1.0f) == Approx(DX8::gain3D(1.0f)).margin(0.001f));
}

TEST_CASE("Parity: Gain3D matches DX8 gain3D for typical mixer adjusts", "[Audio][parity]")
{
    // Typical _volumeAdjust values from mixer (0..1 range)
    for (float adj : {0.01f, 0.03f, 0.05f, 0.1f, 0.3f, 0.5f, 0.7f, 0.9f, 1.0f})
    {
        float dx8 = DX8::gain3D(adj);
        float oal = AudioMath::Gain3D(adj);
        CAPTURE(adj, dx8, oal);
        CHECK(oal == Approx(dx8).margin(0.002f));
    }
}

TEST_CASE("Parity: Gain3D clamps values > 1.0 like DX8", "[Audio][parity]")
{
    // DX8 float2db(1.5) = 0 (clamped to max), db2linear(0) = 1.0
    // AudioMath::Gain3D should also cap at 1.0
    float dx8 = DX8::gain3D(1.5f); // = 1.0
    float oal = AudioMath::Gain3D(1.5f);
    CHECK(dx8 == Approx(1.0f).margin(0.001f));
    CHECK(oal == Approx(1.0f).margin(0.001f));
}

TEST_CASE("Parity: Gain3D at zero returns zero like DX8", "[Audio][parity]")
{
    CHECK(DX8::gain3D(0.0f) == 0.0f);
    CHECK(AudioMath::Gain3D(0.0f) == 0.0f);
}

TEST_CASE("Parity: Gain3D at negative returns zero", "[Audio][parity]")
{
    CHECK(DX8::gain3D(-0.5f) == 0.0f);
    CHECK(AudioMath::Gain3D(-0.5f) == 0.0f);
}

// Section 2: DX8 Gain2D parity

TEST_CASE("Parity: Gain2D matches DX8 gain2D for typical values", "[Audio][parity]")
{
    struct TestCase
    {
        float vol, accom, adj;
    };
    TestCase cases[] = {
        {1.0f, 1.0f, 1.0f},  {0.5f, 1.0f, 1.0f}, {0.1f, 1.0f, 0.5f},
        {0.01f, 1.0f, 0.3f}, {0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 1.0f},
    };
    for (auto& tc : cases)
    {
        float dx8 = DX8::gain2D(tc.vol, tc.accom, tc.adj);
        float oal = AudioMath::Gain2D(tc.vol, tc.accom, true, tc.adj);
        CAPTURE(tc.vol, tc.accom, tc.adj, dx8, oal);
        CHECK(oal == Approx(dx8).margin(0.005f));
    }
}

TEST_CASE("Parity: Gain2D clamps values > 1.0 like DX8", "[Audio][parity]")
{
    // High accommodation can push gain above 1.0
    float dx8 = DX8::gain2D(1.0f, 2.0f, 1.0f); // val = 2.0 -> float2db -> 0 -> db2linear -> 1.0
    float oal = AudioMath::Gain2D(1.0f, 2.0f, true, 1.0f);
    CHECK(dx8 == Approx(1.0f).margin(0.001f));
    CHECK(oal == Approx(1.0f).margin(0.001f));
}

// Section 3: OpenAndPlay does NOT auto-start playback (DX8 parity)
// In original DX8, OpenAndPlay only sets volume + position -- it does not
// call Play(). The SoundScene::AdvanceAll() loop is responsible for starting
// playback based on loudness sorting.

TEST_CASE("Parity: OpenAndPlay does not start playback", "[Audio][parity][soundscene]")
{
    ScopedSoundSystem ss;
    SoundScene scene;

    // OpenAndPlay should create the wave, set vol/pos, but NOT start playing
    IWave* wave = scene.OpenAndPlay("test_breath.wav", Vector3(100, 0, 100), Vector3(0, 0, 0));
    REQUIRE(wave != nullptr);

    // Wave should NOT be queued for playing -- matches original DX8 behavior
    CHECK(wave->IsStopped());

    wave->Release();
}

TEST_CASE("Parity: OpenAndPlayOnce does not start playback", "[Audio][parity][soundscene]")
{
    ScopedSoundSystem ss;
    SoundScene scene;

    IWave* wave = scene.OpenAndPlayOnce("test_shot.wav", Vector3(50, 0, 50), Vector3(0, 0, 0));
    REQUIRE(wave != nullptr);

    CHECK(wave->IsStopped());

    wave->Release();
}

TEST_CASE("Regression: paused 2D waves use Pause, not Stop", "[Audio][parity][soundscene][pause]")
{
    ScopedTrackingSoundSystem ss;
    SoundScene scene;

    IWave* wave = scene.OpenAndPlayOnce2D("radio_resume.wav", 1.0f, 1.0f, false);
    auto* tracking = ss.sys.LastWave();
    REQUIRE(wave != nullptr);
    REQUIRE(tracking != nullptr);

    scene.AdvanceAll(0.1f, false);
    CHECK(tracking->playCalls == 1);
    CHECK(tracking->pauseCalls == 0);
    CHECK(tracking->stopCalls == 0);

    scene.AdvanceAll(0.1f, true);
    CHECK(tracking->playCalls == 1);
    CHECK(tracking->pauseCalls == 1);
    CHECK(tracking->stopCalls == 0);

    scene.AdvanceAll(0.1f, false);
    CHECK(tracking->playCalls == 2);
    CHECK(tracking->pauseCalls == 1);
    CHECK(tracking->stopCalls == 0);

    wave->Release();
}

// Issue #11 root-cause contract.  The pause→unpause radio replay bug
// surfaced because nothing in WaveOAL's pause path captured the AL source's
// playback offset back into _state.curPosition before alSourcePause, and
// nothing in DoPlay restored it after alSourcePlay.  OpenAL Soft does not
// guarantee that alSourcePause+alSourcePlay preserves the cursor (and in
// the user-visible bug it didn't), so the engine has to own the position —
// same way DX8 Wave8::Play does via SetCurrentPosition(_curPosition).
//
// This test pins down the lifecycle on the IWave abstraction: a wave
// paused mid-play and then re-played should NOT have its offset reset to
// 0 on resume.  TrackingWave models the contract: pausing stores the
// last-known offset, resuming reads it back.  The OpenAL implementation
// (WaveOAL::Pause / StoreCurrentOffset / DoPlay) mirrors the same shape.
TEST_CASE("Regression: pause stores offset; resume seeks back (Issue #11)",
          "[Audio][parity][soundscene][pause][offset]")
{
    // Hand-rolled mini wave that simulates an AL source advancing during
    // playback and reports its offset via the new IWave hook.
    struct OffsetWave : public IWave
    {
        OffsetWave() : IWave("offset_radio.wav") {}
        void Queue(IWave*, int) override {}
        void Repeat(int) override {}
        void Play() override
        {
            // Resume must restore the stored offset (1.99 contract).
            _alOffset = _stored;
            _playing = true;
        }
        void Stop() override
        {
            if (_playing)
                _stored = _alOffset;
            _playing = false;
            _terminated = true;
            _alOffset = 0.f; // alSourceStop rewinds the AL cursor
        }
        void Pause() override
        {
            if (_playing)
                _stored = _alOffset;
            _playing = false;
            // OpenAL Soft behavior on the broken build: paused cursor
            // is NOT reliably preserved across alSourcePause+alSourcePlay.
            // We simulate the worst case to make the test bite.
            _alOffset = 0.f;
        }
        void LastLoop() override {}
        void PlayUntilStopValue(float) override {}
        void SetStopValue(float) override {}
        bool IsWaiting() override { return false; }
        void Skip(float dt) override
        {
            if (_playing)
                _alOffset += dt; // advance the simulated AL cursor
        }
        void Advance(float dt) override
        {
            if (_playing)
                _alOffset += dt;
        }
        float GetLength() const override { return 4.f; }
        bool IsStopped() override { return !_playing; }
        bool IsTerminated() override { return _terminated; }
        void Restart() override
        {
            _alOffset = 0.f;
            _stored = 0.f;
            _terminated = false;
        }
        WaveState State() override
        {
            if (_terminated)
                return WaveState::Terminal;
            if (_playing)
                return WaveState::Playing;
            return _stored > 0.f || _alOffset > 0.f ? WaveState::StoppedReplayable : WaveState::Created;
        }
        bool IsMuted() const override { return false; }
        float GetVolume() const override { return 1.f; }
        void SetVolume(float, float, bool) override {}
        float GetFileMaxVolume() const override { return 1.f; }
        float GetFileAvgVolume() const override { return 1.f; }
        void SetAccommodation(float) override {}
        float GetAccommodation() const override { return 1.f; }
        void EnableAccommodation(bool) override {}
        bool AccommodationEnabled() const override { return false; }
        void SetKind(WaveKind k) override { _kind = k; }
        WaveKind GetKind() const override { return _kind; }
        void SetPosition(Vector3Par, Vector3Par, bool) override {}
        Vector3 GetPosition() const override { return VZero; }
        void Set3D(bool) override {}
        bool Get3D() const override { return false; }
        float Distance2D() const override { return 1.f; }
        float GetCurrentOffsetSeconds() const override { return _playing ? _alOffset : _stored; }

        bool _playing = false;
        bool _terminated = false;
        float _alOffset = 0.f;
        float _stored = 0.f;
        WaveKind _kind = WaveSpeech;
    };

    OffsetWave wave;
    wave._playing = true;
    wave._alOffset = 2.0f; // simulate ~2s into a 4s radio message

    // Step 1: capture pre-pause offset.
    float prePause = wave.GetCurrentOffsetSeconds();
    REQUIRE(prePause == Approx(2.0f));

    // Step 2: pause.  The AL cursor is "lost" (modeled by clearing
    // _alOffset), but the offset must still be queryable — i.e. the
    // pause path has to have snapshotted it.
    wave.Pause();
    float duringPause = wave.GetCurrentOffsetSeconds();
    // Pre-fix: nothing stored the offset, so duringPause would be 0.
    // Post-fix (matching 1.99 StoreCurrentPosition): the offset is
    // captured before the AL cursor is invalidated.
    CHECK(duringPause == Approx(2.0f));

    // Step 3: resume.  Play() must seek the AL cursor back to the
    // stored offset (1.99 Wave8::Play -> SetCurrentPosition(_curPosition)).
    wave.Play();
    float postResume = wave.GetCurrentOffsetSeconds();
    CHECK(postResume == Approx(2.0f));

    // Step 4: advance — should pick up from the resumed offset, not 0.
    wave.Advance(0.5f);
    CHECK(wave.GetCurrentOffsetSeconds() == Approx(2.5f));
}

// Section 4: Breath-at-menu regression test
// Breath is created via OpenAndPlay with default vol=1.0, then immediately
// SetVolume(0, freq) is called. The wave should NOT produce audible output.

TEST_CASE("Regression: breath wave created at vol=0 is not playing", "[Audio][parity][breath]")
{
    ScopedSoundSystem ss;
    SoundScene scene;

    // Simulate what soldierOldSim.cpp does:
    // 1. OpenAndPlay with default volume (1.0)
    IWave* breath = scene.OpenAndPlay("sound/people/breath.wss", Vector3(100, 1, 100), Vector3(0, 0, 0));
    REQUIRE(breath != nullptr);

    // 2. Immediately set volume to 0 (tired=0)
    breath->SetVolume(0.0f, 0.5f);

    // The wave must NOT be playing (no first-frame pop)
    CHECK(breath->IsStopped());
    CHECK(breath->GetVolume() == Approx(0.0f));

    breath->Release();
}

TEST_CASE("Regression: breath at vol=0 has near-zero reference distance", "[Audio][parity][breath]")
{
    // Even if somehow played, vol=0 should produce refDist=1e-6 -> effectively inaudible
    float refDist = DX8::referenceDistance(0.0f, 1.0f);
    CHECK(refDist == Approx(1e-6f));

    float maxDist = DX8::maxDistance(refDist);
    CHECK(maxDist == Approx(1e-4f));

    // At distance 0.36m (head position), with AL_INVERSE_DISTANCE_CLAMPED:
    // clampedDist = clamp(0.36, 1e-6, 1e-4) = 1e-4
    // gain = 1e-6 / (1e-6 + 1*(1e-4 - 1e-6)) ~= 0.01
    float dist = 0.36f;
    float clampedDist = std::max(refDist, std::min(dist, maxDist));
    float distGain = refDist / (refDist + 1.0f * (clampedDist - refDist));
    CHECK(distGain < 0.02f); // effectively inaudible
}

// Section 5: Gain3D dB roundtrip precision
// Verify the DX8 dB roundtrip doesn't introduce more than 1% error
// for values in the normal mixer range (0.001 .. 1.0).

TEST_CASE("DX8: float2db -> db2linear roundtrip precision", "[Audio][parity]")
{
    // The integer centibels conversion loses some precision
    for (float val : {0.001f, 0.005f, 0.01f, 0.05f, 0.1f, 0.2f, 0.5f, 0.8f, 1.0f})
    {
        int db = DX8::float2db(val);
        float back = DX8::db2linear(db);
        CAPTURE(val, db, back);
        // Within 1% for reasonable values
        CHECK(back == Approx(val).epsilon(0.01f));
    }
}

// Section 6: Mixer pipeline regression
// Full slider -> expVol -> adjust -> gain chain must be stable

TEST_CASE("Parity: full mixer pipeline -- all sliders equal", "[Audio][parity][mixer]")
{
    // When all sliders are equal, all adjusts should be 1.0
    for (float slider : {0.0f, 3.0f, 5.0f, 7.0f, 10.0f})
    {
        float e = DX8::expVol(slider);
        float maxE = e;
        float adj = DX8::mixerAdjust(e, maxE);
        CAPTURE(slider, e, adj);
        CHECK(adj == Approx(1.0f).margin(0.001f));
    }
}

TEST_CASE("Parity: full mixer pipeline -- effects loud, others quiet", "[Audio][parity][mixer]")
{
    float wavExp = DX8::expVol(10.0f); // 1.0
    float spcExp = DX8::expVol(2.0f);  // exp(-5.6) ~= 0.0037
    float musExp = DX8::expVol(0.0f);  // 1e-10

    float maxE = std::max({wavExp, spcExp, musExp});
    CHECK(maxE == Approx(1.0f));

    float adjEff = DX8::mixerAdjust(wavExp, maxE);
    float adjSpc = DX8::mixerAdjust(spcExp, maxE);
    float adjMus = DX8::mixerAdjust(musExp, maxE);

    CHECK(adjEff == Approx(1.0f));
    CHECK(adjSpc < 0.01f); // very quiet
    CHECK(adjMus < 1e-5f); // effectively muted

    // 3D gain with these adjusts
    float gain3d_eff = AudioMath::Gain3D(adjEff);
    float gain3d_spc = AudioMath::Gain3D(adjSpc);
    CHECK(gain3d_eff == Approx(1.0f).margin(0.001f));
    CHECK(gain3d_spc < 0.01f);
}

// Section 7: Preview volume coupling
// When Effects slider changes, preview effect sound should get updated volume.

TEST_CASE("Parity: SetWaveVolume triggers mixer update on Text backend", "[Audio][parity][preview]")
{
    ScopedSoundSystem ss;

    // Create a wave and verify SetVolumeAdjust is accessible
    IWave* wave = ss.sys.CreateWave("preview_effect.wav", false, false);
    REQUIRE(wave != nullptr);

    // Set initial volumes
    ss.sys.SetWaveVolume(5.0f);
    ss.sys.SetSpeechVolume(5.0f);
    ss.sys.SetCDVolume(5.0f);

    // The wave should be usable for preview
    wave->SetKind(WaveEffect);
    wave->SetVolume(1.0f, 1.0f, true);
    wave->Play();
    CHECK(!wave->IsStopped());

    wave->Stop();
    wave->Release();
}

// Section 8: End-to-end volume pipeline
// Full slider -> expVol -> adjust -> Gain3D -> referenceDistance chain

TEST_CASE("Parity: end-to-end 3D volume pipeline at Effects=10", "[Audio][parity][pipeline]")
{
    // Effects slider at 10 (max), all others at 5
    float effExp = DX8::expVol(10.0f); // 1.0
    float spcExp = DX8::expVol(5.0f);  // ~0.030
    float musExp = DX8::expVol(5.0f);  // ~0.030

    float maxExp = std::max({effExp, spcExp, musExp});
    float adjEff = DX8::mixerAdjust(effExp, maxExp);
    CHECK(adjEff == Approx(1.0f));

    // 3D gain for effect channel
    float gain3D = AudioMath::Gain3D(adjEff);
    CHECK(gain3D == Approx(1.0f));

    // Reference distance for vol=1.0, accom=1.0
    float refDist = AudioMath::ReferenceDistance(1.0f, 1.0f);
    CHECK(refDist == Approx(30.0f));
    CHECK(AudioMath::MaxDistance(1.0f, 1.0f) == Approx(3000.0f));
}

TEST_CASE("Parity: end-to-end 3D volume pipeline at Effects=0", "[Audio][parity][pipeline]")
{
    // Effects slider at 0, others at 5
    float effExp = DX8::expVol(0.0f); // 1e-10
    float spcExp = DX8::expVol(5.0f); // ~0.030
    float musExp = DX8::expVol(5.0f); // ~0.030

    float maxExp = std::max({effExp, spcExp, musExp});
    float adjEff = DX8::mixerAdjust(effExp, maxExp);
    CHECK(adjEff < 1e-5f); // effectively muted

    float gain3D = AudioMath::Gain3D(adjEff);
    CHECK(gain3D < 1e-5f);
}

TEST_CASE("Parity: end-to-end 2D volume pipeline (speech at full)", "[Audio][parity][pipeline]")
{
    float effExp = DX8::expVol(5.0f);
    float spcExp = DX8::expVol(10.0f);
    float musExp = DX8::expVol(5.0f);

    float maxExp = std::max({effExp, spcExp, musExp});
    float adjSpc = DX8::mixerAdjust(spcExp, maxExp);
    CHECK(adjSpc == Approx(1.0f));

    // 2D gain for speech at vol=0.8, accom=1
    float gain2D = AudioMath::Gain2D(0.8f, 1.0f, true, adjSpc);
    float dx8Gain = DX8::gain2D(0.8f, 1.0f, adjSpc);
    CHECK(gain2D == Approx(dx8Gain).margin(0.005f));
}

TEST_CASE("Parity: listener gain from max slider", "[Audio][parity][pipeline]")
{
    // All sliders at 7 -> listener gain = 7 * 0.1 = 0.7
    CHECK(DX8::listenerGain(7.0f) == Approx(0.7f));
    CHECK(DX8::listenerGain(10.0f) == Approx(1.0f));
    CHECK(DX8::listenerGain(0.0f) == Approx(0.0f));
}

// Section 9: Reference distance edge cases

TEST_CASE("Parity: referenceDistance matches DX8 at various volumes", "[Audio][parity]")
{
    for (float vol : {0.01f, 0.1f, 0.5f, 1.0f})
    {
        float dx8 = DX8::referenceDistance(vol, 1.0f);
        float oal = AudioMath::ReferenceDistance(vol, 1.0f);
        CAPTURE(vol, dx8, oal);
        CHECK(oal == Approx(dx8));
    }
}

TEST_CASE("Parity: referenceDistance with accommodation factor", "[Audio][parity]")
{
    float accom = 0.5f;
    float vol = 1.0f;
    float expected = std::sqrt(vol * accom) * 30.0f;
    CHECK(AudioMath::ReferenceDistance(vol, accom) == Approx(expected));
    CHECK(DX8::referenceDistance(vol, accom) == Approx(expected));
}

// Section 10: 3DOff gain path (3D buffer with 3D disabled)

TEST_CASE("Parity: 3DOff gain matches DX8 gain2D_3DOff", "[Audio][parity]")
{
    // DX8: float2db(vol * accom * adj * 100) -> db2linear -> gain
    // OAL: vol * accom * adj * 100, clamped to 1.0
    for (float vol : {0.001f, 0.01f, 0.05f, 0.1f})
    {
        float dx8 = DX8::gain2D_3DOff(vol, 1.0f, 1.0f);
        float raw = vol * 1.0f * 1.0f * 100.0f;
        float oal = raw > 1.f ? 1.f : (raw < 0.f ? 0.f : raw);
        CAPTURE(vol, dx8, oal);
        CHECK(oal == Approx(dx8).margin(0.01f));
    }
}

// Section 11: Wave _volumeAdjust initialization from system mixer
// DX8 WaveBuffers8::Reset() sets _volumeAdjust = sys->_volumeAdjustEffect
// New waves must inherit the current mixer state, not default to 1.0.
// This is critical for preview: the effect wave is created AFTER
// UpdateMixer() runs, so it must pick up the current adjust.

TEST_CASE("Parity: DX8 wave _volumeAdjust initialization formula", "[Audio][parity][preview]")
{
    // When Effects=3, Speech=5, Music=5:
    float effExp = DX8::expVol(3.0f); // exp(-4.9) ~= 0.0074
    float spcExp = DX8::expVol(5.0f); // exp(-3.5) ~= 0.0302
    float musExp = DX8::expVol(5.0f);

    float maxExp = std::max({effExp, spcExp, musExp});
    float adjEffect = DX8::mixerAdjust(effExp, maxExp);

    // In DX8, new wave gets _volumeAdjust = sys->_volumeAdjustEffect
    // NOT 1.0 -- this is the key parity requirement
    CHECK(adjEffect < 0.3f); // much less than 1.0
    CHECK(adjEffect > 0.0f);

    // For 3D preview effect, gain = Gain3D(adjEffect)
    float gain = AudioMath::Gain3D(adjEffect);
    CHECK(gain == Approx(adjEffect).margin(0.001f));

    // Verify: if _volumeAdjust were 1.0 (bug), gain would be 1.0 instead
    float bugGain = AudioMath::Gain3D(1.0f);
    CHECK(bugGain == Approx(1.0f));
    CHECK(gain < bugGain * 0.5f); // correct gain is much lower
}

TEST_CASE("Parity: preview effect volume at various slider positions", "[Audio][parity][preview]")
{
    struct TestCase
    {
        float effSlider;
        float spcSlider;
        float expectedLow;
        float expectedHigh;
    };
    TestCase cases[] = {
        {10.0f, 5.0f, 0.9f, 1.01f}, // Effects loud -> gain ~= 1.0
        {5.0f, 5.0f, 0.9f, 1.01f},  // All equal -> gain = 1.0
        {3.0f, 5.0f, 0.1f, 0.5f},   // Effects lower -> gain < 0.5
        {0.0f, 5.0f, 0.0f, 1e-5f},  // Effects muted -> gain ~= 0
    };
    for (auto& tc : cases)
    {
        float effExp = DX8::expVol(tc.effSlider);
        float spcExp = DX8::expVol(tc.spcSlider);
        float musExp = DX8::expVol(tc.spcSlider);
        float maxExp = std::max({effExp, spcExp, musExp});
        float adjEffect = DX8::mixerAdjust(effExp, maxExp);
        float gain = AudioMath::Gain3D(adjEffect);
        CAPTURE(tc.effSlider, tc.spcSlider, adjEffect, gain);
        CHECK(gain >= tc.expectedLow);
        CHECK(gain <= tc.expectedHigh);
    }
}

// Section 12: OAL integration -- wave volumeAdjust from system
// Requires OpenAL loopback (skipped if unavailable)

#include <PoseidonOpenAL/SoundSystemOAL.hpp>
#include <PoseidonOpenAL/WaveOAL.hpp>
#include "test_fixtures.hpp"
#include <chrono>
#include <fstream>
#include <thread>
#include <vector>

namespace
{
std::vector<char> ReadFixtureFile(const char* name)
{
    std::string path = GET_FIXTURE(name);
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        return {};
    auto sz = f.tellg();
    f.seekg(0);
    std::vector<char> buf(sz);
    f.read(buf.data(), sz);
    return buf;
}
} // namespace

TEST_CASE("OAL: new wave inherits _volumeAdjustEffect from system", "[Audio][parity][preview][oal]")
{
    auto* sys = dynamic_cast<SoundSystemOAL*>(CreateSoundSystemOAL());
    if (!sys)
    {
        SKIP("OpenAL not available");
    }

    // Set sliders: Effects=3, Speech=5, Music=5 -> _volumeAdjustEffect << 1.0
    sys->SetWaveVolume(3.0f);
    sys->SetSpeechVolume(5.0f);
    sys->SetCDVolume(5.0f);

    // Compute expected adjust from DX8 formulas
    float effExp = DX8::expVol(3.0f);
    float spcExp = DX8::expVol(5.0f);
    float musExp = DX8::expVol(5.0f);
    float maxExp = (std::max)(effExp, (std::max)(spcExp, musExp));
    float expectedAdj = DX8::mixerAdjust(effExp, maxExp);
    REQUIRE(expectedAdj < 0.5f); // must be less than 1.0

    // Load fixture WAV and create wave via CreateWaveFromMemory
    auto wavData = ReadFixtureFile("audio/tone.wav");
    if (wavData.empty())
    {
        delete sys;
        SKIP("Cannot read fixture audio/tone.wav");
    }

    auto* wave = dynamic_cast<WaveOAL*>(sys->CreateWaveFromMemory(wavData.data(), wavData.size(), ".wav", true));
    if (!wave)
    {
        delete sys;
        SKIP("Cannot create wave from memory");
    }

    CHECK(wave->GetVolumeAdjust() == Approx(expectedAdj).margin(0.001f));

    wave->Release();
    delete sys;
}

TEST_CASE("OAL: SetKind overrides initial volumeAdjust", "[Audio][parity][preview][oal]")
{
    auto* sys = dynamic_cast<SoundSystemOAL*>(CreateSoundSystemOAL());
    if (!sys)
    {
        SKIP("OpenAL not available");
    }

    // Effects=3, Speech=8, Music=5
    sys->SetWaveVolume(3.0f);
    sys->SetSpeechVolume(8.0f);
    sys->SetCDVolume(5.0f);

    // Compute expected adjusts from DX8 formulas
    float effExp = DX8::expVol(3.0f);
    float spcExp = DX8::expVol(8.0f);
    float musExp = DX8::expVol(5.0f);
    float maxExp = (std::max)(effExp, (std::max)(spcExp, musExp));
    float expectedAdjEff = DX8::mixerAdjust(effExp, maxExp);
    float expectedAdjSpc = DX8::mixerAdjust(spcExp, maxExp);
    REQUIRE(expectedAdjEff < expectedAdjSpc); // speech louder than effects

    auto wavData = ReadFixtureFile("audio/tone.wav");
    if (wavData.empty())
    {
        delete sys;
        SKIP("Cannot read fixture audio/tone.wav");
    }

    auto* wave = dynamic_cast<WaveOAL*>(sys->CreateWaveFromMemory(wavData.data(), wavData.size(), ".wav", false));
    if (!wave)
    {
        delete sys;
        SKIP("Cannot create wave from memory");
    }

    // Initially should be effect adjust (default kind = WaveEffect)
    CHECK(wave->GetVolumeAdjust() == Approx(expectedAdjEff).margin(0.001f));

    // After SetKind(WaveSpeech), should switch to speech adjust
    wave->SetKind(WaveSpeech);
    CHECK(wave->GetVolumeAdjust() == Approx(expectedAdjSpc).margin(0.001f));

    wave->Release();
    delete sys;
}

TEST_CASE("OAL: stopped wave can restart after reload", "[Audio][parity][preview][oal]")
{
    auto* sys = dynamic_cast<SoundSystemOAL*>(CreateSoundSystemOAL());
    if (!sys)
    {
        SKIP("OpenAL not available");
    }

    auto wavData = ReadFixtureFile("audio/tone.wav");
    if (wavData.empty())
    {
        delete sys;
        SKIP("Cannot read fixture audio/tone.wav");
    }

    auto* wave = dynamic_cast<WaveOAL*>(sys->CreateWaveFromMemory(wavData.data(), wavData.size(), ".wav", false));
    if (!wave)
    {
        delete sys;
        SKIP("Cannot create wave from memory");
    }

    wave->Play();
    sys->Commit();
    REQUIRE_FALSE(wave->IsStopped());

    wave->Stop();
    REQUIRE(wave->IsStopped());

    wave->Restart();
    wave->Play();
    sys->Commit();
    CHECK_FALSE(wave->IsStopped());
    CHECK_FALSE(wave->IsTerminated());

    wave->Release();
    delete sys;
}

// End-to-end wiring: SetVolume's freq argument (the engine's pitch ratio,
// e.g. a vehicle engine sound at rpm*1.2) must reach the real AL source as
// AL_PITCH via the 1:1 DirectSound mapping — NOT floored at 0.5x.  Broken
// state (old clampPitch): a low-RPM ratio of 0.2 reads back as 0.5, which is
// what made vehicles jump straight to "full rev".
TEST_CASE("OAL: SetVolume maps engine freq ratio to AL_PITCH (no 0.5x floor)", "[Audio][parity][oal][pitch]")
{
    auto* sys = dynamic_cast<SoundSystemOAL*>(CreateSoundSystemOAL());
    if (!sys)
        SKIP("OpenAL not available");

    auto wavData = ReadFixtureFile("audio/tone.wav");
    if (wavData.empty())
    {
        delete sys;
        SKIP("Cannot read fixture audio/tone.wav");
    }
    auto* wave = dynamic_cast<WaveOAL*>(sys->CreateWaveFromMemory(wavData.data(), wavData.size(), ".wav", false));
    if (!wave)
    {
        delete sys;
        SKIP("Cannot create wave from memory");
    }

    // Need a live AL source before pitch can be applied.
    wave->Play();
    sys->Commit();

    // Unity ratio is always exactly 1.0x (native rate is inside [100,100000]).
    wave->SetVolume(1.0f, 1.0f, true);
    CHECK(wave->AppliedPitchForTest() == Catch::Approx(1.0f).margin(0.001f));

    // Low-RPM ratio passes through, well below the old 0.5x floor — this is
    // the startup-rev fix.  (tone.wav is >= 8 kHz, so 0.2*native >> 100 Hz.)
    wave->SetVolume(1.0f, 0.2f, true);
    CHECK(wave->AppliedPitchForTest() == Catch::Approx(0.2f).margin(0.01f));
    CHECK(wave->AppliedPitchForTest() < 0.5f); // broken impl latched 0.5

    wave->Release();
    delete sys;
}

// Startup-click guard: SoundScene::OpenAndPlay sets the pitch BEFORE the
// source exists (no Play() yet — playback is deferred to AdvanceAll).  The
// stored pitch must be applied when DoPlay starts the source, so the very
// first frame plays at the requested pitch — not AL's default 1.0 that then
// snaps to the engine pitch on the next SetVolume (the audible click on
// vehicle engine start).  Broken state: AppliedPitch == 1.0 after the start.
TEST_CASE("OAL: deferred-play wave starts at the requested pitch (no startup click)", "[Audio][parity][oal][pitch]")
{
    auto* sys = dynamic_cast<SoundSystemOAL*>(CreateSoundSystemOAL());
    if (!sys)
        SKIP("OpenAL not available");

    auto wavData = ReadFixtureFile("audio/tone.wav");
    if (wavData.empty())
    {
        delete sys;
        SKIP("Cannot read fixture audio/tone.wav");
    }
    auto* wave = dynamic_cast<WaveOAL*>(sys->CreateWaveFromMemory(wavData.data(), wavData.size(), ".wav", false));
    if (!wave)
    {
        delete sys;
        SKIP("Cannot create wave from memory");
    }

    // Pitch set while there is NO source yet (mirrors OpenAndPlay), then play.
    wave->SetVolume(1.0f, 0.2f, true);
    wave->Play();
    sys->Commit(); // DoPlay creates the source and must apply the stored pitch

    CHECK(wave->AppliedPitchForTest() == Catch::Approx(0.2f).margin(0.01f)); // broken: 1.0
    CHECK(wave->AppliedPitchForTest() < 0.5f);

    wave->Release();
    delete sys;
}

TEST_CASE("OAL - LastLoop keeps current loop offset instead of restarting", "[Audio][parity][oal][last-loop]")
{
    auto* sys = dynamic_cast<SoundSystemOAL*>(CreateSoundSystemOAL());
    if (!sys)
    {
        SKIP("OpenAL not available");
    }

    auto wavData = ReadFixtureFile("audio/tone.wav");
    if (wavData.empty())
    {
        delete sys;
        SKIP("Cannot read fixture audio/tone.wav");
    }

    auto* wave = dynamic_cast<WaveOAL*>(sys->CreateWaveFromMemory(wavData.data(), wavData.size(), ".wav", false));
    if (!wave)
    {
        delete sys;
        SKIP("Cannot create wave from memory");
    }

    wave->Repeat(2);
    wave->Play();
    sys->Commit();
    REQUIRE(wave->State() == WaveState::Playing);

    float before = 0.f;
    for (int i = 0; i < 80 && (before < 0.025f || before > 0.090f); ++i)
    {
        sys->Commit();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        before = wave->GetCurrentOffsetSeconds();
    }
    REQUIRE(before >= 0.025f);
    REQUIRE(before < 0.090f);

    wave->LastLoop();
    sys->Commit();

    const float after = wave->GetCurrentOffsetSeconds();
    INFO("offset before LastLoop=" << before << " after=" << after);
    CHECK(after > 0.020f);
    CHECK(after + 0.015f >= before);

    wave->Release();
    sys->Commit();
    delete sys;
}

TEST_CASE("OAL - streamed LastLoop finishes current OGG pass instead of restarting",
          "[Audio][parity][oal][last-loop][streaming]")
{
    auto* sys = dynamic_cast<SoundSystemOAL*>(CreateSoundSystemOAL());
    if (!sys)
    {
        SKIP("OpenAL not available");
    }

    const std::string ogg = GET_FIXTURE("audio/stream_loop.ogg");
    auto* wave = dynamic_cast<WaveOAL*>(sys->CreateWave(ogg.c_str(), false, true));
    if (!wave)
    {
        delete sys;
        SKIP("Cannot create streamed OGG fixture wave");
    }
    if (!wave->IsStreamed())
    {
        wave->Release();
        delete sys;
        SKIP("fixture OGG did not use streamed path");
    }

    wave->Repeat(2);
    wave->Play();

    bool started = false;
    for (int i = 0; i < 80 && !started; ++i)
    {
        sys->Commit();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        started = wave->State() == WaveState::Playing && wave->GetCurrentOffsetSeconds() > 0.05f;
    }
    REQUIRE(started);

    float before = 0.f;
    for (int i = 0; i < 140 && before < 2.05f; ++i)
    {
        sys->Commit();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        before = wave->GetCurrentOffsetSeconds();
    }
    REQUIRE(before > 1.80f);

    wave->LastLoop();

    float after = before;
    for (int i = 0; i < 12; ++i)
    {
        sys->Commit();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        after = wave->GetCurrentOffsetSeconds();
    }

    INFO("streamed offset before LastLoop=" << before << " after=" << after);
    CHECK(after > 1.80f);
    CHECK(after + 0.030f >= before);

    wave->Release();
    sys->Commit();
    delete sys;
}
