#include <Poseidon/Audio/Voice/VonApp.hpp>
#include <Poseidon/Core/PlayerMuteIgnore.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Poseidon
{

// Default null; game sets this in GameApplication.cpp when harness is active.
std::function<void(uint32_t, int)> gVonReceiveCallback;

// --- VoNRecorder ---

VoNRecorder::VoNRecorder(VoNCodec* codec) : _codec(codec)
{
    _capBuf.resize(codec->frameSize());
    _encBuf.resize(codec->maxEncodedSize());
}

bool VoNRecorder::update(VoNCapture& mic, uint32_t senderId, VoNChatChannel ch, std::vector<uint8_t>& outPacket)
{
    if (!_recording || !mic.isCapturing())
        return false;

    int avail = mic.availableSamples();
    if (avail < _codec->frameSize())
        return false;

    int got = mic.read(_capBuf.data(), _codec->frameSize());
    if (got != _codec->frameSize())
        return false;

    int enc = _codec->encode(_capBuf.data(), got, _encBuf.data(), static_cast<int>(_encBuf.size()));
    if (enc <= 0)
    {
        LOG_WARN(Audio, "VoN: encode failed (got={} samples)", got);
        return false;
    }

    // Build VoNDataPacket
    outPacket.resize(VoNDataPacket::HEADER_SIZE + enc);
    auto* pkt = reinterpret_cast<VoNDataPacket*>(outPacket.data());
    pkt->init(senderId, ch, _origin, static_cast<uint16_t>(got), static_cast<uint16_t>(enc));
    std::memcpy(pkt->payload(), _encBuf.data(), enc);

    _origin += got;
    LOG_TRACE(Audio, "VoN: encoded frame origin={} samples={} bytes={}", _origin - got, got, enc);
    return true;
}

// --- VoNReplayer ---

VoNReplayer::VoNReplayer(std::unique_ptr<VoNCodec> codec, int jbCapacity)
    : _codec(std::move(codec)), _jitter(jbCapacity, _codec->frameSize())
{
    _decBuf.resize(_codec->frameSize());
}

void VoNReplayer::pushPacket(const VoNDataPacket& pkt, const uint8_t* payload)
{
    int dec = _codec->decode(payload, pkt.size, _decBuf.data(), static_cast<int>(_decBuf.size()));
    if (dec <= 0)
    {
        LOG_WARN(Audio, "VoN: decode failed (pkt size={} from ch={})", pkt.size, pkt.channel);
        return;
    }
    // Detect new transmission: origin resets to 0 while playback has progressed
    // well past the initial frame. This avoids spurious resets from retransmitted
    // first packets (which also have origin=0).
    if (pkt.origin == 0 && _jitter.started() && _jitter.nextOrigin() > static_cast<uint64_t>(_codec->frameSize()))
    {
        _jitter.reset();
        LOG_DEBUG(Audio, "VoN: replayer reset for new transmission ch={}", pkt.channel);
    }
    _jitter.push(pkt.origin, _decBuf.data(), dec);
    _playing = true;
    LOG_TRACE(Audio, "VoN: decoded frame ch={} origin={} dec={}", pkt.channel, pkt.origin, dec);
}

int VoNReplayer::pull(int16_t* out, int maxSamples)
{
    int n = _jitter.pull(out, maxSamples);
    if (n == 0 && _jitter.empty())
        _playing = false;
    return n;
}

void VoNReplayer::reset()
{
    _jitter.reset();
    _playing = false;
}

// --- VoNClient ---

VoNClient::VoNClient() = default;

void VoNClient::setCodecFactory(std::function<std::unique_ptr<VoNCodec>()> factory)
{
    _codecFactory = std::move(factory);
}

bool VoNClient::openCapture(const char* device, int sampleRate, int bufferSamples)
{
    if (!_capture.open(device, sampleRate, bufferSamples))
    {
        LOG_ERROR(Audio, "VoN: capture open failed (device={} rate={} buf={})", device ? device : "default", sampleRate,
                  bufferSamples);
        return false;
    }
    if (_codecFactory)
    {
        _recCodec = _codecFactory();
        _recorder = std::make_unique<VoNRecorder>(_recCodec.get());
    }
    LOG_DEBUG(Audio, "VoN: capture opened (rate={} buf={}) recorder={}", sampleRate, bufferSamples,
              _recorder != nullptr);
    return true;
}

void VoNClient::closeCapture()
{
    _recorder.reset();
    _recCodec.reset();
    _capture.close();
}

void VoNClient::setTransmit(bool on)
{
    if (on && !_capture.isCapturing())
        _capture.start();
    if (_recorder)
        _recorder->setRecording(on);
    if (!on && _capture.isCapturing())
        _capture.stop();
    LOG_DEBUG(Audio, "VoN: transmit {}", on ? "ON" : "OFF");
}

bool VoNClient::isTransmitting() const
{
    return _recorder && _recorder->isRecording();
}

void VoNClient::onDataPacket(const VoNDataPacket& pkt, const uint8_t* payload)
{
    // Client-local mute: drop a muted sender's voice before it is decoded,
    // queued, or reported to the harness (so it neither plays nor counts).
    if (IsVoiceMuted(static_cast<int>(pkt.channel)))
        return;

    std::lock_guard<std::mutex> lk(_replayerLock);
    auto it = _replayers.find(pkt.channel);
    if (it == _replayers.end())
    {
        // Auto-create replayer on first packet from this sender
        if (!_codecFactory)
            return;
        _replayers[pkt.channel] = std::make_unique<VoNReplayer>(_codecFactory());
        it = _replayers.find(pkt.channel);
        LOG_DEBUG(Audio, "VoN: auto-created replayer for channel {}", pkt.channel);
    }
    it->second->pushPacket(pkt, payload);

    // Notify harness (if active) about received VoN data
    if (gVonReceiveCallback)
        gVonReceiveCallback(pkt.channel, 1);
}

void VoNClient::update()
{
    if (!_recorder || !_recorder->isRecording())
        return;

    std::vector<uint8_t> packet;
    while (_recorder->update(_capture, _senderId, _chatChannel, packet))
    {
        if (_sink)
            _sink(packet);
    }
}

int VoNClient::sendTestTone(int frames, int amplitude)
{
    if (!_codecFactory || !_sink || _senderId == 0 || frames <= 0)
        return 0;

    auto codec = _codecFactory();
    if (!codec)
        return 0;

    const int frameSamples = codec->frameSize();
    std::vector<int16_t> pcm(frameSamples);
    std::vector<uint8_t> enc(codec->maxEncodedSize());
    std::vector<uint8_t> packet;
    const int clampedAmplitude = std::clamp(amplitude, 1, 30000);
    int sent = 0;

    for (int frame = 0; frame < frames; ++frame)
    {
        for (int i = 0; i < frameSamples; ++i)
        {
            const double phase = static_cast<double>(_testToneOrigin + i) * 2.0 * 3.14159265358979323846 * 440.0 /
                                 static_cast<double>(codec->sampleRate());
            pcm[i] = static_cast<int16_t>(std::sin(phase) * clampedAmplitude);
        }

        const int encSize = codec->encode(pcm.data(), frameSamples, enc.data(), static_cast<int>(enc.size()));
        if (encSize <= 0)
            break;

        packet.resize(VoNDataPacket::HEADER_SIZE + encSize);
        auto* pkt = reinterpret_cast<VoNDataPacket*>(packet.data());
        pkt->init(_senderId, _chatChannel, _testToneOrigin, static_cast<uint16_t>(frameSamples),
                  static_cast<uint16_t>(encSize));
        std::memcpy(pkt->payload(), enc.data(), encSize);
        _sink(packet);

        _testToneOrigin += frameSamples;
        ++sent;
    }

    LOG_DEBUG(Audio, "VoN: sent {} synthetic test frames (player={})", sent, _senderId);
    return sent;
}

int VoNClient::pullSpeaker(uint32_t channel, int16_t* out, int maxSamples)
{
    std::lock_guard<std::mutex> lk(_replayerLock);
    auto it = _replayers.find(channel);
    if (it == _replayers.end())
        return 0;
    return it->second->pull(out, maxSamples);
}

void VoNClient::createChannel(uint32_t channel, const std::vector<uint8_t>& /*codecInfo*/)
{
    std::lock_guard<std::mutex> lk(_replayerLock);
    if (_replayers.count(channel))
        return;
    if (!_codecFactory)
        return;
    _replayers[channel] = std::make_unique<VoNReplayer>(_codecFactory());
    LOG_DEBUG(Audio, "VoN: channel {} created", channel);
}

void VoNClient::removeChannel(uint32_t channel)
{
    std::lock_guard<std::mutex> lk(_replayerLock);
    _replayers.erase(channel);
    LOG_DEBUG(Audio, "VoN: channel {} removed", channel);
}

bool VoNClient::hasChannel(uint32_t channel) const
{
    return _replayers.count(channel) > 0;
}

std::vector<uint32_t> VoNClient::activeChannels() const
{
    std::vector<uint32_t> ids;
    ids.reserve(_replayers.size());
    for (auto& [id, _] : _replayers)
        ids.push_back(id);
    return ids;
}

// --- VoNServer ---

void VoNServer::setRouting(uint32_t sender, VoNChatChannel ch, const std::vector<uint32_t>& targets)
{
    std::lock_guard<std::mutex> lk(_routeLock);
    if (targets.empty())
    {
        // Remove route so that packets arriving before the next
        // SetVoiceChannel are buffered (pending) instead of silently
        // routed to an empty target list.
        _routes.erase(sender);
        LOG_DEBUG(Audio, "VoN srv: route removed sender={} ch={}", sender, static_cast<int>(ch));
        return;
    }
    _routes[sender] = {ch, targets};
    LOG_DEBUG(Audio, "VoN srv: routing sender={} ch={} targets={}", sender, static_cast<int>(ch), targets.size());

    // Flush any packets that arrived before routing was established
    flushPending(sender, _routes[sender]);
}

void VoNServer::onDataPacket(const void* raw, int rawSize)
{
    if (rawSize < VoNDataPacket::HEADER_SIZE)
    {
        LOG_WARN(Audio, "VoN srv: packet too small ({}B)", rawSize);
        return;
    }

    VoNDataPacket pkt;
    std::memcpy(&pkt, raw, VoNDataPacket::HEADER_SIZE);
    if (!pkt.isValid())
    {
        LOG_WARN(Audio, "VoN srv: invalid packet magic");
        return;
    }

    std::lock_guard<std::mutex> lk(_routeLock);
    auto it = _routes.find(pkt.channel);
    if (it == _routes.end())
    {
        // Buffer packet until routing is established (race condition fix)
        auto& pending = _pending[pkt.channel];
        if (static_cast<int>(pending.size()) < MAX_PENDING)
        {
            pending.push_back({std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(raw),
                                                    reinterpret_cast<const uint8_t*>(raw) + rawSize)});
            LOG_TRACE(Audio, "VoN srv: buffered pkt for ch={} (pending={})", pkt.channel, pending.size());
        }
        return;
    }

    if (it->second.channel != pkt.chatChan)
    {
        LOG_DEBUG(Audio, "VoN srv: dropping pkt for sender={} ch={} routedCh={}", pkt.channel,
                  static_cast<int>(pkt.chatChan), static_cast<int>(it->second.channel));
        return;
    }

    LOG_TRACE(Audio, "VoN srv: routing ch={} to {} targets ({}B)", pkt.channel, it->second.targets.size(), rawSize);
    for (uint32_t target : it->second.targets)
    {
        if (target != pkt.channel && _forward)
            _forward(target, raw, rawSize);
    }
}

void VoNServer::removePlayer(uint32_t channel)
{
    std::lock_guard<std::mutex> lk(_routeLock);
    _routes.erase(channel);
    _pending.erase(channel);
    LOG_DEBUG(Audio, "VoN srv: removed player ch={}", channel);
}

void VoNServer::flushPending(uint32_t sender, const Route& route)
{
    auto pit = _pending.find(sender);
    if (pit == _pending.end() || pit->second.empty())
        return;

    LOG_DEBUG(Audio, "VoN srv: flushing {} pending pkts for ch={}", pit->second.size(), sender);
    for (auto& pp : pit->second)
    {
        if (static_cast<int>(pp.data.size()) < VoNDataPacket::HEADER_SIZE)
            continue;

        VoNDataPacket pkt;
        std::memcpy(&pkt, pp.data.data(), VoNDataPacket::HEADER_SIZE);
        if (!pkt.isValid() || pkt.chatChan != route.channel)
        {
            LOG_DEBUG(Audio, "VoN srv: dropping pending pkt for sender={} ch={} routedCh={}", sender,
                      static_cast<int>(pkt.chatChan), static_cast<int>(route.channel));
            continue;
        }

        for (uint32_t target : route.targets)
        {
            if (target != sender && _forward)
                _forward(target, pp.data.data(), static_cast<int>(pp.data.size()));
        }
    }
    _pending.erase(pit);
}

// --- VoNSystem ---

void VoNSystem::initClient()
{
    _client = std::make_unique<VoNClient>();
    LOG_DEBUG(Audio, "VoN: system client created");
}
void VoNSystem::initServer()
{
    _server = std::make_unique<VoNServer>();
    LOG_DEBUG(Audio, "VoN: system server created");
}

void VoNSystem::shutdown()
{
    if (_client)
        _client->closeCapture();
    _client.reset();
    _server.reset();
    LOG_DEBUG(Audio, "VoN: system shutdown");
}

} // namespace Poseidon
