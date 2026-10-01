#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Audio/IAudioSystem.hpp>
#include <Poseidon/Audio/SoundScene.hpp>
#include <Poseidon/IO/ParamFileExt.hpp>
#include <Poseidon/IO/Serialization/ParamArchive.hpp>
#include <Poseidon/Audio/Speaker.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/World/World.hpp>
#include <Random/randomGen.hpp>

#include <Poseidon/UI/Locale/Languages.hpp>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Enums/EnumNames.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Memory/FastAlloc.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>

namespace Poseidon
{
using Poseidon::Foundation::EnumName;

static RStringB RandomMicOut()
{
    const ParamEntry& micOuts = Pars >> "CfgVoice" >> "micOuts";
    int i = toInt(GFxRandGen.RandomValue() * micOuts.GetSize());
    return micOuts[i];
}

BasicSpeaker::BasicSpeaker(const ParamEntry& cfg)
{
    const ParamEntry& dirs = cfg >> "directories";
    int n = dirs.GetSize();
    _directories.Realloc(n);
    _directories.Resize(n);
    for (int i = 0; i < n; i++)
    {
        RString dir = dirs[i];
        if (dir[0] == '\\')
        {
            _directories[i] = (const char*)dir + 1;
        }
        else
        {
            _directories[i] = RString("voice\\") + dir;
        }
    }
}

IWave* BasicSpeaker::Say(RString id, float pitch, bool loop)
{
    RString dir = ENGINE_CONFIG.singleVoice ? _directories[1] : _directories[0];
    const ParamEntry& word = Pars >> "CfgVoice" >> "Words" >> id;
    RString name = dir + word[0].GetValue() + RString(".wss");

    IWave* wave = GSoundScene->OpenAndPlayOnce2D(name, 1, pitch, false);
    if (wave)
    {
        wave->SetKind(WaveSpeech);
        if (loop)
        {
            wave->Repeat(1000);
        }
    }
    return wave;
}

RadioChannel::RadioChannel(ChatChannel chatChannel, NetworkObject* object, RadioNoise noise)
{
    _audible = false;
    _chatChannel = chatChannel;
    _object = object;
    _pauseAfter = 0;
    _pauseAfterMessage = 0;
    _noiseType = noise;
}

bool RadioChannel::Done() const
{
    if (_messageQueue.Size() > 0)
    {
        return false;
    }
    return _actualMsg == nullptr;
}

void RadioChannel::Simulate(float deltaT)
{
    if (_saying)
    {
        if (!_saying->IsTerminated())
        {
            return;
        }
        // release wave
        _saying.Free();
        // stop noise channel
        if (_speaker.IsSpeakerValid() && _noiseType == RNRadio)
        {
            _noise = _speaker.SayNoPitch(RandomMicOut(), false);
        }
        else
        {
            _noise.Free();
        }
        // noise will auto-terminated
    }
    // here _noise must contain _micOut (non-looped)
    if (_noise)
    {
        if (!_noise->IsTerminated())
        {
            return;
        }
        _noise.Free();
        // give other channels an opportunity to start
        // (they may contain higher priority messages)
        return;
    }

    _pauseAfterMessage -= deltaT;
    if (_pauseAfterMessage <= 0)
    {
        NextMessage();
    }
}

struct ForceTrasmitContext : public RefCount
{
    Link<RadioMessage> _message;

  public:
    ForceTrasmitContext(RadioMessage* msg) : _message(msg) {}

    USE_FAST_ALLOCATOR
};

DEFINE_FAST_ALLOCATOR(ForceTrasmitContext)

void ForceTransmit(IWave* wave, RefCount* context)
{
    if (context)
    {
        RadioMessage* msg = static_cast<ForceTrasmitContext*>(context)->_message;
        if (msg && !msg->IsTransmitted())
        {
            msg->Transmitted();
            msg->SetTransmitted();
        }
    }
}

void RadioChannel::Say(Speaker* speaker, RString id, float pauseAfter, bool transmit)
{
    if (!speaker)
    {
        return;
    }

    if (!_saying)
    {
        // start queue
        _speaker = *speaker;
        _saying = speaker->Say(id);
        if (_saying)
        {
            if (transmit)
            {
                _saying->SetOnPlay(ForceTransmit, new ForceTrasmitContext(_actualMsg));
            }
            _saying->Play(); // start playback (once)
            //  start noise
            if (_noiseType == RNRadio)
            {
                _noise = speaker->SayNoPitch("loop", true);
            }
        }
    }
    else
    {
        IWave* wave = speaker->Say(id);
        if (wave)
        {
            if (transmit)
            {
                wave->SetOnPlay(ForceTransmit, new ForceTrasmitContext(_actualMsg));
            }
            wave->Queue(_saying);
        }
    }
    if (pauseAfter > 0.01)
    {
        IWave* wave = GSoundScene->SayPause(pauseAfter);
        if (wave)
        {
            wave->Queue(_saying);
        }
    }
}

void RadioChannel::Transmit(RadioMessage* msg, int language)
{
    // add single message to message queue
    msg->SetLanguage(language);

    const ParamEntry& cls = Res >> "RadioProtocol" >> msg->GetPriorityClass();
    int priority = cls >> "priority";
    float timeout = cls >> "timeout";
    msg->SetPriority(priority);
    msg->SetTimeOut(Glob.time + timeout);

    int n = _messageQueue.Size();
    for (int i = 0; i < n; i++)
    {
        if (priority > _messageQueue[i]->GetPriority())
        {
            _messageQueue.Insert(i, msg);
            return;
        }
    }
    _messageQueue.Add(msg);
}

void RadioChannel::Cancel(RadioMessage* msg)
{
    // remove from queue/current message
    if (msg == _actualMsg)
    {
        msg->Canceled();
        _actualMsg.Free();
        NextMessage();
        return;
    }
    for (int i = 0; i < _messageQueue.Size(); i++)
    {
        if (_messageQueue[i] == msg)
        {
            msg->Canceled();
            _messageQueue.Delete(i);
            return;
        }
    }
    Fail("Canceled message never transmitted.");
}

void RadioChannel::CancelAllMessages()
{
    for (int i = 0; i < _messageQueue.Size(); i++)
    {
        _messageQueue[i]->Canceled();
    }
    _messageQueue.Resize(0);
    if (_actualMsg)
    {
        _actualMsg->Canceled();
    }
    _actualMsg.Free();
    NextMessage();
}

void RadioChannel::Replace(RadioMessage* msg, RadioMessage* with)
{
    // replace in queue/current message
    if (msg == _actualMsg)
    {
        // do not change words of actual message, only text displayed
        _actualMsg->Canceled();
        _actualMsg = with;
        return;
    }
    for (int i = 0; i < _messageQueue.Size(); i++)
    {
        if (_messageQueue[i] == msg)
        {
            _messageQueue[i] = with;
            return;
        }
    }
    Fail("Replaced message never transmitted.");
}

void RadioChannel::SetAudible(bool audible)
{
    if (_audible && !audible)
    {
        if (_actualMsg)
        {
            _actualMsg->Transmitted(); // confirm xmit done
            _actualMsg = nullptr;
            _pauseAfterMessage = 0;
            _pauseAfter = 0;
        }
    }
    _audible = audible;
}

template <>
const EnumName* Poseidon::Foundation::GetEnumNames(RadioNoise dummy)
{
    static const EnumName RadioNoiseNames[] = {EnumName(RNRadio, "RADIO"), EnumName(RNIntercomm, "ICOM"),
                                               EnumName(RNNone, "NONE"), EnumName()};
    return RadioNoiseNames;
}

LSError RadioChannel::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.Serialize("audible", _audible, 1))
    PARAM_CHECK(ar.Serialize("chatChannel", (int&)_chatChannel, 1))
    PARAM_CHECK(ar.Serialize("actualMsg", _actualMsg, 1))
    PARAM_CHECK(ar.Serialize("messageQueue", _messageQueue, 1))
    PARAM_CHECK(ar.SerializeEnum("noiseType", _noiseType, 1, RNRadio))
    return LSOK;
}

bool RadioChannel::IsSilent() const
{
    if (!IsEmpty())
    {
        return false;
    }
    return !_saying && !_noise;
}

RadioMessage* RadioChannel::GetNextMessage(int& index) const
{
    index++;
    if (index < 0)
    {
        index = 0;
    }

    for (; index < _messageQueue.Size(); index++)
    {
        RadioMessage* msg = _messageQueue[index];
        if (msg)
        {
            return msg;
        }
    }
    return nullptr;
}

RadioMessage* RadioChannel::GetPrevMessage(int& index) const
{
    index--;
    int n = _messageQueue.Size();
    if (index >= n)
    {
        index = n - 1;
    }
    for (; index >= 0; index--)
    {
        RadioMessage* msg = _messageQueue[index];
        if (msg)
        {
            return msg;
        }
    }
    return nullptr;
}

RadioMessage* RadioChannel::FindNextMessage(int type, int& index) const
{
    index++;
    if (index < 0)
    {
        index = 0;
    }
    for (; index < _messageQueue.Size(); index++)
    {
        RadioMessage* msg = _messageQueue[index];
        if (!msg)
        {
            continue;
        }
        if (msg->GetType() == type)
        {
            return msg;
        }
    }
    return nullptr;
}

RadioMessage* RadioChannel::FindPrevMessage(int type, int& index) const
{
    index--;
    int n = _messageQueue.Size();
    if (index >= n)
    {
        index = n - 1;
    }
    for (; index >= 0; index--)
    {
        RadioMessage* msg = _messageQueue[index];
        if (!msg)
        {
            continue;
        }
        if (msg->GetType() == type)
        {
            return msg;
        }
    }
    return nullptr;
}

} // namespace Poseidon
