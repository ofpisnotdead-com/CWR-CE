#pragma once

#include <Poseidon/Network/NetworkIface.hpp>

#include <utility>

namespace Poseidon
{

template <class WaveRef, class DeleteSoundFn>
bool ApplyReplicatedNetworkSoundState(WaveRef& wave, SoundStateType state, DeleteSoundFn&& deleteSound)
{
    if (!wave)
    {
        return false;
    }

    switch (state)
    {
        case SSRestart:
            wave->Restart();
            break;
        case SSStop:
            std::forward<DeleteSoundFn>(deleteSound)(wave);
            wave = nullptr;
            break;
        case SSRepeat:
            wave->Repeat(1000);
            break;
        case SSLastLoop:
            wave->LastLoop();
            wave = nullptr;
            break;
        default:
            return false;
    }

    return true;
}

} // namespace Poseidon
