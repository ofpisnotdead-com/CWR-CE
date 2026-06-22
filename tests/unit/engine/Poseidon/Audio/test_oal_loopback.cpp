// OpenAL loopback integration tests — full pipeline without speakers
// Uses ALC_SOFT_loopback to render audio to memory buffers
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <Poseidon/Audio/Streaming/WaveStream.hpp>
#include <Poseidon/Audio/Streaming/WaveLoaders.hpp>
#include "test_fixtures.hpp"
#include <cmath>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <stdint.h>
#include <format>

using namespace Poseidon;
using Catch::Approx;

namespace
{
bool AudioDiagnosticsEnabled()
{
    const char* value = std::getenv("POSEIDON_TEST_LOG");
    return value && value[0] && std::strcmp(value, "0") != 0 && std::strcmp(value, "false") != 0 &&
           std::strcmp(value, "off") != 0;
}
} // namespace

// Loopback device RAII wrapper
struct LoopbackDevice
{
    LPALCLOOPBACKOPENDEVICESOFT alcLoopbackOpenDeviceSOFT = nullptr;
    LPALCRENDERSAMPLESSOFT alcRenderSamplesSOFT = nullptr;
    ALCdevice* device = nullptr;
    ALCcontext* context = nullptr;
    int sampleRate = 44100;
    bool hrtfActive = false; // set after Init(true): true only if HRTF really engaged

    bool Init(bool enableHrtf = false)
    {
        if (!alcIsExtensionPresent(nullptr, "ALC_SOFT_loopback"))
            return false;
        alcLoopbackOpenDeviceSOFT =
            reinterpret_cast<LPALCLOOPBACKOPENDEVICESOFT>(alcGetProcAddress(nullptr, "alcLoopbackOpenDeviceSOFT"));
        alcRenderSamplesSOFT =
            reinterpret_cast<LPALCRENDERSAMPLESSOFT>(alcGetProcAddress(nullptr, "alcRenderSamplesSOFT"));
        if (!alcLoopbackOpenDeviceSOFT || !alcRenderSamplesSOFT)
            return false;

        device = alcLoopbackOpenDeviceSOFT(nullptr);
        if (!device)
            return false;

        ALCint attrs[] = {ALC_FORMAT_TYPE_SOFT,
                          ALC_SHORT_SOFT,
                          ALC_FORMAT_CHANNELS_SOFT,
                          ALC_STEREO_SOFT,
                          ALC_FREQUENCY,
                          sampleRate,
                          ALC_HRTF_SOFT,
                          enableHrtf ? ALC_TRUE : ALC_FALSE,
                          0};
        context = alcCreateContext(device, attrs);
        if (!context)
        {
            alcCloseDevice(device);
            device = nullptr;
            return false;
        }
        alcMakeContextCurrent(context);
        alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);

        if (enableHrtf)
        {
            ALCint hrtfState = ALC_FALSE;
            alcGetIntegerv(device, ALC_HRTF_SOFT, 1, &hrtfState);
            hrtfActive = (hrtfState == ALC_TRUE);
        }
        return true;
    }

    void Render(std::vector<int16_t>& buf, int frames)
    {
        buf.resize(frames * 2); // stereo
        alcRenderSamplesSOFT(device, buf.data(), frames);
    }

    ~LoopbackDevice()
    {
        if (context)
        {
            alcMakeContextCurrent(nullptr);
            alcDestroyContext(context);
        }
        if (device)
            alcCloseDevice(device);
    }
};

static float RmsInt16(const std::vector<int16_t>& buf)
{
    if (buf.empty())
        return 0.f;
    double sum = 0.0;
    for (int16_t s : buf)
        sum += static_cast<double>(s) * s;
    return static_cast<float>(std::sqrt(sum / buf.size()));
}

static bool IsSilence(const std::vector<int16_t>& buf, float threshold = 10.f)
{
    return RmsInt16(buf) < threshold;
}

// Helper: load WAV fixture into AL buffer
static ALuint LoadFixtureToBuffer(const char* fixture)
{
    const char* path = GET_FIXTURE(fixture);
    WaveStream* stream = SoundLoadFile(path);
    if (!stream)
        return 0;

    WAVEFORMATEX fmt;
    stream->GetFormat(fmt);
    int dataSize = stream->GetUncompressedSize();
    std::vector<uint8_t> pcm(dataSize);
    stream->GetData(pcm.data(), 0, dataSize);

    ALenum alFmt;
    if (fmt.nChannels == 1)
        alFmt = fmt.wBitsPerSample == 8 ? AL_FORMAT_MONO8 : AL_FORMAT_MONO16;
    else
        alFmt = fmt.wBitsPerSample == 8 ? AL_FORMAT_STEREO8 : AL_FORMAT_STEREO16;

    ALuint buf;
    alGenBuffers(1, &buf);
    alBufferData(buf, alFmt, pcm.data(), static_cast<ALsizei>(dataSize), fmt.nSamplesPerSec);
    delete stream;

    if (alGetError() != AL_NO_ERROR)
    {
        alDeleteBuffers(1, &buf);
        return 0;
    }
    return buf;
}

TEST_CASE("OpenAL loopback: device init", "[Audio][integration]")
{
    LoopbackDevice dev;
    REQUIRE(dev.Init());

    // Render silence (no sources playing)
    std::vector<int16_t> buf;
    dev.Render(buf, 1024);
    CHECK(IsSilence(buf));
}

TEST_CASE("OpenAL loopback: play WAV fixture produces signal", "[Audio][integration]")
{
    LoopbackDevice dev;
    REQUIRE(dev.Init());

    ALuint alBuf = LoadFixtureToBuffer("audio/tone.wav");
    REQUIRE(alBuf != 0);

    ALuint src;
    alGenSources(1, &src);
    alSourcei(src, AL_BUFFER, static_cast<ALint>(alBuf));
    alSourcef(src, AL_GAIN, 1.f);
    alSourcei(src, AL_SOURCE_RELATIVE, AL_TRUE);
    alSource3f(src, AL_POSITION, 0.f, 0.f, 0.f);
    alSourcePlay(src);

    // Render enough frames for ~300ms of audio
    int frames = dev.sampleRate * 300 / 1000;
    std::vector<int16_t> buf;
    dev.Render(buf, frames);

    CHECK_FALSE(IsSilence(buf));
    float rms = RmsInt16(buf);
    CHECK(rms > 50.f); // meaningful signal

    alDeleteSources(1, &src);
    alDeleteBuffers(1, &alBuf);
}

TEST_CASE("OpenAL loopback: volume 0 produces silence", "[Audio][integration]")
{
    LoopbackDevice dev;
    REQUIRE(dev.Init());

    ALuint alBuf = LoadFixtureToBuffer("audio/tone.wav");
    REQUIRE(alBuf != 0);

    ALuint src;
    alGenSources(1, &src);
    alSourcei(src, AL_BUFFER, static_cast<ALint>(alBuf));
    alSourcef(src, AL_GAIN, 0.f);
    alSourcei(src, AL_SOURCE_RELATIVE, AL_TRUE);
    alSourcePlay(src);

    std::vector<int16_t> buf;
    dev.Render(buf, dev.sampleRate / 4);
    CHECK(IsSilence(buf));

    alDeleteSources(1, &src);
    alDeleteBuffers(1, &alBuf);
}

TEST_CASE("OpenAL loopback: volume scaling", "[Audio][integration]")
{
    LoopbackDevice dev;
    REQUIRE(dev.Init());

    ALuint alBuf = LoadFixtureToBuffer("audio/tone.wav");
    REQUIRE(alBuf != 0);

    // Play at gain=1.0
    ALuint src;
    alGenSources(1, &src);
    alSourcei(src, AL_BUFFER, static_cast<ALint>(alBuf));
    alSourcef(src, AL_GAIN, 1.f);
    alSourcei(src, AL_SOURCE_RELATIVE, AL_TRUE);
    alSourcePlay(src);

    int frames = dev.sampleRate / 10;
    std::vector<int16_t> buf1;
    dev.Render(buf1, frames);
    float rmsLoud = RmsInt16(buf1);

    // Stop, replay at gain=0.1
    alSourceStop(src);
    alSourceRewind(src);
    alSourcef(src, AL_GAIN, 0.1f);
    alSourcePlay(src);

    std::vector<int16_t> buf2;
    dev.Render(buf2, frames);
    float rmsQuiet = RmsInt16(buf2);

    CHECK(rmsLoud > rmsQuiet * 2.f);

    alDeleteSources(1, &src);
    alDeleteBuffers(1, &alBuf);
}

TEST_CASE("OpenAL loopback: 3D distance attenuation", "[Audio][integration]")
{
    LoopbackDevice dev;
    REQUIRE(dev.Init());

    ALuint alBuf = LoadFixtureToBuffer("audio/tone.wav");
    REQUIRE(alBuf != 0);

    // Set listener at origin
    alListener3f(AL_POSITION, 0.f, 0.f, 0.f);

    // Source near (1m)
    ALuint src;
    alGenSources(1, &src);
    alSourcei(src, AL_BUFFER, static_cast<ALint>(alBuf));
    alSourcef(src, AL_GAIN, 1.f);
    alSourcei(src, AL_SOURCE_RELATIVE, AL_FALSE);
    alSource3f(src, AL_POSITION, 1.f, 0.f, 0.f);
    alSourcef(src, AL_REFERENCE_DISTANCE, 1.f);
    alSourcef(src, AL_MAX_DISTANCE, 100.f);
    alSourcef(src, AL_ROLLOFF_FACTOR, 1.f);
    alSourcePlay(src);

    int frames = dev.sampleRate / 10;
    std::vector<int16_t> bufNear;
    dev.Render(bufNear, frames);
    float rmsNear = RmsInt16(bufNear);

    // Move source far (50m)
    alSourceStop(src);
    alSourceRewind(src);
    alSource3f(src, AL_POSITION, 50.f, 0.f, 0.f);
    alSourcePlay(src);

    std::vector<int16_t> bufFar;
    dev.Render(bufFar, frames);
    float rmsFar = RmsInt16(bufFar);

    CHECK(rmsNear > rmsFar);

    alDeleteSources(1, &src);
    alDeleteBuffers(1, &alBuf);
}

TEST_CASE("OpenAL loopback: stopped source produces silence", "[Audio][integration]")
{
    LoopbackDevice dev;
    REQUIRE(dev.Init());

    ALuint alBuf = LoadFixtureToBuffer("audio/tone.wav");
    REQUIRE(alBuf != 0);

    ALuint src;
    alGenSources(1, &src);
    alSourcei(src, AL_BUFFER, static_cast<ALint>(alBuf));
    alSourcef(src, AL_GAIN, 1.f);
    alSourcei(src, AL_SOURCE_RELATIVE, AL_TRUE);
    alSourcePlay(src);
    alSourceStop(src);

    std::vector<int16_t> buf;
    dev.Render(buf, dev.sampleRate / 10);
    CHECK(IsSilence(buf));

    alDeleteSources(1, &src);
    alDeleteBuffers(1, &alBuf);
}

// ---- Spatial-audio rotation regressions ----
// A positional source must stay click-free as the listener rotates next to it:
// the mixer ramps per-frame pan/gain changes, so a fast yaw sweep introduces no
// step discontinuity (no zipper crackle).  Metric: SECOND difference of the
// rendered samples — a band-limited sine has a tiny 2nd diff (~A*w^2), so any
// per-frame gain/pan STEP stands out sharply.  Verified against a real-engine
// capture: a steady tone through the live OpenAL 3D path under continuous
// rotation is statistically identical to the static case.
static constexpr double kSinePi = 3.14159265358979323846;

static ALuint GenSineBuffer(int sampleRate, float freq, float seconds)
{
    int n = static_cast<int>(sampleRate * seconds);
    std::vector<int16_t> pcm(n);
    for (int i = 0; i < n; i++)
        pcm[i] = static_cast<int16_t>(20000.0 * std::sin(2.0 * kSinePi * freq * i / sampleRate));
    ALuint buf;
    alGenBuffers(1, &buf);
    alBufferData(buf, AL_FORMAT_MONO16, pcm.data(), static_cast<ALsizei>(n * sizeof(int16_t)), sampleRate);
    return buf;
}

// Render `totalFrames`, stepping the listener once per `frameChunk` (= one
// "game frame": pose held constant within a chunk, as the engine holds it for
// a whole frame).  Yaw sweeps yaw0..yaw1; orbitR>0 also moves the listener
// POSITION on a circle of that radius synced to yaw (the head/eye arc — the
// engine moves cam.Position() while reporting ~0 velocity).
static std::vector<int16_t> RenderSweep(LoopbackDevice& dev, float yaw0Deg, float yaw1Deg, float orbitR, int frameChunk,
                                        int totalFrames)
{
    std::vector<int16_t> out;
    out.reserve(static_cast<size_t>(totalFrames) * 2);
    int rendered = 0;
    while (rendered < totalFrames)
    {
        float t = static_cast<float>(rendered) / totalFrames;
        float yaw = (yaw0Deg + (yaw1Deg - yaw0Deg) * t) * static_cast<float>(kSinePi / 180.0);
        float ori[6] = {std::sin(yaw), 0.f, -std::cos(yaw), 0.f, 1.f, 0.f};
        alListenerfv(AL_ORIENTATION, ori);
        if (orbitR > 0.f)
            alListener3f(AL_POSITION, orbitR * std::sin(yaw), 0.f, orbitR * (1.f - std::cos(yaw)));
        int chunk = std::min(frameChunk, totalFrames - rendered);
        std::vector<int16_t> buf;
        dev.Render(buf, chunk);
        out.insert(out.end(), buf.begin(), buf.end());
        rendered += chunk;
    }
    return out;
}

// Click metric via SECOND difference |s[i]-2 s[i-1]+s[i-2]|: a band-limited
// sine has a tiny 2nd diff (~A·w^2), so a panning/gain STEP stands out sharply.
static void ClickStats(const std::vector<int16_t>& buf, int channel, int threshold, int& count, int& maxv)
{
    count = 0;
    maxv = 0;
    for (size_t i = channel + 4; i < buf.size(); i += 2)
    {
        int d2 = std::abs(static_cast<int>(buf[i]) - 2 * static_cast<int>(buf[i - 2]) + static_cast<int>(buf[i - 4]));
        maxv = std::max(maxv, d2);
        if (d2 > threshold)
            count++;
    }
}

TEST_CASE("Spatial audio: 3D panning stays click-free under listener rotation", "[Audio][rotation]")
{
    LoopbackDevice dev;
    REQUIRE(dev.Init());

    auto setup = [&](float distM) -> ALuint
    {
        ALuint src;
        alGenSources(1, &src);
        ALuint buf = GenSineBuffer(dev.sampleRate, 220.f, 1.0f); // voice-fundamental-ish tone, looped
        alSourcei(src, AL_BUFFER, static_cast<ALint>(buf));
        alSourcei(src, AL_LOOPING, AL_TRUE);
        alSourcef(src, AL_GAIN, 1.f);
        alSourcei(src, AL_SOURCE_RELATIVE, AL_FALSE);
        alSource3f(src, AL_POSITION, 0.f, 0.f, -distM); // straight ahead at distM
        alSourcef(src, AL_REFERENCE_DISTANCE, 1.f);
        alSourcef(src, AL_MAX_DISTANCE, 100.f);
        alSourcef(src, AL_ROLLOFF_FACTOR, 1.f);
        return src;
    };

    alListener3f(AL_POSITION, 0.f, 0.f, 0.f);
    const int total = dev.sampleRate / 2; // 0.5 s
    const int frame30 = dev.sampleRate / 30;

    struct Case
    {
        const char* name;
        float dist, yaw0, yaw1, orbit;
        int chunk;
    };
    Case cases[] = {
        {"0.3m STATIC", 0.3f, 0.f, 0.f, 0.f, frame30},
        {"0.3m ROTATE160 @30fps", 0.3f, -80.f, 80.f, 0.f, frame30},
        {"0.3m ROTATE340 @30fps (thru back)", 0.3f, -170.f, 170.f, 0.f, frame30},
        {"0.5m ROTATE160 @30fps", 0.5f, -80.f, 80.f, 0.f, frame30},
        {"1m ROTATE160 + eye-arc0.18", 1.f, -80.f, 80.f, 0.18f, frame30},
        {"0.5m ROTATE160 + eye-arc0.18", 0.5f, -80.f, 80.f, 0.18f, frame30},
        {"far(15m) ROTATE160 @30fps", 15.f, -80.f, 80.f, 0.f, frame30},
    };
    const bool diagnostics = AudioDiagnosticsEnabled();
    if (diagnostics)
        std::printf("\n[rotation] sampleRate=%d total=%d  (2nd-diff click threshold=500)\n", dev.sampleRate, total);
    int worstMax = 0, totalClicks = 0;
    for (auto& c : cases)
    {
        alListener3f(AL_POSITION, 0.f, 0.f, 0.f);
        ALuint src = setup(c.dist);
        alSourcePlay(src);
        auto pcm = RenderSweep(dev, c.yaw0, c.yaw1, c.orbit, c.chunk, total);
        int cL, mL, cR, mR;
        ClickStats(pcm, 0, 500, cL, mL);
        ClickStats(pcm, 1, 500, cR, mR);
        if (diagnostics)
            std::printf("  %-34s click2 L/R=%5d/%-5d  max2 L/R=%5d/%-5d\n", c.name, cL, cR, mL, mR);
        worstMax = std::max({worstMax, mL, mR});
        totalClicks += cL + cR;
        alSourceStop(src);
        ALint bufId = 0;
        alGetSourcei(src, AL_BUFFER, &bufId);
        alDeleteSources(1, &src);
        if (bufId)
        {
            ALuint b = static_cast<ALuint>(bufId);
            alSourcei(src, AL_BUFFER, 0);
            alDeleteBuffers(1, &b);
        }
    }
    // Clean panning peaks at a 2nd diff of ~600 with 0-1 stray clicks per case;
    // a per-frame pan/gain zipper would push max2 into the thousands.  1500 sits
    // cleanly between, and the aggregate click budget catches a subtler step.
    CHECK(worstMax < 1500);
    CHECK(totalClicks < 12);
}

// HRTF is the one mixer mechanism the plain-stereo loopback test never
// exercised: OpenAL Soft enables it for headphone output, and per-direction
// HRIR convolution must crossfade as the source's relative angle changes.  This
// is the path headphone users hit; if the crossfade zipped under fast yaw it
// would crackle near a source while rotating yet stay clean straight-on.  Force
// HRTF on and run the same rotate sweep, including a 180-degree snap-flick — the
// crossfade stays click-free.
TEST_CASE("Spatial audio: HRTF crossfade stays click-free under listener rotation", "[Audio][rotation]")
{
    LoopbackDevice hrtf;
    REQUIRE(hrtf.Init(true));
    if (!hrtf.hrtfActive)
    {
        WARN("HRTF did not engage on the loopback device — skipping HRTF zipper probe");
        return;
    }

    auto setup = [&](LoopbackDevice& dev, float distM) -> ALuint
    {
        ALuint src;
        alGenSources(1, &src);
        ALuint buf = GenSineBuffer(dev.sampleRate, 220.f, 1.0f);
        alSourcei(src, AL_BUFFER, static_cast<ALint>(buf));
        alSourcei(src, AL_LOOPING, AL_TRUE);
        alSourcef(src, AL_GAIN, 1.f);
        alSourcei(src, AL_SOURCE_RELATIVE, AL_FALSE);
        alSource3f(src, AL_POSITION, 0.f, 0.f, -distM);
        alSourcef(src, AL_REFERENCE_DISTANCE, 1.f);
        alSourcef(src, AL_MAX_DISTANCE, 100.f);
        alSourcef(src, AL_ROLLOFF_FACTOR, 1.f);
        return src;
    };

    const int total = hrtf.sampleRate / 2; // 0.5 s

    struct Case
    {
        const char* name;
        float dist, yaw0, yaw1, orbit;
        int fps;
    };
    Case cases[] = {
        {"0.3m STATIC", 0.3f, 0.f, 0.f, 0.f, 30},
        {"0.3m ROTATE160 @30fps", 0.3f, -80.f, 80.f, 0.f, 30},
        {"0.3m ROTATE160 @15fps (coarse)", 0.3f, -80.f, 80.f, 0.f, 15},
        {"0.3m ROTATE340 @30fps (thru back)", 0.3f, -170.f, 170.f, 0.f, 30},
        {"0.3m SNAP180 @30fps (fast flick)", 0.3f, -90.f, 90.f, 0.f, 30},
        {"1m ROTATE160 + eye-arc0.18", 1.f, -80.f, 80.f, 0.18f, 30},
    };
    const bool diagnostics = AudioDiagnosticsEnabled();
    if (diagnostics)
        std::printf("\n[rotation-HRTF] HRTF engaged on loopback; same sweep, click2 threshold=500\n");
    int worstHrtf = 0;
    for (auto& c : cases)
    {
        int chunk = hrtf.sampleRate / c.fps;
        alListener3f(AL_POSITION, 0.f, 0.f, 0.f);
        ALuint src = setup(hrtf, c.dist);
        alSourcePlay(src);
        auto pcm = RenderSweep(hrtf, c.yaw0, c.yaw1, c.orbit, chunk, total);
        int cL, mL, cR, mR;
        ClickStats(pcm, 0, 500, cL, mL);
        ClickStats(pcm, 1, 500, cR, mR);
        worstHrtf = std::max({worstHrtf, mL, mR});
        if (diagnostics)
            std::printf("  %-36s click2 L/R=%5d/%-5d  max2 L/R=%5d/%-5d\n", c.name, cL, cR, mL, mR);
        alSourceStop(src);
        ALint bufId = 0;
        alGetSourcei(src, AL_BUFFER, &bufId);
        alDeleteSources(1, &src);
        if (bufId)
        {
            ALuint b = static_cast<ALuint>(bufId);
            alDeleteBuffers(1, &b);
        }
    }
    if (diagnostics)
        std::printf("[rotation-HRTF] worst 2nd-diff across all HRTF cases = %d\n", worstHrtf);
    // HRTF crossfade peaks at a 2nd diff of ~350 even on a 180-degree flick;
    // a zipping crossfade would run into the thousands.  1000 is a clean gate.
    CHECK(worstHrtf < 1000);
}

// The OGG dialogue path streams through a ring of kNumBuffers x kChunkMs
// buffers (WaveStreamingBuffers), refilled once per game frame.  The ring must
// absorb a typical frame hitch without the source draining to AL_STOPPED (an
// underrun = an audible dropout).  Inject a hitch of `hitchMs` (no refill for
// that span) and confirm a 150ms stall — well inside the ring depth — does not
// underrun, while documenting where the ring's ceiling actually is.
TEST_CASE("Streaming ring absorbs a typical frame hitch without underrun", "[Audio][streaming]")
{
    LoopbackDevice dev;
    REQUIRE(dev.Init());

    const int chunkMs = 100, numBuf = 4; // mirrors WaveStreamingBuffers
    const int chunkFrames = dev.sampleRate * chunkMs / 1000;

    const bool diagnostics = AudioDiagnosticsEnabled();
    auto runHitch = [&](int hitchMs) -> bool
    {
        // Pre-generate a long continuous sine split into chunkFrames slices.
        std::vector<ALuint> bufs(64);
        alGenBuffers(64, bufs.data());
        int phase = 0;
        auto fillChunk = [&](ALuint b)
        {
            std::vector<int16_t> pcm(chunkFrames);
            for (int i = 0; i < chunkFrames; i++)
                pcm[i] = static_cast<int16_t>(15000.0 * std::sin(2.0 * kSinePi * 220.0 * (phase + i) / dev.sampleRate));
            phase += chunkFrames;
            alBufferData(b, AL_FORMAT_MONO16, pcm.data(), static_cast<ALsizei>(chunkFrames * 2), dev.sampleRate);
        };
        ALuint src;
        alGenSources(1, &src);
        alSourcei(src, AL_SOURCE_RELATIVE, AL_TRUE);
        alSource3f(src, AL_POSITION, 0.f, 0.f, 0.f);
        int nextBuf = 0;
        for (int i = 0; i < numBuf; i++)
            fillChunk(bufs[nextBuf++]);
        alSourceQueueBuffers(src, numBuf, bufs.data());
        alSourcePlay(src);

        std::vector<int16_t> out;
        bool underran = false;
        auto frame = [&](int frameMs, bool refill)
        {
            if (refill)
            {
                ALint processed = 0;
                alGetSourcei(src, AL_BUFFERS_PROCESSED, &processed);
                while (processed-- > 0)
                {
                    ALuint b = 0;
                    alSourceUnqueueBuffers(src, 1, &b);
                    fillChunk(b);
                    alSourceQueueBuffers(src, 1, &b);
                }
                ALint st = 0;
                alGetSourcei(src, AL_SOURCE_STATE, &st);
                if (st != AL_PLAYING)
                {
                    underran = true;
                    alSourcePlay(src); // engine's recovery
                }
            }
            std::vector<int16_t> buf;
            dev.Render(buf, dev.sampleRate * frameMs / 1000);
            out.insert(out.end(), buf.begin(), buf.end());
        };
        for (int f = 0; f < 6; f++)
            frame(33, true);   // steady-state ~30fps
        frame(hitchMs, false); // THE HITCH: no refill for hitchMs
        for (int f = 0; f < 6; f++)
            frame(33, true); // recover

        // count silence gap (consecutive near-zero samples) after the hitch
        int maxGap = 0, gap = 0;
        for (size_t i = 0; i < out.size(); i += 2)
        {
            if (std::abs(static_cast<int>(out[i])) < 30)
            {
                gap++;
                maxGap = std::max(maxGap, gap);
            }
            else
                gap = 0;
        }
        if (diagnostics)
            std::printf("  hitch=%4dms  underran=%d  maxSilenceGap=%5d samples (%.0f ms)\n", hitchMs, underran ? 1 : 0,
                        maxGap, 1000.0 * maxGap / dev.sampleRate);
        alSourceStop(src);
        alSourcei(src, AL_BUFFER, 0);
        alDeleteSources(1, &src);
        alDeleteBuffers(64, bufs.data());
        return underran;
    };

    if (diagnostics)
        std::printf("\n[streaming] ring=%dx%dms=%dms buffer\n", numBuf, chunkMs, numBuf * chunkMs);
    const bool underran150 = runHitch(150); // typical bad hitch — inside the ring
    runHitch(350);                          // near the edge (informational)
    const bool underran500 = runHitch(500); // exceeds the ring — must underrun

    // The 400ms ring (4 x 100ms) must ride out a 150ms stall; if it underruns
    // here the ring is undersized.  The 500ms stall exceeds the ring and must
    // underrun — that asserts the harness actually detects a drain.
    CHECK_FALSE(underran150);
    CHECK(underran500);
}

TEST_CASE("OpenAL loopback: multiple sources mix", "[Audio][integration]")
{
    LoopbackDevice dev;
    REQUIRE(dev.Init());

    ALuint alBuf = LoadFixtureToBuffer("audio/tone.wav");
    REQUIRE(alBuf != 0);

    // Single source
    ALuint src1;
    alGenSources(1, &src1);
    alSourcei(src1, AL_BUFFER, static_cast<ALint>(alBuf));
    alSourcef(src1, AL_GAIN, 1.f);
    alSourcei(src1, AL_SOURCE_RELATIVE, AL_TRUE);
    alSourcePlay(src1);

    int frames = dev.sampleRate / 10;
    std::vector<int16_t> buf1;
    dev.Render(buf1, frames);
    float rmsSingle = RmsInt16(buf1);

    // Two sources playing same audio
    alSourceStop(src1);
    alSourceRewind(src1);
    ALuint src2;
    alGenSources(1, &src2);
    alSourcei(src2, AL_BUFFER, static_cast<ALint>(alBuf));
    alSourcef(src2, AL_GAIN, 1.f);
    alSourcei(src2, AL_SOURCE_RELATIVE, AL_TRUE);

    alSourcePlay(src1);
    alSourcePlay(src2);

    std::vector<int16_t> buf2;
    dev.Render(buf2, frames);
    float rmsDouble = RmsInt16(buf2);

    // Two sources should be louder than one
    CHECK(rmsDouble > rmsSingle);

    alDeleteSources(1, &src2);
    alDeleteSources(1, &src1);
    alDeleteBuffers(1, &alBuf);
}
