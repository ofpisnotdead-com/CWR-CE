#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Audio/Voice/MicLoopback.hpp>
#include <Poseidon/Audio/Voice/PCMCodec.hpp>
#include <Poseidon/Audio/Voice/VoiceBackend.hpp>
#include <Poseidon/Audio/Voice/VonApp.hpp>
#include <Poseidon/Audio/Voice/VonCapture.hpp>
#include <Poseidon/Audio/Voice/VonSpeaker.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <stdint.h>
#include <stdlib.h>

using namespace Poseidon;
namespace
{
struct TestVoiceBackendState
{
    bool captureOpenReturn = true;
    bool loopbackOpenReturn = true;
    bool speakerFeedReturn = true;
    bool speakerFeedActive = true;
    float speakerFeedLevel = 0.42f;
    int captureAvailableSamples = 0;
    int captureReadValue = 1000;
    std::vector<std::string> devices{"Fake Mic A", "Fake Mic B"};

    bool captureOpen = false;
    bool captureCapturing = false;
    int captureSampleRate = 0;
    int captureOpenCalls = 0;
    int captureCloseCalls = 0;
    int captureStartCalls = 0;
    int captureStopCalls = 0;
    int captureReadCalls = 0;
    int captureLastReadCount = 0;
    float captureLastPeak = 0.0f;

    bool speakerInitialized = false;
    int speakerInitCalls = 0;
    int speakerDestroyCalls = 0;
    int speakerFeedCalls = 0;
    int speakerStopCalls = 0;
    VoNChatChannel speakerChannel = VoNChatChannel::Global;
    std::array<float, 3> speakerPosition{0.0f, 0.0f, 0.0f};

    bool loopbackOpen = false;
    int loopbackOpenCalls = 0;
    int loopbackCloseCalls = 0;
    int loopbackTickCalls = 0;
};

TestVoiceBackendState& VoiceState()
{
    static TestVoiceBackendState state;
    return state;
}

void ResetVoiceState()
{
    VoiceState() = TestVoiceBackendState{};
}

class TestVoiceCaptureBackend : public IVoiceCaptureBackend
{
  public:
    bool open(const char*, int sampleRate, int) override
    {
        auto& state = VoiceState();
        ++state.captureOpenCalls;
        state.captureSampleRate = sampleRate;
        state.captureOpen = state.captureOpenReturn;
        state.captureCapturing = false;
        return state.captureOpenReturn;
    }

    void close() override
    {
        auto& state = VoiceState();
        ++state.captureCloseCalls;
        state.captureOpen = false;
        state.captureCapturing = false;
        state.captureSampleRate = 0;
        state.captureLastPeak = 0.0f;
    }

    void start() override
    {
        auto& state = VoiceState();
        ++state.captureStartCalls;
        if (state.captureOpen)
            state.captureCapturing = true;
    }

    void stop() override
    {
        auto& state = VoiceState();
        ++state.captureStopCalls;
        state.captureCapturing = false;
    }

    int availableSamples() const override
    {
        return VoiceState().captureOpen ? VoiceState().captureAvailableSamples : 0;
    }

    int read(int16_t* buffer, int maxSamples) override
    {
        auto& state = VoiceState();
        if (!state.captureOpen || !state.captureCapturing)
            return 0;

        ++state.captureReadCalls;
        int toRead = std::min(state.captureAvailableSamples, maxSamples);
        state.captureLastReadCount = toRead;
        for (int i = 0; i < toRead; ++i)
            buffer[i] = static_cast<int16_t>(state.captureReadValue);

        state.captureAvailableSamples -= toRead;
        state.captureLastPeak = toRead > 0 ? static_cast<float>(std::abs(state.captureReadValue)) / 32767.0f : 0.0f;
        return toRead;
    }

    float lastFramePeak() const override { return VoiceState().captureLastPeak; }

    bool isOpen() const override { return VoiceState().captureOpen; }

    bool isCapturing() const override { return VoiceState().captureCapturing; }

    int sampleRate() const override { return VoiceState().captureSampleRate; }
};

class TestVoiceSpeakerBackend : public IVoiceSpeakerBackend
{
  public:
    void init() override
    {
        auto& state = VoiceState();
        ++state.speakerInitCalls;
        state.speakerInitialized = true;
    }

    void destroy() override
    {
        auto& state = VoiceState();
        ++state.speakerDestroyCalls;
        state.speakerInitialized = false;
    }

    void setChannel(VoNChatChannel channel) override { VoiceState().speakerChannel = channel; }
    void setPosition(float x, float y, float z) override { VoiceState().speakerPosition = {x, y, z}; }

    void stopStream() override
    {
        auto& state = VoiceState();
        ++state.speakerStopCalls;
        state.speakerFeedActive = false;
        state.speakerFeedLevel = 0.0f;
    }

    bool feed(VoNClient*, uint32_t) override
    {
        auto& state = VoiceState();
        ++state.speakerFeedCalls;
        return state.speakerFeedReturn;
    }

    bool isActive() const override { return VoiceState().speakerFeedActive; }

    float level() const override { return VoiceState().speakerFeedLevel; }
};

class TestVoiceLoopbackBackend : public IVoiceLoopbackBackend
{
  public:
    bool open(int) override
    {
        auto& state = VoiceState();
        ++state.loopbackOpenCalls;
        state.loopbackOpen = state.loopbackOpenReturn;
        return state.loopbackOpenReturn;
    }

    void close() override
    {
        auto& state = VoiceState();
        ++state.loopbackCloseCalls;
        state.loopbackOpen = false;
    }

    void tick(VoNCapture&) override { ++VoiceState().loopbackTickCalls; }

    bool isOpen() const override { return VoiceState().loopbackOpen; }
};

bool TestVoiceBackendAvailable()
{
    return true;
}

std::unique_ptr<IVoiceCaptureBackend> CreateTestCapture()
{
    return std::make_unique<TestVoiceCaptureBackend>();
}

std::unique_ptr<IVoiceSpeakerBackend> CreateTestSpeaker()
{
    return std::make_unique<TestVoiceSpeakerBackend>();
}

std::unique_ptr<IVoiceLoopbackBackend> CreateTestLoopback()
{
    return std::make_unique<TestVoiceLoopbackBackend>();
}

std::vector<std::string> ListTestDevices()
{
    return VoiceState().devices;
}

void EnsureTestVoiceBackend()
{
    static const bool registered = RegisterVoiceBackend({
        .codeName = "test-unit-voice",
        .priority = 1000,
        .isAvailable = &TestVoiceBackendAvailable,
        .createCapture = &CreateTestCapture,
        .createSpeaker = &CreateTestSpeaker,
        .createLoopback = &CreateTestLoopback,
        .listDevices = &ListTestDevices,
    });
    REQUIRE((registered || std::string(GetSelectedVoiceBackend().codeName) == "test-unit-voice"));
}
} // namespace

TEST_CASE("VoNCapture uses selected unit test backend", "[VoN][capture][unit]")
{
    EnsureTestVoiceBackend();
    ResetVoiceState();

    auto devices = VoNCapture::listDevices();
    REQUIRE(devices == std::vector<std::string>{"Fake Mic A", "Fake Mic B"});
    REQUIRE(std::string(GetSelectedVoiceBackend().codeName) == "test-unit-voice");
}

TEST_CASE("VoNCapture forwards lifecycle and sample state", "[VoN][capture][unit]")
{
    EnsureTestVoiceBackend();
    ResetVoiceState();
    VoiceState().captureAvailableSamples = 6;
    VoiceState().captureReadValue = -2000;

    VoNCapture cap;
    REQUIRE(cap.open("Fake Mic A", 16000, 320));
    REQUIRE(cap.isOpen());
    REQUIRE(cap.sampleRate() == 16000);
    REQUIRE(VoiceState().captureOpenCalls == 1);

    cap.start();
    REQUIRE(cap.isCapturing());
    REQUIRE(VoiceState().captureStartCalls == 1);
    REQUIRE(cap.availableSamples() == 6);

    int16_t buffer[8] = {};
    REQUIRE(cap.read(buffer, 4) == 4);
    REQUIRE(VoiceState().captureReadCalls == 1);
    REQUIRE(VoiceState().captureLastReadCount == 4);
    REQUIRE(buffer[0] == -2000);
    REQUIRE(buffer[3] == -2000);
    REQUIRE(cap.availableSamples() == 2);
    REQUIRE(cap.lastFramePeak() == Catch::Approx(2000.0f / 32767.0f));

    cap.stop();
    REQUIRE_FALSE(cap.isCapturing());
    REQUIRE(VoiceState().captureStopCalls == 1);

    cap.close();
    REQUIRE_FALSE(cap.isOpen());
    REQUIRE(VoiceState().captureCloseCalls == 1);
}

TEST_CASE("VoNCapture returns zero reads when backend open fails", "[VoN][capture][unit]")
{
    EnsureTestVoiceBackend();
    ResetVoiceState();
    VoiceState().captureOpenReturn = false;

    VoNCapture cap;
    REQUIRE_FALSE(cap.open(nullptr, 16000, 320));
    REQUIRE_FALSE(cap.isOpen());
    REQUIRE(cap.availableSamples() == 0);

    int16_t buffer[4] = {};
    REQUIRE(cap.read(buffer, 4) == 0);
    REQUIRE(cap.lastFramePeak() == 0.0f);
}

TEST_CASE("VoNClient drains stale capture on every push-to-talk press", "[VoN][capture][unit]")
{
    EnsureTestVoiceBackend();
    ResetVoiceState();

    VoNClient client;
    client.setCodecFactory([]() { return std::make_unique<PCMCodec>(16000); });
    client.setSenderId(77);

    std::vector<uint64_t> origins;
    client.setPacketSink(
        [&](const std::vector<uint8_t>& packet)
        {
            auto* pkt = reinterpret_cast<const VoNDataPacket*>(packet.data());
            origins.push_back(pkt->origin);
        });

    REQUIRE(client.openCapture(nullptr, 16000, 16000));

    VoiceState().captureAvailableSamples = 640;
    client.setTransmit(true);
    REQUIRE(VoiceState().captureStartCalls == 1);
    REQUIRE(VoiceState().captureStopCalls == 0);
    REQUIRE(VoiceState().captureAvailableSamples == 0);

    VoiceState().captureAvailableSamples = 320;
    client.update();
    REQUIRE(origins == std::vector<uint64_t>{0});

    // The device keeps capturing while PTT is released; the ring fills with
    // audio from outside the press.
    client.setTransmit(false);
    REQUIRE(VoiceState().captureStopCalls == 0);
    REQUIRE(VoiceState().captureCapturing);
    VoiceState().captureAvailableSamples = 640;

    // Next press must discard that backlog — only audio captured after the
    // press is transmitted, and the origin stays monotonic across bursts.
    client.setTransmit(true);
    REQUIRE(VoiceState().captureStartCalls == 1);
    REQUIRE(VoiceState().captureStopCalls == 0);
    REQUIRE(VoiceState().captureAvailableSamples == 0);

    VoiceState().captureAvailableSamples = 320;
    client.update();
    REQUIRE(origins == std::vector<uint64_t>{0, 320});
}

TEST_CASE("VoNClient transmit health reflects the sending pipeline", "[VoN][capture][unit]")
{
    EnsureTestVoiceBackend();
    ResetVoiceState();

    VoNClient client;
    client.setCodecFactory([]() { return std::make_unique<PCMCodec>(16000); });

    REQUIRE(client.transmitHealth() == VoNTransmitHealth::Off);

    SECTION("capture open failure reports NoCapture")
    {
        VoiceState().captureOpenReturn = false;
        REQUIRE_FALSE(client.openCapture(nullptr, 16000, 16000));
        client.setSenderId(77);
        client.setPacketSink([](const std::vector<uint8_t>&) {});
        client.setTransmit(true);
        REQUIRE(client.transmitHealth() == VoNTransmitHealth::NoCapture);
    }

    SECTION("missing sender identity reports NotConnected and sends nothing")
    {
        REQUIRE(client.openCapture(nullptr, 16000, 16000));
        int packets = 0;
        client.setPacketSink([&](const std::vector<uint8_t>&) { ++packets; });
        client.setTransmit(true);
        VoiceState().captureAvailableSamples = 640;
        client.update();
        REQUIRE(packets == 0);
        REQUIRE(client.transmitHealth() == VoNTransmitHealth::NotConnected);
    }

    SECTION("packets flowing is Transmitting; silence past the window is Stalled")
    {
        REQUIRE(client.openCapture(nullptr, 16000, 16000));
        client.setSenderId(77);
        int packets = 0;
        client.setPacketSink([&](const std::vector<uint8_t>&) { ++packets; });
        client.setTransmit(true);

        // Grace right after the press, before the first packet.
        const auto start = std::chrono::steady_clock::now();
        REQUIRE(client.transmitHealthAt(start + std::chrono::milliseconds(100)) == VoNTransmitHealth::Transmitting);
        REQUIRE(client.transmitHealthAt(start + std::chrono::milliseconds(900)) == VoNTransmitHealth::Stalled);

        VoiceState().captureAvailableSamples = 320;
        client.update();
        REQUIRE(packets == 1);

        // A packet just left; the capture then goes silent — healthy inside
        // the stall window, Stalled once it elapses.
        const auto sent = std::chrono::steady_clock::now();
        REQUIRE(client.transmitHealthAt(sent + std::chrono::milliseconds(100)) == VoNTransmitHealth::Transmitting);
        REQUIRE(client.transmitHealthAt(sent + std::chrono::milliseconds(900)) == VoNTransmitHealth::Stalled);

        client.setTransmit(false);
        REQUIRE(client.transmitHealth() == VoNTransmitHealth::Off);
    }
}

TEST_CASE("VoNSpeaker syncs state from backend", "[VoN][speaker][unit]")
{
    EnsureTestVoiceBackend();
    ResetVoiceState();
    VoiceState().speakerFeedLevel = 0.75f;
    VoiceState().speakerFeedActive = true;
    VoiceState().speakerFeedReturn = true;

    VoNSpeaker speaker;
    REQUIRE_FALSE(speaker.active);
    REQUIRE(speaker.level == 0.0f);

    speaker.init();
    REQUIRE(VoiceState().speakerInitCalls == 1);
    REQUIRE(speaker.active);
    REQUIRE(speaker.level == Catch::Approx(0.75f));

    speaker.setChannel(VoNChatChannel::Direct);
    REQUIRE(VoiceState().speakerChannel == VoNChatChannel::Direct);

    speaker.setPosition(1.0f, 2.0f, 3.0f);
    REQUIRE(VoiceState().speakerPosition == std::array<float, 3>{1.0f, 2.0f, 3.0f});

    REQUIRE(speaker.feed(nullptr, 9));
    REQUIRE(VoiceState().speakerFeedCalls == 1);
    REQUIRE(speaker.active);
    REQUIRE(speaker.level == Catch::Approx(0.75f));

    speaker.stopStream();
    REQUIRE(VoiceState().speakerStopCalls == 1);
    REQUIRE_FALSE(speaker.active);
    REQUIRE(speaker.level == 0.0f);

    speaker.destroy();
    REQUIRE(VoiceState().speakerDestroyCalls == 1);
    REQUIRE_FALSE(speaker.active);
}

TEST_CASE("MicLoopback forwards lifecycle and tick", "[VoN][loopback][unit]")
{
    EnsureTestVoiceBackend();
    ResetVoiceState();

    VoNCapture capture;
    MicLoopback loopback;

    REQUIRE(loopback.open(16000));
    REQUIRE(loopback.isOpen());
    REQUIRE(VoiceState().loopbackOpenCalls == 1);

    loopback.tick(capture);
    REQUIRE(VoiceState().loopbackTickCalls == 1);

    loopback.close();
    REQUIRE_FALSE(loopback.isOpen());
    REQUIRE(VoiceState().loopbackCloseCalls == 1);
}

TEST_CASE("RegisterVoiceBackend rejects incomplete and duplicate descriptors", "[VoN][backend][unit]")
{
    EnsureTestVoiceBackend();
    ResetVoiceState();

    REQUIRE_FALSE(RegisterVoiceBackend({
        .codeName = "missing-factories",
        .priority = 5,
        .isAvailable = &TestVoiceBackendAvailable,
        .createCapture = nullptr,
        .createSpeaker = &CreateTestSpeaker,
        .createLoopback = &CreateTestLoopback,
        .listDevices = &ListTestDevices,
    }));

    REQUIRE_FALSE(RegisterVoiceBackend({
        .codeName = "test-unit-voice",
        .priority = 1001,
        .isAvailable = &TestVoiceBackendAvailable,
        .createCapture = &CreateTestCapture,
        .createSpeaker = &CreateTestSpeaker,
        .createLoopback = &CreateTestLoopback,
        .listDevices = &ListTestDevices,
    }));
}
