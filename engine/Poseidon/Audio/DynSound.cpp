#include <Poseidon/Audio/DynSound.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Random/randomGen.hpp>

#include <Poseidon/Core/Global.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/Game/TitEffects.hpp>

#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Foundation/Strings/Mbcs.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>

// IsSpeakingRadio (AI/AIRadio) is defined at global scope; declare it here so the
// call below binds to that global definition.

using namespace Poseidon;
bool IsSpeakingRadio(AIUnit* sender);

namespace Poseidon
{

const ParamEntry* FindSound(RString name, SoundPars& pars);

DynSound::DynSound() = default;

DynSound::DynSound(const char* name)
{
    Load(name);
}

void DynSound::Load(const char* name)
{
    _name = name;
    FindSFX(name, _emptySound, _sounds);
}

SoundEntry DynSound::LoadEntry(const ParamEntry& entry)
{
    SoundEntry sound;
    GetValue(sound, entry);
    sound._probab = entry[3];
    sound._min_delay = entry[4], sound._mid_delay = entry[5];
    sound._max_delay = entry[6];
    return sound;
}

bool DynSound::IsOneLoopingSound() const
{
    if (_sounds.Size() != 1)
    {
        return false;
    }
    if (_sounds[0]._max_delay > 0)
    {
        return false;
    }
    if (_emptySound._max_delay > 0)
    {
        return false;
    }
    return true;
}

const SoundEntry& DynSound::SelectSound(float probab) const
{
    for (int i = 0; i < _sounds.Size(); i++)
    {
        probab -= _sounds[i]._probab;
        if (probab <= 0)
        {
            return _sounds[i];
        }
    }
    return _emptySound;
}

void SoundObject::StartSound()
{
    SoundPars pars;
    FindSound(_soundName, pars);

    // Nothing resolved means no audio to play: either FindSound already reported
    // an unknown key, or the entry is subtitle-only (titles, empty sound[] file).
    // Either way play nothing — the same guard DynSoundObject::Simulate uses
    // (pars.name[0]) — so an empty name never reaches the loader. Titles, if any,
    // still run via SimulateTitles().
    if (pars.name.GetLength() == 0)
    {
        return;
    }

    if (_hasObject)
    {
        Object* source = _object;
        if (_looped)
        {
            _sound =
                GSoundScene->OpenAndPlay(pars.name, source->WorldPosition(), source->WorldSpeed(), pars.vol, pars.freq);
        }
        else
        {
            _sound = GSoundScene->OpenAndPlayOnce(pars.name, source->WorldPosition(), source->WorldSpeed(), pars.vol,
                                                  pars.freq);
        }
        source->AttachWave(_sound, pars.freq);
    }
    else
    {
        if (_looped)
        {
            _sound = GSoundScene->OpenAndPlay2D(pars.name, pars.vol, pars.freq, false);
        }
        else
        {
            _sound = GSoundScene->OpenAndPlayOnce2D(pars.name, pars.vol, pars.freq, false);
        }
    }
}

void SoundObject::LoadSound()
{
    SoundPars pars;
    const ParamEntry* cls = FindSound(_soundName, pars);

    if (cls)
    {
        ParamEntry* entry = cls->FindEntry("forceTitles");
        if (entry)
        {
            _forceTitles = *entry;
        }
        if (_forceTitles || USER_CONFIG.showTitles)
        {
            entry = cls->FindEntry("titlesFont");
            if (entry)
            {
                _titlesFont = GEngine->LoadFont(GetFontID(*entry));
                _titlesSize = (*cls) >> "titlesSize";
            }

            const ParamEntry& cfg = (*cls) >> "titles";
            int n = cfg.GetSize() / 2;
            for (int i = 0; i < n; i++)
            {
                int index = _titles.Add();
                float t = cfg[2 * i];
                _titles[index].time = Glob.time + t;
                _titles[index].text = cfg[2 * i + 1];
            }
        }
    }
}

static OLinkArray<SoundObject> SpeakingObjects;

bool IsSpeakingDirect(AIUnit* sender)
{
    if (!sender)
    {
        return false;
    }

    for (int i = 0; i < SpeakingObjects.Size();)
    {
        SoundObject* obj = SpeakingObjects[i];
        if (!obj)
        {
            SpeakingObjects.Delete(i);
            continue;
        }
        if (!obj->IsWaiting() && obj->GetSender() == sender)
        {
            return true;
        }
        i++;
    }
    return false;
}

SoundObject::SoundObject(RString name, Object* source, bool looped, float maxTitlesDistance, float speed)
{
    _index = 0;
    _object = source;
    _soundName = name;

    _hasObject = source != nullptr;
    _looped = looped;
    _paused = false;

    _forceTitles = false;
    _titlesSize = 0;

    _maxTitlesDistance = maxTitlesDistance;
    _speed = speed;

    _waiting = false;
    if (source)
    {
        Person* person = dyn_cast<Person>(source);
        if (person && person->Brain())
        {
            _waiting = true;
            _sender = person->Brain();
            SpeakingObjects.Add(this);
        }
    }
    if (!_waiting)
    {
        LoadSound();
    }
}

bool SoundObject::Simulate(float deltaT, SimulationImportance prec)
{
    if (_hasObject && (!_object || _object->IsDammageDestroyed()))
    {
        _sound = nullptr;
        return false;
    }

    if (_waiting)
    {
        if (IsSpeakingDirect(_sender) || IsSpeakingRadio(_sender))
        {
            return true;
        }
        _waiting = false;
        LoadSound();
    }

    if (_forceTitles || USER_CONFIG.showTitles)
    {
        SimulateTitles();
    }

    if (_paused)
    {
        if (_sound)
        {
            _sound = nullptr;
        }
    }
    else
    {
        if (_soundName.GetLength() > 0 && !_sound)
        {
            StartSound();
        }
        if (_hasObject && _sound)
        {
            if (_object)
            {
                _sound->SetPosition(_object->WorldPosition(), _object->WorldSpeed());
            }
            else
            {
                // object destroyed - stop sound
                _sound = nullptr;
            }
        }
        if (_sound && _sound->IsTerminated())
        {
            _sound = nullptr;
        }
    }

    return (_sound != nullptr || ((_forceTitles || USER_CONFIG.showTitles) && _index < _titles.Size()));
}

void SoundObject::SimulateTitles()
{
    if (_index >= _titles.Size())
    {
        return;
    }
    if (Glob.time >= _titles[_index].time)
    {
        // titles show only within a limited distance
        if (!_object || !GScene->GetCamera() || _maxTitlesDistance == 0 ||
            GScene->GetCamera()->Position().Distance2(_object->WorldTransform().Position()) <=
                Square(_maxTitlesDistance))
        {
            TitleEffect* effect = nullptr;
            if (_titlesFont)
            {
                effect = CreateTitleEffect(TitPlainDown, _titles[_index].text, _speed, _titlesFont, _titlesSize);
            }
            else
            {
                effect = CreateTitleEffect(TitPlainDown, _titles[_index].text, _speed);
            }
            GWorld->SetTitleEffect(effect);
        }
        _index++;
    }
}

SoundOnVehicle::SoundOnVehicle(const char* name, Object* source, float maxTitlesDistance, float speed)
    : Vehicle(Shapes.New("data3d\\empty.p3d", false, true), VehicleTypes.New("SoundOnVehicle"), -1)
{
    _sound = new SoundObject(name, source, false, maxTitlesDistance, speed);
}

void SoundOnVehicle::Simulate(float deltaT, SimulationImportance prec)
{
    if (!_sound || !_sound->Simulate(deltaT, prec))
    {
        SetDelete();
    }
}

void DynSoundObject::Simulate(Object* source, float deltaT, SimulationImportance prec)
{
    if (prec > SimulateInvisibleNear)
    {
        if (_sound)
        {
            _sound->LastLoop();
        }
        return;
    }
    _timeToLive -= deltaT;
    if (_timeToLive > 0)
    {
        return; // continue playing or pause
    }
    // select sound source
    float rand = GFxRandGen.RandomValue();
    const SoundEntry& pars = _dynSound->SelectSound(rand);
    bool loop = _dynSound->IsOneLoopingSound();
    // play selected sound
    IWave* wave = nullptr;
    if (pars.name[0])
    {
        float rndFreq = 1;
        float rndVol = 1;
        if (pars.freqRnd)
        {
            rndFreq = 1 + GFxRandGen.RandomValue() * pars.freqRnd - pars.freqRnd * 0.5;
        }
        if (pars.volRnd)
        {
            rndVol = 1 + GFxRandGen.RandomValue() * pars.volRnd - pars.volRnd * 0.5;
        }
        if (loop)
        {
            wave = GSoundScene->OpenAndPlay(pars.name, source->WorldPosition(), source->WorldSpeed(), pars.vol * rndVol,
                                            pars.freq * rndFreq);
        }
        else
        {
            wave = GSoundScene->OpenAndPlayOnce(pars.name, source->WorldPosition(), source->WorldSpeed(),
                                                pars.vol * rndVol, pars.freq * rndFreq);
        }
        if (wave)
        {
            GSoundScene->AddSound(wave);
        }
    }
    // random time to live
    if (!loop)
    {
        float delay = GFxRandGen.Gauss(pars._min_delay, pars._mid_delay, pars._max_delay);
        if (wave)
        {
            delay += wave->GetLength();
        }
        _timeToLive = delay;
    }
    else
    {
        _timeToLive = 1e10;
    }
    if (_sound)
    {
        _sound->LastLoop();
    }
    _sound = wave;
}

DynSoundBank DynSounds;

DynSoundObject::DynSoundObject(const char* name) : _timeToLive(0)
{
    _dynSound = DynSounds.New(name);
}

void DynSoundObject::StopSound()
{
    if (_sound)
    {
        _sound->LastLoop();
    }
}

DynSoundObject::~DynSoundObject()
{
    StopSound();
}

DynSoundSource::DynSoundSource(const char* name) : Vehicle(nullptr, VehicleTypes.New("DynamicSound"), -1)
{
    if (name)
    {
        _dynSound = new DynSoundObject(name);
    }
}

DynSoundSource::~DynSoundSource() = default;

void DynSoundSource::StopSound()
{
    if (_dynSound)
    {
        _dynSound->StopSound();
    }
}

void DynSoundSource::Simulate(float deltaT, SimulationImportance prec)
{
    if (_dynSound)
    {
        _dynSound->Simulate(this, deltaT, prec);
    }
}

} // namespace Poseidon
namespace Poseidon::Foundation
{
template class Link<DynSound>;
} // namespace Poseidon::Foundation

const AIUnit* SoundObject::GetSender() const
{
    return _sender;
}
