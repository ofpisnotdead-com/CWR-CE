#include <Poseidon/Audio/Voice/VonSpeaker.hpp>

#include <Poseidon/Audio/Voice/VoiceBackend.hpp>

namespace Poseidon
{
VoNSpeaker::VoNSpeaker() = default;

VoNSpeaker::~VoNSpeaker()
{
    destroy();
}

IVoiceSpeakerBackend* VoNSpeaker::EnsureImpl()
{
    if (!_impl)
        _impl = GetSelectedVoiceBackend().createSpeaker();
    return _impl.get();
}

void VoNSpeaker::SyncState()
{
    if (!_impl)
    {
        active = false;
        level = 0.0f;
        return;
    }

    active = _impl->isActive();
    level = _impl->level();
}

void VoNSpeaker::init()
{
    EnsureImpl()->init();
    SyncState();
}

void VoNSpeaker::destroy()
{
    if (!_impl)
        return;

    _impl->destroy();
    SyncState();
}

void VoNSpeaker::setChannel(VoNChatChannel channel)
{
    EnsureImpl()->setChannel(channel);
    SyncState();
}

void VoNSpeaker::setPosition(float x, float y, float z)
{
    EnsureImpl()->setPosition(x, y, z);
    SyncState();
}

void VoNSpeaker::stopStream()
{
    EnsureImpl()->stopStream();
    SyncState();
}

bool VoNSpeaker::feed(VoNClient* client, uint32_t channel)
{
    bool gotData = EnsureImpl()->feed(client, channel);
    SyncState();
    return gotData;
}

} // namespace Poseidon
