#include <Poseidon/Core/Application.hpp>

#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Core/Version.hpp>
#include <Poseidon/World/Scene/Object.hpp>
#include <Poseidon/World/Detection/Detector.hpp>
#include <Poseidon/World/Scene/ObjectClasses.hpp>
#include <Poseidon/World/Scene/Fireplace.hpp>

#include <Poseidon/Game/Commands/GameStateExt.hpp>
#include <Poseidon/UI/Settings/GameSettingsConfig.hpp>
#include <Poseidon/UI/OptionsUI.hpp>

#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/AI/VehicleAI.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/AI/AIRadio.hpp>
#include <Poseidon/UI/Map/UIMap.hpp>
#include <Poseidon/World/Entities/Infantry/SoldierOld.hpp>
#include <Poseidon/World/Entities/Vehicles/Ground/Car.hpp>
#include <Poseidon/World/Entities/Vehicles/Ground/Tank.hpp>
#include <Poseidon/World/Entities/Vehicles/Air/Airplane.hpp>
#include <Poseidon/World/Entities/Vehicles/House.hpp>
#include <Poseidon/Audio/DynSound.hpp>
#include <Poseidon/Audio/IAudioSystem.hpp> // GSoundsys, GetWaveDuration
#include <Poseidon/World/Scene/Camera/CameraHold.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Core/resincl.hpp>

#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/UI/Locale/Stringtable/CodepageTranscode.hpp>

#include <Poseidon/Game/UiActions.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <Poseidon/Foundation/Platform/AppConfig.hpp>

#include <ctype.h>
#include <time.h>
#include <float.h>
#include <limits.h>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

using namespace Poseidon;
void WhatUnits(RefArray<NetworkObject>& units, ChatChannel channel, NetworkObject* object);

namespace Poseidon
{
} // namespace Poseidon

#ifdef _WIN32
#include <io.h>
#endif

#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/IO/PreprocC/Preproc.h>

#include <Poseidon/Foundation/Platform/VersionNo.h>

#include <Poseidon/Game/Commands/GameStateExtCommon.hpp>
#include <Poseidon/Foundation/Enums/EnumNames.hpp>

namespace Poseidon
{
const ParamEntry* FindMusic(RString, SoundPars&);
}

namespace Poseidon
{
RString FindPicture(RString name);
}

using Poseidon::Foundation::EnumName;
using Poseidon::Foundation::FindEnumName;
using Poseidon::Foundation::GetEnumNames;
using Poseidon::Foundation::GetEnumValue;

GameValue ObjGetDammage(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return 0.0f;
    }

    return obj->GetTotalDammage();
}

GameValue ObjSetDammage(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }

    float dammage = oper2;
    obj->SetDammageNetAware(dammage);
    if (0.0 == dammage)
    {
        Airplane* plane = dyn_cast<Airplane>(obj);
        if (plane)
        {
            plane->RepairGear();
        }
    }
    return NOTHING;
}

GameValue ObjAllowDammage(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }

    bool allow = oper2;
    EntityAI* ent = dyn_cast<EntityAI>(obj);
    if (!ent)
    {
        return NOTHING;
    }
    ent->SetAllowDammage(allow);
    return NOTHING;
}

GameValue ObjCanStand(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    Man* man = dyn_cast<Man>(obj);
    if (!man)
    {
        return false;
    }

    return man->IsAbleToStand();
}

GameValue ObjHandsHit(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    Man* man = dyn_cast<Man>(obj);
    if (!man)
    {
        return 1.0f;
    }

    return man->GetHandsHit();
}

GameValue ObjSetFaceAnimation(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj1 = GetObject(oper1);
    if (!obj1)
    {
        return NOTHING;
    }

    Man* soldier = dyn_cast<Man>(obj1);
    if (!soldier)
    {
        return NOTHING;
    }

    int phase = toInt((float)oper2);
    soldier->SetFaceAnimation(phase);
    return NOTHING;
}

const ParamEntry* FindIdentity(RString name)
{
    // find in mission
    const ParamEntry* cls = ExtParsMission.FindEntry("CfgIdentities");
    const ParamEntry* entry = cls ? cls->FindEntry(name) : nullptr;
    if (entry)
    {
        return entry;
    }

    // find in campaign
    cls = ExtParsCampaign.FindEntry("CfgIdentities");
    entry = cls ? cls->FindEntry(name) : nullptr;
    return entry;
}

GameValue ObjSetIdentity(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }

    VehicleWithBrain* person = dyn_cast<VehicleWithBrain>(obj);
    if (!person)
    {
        return NOTHING;
    }

    GameStringType name = oper2;
    const ParamEntry* entry = FindIdentity(name);
    if (entry)
    {
        AIUnitInfo& info = person->GetInfo();
        info._identityContext = RString();
        info._name = DecodeLegacyTextToRString((*entry) >> "name", GLanguage);
        info._face = (*entry) >> "face";
        info._glasses = (*entry) >> "glasses";
        info._speaker = (*entry) >> "speaker";
        info._pitch = (*entry) >> "pitch";

        person->SetFace(info._face);
        person->SetGlasses(info._glasses);
        if (person->Brain())
        {
            person->Brain()->SetSpeaker(info._speaker, info._pitch);
        }
    }

    return NOTHING;
}

GameValue ObjSetFace(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj1 = GetObject(oper1);
    if (!obj1)
    {
        return NOTHING;
    }

    Man* soldier = dyn_cast<Man>(obj1);
    if (!soldier)
    {
        return NOTHING;
    }

    GameStringType name = oper2;
    soldier->SetFace(name);
    return NOTHING;
}

GameValue ObjSetMimic(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj1 = GetObject(oper1);
    if (!obj1)
    {
        return NOTHING;
    }

    Man* soldier = dyn_cast<Man>(obj1);
    if (!soldier)
    {
        return NOTHING;
    }

    GameStringType name = oper2;
    soldier->SetMimic(name);
    return NOTHING;
}

GameValue ObjGetFlagOwner(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return OBJECT_NULL;
    }

    EntityAI* veh = dyn_cast<EntityAI>(obj);
    if (!veh)
    {
        return OBJECT_NULL;
    }

    return GameValueExt(veh->GetFlagOwner());
}

GameValue ObjGetFlag(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return OBJECT_NULL;
    }

    Person* veh = dyn_cast<Person>(obj);
    if (!veh)
    {
        return OBJECT_NULL;
    }

    return GameValueExt(veh->GetFlagCarrier());
}

GameValue ObjSetFlagOwner(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj1 = GetObject(oper1);
    if (!obj1)
    {
        return NOTHING;
    }

    EntityAI* flag = dyn_cast<EntityAI>(obj1);
    if (!flag)
    {
        return NOTHING;
    }

    Object* obj2 = GetObject(oper2);
    Person* owner = dyn_cast<Person>(obj2);
    flag->SetFlagOwner(owner);

    return NOTHING;
}

GameValue ObjSetFlagTexture(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj1 = GetObject(oper1);
    if (!obj1)
    {
        return NOTHING;
    }

    EntityAI* veh = dyn_cast<EntityAI>(obj1);
    if (!veh)
    {
        return NOTHING;
    }

    GameStringType name = oper2;
    veh->SetFlagTexture(name);
    return NOTHING;
}

GameValue ObjSetFlagSide(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }

    FlagCarrier* flag = dyn_cast<FlagCarrier>(obj);
    if (!flag)
    {
        return NOTHING;
    }

    TargetSide side = GetSide(oper2);
    flag->SetFlagSide(side);
    return NOTHING;
}

GameValue ObjSetTexture(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    const GameArrayType& array = oper2;
    if (array.Size() != 2)
    {
        state->SetError(EvalDim, array.Size(), 2);
        return NOTHING;
    }
    if (array[0].GetType() != GameScalar)
    {
        state->TypeError(GameScalar, array[0].GetType());
        return NOTHING;
    }
    if (array[1].GetType() != GameString)
    {
        state->TypeError(GameString, array[1].GetType());
        return NOTHING;
    }

    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }

    EntityAI* veh = dyn_cast<EntityAI>(obj);
    if (!veh)
    {
        return NOTHING;
    }

    int index = toInt((float)array[0]);
    RString name = array[1];

    Ref<Texture> texture = GlobLoadTexture(Poseidon::FindPicture(name));
    veh->SetObjectTexture(index, texture);
    return NOTHING;
}

GameValue ObjGetNearestBuilding(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return ObjNull(state);
    }

    Vector3 pos = obj->Position();
    Vehicle* house = nullptr;
    float minDist2 = FLT_MAX;
    for (int i = 0; i < GWorld->NBuildings(); i++)
    {
        Vehicle* veh = GWorld->GetBuilding(i);
        if (!veh)
        {
            continue;
        }
        if (!veh->GetShape())
        {
            continue;
        }
        if (veh->GetShape()->FindPaths() < 0)
        {
            continue;
        }
        if (!veh->GetIPaths())
        {
            continue;
        }
        float dist2 = veh->Position().Distance2(pos);
        if (dist2 < minDist2)
        {
            minDist2 = dist2;
            house = veh;
        }
    }
    return GameValueExt(house);
}

GameValue ObjGetBuildingPos(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;
    array.Resize(3);
    array[0] = 0.0f;
    array[1] = 0.0f;
    array[2] = 0.0f;

    Object* obj = GetObject(oper1);
    if (obj)
    {
        const IPaths* house = obj->GetIPaths();
        if (house)
        {
            int housePos = toInt((float)oper2);
            if (housePos >= 0 && housePos < house->NPos())
            {
                Vector3Val pos = house->GetPosition(house->GetPos(housePos));
                array[0] = pos.X();
                array[1] = pos.Z();
                array[2] = pos.Y() - GLandscape->SurfaceYAboveWater(pos.X(), pos.Z());
            }
        }
    }
    return value;
}

namespace
{
GameValue nearestObject(const Vector3& pos, const RString& typeName, const float limit = 50);
}

GameValue GetNearestObject(const GameState* state, GameValuePar oper1)
{
    Vector3 pos;
    RString typeName;

    if (!GetPos(state, pos, oper1))
    {
        const GameArrayType& array = oper1;
        if (array.Size() != 2)
        {
            return OBJECT_NULL;
        }
        if (!GetPos(state, pos, array[0]))
        {
            return OBJECT_NULL;
        }
        if (array[1].GetType() != GameString)
        {
            return OBJECT_NULL;
        }
        typeName = array[1];
        state->SetError(EvalOK);
    }

    return nearestObject(pos, typeName);
}

namespace
{
GameValue nearestObject(const Vector3& pos, const RString& typeName, const float limit)
{
    int xMin, xMax, zMin, zMax;
    ObjRadiusRectangle(xMin, xMax, zMin, zMax, pos, pos, limit);
    Object* object = nullptr;
    float minDist2 = Square(limit);
    for (int x = xMin; x <= xMax; x++)
    {
        for (int z = zMin; z <= zMax; z++)
        {
            const ObjectList& list = GLandscape->GetObjects(z, x);
            int n = list.Size();
            for (int i = 0; i < n; i++)
            {
                Object* obj = list[i];

                float dist2 = obj->Position().Distance2(pos);
                if (dist2 >= minDist2)
                {
                    continue;
                }
                if (typeName.GetLength() > 0)
                {
                    Vehicle* veh = dyn_cast<Vehicle>(obj);
                    if (!veh)
                    {
                        continue;
                    }
                    if (stricmp(veh->GetName(), typeName) != 0)
                    {
                        continue;
                    }
                }

                object = obj;
                minDist2 = dist2;
            }
        }
    }

    return GameValueExt(object);
}
} // namespace

GameValue GetNearestObjectByDistance(const GameState* state, GameValuePar oper1)
{
    Vector3 pos;
    RString typeName;

    // oper1 must be [Object(pos), String(type), Number(distance)]
    const GameArrayType& array = oper1;
    if (array.Size() != 3)
    {
        state->SetError(EvalGen);
        return OBJECT_NULL;
    }
    if (!GetPos(state, pos, array[0]))
    {
        state->SetError(EvalGen);
        return OBJECT_NULL;
    }
    if (array[1].GetType() != GameString)
    {
        state->TypeError(GameString, array[1].GetType());
        return OBJECT_NULL;
    }
    typeName = array[1];
    if (array[2].GetType() != GameScalar)
    {
        state->TypeError(GameScalar, array[2].GetType());
        return OBJECT_NULL;
    }
    state->SetError(EvalOK);

    return nearestObject(pos, typeName, static_cast<float>(array[2]));
}

GameValue StrSub(const GameState* state, GameValuePar oper1)
{
    const GameArrayType& array = oper1;
    if (!CheckSize(state, array, 3))
    {
        return RString();
    }
    if (!CheckType(state, array[0], GameString))
    {
        return RString();
    }
    if (!CheckType(state, array[1], GameScalar))
    {
        return RString();
    }
    if (!CheckType(state, array[2], GameScalar))
    {
        return RString();
    }

    GameStringType str = array[0];
    const int len = str.GetLength();
    const int from = toInt((float)array[1]);
    const int to = toInt((float)array[2]);
    if (from < 0 || from >= len)
    {
        return RString();
    }
    if (to > len)
    {
        return RString();
    }
    return RString(str.Data() + from, to - from);
}

GameValue StrSize(const GameState* state, GameValuePar oper1)
{
    GameStringType str = oper1;
    return float(str.GetLength());
}

GameValue ObjSwitchLight(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);

    StreetLamp* lamp = dyn_cast<StreetLamp>(obj);
    if (lamp)
    {
        RString name = oper2;
        StreetLamp::LightState state = GetEnumValue<StreetLamp::LightState>((const char*)name);
        if (state == INT_MIN)
        {
            state = StreetLamp::LSAuto;
        }
        lamp->SwitchLight(state);
    }

    return NOTHING;
}

GameValue ObjInflame(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);

    Fireplace* fire = dyn_cast<Fireplace>(obj);
    if (fire)
    {
        if (fire->IsLocal())
        {
            fire->Inflame(oper2);
        }
        else
        {
            GetNetworkManager().AskForInflameFire(fire, oper2);
        }
    }

    return NOTHING;
}

GameValue ObjInflamed(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);

    Fireplace* fire = dyn_cast<Fireplace>(obj);
    if (fire)
    {
        return fire->Burning();
    }

    return false;
}

GameValue ObjLightSwitched(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);

    StreetLamp* lamp = dyn_cast<StreetLamp>(obj);
    if (lamp)
    {
        StreetLamp::LightState state = lamp->GetLightState();
        return FindEnumName(state);
    }
    return "ERROR";
}

GameValue ObjGetScudState(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);

    Car* scud = dyn_cast<Car>(obj);
    if (scud)
    {
        return scud->GetScudState();
    }
    return 0.0f;
}

GameValue ObjList(const GameState* state, GameValuePar oper1)
{
    Object* obj1 = GetObject(oper1);

    Detector* detect = dyn_cast<Detector>(obj1);
    if (!detect)
    {
        return GameArrayType();
    }

    return detect->GetGameValue();
}

GameValue ObjLeader(const GameState* state, GameValuePar oper1)
{
    Object* obj1 = GetObject(oper1);
    EntityAI* ai = dyn_cast<EntityAI>(obj1);
    if (!ai)
    {
        return OBJECT_NULL;
    }
    AIUnit* unit = ai->CommanderUnit();
    if (!unit)
    {
        return OBJECT_NULL;
    }
    AISubgroup* subgrp = unit->GetSubgroup();
    if (!subgrp)
    {
        return OBJECT_NULL;
    }
    if (!subgrp->Leader())
    {
        return OBJECT_NULL;
    }
    EntityAI* leader = subgrp->Leader()->GetPerson();
    return GameValueExt(leader);
}

GameValue ObjGroupLeader(const GameState* state, GameValuePar oper1)
{
    Object* obj1 = GetObject(oper1);
    EntityAI* ai = dyn_cast<EntityAI>(obj1);
    if (!ai)
    {
        return OBJECT_NULL;
    }
    AIUnit* unit = ai->CommanderUnit();
    if (!unit)
    {
        return OBJECT_NULL;
    }
    AIGroup* grp = unit->GetGroup();
    if (!grp)
    {
        return OBJECT_NULL;
    }
    if (!grp->Leader())
    {
        return OBJECT_NULL;
    }
    EntityAI* leader = grp->Leader()->GetPerson();
    return GameValueExt(leader);
}

GameValue ObjGroup(const GameState* state, GameValuePar oper1)
{
    Object* obj1 = GetObject(oper1);
    EntityAI* ai = dyn_cast<EntityAI>(obj1);
    if (!ai)
    {
        return GROUP_NULL;
    }
    AIUnit* unit = ai->CommanderUnit();
    if (!unit)
    {
        return GROUP_NULL;
    }
    AIGroup* grp = unit->GetGroup();
    return GameValueExt(grp);
}

GameValue GrpLeader(const GameState* state, GameValuePar oper1)
{
    AIGroup* grp1 = GetGroup(oper1);
    if (!grp1)
    {
        return OBJECT_NULL;
    }
    AIUnit* leader = grp1->Leader();
    if (!leader)
    {
        return OBJECT_NULL;
    }
    EntityAI* leaderVeh = leader->GetPerson();
    return GameValueExt(leaderVeh);
}

GameValue GrpUnits(const GameState* state, GameValuePar oper1)
{
    AIGroup* grp1 = GetGroup(oper1);

    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;
    if (grp1)
    {
        array.Realloc(grp1->NUnits());
        for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
        {
            AIUnit* unit = grp1->UnitWithID(i + 1);
            if (!unit)
            {
                continue;
            }
            EntityAI* veh = unit->GetPerson();
            array.Add(GameValueExt(veh));
        }
    }
    return value;
}

GameValue ObjVehicle(const GameState* state, GameValuePar oper1)
{
    Object* obj1 = GetObject(oper1);
    EntityAI* ai = dyn_cast<EntityAI>(obj1);
    if (!ai)
    {
        return oper1;
    }
    AIUnit* unit = ai->CommanderUnit();
    if (!unit)
    {
        return oper1;
    }
    EntityAI* vehicle = unit->GetVehicle();
    return GameValueExt(vehicle);
}

GameValue ObjDriver(const GameState* state, GameValuePar oper1)
{
    Object* obj1 = GetObject(oper1);
    Transport* trans = dyn_cast<Transport>(obj1);
    if (!trans)
    {
        return oper1;
    }
    EntityAI* vehicle = trans->Driver();
    return GameValueExt(vehicle);
}

GameValue ObjCommander(const GameState* state, GameValuePar oper1)
{
    Object* obj1 = GetObject(oper1);
    Transport* trans = dyn_cast<Transport>(obj1);
    if (!trans)
    {
        return oper1;
    }
    EntityAI* vehicle = trans->Commander();
    return GameValueExt(vehicle);
}

GameValue ObjGunner(const GameState* state, GameValuePar oper1)
{
    Object* obj1 = GetObject(oper1);
    Transport* trans = dyn_cast<Transport>(obj1);
    if (!trans)
    {
        return oper1;
    }
    EntityAI* vehicle = trans->Gunner();
    return GameValueExt(vehicle);
}

GameValue ObjSay(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj1 = GetObject(oper1);
    GameStringType voice;
    float distance = 100.0f;
    float speed = 1.0f;
    if (oper2.GetType() == GameString)
    {
        voice = oper2;
    }
    else
    {
        PoseidonAssert(oper2.GetType() == GameArray);
        const GameArrayType& array = oper2;

        switch (array.Size())
        {
            case 3:
                if (array[2].GetType() != GameScalar)
                {
                    state->TypeError(GameScalar, array[2].GetType());
                    return NOTHING;
                }
                speed = array[2];

            case 2:
                if (array[1].GetType() != GameScalar)
                {
                    state->TypeError(GameScalar, array[1].GetType());
                    return NOTHING;
                }
                distance = array[1];

                if (array[0].GetType() != GameString)
                {
                    state->TypeError(GameString, array[0].GetType());
                    return NOTHING;
                }
                voice = array[0];
                break;
            default:
                state->SetError(EvalDim, array.Size(), 2);
                return NOTHING;
        }
    }

    if (!obj1)
    {
        return NOTHING;
    }
    if (obj1->IsDammageDestroyed())
    {
        return NOTHING;
    }

    Vehicle* veh = new SoundOnVehicle(voice, obj1, distance, speed);
    veh->SetPosition(obj1->Position());
    GWorld->AddBuilding(veh);
    return NOTHING;
}

GameValue PlaySound(const GameState* state, GameValuePar oper1)
{
    GameStringType voice = oper1;
    Vehicle* veh = new SoundOnVehicle(voice, nullptr);
    GWorld->AddBuilding(veh);
    return NOTHING;
}

// soundLength "classname" — duration in seconds of the CfgSounds entry's clip, resolved for
// the current voice language exactly as say/playSound do (FindSound applies the per-language
// "<base>.<lang>.<ext>" suffix). Returns 0 for a subtitle-only entry or an unresolved key.
// Cutscene scripts can pace dialog to the real clip regardless of language, e.g.
//   unit say "x00v05"; ~ (soundLength "x00v05") + 0.4
GameValue SoundLength(const GameState* state, GameValuePar oper1)
{
    GameStringType name = oper1;
    SoundPars pars;
    pars.name = "";
    FindSound(name, pars);
    if (pars.name.GetLength() <= 0 || !GSoundsys)
    {
        return 0.0f;
    }
    return GSoundsys->GetWaveDuration(pars.name);
}

GameValue VoiceLanguage(const GameState* /*state*/)
{
    return GameValue(RString(GetSelectedVoiceLanguage().c_str()));
}

const ParamEntry* FindMusic(RString name, SoundPars& pars);

GameValue PlayMusic(const GameState* state, GameValuePar oper1)
{
    if (oper1.GetType() == GameArray)
    {
        const GameArrayType& array = oper1;
        if (array.Size() != 2)
        {
            state->SetError(EvalDim, array.Size(), 2);
            return NOTHING;
        }
        if (array[0].GetType() != GameString)
        {
            state->TypeError(GameString, array[0].GetType());
            return NOTHING;
        }
        if (array[1].GetType() != GameScalar)
        {
            state->TypeError(GameScalar, array[1].GetType());
            return NOTHING;
        }
        GameStringType voice = array[0];
        GameScalarType pos = array[1];

        if (voice.GetLength() <= 0)
        {
            GSoundScene->StopMusicTrack();
            return NOTHING;
        }

        SoundPars sound;
        if (Poseidon::FindMusic(voice, sound))
        {
            GSoundScene->StartMusicTrack(sound, pos);
        }
        return NOTHING;
    }

    GameStringType voice = oper1;

    if (voice.GetLength() <= 0)
    {
        GSoundScene->StopMusicTrack();
        return NOTHING;
    }

    SoundPars sound;
    if (Poseidon::FindMusic(voice, sound))
    {
        GSoundScene->StartMusicTrack(sound);
    }
    return NOTHING;
}
GameValue SetMusicVolume(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    GSoundScene->SetMusicVolume(oper2, oper1);
    return NOTHING;
}
GameValue GetMusicVolume(const GameState* state)
{
    return GSoundScene->GetMusicVolume();
}
GameValue SetSoundVolume(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    GSoundScene->SetSoundVolume(oper2, oper1);
    return NOTHING;
}
GameValue GetSoundVolume(const GameState* state)
{
    return GSoundScene->GetSoundVolume();
}

static bool SendRadio(RadioChannel& channel, GameValuePar oper1, GameValuePar oper2)
{
    GameStringType wave = oper2;
    Object* obj = GetObject(oper1);
    EntityAI* veh = dyn_cast<EntityAI>(obj);
    AIUnit* unit = veh ? veh->CommanderUnit() : nullptr;
    channel.Transmit(new RadioMessageText(wave, unit), 0);
    return true;
}

static bool SendRadio(RadioChannel& channel, RString identity, GameValuePar oper2)
{
    GameStringType wave = oper2;
    if (!(Pars >> "CfgHQIdentities").FindEntry(identity))
    {
        return false;
    }
    // send class name, translate in RadioChannel::Say
    channel.Transmit(new RadioMessageText(wave, identity), 0);
    return true;
}

GameValue ObjGlobalRadio(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    if (!SendRadio(GWorld->GetRadio(), oper1, oper2))
    {
        state->SetError(EvalGen);
    }
    return NOTHING;
}

GameValue ObjSideRadio(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    if (oper1.GetType() == GameArray)
    {
        const GameArrayType& array = oper1;
        if (array.Size() != 2)
        {
            state->SetError(EvalDim, array.Size(), 2);
            return NOTHING;
        }
        if (array[0].GetType() != GameSide)
        {
            state->TypeError(GameSide, array[0].GetType());
            return NOTHING;
        }
        if (array[1].GetType() != GameString)
        {
            state->TypeError(GameString, array[1].GetType());
            return NOTHING;
        }
        AICenter* center = GWorld->GetCenter(GetSide(array[0]));
        if (!center)
        {
            return NOTHING;
        }
        GameStringType identity = array[1];
        if (!SendRadio(center->GetRadio(), identity, oper2))
        {
            state->SetError(EvalGen);
        }
    }
    else
    {
        AIGroup* grp = GetGroup(oper1);
        if (!grp)
        {
            return NOTHING;
        }
        AICenter* center = grp->GetCenter();
        if (!center)
        {
            return NOTHING;
        }
        if (!SendRadio(center->GetRadio(), oper1, oper2))
        {
            state->SetError(EvalGen);
        }
    }
    return NOTHING;
}

GameValue ObjGroupRadio(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    AIGroup* grp = GetGroup(oper1);
    if (!grp)
    {
        return NOTHING;
    }
    if (!SendRadio(grp->GetRadio(), oper1, oper2))
    {
        state->SetError(EvalGen);
    }
    return NOTHING;
}

GameValue ObjVehicleRadio(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    AIUnit* unit = GetUnit(state, oper1);
    if (!unit)
    {
        return NOTHING;
    }
    Transport* veh = unit->GetVehicleIn();
    if (!veh)
    {
        return NOTHING;
    }
    if (!SendRadio(veh->GetRadio(), oper1, oper2))
    {
        state->SetError(EvalGen);
    }
    return NOTHING;
}

namespace Poseidon::Foundation
{
template class Ref<NetworkObject>;
} // namespace Poseidon::Foundation

static bool SendChat(ChatChannel channel, NetworkObject* object, AIUnit* sender, RString message, bool audible)
{
    if (audible)
    {
        GChatList.Add(channel, sender, DecodeLegacyTextToRString(message, GLanguage), false, true);
    }
    // Not sent over the network: this drives title-effect-style chat, activated locally on each client.
    return true;
}

static bool SendChat(ChatChannel channel, NetworkObject* object, RString identity, RString message, bool audible)
{
    const ParamEntry* cfg = (Pars >> "CfgHQIdentities").FindEntry(identity);
    if (!cfg)
    {
        return false;
    }

    // used only in GChatList.Add - write directly display name
    RString sender = DecodeLegacyTextToRString(*cfg >> "name", GLanguage);

    if (audible)
    {
        GChatList.Add(channel, sender, DecodeLegacyTextToRString(message, GLanguage), false, true);
    }
    // Not sent over the network: this drives title-effect-style chat, activated locally on each client.
    return true;
}

GameValue ObjGlobalChat(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    if (AppConfig::Instance().IsSimulateMode())
        LOG_INFO(Mission, "[globalChat] {}", (const char*)oper2.GetText());
    EntityAI* veh = dyn_cast<EntityAI>(GetObject(oper1));
    if (!veh)
    {
        return NOTHING;
    }
    AIUnit* unit = veh->CommanderUnit();
    if (!unit)
    {
        return NOTHING;
    }
    bool audible = GWorld->GetRadio().IsAudible();
    if (!SendChat(CCGlobal, nullptr, unit, oper2, audible))
    {
        state->SetError(EvalGen);
    }
    return NOTHING;
}

GameValue ObjSideChat(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    if (AppConfig::Instance().IsSimulateMode())
        LOG_INFO(Mission, "[sideChat] {}", (const char*)oper2.GetText());
    if (oper1.GetType() == GameArray)
    {
        const GameArrayType& array = oper1;
        if (array.Size() != 2)
        {
            state->SetError(EvalDim, array.Size(), 2);
            return NOTHING;
        }
        if (array[0].GetType() != GameSide)
        {
            state->TypeError(GameSide, array[0].GetType());
            return NOTHING;
        }
        if (array[1].GetType() != GameString)
        {
            state->TypeError(GameString, array[1].GetType());
            return NOTHING;
        }
        AICenter* center = GWorld->GetCenter(GetSide(array[0]));
        if (!center)
        {
            return NOTHING;
        }
        RString identity = array[1];
        bool audible = center->GetRadio().IsAudible();
        if (!SendChat(CCSide, nullptr, identity, oper2, audible))
        {
            state->SetError(EvalGen);
        }
    }
    else
    {
        EntityAI* veh = dyn_cast<EntityAI>(GetObject(oper1));
        if (!veh)
        {
            return NOTHING;
        }
        AIUnit* unit = veh->CommanderUnit();
        if (!unit)
        {
            return NOTHING;
        }
        AIGroup* grp = unit->GetGroup();
        if (!grp)
        {
            return NOTHING;
        }
        AICenter* center = grp->GetCenter();
        if (!center)
        {
            return NOTHING;
        }
        bool audible = center->GetRadio().IsAudible();
        if (!SendChat(CCSide, center, unit, oper2, audible))
        {
            state->SetError(EvalGen);
        }
    }
    return NOTHING;
}

// Test hook: global chat from the local player.  Uses SendChat so the message
// reaches other clients via the server relay.  No player guard: SendChat falls
// back to the name-only path when the player or brain is null (common in JIP
// before NMTSelectPlayer arrives), which still broadcasts correctly.
GameValue TriSideChat(const GameState* /*state*/, GameValuePar arg)
{
    if (!GWorld)
        return GameValue("FAIL:no_world");
    void SendChat(ChatChannel channel, RString text);
    RString text = arg;
    SendChat(CCGlobal, text);
    return GameValue("OK");
}

GameValue ObjGroupChat(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    if (AppConfig::Instance().IsSimulateMode())
        LOG_INFO(Mission, "[groupChat] {}", (const char*)oper2.GetText());
    EntityAI* veh = dyn_cast<EntityAI>(GetObject(oper1));
    if (!veh)
    {
        return NOTHING;
    }
    AIUnit* unit = veh->CommanderUnit();
    if (!unit)
    {
        return NOTHING;
    }
    AIGroup* grp = unit->GetGroup();
    if (!grp)
    {
        return NOTHING;
    }

    bool audible = grp->GetRadio().IsAudible();
    if (!SendChat(CCGroup, grp, unit, oper2, audible))
    {
        state->SetError(EvalGen);
    }
    return NOTHING;
}

GameValue ObjVehicleChat(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    if (AppConfig::Instance().IsSimulateMode())
        LOG_INFO(Mission, "[vehicleChat] {}", (const char*)oper2.GetText());
    Transport* veh = dyn_cast<Transport>(GetObject(oper1));
    if (!veh)
    {
        return NOTHING;
    }
    AIUnit* unit = veh->CommanderUnit();
    if (!unit)
    {
        return NOTHING;
    }
    bool audible = veh->GetRadio().IsAudible();
    if (!SendChat(CCVehicle, veh, unit, oper2, audible))
    {
        state->SetError(EvalGen);
    }
    return NOTHING;
}

GameValue ObjPlayMove(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    EntityAI* obj = dyn_cast<EntityAI>(GetObject(oper1));
    GameStringType move = oper2;

    if (!obj)
    {
        return NOTHING;
    }
    obj->PlayMove(move);
    return NOTHING;
}

GameValue ObjSwitchMove(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    EntityAI* obj = dyn_cast<EntityAI>(GetObject(oper1));
    GameStringType move = oper2;

    if (!obj)
    {
        return NOTHING;
    }
    obj->SwitchMove(move);
    return NOTHING;
}

GameValue ObjCameraEffect(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj1 = GetObject(oper1);
    if (!obj1)
    {
        return NOTHING;
    }

    const GameArrayType& array = oper2;
    if (array.Size() != 2)
    {
        state->SetError(EvalDim, array.Size(), 2);
        return NOTHING;
    }
    if (array[0].GetType() != GameString)
    {
        state->TypeError(GameString, array[0].GetType());
        return NOTHING;
    }
    if (array[1].GetType() != GameString)
    {
        state->TypeError(GameString, array[1].GetType());
        return NOTHING;
    }

    GameStringType effect = array[0];
    GameStringType str = array[1];
    CamEffectPosition pos = CamEffectTop;
    const EnumName* names = GetEnumNames(pos);
    for (int i = 0; names[i].IsValid(); i++)
    {
        if (stricmp(names[i].name, str) == 0)
        {
            pos = (CamEffectPosition)names[i].value;
            // if we are starting camera effect on camera holder
            // it is script controlled and should be infinite
            // also intro camera effects are infinite
            bool infinite = (dyn_cast<CameraHolder>(obj1) != nullptr || GWorld->GetMode() == GModeIntro);
            GWorld->SetCameraEffect(CreateCameraEffect(obj1, effect, pos, infinite));
            return NOTHING;
        }
    }

    return NOTHING;
}

GameValue CamCreate(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    GameStringType effect = oper1;
    Vector3 pos;
    if (!GetPos(state, pos, oper2))
    {
        LOG_WARN(Script, "camCreate: failed to get position from argument");
        return OBJECT_NULL;
    }

    RString type = effect;
    // camCreate makes any non-AI vehicle at an exact position — cameras and seagulls, but also
    // game logics, radios, empty vehicles and static objects (e.g. "Logic" camCreate [...],
    // "radio" camCreate getpos player). The original engine allowed every type (its
    // camera/seagull guard was a no-op — `!strcmpi && !strcmpi` is never both-true); an unknown
    // type is rejected naturally below when NewNonAIVehicle returns null.
    Vehicle* veh = NewNonAIVehicle(type, nullptr);
    if (!veh)
    {
        LOG_WARN(Script, "camCreate: NewNonAIVehicle('{}') returned null", (const char*)type);
        return OBJECT_NULL;
    }

    veh->SetPosition(pos);

    GWorld->AddAnimal(veh); // insert vehicle to both landscape and world

    LOG_DEBUG(Script, "camCreate: created '{}' at ({:.1f}, {:.1f}, {:.1f})", (const char*)type, pos.X(), pos.Y(),
              pos.Z());
    return GameValueExt(veh);
}

GameValue CamDestroy(const GameState* state, GameValuePar oper1)
{
    CameraHolder* cam = dyn_cast<CameraHolder>(GetObject(oper1));
    if (!cam)
    {
        return NOTHING;
    }
    cam->SetDelete();
    return NOTHING;
}

GameValue CamCommited(const GameState* state, GameValuePar oper1)
{
    CameraHolder* cam = dyn_cast<CameraHolder>(GetObject(oper1));
    if (!cam)
    {
        LOG_WARN(Script, "camCommitted: camera object is null (dyn_cast failed)");
        return NOTHING;
    }
    return cam->GetCommited();
}

GameValue CamSetPos(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    CameraHolder* cam = dyn_cast<CameraHolder>(GetObject(oper1));
    if (!cam)
    {
        return NOTHING;
    }

    Vector3 pos;
    if (!GetPos(state, pos, oper2))
    {
        return NOTHING;
    }
    cam->SetPos(pos);

    return NOTHING;
}

GameValue CamSetRelPos(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    CameraHolder* cam = dyn_cast<CameraHolder>(GetObject(oper1));
    if (!cam)
    {
        return NOTHING;
    }

    Vector3 pos;
    if (!GetVector(pos, oper2))
    {
        state->SetError(EvalGen);
        return NOTHING;
    }
    cam->SetPos(cam->GetTarget().PositionRelToAbs(pos));

    return NOTHING;
}

GameValue CamSetFOV(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    CameraHolder* cam = dyn_cast<CameraHolder>(GetObject(oper1));
    if (!cam)
    {
        return NOTHING;
    }
    cam->SetFOV(oper2);
    return NOTHING;
}

GameValue CamSetFOVRange(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    return NOTHING;
}

GameValue CamSetDive(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    CameraHolder* cam = dyn_cast<CameraHolder>(GetObject(oper1));
    if (!cam)
    {
        return NOTHING;
    }
    cam->SetDive(oper2);
    return NOTHING;
}

GameValue CamSetBank(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    CameraHolder* cam = dyn_cast<CameraHolder>(GetObject(oper1));
    if (!cam)
    {
        return NOTHING;
    }
    cam->SetBank(oper2);
    return NOTHING;
}

GameValue CamSetDir(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    CameraHolder* cam = dyn_cast<CameraHolder>(GetObject(oper1));
    if (!cam)
    {
        return NOTHING;
    }
    cam->SetHeading(oper2);
    return NOTHING;
}

GameValue CamSetTargetObj(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    CameraHolder* cam = dyn_cast<CameraHolder>(GetObject(oper1));
    if (!cam)
    {
        return NOTHING;
    }
    cam->SetTarget(GetObject(oper2));
    return NOTHING;
}

GameValue CamSetTargetVec(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    CameraHolder* cam = dyn_cast<CameraHolder>(GetObject(oper1));
    if (!cam)
    {
        return NOTHING;
    }
    Vector3 pos;
    if (!GetPos(state, pos, oper2))
    {
        return NOTHING;
    }
    cam->SetTarget(pos);
    return NOTHING;
}

GameValue CamCommand(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj1 = GetObject(oper1);
    CameraHolder* cam = dyn_cast<CameraHolder>(obj1);
    if (!cam)
    {
        return NOTHING;
    }
    cam->Command(oper2);
    return NOTHING;
}

GameValue CamCommit(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj1 = GetObject(oper1);
    CameraHolder* cam = dyn_cast<CameraHolder>(obj1);
    if (!cam)
    {
        return NOTHING;
    }

    float time = oper2;
    cam->Commit(time);
    return NOTHING;
}

template <>
const EnumName* Poseidon::Foundation::GetEnumNames(CameraType)
{
    static const EnumName CameraTypeNames[] = {EnumName(CamInternal, "INTERNAL"), EnumName(CamGunner, "GUNNER"),
                                               EnumName(CamExternal, "EXTERNAL"), EnumName(CamGroup, "GROUP"),
                                               EnumName(CamInternal, "CARGO"),    EnumName()};
    return CameraTypeNames;
}

GameValue ObjSwitchCamera(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }

    GameStringType str = oper2;
    CameraType type = CamInternal;
    const EnumName* names = GetEnumNames(type);
    for (int i = 0; names[i].IsValid(); i++)
    {
        if (stricmp(names[i].name, str) == 0)
        {
            type = (CameraType)names[i].value;
            GWorld->SwitchCameraTo(obj, type);
            return NOTHING;
        }
    }

    return NOTHING;
}

bool ParseEffect(const GameState* state, GameStringType& effect, GameStringType& str, float& speed, GameValuePar oper1)
{
    const GameArrayType& array = oper1;
    if (array.Size() < 2 || array.Size() > 3)
    {
        state->SetError(EvalDim, array.Size(), 2);
        return false;
    }
    if (array[0].GetType() != GameString)
    {
        state->TypeError(GameString, array[0].GetType());
        return false;
    }
    if (array[1].GetType() != GameString)
    {
        state->TypeError(GameString, array[1].GetType());
        return false;
    }

    effect = array[0];
    str = array[1];
    speed = 1;
    if (array.Size() == 3)
    {
        if (array[2].GetType() != GameScalar)
        {
            state->TypeError(GameScalar, array[2].GetType());
            return false;
        }
        speed = array[2];
    }
    return true;
}

static TitEffectName TitEffectFromString(const GameStringType& str)
{
    TitEffectName name = NTitEffects;
    const EnumName* names = GetEnumNames(name);
    for (int i = 0; names[i].IsValid(); i++)
    {
        if (stricmp(names[i].name, str) == 0)
        {
            name = (TitEffectName)names[i].value;
            break;
        }
    }
    return name;
}

#define TITLE_PREFIX                                    \
    float speed;                                        \
    GameStringType effect, str;                         \
    if (!ParseEffect(state, effect, str, speed, oper1)) \
    {                                                   \
        return NOTHING;                                 \
    }                                                   \
    TitEffectName name = TitEffectFromString(str);      \
    if (name == NTitEffects)                            \
        return NOTHING;

GameValue GameTitleText(const GameState* state, GameValuePar oper1)
{
    TITLE_PREFIX

    GWorld->SetTitleEffect(CreateTitleEffect(name, effect, speed));
    return NOTHING;
}

GameValue GameTitleRsc(const GameState* state, GameValuePar oper1)
{
    TITLE_PREFIX

    const ParamEntry* cls = FindRscTitle(effect);
    if (cls)
    {
        GWorld->SetTitleEffect(CreateTitleEffectRsc(name, *cls));
    }
    return NOTHING;
}

GameValue GameTitleObj(const GameState* state, GameValuePar oper1)
{
    TITLE_PREFIX

    GWorld->SetTitleEffect(CreateTitleEffectObj(name, Pars >> "CfgTitles" >> effect));
    return NOTHING;
}

GameValue CutText(const GameState* state, GameValuePar oper1)
{
    TITLE_PREFIX

    GWorld->SetCutEffect(CreateTitleEffect(name, effect, speed));
    return NOTHING;
}

GameValue CutRsc(const GameState* state, GameValuePar oper1)
{
    TITLE_PREFIX

    const ParamEntry* cls = FindRscTitle(effect);
    if (cls)
    {
        GWorld->SetCutEffect(CreateTitleEffectRsc(name, *cls));
    }
    return NOTHING;
}

GameValue CutObj(const GameState* state, GameValuePar oper1)
{
    TITLE_PREFIX

    GWorld->SetCutEffect(CreateTitleEffectObj(name, Pars >> "CfgTitles" >> effect));
    return NOTHING;
}

GameValue SkipDayTime(const GameState* state, GameValuePar oper1)
{
    float time = oper1;

    if (Glob.clock.AdvanceTime(time * OneHour))
    {
        GScene->MainLight()->Recalculate();
        GScene->MainLightChanged();
        GLandscape->OnTimeSkipped();
    }

    return NOTHING;
}

#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/UI/Settings/ViewDistance.hpp>

void SetVisibility(float distance)
{
    // The OFP-ratio derivation (objects = 2/3 VD, shadows = 5/18 VD, clamps) is
    // a pure, unit-tested function (ViewDistanceResolver).  The mission drives
    // the master distance via setViewDistance — e.g. the demo ambush intro's
    // `setViewDistance 2000` renders objects out to ~1333 m so the distant scene
    // is visible through the binoculars.
    const ViewDistances vd = ViewDistanceResolver::Derive(distance);
    LOG_DEBUG(Script, "Visibility set: view={} objects={} shadows={}", vd.view, vd.objects, vd.shadows);
    ENGINE_CONFIG.tacticalZ = vd.view;
    ENGINE_CONFIG.horizontZ = vd.view;
    ENGINE_CONFIG.objectsZ = vd.objects;
    ENGINE_CONFIG.shadowsZ = vd.shadows;
    GLandscape->Simulate(0);
    GScene->ResetFog();
}

GameValue SetTerrainGrid(const GameState* state, GameValuePar oper1)
{
    float preferredGrid = oper1;
    GWorld->AdjustSubdivisionGrid(preferredGrid);
    GLandscape->FillCache(*GScene->GetCamera());
    return NOTHING;
}

GameValue SetViewDistance(const GameState* state, GameValuePar oper1)
{
    float distance = oper1;
    SetVisibility(distance);
    GLandscape->FillCache(*GScene->GetCamera());
    return NOTHING;
}

GameValue SetOvercast(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    GWorld->SetWeather(oper2, -1, oper1);
    return NOTHING;
}

GameValue SetRain(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    GLandscape->SetRain(oper2, oper1);
    return NOTHING;
}

GameValue SetFog(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    GWorld->SetWeather(-1, oper2, oper1);
    return NOTHING;
}

GameValue SetObjectiveStatus(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    GameStringType name = GameStringType("OBJ_") + oper1;
    GameStringType str = oper2;
    ObjectiveStatus status = OSActive; // avoid warning
    const EnumName* names = GetEnumNames(status);
    for (int i = 0; names[i].IsValid(); i++)
    {
        if (stricmp(names[i].name, str) == 0)
        {
            status = (ObjectiveStatus)names[i].value;
            break;
        }
    }
    int value = status;
    int oldValue = toInt((float)state->VarGet(name));
    if (oldValue == value)
    {
        return NOTHING;
    }
    RString message = status == OSDone ? LocalizeString(IDS_OBJECTIVE_DONE) : LocalizeString(IDS_OBJECTIVE_UPDATED);
    if (USER_CONFIG.easyMode && status != OSHidden)
    {
        GWorld->UI()->ShowHint(message);
    }
    const_cast<GameState*>(state)->VarSet(name, GameValue((float)value), true);
    DisplayMap* map = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (map)
    {
        GameVarSpace local;
        state->BeginContext(&local);
        map->UpdatePlan();
        state->EndContext();
    }
    return NOTHING;
}

GameValue MapForce(const GameState* state, GameValuePar oper1)
{
    GWorld->ForceMap(oper1);
    return NOTHING;
}

GameValue MapAnimClear(const GameState* state)
{
    DisplayMap* disp = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (!disp)
    {
        return NOTHING;
    }
    CStaticMap* map = disp->GetMap();
    if (!map)
    {
        return NOTHING;
    }

    map->ClearAnimation();
    return NOTHING;
}

GameValue MapAnimAdd(const GameState* state, GameValuePar oper1)
{
    const GameArrayType& array = oper1;
    if (array.Size() != 3)
    {
        state->SetError(EvalDim, array.Size(), 3);
        return NOTHING;
    }

    if (array[0].GetType() != GameScalar)
    {
        state->TypeError(GameScalar, array[0].GetType());
        return NOTHING;
    }
    float time = array[0];

    if (array[1].GetType() != GameScalar)
    {
        state->TypeError(GameScalar, array[1].GetType());
        return NOTHING;
    }
    float zoom = array[1];

    Vector3 pos;
    if (!GetPos(state, pos, array[2]))
    {
        return NOTHING;
    }

    DisplayMap* disp = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (!disp)
    {
        return NOTHING;
    }
    CStaticMap* map = disp->GetMap();
    if (!map)
    {
        return NOTHING;
    }

    map->AddAnimationPhase(time, zoom, pos);
    return NOTHING;
}

GameValue MapAnimCommit(const GameState* state)
{
    DisplayMap* disp = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (!disp)
    {
        return NOTHING;
    }
    CStaticMap* map = disp->GetMap();
    if (!map)
    {
        return NOTHING;
    }

    map->CreateInterpolator();
    return NOTHING;
}

GameValue MapAnimDone(const GameState* state)
{
    DisplayMap* disp = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (!disp)
    {
        return true;
    }
    CStaticMap* map = disp->GetMap();
    if (!map)
    {
        return true;
    }

    return !map->HasAnimation();
}

GameValue IsMapShown(const GameState* state)
{
    DisplayMap* map = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (!map)
    {
        return false;
    }
    return map->IsShownMap();
}

GameValue IsWatchShown(const GameState* state)
{
    DisplayMap* map = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (!map)
    {
        return false;
    }
    return map->IsShownWatch();
}

GameValue IsCompassShown(const GameState* state)
{
    DisplayMap* map = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (!map)
    {
        return false;
    }
    return map->IsShownCompass();
}

GameValue IsWalkieTalkieShown(const GameState* state)
{
    DisplayMap* map = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (!map)
    {
        return false;
    }
    return map->IsShownWalkieTalkie();
}

GameValue IsNotepadShown(const GameState* state)
{
    DisplayMap* map = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (!map)
    {
        return false;
    }
    return map->IsShownNotepad();
}

GameValue IsWarrantShown(const GameState* state)
{
    DisplayMap* map = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (!map)
    {
        return false;
    }
    return map->IsShownWarrant();
}

GameValue IsGPSShown(const GameState* state)
{
    DisplayMap* map = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (!map)
    {
        return false;
    }
    return map->IsShownGPS();
}

GameValue ObjSide(const GameState* state, GameValuePar oper1)
{
    Object* obj1 = GetObject(oper1);
    EntityAI* ai = dyn_cast<EntityAI>(obj1);
    if (!ai)
    {
        return CreateGameSide(TSideUnknown);
    }
    return CreateGameSide(ai->GetTargetSide());
}

GameValue ObjName(const GameState* state, GameValuePar oper1)
{
    Object* obj1 = GetObject(oper1);
    EntityAI* veh = dyn_cast<EntityAI>(obj1);
    if (!veh)
    {
        return "Error: No vehicle";
    }
    AIUnit* unit = veh->CommanderUnit();
    if (!unit)
    {
        unit = veh->PilotUnit();
    }
    if (!unit)
    {
        unit = veh->GunnerUnit();
    }
    if (!unit)
    {
        return "Error: No unit";
    }
    return unit->GetPerson()->GetInfo()._name;
}

GameValue SideWest(const GameState* state)
{
    return CreateGameSide(TWest);
}
GameValue SideEast(const GameState* state)
{
    return CreateGameSide(TEast);
}
GameValue SideCivilian(const GameState* state)
{
    return CreateGameSide(TCivilian);
}
GameValue SideResistance(const GameState* state)
{
    return CreateGameSide(TGuerrila);
}
GameValue SideLogic(const GameState* state)
{
    return CreateGameSide(TLogic);
}
GameValue SideEnemy(const GameState* state)
{
    return CreateGameSide(TEnemy);
}
GameValue SideFriendly(const GameState* state)
{
    return CreateGameSide(TFriendly);
}

GameValue SideCmpNE(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    TargetSide side1 = GetSide(oper1);
    TargetSide side2 = GetSide(oper2);
    return side1 != side2;
}

GameValue SideCmpE(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    TargetSide side1 = GetSide(oper1);
    TargetSide side2 = GetSide(oper2);
    return side1 == side2;
}

GameValue BoolCmpNE(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    bool b1 = oper1, b2 = oper2;
    return b1 && !b2 || !b1 && b2;
}

GameValue BoolCmpEq(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    bool b1 = oper1, b2 = oper2;
    return b1 && b2 || !b1 && !b2;
}

GameValue SaveGame(const GameState* state)
{
    // do not save when player is dead
    Person* player = GWorld->GetRealPlayer();
    if (!player || player->IsDammageDestroyed())
    {
        return NOTHING;
    }

    RString name = GetSaveDirectory() + RString("autosave.fps");
    GWorld->SaveBin(name, IDS_AUTOSAVE_GAME);
    return NOTHING;
}

GameValue ShowMap(const GameState* state, GameValuePar oper1)
{
    DisplayMap* map = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (map)
    {
        map->ShowMap(oper1);
    }
    return NOTHING;
}

GameValue ShowWatch(const GameState* state, GameValuePar oper1)
{
    DisplayMap* map = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (map)
    {
        map->ShowWatch(oper1);
    }
    return NOTHING;
}

GameValue ShowCompass(const GameState* state, GameValuePar oper1)
{
    DisplayMap* map = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (map)
    {
        map->ShowCompass(oper1);
    }
    return NOTHING;
}

GameValue ShowWalkieTalkie(const GameState* state, GameValuePar oper1)
{
    DisplayMap* map = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (map)
    {
        map->ShowWalkieTalkie(oper1);
    }
    return NOTHING;
}

GameValue ShowNotepad(const GameState* state, GameValuePar oper1)
{
    DisplayMap* map = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (map)
    {
        map->ShowNotepad(oper1);
    }
    return NOTHING;
}

GameValue ShowWarrant(const GameState* state, GameValuePar oper1)
{
    DisplayMap* map = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (map)
    {
        map->ShowWarrant(oper1);
    }
    return NOTHING;
}

GameValue ShowGPS(const GameState* state, GameValuePar oper1)
{
    DisplayMap* map = dynamic_cast<DisplayMap*>(GWorld->Map());
    if (map)
    {
        map->ShowGPS(oper1);
    }
    return NOTHING;
}

GameValue EnableRadio(const GameState* state, GameValuePar oper1)
{
    GWorld->EnableRadio(oper1);
    return NOTHING;
}

GameValue SetRadioMessage(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    float temp = oper1;
    ArcadeSensorActivation activation;
    switch (toInt(temp))
    {
        case 1:
            activation = ASAAlpha;
            break;
        case 2:
            activation = ASABravo;
            break;
        case 3:
            activation = ASACharlie;
            break;
        case 4:
            activation = ASADelta;
            break;
        case 5:
            activation = ASAEcho;
            break;
        case 6:
            activation = ASAFoxtrot;
            break;
        case 7:
            activation = ASAGolf;
            break;
        case 8:
            activation = ASAHotel;
            break;
        case 9:
            activation = ASAIndia;
            break;
        case 10:
            activation = ASAJuliet;
            break;
        default:
            return NOTHING;
    }
    GameStringType message = oper2;
    for (int i = 0; i < sensorsMap.Size(); i++)
    {
        Vehicle* veh = sensorsMap[i];
        Detector* detector = dyn_cast<Detector>(veh);
        if (!detector)
        {
            continue;
        }
        if (detector->GetActivationBy() == activation)
        {
            detector->SetText(message);
        }
    }

    return NOTHING;
}

GameValue ShowHint(const GameState* state, GameValuePar oper1)
{
    if (AppConfig::Instance().IsSimulateMode())
        LOG_INFO(Mission, "[hint] {}", (const char*)oper1.GetText());
    if (GWorld->UI())
        GWorld->UI()->ShowHint(oper1);
    return NOTHING;
}

GameValue ShowHintCadet(const GameState* state, GameValuePar oper1)
{
    if (USER_CONFIG.easyMode && GWorld->UI())
    {
        GWorld->UI()->ShowHint(oper1);
    }
    return NOTHING;
}

GameValue ShowHintC(const GameState* state, GameValuePar oper1)
{
    if (AppConfig::Instance().IsSimulateMode())
        LOG_INFO(Mission, "[hintC] {}", (const char*)oper1.GetText());
    Display* disp = Poseidon::CurrentDisplay();
    if (!disp || disp->IDD() != IDD_MISSION)
    {
        return NOTHING;
    }
    PoseidonAssert(dynamic_cast<DisplayMission*>(disp));
    DisplayMission* mission = static_cast<DisplayMission*>(disp);
    mission->ShowHint(oper1);
    return NOTHING;
}

GameValue DisableUserInput(const GameState* state, GameValuePar oper1)
{
    GWorld->DisableUserInput(oper1);
    return NOTHING;
}

GameValue PublicVariable(const GameState* state, GameValuePar oper1)
{
    GetNetworkManager().PublicVariable(oper1);
    return NOTHING;
}

GameValue ObjLocked(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return false;
    }
    Transport* veh = dyn_cast<Transport>(obj);
    if (!veh)
    {
        return false;
    }
    return veh->GetLock() == LSLocked;
}

GameValue ObjLock(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }
    Transport* veh = dyn_cast<Transport>(obj);
    if (!veh)
    {
        return NOTHING;
    }
    bool lock = oper2;
    veh->SetLock(lock ? LSLocked : LSDefault);
    return NOTHING;
}

GameValue ObjStopped(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return false;
    }
    EntityAI* veh = dyn_cast<EntityAI>(obj);
    if (!veh)
    {
        return false;
    }
    return veh->IsUserStopped();
}

GameValue ObjStop(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }
    EntityAI* veh = dyn_cast<EntityAI>(obj);
    if (!veh)
    {
        return NOTHING;
    }
    veh->UserStop(oper2);
    return NOTHING;
}

GameValue ObjDisableAI(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }
    EntityAI* veh = dyn_cast<EntityAI>(obj);
    if (!veh)
    {
        return NOTHING;
    }
    AIUnit* unit = veh->CommanderUnit();
    if (!unit)
    {
        return NOTHING;
    }
    GameStringType str = oper2;
    const char* ss = str;
    AIUnit::DisabledAI s = GetEnumValue<AIUnit::DisabledAI>(ss);
    int dai = unit->GetAIDisabled();
    if (s == INT_MIN)
    {
        s = (AIUnit::DisabledAI)0;
    }
    unit->SetAIDisabled(dai | s);
    return NOTHING;
}

GameValue ObjLand(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }
    Transport* veh = dyn_cast<Transport>(obj);
    if (!veh)
    {
        return NOTHING;
    }
    GameStringType name = oper2;
    Transport::LandingMode mode = GetEnumValue<Transport::LandingMode>((const char*)name);
    if (mode == INT_MIN)
    {
        mode = Transport::LMLand;
    }
    veh->LandStarted(mode);
    return NOTHING;
}

GameValue ObjAssignAsCommander(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj1 = GetObject(oper1);
    if (!obj1)
    {
        return NOTHING;
    }
    Person* soldier = dyn_cast<Person>(obj1);
    if (!soldier)
    {
        return NOTHING;
    }
    AIUnit* unit = soldier->Brain();
    if (!unit)
    {
        return NOTHING;
    }

    Object* obj2 = GetObject(oper2);
    if (!obj2)
    {
        return NOTHING;
    }
    Transport* veh = dyn_cast<Transport>(obj2);
    if (!veh)
    {
        return NOTHING;
    }

    unit->AssignAsCommander(veh);
    return NOTHING;
}

GameValue ObjAssignAsDriver(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj1 = GetObject(oper1);
    if (!obj1)
    {
        return NOTHING;
    }
    Person* soldier = dyn_cast<Person>(obj1);
    if (!soldier)
    {
        return NOTHING;
    }
    AIUnit* unit = soldier->Brain();
    if (!unit)
    {
        return NOTHING;
    }

    Object* obj2 = GetObject(oper2);
    if (!obj2)
    {
        return NOTHING;
    }
    Transport* veh = dyn_cast<Transport>(obj2);
    if (!veh)
    {
        return NOTHING;
    }

    unit->AssignAsDriver(veh);
    return NOTHING;
}

GameValue ObjAssignAsGunner(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj1 = GetObject(oper1);
    if (!obj1)
    {
        return NOTHING;
    }
    Person* soldier = dyn_cast<Person>(obj1);
    if (!soldier)
    {
        return NOTHING;
    }
    AIUnit* unit = soldier->Brain();
    if (!unit)
    {
        return NOTHING;
    }

    Object* obj2 = GetObject(oper2);
    if (!obj2)
    {
        return NOTHING;
    }
    Transport* veh = dyn_cast<Transport>(obj2);
    if (!veh)
    {
        return NOTHING;
    }

    unit->AssignAsGunner(veh);
    return NOTHING;
}

GameValue ObjAssignAsCargo(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj1 = GetObject(oper1);
    if (!obj1)
    {
        return NOTHING;
    }
    Person* soldier = dyn_cast<Person>(obj1);
    if (!soldier)
    {
        return NOTHING;
    }
    AIUnit* unit = soldier->Brain();
    if (!unit)
    {
        return NOTHING;
    }

    Object* obj2 = GetObject(oper2);
    if (!obj2)
    {
        return NOTHING;
    }
    Transport* veh = dyn_cast<Transport>(obj2);
    if (!veh)
    {
        return NOTHING;
    }

    unit->AssignAsCargo(veh);
    return NOTHING;
}

GameValue ObjUnassignVehicle(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }
    Person* soldier = dyn_cast<Person>(obj);
    if (!soldier)
    {
        return NOTHING;
    }
    AIUnit* unit = soldier->Brain();
    if (!unit)
    {
        return NOTHING;
    }

    unit->UnassignVehicle();
    return NOTHING;
}

GameValue GrpLeaveVehicle(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    AIGroup* grp = GetGroup(oper1);
    if (!grp)
    {
        return NOTHING;
    }

    Object* obj = GetObject(oper2);
    if (!obj)
    {
        return NOTHING;
    }
    Transport* veh = dyn_cast<Transport>(obj);
    if (!veh)
    {
        return NOTHING;
    }

    grp->UnassignVehicle(veh);
    return NOTHING;
}

static bool PrepareMoveIn(AIUnit* unit, Transport* target)
{
    Transport* in = unit->GetVehicleIn();
    if (in == target)
    {
        return false;
    }
    if (in)
    {
        RptF("MoveIn: Soldier %s already in vehicle %s", (const char*)unit->GetDebugName(),
             (const char*)in->GetDebugName());
        return false;
    }
    return true;
}

CameraType ValidateCamera(CameraType cam);

GameValue ObjMoveInCommander(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj1 = GetObject(oper1);
    if (!obj1)
    {
        return NOTHING;
    }
    Person* soldier = dyn_cast<Person>(obj1);
    if (!soldier)
    {
        return NOTHING;
    }
    if (!soldier->IsLocal())
    {
        return NOTHING;
    }
    AIUnit* unit = soldier->Brain();
    if (!unit)
    {
        return NOTHING;
    }

    Object* obj2 = GetObject(oper2);
    if (!obj2)
    {
        return NOTHING;
    }
    Transport* veh = dyn_cast<Transport>(obj2);
    if (!veh)
    {
        return NOTHING;
    }
    if (!PrepareMoveIn(unit, veh))
    {
        return NOTHING;
    }

    if (veh->QCanIGetInCommander(soldier))
    {
        if (veh->IsLocal())
        {
            veh->GetInCommander(soldier, false);
        }
        else
        {
            GetNetworkManager().AskForGetIn(soldier, veh, GIPCommander);
        }
        soldier->Brain()->AssignAsCommander(veh);
        soldier->Brain()->OrderGetIn(true);
        if (GWorld->FocusOn() == soldier->Brain() && veh->IsLocal())
        {
            GWorld->SwitchCameraTo(veh, ValidateCamera(GWorld->GetCameraType()));
        }
    }
    return NOTHING;
}

GameValue ObjMoveInDriver(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj1 = GetObject(oper1);
    if (!obj1)
    {
        return NOTHING;
    }
    Person* soldier = dyn_cast<Person>(obj1);
    if (!soldier)
    {
        return NOTHING;
    }
    if (!soldier->IsLocal())
    {
        return NOTHING;
    }
    AIUnit* unit = soldier->Brain();
    if (!unit)
    {
        return NOTHING;
    }

    Object* obj2 = GetObject(oper2);
    if (!obj2)
    {
        return NOTHING;
    }
    Transport* veh = dyn_cast<Transport>(obj2);
    if (!veh)
    {
        return NOTHING;
    }
    if (!PrepareMoveIn(unit, veh))
    {
        return NOTHING;
    }

    if (veh->QCanIGetIn(soldier))
    {
        if (veh->IsLocal())
        {
            veh->GetInDriver(soldier, false);
        }
        else
        {
            GetNetworkManager().AskForGetIn(soldier, veh, GIPDriver);
        }
        soldier->Brain()->AssignAsDriver(veh);
        soldier->Brain()->OrderGetIn(true);
        if (GWorld->FocusOn() == soldier->Brain() && veh->IsLocal())
        {
            GWorld->SwitchCameraTo(veh, ValidateCamera(GWorld->GetCameraType()));
        }
    }
    return NOTHING;
}

GameValue ObjMoveInGunner(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj1 = GetObject(oper1);
    if (!obj1)
    {
        return NOTHING;
    }
    Person* soldier = dyn_cast<Person>(obj1);
    if (!soldier)
    {
        return NOTHING;
    }
    if (!soldier->IsLocal())
    {
        return NOTHING;
    }
    AIUnit* unit = soldier->Brain();
    if (!unit)
    {
        return NOTHING;
    }

    Object* obj2 = GetObject(oper2);
    if (!obj2)
    {
        return NOTHING;
    }
    Transport* veh = dyn_cast<Transport>(obj2);
    if (!veh)
    {
        return NOTHING;
    }
    if (!PrepareMoveIn(unit, veh))
    {
        return NOTHING;
    }

    if (veh->QCanIGetInGunner(soldier))
    {
        if (veh->IsLocal())
        {
            veh->GetInGunner(soldier, false);
        }
        else
        {
            GetNetworkManager().AskForGetIn(soldier, veh, GIPGunner);
        }
        soldier->Brain()->AssignAsGunner(veh);
        soldier->Brain()->OrderGetIn(true);
        if (GWorld->FocusOn() == soldier->Brain() && veh->IsLocal())
        {
            GWorld->SwitchCameraTo(veh, ValidateCamera(GWorld->GetCameraType()));
        }
    }
    return NOTHING;
}

GameValue ObjMoveInCargo(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj1 = GetObject(oper1);
    if (!obj1)
    {
        return NOTHING;
    }
    Person* soldier = dyn_cast<Person>(obj1);
    if (!soldier)
    {
        return NOTHING;
    }
    if (!soldier->IsLocal())
    {
        return NOTHING;
    }
    AIUnit* unit = soldier->Brain();
    if (!unit)
    {
        return NOTHING;
    }

    Object* obj2 = GetObject(oper2);
    if (!obj2)
    {
        return NOTHING;
    }
    Transport* veh = dyn_cast<Transport>(obj2);
    if (!veh)
    {
        return NOTHING;
    }
    if (!PrepareMoveIn(unit, veh))
    {
        return NOTHING;
    }

    if (veh->QCanIGetInCargo(soldier))
    {
        if (veh->IsLocal())
        {
            veh->GetInCargo(soldier, false);
        }
        else
        {
            GetNetworkManager().AskForGetIn(soldier, veh, GIPCargo);
        }
        soldier->Brain()->SetState(AIUnit::InCargo);
        soldier->Brain()->AssignAsCargo(veh);
        soldier->Brain()->OrderGetIn(true);
        if (GWorld->FocusOn() == soldier->Brain() && veh->IsLocal())
        {
            GWorld->SwitchCameraTo(veh, ValidateCamera(GWorld->GetCameraType()));
        }
    }
    return NOTHING;
}

GameValue GrpAllowFleeing(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    AIGroup* grp = GetGroup(oper1);
    if (!grp)
    {
        return NOTHING;
    }

    grp->AllowFleeing(oper2);
    return NOTHING;
}
