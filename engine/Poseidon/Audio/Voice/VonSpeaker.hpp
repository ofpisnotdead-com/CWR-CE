#pragma once

#include <cstdint>
#include <memory>

#include <Poseidon/Audio/Voice/VonNet.hpp>

namespace Poseidon
{
class IVoiceSpeakerBackend;
class VoNClient;

struct VoNSpeaker
{
    VoNSpeaker();
    ~VoNSpeaker();

    VoNSpeaker(const VoNSpeaker&) = delete;
    VoNSpeaker& operator=(const VoNSpeaker&) = delete;

    void init();
    void destroy();
    void setChannel(VoNChatChannel channel);
    void setPosition(float x, float y, float z);
    void stopStream();
    bool feed(VoNClient* client, uint32_t channel);

    bool active = false;
    float level = 0.0f;

  private:
    IVoiceSpeakerBackend* EnsureImpl();
    void SyncState();

    std::unique_ptr<IVoiceSpeakerBackend> _impl;
};

} // namespace Poseidon
