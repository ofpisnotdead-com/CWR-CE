#include <Poseidon/Audio/Voice/VoiceBackend.hpp>

#include <Poseidon/Audio/Voice/VonApp.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string_view>

namespace Poseidon
{
namespace
{
class DummyVoiceCaptureBackend : public IVoiceCaptureBackend
{
  public:
    bool open(const char*, int sampleRate, int) override
    {
        _sampleRate = sampleRate;
        return false;
    }

    void close() override { _sampleRate = 0; }
    void start() override {}
    void stop() override {}
    int availableSamples() const override { return 0; }
    int read(int16_t*, int) override { return 0; }
    float lastFramePeak() const override { return 0.0f; }
    bool isOpen() const override { return false; }
    bool isCapturing() const override { return false; }
    int sampleRate() const override { return _sampleRate; }

  private:
    int _sampleRate = 0;
};

class DummyVoiceSpeakerBackend : public IVoiceSpeakerBackend
{
  public:
    void init() override
    {
        _active = false;
        _drainFrames = 0;
        _level = 0.0f;
    }

    void destroy() override
    {
        _active = false;
        _drainFrames = 0;
        _level = 0.0f;
    }

    void setChannel(VoNChatChannel) override {}
    void setPosition(float, float, float) override {}
    void stopStream() override
    {
        _active = false;
        _drainFrames = 0;
        _level = 0.0f;
    }

    bool feed(VoNClient* client, uint32_t channel) override
    {
        if (!client)
            return false;

        int16_t decoded[kFrameSamples];
        bool gotData = false;
        while (client->pullSpeaker(channel, decoded, kFrameSamples) == kFrameSamples)
        {
            gotData = true;
            UpdateLevel(decoded, kFrameSamples);
        }

        if (gotData)
        {
            _active = true;
            _drainFrames = 0;
            return true;
        }

        if (_active && ++_drainFrames > kDrainLimit)
            stopStream();
        return false;
    }

    bool isActive() const override { return _active; }
    float level() const override { return _level; }

  private:
    void UpdateLevel(const int16_t* pcm, int n)
    {
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
            sum += static_cast<double>(pcm[i]) * pcm[i];
        _level = static_cast<float>(std::sqrt(sum / n) / 32768.0);
    }

    static constexpr int kFrameSamples = 320;
    static constexpr int kDrainLimit = 30;

    bool _active = false;
    int _drainFrames = 0;
    float _level = 0.0f;
};

class DummyVoiceLoopbackBackend : public IVoiceLoopbackBackend
{
  public:
    bool open(int) override { return false; }
    void close() override {}
    void tick(VoNCapture&) override {}
    bool isOpen() const override { return false; }
};

// Failure modes selectable through the POSEIDON_VOICE_TEST_TONE value, so
// tests can exercise the transmitter-side health reporting:
//   "none" — the capture device fails to open (no microphone present)
//   "dead" — the device opens but never delivers a sample (stalled driver)
//   anything else — normal always-hot tone microphone
enum class TestToneMode
{
    Tone,
    Dead,
    None
};

TestToneMode TestToneModeFromEnv()
{
    const char* value = std::getenv("POSEIDON_VOICE_TEST_TONE");
    if (!value)
        return TestToneMode::Tone;
    if (std::string_view(value) == "none")
        return TestToneMode::None;
    if (std::string_view(value) == "dead")
        return TestToneMode::Dead;
    return TestToneMode::Tone;
}

// Always-hot synthetic microphone with the platform ring semantics the VoN
// client lifecycle depends on: samples accrue in real time while capturing
// (even when nobody transmits), production beyond the ring capacity is
// dropped, and stop() freezes production but keeps buffered samples readable.
class TestToneVoiceCaptureBackend : public IVoiceCaptureBackend
{
  public:
    bool open(const char*, int sampleRate, int bufferSamples) override
    {
        _mode = TestToneModeFromEnv();
        if (_mode == TestToneMode::None)
            return false;
        _sampleRate = sampleRate > 0 ? sampleRate : 16000;
        _capacity = bufferSamples > 0 ? bufferSamples : _sampleRate;
        _open = true;
        _capturing = false;
        _produced = 0;
        _consumed = 0;
        _lastFramePeak = 0.0f;
        return true;
    }

    void close() override
    {
        _open = false;
        _capturing = false;
    }

    void start() override
    {
        if (!_open || _capturing)
            return;
        _capturing = true;
        _lastProduceTime = std::chrono::steady_clock::now();
    }

    void stop() override
    {
        produce();
        _capturing = false;
    }

    int availableSamples() const override
    {
        produce();
        return static_cast<int>(_produced - _consumed);
    }

    int read(int16_t* buffer, int maxSamples) override
    {
        produce();
        const int64_t toRead = std::min<int64_t>(maxSamples, _produced - _consumed);
        if (toRead <= 0)
            return 0;

        for (int64_t i = 0; i < toRead; ++i)
        {
            const double phase = static_cast<double>(_consumed + i) * 2.0 * 3.14159265358979323846 * 440.0 /
                                 static_cast<double>(_sampleRate);
            buffer[i] = static_cast<int16_t>(std::sin(phase) * kAmplitude);
        }
        _consumed += toRead;
        _lastFramePeak = static_cast<float>(kAmplitude) / 32767.0f;
        return static_cast<int>(toRead);
    }

    float lastFramePeak() const override { return _lastFramePeak; }
    bool isOpen() const override { return _open; }
    bool isCapturing() const override { return _capturing; }
    int sampleRate() const override { return _sampleRate; }

  private:
    void produce() const
    {
        if (!_capturing || _mode == TestToneMode::Dead)
            return;
        const auto now = std::chrono::steady_clock::now();
        const int64_t elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(now - _lastProduceTime).count();
        const int64_t samples = elapsedNs * _sampleRate / 1'000'000'000;
        if (samples <= 0)
            return;
        // Advance the clock by whole samples only, so fractional remainders
        // carry over instead of drifting the sample rate.
        _lastProduceTime += std::chrono::nanoseconds(samples * 1'000'000'000 / _sampleRate);
        _produced = std::min(_produced + samples, _consumed + _capacity);
    }

    static constexpr int kAmplitude = 12000;

    TestToneMode _mode = TestToneMode::Tone;
    int _sampleRate = 16000;
    int64_t _capacity = 16000;
    bool _open = false;
    bool _capturing = false;
    float _lastFramePeak = 0.0f;
    mutable int64_t _produced = 0;
    int64_t _consumed = 0;
    mutable std::chrono::steady_clock::time_point _lastProduceTime{};
};

// Pulls decoded audio at real-time playback rate like a sound device. The
// receive-side failure modes (jitter-buffer wedge, drain timeout) only
// manifest when the consumer cannot outrun the producer — a drain-everything
// speaker hides them, so the paced pull is the point of this backend.
class TestToneVoiceSpeakerBackend : public IVoiceSpeakerBackend
{
  public:
    void init() override
    {
        _active = false;
        _drainFeeds = 0;
        _level = 0.0f;
        _budgetSamples = 0;
        _lastFeedTime = std::chrono::steady_clock::now();
    }

    void destroy() override { stopStream(); }
    void setChannel(VoNChatChannel) override {}
    void setPosition(float, float, float) override {}

    void stopStream() override
    {
        _active = false;
        _drainFeeds = 0;
        _level = 0.0f;
        _budgetSamples = 0;
    }

    bool feed(VoNClient* client, uint32_t channel) override
    {
        if (!client)
            return false;

        const auto now = std::chrono::steady_clock::now();
        const int64_t elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(now - _lastFeedTime).count();
        _lastFeedTime = now;
        // Cap the budget like a device with a short playback queue, so a
        // long pump gap cannot instant-drain a packet flood.
        _budgetSamples = std::min<int64_t>(_budgetSamples + elapsedNs * kSampleRate / 1'000'000'000,
                                           static_cast<int64_t>(4) * kFrameSamples);

        int16_t decoded[kFrameSamples];
        bool gotData = false;
        while (_budgetSamples >= kFrameSamples && client->pullSpeaker(channel, decoded, kFrameSamples) == kFrameSamples)
        {
            _budgetSamples -= kFrameSamples;
            gotData = true;
            UpdateLevel(decoded, kFrameSamples);
        }

        if (gotData)
        {
            _active = true;
            _drainFeeds = 0;
            return true;
        }

        if (_active && ++_drainFeeds > kDrainLimit)
            stopStream();
        return false;
    }

    bool isActive() const override { return _active; }
    float level() const override { return _level; }

  private:
    void UpdateLevel(const int16_t* pcm, int n)
    {
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
            sum += static_cast<double>(pcm[i]) * pcm[i];
        _level = static_cast<float>(std::sqrt(sum / n) / 32768.0);
    }

    static constexpr int kSampleRate = 16000;
    static constexpr int kFrameSamples = 320;
    static constexpr int kDrainLimit = 30;

    bool _active = false;
    int _drainFeeds = 0;
    float _level = 0.0f;
    int64_t _budgetSamples = 0;
    std::chrono::steady_clock::time_point _lastFeedTime{};
};

bool IsTestToneVoiceBackendEnabled()
{
    const char* value = std::getenv("POSEIDON_VOICE_TEST_TONE");
    return value != nullptr && *value != '\0';
}

std::unique_ptr<IVoiceCaptureBackend> CreateTestToneCapture()
{
    return std::make_unique<TestToneVoiceCaptureBackend>();
}

std::unique_ptr<IVoiceSpeakerBackend> CreateTestToneSpeaker()
{
    return std::make_unique<TestToneVoiceSpeakerBackend>();
}

bool IsBackendAvailable(const VoiceBackendDescriptor& backend)
{
    return backend.isAvailable == nullptr || backend.isAvailable();
}

std::unique_ptr<IVoiceCaptureBackend> CreateDummyCapture()
{
    return std::make_unique<DummyVoiceCaptureBackend>();
}

std::unique_ptr<IVoiceSpeakerBackend> CreateDummySpeaker()
{
    return std::make_unique<DummyVoiceSpeakerBackend>();
}

std::unique_ptr<IVoiceLoopbackBackend> CreateDummyLoopback()
{
    return std::make_unique<DummyVoiceLoopbackBackend>();
}

std::vector<std::string> ListDummyDevices()
{
    return {};
}

VoiceBackendDescriptor& DummyBackend()
{
    static VoiceBackendDescriptor backend{
        .codeName = "dummy",
        .priority = 0,
        .isAvailable = nullptr,
        .createCapture = &CreateDummyCapture,
        .createSpeaker = &CreateDummySpeaker,
        .createLoopback = &CreateDummyLoopback,
        .listDevices = &ListDummyDevices,
    };
    return backend;
}

std::vector<VoiceBackendDescriptor>& Registry()
{
    static std::vector<VoiceBackendDescriptor> registry;
    return registry;
}
} // namespace

bool RegisterVoiceBackend(const VoiceBackendDescriptor& backend)
{
    if (!backend.createCapture || !backend.createSpeaker || !backend.createLoopback)
        return false;

    auto& registry = Registry();
    const auto existing =
        std::find_if(registry.begin(), registry.end(), [&](const VoiceBackendDescriptor& item)
                     { return std::string_view(item.codeName) == std::string_view(backend.codeName); });
    if (existing != registry.end())
        return false;

    registry.push_back(backend);
    std::sort(registry.begin(), registry.end(),
              [](const VoiceBackendDescriptor& lhs, const VoiceBackendDescriptor& rhs)
              {
                  if (lhs.priority != rhs.priority)
                      return lhs.priority < rhs.priority;
                  return std::string(lhs.codeName) < std::string(rhs.codeName);
              });
    return true;
}

const VoiceBackendDescriptor& GetSelectedVoiceBackend()
{
    auto& registry = Registry();
    for (auto it = registry.rbegin(); it != registry.rend(); ++it)
    {
        if (IsBackendAvailable(*it))
            return *it;
    }
    return DummyBackend();
}

void RegisterTestToneVoiceBackend()
{
    static const bool registered = RegisterVoiceBackend({
        .codeName = "test-tone",
        .priority = 100,
        .isAvailable = &IsTestToneVoiceBackendEnabled,
        .createCapture = &CreateTestToneCapture,
        .createSpeaker = &CreateTestToneSpeaker,
        .createLoopback = &CreateDummyLoopback,
        .listDevices = &ListDummyDevices,
    });
    (void)registered;
}

} // namespace Poseidon
