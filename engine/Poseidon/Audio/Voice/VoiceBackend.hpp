#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <Poseidon/Audio/Voice/VonNet.hpp>

namespace Poseidon
{
class VoNCapture;
class VoNClient;

class IVoiceCaptureBackend
{
  public:
    virtual ~IVoiceCaptureBackend() = default;

    virtual bool open(const char* deviceName, int sampleRate, int bufferSamples) = 0;
    virtual void close() = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual int availableSamples() const = 0;
    virtual int read(int16_t* buffer, int maxSamples) = 0;
    virtual float lastFramePeak() const = 0;
    virtual bool isOpen() const = 0;
    virtual bool isCapturing() const = 0;
    virtual int sampleRate() const = 0;
};

class IVoiceSpeakerBackend
{
  public:
    virtual ~IVoiceSpeakerBackend() = default;

    virtual void init() = 0;
    virtual void destroy() = 0;
    virtual void setChannel(VoNChatChannel channel) = 0;
    virtual void setPosition(float x, float y, float z) = 0;
    virtual void stopStream() = 0;
    virtual bool feed(VoNClient* client, uint32_t channel) = 0;
    virtual bool isActive() const = 0;
    virtual float level() const = 0;
};

class IVoiceLoopbackBackend
{
  public:
    virtual ~IVoiceLoopbackBackend() = default;

    virtual bool open(int sampleRate) = 0;
    virtual void close() = 0;
    virtual void tick(VoNCapture& capture) = 0;
    virtual bool isOpen() const = 0;
};

struct VoiceBackendDescriptor
{
    const char* codeName = "dummy";
    int priority = 0;
    bool (*isAvailable)() = nullptr;
    std::unique_ptr<IVoiceCaptureBackend> (*createCapture)() = nullptr;
    std::unique_ptr<IVoiceSpeakerBackend> (*createSpeaker)() = nullptr;
    std::unique_ptr<IVoiceLoopbackBackend> (*createLoopback)() = nullptr;
    std::vector<std::string> (*listDevices)() = nullptr;
};

bool RegisterVoiceBackend(const VoiceBackendDescriptor& backend);
const VoiceBackendDescriptor& GetSelectedVoiceBackend();

void RegisterOpenALVoiceBackend();

// Test-only backend simulating an always-hot microphone that produces a
// continuous 440 Hz tone in real time. Selected over the platform backends
// only when POSEIDON_VOICE_TEST_TONE is set in the environment, so live
// integration tests can drive the real capture -> recorder -> network path
// on machines without input devices.
void RegisterTestToneVoiceBackend();

} // namespace Poseidon
