#include <Poseidon/Audio/Voice/VonApp.hpp>
#include <Poseidon/Core/PlayerMuteIgnore.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace Poseidon
{
namespace
{
int DrainCapture(VoNCapture& capture)
{
    if (!capture.isCapturing())
        return 0;

    int drained = 0;
    std::vector<int16_t> discard(1024);
    while (capture.availableSamples() > 0)
    {
        int got = capture.read(discard.data(), static_cast<int>(discard.size()));
        if (got <= 0)
            break;
        drained += got;
    }
    return drained;
}

// Diagnostic rate limit for per-packet VoN# traps: log the first few packets
// of every stream/burst, then sample.
bool TrapSample(uint32_t n)
{
    return n <= 3 || n % 50 == 0;
}
} // namespace

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

bool VoNReplayer::pushPacket(const VoNDataPacket& pkt, const uint8_t* payload)
{
    _dbgChannel = pkt.channel;
    int dec = _codec->decode(payload, pkt.size, _decBuf.data(), static_cast<int>(_decBuf.size()));
    if (dec <= 0)
    {
        LOG_WARN(Audio, "VoN# rx decode-fail ch={} bytes={}", pkt.channel, pkt.size);
        return false;
    }
    if (pkt.origin == 0 && _jitter.started())
    {
        _jitter.reset();
        LOG_DEBUG(Audio, "VoN# rx jb origin-reset ch={}", pkt.channel);
    }
    const VoNJitterPush res = _jitter.push(pkt.origin, _decBuf.data(), dec);
    if (res == VoNJitterPush::Resync)
    {
        LOG_DEBUG(Audio, "VoN# rx jb RESYNC ch={} origin={} cursor-was-behind", pkt.channel, pkt.origin);
    }
    else if (res != VoNJitterPush::Accepted)
    {
        ++_jbDrops;
        if (TrapSample(_jbDrops))
            LOG_WARN(Audio, "VoN# rx jb DROP({}) ch={} origin={} cursor={} buffered={} drops={}", static_cast<int>(res),
                     pkt.channel, pkt.origin, _jitter.nextOrigin(), _jitter.buffered(), _jbDrops);
    }
    if (!_playing)
        LOG_TRACE(Audio, "VoN# rx stream-start ch={} origin={} jb={}", pkt.channel, pkt.origin, _jitter.buffered());
    _playing = true;
    ++_rxSinceStart;
    if (TrapSample(_rxSinceStart))
        LOG_TRACE(Audio, "VoN# rx pkt ch={} n={} origin={} dec={} jb={}", pkt.channel, _rxSinceStart, pkt.origin, dec,
                  _jitter.buffered());
    return true;
}

int VoNReplayer::pull(int16_t* out, int maxSamples)
{
    int n = _jitter.pull(out, maxSamples);
    if (n == 0 && _jitter.empty() && _playing)
    {
        _playing = false;
        LOG_TRACE(Audio, "VoN# rx stream-dry ch={} rxPkts={} underruns={}", _dbgChannel, _rxSinceStart,
                  _jitter.underrunGapFrames());
        _rxSinceStart = 0;
    }
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
    {
        _capture.start();
    }
    int drained = 0;
    if (on)
    {
        // The device keeps capturing between bursts (OpenAL stop/start
        // cycles are unreliable), so the ring accumulates while PTT is
        // released. Drain it on every press: the backlog is audio from
        // outside the press — sending it both violates push-to-talk and
        // floods receivers past their jitter window in a single update.
        drained = DrainCapture(_capture);
    }
    if (on && !_transmitRequested)
    {
        _transmitStartAt = std::chrono::steady_clock::now();
        _txBurstPackets = 0;
        _txBlockedLogged = false;
    }
    _transmitRequested = on;
    if (_recorder)
        _recorder->setRecording(on);
    if (on)
        LOG_DEBUG(Audio, "VoN# tx ON sender={} capOpen={} capturing={} drained={} origin={} sink={} rec={}", _senderId,
                  _capture.isOpen(), _capture.isCapturing(), drained, _recorder ? _recorder->totalSamples() : 0,
                  static_cast<bool>(_sink), _recorder != nullptr);
    else
        LOG_DEBUG(Audio, "VoN# tx OFF sender={} burstPkts={} origin={}", _senderId, _txBurstPackets,
                  _recorder ? _recorder->totalSamples() : 0);
}

bool VoNClient::isTransmitting() const
{
    return _recorder && _recorder->isRecording();
}

VoNTransmitHealth VoNClient::transmitHealth() const
{
    return transmitHealthAt(std::chrono::steady_clock::now());
}

VoNTransmitHealth VoNClient::transmitHealthAt(std::chrono::steady_clock::time_point now) const
{
    if (!_transmitRequested)
        return VoNTransmitHealth::Off;
    if (!_recorder || !_capture.isOpen())
        return VoNTransmitHealth::NoCapture;
    if (_senderId == 0 || !_sink)
        return VoNTransmitHealth::NotConnected;
    // Grace from transmit start, then a packet must have left recently.
    const auto newest = std::max(_lastPacketAt, _transmitStartAt);
    if (now - newest > kTransmitStallWindow)
        return VoNTransmitHealth::Stalled;
    return VoNTransmitHealth::Transmitting;
}

void VoNClient::onDataPacket(const VoNDataPacket& pkt, const uint8_t* payload)
{
    // Client-local mute: drop a muted sender's voice before it is decoded,
    // queued, or reported to the harness (so it neither plays nor counts).
    if (IsVoiceMuted(static_cast<int>(pkt.channel)))
    {
        ++_muteDrops;
        if (TrapSample(_muteDrops))
            LOG_DEBUG(Audio, "VoN# rx MUTED ch={} drops={}", pkt.channel, _muteDrops);
        return;
    }

    std::lock_guard<std::mutex> lk(_replayerLock);
    auto it = _replayers.find(pkt.channel);
    if (it == _replayers.end())
    {
        // Auto-create replayer on first packet from this sender
        if (!_codecFactory)
        {
            LOG_WARN(Audio, "VoN# rx NO-CODEC-FACTORY ch={}", pkt.channel);
            return;
        }
        _replayers[pkt.channel] = std::make_unique<VoNReplayer>(_codecFactory());
        it = _replayers.find(pkt.channel);
        LOG_DEBUG(Audio, "VoN# rx replayer-created ch={}", pkt.channel);
    }
    if (!it->second->pushPacket(pkt, payload))
        return;

    // Notify harness (if active) about received VoN data
    if (gVonReceiveCallback)
        gVonReceiveCallback(pkt.channel, 1);
}

void VoNClient::update()
{
    if (!_recorder || !_recorder->isRecording())
        return;

    // Without an assigned sender id the packets would be stamped with
    // channel 0 and the server could never route them — don't send junk;
    // transmitHealth() reports NotConnected instead.
    if (_senderId == 0 || !_sink)
    {
        if (!_txBlockedLogged)
        {
            LOG_WARN(Audio, "VoN# tx BLOCKED sender={} sink={}", _senderId, static_cast<bool>(_sink));
            _txBlockedLogged = true;
        }
        return;
    }

    std::vector<uint8_t> packet;
    while (_recorder->update(_capture, _senderId, _chatChannel, packet))
    {
        _sink(packet);
        _lastPacketAt = std::chrono::steady_clock::now();
        ++_txBurstPackets;
        if (TrapSample(_txBurstPackets))
        {
            const auto* pkt = reinterpret_cast<const VoNDataPacket*>(packet.data());
            LOG_TRACE(Audio, "VoN# tx pkt sender={} n={} origin={} bytes={} ch={}", _senderId, _txBurstPackets,
                      pkt->origin, pkt->size, static_cast<int>(pkt->chatChan));
        }
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
        LOG_DEBUG(Audio, "VoN# srv route- sender={} ch={}", sender, static_cast<int>(ch));
        return;
    }
    _routes[sender] = {ch, targets};
    std::string targetList;
    for (uint32_t t : targets)
    {
        if (!targetList.empty())
            targetList += ',';
        targetList += std::to_string(t);
    }
    LOG_DEBUG(Audio, "VoN# srv route+ sender={} ch={} targets=[{}]", sender, static_cast<int>(ch), targetList);

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
            if (pending.size() == 1 || pending.size() % 25 == 0)
                LOG_DEBUG(Audio, "VoN# srv NO-ROUTE pend sender={} origin={} pending={}", pkt.channel, pkt.origin,
                          pending.size());
        }
        else
        {
            LOG_WARN(Audio, "VoN# srv NO-ROUTE overflow-drop sender={} origin={} (cap {})", pkt.channel, pkt.origin,
                     MAX_PENDING);
        }
        return;
    }

    if (it->second.channel != pkt.chatChan)
    {
        LOG_WARN(Audio, "VoN# srv CH-DROP sender={} pktCh={} routeCh={} origin={}", pkt.channel,
                 static_cast<int>(pkt.chatChan), static_cast<int>(it->second.channel), pkt.origin);
        return;
    }

    ++it->second.fwdCount;
    if (TrapSample(it->second.fwdCount))
        LOG_TRACE(Audio, "VoN# srv fwd sender={} n={} origin={} targets={} fwd-hook={}", pkt.channel,
                  it->second.fwdCount, pkt.origin, it->second.targets.size(), static_cast<bool>(_forward));
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

    int forwarded = 0;
    int dropped = 0;
    for (auto& pp : pit->second)
    {
        if (static_cast<int>(pp.data.size()) < VoNDataPacket::HEADER_SIZE)
            continue;

        VoNDataPacket pkt;
        std::memcpy(&pkt, pp.data.data(), VoNDataPacket::HEADER_SIZE);
        if (!pkt.isValid() || pkt.chatChan != route.channel)
        {
            ++dropped;
            continue;
        }

        ++forwarded;
        for (uint32_t target : route.targets)
        {
            if (target != sender && _forward)
                _forward(target, pp.data.data(), static_cast<int>(pp.data.size()));
        }
    }
    LOG_DEBUG(Audio, "VoN# srv flush sender={} fwd={} chDrop={}", sender, forwarded, dropped);
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
