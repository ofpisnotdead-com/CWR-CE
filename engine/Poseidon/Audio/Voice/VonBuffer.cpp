#include <Poseidon/Audio/Voice/VonBuffer.hpp>
#include <algorithm>
#include <cstring>

namespace Poseidon
{
VoNJitterBuffer::VoNJitterBuffer(int capacity, int frameSamples) : _capacity(capacity), _frameSamples(frameSamples)
{
    _ring.resize(capacity);
    for (auto& f : _ring)
        f.pcm.resize(frameSamples, 0);
}

VoNJitterPush VoNJitterBuffer::push(uint64_t origin, const int16_t* pcm, int samples)
{
    if (samples != _frameSamples)
        return VoNJitterPush::BadSize;

    if (!_started)
    {
        _nextOrigin = origin;
        _started = true;
    }

    // Accept earlier packets if within capacity (out-of-order arrival before pull)
    if (origin < _nextOrigin)
    {
        int behind = static_cast<int>((_nextOrigin - origin) / _frameSamples);
        if (behind >= _capacity)
            return VoNJitterPush::TooOld;
        _nextOrigin = origin; // shift base back
    }

    // Far ahead of the play cursor: the sender skipped past the buffer
    // window (burst gap, capture backlog, sustained loss). Voice frames
    // are unreliable and never retransmitted, so the frames the cursor
    // waits for can no longer arrive — resync to the new stream position
    // instead of muting the sender forever.
    bool resynced = false;
    int ahead = static_cast<int>((origin - _nextOrigin) / _frameSamples);
    if (ahead >= _capacity)
    {
        for (auto& f : _ring)
            f.valid = false;
        _nextOrigin = origin;
        resynced = true;
    }

    // Duplicate check + find free slot (linear scan, origin-keyed)
    int freeSlot = -1;
    for (int i = 0; i < _capacity; ++i)
    {
        if (_ring[i].valid && _ring[i].origin == origin)
            return VoNJitterPush::Duplicate;
        if (!_ring[i].valid && freeSlot < 0)
            freeSlot = i;
    }
    if (freeSlot < 0)
        return VoNJitterPush::Full;

    _ring[freeSlot].origin = origin;
    _ring[freeSlot].valid = true;
    std::memcpy(_ring[freeSlot].pcm.data(), pcm, samples * sizeof(int16_t));
    return resynced ? VoNJitterPush::Resync : VoNJitterPush::Accepted;
}

int VoNJitterBuffer::pull(int16_t* out, int maxSamples)
{
    if (!_started || maxSamples < _frameSamples)
        return 0;

    // Find the slot for _nextOrigin
    int idx = -1;
    bool anyValid = false;
    for (int i = 0; i < _capacity; ++i)
    {
        if (_ring[i].valid)
        {
            anyValid = true;
            if (_ring[i].origin == _nextOrigin)
                idx = i;
        }
    }

    // Buffer fully drained — signal caller to stop pulling
    if (idx < 0 && !anyValid)
        return 0;

    if (idx >= 0)
    {
        std::memcpy(out, _ring[idx].pcm.data(), _frameSamples * sizeof(int16_t));
        _ring[idx].valid = false;
    }
    else
    {
        // Gap during active speech — output silence to keep stream
        // continuous, and tick the underrun counter so callers /
        // tests / load regressions can observe the loss event
        // (audio-invariants A-31).
        std::memset(out, 0, _frameSamples * sizeof(int16_t));
        ++_underrunGapFrames;
    }

    _nextOrigin += _frameSamples;
    return _frameSamples;
}

void VoNJitterBuffer::reset()
{
    for (auto& f : _ring)
        f.valid = false;
    _nextOrigin = 0;
    _started = false;
    _underrunGapFrames = 0;
}

int VoNJitterBuffer::buffered() const
{
    int count = 0;
    for (auto& f : _ring)
        if (f.valid)
            ++count;
    return count;
}

} // namespace Poseidon
