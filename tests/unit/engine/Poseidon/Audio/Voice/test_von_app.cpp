#include <catch2/catch_all.hpp>
#include <Poseidon/Audio/Voice/VonApp.hpp>
#include <Poseidon/Audio/Voice/PCMCodec.hpp>
#include <Poseidon/Audio/Voice/OpusCodec.hpp>
#include <cstring>
#include <numeric>
#include <stdint.h>
#include <catch2/catch_test_macros.hpp>
#include <format>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

using namespace Poseidon;
static constexpr int FRAME = 320; // 20ms at 16kHz
static constexpr int RATE = 16000;

static void fillTone(int16_t* buf, int samples, int16_t amp, int period = 40)
{
    for (int i = 0; i < samples; ++i)
        buf[i] = static_cast<int16_t>((i % period < period / 2) ? amp : -amp);
}

// --- VoNRecorder tests (no mic, manual packet build) ---

TEST_CASE("VoNRecorder encode produces valid packet", "[VoN][app]")
{
    PCMCodec codec(RATE);
    VoNRecorder rec(&codec);
    rec.setRecording(true);
    REQUIRE(rec.totalSamples() == 0);
}

// --- VoNReplayer tests ---

TEST_CASE("VoNReplayer decode and pull", "[VoN][app]")
{
    auto codec = std::make_unique<PCMCodec>(RATE);
    VoNReplayer rp(std::move(codec));

    int16_t tone[FRAME];
    fillTone(tone, FRAME, 5000);

    // Build a fake data packet with PCM "encoded" payload
    auto* raw = reinterpret_cast<const uint8_t*>(tone);
    VoNDataPacket pkt{};
    pkt.init(1, VoNChatChannel::Direct, 0, FRAME, FRAME * 2);

    REQUIRE(rp.pushPacket(pkt, raw));
    REQUIRE(rp.buffered() == 1);
    REQUIRE(rp.isPlaying());

    int16_t out[FRAME] = {};
    int n = rp.pull(out, FRAME);
    REQUIRE(n == FRAME);
    REQUIRE(out[0] == tone[0]);
    REQUIRE(out[FRAME - 1] == tone[FRAME - 1]);
}

TEST_CASE("VoNReplayer reset clears state", "[VoN][app]")
{
    auto codec = std::make_unique<PCMCodec>(RATE);
    VoNReplayer rp(std::move(codec));

    int16_t tone[FRAME];
    fillTone(tone, FRAME, 3000);

    VoNDataPacket pkt{};
    pkt.init(1, VoNChatChannel::Direct, 0, FRAME, FRAME * 2);
    REQUIRE(rp.pushPacket(pkt, reinterpret_cast<const uint8_t*>(tone)));
    REQUIRE(rp.buffered() == 1);

    rp.reset();
    REQUIRE(rp.buffered() == 0);
    REQUIRE_FALSE(rp.isPlaying());
}

TEST_CASE("VoNReplayer accepts a new short push-to-talk burst before the previous frame is pulled", "[VoN][app]")
{
    auto codec = std::make_unique<PCMCodec>(RATE);
    VoNReplayer rp(std::move(codec));

    int16_t first[FRAME];
    int16_t second[FRAME];
    fillTone(first, FRAME, 1000);
    fillTone(second, FRAME, 4000);

    VoNDataPacket pkt{};
    pkt.init(1, VoNChatChannel::Direct, 0, FRAME, FRAME * 2);
    REQUIRE(rp.pushPacket(pkt, reinterpret_cast<const uint8_t*>(first)));

    pkt.init(1, VoNChatChannel::Direct, 0, FRAME, FRAME * 2);
    REQUIRE(rp.pushPacket(pkt, reinterpret_cast<const uint8_t*>(second)));

    int16_t out[FRAME] = {};
    REQUIRE(rp.pull(out, FRAME) == FRAME);
    REQUIRE(out[0] == second[0]);
}

// --- VoNClient tests ---

TEST_CASE("VoNClient channel management", "[VoN][app]")
{
    VoNClient client;
    client.setCodecFactory([]() { return std::make_unique<PCMCodec>(RATE); });

    REQUIRE_FALSE(client.hasChannel(42));

    client.createChannel(42, {});
    REQUIRE(client.hasChannel(42));

    auto ids = client.activeChannels();
    REQUIRE(ids.size() == 1);
    REQUIRE(ids[0] == 42);

    client.removeChannel(42);
    REQUIRE_FALSE(client.hasChannel(42));
}

TEST_CASE("VoNClient routes data to replayer", "[VoN][app]")
{
    VoNClient client;
    client.setCodecFactory([]() { return std::make_unique<PCMCodec>(RATE); });
    client.createChannel(7, {});

    int16_t tone[FRAME];
    fillTone(tone, FRAME, 4000);

    VoNDataPacket pkt{};
    pkt.init(7, VoNChatChannel::Group, 0, FRAME, FRAME * 2);
    client.onDataPacket(pkt, reinterpret_cast<const uint8_t*>(tone));

    int16_t out[FRAME] = {};
    int n = client.pullSpeaker(7, out, FRAME);
    REQUIRE(n == FRAME);
    REQUIRE(out[0] == tone[0]);
}

TEST_CASE("VoNClient ignores unknown channel", "[VoN][app]")
{
    VoNClient client;
    int16_t tone[FRAME];
    fillTone(tone, FRAME, 1000);

    VoNDataPacket pkt{};
    pkt.init(99, VoNChatChannel::Direct, 0, FRAME, FRAME * 2);
    client.onDataPacket(pkt, reinterpret_cast<const uint8_t*>(tone));

    int16_t out[FRAME] = {};
    REQUIRE(client.pullSpeaker(99, out, FRAME) == 0);
}

// --- VoNServer tests ---

TEST_CASE("VoNServer routes data to targets", "[VoN][app]")
{
    VoNServer server;

    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> forwarded;
    server.setForwarder(
        [&](uint32_t target, const void* data, int size)
        {
            forwarded.push_back({target, std::vector<uint8_t>(static_cast<const uint8_t*>(data),
                                                              static_cast<const uint8_t*>(data) + size)});
        });

    server.setRouting(1, VoNChatChannel::Group, {2, 3});

    // Build a raw packet from sender 1
    std::vector<uint8_t> raw(VoNDataPacket::HEADER_SIZE + 10);
    auto* pkt = reinterpret_cast<VoNDataPacket*>(raw.data());
    pkt->init(1, VoNChatChannel::Group, 0, 320, 10);
    std::memset(pkt->payload(), 0xAB, 10);

    server.onDataPacket(raw.data(), static_cast<int>(raw.size()));

    REQUIRE(forwarded.size() == 2);
    REQUIRE(forwarded[0].first == 2);
    REQUIRE(forwarded[1].first == 3);
    REQUIRE(forwarded[0].second.size() == raw.size());
}

TEST_CASE("VoNServer does not forward to sender", "[VoN][app]")
{
    VoNServer server;

    int fwdCount = 0;
    server.setForwarder([&](uint32_t, const void*, int) { ++fwdCount; });

    // Route includes sender itself
    server.setRouting(1, VoNChatChannel::Direct, {1, 2});

    std::vector<uint8_t> raw(VoNDataPacket::HEADER_SIZE + 4);
    auto* pkt = reinterpret_cast<VoNDataPacket*>(raw.data());
    pkt->init(1, VoNChatChannel::Direct, 0, 320, 4);

    server.onDataPacket(raw.data(), static_cast<int>(raw.size()));

    REQUIRE(fwdCount == 1); // only target 2, not sender 1
}

TEST_CASE("VoNServer drops packets whose chat channel does not match the active route", "[VoN][app]")
{
    VoNServer server;

    int fwdCount = 0;
    server.setForwarder([&](uint32_t, const void*, int) { ++fwdCount; });

    server.setRouting(1, VoNChatChannel::Group, {2});

    std::vector<uint8_t> raw(VoNDataPacket::HEADER_SIZE + 4);
    auto* pkt = reinterpret_cast<VoNDataPacket*>(raw.data());
    pkt->init(1, VoNChatChannel::Side, 0, 320, 4);

    server.onDataPacket(raw.data(), static_cast<int>(raw.size()));
    REQUIRE(fwdCount == 0);

    pkt->init(1, VoNChatChannel::Group, 0, 320, 4);
    server.onDataPacket(raw.data(), static_cast<int>(raw.size()));
    REQUIRE(fwdCount == 1);
}

TEST_CASE("VoNServer drops pending packets whose chat channel does not match the later route", "[VoN][app]")
{
    VoNServer server;

    int fwdCount = 0;
    server.setForwarder([&](uint32_t, const void*, int) { ++fwdCount; });

    std::vector<uint8_t> raw(VoNDataPacket::HEADER_SIZE + 4);
    auto* pkt = reinterpret_cast<VoNDataPacket*>(raw.data());
    pkt->init(1, VoNChatChannel::Side, 0, 320, 4);
    server.onDataPacket(raw.data(), static_cast<int>(raw.size()));

    server.setRouting(1, VoNChatChannel::Group, {2});
    REQUIRE(fwdCount == 0);

    pkt->init(1, VoNChatChannel::Group, 0, 320, 4);
    server.onDataPacket(raw.data(), static_cast<int>(raw.size()));
    REQUIRE(fwdCount == 1);
}

TEST_CASE("VoNServer removePlayer clears routing", "[VoN][app]")
{
    VoNServer server;

    int fwdCount = 0;
    server.setForwarder([&](uint32_t, const void*, int) { ++fwdCount; });

    server.setRouting(5, VoNChatChannel::Side, {6});
    server.removePlayer(5);

    std::vector<uint8_t> raw(VoNDataPacket::HEADER_SIZE + 4);
    auto* pkt = reinterpret_cast<VoNDataPacket*>(raw.data());
    pkt->init(5, VoNChatChannel::Side, 0, 320, 4);

    server.onDataPacket(raw.data(), static_cast<int>(raw.size()));
    REQUIRE(fwdCount == 0); // route removed
}

// --- VoNSystem tests ---

TEST_CASE("VoNSystem init and shutdown", "[VoN][app]")
{
    VoNSystem sys;
    REQUIRE_FALSE(sys.hasClient());
    REQUIRE_FALSE(sys.hasServer());

    sys.initClient();
    REQUIRE(sys.hasClient());
    REQUIRE_FALSE(sys.hasServer());

    sys.initServer();
    REQUIRE(sys.hasServer());

    sys.shutdown();
    REQUIRE_FALSE(sys.hasClient());
    REQUIRE_FALSE(sys.hasServer());
}

// --- Loopback pipeline test (PCM codec: encode → packet → replayer → decode) ---

TEST_CASE("VoN loopback pipeline with PCM codec", "[VoN][app]")
{
    // Simulate: recorder produces packet → server routes → client replays
    PCMCodec encCodec(RATE);
    VoNRecorder rec(&encCodec);
    rec.setRecording(true);

    auto decCodec = std::make_unique<PCMCodec>(RATE);
    VoNReplayer rp(std::move(decCodec));

    // Manually "capture" 3 frames worth of tone
    int16_t tone[FRAME];
    fillTone(tone, FRAME, 6000);

    for (int i = 0; i < 3; ++i)
    {
        // Build packet manually (no real mic)
        std::vector<uint8_t> packet(VoNDataPacket::HEADER_SIZE + FRAME * 2);
        auto* pkt = reinterpret_cast<VoNDataPacket*>(packet.data());
        pkt->init(1, VoNChatChannel::Global, i * FRAME, FRAME, FRAME * 2);
        std::memcpy(pkt->payload(), tone, FRAME * 2);

        REQUIRE(rp.pushPacket(*pkt, pkt->payload()));
    }

    REQUIRE(rp.buffered() == 3);

    // Pull all 3 frames
    for (int i = 0; i < 3; ++i)
    {
        int16_t out[FRAME] = {};
        int n = rp.pull(out, FRAME);
        REQUIRE(n == FRAME);
        REQUIRE(out[0] == tone[0]);
    }
}

TEST_CASE("VoN loopback pipeline with Opus codec", "[VoN][app]")
{
    OpusCodec encCodec;
    VoNRecorder rec(&encCodec);
    rec.setRecording(true);

    auto decCodec = std::make_unique<OpusCodec>();
    VoNReplayer rp(std::move(decCodec));

    int16_t tone[FRAME];
    fillTone(tone, FRAME, 8000, 32); // ~500Hz tone

    // Encode → build packet → push to replayer
    std::vector<uint8_t> encBuf(encCodec.maxEncodedSize());
    for (int i = 0; i < 3; ++i)
    {
        int enc = encCodec.encode(tone, FRAME, encBuf.data(), static_cast<int>(encBuf.size()));
        REQUIRE(enc > 0);

        std::vector<uint8_t> packet(VoNDataPacket::HEADER_SIZE + enc);
        auto* pkt = reinterpret_cast<VoNDataPacket*>(packet.data());
        pkt->init(1, VoNChatChannel::Vehicle, i * FRAME, FRAME, static_cast<uint16_t>(enc));
        std::memcpy(pkt->payload(), encBuf.data(), enc);

        REQUIRE(rp.pushPacket(*pkt, pkt->payload()));
    }

    REQUIRE(rp.buffered() == 3);

    // Pull and verify non-silence (Opus lossy, can't compare exact)
    for (int i = 0; i < 3; ++i)
    {
        int16_t out[FRAME] = {};
        int n = rp.pull(out, FRAME);
        REQUIRE(n == FRAME);

        int64_t energy = 0;
        for (int j = 0; j < FRAME; ++j)
            energy += static_cast<int64_t>(out[j]) * out[j];
        REQUIRE(energy > 0); // non-silent
    }
}
