#pragma once

#include <Poseidon/Audio/Voice/VoiceBackend.hpp>
#include <Poseidon/Audio/Voice/VonApp.hpp>
#include <PoseidonOpenAL/OpenALRuntime.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <cstring>
#include <cmath>

namespace Poseidon
{
class VoNSpeakerOpenAL : public IVoiceSpeakerBackend
{
  public:
    void init() override
    {
        if (!OpenALRuntime::EnsureLoaded())
        {
            LOG_WARN(Audio, "VoN# spk NO-OPENAL (drain-only fallback, audio inaudible)");
            return;
        }

        alGenSources(1, &source);
        if (alGetError() != AL_NO_ERROR)
        {
            source = 0;
            LOG_WARN(Audio, "VoN# spk NO-SOURCE (drain-only fallback, audio inaudible)");
            return;
        }
        alGenBuffers(NUM_BUFS, bufs);
        if (alGetError() != AL_NO_ERROR)
        {
            alDeleteSources(1, &source);
            source = 0;
            LOG_WARN(Audio, "VoN# spk NO-BUFFERS (drain-only fallback, audio inaudible)");
            return;
        }
        active = false;
        drainFrames = 0;
        freeBufCount = NUM_BUFS;
        for (int i = 0; i < NUM_BUFS; ++i)
            freeBufs[i] = bufs[i];
        applySpatialState();
    }

    void destroy() override
    {
        if (!source)
            return;

        alSourceStop(source);
        ALint queued = 0;
        alGetSourcei(source, AL_BUFFERS_QUEUED, &queued);
        while (queued-- > 0)
        {
            ALuint buf;
            alSourceUnqueueBuffers(source, 1, &buf);
        }
        alDeleteSources(1, &source);
        alDeleteBuffers(NUM_BUFS, bufs);
        source = 0;
        active = false;
        levelValue = 0.0f;
    }

    void setChannel(VoNChatChannel channel) override
    {
        chatChannel = channel;
        applySpatialState();
    }

    void setPosition(float x, float y, float z) override
    {
        position[0] = x;
        position[1] = y;
        position[2] = z;
        applySpatialState();
    }

    void stopStream() override
    {
        if (!source)
            return;
        alSourceStop(source);
        ALint queued = 0;
        alGetSourcei(source, AL_BUFFERS_QUEUED, &queued);
        while (queued-- > 0)
        {
            ALuint buf;
            alSourceUnqueueBuffers(source, 1, &buf);
        }
        // Back to AL_INITIAL: on an AL_STOPPED source every queued buffer
        // counts as processed, so the next stream's buffers would be
        // recycled as fast as they are queued and AL_BUFFERS_QUEUED could
        // never reach START_THRESHOLD — the speaker would stay mute for
        // every stream after the first.
        alSourceRewind(source);
        active = false;
        drainFrames = 0;
        // Return all buffers to free pool
        freeBufCount = NUM_BUFS;
        for (int i = 0; i < NUM_BUFS; ++i)
            freeBufs[i] = bufs[i];
    }

    bool feed(VoNClient* client, uint32_t channel) override
    {
        if (!source)
            return drainDecodedOnly(client, channel);

        int16_t decoded[FRAME_SAMPLES];

        // Recycle processed buffers back to free pool
        ALint processed = 0;
        alGetSourcei(source, AL_BUFFERS_PROCESSED, &processed);
        while (processed > 0)
        {
            ALuint buf;
            alSourceUnqueueBuffers(source, 1, &buf);
            freeBufs[freeBufCount++] = buf;
            --processed;
        }
        trapAlError(channel, "recycle");

        // Fill free buffers with data from jitter buffer
        int queued = 0;
        while (freeBufCount > 0)
        {
            if (client->pullSpeaker(channel, decoded, FRAME_SAMPLES) != FRAME_SAMPLES)
                break;
            ALuint buf = freeBufs[--freeBufCount];
            alBufferData(buf, AL_FORMAT_MONO16, decoded, FRAME_SAMPLES * 2, SAMPLE_RATE);
            alSourceQueueBuffers(source, 1, &buf);
            ++queued;
            updateLevel(decoded, FRAME_SAMPLES);
        }
        if (queued > 0)
            trapAlError(channel, "queue");

        bool gotData = queued > 0;

        if (!active)
        {
            // Need enough buffered before starting (A3: ~100ms)
            ALint totalQueued = 0;
            alGetSourcei(source, AL_BUFFERS_QUEUED, &totalQueued);
            if (totalQueued >= START_THRESHOLD)
            {
                alSourcePlay(source);
                trapAlError(channel, "play");
                active = true;
                drainFrames = 0;
                LOG_DEBUG(Audio, "VoN# spk start ch={} queued={}", channel, totalQueued);
            }
            return false;
        }

        // A3-style: if source stopped (underrun), just restart — no reset
        ALint state;
        alGetSourcei(source, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING)
        {
            ALint stillQueued = 0;
            alGetSourcei(source, AL_BUFFERS_QUEUED, &stillQueued);
            if (stillQueued > 0)
            {
                alSourcePlay(source);
                trapAlError(channel, "resume");
                ++resumeCount;
                if (resumeCount <= 3 || resumeCount % 20 == 0)
                    LOG_DEBUG(Audio, "VoN# spk resume ch={} queued={} n={}", channel, stillQueued, resumeCount);
            }
        }

        // Drain timeout: count frames with no real data from jitter buffer.
        // A3 uses m_noMsgCount > 10 at 50ms = 500ms; we use 30 at ~16ms = ~500ms.
        if (gotData)
        {
            drainFrames = 0;
        }
        else
        {
            ++drainFrames;
            if (drainFrames > DRAIN_LIMIT)
            {
                LOG_DEBUG(Audio, "VoN# spk stop ch={} (drain timeout)", channel);
                stopStream();
                levelValue = 0.0f;
            }
        }

        return gotData;
    }

    bool isActive() const override { return active; }
    float level() const override { return levelValue; }

  private:
    bool isDirect() const { return chatChannel == VoNChatChannel::Direct; }

    void applySpatialState()
    {
        if (!source)
            return;

        if (isDirect())
        {
            alSourcei(source, AL_SOURCE_RELATIVE, AL_FALSE);
            alSource3f(source, AL_POSITION, -position[0], position[1], position[2]);
            alSourcef(source, AL_REFERENCE_DISTANCE, 1.0f);
            alSourcef(source, AL_MAX_DISTANCE, 20.0f);
            alSourcef(source, AL_ROLLOFF_FACTOR, 1.0f);
        }
        else
        {
            alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);
            alSource3f(source, AL_POSITION, 0.0f, 0.0f, 0.0f);
            alSourcef(source, AL_REFERENCE_DISTANCE, 1.0f);
            alSourcef(source, AL_MAX_DISTANCE, 1.0f);
            alSourcef(source, AL_ROLLOFF_FACTOR, 0.0f);
        }
    }

    bool drainDecodedOnly(VoNClient* client, uint32_t channel)
    {
        if (!client)
            return false;

        int16_t decoded[FRAME_SAMPLES];
        bool gotData = false;
        while (client->pullSpeaker(channel, decoded, FRAME_SAMPLES) == FRAME_SAMPLES)
        {
            gotData = true;
            updateLevel(decoded, FRAME_SAMPLES);
        }

        if (gotData)
        {
            active = true;
            drainFrames = 0;
            return true;
        }

        if (active && ++drainFrames > DRAIN_LIMIT)
        {
            active = false;
            levelValue = 0.0f;
            drainFrames = 0;
        }
        return false;
    }

    void updateLevel(const int16_t* pcm, int n)
    {
        double sum = 0;
        for (int i = 0; i < n; ++i)
            sum += static_cast<double>(pcm[i]) * pcm[i];
        levelValue = static_cast<float>(std::sqrt(sum / n) / 32768.0);
    }

    void trapAlError(uint32_t channel, const char* stage)
    {
        ALenum err = alGetError();
        if (err != AL_NO_ERROR)
            LOG_WARN(Audio, "VoN# spk AL-ERROR ch={} stage={} err={:#x}", channel, stage, static_cast<unsigned>(err));
    }

    ALuint source = 0;
    VoNChatChannel chatChannel = VoNChatChannel::Global;
    float position[3] = {0.0f, 0.0f, 0.0f};
    static constexpr int NUM_BUFS = 24;
    static constexpr int FRAME_SAMPLES = 320;
    static constexpr int SAMPLE_RATE = 16000;
    static constexpr int START_THRESHOLD = 5;
    static constexpr int DRAIN_LIMIT = 30;

    ALuint bufs[NUM_BUFS] = {};
    bool active = false;
    int freeBufCount = 0;
    ALuint freeBufs[NUM_BUFS] = {};
    int drainFrames = 0;
    float levelValue = 0.0f;
    unsigned resumeCount = 0; // diagnostic (VoN# log lines)

  public:
    ALuint sourceForTests() const { return source; }
};

} // namespace Poseidon
