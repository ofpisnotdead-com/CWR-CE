#include <Poseidon/Core/Application.hpp>
#include <Poseidon/World/Entities/Vehicles/Transport.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/Entities/Weapons/ProxyWeapon.hpp>
#include <Poseidon/World/Entities/Infantry/Person.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Random/randomGen.hpp>
#include <Poseidon/Dev/Diag/DiagModes.hpp>

#include <Poseidon/Game/TitEffects.hpp>
#include <Poseidon/World/Scene/Camera/CamEffects.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/UI/Locale/Sentences.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>

#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/Game/UiActions.hpp>
#include <Poseidon/World/Simulation/FrameInv.hpp>

#include <Poseidon/World/Entities/Infantry/MoveActions.hpp>
#include <Poseidon/World/Entities/Infantry/ManActs.hpp>
#include <Poseidon/Graphics/Rendering/Draw/SpecLods.hpp>

#include <Poseidon/Foundation/Enums/EnumNames.hpp>
#include <limits.h>
#include <stdio.h>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/BankArray.hpp>
#include <Poseidon/Foundation/Containers/BoolArray.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>

#pragma warning(disable : 4355)
#include <Poseidon/Foundation/Common/Filenames.hpp>
#pragma warning(disable : 4355)

using namespace Poseidon;
void UpdateWeaponsInBriefing();
extern SRef<EntityAI> GDummyVehicle;

namespace Poseidon
{
using Foundation::EnumName;

DEFINE_CASTING(Transport)

// config identifiers
#define TitleTxt 0     // down fades text
#define TitleTxtDown 1 // down fades text
#define TitleRsc 2     // resource text
#define TitleObj 3

void CutScene(const char* name)
{
    if (!GWorld->CameraOn())
    {
        return;
    }

    static const char* lastName = nullptr;
    static Link<TitleEffect> lastEffect;
    static Link<IWave> lastSound;

    TitleEffect* effect = GWorld->GetTitleEffect();
    if (effect && effect == lastEffect)
    {
        if (!name)
        {
            effect->Terminate();
            return;
        }
        else if (name == lastName)
        {
            effect->Prolong(1);
            // Note: prolong sound effect
        }
    }
    if (effect || !name)
    {
        return;
    }

    if (GWorld->GetCameraEffect())
    {
        return;
    }

    const ParamEntry& entry = Pars >> "CfgCutScenes" >> name;
    int titName = entry >> "titleType";
    RString titText = entry >> "title";

    TitleEffect* titEff = nullptr;
    switch (titName)
    {
        case TitleTxt:
            titEff = CreateTitleEffect(TitPlain, titText);
            break;
        case TitleTxtDown:
            titEff = CreateTitleEffect(TitPlainDown, titText);
            break;
    }
    if (titEff)
    {
        GWorld->SetTitleEffect(titEff);
        lastEffect = titEff;
        lastName = name;
    }

    SoundPars pars;
    GetValue(pars, entry >> "sound");
    if (pars.name.GetLength() > 0)
    {
        IWave* wave = GSoundScene->OpenAndPlayOnce(pars.name, GWorld->CameraOn()->Position(),
                                                   GWorld->CameraOn()->ObjectSpeed(), pars.vol, pars.freq);
        if (wave)
        {
            GSoundScene->AddSound(wave);
            lastSound = wave;
        }
    }
}

static bool IsSupply(EntityAI* vehicle)
{
    const VehicleType* type = vehicle->GetType();
    if (type->GetMaxFuelCargo() > 0)
    {
        return true;
    }
    if (type->GetMaxRepairCargo())
    {
        return true;
    }
    if (type->GetMaxAmmoCargo())
    {
        return true;
    }
    if (type->GetMaxWeaponsCargo() > 0)
    {
        return true;
    }
    if (type->GetMaxMagazinesCargo() > 0)
    {
        return true;
    }
    if (type->IsAttendant())
    {
        return true;
    }
    for (int i = 0; i < type->_weaponCargo.Size(); i++)
    {
        if (type->_weaponCargo[i].count > 0)
        {
            return true;
        }
    }
    for (int i = 0; i < type->_magazineCargo.Size(); i++)
    {
        if (type->_magazineCargo[i].count > 0)
        {
            return true;
        }
    }
    return type->_forceSupply;
}

ResourceSupply::ResourceSupply(EntityAI* vehicle) : _parent(vehicle)
{
    const VehicleType* type = vehicle->GetType();
    _fuelCargo = type->GetMaxFuelCargo();
    _repairCargo = type->GetMaxRepairCargo();
    _ammoCargo = type->GetMaxAmmoCargo();
    for (int i = 0; i < type->_weaponCargo.Size(); i++)
    {
        const WeaponCargoItem& item = type->_weaponCargo[i];
        AddWeaponCargo(item.weapon, item.count);
    }
    for (int i = 0; i < type->_magazineCargo.Size(); i++)
    {
        const MagazineCargoItem& item = type->_magazineCargo[i];
        AddMagazineCargo(item.magazine, item.count);
    }
    _action = ATNone;
    _actionParam = 0;
    _actionParam2 = 0;
}

ResourceSupply::ResourceSupply()
{
    _fuelCargo = 0;
    _repairCargo = 0;
    _ammoCargo = 0;
    _action = ATNone;
    _actionParam = 0;
    _actionParam2 = 0;
}

} // namespace Poseidon
bool CheckSupply(EntityAI* vehicle, EntityAI* parent, SupportCheckF check, float limit, bool now)
{
    using namespace Poseidon;
    if (parent == vehicle)
    {
        return false;
    }

    if (now)
    {
        // check distance
        Vector3 supplyPos = parent->PositionModelToWorld(parent->GetType()->GetSupplyPoint());
        float supplyRadius = parent->GetType()->GetSupplyRadius();

        float dist2 = vehicle->Position().Distance2(supplyPos);
        if (dist2 > Square(40))
        {
            return false;
        }
        float maxDistance = vehicle->CollisionSize() * 1.2 + 1 + supplyRadius;
        if (dist2 > Square(maxDistance))
        {
            return false;
        }
        //	if (vehicle->Speed().Distance2(parent->Speed()) > Square(0.5)) return false;
    }

    if (check)
    {
        return (vehicle->*check)() > limit;
    }
    else
    {
        return true;
    }
}
namespace Poseidon
{

bool ResourceSupply::Check(EntityAI* vehicle, SupportCheckF check, float limit, bool now) const
{
    if (!vehicle)
    {
        return false;
    }
    if (!vehicle->GetShape())
    {
        return false;
    }

    return CheckSupply(vehicle, _parent, check, limit, now);
}

template <>
const EnumName* Foundation::GetEnumNames(UIActionType dummy)
{
    static const EnumName UIActionNames[] = {EnumName(ATNone, "NONE"),
                                             EnumName(ATGetInCommander, "GETIN COMMANDER"),
                                             EnumName(ATGetInDriver, "GETIN DRIVER"),
                                             EnumName(ATGetInGunner, "GETIN GUNNER"),
                                             EnumName(ATGetInCargo, "GETIN CARGO"),
                                             EnumName(ATHeal, "HEAL"),
                                             EnumName(ATRepair, "REPAIR"),
                                             EnumName(ATRefuel, "REFUEL"),
                                             EnumName(ATRearm, "REARM"),
                                             EnumName(ATGetOut, "GETOUT"),
                                             EnumName(ATLightOn, "LIGHT ON"),
                                             EnumName(ATLightOff, "LIGHT OFF"),
                                             EnumName(ATEngineOn, "ENGINE ON"),
                                             EnumName(ATEngineOff, "ENGINE OFF"),
                                             EnumName(ATSwitchWeapon, "SWITCH WEAPON"),
                                             EnumName(ATUseWeapon, "USE WEAPON"),
                                             EnumName(ATLoadMagazine, "LOADMAGAZINE"),
                                             EnumName(ATTakeWeapon, "TAKE WEAPON"),
                                             EnumName(ATTakeMagazine, "TAKE MAGAZINE"),
                                             EnumName(ATTakeFlag, "TAKE FLAG"),
                                             EnumName(ATReturnFlag, "RETURN FLAG"),
                                             EnumName(ATTurnIn, "TURNIN"),
                                             EnumName(ATTurnOut, "TURNOUT"),
                                             EnumName(ATWeaponInHand, "WEAPONINHAND"),
                                             EnumName(ATWeaponOnBack, "WEAPONONBACK"),
                                             EnumName(ATSitDown, "SITDOWN"),
                                             EnumName(ATLand, "LAND"),
                                             EnumName(ATCancelLand, "CANCEL LAND"),
                                             EnumName(ATEject, "EJECT"),
                                             EnumName(ATMoveToDriver, "MOVETODRIVER"),
                                             EnumName(ATMoveToGunner, "MOVETOGUNNER"),
                                             EnumName(ATMoveToCommander, "MOVETOCOMMANDER"),
                                             EnumName(ATMoveToCargo, "MOVETOCARGO"),
                                             EnumName(ATHideBody, "HIDEBODY"),
                                             EnumName(ATTouchOff, "TOUCHOFF"),
                                             EnumName(ATSetTimer, "SETTIMER"),
                                             EnumName(ATDeactivate, "DEACTIVATE"),
                                             EnumName(ATManualFire, "MANUALFIRE"),
                                             EnumName(ATNVGoggles, "NVGOGGLES"),
                                             EnumName(ATAutoHover, "AUTOHOVER"),
                                             EnumName(ATStrokeFist, "STROKEFIST"),
                                             EnumName(ATStrokeGun, "STROKEGUN"),

                                             EnumName(ATLadderUp, "LADDERUP"),
                                             EnumName(ATLadderDown, "LADDERDOWN"),
                                             EnumName(ATLadderOnDown, "LADDERONDOWN"),
                                             EnumName(ATLadderOnUp, "LADDERONUP"),
                                             EnumName(ATLadderOff, "LADDEROFF"),
                                             EnumName(ATFireInflame, "FIRE INFLAME"),
                                             EnumName(ATFirePutDown, "FIRE PUT DOWN"),

                                             EnumName(ATLandGear, "LAND GEAR"),
                                             EnumName(ATFlapsDown, "FLAPS DOWN"),
                                             EnumName(ATFlapsUp, "FLAPS UP"),

                                             EnumName(ATSalute, "SALUTE"),

                                             EnumName(ATScudLaunch, "SCUD LAUNCH"),
                                             EnumName(ATScudStart, "SCUD START"),

                                             EnumName(ATUser, "USER"),

                                             EnumName(ATDropWeapon, "DROP WEAPON"),
                                             EnumName(ATDropMagazine, "DROP MAGAZINE"),

                                             EnumName(ATUserType, "USER TYPE"),

                                             EnumName(ATHandGunOn, "HANDGUN ON"),
                                             EnumName(ATHandGunOff, "HANDGUN OFF"),

                                             EnumName(ATTakeMine, "TAKE MINE"),
                                             EnumName(ATDeactivateMine, "DEACTIVATE MINE"),

                                             EnumName(ATUseMagazine, "USE MAGAZINE"),

                                             EnumName()};
    return UIActionNames;
}

LSError ResourceSupply::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.Serialize("fuelCargo", _fuelCargo, 1, 0))
    PARAM_CHECK(ar.Serialize("repairCargo", _repairCargo, 1, 0))
    PARAM_CHECK(ar.Serialize("ammoCargo", _ammoCargo, 1, 0))

    if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
    {
        PARAM_CHECK(ar.SerializeRef("Supplying", _supplying, 1))
        PARAM_CHECK(ar.SerializeRef("Alloc", _alloc, 1))
        PARAM_CHECK(ar.SerializeEnum("action", _action, 1, (UIActionType)ATNone))
        PARAM_CHECK(ar.Serialize("actionParam", _actionParam, 1, 0))
        PARAM_CHECK(ar.Serialize("actionParam2", _actionParam2, 1, 0))
        PARAM_CHECK(ar.Serialize("actionParam3", _actionParam3, 1, ""))
    }

    PARAM_CHECK(ar.Serialize("WeaponCargo", _weaponCargo, 1));
    PARAM_CHECK(ar.Serialize("MagazineCargo", _magazineCargo, 1));

    return LSOK;
}

IndicesResourceSupply::IndicesResourceSupply()
{
    fuelCargo = -1;
    repairCargo = -1;
    ammoCargo = -1;
    supplying = -1;
    alloc = -1;
    action = -1;
    actionParam = -1;
    actionParam2 = -1;
    actionParam3 = -1;
}

void IndicesResourceSupply::Scan(NetworkMessageFormatBase* format)
{
    SCAN(fuelCargo)
    SCAN(repairCargo)
    SCAN(ammoCargo)
    SCAN(supplying)
    SCAN(alloc)
    SCAN(action)
    SCAN(actionParam)
    SCAN(actionParam2)
    SCAN(actionParam3)
}

void ResourceSupply::CreateFormat(NetworkMessageFormat& format)
{
    format.Add("fuelCargo", NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Transported fuel"), ET_ABS_DIF,
               0.01 * ERR_COEF_VALUE_MINOR);
    format.Add("repairCargo", NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Transported repair material"), ET_ABS_DIF,
               0.01 * ERR_COEF_VALUE_MINOR);
    format.Add("ammoCargo", NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Transported ammunition (for vehicles)"),
               ET_ABS_DIF, 0.01 * ERR_COEF_VALUE_MINOR);
    format.Add("supplying", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Currently supplying unit"), ET_NOT_EQUAL,
               ERR_COEF_MODE);
    format.Add("alloc", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Unit allocated for supplying"), ET_NOT_EQUAL,
               ERR_COEF_MODE);
    format.Add("action", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, ATNone), DOC_MSG("Currently processing action"),
               ET_NOT_EQUAL, ERR_COEF_MODE);
    format.Add("actionParam", NDTInteger, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Action parameter"), ET_NOT_EQUAL,
               ERR_COEF_MODE);
    format.Add("actionParam2", NDTInteger, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Action parameter"), ET_NOT_EQUAL,
               ERR_COEF_MODE);
    format.Add("actionParam3", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Action parameter"), ET_NOT_EQUAL,
               ERR_COEF_MODE);
}

TMError ResourceSupply::TransferMsg(NetworkMessageContext& ctx, const IndicesResourceSupply* indices)
{
    ITRANSF(fuelCargo)
    ITRANSF(repairCargo)
    ITRANSF(ammoCargo)
    ITRANSF_REF(supplying)
    ITRANSF_REF(alloc)
    ITRANSF_ENUM(action)
    ITRANSF(actionParam)
    ITRANSF(actionParam2)
    ITRANSF(actionParam3)
    return TMOK;
}

float ResourceSupply::CalculateError(NetworkMessageContext& ctx, const IndicesResourceSupply* indices)
{
    float error = 0;
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
            ICALCERR_NEQREF(EntityAI, alloc, ERR_COEF_MODE)
            ICALCERR_NEQREF(EntityAI, supplying, ERR_COEF_MODE)
            ICALCERR_NEQ(int, action, ERR_COEF_MODE)
            ICALCERR_NEQ(int, actionParam, ERR_COEF_MODE)
            ICALCERR_NEQ(int, actionParam2, ERR_COEF_MODE)
            ICALCERR_NEQSTR(actionParam3, ERR_COEF_MODE)
            ICALCERR_ABSDIF(float, fuelCargo, 0.01 * ERR_COEF_VALUE_MINOR)
            ICALCERR_ABSDIF(float, repairCargo, 0.01 * ERR_COEF_VALUE_MINOR)
            ICALCERR_ABSDIF(float, ammoCargo, 0.01 * ERR_COEF_VALUE_MINOR)
            break;
        default:
            break;
    }
    return error;
}

bool ResourceSupply::Supply(EntityAI* vehicle, UIActionType action, int param, int param2, RString param3)
{
    if (vehicle)
    {
        if (_alloc && _alloc != vehicle)
        {
            return false;
        }
    }
    _supplying = vehicle;
    _action = action;
    _actionParam = param;
    _actionParam2 = param2;
    _actionParam3 = param3;
    return true;
}

struct MagazineInWeapon
{
    Ref<MuzzleType> muzzle;
    Ref<Magazine> magazine;
};

void ResourceSupply::Simulate(float deltaT, SimulationImportance prec)
{
    EntityAI* veh = _parent;
    VehicleSupply* parent = dyn_cast<VehicleSupply>(veh);
    DoAssert(parent);

    if (_alloc)
    {
        // we are allocated to some unit
        float limit2 = _alloc->QIsManual() ? Square(30) : Square(100);
        if (_alloc->Position().Distance2(veh->Position()) > limit2)
        {
            // allocated vehicle is very far - unallocate it
            _alloc = nullptr;
            _supplying = nullptr;
        }
    }
    if (_supplying)
    {
        if (_supplying->Speed().Distance2(veh->Speed()) > Square(2))
        {
            // unit in cargo can still exchange items even when the vehicle moves
            AIUnit* unit = _supplying->CommanderUnit();
            if (unit && unit->GetPerson() == _supplying && unit->GetVehicleIn() == _parent)
            {
                // unit in cargo space of vehicle can take / drop items even when vehicle moves
            }
            else
            {
                _supplying = nullptr;
            }
        }
    }
    if (_supplying)
    {
        const float thold = 0;
        const float fuelThold = 0.01;
        EntityAI* target = _supplying;
        switch (_action)
        {
            case ATHeal:
                if (veh->GetType()->IsAttendant() && target->CommanderUnit() && target->NeedsAmbulance() > 0)
                {
                    EntityAI* person = target->CommanderUnit()->GetPerson();
                    person->Repair(1);
                    _supplying = nullptr; // done
                    _alloc = nullptr;
                    return;
                }
                break;
            case ATRepair:
                if (GetRepairCargo() > 0 && target->NeedsRepair() > 0)
                {
                    float dammage = target->GetTotalDammage();
                    float total = target->GetType()->GetCost();
                    float howMuch = 0.3 * total * dammage;
                    if (howMuch <= total * thold)
                    {
                        _supplying = nullptr; // done
                    }
                    saturateMin(howMuch, _repairCargo);
                    const float maxFlow = total * 0.1;
                    saturateMin(howMuch, maxFlow * deltaT);
                    target->Repair(howMuch / total);
                    _repairCargo -= howMuch;
                    if (target == GWorld->CameraOn())
                    {
                        CutScene("Repair");
                    }
                    return;
                }
                break;
            case ATRefuel:
                if (GetFuelCargo() > 0 && target->NeedsRefuel() > 0)
                {
                    float total = target->GetType()->GetFuelCapacity();
                    float howMuch = total - target->GetFuel();
                    if (howMuch <= total * fuelThold)
                    {
                        _supplying = nullptr; // done
                    }
                    saturateMin(howMuch, _fuelCargo);
                    const float maxFlow = total * 0.1;
                    saturateMin(howMuch, maxFlow * deltaT);
                    target->Refuel(howMuch);
                    _fuelCargo -= howMuch;
                    if (target == GWorld->CameraOn())
                    {
                        CutScene("Refuel");
                    }
                    return;
                }
                break;
            case ATRearm:
            {
                Person* man = dyn_cast<Person>(target);
                if (man)
                {
                    AIUnit* unit = man->Brain();
                    if (!unit)
                    {
                        return;
                    }
                    const MuzzleType* muzzle1 = nullptr;
                    const MuzzleType* muzzle2 = nullptr;
                    int slots1 = 0, slots2 = 0, slots3 = 0;
                    unit->CheckAmmo(muzzle1, muzzle2, slots1, slots2, slots3);
                    for (int i = 0; i < GetMagazineCargoSize();)
                    {
                        Ref<const Magazine> magazine = GetMagazineCargo(i);
                        if (!magazine || magazine->_ammo == 0)
                        {
                            i++;
                            continue;
                        }
                        // check if magazine can be used
                        const MagazineType* type = magazine->_type;
                        int slots = GetItemSlotsCount(type->_magazineType);

                        bool add = false;
                        if (muzzle1 && muzzle1->CanUse(type))
                        {
                            if (slots <= slots1)
                            {
                                slots1 -= slots;
                                add = true;
                            }
                        }
                        else if (muzzle2 && muzzle2->CanUse(type))
                        {
                            if (slots <= slots2)
                            {
                                slots2 -= slots;
                                add = true;
                            }
                        }
                        else if (man->IsMagazineUsable(type))
                        {
                            if (slots <= slots3)
                            {
                                slots3 -= slots;
                                add = true;
                            }
                        }
                        if (add)
                        {
                            // add magazine
                            AUTO_STATIC_ARRAY(Ref<const Magazine>, conflict, 16);
                            if (man->CheckMagazine(magazine, conflict))
                            {
                                for (int j = 0; j < conflict.Size(); j++)
                                {
                                    const Magazine* m = conflict[j];
                                    man->RemoveMagazine(m);
                                    AddMagazineCargo(const_cast<Magazine*>(m));
                                    GetNetworkManager().AddMagazineCargo(parent, m);
                                }
                                RemoveMagazineCargo(const_cast<Magazine*>(magazine.GetRef()));
                                GetNetworkManager().RemoveMagazineCargo(parent, magazine->_creator, magazine->_id);
                                DoVerify(man->AddMagazine(const_cast<Magazine*>(magazine.GetRef())) >= 0);
                            }
                            else
                            {
                                i++;
                            }
                        }
                        else
                        {
                            i++;
                        }
                    }
                    _supplying = nullptr; // done
                    if (target == GWorld->CameraOn())
                    {
                        CutScene("TakeMagazine");
                    }
                    return;
                }
                else
                {
                    if (GetAmmoCargo() > 0 && target->NeedsRearm() > 0)
                    {
                        float total = target->GetMaxAmmoCost();
                        float howMuch = total - target->GetAmmoCost();
                        if (howMuch <= total * thold)
                        {
                            _supplying = nullptr; // done
                        }
                        saturateMin(howMuch, _ammoCargo);
                        const float maxFlow1 = total * 0.05;
                        const float maxFlow2 = 3000;
                        saturateMin(howMuch, maxFlow1 * deltaT);
                        saturateMin(howMuch, maxFlow2 * deltaT);
                        target->Rearm(howMuch);
                        _ammoCargo -= howMuch;
                        if (target == GWorld->CameraOn())
                        {
                            CutScene("Rearm");
                        }
                    }
                    return;
                }
            }
            break;
            case ATTakeWeapon:
            {
                // find weapon
                Ref<WeaponType> weapon = WeaponTypes.New(_actionParam3);
                if (!weapon)
                {
                    break;
                }
                AUTO_STATIC_ARRAY(Ref<const WeaponType>, conflict, 16);
                if (FindWeapon(weapon) && target->CheckWeapon(weapon, conflict))
                {
                    AUTO_STATIC_ARRAY(MagazineInWeapon, miwConflict, 16);
                    for (int i = 0; i < conflict.Size(); i++)
                    {
                        const WeaponType* weapon = conflict[i];
                        for (int j = 0; j < weapon->_muzzles.Size(); j++)
                        {
                            MuzzleType* muzzle = weapon->_muzzles[j];
                            for (int k = 0; k < target->NMagazineSlots(); k++)
                            {
                                const MagazineSlot& slot = target->GetMagazineSlot(k);
                                if (slot._muzzle == muzzle)
                                {
                                    if (slot._magazine && slot._magazine->_ammo > 0)
                                    {
                                        int index = miwConflict.Add();
                                        miwConflict[index].muzzle = slot._muzzle;
                                        miwConflict[index].magazine = slot._magazine;
                                    }
                                    break;
                                }
                            }
                        }
                    }
                    for (int i = 0; i < conflict.Size(); i++)
                    {
                        const WeaponType* wt = conflict[i];
                        target->RemoveWeapon(wt, false);
                        AddWeaponCargo(const_cast<WeaponType*>(wt), 1);
                        GetNetworkManager().AddWeaponCargo(parent, wt->GetName());
                    }
                    RemoveWeaponCargo(weapon);
                    GetNetworkManager().RemoveWeaponCargo(parent, weapon->GetName());
                    DoVerify(target->AddWeapon(weapon, false, false, false) >= 0);
                    if (GWorld->FocusOn() && GWorld->FocusOn()->GetVehicle() == target)
                    {
                        GWorld->UI()->ResetVehicle(target);
                    }
                    for (int i = 0; i < miwConflict.Size(); i++)
                    {
                        Ref<Magazine> magazine = miwConflict[i].magazine;
                        if (!magazine)
                        {
                            continue;
                        }
                        target->RemoveMagazine(magazine);
                        AddMagazineCargo(magazine);
                        GetNetworkManager().AddMagazineCargo(parent, magazine);
                    }
                    for (int i = 0; i < target->NMagazines();)
                    {
                        Ref<Magazine> magazine = target->GetMagazine(i);
                        if (!magazine || target->IsMagazineUsable(magazine->_type))
                        {
                            i++;
                            continue;
                        }
                        target->RemoveMagazine(magazine);
                        if (magazine->_ammo > 0)
                        {
                            AddMagazineCargo(magazine);
                            GetNetworkManager().AddMagazineCargo(parent, magazine);
                        }
                    }
                    for (int i = 0; i < weapon->_muzzles.Size(); i++)
                    {
                        MuzzleType* muzzle = weapon->_muzzles[i];
                        for (int j = 0; j < muzzle->_magazines.Size(); j++)
                        {
                            MagazineType* type = muzzle->_magazines[j];
                            for (int k = 0; k < _magazineCargo.Size();)
                            {
                                Ref<Magazine> magazine = _magazineCargo[k];
                                if (!magazine || magazine->_type != type)
                                {
                                    k++;
                                    continue;
                                }
                                // add magazine
                                AUTO_STATIC_ARRAY(Ref<const Magazine>, conflict, 16);
                                if (!target->CheckMagazine(magazine, conflict) || conflict.Size() > 0)
                                {
                                    goto EnoughMagazines;
                                }
                                RemoveMagazineCargo(magazine);
                                GetNetworkManager().RemoveMagazineCargo(parent, magazine->_creator, magazine->_id);
                                DoVerify(target->AddMagazine(magazine) >= 0);
                            }
                        }
                    }
                EnoughMagazines:
                    for (int i = 0; i < weapon->_muzzles.Size(); i++)
                    {
                        MuzzleType* muzzle = weapon->_muzzles[i];
                        int best = target->FindMagazineByType(muzzle);
                        if (best >= 0)
                        {
                            target->AttachMagazine(muzzle, target->GetMagazine(best));
                        }
                    }
                }
                _supplying = nullptr; // done

                target->OnWeaponChanged();
                if (target == GWorld->CameraOn())
                {
                    CutScene("TakeWeapon");
                }
                else
                {
                    AIUnit* unit = target->CommanderUnit();
                    if (unit && unit->GetVehicleIn() == GWorld->CameraOn())
                    {
                        CutScene("TakeWeapon");
                    }
                }

                UpdateWeaponsInBriefing();
                return;
            }
            break;
            case ATTakeMagazine:
            {
                Ref<const Magazine> magazine = FindMagazine(_actionParam3);
                if (!magazine)
                {
                    break;
                }
                AUTO_STATIC_ARRAY(Ref<const Magazine>, conflict, 16);
                if (target->CheckMagazine(magazine, conflict))
                {
                    for (int i = 0; i < conflict.Size(); i++)
                    {
                        const Magazine* m = conflict[i];
                        target->RemoveMagazine(m);
                        if (m->_ammo > 0)
                        {
                            AddMagazineCargo(const_cast<Magazine*>(m));
                            GetNetworkManager().AddMagazineCargo(parent, m);
                        }
                    }
                    RemoveMagazineCargo(const_cast<Magazine*>(magazine.GetRef()));
                    GetNetworkManager().RemoveMagazineCargo(parent, magazine->_creator, magazine->_id);
                    Verify(target->AddMagazine(const_cast<Magazine*>(magazine.GetRef()), false, true) >= 0);
                }
                _supplying = nullptr; // done
                if (target == GWorld->CameraOn())
                {
                    CutScene("TakeMagazine");
                }
                else
                {
                    AIUnit* unit = target->CommanderUnit();
                    if (unit && unit->GetVehicleIn() == GWorld->CameraOn())
                    {
                        CutScene("TakeMagazine");
                    }
                }

                UpdateWeaponsInBriefing();

                return;
            }
            break;
            case ATDropWeapon:
            {
                // find weapon
                Ref<WeaponType> weapon = WeaponTypes.New(_actionParam3);
                if (!weapon || !weapon->_canDrop)
                {
                    break;
                }

                if (target->FindWeapon(weapon))
                {
                    target->RemoveWeapon(weapon);
                    if (GWorld->FocusOn() && GWorld->FocusOn()->GetVehicle() == target)
                    {
                        GWorld->UI()->ResetVehicle(target);
                    }

                    AddWeaponCargo(weapon, 1);
                    GetNetworkManager().AddWeaponCargo(parent, weapon->GetName());

                    for (int i = 0; i < target->NMagazines();)
                    {
                        Ref<Magazine> magazine = target->GetMagazine(i);
                        if (!magazine || target->IsMagazineUsable(magazine->_type))
                        {
                            i++;
                            continue;
                        }
                        target->RemoveMagazine(magazine);
                        if (magazine->_ammo > 0)
                        {
                            AddMagazineCargo(magazine);
                            GetNetworkManager().AddMagazineCargo(parent, magazine);
                        }
                    }
                }
                _supplying = nullptr; // done
                if (target == GWorld->CameraOn())
                {
                    CutScene("DropWeapon");
                }
                else
                {
                    AIUnit* unit = target->CommanderUnit();
                    if (unit && unit->GetVehicleIn() == GWorld->CameraOn())
                    {
                        CutScene("DropWeapon");
                    }
                }
                return;
            }
            break;
            case ATDropMagazine:
            {
                Ref<MagazineType> type = MagazineTypes.New(_actionParam3);
                if (!type)
                {
                    break;
                }

                Ref<const Magazine> magazine;
                int minCount = INT_MAX;
                for (int i = 0; i < target->NMagazines(); i++)
                {
                    const Magazine* m = target->GetMagazine(i);
                    if (!m)
                    {
                        continue;
                    }
                    if (m->_type != type)
                    {
                        continue;
                    }
                    if (target->IsMagazineUsed(m))
                    {
                        continue;
                    }
                    if (m->_ammo < minCount)
                    {
                        magazine = m;
                        minCount = m->_ammo;
                    }
                }
                if (!magazine)
                {
                    for (int i = 0; i < target->NMagazines(); i++)
                    {
                        const Magazine* m = target->GetMagazine(i);
                        if (!m)
                        {
                            continue;
                        }
                        if (m->_type != type)
                        {
                            continue;
                        }
                        if (m->_ammo < minCount)
                        {
                            magazine = m;
                            minCount = m->_ammo;
                        }
                    }
                }

                if (magazine)
                {
                    target->RemoveMagazine(magazine);
                    if (minCount > 0)
                    {
                        AddMagazineCargo(const_cast<Magazine*>(magazine.GetRef()));
                        GetNetworkManager().AddMagazineCargo(parent, magazine);
                    }
                }

                _supplying = nullptr; // done
                if (target == GWorld->CameraOn())
                {
                    CutScene("DropMagazine");
                }
                else
                {
                    AIUnit* unit = target->CommanderUnit();
                    if (unit && unit->GetVehicleIn() == GWorld->CameraOn())
                    {
                        CutScene("DropMagazine");
                    }
                }
                return;
            }
            break;
            default:
                Fail("Unsupported action");
                break;
        }
        _supplying = nullptr;
    }
    CutScene(nullptr);
}

void ResourceSupply::GetActions(UIActions& actions, AIUnit* unit, bool now)
{
    if (!unit)
    {
        return;
    }

    EntityAI* veh = unit->GetVehicle();
    if (now && !veh->IsLocal())
    {
        return;
    }

    if (_alloc && _alloc != veh)
    {
        return; // wait
    }

    bool inCargo = unit->GetVehicleIn() == _parent && unit->IsInCargo();
    bool check = Check(veh, nullptr, 0, now); // check distance

    if (check)
    {
        if (GetRepairCargo() > 0)
        {
            if (Check(veh, &EntityAI::NeedsRepair, 0, now))
            {
                actions.Add(ATRepair, _parent, 0.5, 0, true);
            }
        }
        if (GetFuelCargo() > 0)
        {
            if (Check(veh, &EntityAI::NeedsRefuel, 0, now))
            {
                actions.Add(ATRefuel, _parent, 0.5, 0, true);
            }
        }
        if (GetAmmoCargo() > 0)
        {
            if (Check(veh, &EntityAI::NeedsRearm, 0, now))
            {
                actions.Add(ATRearm, _parent, 0.5, 0, true);
            }
        }
        if (_parent->GetType()->IsAttendant())
        {
            if (Check(veh, &EntityAI::NeedsAmbulance, 0, now))
            {
                actions.Add(ATHeal, _parent, 0.5, 0, true);
            }
        }
    }

    if (!check && !inCargo)
    {
        return;
    }

    bool soldier = unit->IsFreeSoldier();
    if (inCargo || (soldier && !unit->GetPerson()->IsActionInProgress(MFReload)))
    {
        veh = unit->GetPerson();

        AUTO_STATIC_ARRAY(bool, checked, 32);
        // weapons
        int n = _weaponCargo.Size();
        checked.Resize(n);
        for (int i = 0; i < n; i++)
        {
            checked[i] = false;
        }
        for (int i = 0; i < n; i++)
        {
            if (checked[i])
            {
                continue;
            }
            checked[i] = true;
            const WeaponType* weapon = _weaponCargo[i];
            PoseidonAssert(weapon->_scope == 2);
            AUTO_STATIC_ARRAY(Ref<const WeaponType>, conflict, 16);
            if (!veh->CheckWeapon(weapon, conflict))
            {
                continue;
            }
            actions.Add(ATTakeWeapon, _parent, 0.52, 0, true, false, 0, weapon->GetName());
            for (int j = i + 1; j < n; j++)
            {
                if (_weaponCargo[j] == weapon)
                {
                    checked[j] = true;
                }
            }
        }
        // magazines
        n = _magazineCargo.Size();
        checked.Resize(n);
        for (int i = 0; i < n; i++)
        {
            checked[i] = false;
        }
        for (int i = 0; i < n; i++)
        {
            if (checked[i])
            {
                continue;
            }
            checked[i] = true;
            const Magazine* magazine = _magazineCargo[i];
            PoseidonAssert(magazine->_type->_scope == 2);
            if (magazine->_ammo == 0)
            {
                continue;
            }
            if (!veh->IsMagazineUsable(magazine->_type))
            {
                continue;
            }
            for (int j = i + 1; j < n; j++)
            {
                const Magazine* magazineJ = _magazineCargo[j];
                if (magazineJ->_type == magazine->_type)
                {
                    checked[j] = true;
                    if (magazineJ->_ammo > magazine->_ammo)
                    {
                        magazine = magazineJ;
                    }
                }
            }
            AUTO_STATIC_ARRAY(Ref<const Magazine>, conflict, 16);
            if (!veh->CheckMagazine(magazine, conflict))
            {
                continue;
            }
            actions.Add(ATTakeMagazine, _parent, 0.53, 0, true, false, 0, magazine->_type->GetName());
        }
        {
            int index = veh->SelectedWeapon();
            if (index >= 0 && !_parent->GetType()->_showWeaponCargo)
            {
                const MagazineSlot& slot = veh->GetMagazineSlot(index);
                if (GetFreeWeaponCargo() > 0 && slot._weapon && slot._weapon->_canDrop)
                {
                    actions.Add(ATDropWeapon, _parent, -0.01, 0, true, false, 0, slot._weapon->GetName());
                }
                if (GetFreeMagazineCargo() > 0 && slot._magazine)
                {
                    const MagazineType* type = slot._magazine->_type;
                    for (int i = 0; i < veh->NMagazines(); i++)
                    {
                        const Magazine* magazine = veh->GetMagazine(i);
                        if (!magazine)
                        {
                            continue;
                        }
                        if (magazine->_ammo == 0)
                        {
                            continue;
                        }
                        if (magazine->_type != type)
                        {
                            continue;
                        }
                        actions.Add(ATDropMagazine, _parent, -0.02, 0, true, false, 0, type->GetName());
                        break;
                    }
                }
            }
        }
    }
}

bool ResourceSupply::FindWeapon(const WeaponType* weapon) const
{
    for (int i = 0; i < _weaponCargo.Size(); i++)
    {
        if (_weaponCargo[i] == weapon)
        {
            return true;
        }
    }
    return false;
}

bool ResourceSupply::FindMagazine(const Magazine* magazine) const
{
    for (int i = 0; i < _magazineCargo.Size(); i++)
    {
        if (_magazineCargo[i] == magazine)
        {
            return true;
        }
    }
    return false;
}

const Magazine* ResourceSupply::FindMagazine(RString name) const
{
    Ref<MagazineType> type = MagazineTypes.New(name);
    const Magazine* magazine = nullptr;
    int ammo = 0;
    for (int i = 0; i < _magazineCargo.Size(); i++)
    {
        Magazine* m = _magazineCargo[i];
        if (!m)
        {
            continue;
        }
        if (m->_type != type)
        {
            continue;
        }
        if (m->_ammo > ammo)
        {
            magazine = m;
            ammo = m->_ammo;
        }
    }
    return magazine;
}

const Magazine* ResourceSupply::FindMagazine(int creator, int id) const
{
    for (int i = 0; i < _magazineCargo.Size(); i++)
    {
        const Magazine* magazine = _magazineCargo[i];
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

static bool WeaponCanBeDropped(Vector3Par p)
{
    int lx = (int)(p.X() * InvLandGrid), lz = (int)(p.Z() * InvLandGrid);
    for (int lxx = lx - 1; lxx <= lx + 1; lxx++)
    {
        for (int lzz = lz - 1; lzz <= lz + 1; lzz++)
        {
            if (!InRange(lxx, lzz))
            {
                continue;
            }
            const ObjectList& list = GLandscape->GetObjects(lzz, lxx);
            for (int o = 0; o < list.Size(); o++)
            {
                const Object* oo = list[o];
                if (oo->GetType() != Primary && oo->GetType() != TypeVehicle)
                {
                    continue;
                }
                if (!oo->GetShape())
                {
                    continue;
                }
                float colSize = oo->CollisionSize();
                saturateMax(colSize, 1);
                float distXZ2 = p.DistanceXZ2(oo->Position());
                if (distXZ2 >= Square(colSize))
                {
                    continue;
                }
                return false;
            }
        }
    }
    return true;
}

const int WeaponCountInOperGrid = 3;

static bool OperGridWeaponCanBeDropped(Vector3Par pos, void* context)
{
    // one square fits several weapons (3x3 m)
    const int weaponMax = (WeaponCountInOperGrid - 1) / 2;
    const float weaponDropGrid = OperItemGrid * (1.0f / WeaponCountInOperGrid);
    for (int x = -weaponMax; x <= weaponMax; x++)
    {
        for (int z = -weaponMax; z <= weaponMax; z++)
        {
            Vector3 p = pos + Vector3(x * weaponDropGrid, 0, z * weaponDropGrid);
            if (WeaponCanBeDropped(p))
            {
                return true;
            }
        }
    }
    return false;
}

static bool FindDropPos(Matrix4& transform)
{
    Vector3 pos = transform.Position(), normal = VUp;

    if (!GDummyVehicle || !AIUnit::FindFreePosition(pos, normal, true, GDummyVehicle, OperGridWeaponCanBeDropped))
    {
        return false;
    }

    if (WeaponCanBeDropped(pos))
    {
        transform.SetPosition(pos);
        Matrix3 dir(MRotationY, H_PI * 2 * GRandGen.RandomValue());
        transform.SetUpAndDirection(normal, dir.Direction());
        return true;
    }
    const int weaponMax = (WeaponCountInOperGrid - 1) / 2;
    const float weaponDropGrid = OperItemGrid * (1.0f / WeaponCountInOperGrid);
    for (int x = -weaponMax; x <= weaponMax; x++)
    {
        for (int z = -weaponMax; z <= weaponMax; z++)
        {
            Vector3 p = pos + Vector3(x * weaponDropGrid, 0, z * weaponDropGrid);
            if (WeaponCanBeDropped(p))
            {
                transform.SetPosition(p);
                Matrix3 dir(MRotationY, H_PI * 2 * GRandGen.RandomValue());
                transform.SetUpAndDirection(normal, dir.Direction());
                return true;
            }
        }
    }
    return false;
}

static bool DropWeapon(WeaponType* weapon, const Matrix4& trans)
{
    Ref<EntityAI> veh = NewVehicle("WeaponHolder");
    int slots = weapon->_weaponType;
    if ((slots & MaskSlotSecondary) != 0 && (slots & MaskSlotPrimary) == 0)
    {
        veh = NewVehicle("SecondaryWeaponHolder");
    }
    else
    {
        veh = NewVehicle("WeaponHolder");
    }
    Ref<VehicleSupply> container = dyn_cast<VehicleSupply, EntityAI>(veh);
    if (!container)
    {
        return false;
    }

    Matrix4 transform = trans;
    if (!FindDropPos(transform))
    {
        return false;
    }
    container->PlaceOnSurface(transform);
    container->SetTransform(transform);
    container->Init(transform);

    int free = container->GetFreeWeaponCargo();
    if (free > 0)
    {
        GWorld->AddBuilding(container);

        if (GWorld->GetMode() == GModeNetware)
        {
            GetNetworkManager().CreateVehicle(container, VLTBuilding, "", -1);
        }

        container->AddWeaponCargo(weapon, 1);
        if (GWorld->GetMode() == GModeNetware)
        {
            GetNetworkManager().AddWeaponCargo(container, weapon->GetName());
        }

        return true;
    }
    else
    {
        LOG_ERROR(Physics, "WeaponHolder has no space for weapon");
        return false;
    }
}

static bool DropMagazine(Magazine* magazine, Matrix4& transform)
{
    Ref<EntityAI> veh = NewVehicle("WeaponHolder");
    Ref<VehicleSupply> container = dyn_cast<VehicleSupply, EntityAI>(veh);
    if (!container)
    {
        return false;
    }

    if (!FindDropPos(transform))
    {
        return false;
    }
    container->PlaceOnSurface(transform);
    container->SetTransform(transform);
    container->Init(transform);

    int free = container->GetFreeMagazineCargo();
    if (free > 0)
    {
        GWorld->AddBuilding(container);
        if (GWorld->GetMode() == GModeNetware)
        {
            GetNetworkManager().CreateVehicle(container, VLTBuilding, "", -1);
        }

        container->AddMagazineCargo(magazine);
        if (GWorld->GetMode() == GModeNetware)
        {
            GetNetworkManager().AddMagazineCargo(container, magazine);
        }

        return true;
    }
    else
    {
        LOG_ERROR(Physics, "WeaponHolder has no space for magazine");
        return false;
    }
}

int ResourceSupply::AddWeaponCargo(WeaponType* weapon, int count, bool deleteWhenFull)
{
    int free = GetFreeWeaponCargo();
    if (count > free && !deleteWhenFull)
    {
        Vector3 pos = _parent->PositionModelToWorld(_parent->GetType()->GetSupplyPoint());
        Matrix3 dir = _parent->Orientation();
        Matrix4 transform;
        transform.SetPosition(pos);
        transform.SetOrientation(dir);
        while (count > free && count > 0)
        {
            DropWeapon(weapon, transform);
            count--;
        }
    }

    int index = -1;
    for (int j = 0; j < count; j++)
    {
        if (_parent->GetType()->_showWeaponCargo)
        {
            weapon->ShapeAddRef();
        }
        index = _weaponCargo.Add(weapon);
    }
    return index;
}

bool ResourceSupply::RemoveWeaponCargo(WeaponType* weapon)
{
    for (int i = 0; i < _weaponCargo.Size(); i++)
    {
        if (_weaponCargo[i] == weapon)
        {
            if (_parent->GetType()->_showWeaponCargo)
            {
                weapon->ShapeRelease();
            }
            _weaponCargo.Delete(i);
            if (_parent->GetType()->_forceSupply && _weaponCargo.Size() == 0 && _magazineCargo.Size() == 0)
            {
                _parent->SetDelete();
            }
            return true;
        }
    }
    return false;
}

void ResourceSupply::ClearWeaponCargo()
{
    if (_parent->GetType()->_showWeaponCargo)
    {
        for (int i = 0; i < _weaponCargo.Size(); i++)
        {
            if (_weaponCargo[i])
            {
                _weaponCargo[i]->ShapeRelease();
            }
        }
    }
    _weaponCargo.Clear();
    if (_parent->GetType()->_forceSupply && _magazineCargo.Size() == 0)
    {
        _parent->SetDelete();
    }
}

int ResourceSupply::AddMagazineCargo(Magazine* magazine, bool deleteWhenFull)
{
    int free = GetFreeMagazineCargo();
    if (free < 1 && !deleteWhenFull)
    {
        Vector3 pos = _parent->PositionModelToWorld(_parent->GetType()->GetSupplyPoint());
        Matrix3 dir = _parent->Orientation();
        Matrix4 transform;
        transform.SetPosition(pos);
        transform.SetOrientation(dir);
        DropMagazine(magazine, transform);
        return -1;
    }

    if (_parent->GetType()->_showWeaponCargo)
    {
        magazine->_type->MagazineShapeAddRef();
    }
    return _magazineCargo.Add(magazine);
}

int ResourceSupply::AddMagazineCargo(MagazineType* type, int count, bool deleteWhenFull)
{
    int free = GetFreeMagazineCargo();
    if (count > free && !deleteWhenFull)
    {
        Vector3 pos = _parent->PositionModelToWorld(_parent->GetType()->GetSupplyPoint());
        Matrix3 dir = _parent->Orientation();
        Matrix4 transform;
        transform.SetPosition(pos);
        transform.SetOrientation(dir);
        while (count > free && count > 0)
        {
            Ref<Magazine> magazine = new Magazine(type);
            magazine->_ammo = type->_maxAmmo;
            magazine->_reload = 0;
            magazine->_reloadMagazine = 0;
            DropMagazine(magazine, transform);
            count--;
        }
    }

    int index = -1;
    for (int j = 0; j < count; j++)
    {
        Ref<Magazine> magazine = new Magazine(type);
        magazine->_ammo = type->_maxAmmo;
        magazine->_reload = 0;
        magazine->_reloadMagazine = 0;
        if (_parent->GetType()->_showWeaponCargo)
        {
            magazine->_type->MagazineShapeAddRef();
        }
        index = _magazineCargo.Add(magazine);
    }
    return index;
}

bool ResourceSupply::RemoveMagazineCargo(Magazine* magazine)
{
    for (int i = 0; i < _magazineCargo.Size(); i++)
    {
        if (_magazineCargo[i] == magazine)
        {
            if (_parent->GetType()->_showWeaponCargo)
            {
                magazine->_type->MagazineShapeRelease();
            }
            _magazineCargo.Delete(i);
            if (_parent->GetType()->_forceSupply && _weaponCargo.Size() == 0 && _magazineCargo.Size() == 0)
            {
                _parent->SetDelete();
            }
            return true;
        }
    }
    return false;
}

void ResourceSupply::ClearMagazineCargo()
{
    if (_parent->GetType()->_showWeaponCargo)
    {
        for (int i = 0; i < _magazineCargo.Size(); i++)
        {
            if (_magazineCargo[i])
            {
                _magazineCargo[i]->_type->MagazineShapeRelease();
            }
        }
    }

    _magazineCargo.Clear();
    if (_parent->GetType()->_forceSupply && _weaponCargo.Size() == 0)
    {
        _parent->SetDelete();
    }
}

DEFINE_CASTING(VehicleSupply)

VehicleSupply::VehicleSupply(EntityAIType* name, bool fullCreate) : base(name, fullCreate)
{
    if (IsSupply(this))
    {
        _supply = new ResourceSupply(this);
    }
}

void VehicleSupply::SupplyStarted(AIUnit* unit)
{
    LOG_DEBUG(Physics, "{} SupplyStarted for {}", (const char*)GetDebugName(), (const char*)unit->GetDebugName());

    PoseidonAssert(_supplyUnits.Find(unit) < 0);
    _supplyUnits.AddUnique(unit);
}

void VehicleSupply::SupplyFinished(AIUnit* unit)
{
    // done/canceled
    LOG_DEBUG(Physics, "{} SupplyFinished for {}", (const char*)GetDebugName(), (const char*)unit->GetDebugName());
    if (GetAllocSupply() == unit->GetVehicle())
    {
        LOG_DEBUG(Physics, "  patch unalloc");
        SetAllocSupply(nullptr);
    }

    for (int i = 0; i < _supplyUnits.Size(); i++)
    {
        if (unit == _supplyUnits[i])
        {
            _supplyUnits.Delete(i);
            break;
        }
    }

    if (_supplyUnits.Size() == 0)
    {
        if (GetAllocSupply())
        {
            LOG_DEBUG(Physics, "  {}: hard patch unalloc", (const char*)GetDebugName());
        }
    }
    UpdateStop();
}

void VehicleSupply::WaitForSupply(AIUnit* unit)
{
    // add to queue only when commander is alive
    AIUnit* commander = CommanderUnit();
    if (!commander || commander->GetLifeState() != AIUnit::LSAlive)
    {
        return;
    }
    SupplyStarted(unit);
}

bool VehicleSupply::CanCancelStop() const
{
    return true;
}

bool VehicleSupply::FindWeapon(const WeaponType* weapon) const
{
    if (_supply && _supply->FindWeapon(weapon))
    {
        return true;
    }
    return base::FindWeapon(weapon);
}

bool VehicleSupply::FindMagazine(const Magazine* magazine) const
{
    if (_supply && _supply->FindMagazine(magazine))
    {
        return true;
    }
    return base::FindMagazine(magazine);
}

const Magazine* VehicleSupply::FindMagazine(RString name) const
{
    if (_supply)
    {
        const Magazine* magazine = _supply->FindMagazine(name);
        if (magazine)
        {
            return magazine;
        }
    }
    return base::FindMagazine(name);
}

const Magazine* VehicleSupply::FindMagazine(int creator, int id) const
{
    if (_supply)
    {
        const Magazine* magazine = _supply->FindMagazine(creator, id);
        if (magazine)
        {
            return magazine;
        }
    }
    return base::FindMagazine(creator, id);
}

LSError VehicleSupply::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar))
    PARAM_CHECK(ar.Serialize("Supply", _supply, 1))
    if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassSecond)
    {
        if (_supply)
        {
            _supply->SetParent(this);
        }
    }
    PARAM_CHECK(ar.SerializeRefs("SupplyUnits", _supplyUnits, 1))
    return LSOK;
}

NetworkMessageType VehicleSupply::GetNMType(NetworkMessageClass cls) const
{
    switch (cls)
    {
        case NMCUpdateGeneric:
            return NMTUpdateVehicleSupply;
        default:
            return base::GetNMType(cls);
    }
}

IndicesUpdateVehicleSupply::IndicesUpdateVehicleSupply() = default;

void IndicesUpdateVehicleSupply::Scan(NetworkMessageFormatBase* format)
{
    base::Scan(format);

    supply.Scan(format);
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesUpdateVehicleSupply()
{
    using namespace Poseidon;
    return new IndicesUpdateVehicleSupply();
}
namespace Poseidon
{

NetworkMessageFormat& VehicleSupply::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    switch (cls)
    {
        case NMCUpdateGeneric:
            base::CreateFormat(cls, format);
            ResourceSupply::CreateFormat(format);
            break;
        default:
            base::CreateFormat(cls, format);
            break;
    }
    return format;
}

TMError VehicleSupply::TransferMsg(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
            TMCHECK(base::TransferMsg(ctx))
            if (_supply)
            {
                PoseidonAssert(dynamic_cast<const IndicesUpdateVehicleSupply*>(ctx.GetIndices()))
                    const IndicesUpdateVehicleSupply* indices =
                        static_cast<const IndicesUpdateVehicleSupply*>(ctx.GetIndices());

                TMCHECK(_supply->TransferMsg(ctx, &indices->supply))
            }
            break;
        default:
            return base::TransferMsg(ctx);
    }
    return TMOK;
}

float VehicleSupply::CalculateError(NetworkMessageContext& ctx)
{
    float error = 0;
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
            error += base::CalculateError(ctx);
            if (_supply)
            {
                PoseidonAssert(dynamic_cast<const IndicesUpdateVehicleSupply*>(ctx.GetIndices()))
                    const IndicesUpdateVehicleSupply* indices =
                        static_cast<const IndicesUpdateVehicleSupply*>(ctx.GetIndices());
                error += _supply->CalculateError(ctx, &indices->supply);
            }
            break;
        default:
            error += base::CalculateError(ctx);
            break;
    }

    return error;
}

void VehicleSupply::ResetStatus()
{
    if (IsSupply(this))
    {
        _supply = new ResourceSupply(this);
    }

    base::ResetStatus();
}

void VehicleSupply::SetAllocSupply(EntityAI* vehicle)
{
    if (_supply)
    {
        if (_supply->GetAlloc() != vehicle)
        {
            LOG_DEBUG(Physics, "{} Allocated for {}", (const char*)GetDebugName(),
                      vehicle ? (const char*)vehicle->GetDebugName() : "nullptr");
            _supply->SetAlloc(vehicle);
        }
    }
}

EntityAI* VehicleSupply::GetAllocSupply() const
{
    return _supply ? _supply->GetAlloc() : nullptr;
}

bool VehicleSupply::Supply(EntityAI* vehicle, UIActionType action, int param, int param2, RString param3)
{
    return _supply ? _supply->Supply(vehicle, action, param, param2, param3) : false;
}

EntityAI* VehicleSupply::GetSupplying() const
{
    return _supply ? _supply->GetSupplying() : nullptr;
}

float VehicleSupply::GetExplosives() const
{
    float expl = GetType()->_secondaryExplosion;
    if (expl >= 0)
    {
        return expl;
    }
    // negative _secondaryExplosion is a multiplier on the computed value
    const float AmmoCostToExplosion = 0.05;
    const float FuelCostToExplosion = 0.10;
    float explosion = base::GetExplosives();
    float fuelExplosion = (GetFuel() + GetFuelCargo()) * FuelCostToExplosion;
    float cargoExplosion = GetAmmoCargo() * AmmoCostToExplosion;
    explosion += fuelExplosion;
    explosion += cargoExplosion;
    return explosion * fabs(expl);
}

void VehicleSupply::Simulate(float deltaT, SimulationImportance prec)
{
    if (_supply && !IsDammageDestroyed())
    {
        _supply->Simulate(deltaT, SimulateVisibleFar);
    }
    base::Simulate(deltaT, prec);
}

RString VehicleSupply::GetActionName(const UIAction& action)
{
    return base::GetActionName(action);
}

void VehicleSupply::PerformAction(const UIAction& action, AIUnit* unit)
{
    EntityAI* veh = unit->GetVehicle();
    switch (action.type)
    {
        case ATTakeWeapon:
        case ATTakeMagazine:
        case ATDropWeapon:
        case ATDropMagazine:
            veh = unit->GetPerson();
            // continue
        case ATHeal:
        case ATRepair:
        case ATRefuel:
        case ATRearm:
            if (!unit->GetVehicle()->IsActionInProgress(MFReload))
            {
                Supply(veh, action.type, action.param, action.param2, action.param3);
            }
            break;
        default:
            base::PerformAction(action, unit);
            break;
    }
}

void VehicleSupply::GetActions(UIActions& actions, AIUnit* unit, bool now)
{
    base::GetActions(actions, unit, now);
    if (unit && _supply && !IsDammageDestroyed())
    {
        AIGroup* grp = unit->GetGroup();
        AICenter* center = grp ? grp->GetCenter() : nullptr;
        if (center && center->IsEnemy(GetTargetSide()))
        {
            return;
        }
        _supply->GetActions(actions, unit, now);
    }
}

TransportType::TransportType(const ParamEntry* param) : base(param) {}

static ManVehAction GetActionVehName(const RStringB& name)
{
    int id = GActionVehNames.GetValue(name);
    if (id >= 0)
    {
        return ManVehAction(id);
    }
    RptF("%s not found in GActionVehNames", (const char*)name);
    return ManVehActNone;
}

void TransportType::Load(const ParamEntry& par)
{
    base::Load(par);

#define GET_PAR(x) _##x = par >> #x

    GET_PAR(getInRadius);

    GET_PAR(hasDriver);
    GET_PAR(hasGunner);
    GET_PAR(hasCommander);
    GET_PAR(driverIsCommander);

    _crew = par >> "crew";
    _getInAction = ManAction(int(par >> "getInAction"));
    _getOutAction = ManAction(int(par >> "getOutAction"));
    _driverAction = GetActionVehName(par >> "driverAction");
    _gunnerAction = GetActionVehName(par >> "gunnerAction");
    _commanderAction = GetActionVehName(par >> "commanderAction");
    _driverInAction = GetActionVehName(par >> "driverInAction");
    _gunnerInAction = GetActionVehName(par >> "gunnerInAction");
    _commanderInAction = GetActionVehName(par >> "commanderInAction");

    _maxManCargo = par >> "transportSoldier";
    _typicalCargo.Resize(0);
    const ParamEntry& typCargo = par >> "typicalCargo";
    for (int i = 0; i < typCargo.GetSize(); i++)
    {
        RString name = typCargo[i];
        const VehicleNonAIType* vType = VehicleTypes.New(name);
        const VehicleType* type = dynamic_cast<const VehicleType*>(vType);
        if (!type)
        {
            continue;
        }
        _typicalCargo.Add(const_cast<VehicleType*>(type));
    }
    _typicalCargo.Compact();

    const ParamEntry& cargo = par >> "cargoAction";
    _cargoAction.Realloc(cargo.GetSize());
    _cargoAction.Resize(cargo.GetSize());
    for (int i = 0; i < cargo.GetSize(); i++)
    {
        _cargoAction[i] = GetActionVehName(cargo[i]);
    }
    const ParamEntry& cargoCoDriver = par >> "cargoIsCoDriver";
    _cargoCoDriver.Realloc(cargoCoDriver.GetSize());
    _cargoCoDriver.Resize(cargoCoDriver.GetSize());
    for (int i = 0; i < cargoCoDriver.GetSize(); i++)
    {
        _cargoCoDriver[i] = cargoCoDriver[i];
    }

    GET_PAR(hideProxyInCombat);

    GET_PAR(forceHideGunner);
    GET_PAR(forceHideDriver);
    GET_PAR(forceHideCommander);

    GET_PAR(outGunnerMayFire);
    GET_PAR(viewGunnerInExternal);
    GET_PAR(unloadInCombat);

    GET_PAR(insideSoundCoef);

    _driverOpticsColor = GetPackedColor(par >> "driverOpticsColor");
    _gunnerOpticsColor = GetPackedColor(par >> "gunnerOpticsColor");
    _commanderOpticsColor = GetPackedColor(par >> "commanderOpticsColor");

    _viewCommander.Load(par >> "ViewCommander");
    _viewGunner.Load(par >> "ViewGunner");
    _viewCargo.Load(par >> "ViewCargo");
    _viewOptics.Load(par >> "ViewOptics");

    GET_PAR(gunnerUsesPilotView);
    GET_PAR(commanderUsesPilotView);

    GET_PAR(castDriverShadow);
    GET_PAR(castGunnerShadow);
    GET_PAR(castCommanderShadow);
    GET_PAR(castCargoShadow);

    GET_PAR(ejectDeadDriver);
    GET_PAR(ejectDeadGunner);
    GET_PAR(ejectDeadCommander);
    GET_PAR(ejectDeadCargo);

    GET_PAR(hideWeaponsDriver);
    GET_PAR(hideWeaponsGunner);
    GET_PAR(hideWeaponsCommander);
    GET_PAR(hideWeaponsCargo);

#undef GET_PAR
}

void TransportType::AddProxy(ManProxy& mProxy, const ProxyObject& proxy, ManAnimationType anim)
{
    Matrix4& trans = mProxy.transform;
    trans = proxy.obj->Transform();
    LODShapeWithShadow* pshape = proxy.obj->GetShape();
    trans.SetPosition(trans.FastTransform(-pshape->BoundingCenter()));
    // shape is Y-reversed; restore correct orientation
    trans.SetOrientation(trans.Orientation() * Matrix3(MScale, -1, 1, -1));
    mProxy.selection = proxy.selection;
}

Threat TransportType::GetStrategicThreat(float distance2, float visibility, float cosAngle) const
{
    Threat threat = base::GetStrategicThreat(distance2, visibility, cosAngle);
    for (int i = 0; i < _typicalCargo.Size(); i++)
    {
        const VehicleType* type = _typicalCargo[i];
        PoseidonAssert(this != type);
        threat += type->GetStrategicThreat(distance2, visibility, -1);
    }
    return threat;
}

Threat TransportType::GetDammagePerMinute(float distance2, float visibility, EntityAI* vehicle) const
{
    Threat threat = base::GetDammagePerMinute(distance2, visibility, vehicle);

    if (vehicle && _maxManCargo > 0)
    {
        PoseidonAssert(dyn_cast<const Transport>(vehicle));
        PoseidonAssert(vehicle->GetType() == this);
        const Transport* transp = static_cast<const Transport*>(vehicle);
        const ManCargo& cargo = transp->GetManCargo();
        for (int i = 0; i < cargo.Size(); i++)
        {
            Person* man = cargo[i];
            if (man)
            {
                threat += man->GetType()->GetDammagePerMinute(distance2, visibility, man);
            }
        }
    }
    else
    {
        for (int i = 0; i < _typicalCargo.Size(); i++)
        {
            const VehicleType* type = _typicalCargo[i];
            PoseidonAssert(type != this);
            threat += type->GetDammagePerMinute(distance2, visibility);
        }
    }

    return threat;
}

static bool NameInList(const ParamEntry& cfg, const char* name)
{
    for (int i = 0; i < cfg.GetSize(); i++)
    {
        RStringB val = cfg[i];
        if (!strcmpi(val, name))
        {
            return true;
        }
    }
    return false;
}

void TransportType::InitShape()
{
    base::InitShape();

    for (int level = 0; level < _shape->NLevels(); level++)
    {
        LevelProxies& proxies = _proxies[level];
        Shape* shape = _shape->LevelOpaque(level);
        proxies._driverProxy = ManProxy();
        proxies._gunnerProxy = ManProxy();
        proxies._commanderProxy = ManProxy();
        proxies._cargoProxy.Resize(0);

        for (int i = 0; i < shape->NProxies(); i++)
        {
            const ProxyObject& proxy = shape->Proxy(i);
            ProxyCrew* proxyCrew = dyn_cast<ProxyCrew, Object>(proxy.obj);
            if (proxyCrew)
            {
                // check crew type
                CrewPosition pos = proxyCrew->Type()->GetCrewPosition();
                if (pos == CPDriver)
                {
                    AddProxy(proxies._driverProxy, proxy, "Driver");
                }
                else if (pos == CPGunner)
                {
                    AddProxy(proxies._gunnerProxy, proxy, "Gunner");
                }
                else if (pos == CPCommander)
                {
                    AddProxy(proxies._commanderProxy, proxy, "Commander");
                }
                else if (pos == CPCargo)
                {
                    int id = proxy.id;
                    if (id < 1)
                    {
                        Fail("Bad cargo proxy index");
                        continue;
                    }
                    if (proxies._cargoProxy.Size() < id)
                    {
                        proxies._cargoProxy.Resize(id);
                    }
                    ManProxy& mProxy = proxies._cargoProxy[id - 1];
                    AddProxy(mProxy, proxy, "Cargo");
                }
                continue;
            }

            char shortname[256];
            Vehicle* proxyVeh = dyn_cast<Vehicle, Object>(proxy.obj);
            if (proxyVeh)
            {
                snprintf(shortname, sizeof(shortname), "%s", (const char*)proxyVeh->GetName());
            }
            else
            {
                if (proxy.obj->GetShape())
                {
                    GetFilename(shortname, proxy.obj->GetShape()->Name());
                }
                else
                {
                    *shortname = 0;
                }
            }
            const ParamEntry& cfg = Pars >> "CfgCrew";
            const ParamEntry& driverList = cfg >> "drivers";
            const ParamEntry& gunnerList = cfg >> "gunners";
            const ParamEntry& commanderList = cfg >> "commanders";
            const ParamEntry& cargoList = cfg >> "cargo";

            if (NameInList(driverList, shortname))
            {
                AddProxy(proxies._driverProxy, proxy, "Driver");
                RptF("%s: old crew proxy %s", (const char*)GetName(), shortname);
            }
            else if (NameInList(gunnerList, shortname))
            {
                AddProxy(proxies._gunnerProxy, proxy, "Gunner");
                RptF("%s: old crew proxy %s", (const char*)GetName(), shortname);
            }
            else if (NameInList(commanderList, shortname))
            {
                AddProxy(proxies._commanderProxy, proxy, "Commander");
                RptF("%s: old crew proxy %s", (const char*)GetName(), shortname);
            }
            else if (NameInList(cargoList, shortname))
            {
                int id = proxy.id;
                if (id < 1)
                {
                    Fail("Bad cargo proxy index");
                    continue;
                }
                if (proxies._cargoProxy.Size() < id)
                {
                    proxies._cargoProxy.Resize(id);
                }
                ManProxy& mProxy = proxies._cargoProxy[id - 1];
                AddProxy(mProxy, proxy, "Cargo");
                RptF("%s: old crew proxy %s", (const char*)GetName(), shortname);
            }
        }
    }

    Shape* memory = _shape->MemoryLevel();
    if (memory)
    {
        _driverOpticsPos = memory->FindNamedSel("driverview");
        if (_driverOpticsPos < 0)
        {
            _driverOpticsPos = memory->FindNamedSel("pilot");
        }
        _gunnerOpticsPos = memory->FindNamedSel("gunnerview");
        _commanderOpticsPos = memory->FindNamedSel("commanderview");
    }
    else
    {
        _driverOpticsPos = -1;
        _gunnerOpticsPos = -1;
        _commanderOpticsPos = -1;
    }

    const ParamEntry& par = *_par;

    RStringB oModelName = par >> "driverOpticsModel";
    _driverOpticsModel = oModelName.GetLength() > 0 ? Shapes.New(::GetShapeName(oModelName), true, false) : nullptr;
    if (_driverOpticsModel && _driverOpticsModel->NLevels() > 0)
    {
        _driverOpticsModel->LevelOpaque(0)->MakeCockpit();
        _driverOpticsModel->OrSpecial(BestMipmap | NoDropdown);
    }
    oModelName = par >> "gunnerOpticsModel";
    _gunnerOpticsModel = oModelName.GetLength() > 0 ? Shapes.New(::GetShapeName(oModelName), true, false) : nullptr;
    if (_gunnerOpticsModel && _gunnerOpticsModel->NLevels() > 0)
    {
        _gunnerOpticsModel->LevelOpaque(0)->MakeCockpit();
        _gunnerOpticsModel->OrSpecial(BestMipmap | NoDropdown);
    }
    oModelName = par >> "commanderOpticsModel";
    _commanderOpticsModel = oModelName.GetLength() > 0 ? Shapes.New(::GetShapeName(oModelName), true, false) : nullptr;
    if (_commanderOpticsModel && _commanderOpticsModel->NLevels() > 0)
    {
        _commanderOpticsModel->LevelOpaque(0)->MakeCockpit();
        _commanderOpticsModel->OrSpecial(BestMipmap | NoDropdown);
    }

    _driverGetInPos.Resize(0);
    _commanderGetInPos.Resize(0);
    _gunnerGetInPos.Resize(0);
    _cargoGetInPos.Resize(0);
    if (memory)
    {
        int index = memory->FindNamedSel("pos driver");
        if (index >= 0)
        {
            const Selection& sel = memory->NamedSel(index);
            for (int i = 0; i < sel.Size(); i++)
            {
                if (sel[i] >= 0)
                {
                    _driverGetInPos.Add(memory->Pos(sel[i]));
                }
            }
        }
        index = memory->FindNamedSel("pos commander");
        if (index >= 0)
        {
            const Selection& sel = memory->NamedSel(index);
            for (int i = 0; i < sel.Size(); i++)
            {
                if (sel[i] >= 0)
                {
                    _commanderGetInPos.Add(memory->Pos(sel[i]));
                }
            }
        }
        index = memory->FindNamedSel("pos gunner");
        if (index >= 0)
        {
            const Selection& sel = memory->NamedSel(index);
            for (int i = 0; i < sel.Size(); i++)
            {
                if (sel[i] >= 0)
                {
                    _gunnerGetInPos.Add(memory->Pos(sel[i]));
                }
            }
        }
        index = memory->FindNamedSel("pos cargo");
        if (index >= 0)
        {
            const Selection& sel = memory->NamedSel(index);
            for (int i = 0; i < sel.Size(); i++)
            {
                if (sel[i] >= 0)
                {
                    _cargoGetInPos.Add(memory->Pos(sel[i]));
                }
            }
        }
        index = memory->FindNamedSel("pos codriver");
        if (index >= 0)
        {
            const Selection& sel = memory->NamedSel(index);
            for (int i = 0; i < sel.Size(); i++)
            {
                if (sel[i] >= 0)
                {
                    _coDriverGetInPos.Add(memory->Pos(sel[i]));
                }
            }
        }
    }
    if (_driverGetInPos.Size() == 0)
    {
        float sizeCoef = _shape->GeometrySphere() * (1.0 / 4);
        _driverGetInPos.Add(Vector3(+3 * sizeCoef, +1, -3 * sizeCoef));
    }
    if (_commanderGetInPos.Size() == 0)
    {
        _commanderGetInPos = _driverGetInPos;
    }
    if (_gunnerGetInPos.Size() == 0)
    {
        _gunnerGetInPos = _driverGetInPos;
    }
    if (_cargoGetInPos.Size() == 0)
    {
        _cargoGetInPos = _driverGetInPos;
    }
    if (_coDriverGetInPos.Size() == 0)
    {
        _coDriverGetInPos = _cargoGetInPos;
    }
    _driverGetInPos.Compact();
    _commanderGetInPos.Compact();
    _gunnerGetInPos.Compact();
    _cargoGetInPos.Compact();
    _coDriverGetInPos.Compact();

    if (_driverGetInPos.Size() == 0)
    {
        LOG_DEBUG(Physics, "Type {} missing driver position", (const char*)GetName());
    }
}

void TransportType::DeinitShape()
{
    _driverOpticsModel.Free();
    _gunnerOpticsModel.Free();
    _commanderOpticsModel.Free();
    base::DeinitShape();
}

template <>
const EnumName* Foundation::GetEnumNames(Transport::LandingMode dummy)
{
    static const EnumName LandingModeNames[] = {
        EnumName(Transport::LMNone, "NONE"), EnumName(Transport::LMLand, "LAND"),
        EnumName(Transport::LMGetIn, "GET IN"), EnumName(Transport::LMGetOut, "GET OUT"), EnumName()};
    return LandingModeNames;
}

Transport::Transport(VehicleType* name, Person* driver, bool fullCreate)
    : base(name, fullCreate),

      _getinTime(0), _getoutTime(Glob.time - 60), _engineOff(true),

      _getOutAfterDammage(TIME_MAX), _explosionTime(TIME_MAX),

      _turretFrontUntil(TIME_MIN),

      _driverPos(VZero), _dirWanted(VZero), _moveMode(VMMFormation), _turnMode(VTMNone), _fireEnabled(false),
      _mouseDirWanted(VForward), _lock(LSDefault), _landing(LMNone),

      _radio(CCVehicle, this, RNIntercomm),

      _showDmg(false), _showDmgValid(Glob.time - 60),

      _randomizer(GRandGen.RandomValue()), // stagger per-vehicle sound frequency

      _commanderHidden(Type()->_hideProxyInCombat), _driverHidden(Type()->_hideProxyInCombat),
      _gunnerHidden(Type()->_hideProxyInCombat), _commanderHiddenWanted(Type()->_hideProxyInCombat),
      _driverHiddenWanted(Type()->_hideProxyInCombat), _gunnerHiddenWanted(Type()->_hideProxyInCombat),

      _manualFire(false),

      _doCrash(CrashNone)
{
    _destrType = GetType()->GetDestructType();
    if ((DestructType)_destrType == DestructDefault)
    {
        _destrType = DestructEngine;
    }

    DoAssert(driver == nullptr);

    DriverConstruct(driver);
    _manCargo.Resize(Type()->_maxManCargo);
    _fuel = Type()->GetFuelCapacity();
}

AIGroup* Transport::GetGroupAssigned()
{
    return _groupAssigned;
}
AIUnit* Transport::GetDriverAssigned()
{
    return _driverAssigned;
}
AIUnit* Transport::GetGunnerAssigned()
{
    return _gunnerAssigned;
}
AIUnit* Transport::GetCommanderAssigned()
{
    return _commanderAssigned;
}
AIUnit* Transport::GetCargoAssigned(int i)
{
    _cargoAssigned.Compact();
    return _cargoAssigned[i];
}
void Transport::AssignGroup(AIGroup* grp)
{
    _groupAssigned = grp;
}
void Transport::AssignDriver(AIUnit* unit)
{
    _driverAssigned = unit;
}
void Transport::AssignGunner(AIUnit* unit)
{
    _gunnerAssigned = unit;
}
void Transport::AssignCommander(AIUnit* unit)
{
    _commanderAssigned = unit;
}
void Transport::AssignCargo(AIUnit* cargo)
{
    _cargoAssigned.Add(cargo);
}

int Transport::NCargoAssigned()
{
    return _cargoAssigned.Count();
}

} // namespace Poseidon
