#include <Poseidon/Core/Application.hpp>
#include <Poseidon/AI/VehicleAI.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/AI/AIRadio.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Entities/Vehicles/Transport.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/World/Entities/Weapons/Shots.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Audio/Core/WaveLifecycle.hpp>
#include <float.h>
#include <limits.h>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/BankArray.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/Types/RemoveLinks.hpp>
#include <Poseidon/Foundation/platform.hpp>
#include <Random/randomGen.hpp>
#include <Poseidon/Foundation/Strings/Bstring.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/World/Scene/Thing.hpp>
#include <Poseidon/Game/OperMap.hpp>
#include <Poseidon/World/Simulation/FrameInv.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Dev/Diag/DiagModes.hpp>
#include <Poseidon/Dev/Debug/DebugCheats.hpp>
#include <Poseidon/Graphics/Rendering/Draw/SpecLods.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Graphics/Textures/TexturePreload.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Game/UiActions.hpp>
#include <Poseidon/World/Entities/Infantry/MoveActions.hpp>
#include <Poseidon/World/Detection/Detector.hpp>
#include <Poseidon/Foundation/Enums/EnumNames.hpp>
#include <Poseidon/Game/Commands/GameStateExt.hpp>
#include <Poseidon/Foundation/Strings/Mbcs.hpp>

extern void SDLGamepad_PlayRamp(float beg, float end, float dur);

namespace Poseidon
{
using namespace Foundation;
using Foundation::EnumName;
extern void SDLGamepad_SetEngine(float mag);

using namespace Dev;

static BankArray<RecoilFunction> RecoilFunctions;

inline float CalcHitDammage(float distance2, float valRange2)
{
    if (distance2 <= valRange2)
    {
        return 1;
    }
    else
    {
        return valRange2 * valRange2 / (distance2 * distance2);
    }
}

float EntityAI::DirectLocalHit(int component, float val)
{
    if (!_allowDammage)
    {
        return 0;
    }
    if (component < 0)
    {
        return 1;
    }
    // scan for corresponding hitpoint
    const HitPointList& hitpoints = GetType()->GetHitPoints();
    for (int i = 0; i < hitpoints.Size(); i++)
    {
        const HitPoint& hit = *hitpoints[i];
        if (!hit.IsConnectedCC(component))
        {
            continue;
        }

        float hitVal = val * hit.GetInvArmor();

        float oldHit = _hit[i];
        float newHit = oldHit + hitVal;
        saturateMin(newHit, 1);
        _hit[i] = newHit;

        return hit.GetPassThrough();
    }
    return 1;
}

void EntityAI::ChangeHit(int i, float newHit)
{
    float oldHit = _hit[i];
    saturateMin(newHit, 1);
    _hit[i] = newHit;
    if ((oldHit < 0.5 && newHit >= 0.5) || (oldHit < 0.7 && newHit >= 0.7) || (oldHit < 0.9 && newHit >= 0.9))
    {
        ShowDammage(i);
    }
}

float EntityAI::LocalHit(Vector3Par pos, float val, float valRange)
{
    if (!_allowDammage)
    {
        return 0;
    }
    // scan all hitpoints and dammage them
    Shape* hitShape = _shape->HitpointsLevel();
    if (!hitShape)
    {
        return 1;
    }

    Animate(_shape->FindHitpoints());

    if (valRange < 0)
    {
        // note: this change of local dammage model was done
        // to improve hit locality for soldiers
        // time showed in is suitable for all vehicles
        valRange *= 0.25; // smaller area around direct hit
                          // val *= 2; // but stronger effect
    }

    float valRange2 = Square(valRange);
    const HitPointList& hitpoints = GetType()->GetHitPoints();
    for (int i = 0; i < hitpoints.Size(); i++)
    {
        const HitPoint& hit = *hitpoints[i];
        int index = hit.GetSelection();
        if (index >= 0)
        {
            const NamedSelection& sel = hitShape->NamedSel(index);
            for (int j = 0; j < sel.Size(); j++)
            {
                int pIndex = sel[j];
                Vector3Val hitPt = hitShape->Pos(pIndex);
                float distance2 = hitPt.Distance2(pos);
                float dammage = val * CalcHitDammage(distance2, valRange2);
                if (dammage > 1e-4)
                {
                    float hitVal = dammage / hit.GetArmor();
                    ChangeHit(i, _hit[i] + hitVal);
                }
            }
        }
    }
    Deanimate(_shape->FindHitpoints());
    return GetType()->GetStructuralDammageCoef();
}

static bool FindCeaseFireInRadio(RadioChannel& radio, AIUnit* to)
{
    // check radio channel
    int index = INT_MAX;
    while (true)
    {
        RadioMessage* msg = radio.FindPrevMessage(RMTCeaseFire, index);
        if (!msg)
        {
            break;
        }
        AI_ERROR(dynamic_cast<RadioMessageCeaseFire*>(msg));
        RadioMessageCeaseFire* msgTyped = static_cast<RadioMessageCeaseFire*>(msg);
        AI_ERROR(msgTyped);
        if (msgTyped->GetTo() == to)
        {
            return true;
        }
    }

    {
        RadioMessage* msg = radio.GetActualMessage();
        if (msg && msg->GetType() == RMTCeaseFire)
        {
            AI_ERROR(dynamic_cast<RadioMessageCeaseFire*>(msg));
            RadioMessageCeaseFire* msgTyped = static_cast<RadioMessageCeaseFire*>(msg);
            AI_ERROR(msgTyped);
            if (msgTyped->GetTo() == to)
            {
                return true;
            }
        }
    }

    return false;
}

void EntityAI::ShowDammage(int part)
{
    if (part < 0 || part >= _hit.Size())
    {
        return;
    }
    RString name = HitpointName(part);
    OnEvent(EEDammaged, name, _hit[part]);
}

void EntityAI::HitBy(EntityAI* killer, float howMuch, RString ammo)
{
    if (IsLocal() && howMuch >= 0.05 && killer && !_isDead)
    {
        GStats.OnVehicleDamaged(this, killer, howMuch, ammo);
        GetNetworkManager().OnVehicleDamaged(this, killer, howMuch, ammo);
    }
    if (IsLocal() && howMuch >= 0.05 && !_isDead)
    {
        OnEvent(EEHit, killer, howMuch);
    }
    base::HitBy(killer, howMuch, ammo);
    AIGroup* g = GetGroup();
    if (g && killer)
    {
        AIUnit* killerUnit = killer->CommanderUnit();
        if (!killerUnit)
        {
            return;
        }
        AIGroup* killerGroup = killerUnit->GetGroup();
        // if we are dammaged by other unit of the same group,  do not react
        if (!killerGroup)
        {
            return;
        }
        AICenter* killerCenter = killerGroup->GetCenter();
        if (!killerCenter)
        {
            return;
        }
        if (killerGroup == g &&
            (!CommanderUnit() || CommanderUnit()->GetPerson()->GetExperience() >= ExperienceRenegadeLimit))
        {
            AIUnit* sender = CommanderUnit();
            if (howMuch >= 0.05 && killer != this && killerUnit->GetVehicleIn() != this &&
                (!sender || sender->GetVehicleIn() != killer))
            {
                Log("Friendly fire (in group): %s by %s (%s)", (const char*)GetDebugName(),
                    (const char*)killer->GetDebugName(), (const char*)killer->GetType()->GetName());
                RadioChannel& radio = g->GetRadio();
                if (!FindCeaseFireInRadio(radio, killerUnit))
                {
                    if (!sender || sender->GetPerson()->IsDammageDestroyed())
                    {
                        sender = g->Leader();
                    }
                    if (sender && !sender->GetPerson()->IsDammageDestroyed() && !sender->IsAnyPlayer())
                    {
                        radio.Transmit(new RadioMessageCeaseFire(sender, killerUnit, true),
                                       killerCenter->GetLanguage());
                    }
                }
            }
            return;
        }
        // reveal killer (at random position around us)
        Vector3 pos = Position() + Vector3(GRandGen.RandomValue() * 200 - 100, GRandGen.RandomValue() * 20 - 10,
                                           GRandGen.RandomValue() * 200 - 100);
        float accuracy = 0.1;
        float sideAcc = 1.5;
        float delay = 0.8f * GetInvAbility();
        float delayOtherUnits = 15;
        // reveal side of killer
        // reporting units knows almost immediatelly about the target
        // other units will notice it after a short delay
        g->AddTarget(killer, accuracy, sideAcc, delayOtherUnits, &pos, CommanderUnit(), delay);

        // do not disclose when killer is friendly
        AICenter* gCenter = g->GetCenter();
        if (!gCenter)
        {
            return;
        }
        if (gCenter->IsEnemy(killerCenter->GetSide()))
        {
            AIUnit* vehBrain = CommanderUnit();
            if (vehBrain)
            {
                vehBrain->Disclose();
                if (PilotUnit() && PilotUnit() != vehBrain)
                {
                    PilotUnit()->Disclose();
                }
                if (GunnerUnit() && GunnerUnit() != vehBrain)
                {
                    GunnerUnit()->Disclose();
                }
            }
        }
        else if (gCenter == killerCenter &&
                 (!CommanderUnit() || CommanderUnit()->GetPerson()->GetExperience() >= ExperienceRenegadeLimit))
        {
            AIUnit* sender = CommanderUnit();
            if (howMuch >= 0.05 && killer != this && killerUnit->GetVehicleIn() != this &&
                (!sender || sender->GetVehicleIn() != killer))
            {
                Log("Friendly fire: %s by %s (%s), dist %.1f", (const char*)GetDebugName(),
                    (const char*)killer->GetDebugName(), (const char*)killer->GetType()->GetName(),
                    killer->Position().Distance(Position()));
                RadioChannel& radio = gCenter->GetRadio();
                if (!FindCeaseFireInRadio(radio, killerUnit))
                {
                    if (!sender || sender->GetPerson()->IsDammageDestroyed())
                    {
                        sender = g->Leader();
                    }
                    if (sender && !sender->GetPerson()->IsDammageDestroyed() && !sender->IsAnyPlayer())
                    {
                        radio.Transmit(new RadioMessageCeaseFire(sender, killerUnit, false),
                                       killerCenter->GetLanguage());
                    }
                }
            }
        }
    }
}

void EntityAI::DoDammage(EntityAI* owner, Vector3Par pos, float val, float valRange, RString ammo)
{
    if (owner)
    {
        _lastDammage = owner;
        _lastDammageTime = Glob.time;
    }

    base::DoDammage(owner, pos, val, valRange, ammo);
}

bool EntityAI::IsDammageDestroyed() const
{
    if (_isDead)
    {
        return true;
    }
    return base::IsDammageDestroyed();
}

void EntityAI::Repair(float ammount)
{
    {
        // repair all hitpoints
        for (int i = 0; i < _hit.Size(); i++)
        {
            _hit[i] = 0;
        }
    }
    base::Repair(ammount);
}

void EntityAI::SetDammage(float dammage)
{
    if (!_allowDammage && dammage > GetTotalDammage())
    {
        return;
    }

    for (int i = 0; i < _hit.Size(); i++)
    {
        _hit[i] = dammage;
    }
    bool doDammage = GetTotalDammage() > dammage;
    base::SetDammage(dammage);
    if (doDammage)
    {
        ReactToDammage();
    }
    _isDead = false;
    _isDead = IsDammageDestroyed();
}

void EntityAI::ReactToDammage() {}

void EntityAI::Destroy(EntityAI* killer, float overkill, float minExp, float maxExp)
{
    if (IsLocal())
    {
        OnEvent(EEKilled, killer);
        GStats.OnVehicleDestroyed(this, killer);
        GetNetworkManager().OnVehicleDestroyed(this, killer);
    }
    base::Destroy(killer, overkill, minExp, maxExp);
    if (killer)
    {
        // use Entity member to get original target side
        // all dead bodies are considered civilian
        TargetSide origSide = Entity::GetTargetSide();

        // increase killer's experience
        AIUnit* kBrain = killer->CommanderUnit();
        if (kBrain)
        {
            kBrain->IncreaseExperience(*GetType(), origSide);
            // send radio message
            AIGroup* killerGroup = kBrain->GetGroup();
            if (killerGroup && killerGroup->GetCenter()->IsEnemy(origSide))
            {
                // find corresponding target
                Target* tar = killerGroup->FindTargetAll(this);
                if (tar)
                {
                    // mark killer
                    // when destroyed will be set, it will be marked for reporting
                    tar->idKiller = killer;
                }
            }
        }
        if (killer->GunnerUnit() && killer->GunnerUnit() != kBrain)
        {
            killer->GunnerUnit()->IncreaseExperience(*GetType(), origSide);
        }
        if (killer->PilotUnit() && killer->PilotUnit() != kBrain)
        {
            killer->PilotUnit()->IncreaseExperience(*GetType(), origSide);
        }
    }
}

/*!
\param creator player id of client where magazine was created
\param id unique id of magazine on given client
\return pointer to magazine or nullptr when not found
*/
const Magazine* EntityAI::FindMagazine(int creator, int id) const
{
    for (int i = 0; i < NMagazines(); i++)
    {
        const Magazine* magazine = GetMagazine(i);
        if (!magazine)
        {
            continue;
        }
        if (magazine->_creator == creator && magazine->_id == id)
        {
            return magazine;
        }
    }
    return nullptr;
}

const Magazine* EntityAI::FindMagazine(RString name) const
{
    Ref<MagazineType> type = MagazineTypes.New(name);
    const Magazine* magazine = nullptr;
    int ammo = 0;
    for (int i = 0; i < NMagazines(); i++)
    {
        const Magazine* m = GetMagazine(i);
        if (!m)
        {
            continue;
        }
        if (m->_type != type)
        {
            continue;
        }
        if (m->_ammo > ammo && !IsMagazineUsed(m))
        {
            magazine = m;
            ammo = m->_ammo;
        }
    }
    return magazine;
}

/*!
\param magazine checked magazine
\return true if magazine is used (loaded) in some weapon
*/
bool EntityAI::IsMagazineUsed(const Magazine* magazine) const
{
    if (magazine->_ammo <= 0)
    {
        return false;
    }

    for (int i = 0; i < NMagazineSlots(); i++)
    {
        if (GetMagazineSlot(i)._magazine == magazine)
        {
            return true;
        }
    }
    return false;
}

/*!
\param s index of slot reload to
\param m index of reloading magazine
\param afterAnimation true if reload immediatelly (no delay)
*/
bool EntityAI::ReloadMagazineTimed(int s, int m, bool afterAnimation)
{
    if (m >= NMagazines() || s >= NMagazineSlots())
    {
        return false;
    }

    Magazine* magazine = GetMagazine(m);
    if (!magazine)
    {
        return false;
    }

    MagazineSlot& slot = _magazineSlots[s];
    Magazine* oldMagazine = slot._magazine;
    if (oldMagazine == magazine)
    {
        // FIX: do not reload magazine with itself
        return false;
    }

    const MuzzleType* muzzle = slot._muzzle;
    if (!muzzle->CanUse(magazine->_type))
    {
        RptF("Cannot use magazine %s in muzzle %s", (const char*)magazine->_type->GetName(),
             (const char*)muzzle->GetName());
        return false;
    }

    if (magazine->_type->_modes.Size() != muzzle->_nModes)
    {
        LOG_ERROR(AI, "Error: Reload magazine {} into {} {}", (const char*)magazine->_type->GetName(),
                  (const char*)slot._weapon->GetName(), (const char*)muzzle->GetName());
        return false;
    }

    if (slot._mode < 0 || slot._mode >= magazine->_type->_modes.Size())
    {
        return false;
    }

    // destroy empty magazine
    if (oldMagazine && oldMagazine->_ammo == 0)
    {
        RemoveMagazine(oldMagazine);
    }

    // prepare magazine
    magazine->_reloadMagazine =
        afterAnimation ? 0 : muzzle->_magazineReloadTime * GetInvAbility() * GRandGen.PlusMinus(1, 0.2);
    const WeaponModeType* mode = magazine->_type->_modes[slot._mode];

    // vary reload time depending on skill in range 0..2

    float reloadAbility = (GetInvAbility() - 1) * 0.25 + 1;
    // compress reload ability to range 1..2

    magazine->_reload = mode->_reloadTime * reloadAbility * GRandGen.PlusMinus(1, 0.1);

    // change in all slots with curent muzzle
    for (int j = 0; j < _magazineSlots.Size(); j++)
    {
        MagazineSlot& slot = _magazineSlots[j];
        if (slot._muzzle == muzzle)
        {
            slot._magazine = magazine;
        }
    }
    return true;
}

/*!
\param muzzle reloaded muzzle
\param oldMagazineType prefered magazine type
\return index of magazine if found, otherwise -1
*/
int EntityAI::FindMagazineByType(const MuzzleType* muzzle, const MagazineType* oldMagazineType)
{
    // search for reserve magazine
    // first search the same type as old magazine
    if (oldMagazineType)
    {
        int ammoMax = 0;
        int jBest = -1;
        for (int j = 0; j < NMagazines(); j++)
        {
            Magazine* magazine = GetMagazine(j);
            if (magazine && magazine->_type == oldMagazineType && magazine->_ammo > ammoMax)
            {
                ammoMax = magazine->_ammo;
                jBest = j;
            }
        }
        if (jBest >= 0)
        {
            return jBest;
        }
    }
    // then other magazines fits into muzzle
    for (int i = 0; i < muzzle->_magazines.Size(); i++)
    {
        const MagazineType* type = muzzle->_magazines[i];
        if (type == oldMagazineType)
        {
            continue;
        }
        int ammoMax = 0;
        int jBest = -1;
        for (int j = 0; j < NMagazines(); j++)
        {
            Magazine* magazine = GetMagazine(j);
            if (magazine && magazine->_type == type && magazine->_ammo > ammoMax)
            {
                ammoMax = magazine->_ammo;
                jBest = j;
            }
        }
        if (jBest >= 0)
        {
            return jBest;
        }
    }
    return -1;
}

static Vector3 GetWeaponSoundPos(EntityAI* veh, int weapon)
{
    return veh->PositionModelToWorld(veh->GetWeaponPoint(weapon));
}

/*!
\param slotIndex magazine slot to reload
*/
bool EntityAI::ReloadMagazine(int slotIndex)
{
    const MagazineSlot& slot = GetMagazineSlot(slotIndex);
    const MuzzleType* muzzle = slot._muzzle;
    Magazine* oldMagazine = slot._magazine;

    const MagazineType* oldMagazineType = oldMagazine ? oldMagazine->_type : nullptr;

    int iMagazine = FindMagazineByType(muzzle, oldMagazineType);

    if (iMagazine < 0)
    {
        return false;
    }

    return ReloadMagazine(slotIndex, iMagazine);
}

/*!
\param slotIndex magazine slot to reload
\param iMagazine index of reloading magazine
*/
bool EntityAI::ReloadMagazine(int slotIndex, int iMagazine)
{
    return ReloadMagazineTimed(slotIndex, iMagazine, false);
}

/*!
\param weapon magazine slot index
\param muzzle reloading muzzle
*/
void EntityAI::PlayReloadMagazineSound(int weapon, const MuzzleType* muzzle)
{
    // make sound when reloaded
    const SoundPars& pars = muzzle->_reloadMagazineSound;
    if (pars.name.GetLength() > 0)
    {
        float rndFreq = GRandGen.RandomValue() * 0.1 + 0.95;
        IWave* wave = GSoundScene->OpenAndPlayOnce(pars.name, GetWeaponSoundPos(this, weapon), Speed(), pars.vol,
                                                   pars.freq * rndFreq);
        if (wave)
        {
            GSoundScene->SimulateSpeedOfSound(wave);
            GSoundScene->AddSound(wave);
            _reloadMagazineSound = wave;
        }
    }
}

bool EntityAI::WeaponSoundCacheStale(IWave* wave)
{
    // audio-invariants A-13: route cache-validity checks through the
    // single named helper so future FSM extensions (LoadError as a
    // distinct state, hardware-loss-revives-to-Created, etc.) don't
    // require chasing every call site.
    return audio::IsCacheStale(wave);
}

void EntityAI::PlayEmptyMagazineSound(int weapon)
{
    const MuzzleType* muzzle = _magazineSlots[weapon]._muzzle;
    const SoundPars& sound = muzzle->_sound;

    if (sound.name.GetLength() > 0)
    {
        float rndFreq = 1;
        float volume = sound.vol;
        float freq = sound.freq * rndFreq;
        _weaponFired.Access(weapon); // playing sound of the weapon
        IWave* wave = _weaponFired[weapon];
        if (WeaponSoundCacheStale(wave))
        {
            _weaponFired[weapon] = nullptr;
            wave = nullptr;
        }
        if (wave)
        {
            // tell sound it should stop
            wave->LastLoop();
            _weaponFired[weapon] = nullptr;
        }
        _weaponFiredTime.Access(weapon);
        _weaponFiredTime[weapon] = Glob.time;
        IWave* newWave =
            GSoundScene->OpenAndPlayOnce(sound.name, GetWeaponSoundPos(this, weapon), Speed(), volume, freq);
        if (newWave)
        {
            GSoundScene->SimulateSpeedOfSound(newWave);
            GSoundScene->AddSound(newWave);
            _weaponFired[weapon] = newWave;
        }
    }
}

void EntityAI::PlaceOnSurface(Matrix4& trans)
{
    base::PlaceOnSurface(trans);
}

bool EntityAI::AutoReload(int weapon)
{
    // this should be called whenever when:
    // slot is empty
    // there is some magazine that fits into the slot
    // such cases are:
    //   FireWeapon
    //   AddMagazine ????
    //   AddWeapon ????

    const MagazineSlot& slot = GetMagazineSlot(weapon);
    const MuzzleType* muzzle = slot._muzzle;
    if (muzzle->_autoReload || !GWorld->PlayerOn() || !GWorld->PlayerOn()->Brain() ||
        GWorld->PlayerOn()->Brain() != CommanderUnit())
    {
        return ReloadMagazine(weapon);
    }
    return false;
}

int EntityAI::AutoReloadAll()
{
    int ret = -1;
    for (int s = 0; s < NMagazineSlots();)
    {
        const MagazineSlot& slot = GetMagazineSlot(s);
        if (!slot._magazine && slot._muzzle)
        {
            if (AutoReload(s) && ret < 0)
            {
                ret = s;
            }
        }
        if (slot._muzzle)
        {
            s += slot._muzzle->_nModes;
        }
        else
        {
            s++;
        }
    }
    return ret;
}

void EntityAI::Draw(int level, ClipFlags clipFlags, const FrameBase& pos)
{
    base::Draw(level, clipFlags, pos);

    RString text;
    if (CommanderUnit())
    {
        text = CommanderUnit()->GetPerson()->GetInfo()._squadTitle;
    }

    if (text.GetLength() > 0)
    {
        GetType()->_squadTitles.Draw(level, clipFlags, pos, text);
    }
}

bool EntityAI::AimWeaponForceFire(int weapon)
{
    Vector3 dir = Direction();
    dir[1] = 3;
    dir.Normalize();
    return AimWeapon(_currentWeapon, dir);
}

void EntityAI::SimulateWeaponActivity(float deltaT, SimulationImportance prec)
{
    // reload all muzzles
    const Magazine* lastMag = nullptr;
    for (int i = 0; i < NMagazineSlots(); i++)
    {
        const MagazineSlot& slot = GetMagazineSlot(i);
        const MuzzleType* muzzle = slot._muzzle;
        Magazine* magazine = slot._magazine;
        if (magazine && magazine->_ammo > 0)
        {
            // reload each magazine once
            if (lastMag == magazine)
            {
                // same magazine in two magazine slots
                continue;
            }
#if 1
            bool found = false;
            for (int j = 0; j < i; j++)
            {
                if (GetMagazineSlot(j)._magazine == magazine)
                {
                    found = true;
                    Fail("Bad double magazine detection");
                    break;
                }
            }
            if (found)
            {
                continue;
            }
#endif
            lastMag = magazine;
            if (magazine->_reloadMagazine > 0)
            {
                magazine->_reloadMagazine -= deltaT;
                if (magazine->_reloadMagazine <= muzzle->_reloadMagazineSoundDuration && !_reloadMagazineSound)
                {
                    // make sound when reloaded
                    PlayReloadMagazineSound(i, muzzle);
                    saturateMax(magazine->_reloadMagazine, 0);
                }
            }
            else if (magazine->_reload > 0)
            {
                magazine->_reload -= deltaT;
                if (magazine->_reload <= muzzle->_reloadSoundDuration && !_reloadSound)
                {
                    // make sound when reloaded
                    const SoundPars& pars = muzzle->_reloadSound;
                    if (pars.name.GetLength() > 0)
                    {
                        float rndFreq = GRandGen.RandomValue() * 0.1 + 0.95;
                        IWave* wave =
                            GSoundScene->OpenAndPlayOnce(pars.name, Position(), Speed(), pars.vol, pars.freq * rndFreq);
                        if (wave)
                        {
                            GSoundScene->SimulateSpeedOfSound(wave);
                            GSoundScene->AddSound(wave);
                            _reloadSound = wave;
                        }
                    }
                    saturateMax(magazine->_reload, 0);
                }
            }
        }
    }

    // simulate shooting visibility
    _shootTimeRest -= deltaT;
    if (_shootTimeRest < 0)
    {
        // return to default
        _shootTimeRest = 1e10;
        _shootVisible = 0;
        _shootAudible = 0;
    }

    if (_forceFireWeapon >= 0 && !_isDead)
    {
        if (_forceFireWeapon == SelectedWeapon())
        {
            bool aimed = AimWeaponForceFire(_currentWeapon);
            if (GetWeaponLoaded(_currentWeapon) && (GetWeaponDirection(_currentWeapon).Y() >= 0.7 || aimed))
            {
                const WeaponModeType* mode = GetWeaponMode(_currentWeapon);
                if (mode && mode->_useAction)
                {
                    UIAction action;
                    action.type = ATUseWeapon;
                    action.target = this;
                    action.param = _currentWeapon;
                    action.param2 = 0;
                    action.priority = 0;
                    action.showWindow = false;
                    action.hideOnUse = false;
                    StartActionProcessing(action, GunnerUnit());
                    _forceFireWeapon = -1;
                }
                else
                {
                    if (FireWeapon(_currentWeapon, nullptr))
                    {
                        _forceFireWeapon = -1;
                    }
                }
            }
        }
    }

    AIUnit* unit = GWorld->FocusOn();
    if (unit && unit == CommanderUnit() && !unit->IsInCargo() && !GWorld->GetCameraEffect())
    {
        if (InputSubsystem::Instance().GetActionToDo(UAHeadlights))
        {
            _pilotLight = !_pilotLight;
        }
    }

    // reflectors
    for (int i = 0; i < _reflectors.Size(); i++)
    {
        Light* light = _reflectors[i];
        const ReflectorInfo& info = GetType()->_reflectors[i];
        bool on = _pilotLight && GetHit(info.hitPoint) < 0.9;
        light->Switch(on);
    }

    // advance time for all looping weapon sounds
    for (int i = 0; i < _weaponFired.Size(); i++)
    {
        IWave* wave = _weaponFired[i];
        if (wave)
        {
            if (WeaponSoundCacheStale(wave))
            {
                _weaponFired[i] = nullptr;
                continue;
            }
            float time = Glob.time - _weaponFiredTime[i];
            wave->SetStopValue(time);
            // send termination request when wave is no longer needed
            // this is difficult, beacause only Link is stored here
        }
    }
}

void EntityAI::IsMoved()
{
    // move condition detected
    _lastMovement = Glob.time;
    CancelStop();
}
void EntityAI::StopDetected()
{
    // stop condition detected
    if (Glob.time > _lastMovement + TimeToStop())
    {
        Stop();
        _speed = VZero;
        _angMomentum = VZero;
    }
}

void EntityAI::SwitchLight(bool on)
{
    for (int i = 0; i < _reflectors.Size(); i++)
    {
        Light* light = _reflectors[i];
        light->Switch(on);
    }
}

void EntityAI::Sound(bool inside, float deltaT)
{
    for (int i = 0; i < _weaponFired.Size(); i++)
    {
        IWave* wave = _weaponFired[i];
        if (WeaponSoundCacheStale(wave))
        {
            _weaponFired[i] = nullptr;
            continue;
        }
        wave->SetPosition(GetWeaponSoundPos(this, i), Speed());
    }
}

void EntityAI::UnloadSound()
{
    // reset all sounds
    _weaponFired.Clear();
    _weaponFiredTime.Clear();
}

void EntityAI::StartRecoilFF()
{
    if (_recoil)
    {
        RecoilFFRamp ramp;
        _recoil->GetFFRamp(_recoilFFIndex, ramp);

        float ffCoef = 10;
        float begMag = ramp._begAmplitude * _recoilFactor * ffCoef;
        float endMag = ramp._endAmplitude * _recoilFactor * ffCoef;
        SDLGamepad_PlayRamp(begMag, endMag, ramp._duration);
    }
    else
    {
        SDLGamepad_PlayRamp(0, 0, 0);
    }
}

void EntityAI::OnRecoilAbort() {}

float EntityAI::GetRecoilFactor() const
{
    return 0.5f;
}

void EntityAI::OnAddImpulse(Vector3Par force, Vector3Par torque)
{
    // construct artificial recoil effect based on impulse
    // offset is based on force?
    // angle on torque?
    float factor = fabs(force.Z() * GetInvMass()) * 0.4f;

    static RStringB impulse = "impulse";
    Ref<RecoilFunction> recoil = RecoilFunctions.New(impulse);
    saturate(factor, 0, 1);
    StartRecoil(recoil, factor * GetRecoilFactor());
}

void EntityAI::StartRecoil(RecoilFunction* recoil, float recoilFactor)
{
    if (recoil->GetTerminated(0))
    {
        // no need to start recoil that has already been terminated
        return;
    }
    if (_recoil)
    {
        if (recoilFactor < _recoilFactor)
        {
            // current recoil is stronger - prefer it
            return;
        }
        OnRecoilAbort();
    }
    _recoil = recoil;
    _recoilTime = 0;
    _recoilFactor = recoilFactor;
    _recoilFFIndex = 0;
    if (this == GWorld->CameraOn())
    {
        StartRecoilFF();
    }
}

void EntityAI::Simulate(float deltaT, SimulationImportance prec)
{
    if (_shape && !_static) // do not lock buildings
    {
        if (HasGeometry() && (_isStopped || _speed.SquareSize() < Square(1.5)) &&
            (!Airborne() || // lock for vehicle that are not airborne
                            // lock also for low static airborne vehicle
             Position().Y() < GLandscape->SurfaceYAboveWater(Position().X(), Position().Z()) + 15))
        {
            LockPosition();
        }
        else
        {
            UnlockPosition();
        }
    }
    float delta;
    delta = _avoidAsideWanted - _avoidAside;
    if (fabs(_avoidAsideWanted) > fabs(_avoidAside))
    {
        saturate(delta, -20 * deltaT, +20 * deltaT);
    }
    else
    {
        saturate(delta, -0.2 * deltaT, +0.2 * deltaT);
    }
    _avoidAside += delta;

    SimulateWeaponActivity(deltaT, prec);

    // simulate flag
    Texture* texture = GetFlagTexture();
    if (texture)
    {
        Shape* sShape = _shape->LevelOpaque(0);
        if (sShape)
        {
            for (int i = 0; i < sShape->NProxies(); i++)
            {
                const ProxyObject& proxy = sShape->Proxy(i);
                Entity* veh = dyn_cast<Entity>(proxy.obj.GetRef());
                if (veh)
                {
                    LODShapeWithShadow* lodShape = veh->GetShape();
                    if (strnicmp(lodShape->Name(), "data3d\\flag_", 12) == 0)
                    {
                        Matrix4 proxyTransform = AnimateProxyMatrix(0, proxy);

                        Matrix4Val pTransform = Transform() * proxyTransform;

                        Vector3 speed(VZero);
                        if (_flag)
                        {
                            speed = (1.0 / deltaT) * (pTransform.Position() - _flag->Position());
                        }
                        else
                        {
                            _flag = dyn_cast<Flag>(NewNonAIVehicle(veh->GetNonAIType()->GetName(), lodShape->Name()));
                            _flag->Init(pTransform);
                        }
                        // proxy may be animation
                        _flag->SetTransform(pTransform);
                        _flag->SetSpeed(speed);
                        _flag->FlagSimulate(pTransform, deltaT, prec);
                    }
                }
            }
        }
    }
    else
    {
        if (_flag)
        {
            _flag = nullptr;
        }
    }

    // simulate burst fire
    if (IsLocal())
    {
        int weapon = _currentWeapon;
        if (weapon >= 0 && weapon < NMagazineSlots())
        {
            const Magazine* magazine = GetMagazineSlot(weapon)._magazine;
            if (magazine && magazine->_burstLeft > 0)
            {
                // fire on same target as before
                FireWeapon(weapon, _shootTarget);
            }
        }
    }

    if (_recoil)
    {
        _recoilTime += deltaT;

        if (this == GWorld->CameraOn())
        {
            // force feedback simulation is necessary
            RecoilFFRamp ramp;
            _recoil->GetFFRamp(_recoilFFIndex, ramp);
            // check if we need to advance recoil index
            if (_recoilTime >= ramp._startTime + ramp._duration)
            {
                _recoilFFIndex++;
                StartRecoilFF();
            }
        }
        if (_recoil->GetTerminated(_recoilTime))
        {
            _recoil.Free();
            if (this == GWorld->CameraOn())
            {
                StartRecoilFF();
            }
        }
    }
    if (IsLocal())
    {
        if (!_laserTargetOn)
        {
            StopLaser();
        }
        else
        {
            TrackLaser(_currentWeapon);
        }
    }
}

void EntityAI::AddDefaultWeapons() {}

void EntityAI::Init(Matrix4Par pos)
{
    // called before vehicle is placed
    if (_flag)
    {
        Matrix4 trans = pos * _flag->Transform();
        _flag->Init(trans);
    }
    OnEvent(EEInit);
}

void EntityAI::InitUnits()
{
    // called atfer vehicle is placed in landscape
    // unit are setup and initialized
}

/*\deprecated Currently not used*/

Texture* EntityAI::GetSideSign() const
{
    if (_targetSide == TEast)
    {
        return GLOB_SCENE->Preloaded(SignSideE);
    }
    else if (_targetSide == TWest)
    {
        return GLOB_SCENE->Preloaded(SignSideW);
    }
    else if (_targetSide == TGuerrila)
    {
        return GLOB_SCENE->Preloaded(SignSideG);
    }
    return nullptr; // neutral, guerilla .. etc
}

// all vehicles are supposed to be animated unless explicitly overridden
bool EntityAI::IsAnimated(int level) const
{
    return true;
}
bool EntityAI::IsAnimatedShadow(int level) const
{
    return true;
}

void EntityAI::Animate(int level)
{
    Shape* shape = _shape->Level(level);
    if (!shape)
    {
        return;
    }
    base::Animate(level);
    const EntityAIType* type = GetType();
    Texture* tex;

    if (CommanderUnit())
    {
        tex = CommanderUnit()->GetPerson()->GetInfo()._squadPicture;
    }
    else
    {
        tex = nullptr;
    }
    if (tex != _squadTexture)
    {
        // ?? unregister _squadTexture ??
        if (tex)
        {
            _shape->RegisterTexture(tex, type->_clan);
        }
        _squadTexture = tex;
    }
    type->_clan.SetTexture(_shape, level, tex);
    if (tex)
    {
        type->_clan.Unhide(_shape, level);
    }
    else
    {
        type->_clan.Hide(_shape, level);
    }

    if (_pilotLight)
    {
        type->_backLights.Unhide(_shape, level);
    }
    else
    {
        type->_backLights.Hide(_shape, level);
    }

    // reflectors
    for (int i = 0; i < _reflectors.Size(); i++)
    {
        const ReflectorInfo& info = type->_reflectors[i];
        bool on = _pilotLight && GetHit(info.hitPoint) < 0.9;
        if (on)
        {
            info.selection.Unhide(_shape, level);
        }
        else
        {
            info.selection.Hide(_shape, level);
        }
    }

    // hidden selections
    for (int i = 0; i < type->_hiddenSelections.Size(); i++)
    {
        if (_hiddenSelectionsTextures[i])
        {
            type->_hiddenSelections[i].Unhide(_shape, level);
            type->_hiddenSelections[i].SetTexture(_shape, level, _hiddenSelectionsTextures[i]);
        }
        else
        {
            type->_hiddenSelections[i].Hide(_shape, level);
        }
    }

    // reload animations
    if (_currentWeapon >= 0)
    {
        const MagazineSlot& s = GetMagazineSlot(_currentWeapon);
        const WeaponType* weapon = s._weapon;
        const Magazine* magazine = s._magazine;
        const MagazineType* magazineType = magazine ? magazine->_type : nullptr;
        if (weapon && magazineType && magazineType->_modes.Size() > 0)
        {
            AnimationType* animation = nullptr;
            float multiplier = 0;
            for (int i = 0; i < type->_reloadAnimations.Size(); i++)
            {
                if (type->_reloadAnimations[i].weapon == weapon)
                {
                    animation = type->_reloadAnimations[i].animation;
                    multiplier = type->_reloadAnimations[i].multiplier;
                    break;
                }
            }
            if (animation && animation->GetSelection(level) >= 0)
            {
                float reload = magazine->_reload / magazineType->_modes[0]->_reloadTime;
                saturate(reload, 0, 1);
                float coef = (magazine->_ammo + reload) / magazineType->_maxAmmo;
                Matrix4 baseAnim = MIdentity;
                AnimateMatrix(baseAnim, level, animation->GetSelection(level));
                animation->Animate(_shape, level, fastFmod(multiplier * coef, 1), baseAnim);
            }
        }
    }
}

void EntityAI::Deanimate(int level)
{
    Shape* shape = _shape->Level(level);
    if (!shape)
    {
        return;
    }
    const EntityAIType* type = GetType();
    type->_unitNumber.SetTexture(_shape, level, nullptr);
    type->_groupSign.SetTexture(_shape, level, nullptr);
    type->_sectorSign.SetTexture(_shape, level, nullptr);
    type->_clan.SetTexture(_shape, level, nullptr);

    // hidden selections
    for (int i = 0; i < type->_hiddenSelections.Size(); i++)
    {
        type->_hiddenSelections[i].Unhide(_shape, level);
    }

    base::Deanimate(level);
}

float EntityAI::GetAimed(int weapon, Target* target) const
{
    if (weapon < 0)
    {
        return 0;
    }
    if (!target)
    {
        return 0;
    }
    if (!target->idExact)
    {
        return 0;
    }
    if (target->lastSeen < Glob.time - 5)
    {
        return 0;
    }

    float visible = _visTracker.Value(this, _currentWeapon, target->idExact, 0.9);
    const Magazine* magazine = GetMagazineSlot(weapon)._magazine;
    const MagazineType* aInfo = magazine ? magazine->_type : nullptr;
    const WeaponModeType* mode = GetWeaponMode(weapon);

    const AmmoType* ammo = mode ? mode->_ammo : nullptr;
    if (!ammo)
    {
        return 1;
    }
    if (target->posError.SquareSize() > Square(ammo->indirectHitRange * 2))
    {
        return 0;
    }

    // decrease landscape occlussion significance
    visible = 1 - (1 - visible) * 0.3f;

    Vector3 ap = target->AimingPosition();
    if (ammo && ammo->_simulation != AmmoShotMissile)
    {
        // predict shot result
        float dist = ap.Distance(Position());
        float time = dist * aInfo->_invInitSpeed;
        Vector3 estPos = ap + target->speed * time;
        Vector3 wDir = GetWeaponDirection(weapon);
        Vector3 wPos = PositionModelToWorld(GetWeaponPoint(weapon));
        float eDist = wPos.Distance(estPos);
        Vector3 hit = wPos + wDir * eDist;
        hit[1] -= G_CONST * time * time * 0.5;
        Vector3 hError = hit - estPos;
        hError[1] *= 2;
        float error = hError.Size() * 0.5;

        float tgtSize = target->idExact->GetShape()->GeometrySphere();
        float maxError = tgtSize * 0.7 + mode->_ammo->indirectHitRange * 0.3;
        maxError += dist * mode->_dispersion;

        if (mode->_ammo->_simulation != AmmoShotBullet)
        {
            maxError *= 2;
        }

#if _ENABLE_CHEATS
        if ((Object*)this == GWorld->CameraOn() && CHECK_DIAG(DECombat))
        {
            GlobalShowMessage(2000, "Error %.1f, tgtSize %.1f, maxError %.1f", error, tgtSize, maxError);
        }
#endif
        return (error < maxError) * visible;
    }
    else
    {
        Vector3 relPos = ap - PositionModelToWorld(GetWeaponPoint(weapon));
        // check if target is in front of us
        if (relPos.SquareSize() <= Square(30))
        {
            return 0; // missile fire impossible
        }
        // check if target position is withing missile lock cone
        Vector3 wDir = GetWeaponDirection(weapon);
        float wepCos = relPos.CosAngle(wDir);
        float wepAimed = 1 - (1 - wepCos) * 30;
        saturateMax(wepAimed, 0);
        return visible * wepAimed;
    }
}

typedef StaticArrayAuto<OLink<EntityAI>> EntityAIList;

static void CheckNearFriendlies(EntityAIList& res, const EntityAI* from, const EntityAI* target, Vector3Par dir,
                                float maxDistance, float maxAside)
{
    // check all near friendly troops
    // do not check myself and target
    // collect information about near troops
    int xMin, xMax, zMin, zMax;
    Vector3Val fromPos = from->Position();
    ObjRadiusRectangle(xMin, xMax, zMin, zMax, fromPos, fromPos, maxDistance);
    AIGroup* grp = from->GetGroup();
    if (!grp)
    {
        return;
    }
    AICenter* center = grp->GetCenter();
    if (!center)
    {
        return;
    }

    for (int z = zMin; z <= zMax; z++)
    {
        for (int x = xMin; x <= xMax; x++)
        {
            const ObjectList& list = GLandscape->GetObjects(z, x);
            for (int oi = 0; oi < list.Size(); oi++)
            {
                Object* obj = list[oi];
                Vector3Val tgtPos = obj->Position();
                if (tgtPos.Distance2(fromPos) > Square(maxDistance))
                {
                    continue;
                }
                EntityAI* tgt = dyn_cast<EntityAI>(obj);
                if (!tgt)
                {
                    continue;
                }
                if (tgt == target || tgt == from)
                {
                    continue;
                }
                TargetSide side = tgt->GetTargetSide();
                if (!center->IsFriendly(side))
                {
                    continue;
                }
                if (tgt->IsDammageDestroyed())
                {
                    continue;
                }

                // calculate asside distance
                // this is distance of tgt from line fromPos,dir

                Vector3 tgtAimPos = tgt->AimingPosition();
                // find nearest point on line fromPos,dir to tgtAimPos
                float t = dir * (tgtAimPos - fromPos);
                // t is front-back distance from fromPos to tgtAimPos
                Vector3 nearest = fromPos + dir * t;
                if (t < -maxAside * 0.5f)
                {
                    continue;
                }
                if (nearest.Distance2(tgtAimPos) > Square(maxAside))
                {
                    continue;
                }
                res.Add(tgt);
            }
        }
    }
}

bool EntityAI::CheckFriendlyFire(int weapon, Target* target) const
{
    const WeaponModeType* mode = GetWeaponMode(weapon);
    // if we have no ammo, we can safely fire, we can do no harm
    if (!mode)
    {
        return true;
    }
    const AmmoType* ammo = mode->_ammo;
    if (!ammo)
    {
        return true;
    }

    switch (ammo->_simulation)
    {
        case AmmoShotBullet:
        {
            AUTO_STATIC_ARRAY(OLink<EntityAI>, nearFriendlies, 32);
            // check near part of bullet trajectory
            Vector3Par dir = GetWeaponDirection(weapon);
            float distToTarget = target->AimingPosition().Distance(Position());
            // if target is very near, be less carefull about friendly fire
            float radius = 2.0f;
            float maxDist = 50;
            if (distToTarget < maxDist)
            {
                maxDist = distToTarget;
                radius = 1.0f;
                if (distToTarget < 25)
                {
                    radius = 0.5f;
                }
            }
            CheckNearFriendlies(nearFriendlies, this, target->idExact, dir, maxDist, radius);
            if (nearFriendlies.Size() > 0)
            {
                // some friendly unit blocks fire - we have to wait
                return false;
            }
        }
    }

    // default: unknown weapon - assume it is safe to fire
    return true;
}

float EntityAI::GetFormationTime() const
{
    return GetType()->GetFormationTime();
}
float EntityAI::GetInvFormationTime() const
{
    return GetType()->GetInvFormationTime();
}

/*!
Armored vehicles do not care much for collisions, but
for soft vehicles, air vehicles or men collision can do much dammage
or it can be even critical.
*/

float EntityAI::AfraidOfCollision(VehicleKind with) const
{
    switch (GetType()->GetKind())
    {
        case VArmor:
            return 1;
        case VSoft:
            if (with == VSoft)
            {
                return 1;
            }
            return 2;
        default:
            return 4; // be very carefull
    }
}

/*!
    For most entities this is very low (0-5 m),
    as land vehicles or men move on the ground.
    This is significant for aerial vehicles.
*/

float EntityAI::GetCombatHeight() const
{
    return -_shape->Min()[1];
}
float EntityAI::GetMinCombatHeight() const
{
    return -_shape->Min()[1];
}

float EntityAI::GetMaxCombatHeight() const
{
    return -_shape->Min()[1];
}

void EntityAI::AimWeaponManDir(int weapon, Vector3Par direction)
{
    AimWeapon(weapon, direction);
}

bool EntityAI::AimWeapon(int weapon, Target* target)
{
    Vector3 dir;
    bool ret = CalculateAimWeapon(weapon, dir, target);
    if (!ret)
    {
        return false;
    }
    return AimWeapon(weapon, dir);
}

bool EntityAI::AimObserver(Target* target)
{
    Vector3 dir;
    bool ret = CalculateAimObserver(dir, target);
    if (!ret)
    {
        return false;
    }
    return AimObserver(dir);
}

/*!
This functions is used for manual units when UI is reporting cursor position
when using mouse controls.
*/

void EntityAI::AimDriver(Vector3Par direction) {}

/*!
This function is used for manual units
to adjust weapon depending on current field of view.
Example of such weapon is M21 sniper rifle.
*/

void EntityAI::AdjustWeapon(int weapon, CameraType camType, float fov, Vector3& camDir)
{
    // default is no adjustment
}

// if necessary, ask server to aim weapon
void EntityAI::AskForAimWeapon(int weapon, Vector3Val dir)
{
    if (weapon < 0)
    {
        return;
    }
    if (_aimWeaponAsked.Size() <= weapon)
    {
        int oldSize = _aimWeaponAsked.Size();
        _aimWeaponAsked.Access(weapon);
        for (int i = oldSize; i < _aimWeaponAsked.Size(); i++)
        {
            _aimWeaponAsked[i] = VZero;
        }
    }
    Vector3& lastRequest = _aimWeaponAsked[weapon];
    if (dir.Distance2(lastRequest) > Square(0.01))
    {
        lastRequest = dir;
        GetNetworkManager().AskForAimWeapon(this, weapon, dir);
    }
}

// if necessary, ask server to aim observer
void EntityAI::AskForAimObserver(Vector3Val dir)
{
    if (dir.Distance2(_aimObserverAsked) > Square(0.01))
    {
        _aimObserverAsked = dir;
        GetNetworkManager().AskForAimObserver(this, dir);
    }
}

Vector3 EntityAI::GetEyeDirectionWanted() const
{
    return GetEyeDirection();
}

Vector3 EntityAI::GetWeaponDirectionWanted(int weapon) const
{
    return GetWeaponDirection(weapon);
}

/*!
\return
This direction returned is typical the head (eye) direction
or direction of observer turret when appropriate.
*/

Vector3 EntityAI::GetEyeDirection() const
{
    if (NWeaponSystems() <= 0)
    {
        return Direction();
    }
    return GetWeaponDirection(0);
}

Vector3 EntityAI::GetWeaponDirection(int weapon) const
{
    // in world coordinates
    return Direction();
}

Vector3 EntityAI::GetWeaponCenter(int weapon) const
{
    // in model coordinates
    return VZero;
}

Vector3 EntityAI::GetWeaponPoint(int weapon) const
{
    return GetWeaponCenter(weapon);
}

bool EntityAI::GetWeaponCartridgePos(int weapon, Matrix4& pos, Vector3& vel) const
{
    if (weapon >= 0 && weapon < NMagazineSlots())
    {
        const MuzzleType* muzzle = GetMagazineSlot(weapon)._muzzle;

        if (muzzle->_cartridgeOutPosIndex < 0 || muzzle->_cartridgeOutEndIndex < 0)
        {
            return false;
        }
        int memLevel = _shape->FindMemoryLevel();

        Vector3 cPos = AnimatePoint(memLevel, muzzle->_cartridgeOutPosIndex);
        Vector3 cEnd = AnimatePoint(memLevel, muzzle->_cartridgeOutEndIndex);

        Vector3Val cDir = GetWeaponDirection(weapon);

        PositionModelToWorld(cPos, cPos);
        PositionModelToWorld(cEnd, cEnd);

        vel = (cEnd - cPos) * 3;

        // create matrix for cartridge position
        pos.SetDirectionAndUp(cDir, VUp);
        pos.SetPosition(cPos);

        return true;
    }
    // convert neutral gun position

    pos = MIdentity;
    vel = VZero;

    return false;
}

bool EntityAI::GetWeaponLoaded(int weapon) const
{
    const Magazine* magazine = GetMagazineSlot(weapon)._magazine;
    if (!magazine)
    {
        return true; // no ammo
    }
    const WeaponModeType* mode = GetWeaponMode(weapon);
    if (!mode)
    {
        return true; // bad weapon
    }
    if (!mode->_ammo)
    {
        return true; // no ammo
    }
    if (mode->_ammo->_simulation == AmmoNone)
    {
        return true; // no ammo
    }
    if (magazine->_reload > 0)
    {
        return false; // not loaded
    }
    if (magazine->_reloadMagazine > 0)
    {
        return false; // not loaded
    }
    if (IsActionInProgress(MFReload))
    {
        return false; // not loaded
    }

    if (magazine->_ammo <= 0)
    {
        return false; // no ammo
    }
    return true;
}

bool EntityAI::GetWeaponReady(int weapon, Target* target) const
{
    const WeaponModeType* mode = GetWeaponMode(weapon);
    if (!mode)
    {
        return false;
    }
    if (!target)
    {
        return true;
    }
    float timeFromLastShot = Glob.time - _lastShotTime;
    float rateOfFire = mode->_aiRateOfFire;
    if (timeFromLastShot > rateOfFire)
    {
        return true;
    }
    float dist2 = target->AimingPosition().Distance2(Position());
    float maxDist2 = Square(mode->_aiRateOfFireDistance);
    if (dist2 < maxDist2)
    {
        return Square(timeFromLastShot) * maxDist2 > Square(rateOfFire) * dist2;
    }
    else
    {
        return timeFromLastShot > rateOfFire;
    }
}

bool EntityAI::FireMissile(int weapon, Vector3Par offset, Vector3Par direction, Vector3Par initSpeed,
                           TargetType* target)
{
    if (_isDead || _isUpsideDown)
    {
        return false;
    }
    const WeaponModeType* mode = GetWeaponMode(weapon);
    const AmmoType* type = mode ? mode->_ammo : nullptr;

    _fire._nextWeaponSwitch = Glob.time + GetInvAbility() * 0.5;

    if (!EffectiveGunnerUnit()->GetPerson()->IsLocal())
    {
        // remote vehicles do not shot
        return true;
    }

    if (target && target->IsEventHandler(EEIncomingMissile))
    {
        target->OnEvent(EEIncomingMissile, type->GetName(), this);
        GetNetworkManager().OnIncomingMissile(target, type->GetName(), this);
    }

    Entity* shot = NewShot(this, type, target);
    Vector3 wDirection(NoInit);
    DirectionModelToWorld(wDirection, direction.Normalized());
    shot->SetOrient(wDirection, VUp);
    shot->SetSpeed(_speed + DirectionModelToWorld(initSpeed));
    shot->SetPosition(PositionModelToWorld(offset));
    GLOB_WORLD->AddFastVehicle(shot);

    if (GWorld->GetMode() == GModeNetware)
    {
        // shot created
        GetNetworkManager().CreateVehicle(shot, VLTFast, "", -1);
    }
#if _ENABLE_CHEATS
    if (this == GWorld->CameraOn() && CHECK_DIAG(DECombat))
    {
        GWorld->SwitchCameraTo(shot, CamInternal);
    }
#endif

    AIUnit* unit = CommanderUnit();
    Target* assigned = unit ? unit->GetTargetAssigned() : nullptr;
    if (assigned && target == assigned->idExact)
    {
        _lastShotAtAssignedTarget = Glob.time;
    }
    _lastShot = shot;

    return true;
}

bool EntityAI::FireShell(int weapon, Vector3Par offset, Vector3Par direction, TargetType* target)
{
    // fire
    if (_isDead || _isUpsideDown)
    {
        return false;
    }

    _fire._nextWeaponSwitch = Glob.time + GetInvAbility() * 0.5;

    const Magazine* magazine = GetMagazineSlot(weapon)._magazine;
    if (!magazine)
    {
        return false;
    }
    const MagazineType* aInfo = magazine->_type;
    const WeaponModeType* mode = GetWeaponMode(weapon);
    AI_ERROR(mode);
    const AmmoType* type = mode->_ammo;
    if (!type)
    {
        return false;
    }

    if (!EffectiveGunnerUnit()->GetPerson()->IsLocal())
    {
        // remote vehicles do not shot
        return true;
    }

    Entity* shot = NewShot(this, type, nullptr);
    Vector3 wDirection(NoInit);
    DirectionModelToWorld(wDirection, direction);
    shot->SetOrient(wDirection, VUp);
    // do random dispersion of projectiles
    const int gausPassNum = 4;
    float gaussX = 0, gaussY = 0;
    for (int c = gausPassNum; --c >= 0;)
    {
        gaussX += GRandGen.RandomValue();
        gaussY += GRandGen.RandomValue();
    }
    gaussX *= 2.0 / gausPassNum;
    gaussY *= 2.0 / gausPassNum;

    float randomX = (gaussX - 1) * mode->_dispersion;
    float randomY = (gaussY - 1) * mode->_dispersion;

    if (QIsManual())
    {
        // IF_FADE block removed - was always dead code with _DISABLE_CRC_PROTECTION=1
    }

    Vector3 dir(randomX, randomY, 1);
    shot->DirectionModelToWorld(dir, dir);
    shot->SetOrient(dir, shot->DirectionUp());
    // final speed and position
    shot->SetSpeed(_speed + shot->Direction() * aInfo->_initSpeed);
    shot->SetPosition(PositionModelToWorld(offset));
    GLOB_WORLD->AddFastVehicle(shot);

    if (GWorld->GetMode() == GModeNetware)
    {
        // shot created
        GetNetworkManager().CreateVehicle(shot, VLTFast, "", -1);
    }

    AIUnit* unit = CommanderUnit();
    Target* assigned = unit ? unit->GetTargetAssigned() : nullptr;
    if (assigned && target == assigned->idExact)
    {
        _lastShotAtAssignedTarget = Glob.time;
    }
    _lastShot = shot;

    return true;
}

bool EntityAI::FireMGun(int weapon, Vector3Par offset, Vector3Par direction, TargetType* target)
{
    if (_isDead || _isUpsideDown)
    {
        return false;
    }

    // fire
    const MagazineSlot& slot = GetMagazineSlot(weapon);
    const Magazine* magazine = slot._magazine;
    if (!magazine)
    {
        return false;
    }
    const MagazineType* aInfo = magazine->_type;
    if (!aInfo || slot._mode < 0 || slot._mode >= aInfo->_modes.Size())
    {
        return false;
    }
    const WeaponModeType* mode = aInfo->_modes[slot._mode];
    const AmmoType* type = mode->_ammo;
    if (!type)
    {
        return false;
    }
    const MuzzleType& info = *slot._muzzle;

    if (!EffectiveGunnerUnit()->GetPerson()->IsLocal())
    {
        // remote vehicles do not shot
        return true;
    }

    Entity* shell = NewShot(this, type, nullptr);
    Vector3 wDirection(NoInit);
    DirectionModelToWorld(wDirection, direction);
    shell->SetOrient(wDirection, VUp);
    // do random dispersion of projectiles
    const int gausPassNum = 4;
    float gaussX = 0, gaussY = 0;
    for (int c = gausPassNum; --c >= 0;)
    {
        gaussX += GRandGen.RandomValue();
        gaussY += GRandGen.RandomValue();
    }
    gaussX *= 2.0 / gausPassNum;
    gaussY *= 2.0 / gausPassNum;
    float randomX = (gaussX - 1) * (mode->_dispersion);
    float randomY = (gaussY - 1) * (mode->_dispersion);
    if (!QIsManual())
    {
        randomX *= info._aiDispersionCoefX;
        randomY *= info._aiDispersionCoefY;
    }
    else
    {
        // IF_FADE block removed - was always dead code with _DISABLE_CRC_PROTECTION=1
    }

    _fire._nextWeaponSwitch = Glob.time + GetInvAbility() * 0.5;

    Vector3 dir(randomX, randomY, 1);
    shell->DirectionModelToWorld(dir, dir);
    shell->SetOrient(dir, shell->DirectionUp());
    // final speed and position
    shell->SetSpeed(_speed + dir * aInfo->_initSpeed);
    shell->SetPosition(PositionModelToWorld(offset));
    GLOB_WORLD->AddFastVehicle(shell);

    if (GWorld->GetMode() == GModeNetware)
    {
        // shot created
        GetNetworkManager().CreateVehicle(shell, VLTFast, "", -1);
    }

    AIUnit* unit = CommanderUnit();
    Target* assigned = unit ? unit->GetTargetAssigned() : nullptr;
    if (assigned && target == assigned->idExact)
    {
        _lastShotAtAssignedTarget = Glob.time;
    }
    _lastShot = shell;

    return true;
}

bool EntityAI::CalculateLaser(Vector3& pos, Vector3& dir, int weapon) const
{
    dir = GetWeaponDirection(weapon);
    dir.Normalize();
    Vector3 weaponPos = PositionModelToWorld(GetWeaponPoint(weapon));

    // calculate intersection with land
    // between Position() and position
    float maxDist = ENGINE_CONFIG.horizontZ;

    float t = GLandscape->IntersectWithGroundOrSea(&pos, weaponPos, dir, 0, maxDist * 1.1);
    bool found = false;
    if (t <= maxDist)
    {
        // some intersection with land found
        found = true;
        pos[1] = GLandscape->SurfaceY(pos[0], pos[2]);
        pos[1] += 0.05f;
    }
    else
    {
        t = maxDist;
        pos = weaponPos + maxDist * dir;
    }

    // try to find some intersection with object

    CollisionBuffer collision;
    GLandscape->ObjectCollision(collision, _laserTarget, const_cast<EntityAI*>(this), weaponPos, pos, 0.2f);
    if (collision.Size() > 0)
    {
        // check first non-glass collision
        float minT = FLT_MAX;
        int minI = -1;

        Texture* glass = GPreloadedTextures.New(TextureBlack);
        for (int i = 0; i < collision.Size(); i++)
        {
            // info.pos is relative to object
            CollisionInfo& info = collision[i];
            // we can go through some textures

            if (info.texture == glass)
            {
                continue;
            }
            if (info.object)
            {
                if (minT > info.under)
                {
                    minT = info.under, minI = i;
                }
            }
        }
        if (minI >= 0)
        {
            CollisionInfo& info = collision[minI];
            pos = info.object->PositionModelToTop(info.pos);
            // make point a little bit off-surface to make sure it is visible
            pos -= dir * 0.07f;
            found = true;
        }
    }

    return found;
}

void EntityAI::StopLaser()
{
    if (_laserTarget)
    {
        _laserTarget->SetDelete();
    }
}

void EntityAI::TrackLaser(int weapon)
{
    // create a new laser target
    Vector3 pos, dir;
    if (CalculateLaser(pos, dir, weapon))
    {
        if (!_laserTarget)
        {
            TargetSide side = GetGroup()->GetCenter()->GetSide();
            const char* laserName = "LaserTargetC";
            if (side == TEast)
            {
                laserName = "LaserTargetE";
            }
            else if (side == TWest)
            {
                laserName = "LaserTargetW";
            }
            EntityAI* ltgt = NewVehicle(laserName, "");

            if (ltgt)
            {
                // laser target is oriented to face target
                // its forward direction goes away from laser source
                if (dir.Distance2(VUp) > Square(0.2))
                {
                    ltgt->SetDirectionAndUp(dir, VUp);
                }
                else
                {
                    ltgt->SetDirectionAndAside(dir, VAside);
                }
                ltgt->SetPosition(pos);
                ltgt->Init(*ltgt);

                GWorld->AddVehicle(ltgt);
                GetNetworkManager().CreateVehicle(ltgt, VLTVehicle, "", -1);

                _laserTarget = ltgt;
            }
        }
        else
        {
            _laserTarget->Move(pos);
        }
    }
    // when we do not call move, target is not activated
    // it stops shining
    // and after some time is autodestroys
}

void EntityAI::FireLaser(int weapon, TargetType* target)
{
    _laserTargetOn = !_laserTargetOn;
}

RString EntityAI::GetCurrentMove() const
{
    // normal vehicle cannot play any motion capture moves
    return RString();
}

void EntityAI::PlayMove(RStringB move, ActionContextBase* context)
{
    // normal vehicle cannot play any motion capture moves
}

void EntityAI::SwitchMove(RStringB move, ActionContextBase* context)
{
    // normal vehicle cannot play any motion capture moves
}

const float DeadAbility = 0.2; // private

float EntityAI::GetAbility() const
{
    // returns from 1 (able) to 0.5 (unable)
    AIUnit* brain = CommanderUnit();
    if (!brain)
    {
        return DeadAbility;
    }
    return brain->GetAbility();
}

float EntityAI::GetInvAbility() const
{
    // returns from 1 (able) to 2 (unable)
    AIUnit* brain = CommanderUnit();
    if (!brain)
    {
        return 1 / DeadAbility;
    }
    return brain->GetInvAbility();
}

float EntityAI::GetTypeCost(OperItemType type) const
{
    static const float costs[] = {
        1.0, // OITNormal,
        3.0,
        3.0,
        3.0,              // OITAvoidBush, OITAvoidTree, OITAvoid
        SET_UNACCESSIBLE, // OITWater,
        SET_UNACCESSIBLE, // OITSpaceRoad
        0.5,              //  OITRoad
        SET_UNACCESSIBLE, // OITSpaceBush
        SET_UNACCESSIBLE, // OITSpaceTree
        SET_UNACCESSIBLE, // OITSpace
        0.5               // OITRoadForced
    };
    AI_ERROR(sizeof(costs) / sizeof(*costs) == NOperItemType);
    return costs[type];
}

void EntityAI::FireWeaponEffects(int weapon, const Magazine* magazine, EntityAI* target)
{
    if (weapon < 0 || weapon >= NMagazineSlots())
    {
        return;
    }
    const MagazineSlot& slot = GetMagazineSlot(weapon);
    if (slot._magazine != magazine)
    {
        return;
    }

    const MuzzleType* muzzle = slot._muzzle;
    const SoundPars* sound = &muzzle->_sound;
    const MagazineType* mType = magazine ? magazine->_type : nullptr;
    const WeaponModeType* mode = nullptr;
    bool continuous = muzzle ? muzzle->_soundContinuous : false;
    bool noSound = false;
    if (mType && slot._mode >= 0 && slot._mode < mType->_modes.Size())
    {
        mode = mType->_modes[slot._mode];
        sound = &mode->_sound;
        continuous = mode->_soundContinuous;
        // sound only on first shot in burst
        if (magazine->_burstLeft > 0)
        {
            noSound = true;
        }
    }

    // play sound
    if (sound->name.GetLength() > 0 && !noSound)
    {
        float rndFreq = 1;
        float volume = sound->vol;
        float freq = sound->freq * rndFreq;
        _weaponFired.Access(weapon);     // playing sound of the weapon
        _weaponFiredTime.Access(weapon); // playing sound of the weapon
        IWave* wave = _weaponFired[weapon];
        if (WeaponSoundCacheStale(wave))
        {
            _weaponFired[weapon] = nullptr;
            wave = nullptr;
        }
        if (!wave)
        {
            IWave* newWave =
                GSoundScene->OpenAndPlayOnce(sound->name, GetWeaponSoundPos(this, weapon), Speed(), volume, freq);
            if (newWave)
            {
                if (!continuous)
                {
                    GSoundScene->SimulateSpeedOfSound(newWave);
                }
                GSoundScene->AddSound(newWave);
                _weaponFired[weapon] = newWave;
                if (continuous)
                {
                    newWave->PlayUntilStopValue(newWave->GetLength());
                }
            }
            _weaponFiredTime[weapon] = Glob.time;
        }
        else
        {
            // sound may be single loop or repeated
            // if the soond is repeating, it should be marked
            // if not, it should be restarted
            if (!continuous)
            {
                wave->Restart();
            }
            else
            {
                _weaponFiredTime[weapon] = Glob.time;
                wave->PlayUntilStopValue(wave->GetLength());
            }
        }
    }

    const AmmoType* type = mode ? mode->_ammo : nullptr;
    if (type)
    {
        _shootVisible = type->visibleFire;
        _shootAudible = type->audibleFire;
        _shootTimeRest = type->visibleFireTime;
        _shootTarget = target;
// perform cartridge effects
#if 1
        if (type->_cartridgeType && EnableVisualEffects(SimulateVisibleNear))
        {
            Vector3 viewerPos = GScene->GetCamera()->Position();

            SimulationImportance importance = CalculateImportance(&viewerPos, 1);
            if (importance <= SimulateVisibleNear)
            {
                // check current muzzle catridge position / velocity
                // create a transformation from weapon direction
                Matrix4 pos;
                Vector3 vel;
                if (GetWeaponCartridgePos(weapon, pos, vel))
                {
                    pos = Transform() * pos;
                    vel = Transform().Rotate(vel);

                    Entity* fx = CreateThing(type->_cartridgeType, pos, vel);
                    if (fx)
                    {
                        // any additional processing
                    }
                }
            }
        }
#endif
    }

    RStringB recoilName = mode->_recoilName;
    Ref<RecoilFunction> recoil = RecoilFunctions.New(recoilName);
    StartRecoil(recoil, GetRecoilFactor());

    if (IsEventHandler(EEFired))
    {
        GameValue value = GWorld->GetGameState()->CreateGameValue(GameArray);
        GameArrayType& arguments = value;
        const MagazineSlot& slot = GetMagazineSlot(weapon);
        RString weaponName = slot._weapon->GetName();
        RString muzzleName = slot._muzzle->GetName();
        RString modeName = mode ? mode->GetName() : "";
        RString ammoName = type ? type->GetName() : "";
        arguments.Add(GameValueExt(this));
        arguments.Add(weaponName);
        arguments.Add(muzzleName);
        arguments.Add(modeName);
        arguments.Add(ammoName);
        OnEvent(EEFired, value);
    }
}

bool EntityAI::FireWeapon(int weapon, TargetType* target)
{
    if (weapon >= NMagazineSlots() || weapon < 0)
    {
        return false; // nothing to fire
    }

    _lastShotTime = Glob.time;

    const MagazineSlot& slot = GetMagazineSlot(weapon);
    Magazine* magazine = slot._magazine;

    FireWeaponEffects(weapon, magazine, target);
    GetNetworkManager().FireWeapon(this, weapon, magazine, target);

    if (!magazine)
    {
        return false;
    }

    const WeaponModeType* mode = nullptr;
    const MagazineType* type = magazine ? magazine->_type : nullptr;
    if (magazine)
    {
        if (type && slot._mode >= 0 && slot._mode < type->_modes.Size())
        {
            mode = type->_modes[slot._mode];
        }
    }

    // only last bullet in burst should be reloaded based on ability

    if (magazine->_burstLeft <= 0)
    {
        magazine->_burstLeft = mode->_burst;
        saturateMin(magazine->_burstLeft, magazine->_ammo / mode->_mult);
    }

    if (magazine->_burstLeft == 1 || mode->_burst == 0)
    {
        AIUnit* unit = EffectiveGunnerUnit();
        if (!unit)
        {
            unit = CommanderUnit();
        }
        float invAbility = unit ? unit->GetInvAbility() : 1 / DeadAbility;
        magazine->_reload = mode->_reloadTime * invAbility * GRandGen.PlusMinus(1, 0.1);
    }
    else
    {
        magazine->_reload = mode->_reloadTime;
    }

    magazine->_burstLeft--; // count shots in burst

    int burst = mode->_mult;
    saturateMin(burst, magazine->_ammo);
    magazine->_ammo -= burst;
    // Infinite ammo cheat — refund the burst we just consumed.  Hook
    // lives here so it covers MGun / shell / missile firing through
    // the single ammo-decrement chokepoint, and stays inactive for AI
    // weapons (gated on GetRealPlayer() == this).
    if (DebugCheats::Cmd_InfiniteAmmo::IsActive() && GWorld && GWorld->GetRealPlayer() == this)
        magazine->_ammo += burst;
    if (!IsLocal())
    {
        GetNetworkManager().AskForAmmo(this, weapon, burst);
    }

    // remove magazine
    if (magazine->_ammo <= 0)
    {
        if (weapon != _currentWeapon)
        {
            RemoveMagazine(magazine);
        }
        // try to start auto-reloading
        AutoReload(weapon);
    }
    return true;
}

RefArray<Object> MapDiags;

#define DIAG_VEHICLE 0
#define DIAG_ROAD 0

template <>
const EnumName* Foundation::GetEnumNames(SimulationImportance dummy)
{
    static const EnumName SimulationImportanceNames[] = {EnumName(SimulateCamera, "Camera"),
                                                         EnumName(SimulateVisibleNear, "Near"),
                                                         EnumName(SimulateVisibleFar, "Far"),
                                                         EnumName(SimulateInvisibleNear, "InvisNear"),
                                                         EnumName(SimulateInvisibleFar, "InvisFar"),
                                                         EnumName(SimulateDefault, "Default"),
                                                         EnumName()};
    return SimulationImportanceNames;
}

RString EntityAI::DiagText() const
{
    AIUnit* unit = PilotUnit();
    BString<4096> buf;
    strcpy(buf, "");
    if (unit)
    {
        float pSpeed = 0;
        const Path& path = unit->GetPath();
        if (path.Size() >= 2)
        {
            float cost = path.CostAtPos(Position());
            pSpeed = path.SpeedAtCost(cost);
        }
        sprintf(buf, "Spd %.1f (path %d: v:%.1f, vMax:%.1f: t:%.2f%s)", ModelSpeed().Z() * 3.6, path.Size(),
                pSpeed * 3.6, _limitSpeed * 3.6, Glob.time - path.GetSearchTime(), path.GetOnRoad() ? " Road" : "");
    }
    if (GetStopped())
    {
        strcat(buf, " STOP");
    }
    strcat(buf, " ");
    strcat(buf, FindEnumName(_prec));

    if (!IsLocal())
    {
        if (Glob.time - _lastUpdateTime > 0.100)
        {
            BString<256> add;
            sprintf(add, " Last update %.3f", Glob.time - _lastUpdateTime);
            strcat(buf, add);
        }
        if (CheckPredictionFrozen())
        {
            strcat(buf, " Frozen");
        }
    }

    Shape* hitShape = _shape->HitpointsLevel();
    if (hitShape)
    {
        //  report dammages
#if _ENABLE_CHEATS
        if (CHECK_DIAG(DEDammage))
        {
            char hits[512];
            hits[0] = 0;
            if (GetRawTotalDammage() > 0)
            {
                sprintf(hits + strlen(hits), " Tot:%.2f", GetRawTotalDammage());
            }
            const HitPointList& hitpoints = GetType()->GetHitPoints();
            for (int i = 0; i < hitpoints.Size(); i++)
            {
                const HitPoint& hit = *hitpoints[i];
                int index = hit.GetSelection();
                if (index < 0)
                    continue;
                const NamedSelection& sel = hitShape->NamedSel(index);
                float hitVal = _hit[i];
                if (hitVal > 0.3)
                {
                    sprintf(hits + strlen(hits), " %s:%.2f", sel.Name(), hitVal);
                }
            }
            strcat(buf, hits);
        }
#endif
    }

    return (const char*)buf;
}

inline PackedColor CostToColor(float cost)
{
    static const float int1 = 1.0;
    static const float invint1 = (1.0 / int1);
    static const float int2 = 4.0;
    static const float invint2 = (1.0 / int2);
    static const float int12 = int1 + int2;

    static const float alpha = 0.5;
    static const Color colorR(1, 0.5f, 0, alpha);
    static const Color colorG(0, 1, 0, alpha);
    static const Color colorY(1, 1, 0, alpha);

    saturateMin(cost, int12);
    if (cost <= int1)
    {
        return PackedColor(colorY * (cost * invint1) + colorG * (1 - cost * invint1));
    }
    else
    {
        cost -= int1;
        return PackedColor(colorR * (cost * invint2) + colorY * (1 - cost * invint2));
    }
}

} // namespace Poseidon
