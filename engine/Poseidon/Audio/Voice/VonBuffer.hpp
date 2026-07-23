#pragma once
// VoN jitter buffer — reorder, dedup, and smooth voice packet delivery

#include <cstdint>
#include <cstring>
#include <vector>

namespace Poseidon
{
// What VoNJitterBuffer::push did with a frame — callers log the non-Accepted
// outcomes, which are exactly the events that eat audio.
enum class VoNJitterPush : uint8_t
{
    Accepted = 0,
    BadSize,   // samples != frameSamples
    Duplicate, // origin already buffered
    TooOld,    // behind the play cursor beyond capacity
    Resync,    // far ahead — buffer jumped to the new stream position
    Full       // no free slot
};

class VoNJitterBuffer
{
  public:
    // capacity = max frames buffered, frameSamples = samples per codec frame
    explicit VoNJitterBuffer(int capacity = 16, int frameSamples = 320);

    // Push a decoded frame with its origin sample offset
    VoNJitterPush push(uint64_t origin, const int16_t* pcm, int samples);

    // Pull next frame in order. Returns samples written (frameSamples or 0).
    // If gap detected, writes silence. If empty, returns 0.
    int pull(int16_t* out, int maxSamples);

    void reset();
    int buffered() const; // number of frames currently queued
    bool empty() const { return buffered() == 0; }
    bool started() const { return _started; }
    uint64_t nextOrigin() const { return _nextOrigin; }

    // Session-cumulative count of frames where pull() emitted silence
    // because the expected origin slot was empty — audio-invariants A-31.
    // Each underrun is a real network-loss / late-arrival event, not a
    // shutdown-quiet frame.  Reset by reset().
    int underrunGapFrames() const { return _underrunGapFrames; }

  private:
    struct Frame
    {
        uint64_t origin = 0;
        bool valid = false;
        std::vector<int16_t> pcm;
    };

    std::vector<Frame> _ring;
    int _capacity;
    int _frameSamples;
    uint64_t _nextOrigin = 0; // next expected origin
    bool _started = false;
    int _underrunGapFrames = 0; // session-cumulative gap-fill events (A-31)
};

} // namespace Poseidon
