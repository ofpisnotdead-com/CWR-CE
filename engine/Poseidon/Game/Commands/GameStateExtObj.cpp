
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Core/Version.hpp>
#include <Poseidon/World/Scene/Object.hpp>
#include <Poseidon/World/Detection/Detector.hpp>
#include <Poseidon/World/Scene/ObjectClasses.hpp>
#include <Poseidon/World/Scene/Fireplace.hpp>

#include <Poseidon/Game/Commands/GameStateExt.hpp>

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
#include <Poseidon/World/Scene/Camera/CameraHold.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Core/resincl.hpp>

#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>

#include <Poseidon/Game/UiActions.hpp>

#include <ctype.h>
#include <time.h>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/BankArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

using namespace Poseidon;
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

GameValue ObjNull(const GameState* state)
{
    return OBJECT_NULL;
}

GameValue GrpNull(const GameState* state)
{
    return GROUP_NULL;
}

GameValue ObjIsNull(const GameState* state, GameValuePar oper1)
{
    if (oper1.GetType() == GameObject)
    {
        Object* obj = static_cast<GameDataObject*>(oper1.GetData())->GetObject();
        return !obj;
    }
    else
    {
        return true;
    }
}

GameValue GrpIsNull(const GameState* state, GameValuePar oper1)
{
    if (oper1.GetType() == GameGroup)
    {
        AIGroup* grp = static_cast<GameDataGroup*>(oper1.GetData())->GetGroup();
        return !grp;
    }
    else
    {
        return true;
    }
}

GameValue ObjAlive(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return false;
    }
    return (!obj->IsDammageDestroyed());
}

GameValue ObjCanMove(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return false;
    }
    if (obj->IsDammageDestroyed())
    {
        return false;
    }
    EntityAI* ai = dyn_cast<EntityAI>(obj);
    if (!ai)
    {
        return false;
    }
    return ai->IsAbleToMove();
}

GameValue ObjCanFire(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return false;
    }
    if (obj->IsDammageDestroyed())
    {
        return false;
    }
    EntityAI* ai = dyn_cast<EntityAI>(obj);
    if (!ai)
    {
        return false;
    }
    return ai->IsAbleToFire();
}

GameValue ObjIsLocal(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return false;
    }
    return obj->IsLocal();
}

GameValue ObjFlee(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return false;
    }
    if (obj->IsDammageDestroyed())
    {
        return false;
    }
    EntityAI* ai = dyn_cast<EntityAI>(obj);
    if (!ai)
    {
        return false;
    }
    AIGroup* grp = ai->GetGroup();
    if (!grp)
    {
        return false;
    }
    return grp->GetFlee();
}

GameValue ObjSetFuel(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }
    if (obj->IsDammageDestroyed())
    {
        return NOTHING;
    }
    EntityAI* ai = dyn_cast<EntityAI>(obj);
    if (!ai)
    {
        return NOTHING;
    }
    float maxFuel = ai->GetType()->GetFuelCapacity();
    float ammount = oper2;
    ai->Refuel(ammount * maxFuel - ai->GetFuel());
    return NOTHING;
}

GameValue ObjFuel(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return 0.0f;
    }
    if (obj->IsDammageDestroyed())
    {
        return 0.0f;
    }
    EntityAI* ai = dyn_cast<EntityAI>(obj);
    if (!ai)
    {
        return 0.0f;
    }
    float maxFuel = ai->GetType()->GetFuelCapacity();
    float fuel = maxFuel <= 0 ? 1 : ai->GetFuel() / maxFuel;
    return fuel;
}

GameValue ObjSetFuelCargo(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }
    if (obj->IsDammageDestroyed())
    {
        return NOTHING;
    }
    VehicleSupply* veh = dyn_cast<VehicleSupply>(obj);
    if (!veh)
    {
        return NOTHING;
    }
    float max = veh->GetType()->GetMaxFuelCargo();
    float current = veh->GetFuelCargo();
    float ammount = oper2;
    veh->LoadFuelCargo(ammount * max - current);
    return NOTHING;
}

GameValue ObjSetRepairCargo(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }
    if (obj->IsDammageDestroyed())
    {
        return NOTHING;
    }
    VehicleSupply* veh = dyn_cast<VehicleSupply>(obj);
    if (!veh)
    {
        return NOTHING;
    }
    float max = veh->GetType()->GetMaxRepairCargo();
    float current = veh->GetRepairCargo();
    float ammount = oper2;
    veh->LoadRepairCargo(ammount * max - current);
    return NOTHING;
}

GameValue ObjSetAmmoCargo(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }
    if (obj->IsDammageDestroyed())
    {
        return NOTHING;
    }
    VehicleSupply* veh = dyn_cast<VehicleSupply>(obj);
    if (!veh)
    {
        return NOTHING;
    }
    float max = veh->GetType()->GetMaxAmmoCargo();
    float current = veh->GetAmmoCargo();
    float ammount = oper2;
    veh->LoadAmmoCargo(ammount * max - current);
    return NOTHING;
}

GameValue ObjSetInfantryAmmoCargo(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    return NOTHING;
}

GameValue ObjClearWeaponCargo(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }
    if (obj->IsDammageDestroyed())
    {
        return NOTHING;
    }
    VehicleSupply* veh = dyn_cast<VehicleSupply>(obj);
    if (!veh)
    {
        return NOTHING;
    }
    veh->ClearWeaponCargo();
    return NOTHING;
}

GameValue ObjClearMagazineCargo(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }
    if (obj->IsDammageDestroyed())
    {
        return NOTHING;
    }
    VehicleSupply* veh = dyn_cast<VehicleSupply>(obj);
    if (!veh)
    {
        return NOTHING;
    }
    veh->ClearMagazineCargo();
    return NOTHING;
}

GameValue ObjAddWeaponCargo(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }
    if (obj->IsDammageDestroyed())
    {
        return NOTHING;
    }
    VehicleSupply* veh = dyn_cast<VehicleSupply>(obj);
    if (!veh)
    {
        return NOTHING;
    }

    const GameArrayType& array = oper2;
    if (array.Size() != 2)
    {
        return NOTHING;
    }
    if (array[0].GetType() != GameString || array[1].GetType() != GameScalar)
    {
        return NOTHING;
    }

    RString name = array[0];
    Ref<WeaponType> weapon = WeaponTypes.New(name);
    if (!weapon)
    {
        return NOTHING;
    }
    int count = toInt((float)array[1]);
    veh->AddWeaponCargo(weapon, count, true);

    return NOTHING;
}

GameValue ObjAddMagazineCargo(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }
    if (obj->IsDammageDestroyed())
    {
        return NOTHING;
    }
    VehicleSupply* veh = dyn_cast<VehicleSupply>(obj);
    if (!veh)
    {
        return NOTHING;
    }

    const GameArrayType& array = oper2;
    if (array.Size() != 2)
    {
        return NOTHING;
    }
    if (array[0].GetType() != GameString || array[1].GetType() != GameScalar)
    {
        return NOTHING;
    }

    RString name = array[0];
    Ref<MagazineType> magazine = MagazineTypes.New(name);
    if (!magazine)
    {
        return NOTHING;
    }
    int count = toInt((float)array[1]);
    veh->AddMagazineCargo(magazine, count, true);
    return NOTHING;
}

GameValue ObjSelectWeapon(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }
    if (obj->IsDammageDestroyed())
    {
        return NOTHING;
    }
    EntityAI* veh = dyn_cast<EntityAI>(obj);
    if (!veh)
    {
        return NOTHING;
    }

    RString name = oper2;
    for (int i = 0; i < veh->NMagazineSlots(); i++)
    {
        const MuzzleType* muzzle = veh->GetMagazineSlot(i)._muzzle;
        if (muzzle && stricmp(muzzle->GetName(), name) == 0)
        {
            veh->SelectWeapon(i);
            return NOTHING;
        }
    }
    return NOTHING;
}

GameValue ObjExperience(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return 0.0f;
    }
    if (obj->IsDammageDestroyed())
    {
        return 0.0f;
    }
    EntityAI* ai = dyn_cast<EntityAI>(obj);
    if (!ai)
    {
        return 0.0f;
    }
    AIUnit* unit = ai->CommanderUnit();
    if (!unit)
    {
        return 0.0f;
    }
    return unit->GetPerson()->GetExperience();
}

GameValue ObjGetSkill(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return 0.0f;
    }
    if (obj->IsDammageDestroyed())
    {
        return 0.0f;
    }
    EntityAI* ai = dyn_cast<EntityAI>(obj);
    if (!ai)
    {
        return 0.0f;
    }
    AIUnit* unit = ai->CommanderUnit();
    if (!unit)
    {
        return 0.0f;
    }
    return unit->GetAbility();
}

GameValue ObjGetPrimaryWeapon(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return "";
    }
    EntityAI* ai = dyn_cast<EntityAI>(obj);
    if (!ai)
    {
        return "";
    }

    for (int i = 0; i < ai->NWeaponSystems(); i++)
    {
        const WeaponType* weapon = ai->GetWeaponSystem(i);
        if (!weapon)
        {
            continue;
        }
        if ((weapon->_weaponType & MaskSlotPrimary) != 0)
        {
            return weapon->GetName();
        }
    }

    return "";
}

GameValue ObjGetSecondaryWeapon(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return "";
    }
    EntityAI* ai = dyn_cast<EntityAI>(obj);
    if (!ai)
    {
        return "";
    }

    for (int i = 0; i < ai->NWeaponSystems(); i++)
    {
        const WeaponType* weapon = ai->GetWeaponSystem(i);
        if (!weapon)
        {
            continue;
        }
        if ((weapon->_weaponType & MaskSlotSecondary) != 0 && (weapon->_weaponType & MaskSlotPrimary) == 0)
        {
            return weapon->GetName();
        }
    }

    return "";
}

GameValue ObjGetAllWeapons(const GameState* state, GameValuePar oper1)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;

    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return value;
    }
    EntityAI* ai = dyn_cast<EntityAI>(obj);
    if (!ai)
    {
        return value;
    }

    for (int i = 0; i < ai->NWeaponSystems(); i++)
    {
        const WeaponType* weapon = ai->GetWeaponSystem(i);
        if (!weapon)
        {
            continue;
        }
        if (weapon->_weaponType == 0)
        {
            continue;
        }
        array.Add(weapon->GetName());
    }

    return value;
}

GameValue ObjGetAllMagazines(const GameState* state, GameValuePar oper1)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;

    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return value;
    }
    EntityAI* ai = dyn_cast<EntityAI>(obj);
    if (!ai)
    {
        return value;
    }

    for (int i = 0; i < ai->NMagazines(); i++)
    {
        const Magazine* magazine = ai->GetMagazine(i);
        if (!magazine)
        {
            continue;
        }
        const MagazineType* type = magazine->_type;
        if (type)
        {
            array.Add(type->GetName());
        }
    }

    return value;
}

GameValue GetObject(const GameState* state, GameValuePar oper1)
{
    int id = toInt((float)oper1);
    Object* obj = GLandscape->GetObject(id);
    if (!obj)
    {
        return GameValueExt((Object*)nullptr);
    }
    if (obj->GetType() != Primary && obj->GetType() != Network)
    {
        return GameValueExt((Object*)nullptr);
    }
    return GameValueExt(obj);
}

GameValue ObjSetCaptive(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }
    if (obj->IsDammageDestroyed())
    {
        return NOTHING;
    }
    EntityAI* ai = dyn_cast<EntityAI>(obj);
    if (!ai)
    {
        return NOTHING;
    }
    AIUnit* unit = ai->CommanderUnit();
    if (!unit)
    {
        return NOTHING;
    }
    unit->SetCaptive(oper2);
    return NOTHING;
}

GameValue ObjCaptive(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return false;
    }
    if (obj->IsDammageDestroyed())
    {
        return false;
    }
    EntityAI* ai = dyn_cast<EntityAI>(obj);
    if (!ai)
    {
        return false;
    }
    AIUnit* unit = ai->CommanderUnit();
    if (!unit)
    {
        return false;
    }
    return unit->GetCaptive();
}

GameValue ObjAddExperience(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }
    if (obj->IsDammageDestroyed())
    {
        return NOTHING;
    }
    EntityAI* ai = dyn_cast<EntityAI>(obj);
    if (!ai)
    {
        return NOTHING;
    }
    AIUnit* unit = ai->CommanderUnit();
    if (!unit)
    {
        return NOTHING;
    }
    float exp = oper2;
    unit->GetPerson()->GetInfo()._experience += exp;
    return NOTHING;
}

GameValue ObjSetSkill(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }
    if (obj->IsDammageDestroyed())
    {
        return NOTHING;
    }
    EntityAI* ai = dyn_cast<EntityAI>(obj);
    if (!ai)
    {
        return NOTHING;
    }
    AIUnit* unit = ai->CommanderUnit();
    if (!unit)
    {
        return NOTHING;
    }
    float skill = oper2;
    saturate(skill, 0, 1);
    unit->SetAbility(skill);
    return NOTHING;
}

GameValue ObjAddScore(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }
    EntityAI* ai = dyn_cast<EntityAI>(obj);
    if (!ai)
    {
        return NOTHING;
    }
    AIUnit* unit = ai->CommanderUnit();
    if (!unit)
    {
        return NOTHING;
    }
    int score = toInt((float)oper2);
    TargetSide playerSide = TSideUnknown;
    AIGroup* grp = unit->GetGroup();
    if (grp)
    {
        AICenter* center = grp->GetCenter();
        if (center)
        {
            playerSide = center->GetSide();
        }
    }
    GStats._mission.AddMPScore(unit->GetPerson()->GetInfo()._name, playerSide, score);
    return NOTHING;
}

GameValue ObjGetScore(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return 0.0f;
    }
    EntityAI* ai = dyn_cast<EntityAI>(obj);
    if (!ai)
    {
        return 0.0f;
    }
    AIUnit* unit = ai->CommanderUnit();
    if (!unit)
    {
        return 0.0f;
    }
    RString name = unit->GetPerson()->GetInfo()._name;
    for (int i = 0; i < GStats._mission._tableMP.Size(); i++)
    {
        const AIStatsMPRow& row = GStats._mission._tableMP[i];
        if (stricmp(name, row.player) == 0)
        {
            return (float)row.killsTotal;
        }
    }
    return 0.0f;
}

GameValue ObjSomeAmmo(const GameState* state, GameValuePar oper1)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return false;
    }
    if (obj->IsDammageDestroyed())
    {
        return false;
    }
    EntityAI* ai = dyn_cast<EntityAI>(obj);
    if (!ai)
    {
        return false;
    }
    for (int i = 0; i < ai->NMagazines(); i++)
    {
        Magazine* magazine = ai->GetMagazine(i);
        if (!magazine)
        {
            continue;
        }
        if (magazine->_ammo > 0)
        {
            return true;
        }
    }
    return false;
}

GameValue ObjDistance(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj1 = GetObject(oper1);
    Object* obj2 = GetObject(oper2);
    if (!obj1 || !obj2)
    {
        return 1e10f;
    }

    Vector3 pos1 = obj1->WorldPosition();
    Vector3 pos2 = obj2->WorldPosition();

    return pos1.Distance(pos2);
}

GameValue ObjListIn(const GameState* state, GameValuePar oper1)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;

    Object* obj1 = GetObject(oper1);
    if (!obj1)
    {
        return value;
    }

    Transport* trans = dyn_cast<Transport>(obj1);
    if (!trans)
    {
        array.Add(GameValueExt(obj1));
        return value;
    }
// add crew and cargo
#define ADD_MAN(man)                      \
    {                                     \
        Object* obj = man;                \
        if (obj)                          \
            array.Add(GameValueExt(obj)); \
    }
    ADD_MAN(trans->Driver());
    ADD_MAN(trans->Commander());
    ADD_MAN(trans->Gunner());
    const ManCargo& cargo = trans->GetManCargo();
    for (int i = 0; i < cargo.Size(); i++)
        ADD_MAN(cargo[i]);
#undef ADD_MAN

    return value;
}

GameValue ObjIn(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj1 = GetObject(oper1);
    Object* obj2 = GetObject(oper2);
    if (!obj1 || !obj2)
    {
        return false;
    }

    EntityAI* ai = dyn_cast<EntityAI>(obj1);
    EntityAI* trans = dyn_cast<EntityAI>(obj2);
    if (!ai || !trans)
    {
        return false;
    }

    AIUnit* unit = ai->CommanderUnit();
    if (!unit)
    {
        return false; // unit is dead - cannot be in
    }

    return (unit->GetVehicle() == trans);
}

GameValue ObjAmmo(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    EntityAI* ai = dyn_cast<EntityAI>(GetObject(oper1));
    if (!ai)
    {
        return 0.0f;
    }
    GameStringType weapon = oper2;
    for (int i = 0; i < ai->NMagazineSlots(); i++)
    {
        const MagazineSlot& slot = ai->GetMagazineSlot(i);
        if (!slot._magazine)
        {
            continue;
        }
        if (strcmpi(slot._muzzle->GetName(), weapon) != 0)
        {
            continue;
        }
        return float(slot._magazine->_ammo);
    }
    return 0.0f;
}

GameValue ObjMagazinesArray(const GameState* state, GameValuePar oper1)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;

    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return value;
    }
    EntityAI* ai = dyn_cast<EntityAI>(obj);
    if (!ai)
    {
        return value;
    }

    for (int i = 0; i < ai->NMagazines(); i++)
    {
        const Magazine* magazine = ai->GetMagazine(i);
        if (!magazine)
        {
            continue;
        }
        const MagazineType* type = magazine->_type;
        if (type)
        {
            array.Add(type->GetName());
            array.Add(float(magazine->_ammo));
        }
    }

    return value;
}

GameValue ObjUnitLoadout(const GameState* state, GameValuePar oper1)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& loadout = value;

    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return value;
    }
    EntityAI* ai = dyn_cast<EntityAI>(obj);
    if (!ai)
    {
        return value;
    }

    GameValue weapons = state->CreateGameValue(GameArray);
    GameArrayType& weaponArray = weapons;
    for (int i = 0; i < ai->NWeaponSystems(); i++)
    {
        const WeaponType* weapon = ai->GetWeaponSystem(i);
        if (!weapon || weapon->_weaponType == 0)
        {
            continue;
        }
        weaponArray.Add(weapon->GetName());
    }

    GameValue magazines = state->CreateGameValue(GameArray);
    GameArrayType& magazineArray = magazines;
    for (int i = 0; i < ai->NMagazines(); i++)
    {
        const Magazine* magazine = ai->GetMagazine(i);
        if (!magazine || !magazine->_type)
        {
            continue;
        }
        magazineArray.Add(magazine->_type->GetName());
        magazineArray.Add(static_cast<float>(magazine->_ammo));
    }

    RString selectedWeapon;
    const int selected = ai->SelectedWeapon();
    if (selected >= 0 && selected < ai->NMagazineSlots())
    {
        const MuzzleType* muzzle = ai->GetMagazineSlot(selected)._muzzle;
        if (muzzle)
        {
            selectedWeapon = muzzle->GetName();
        }
    }

    loadout.Add(weapons);
    loadout.Add(magazines);
    loadout.Add(selectedWeapon);
    return value;
}

GameValue ObjSetUnitLoadout(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }
    EntityAI* ai = dyn_cast<EntityAI>(obj);
    if (!ai)
    {
        return NOTHING;
    }

    const GameArrayType& loadout = oper2;
    if (loadout.Size() < 2)
    {
        state->SetError(EvalDim, loadout.Size(), 2);
        return NOTHING;
    }
    if (!CheckType(state, loadout[0], GameArray) || !CheckType(state, loadout[1], GameArray))
    {
        return NOTHING;
    }
    if (loadout.Size() >= 3 && loadout[2].GetType() != GameString)
    {
        state->TypeError(GameString, loadout[2].GetType());
        return NOTHING;
    }

    const GameArrayType& weapons = loadout[0];
    const GameArrayType& magazines = loadout[1];
    for (int i = 0; i < weapons.Size(); ++i)
    {
        if (!CheckType(state, weapons[i], GameString))
        {
            return NOTHING;
        }
    }
    if ((magazines.Size() % 2) != 0)
    {
        state->SetError(EvalDim, magazines.Size(), magazines.Size() + 1);
        return NOTHING;
    }
    for (int i = 0; i < magazines.Size(); i += 2)
    {
        if (!CheckType(state, magazines[i], GameString) || !CheckType(state, magazines[i + 1], GameScalar))
        {
            return NOTHING;
        }
    }

    ai->RemoveAllWeapons();
    ai->RemoveAllMagazines();

    for (int i = 0; i < magazines.Size(); i += 2)
    {
        GameStringType magazineName = magazines[i];
        const int ammo = toInt(static_cast<float>(magazines[i + 1]));
        Ref<MagazineType> magazineType = MagazineTypes.New(magazineName);
        if (!magazineType)
        {
            continue;
        }
        Ref<Magazine> magazine = new Magazine(magazineType);
        magazine->_ammo = ammo >= 0 ? ammo : magazineType->_maxAmmo;
        ai->AddMagazine(magazine, false);
    }

    for (int i = 0; i < weapons.Size(); ++i)
    {
        GameStringType weaponName = weapons[i];
        ai->AddWeapon(weaponName);
    }

    ai->AutoReloadAll();

    if (loadout.Size() >= 3)
    {
        GameStringType selectedWeapon = loadout[2];
        if (selectedWeapon.GetLength() > 0)
        {
            for (int i = 0; i < ai->NMagazineSlots(); i++)
            {
                const MuzzleType* muzzle = ai->GetMagazineSlot(i)._muzzle;
                if (muzzle && stricmp(muzzle->GetName(), selectedWeapon) == 0)
                {
                    ai->SelectWeapon(i);
                    break;
                }
            }
        }
    }

    return NOTHING;
}

GameValue ObjAmmoArray(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;

    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return value;
    }
    EntityAI* ai = dyn_cast<EntityAI>(obj);
    if (!ai)
    {
        return value;
    }

    GameStringType muzzle = oper2;
    const MagazineSlot* pSlot = ai->GetmagazineSlotByMuzzle(muzzle);
    if (!pSlot)
    {
        return value;
    }

    const Magazine* magazine = pSlot->_magazine;
    if (!magazine)
    {
        return value;
    }
    const float round = magazine->_ammo;

    const MagazineType* type = magazine ? magazine->_type : nullptr;
    if (!type)
    {
        return value;
    }

    // Emit (magazine name, count) for each fire mode; count is a magazine attribute, so it repeats.

    const RStringB& magName = type->GetName();
    const int modeCnt = type->_modes.Size();
    array.Resize(2 * modeCnt);
    for (int modeIdx = 0; modeIdx < modeCnt; ++modeIdx)
    {
        array[modeIdx * 2] = magName;
        array[modeIdx * 2 + 1] = round;
    }
    return value;
}

GameValue ObjAddMagazinePrecise(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }
    EntityAI* ai = dyn_cast<EntityAI>(obj);
    if (!ai)
    {
        return NOTHING;
    }

    const GameArrayType& array = oper2;
    if (!CheckSize(state, array, 2))
    {
        return NOTHING;
    }
    if (!CheckType(state, array[0], GameString))
    {
        return NOTHING;
    }
    if (!CheckType(state, array[1], GameScalar))
    {
        return NOTHING;
    }

    GameStringType magName = array[0];
    const ParamEntry* ammoClass = (Pars >> "CfgWeapons").FindEntry(magName);
    if (!ammoClass)
    {
        return NOTHING;
    }

    int round = toInt((float)array[1]);
    if (round <= 0)
    {
        return NOTHING;
    }

    ai->AddMagazine(magName, false, round); // force is default value false
    ai->AutoReloadAll();

    return NOTHING;
}

GameValue ObjGetHitpointsNames(const GameState* state, GameValuePar oper1)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;

    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return value;
    }
    EntityAI* ai = dyn_cast<EntityAI>(obj);
    if (!ai)
    {
        return value;
    }

    const int hitPointsCnt = ai->GetType()->GetHitPoints().Size();
    if (hitPointsCnt <= 0)
    {
        return value;
    }

    array.Resize(hitPointsCnt);
    for (int i = 0; i < hitPointsCnt; ++i)
    {
        array[i] = ai->HitpointName(i);
    }

    return value;
}

GameValue ObjGetSelectionDammage(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return 0.0f;
    }
    EntityAI* ai = dyn_cast<EntityAI>(obj);
    if (!ai)
    {
        return 0.0f;
    }

    GameStringType name = oper2;

    const int hitPointsCnt = ai->GetType()->GetHitPoints().Size();
    if (hitPointsCnt <= 0)
    {
        return 0.0f;
    }

    for (int i = 0; i < hitPointsCnt; ++i)
    {
        if (stricmp(ai->HitpointName(i), name) == 0)
        {
            return ai->GetHitCont(*ai->GetType()->GetHitPoints()[i]);
        }
    }

    return 0.0f;
}

GameValue ObjSetSelectionDammage(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return NOTHING;
    }
    EntityAI* ai = dyn_cast<EntityAI>(obj);
    if (!ai)
    {
        return NOTHING;
    }

    const GameArrayType& array = oper2;
    if (!CheckSize(state, array, 2))
    {
        return NOTHING;
    }
    if (!CheckType(state, array[0], GameString))
    {
        return NOTHING;
    }
    if (!CheckType(state, array[1], GameScalar))
    {
        return NOTHING;
    }

    GameStringType name = array[0];
    float hit = array[1];
    if (hit < 0.0 || hit > 1.0)
    {
        return NOTHING;
    }

    const int hitPointsCnt = ai->GetType()->GetHitPoints().Size();
    if (hitPointsCnt <= 0)
    {
        return NOTHING;
    }

    for (int i = 0; i < hitPointsCnt; ++i)
    {
        if (stricmp(ai->HitpointName(i), name) == 0)
        {
            ai->ChangeHit(i, hit);
            return NOTHING;
        }
    }

    return NOTHING;
}

GameValue DebugShow(const GameState* state, GameValuePar oper1)
{
    const GameArrayType& array = oper1;
    if (!CheckSize(state, array, 2))
    {
        return NOTHING;
    }
    if (!CheckType(state, array[0], GameString))
    {
        return NOTHING;
    }
    if (!CheckType(state, array[1], GameScalar))
    {
        return NOTHING;
    }

    state->ShowDebug(array[0], toInt(float(array[1])));
    return NOTHING;
}

GameValue ObjHasWeapon(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    EntityAI* veh = dyn_cast<EntityAI>(GetObject(oper1));
    if (!veh)
    {
        return false;
    }

    GameStringType name = oper2;
    for (int i = 0; i < veh->NWeaponSystems(); i++)
    {
        const WeaponType* weapon = veh->GetWeaponSystem(i);
        if (!weapon)
        {
            continue;
        }
        if (stricmp(weapon->GetName(), name) == 0)
        {
            return true;
        }
    }
    return false;
}

GameValue ObjAddWeapon(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    EntityAI* ai = dyn_cast<EntityAI>(GetObject(oper1));
    if (!ai)
    {
        return NOTHING;
    }
    GameStringType weapon = oper2;
    ai->AddWeapon(weapon);
    ai->AutoReloadAll();
    return NOTHING;
}

GameValue ObjRemoveWeapon(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    EntityAI* ai = dyn_cast<EntityAI>(GetObject(oper1));
    if (!ai)
    {
        return NOTHING;
    }
    GameStringType weapon = oper2;
    ai->RemoveWeapon(weapon);
    return NOTHING;
}

GameValue ObjRemoveAllWeapons(const GameState* state, GameValuePar oper1)
{
    EntityAI* ai = dyn_cast<EntityAI>(GetObject(oper1));
    if (!ai)
    {
        return NOTHING;
    }
    ai->MinimalWeapons();
    return NOTHING;
}

GameValue ObjAddMagazine(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    EntityAI* ai = dyn_cast<EntityAI>(GetObject(oper1));
    if (!ai)
    {
        return NOTHING;
    }
    GameStringType magazine = oper2;
    ai->AddMagazine(magazine);
    ai->AutoReloadAll();
    return NOTHING;
}

GameValue ObjRemoveMagazine(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    EntityAI* ai = dyn_cast<EntityAI>(GetObject(oper1));
    if (!ai)
    {
        return NOTHING;
    }
    GameStringType magazine = oper2;
    ai->RemoveMagazine(magazine);
    return NOTHING;
}

GameValue ObjRemoveMagazines(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    EntityAI* ai = dyn_cast<EntityAI>(GetObject(oper1));
    if (!ai)
    {
        return NOTHING;
    }
    GameStringType magazine = oper2;
    ai->RemoveMagazines(magazine);
    return NOTHING;
}

GameValue ObjFire(const GameState* state, GameValuePar oper1, GameValuePar oper2)
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

    GameStringType muzzle = oper2;

    for (int i = 0; i < veh->NMagazineSlots(); i++)
    {
        const MagazineSlot& slot = veh->GetMagazineSlot(i);
        if (stricmp(slot._muzzle->GetName(), muzzle) != 0)
        {
            continue;
        }

        if (!slot._magazine)
        {
            return NOTHING;
        }
        if (slot._magazine->_type->_maxAmmo > 0 && slot._magazine->_ammo == 0)
        {
            return NOTHING;
        }

        veh->ForceFire(i);
        return NOTHING;
    }
    return NOTHING;
}

// return -1 if invalid
GameValue ObjMuzzleReloadTime(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj = GetObject(oper1);
    if (!obj)
    {
        return -1.0f;
    }
    EntityAI* veh = dyn_cast<EntityAI>(obj);
    if (!veh)
    {
        return -1.0f;
    }

    GameStringType muzzle = oper2;

    const MagazineSlot* pSlot = veh->GetmagazineSlotByMuzzle(muzzle);
    if (!pSlot)
    {
        return -1.0f;
    }

    if (!pSlot->_magazine)
    {
        return -1.0f;
    }
    float reloadMag = pSlot->_magazine->_reloadMagazine;
    if (reloadMag < 0.0f)
    {
        reloadMag = 0.0f;
    }
    float reload = pSlot->_magazine->_reload;
    if (reload < 0.0f)
    {
        reload = 0.0f;
    }
    return reloadMag + reload;
}

GameValue ObjFireEx(const GameState* state, GameValuePar oper1, GameValuePar oper2)
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

    const GameArrayType& array = oper2;
    GameStringType muzzle;
    GameStringType mode;
    GameStringType magazine;

    switch (array.Size())
    {
        case 3:
            if (array[2].GetType() != GameString)
            {
                return NOTHING;
            }
            magazine = array[2];
        case 2:
            if (array[0].GetType() != GameString)
            {
                return NOTHING;
            }
            muzzle = array[0];
            if (array[1].GetType() != GameString)
            {
                return NOTHING;
            }
            mode = array[1];
            break;
        default:
            return NOTHING;
    }

    for (int i = 0; i < veh->NMagazineSlots(); i++)
    {
        const MagazineSlot& slot = veh->GetMagazineSlot(i);
        if (stricmp(slot._muzzle->GetName(), muzzle) != 0)
        {
            continue;
        }

        if (magazine.GetLength() > 0)
        {
            Ref<MagazineType> type = MagazineTypes.New(magazine);
            int best = veh->FindBestMagazine(type, 0);
            if (best >= 0)
            {
                veh->AttachMagazine(slot._muzzle, veh->GetMagazine(best));
            }
        }
        if (!slot._magazine)
        {
            return NOTHING;
        }

        // check mode
        const WeaponModeType* modeType = veh->GetWeaponMode(i);
        PoseidonAssert(modeType);
        if (stricmp(modeType->GetName(), mode) != 0)
        {
            continue;
        }

        if (slot._magazine->_type->_maxAmmo > 0 && slot._magazine->_ammo == 0)
        {
            return NOTHING;
        }

        veh->ForceFire(i);
        return NOTHING;
    }
    return NOTHING;
}

GameValue ObjCmpE(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj1 = GetObject(oper1);
    Object* obj2 = GetObject(oper2);
    if (!obj1 || !obj2)
    {
        return false;
    }
    return obj1 == obj2;
}

GameValue ObjCmpNE(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    Object* obj1 = GetObject(oper1);
    Object* obj2 = GetObject(oper2);
    if (!obj1 || !obj2)
    {
        return true;
    }
    return obj1 != obj2;
}

GameValue GrpCmpE(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    AIGroup* obj1 = GetGroup(oper1);
    AIGroup* obj2 = GetGroup(oper2);
    if (!obj1 || !obj2)
    {
        return false;
    }
    return obj1 == obj2;
}

GameValue GrpCmpNE(const GameState* state, GameValuePar oper1, GameValuePar oper2)
{
    AIGroup* obj1 = GetGroup(oper1);
    AIGroup* obj2 = GetGroup(oper2);
    if (!obj1 || !obj2)
    {
        return true;
    }
    return obj1 != obj2;
}
