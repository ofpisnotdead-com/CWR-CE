#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Audio/Voice/PCMCodec.hpp>
#include <PoseidonOpenAL/OpenALRuntime.hpp>
#include <PoseidonOpenAL/Voice/VoNSpeakerOpenAL.hpp>

#include <cstring>
#include <memory>
#include <vector>

using namespace Poseidon;
using Catch::Approx;

namespace
{
class LoopbackContext
{
  public:
    bool init()
    {
        if (!OpenALRuntime::EnsureLoaded())
            return false;

        _device = alcOpenDevice(nullptr);
        if (!_device)
            return false;

        _context = alcCreateContext(_device, nullptr);
        if (!_context)
        {
            alcCloseDevice(_device);
            _device = nullptr;
            return false;
        }

        alcMakeContextCurrent(_context);
        alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);
        return true;
    }

    ~LoopbackContext()
    {
        if (_context)
        {
            alcMakeContextCurrent(nullptr);
            alcDestroyContext(_context);
        }
        if (_device)
            alcCloseDevice(_device);
    }

  private:
    ALCdevice* _device = nullptr;
    ALCcontext* _context = nullptr;
};
} // namespace

TEST_CASE("VoN OpenAL speaker uses 2D radio and spatial Direct playback", "[Audio][VoN][OpenAL]")
{
    LoopbackContext context;
    if (!context.init())
        SKIP("OpenAL runtime device unavailable");

    VoNSpeakerOpenAL speaker;
    speaker.init();
    ALuint source = speaker.sourceForTests();
    REQUIRE(source != 0);

    ALint relative = AL_FALSE;
    alGetSourcei(source, AL_SOURCE_RELATIVE, &relative);
    CHECK(relative == AL_TRUE);

    speaker.setPosition(5.0f, 6.0f, 7.0f);
    ALfloat x = -1.0f;
    ALfloat y = -1.0f;
    ALfloat z = -1.0f;
    alGetSource3f(source, AL_POSITION, &x, &y, &z);
    CHECK(x == Approx(0.0f));
    CHECK(y == Approx(0.0f));
    CHECK(z == Approx(0.0f));

    speaker.setChannel(VoNChatChannel::Direct);
    alGetSourcei(source, AL_SOURCE_RELATIVE, &relative);
    CHECK(relative == AL_FALSE);
    alGetSource3f(source, AL_POSITION, &x, &y, &z);
    CHECK(x == Approx(-5.0f));
    CHECK(y == Approx(6.0f));
    CHECK(z == Approx(7.0f));

    ALfloat referenceDistance = 0.0f;
    ALfloat maxDistance = 0.0f;
    ALfloat rolloff = 0.0f;
    alGetSourcef(source, AL_REFERENCE_DISTANCE, &referenceDistance);
    alGetSourcef(source, AL_MAX_DISTANCE, &maxDistance);
    alGetSourcef(source, AL_ROLLOFF_FACTOR, &rolloff);
    CHECK(referenceDistance == Approx(1.0f));
    CHECK(maxDistance == Approx(20.0f));
    CHECK(rolloff == Approx(1.0f));

    speaker.setChannel(VoNChatChannel::Group);
    alGetSourcei(source, AL_SOURCE_RELATIVE, &relative);
    CHECK(relative == AL_TRUE);
    alGetSource3f(source, AL_POSITION, &x, &y, &z);
    CHECK(x == Approx(0.0f));
    CHECK(y == Approx(0.0f));
    CHECK(z == Approx(0.0f));
    alGetSourcef(source, AL_ROLLOFF_FACTOR, &rolloff);
    CHECK(rolloff == Approx(0.0f));

    speaker.destroy();
}

// Regression: after the drain-timeout stop at the end of a stream the source
// is AL_STOPPED, where every queued buffer immediately counts as processed.
// Without rewinding to AL_INITIAL the next stream's frames get recycled as
// fast as they are queued, AL_BUFFERS_QUEUED never reaches START_THRESHOLD,
// and the speaker stays mute for every stream after the first.
TEST_CASE("VoN OpenAL speaker restarts playback for a second stream", "[Audio][VoN][OpenAL]")
{
    LoopbackContext context;
    if (!context.init())
        SKIP("OpenAL runtime device unavailable");

    constexpr int FRAME = 320;
    constexpr uint32_t SENDER = 7;

    VoNClient client;
    client.setCodecFactory([]() { return std::make_unique<PCMCodec>(16000); });

    uint64_t origin = 0;
    auto pushFrame = [&]()
    {
        std::vector<int16_t> pcm(FRAME, 1000);
        std::vector<uint8_t> payload(FRAME * 2);
        std::memcpy(payload.data(), pcm.data(), payload.size());
        VoNDataPacket pkt;
        pkt.init(SENDER, VoNChatChannel::Global, origin, FRAME, static_cast<uint16_t>(payload.size()));
        origin += FRAME;
        client.onDataPacket(pkt, payload.data());
    };

    VoNSpeakerOpenAL speaker;
    speaker.init();
    REQUIRE(speaker.sourceForTests() != 0);

    // Stream 1: one frame per pump, playback starts once START_THRESHOLD
    // buffers are queued.
    for (int i = 0; i < 10; ++i)
    {
        pushFrame();
        speaker.feed(&client, SENDER);
    }
    REQUIRE(speaker.isActive());

    // Stream end: pumps with no data until the drain timeout stops the stream.
    for (int i = 0; i < 40 && speaker.isActive(); ++i)
        speaker.feed(&client, SENDER);
    REQUIRE_FALSE(speaker.isActive());

    // Stream 2: same shape as stream 1 — must start playing again.
    for (int i = 0; i < 10; ++i)
    {
        pushFrame();
        speaker.feed(&client, SENDER);
    }
    REQUIRE(speaker.isActive());

    speaker.destroy();
}
