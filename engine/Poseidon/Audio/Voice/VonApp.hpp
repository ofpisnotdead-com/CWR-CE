#pragma once
// VoN application layer — recorder, replayer, client, server, system

#include <Poseidon/Audio/Voice/VonCodec.hpp>
#include <Poseidon/Audio/Voice/VonCapture.hpp>
#include <Poseidon/Audio/Voice/VonBuffer.hpp>
#include <Poseidon/Audio/Voice/VonTransport.hpp>
#include <Poseidon/Audio/Voice/VonNet.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Poseidon
{
class VoNRecorder
{
  public:
    explicit VoNRecorder(VoNCodec* codec);

    // Capture frameSamples from mic, encode, build packet.
    // Returns true if a packet is ready in outPacket.
    bool update(VoNCapture& mic, uint32_t senderId, VoNChatChannel ch, std::vector<uint8_t>& outPacket);

    bool isRecording() const { return _recording; }
    void setRecording(bool on) { _recording = on; }
    uint64_t totalSamples() const { return _origin; }

  private:
    VoNCodec* _codec;
    std::vector<int16_t> _capBuf;
    std::vector<uint8_t> _encBuf;
    uint64_t _origin = 0;
    bool _recording = false;
};

// Receive packet → jitter buffer → decode → PCM
class VoNReplayer
{
  public:
    explicit VoNReplayer(std::unique_ptr<VoNCodec> codec, int jbCapacity = 8);

    // Feed a received data packet
    bool pushPacket(const VoNDataPacket& pkt, const uint8_t* payload);

    // Pull decoded PCM. Returns samples written (frameSamples or 0).
    int pull(int16_t* out, int maxSamples);

    bool isPlaying() const { return _playing; }
    int buffered() const { return _jitter.buffered(); }
    void reset();

  private:
    std::unique_ptr<VoNCodec> _codec;
    VoNJitterBuffer _jitter;
    std::vector<int16_t> _decBuf;
    bool _playing = false;

    // Diagnostic trap state (VoN# log lines): stream identity + rate limits.
    uint32_t _dbgChannel = 0;
    uint32_t _rxSinceStart = 0;
    uint32_t _jbDrops = 0;
};

// Client-side VoN — one recorder + N replayers (one per remote speaker)
class VoNClient
{
  public:
    using PacketSink = std::function<void(const std::vector<uint8_t>&)>;

    VoNClient();

    // Set up codec factory for creating replayers
    void setCodecFactory(std::function<std::unique_ptr<VoNCodec>()> factory);

    void setSenderId(uint32_t id) { _senderId = id; }
    void setChatChannel(VoNChatChannel ch) { _chatChannel = ch; }
    void setPacketSink(PacketSink sink) { _sink = std::move(sink); }

    // Open/close mic capture
    bool openCapture(const char* device, int sampleRate, int bufferSamples);
    void closeCapture();

    // Start/stop recording (push-to-talk)
    void setTransmit(bool on);
    bool isTransmitting() const;

    // Transmitter-side pipeline health for the HUD and harness. The overload
    // taking `now` exists so tests can probe the stall window without sleeping.
    VoNTransmitHealth transmitHealth() const;
    VoNTransmitHealth transmitHealthAt(std::chrono::steady_clock::time_point now) const;

    // Feed incoming voice data packet
    void onDataPacket(const VoNDataPacket& pkt, const uint8_t* payload);

    // Main update — call every frame
    // Captures mic → encodes → sends via sink
    // Returns per-channel decoded PCM for playback
    void update();

    // Test hook: encode and send deterministic PCM frames through the normal
    // packet sink, bypassing physical capture hardware.
    int sendTestTone(int frames, int amplitude);

    // Pull decoded audio for a specific speaker. Returns 0 if no data.
    int pullSpeaker(uint32_t channel, int16_t* out, int maxSamples);

    // Channel management
    void createChannel(uint32_t channel, const std::vector<uint8_t>& codecInfo);
    void removeChannel(uint32_t channel);
    bool hasChannel(uint32_t channel) const;
    std::vector<uint32_t> activeChannels() const;

  private:
    std::function<std::unique_ptr<VoNCodec>()> _codecFactory;
    PacketSink _sink;

    VoNCapture _capture;
    std::unique_ptr<VoNRecorder> _recorder;
    std::unique_ptr<VoNCodec> _recCodec;

    std::mutex _replayerLock;
    std::unordered_map<uint32_t, std::unique_ptr<VoNReplayer>> _replayers;

    uint32_t _senderId = 0;
    VoNChatChannel _chatChannel = VoNChatChannel::Direct;
    uint64_t _testToneOrigin = 0;

    // No packet for this long while transmit is requested = Stalled.
    static constexpr std::chrono::milliseconds kTransmitStallWindow{500};
    bool _transmitRequested = false;
    std::chrono::steady_clock::time_point _transmitStartAt{};
    std::chrono::steady_clock::time_point _lastPacketAt{};

    // Diagnostic trap state (VoN# log lines).
    uint32_t _txBurstPackets = 0;
    bool _txBlockedLogged = false;
    uint32_t _muteDrops = 0;
};

// Server-side VoN — routes voice between clients
class VoNServer
{
  public:
    using ForwardFunc = std::function<void(uint32_t target, const void* data, int size)>;

    void setForwarder(ForwardFunc fn) { _forward = std::move(fn); }

    // Set which targets receive from sender on a chat channel
    void setRouting(uint32_t sender, VoNChatChannel ch, const std::vector<uint32_t>& targets);

    // Handle incoming voice data — forward to routed targets
    void onDataPacket(const void* raw, int rawSize);

    // Remove a player (disconnect)
    void removePlayer(uint32_t channel);

  private:
    struct Route
    {
        VoNChatChannel channel;
        std::vector<uint32_t> targets;
        uint32_t fwdCount = 0; // diagnostic (VoN# log lines)
    };
    std::mutex _routeLock;
    std::unordered_map<uint32_t, Route> _routes; // sender → route
    ForwardFunc _forward;

    // Buffer for voice packets that arrive before routing is established (race condition)
    static constexpr int MAX_PENDING = 50; // ~1 second at 20ms/frame
    struct PendingPacket
    {
        std::vector<uint8_t> data;
    };
    std::unordered_map<uint32_t, std::vector<PendingPacket>> _pending; // channel → buffered packets

    void flushPending(uint32_t sender, const Route& route);
};

// Top-level VoN system — owns client and/or server
class VoNSystem
{
  public:
    VoNSystem() = default;

    // Initialize as client, server, or both (listen server)
    void initClient();
    void initServer();
    void shutdown();

    VoNClient* client() { return _client.get(); }
    VoNServer* server() { return _server.get(); }

    bool hasClient() const { return _client != nullptr; }
    bool hasServer() const { return _server != nullptr; }

  private:
    std::unique_ptr<VoNClient> _client;
    std::unique_ptr<VoNServer> _server;
};

// Harness hook: set by test infrastructure in GameApplication; null by default.
extern std::function<void(uint32_t, int)> gVonReceiveCallback;

} // namespace Poseidon
