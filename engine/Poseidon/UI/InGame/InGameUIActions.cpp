
#include <Poseidon/World/World.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/Entities/Infantry/Person.hpp>
#include <Poseidon/UI/InGame/InGameUIImpl.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>

#include <Poseidon/World/Entities/Infantry/MoveActions.hpp>
#include <Poseidon/World/Entities/Infantry/ManActs.hpp>

#include <Poseidon/Core/resincl.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Foundation/Algorithms/Qsort.hpp>
#include <Poseidon/Foundation/Strings/Mbcs.hpp>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <cmath>
#include <Poseidon/Foundation/Containers/BankArray.hpp>
#include <Poseidon/Foundation/Containers/BoolArray.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

using namespace Poseidon;
namespace Poseidon
{
using Poseidon::Foundation::QSort;

// Global selected units - valid for IngameUI and map

OLink<AIUnit>* GetGSelectedUnits()
{
    static OLink<AIUnit> GSelectedUnits[MAX_UNITS_PER_GROUP];
    return GSelectedUnits;
}

static Team* GetGTeams()
{
    static Team GTeams[MAX_UNITS_PER_GROUP];
    return GTeams;
}

Team GetTeam(int i)
{
    return GetGTeams()[i];
}

void SetTeam(int i, Team team)
{
    GetGTeams()[i] = team;
}

void ClearTeams()
{
    Team* GTeams = GetGTeams();
#ifdef __ICL
#pragma novector
#endif
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        GTeams[i] = TeamMain;
    }
}

PackedBoolArray ListTeam(Team team)
{
    Team* GTeams = GetGTeams();
    PackedBoolArray list;
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        if (GTeams[i] == team)
        {
            list.Set(i, true);
        }
    }
    return list;
}

AIUnit* GetSelectedUnit(int i)
{
    OLink<AIUnit>* GSelectedUnits = GetGSelectedUnits();
    AIUnit* unit = GSelectedUnits[i];
    if (unit == GWorld->FocusOn())
    {
        GSelectedUnits[i] = nullptr;
        return nullptr;
    }
    else
    {
        return unit;
    }
}

void SetSelectedUnit(int i, AIUnit* unit)
{
    OLink<AIUnit>* GSelectedUnits = GetGSelectedUnits();
    if (unit == GWorld->FocusOn())
    {
        unit = nullptr;
    }
    GSelectedUnits[i] = unit;
}

void ClearSelectedUnits()
{
    OLink<AIUnit>* GSelectedUnits = GetGSelectedUnits();
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        GSelectedUnits[i] = nullptr;
    }
}

bool IsEmptySelectedUnits()
{
    OLink<AIUnit>* GSelectedUnits = GetGSelectedUnits();
    if (GWorld->FocusOn())
    {
        GSelectedUnits[GWorld->FocusOn()->ID() - 1] = nullptr;
    }

    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        if (GSelectedUnits[i])
        {
            return false;
        }
    }
    return true;
}

PackedBoolArray ListSelectedUnits()
{
    OLink<AIUnit>* GSelectedUnits = GetGSelectedUnits();
    if (GWorld->FocusOn())
    {
        GSelectedUnits[GWorld->FocusOn()->ID() - 1] = nullptr;
    }

    PackedBoolArray list;
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        if (GSelectedUnits[i])
        {
            list.Set(i, true);
        }
    }
    return list;
}

} // namespace Poseidon
#include <Poseidon/UI/InGame/InGameUIDrawShared.hpp>
UIActions::UIActions()
{
    _selected.type = ATNone;

    _right = 0.98;
    _bottom = 0.88;
    _w = 0.3;
    _h = 0.02;

    _rows = 6;

    _bgColor = PackedColor(Color(0, 0, 0, 0.8));
    _textColor = PackedColor(Color(0.8, 0.8, 0.8, 1));
    _selColor = PackedColor(Color(0.9, 0.8, 0, 1));

    static FontID tahomaB24Font = GetFontID("tahomaB24");
    _font = GEngine->LoadFont(tahomaB24Font);
    //	_size = _font->Height();
    _size = 0.02;

    Hide();
}

void UIActions::Refresh(bool user)
{
    if (user || Glob.uiTime < _dimStart + dimTime)
    {
        _dimStart = Glob.uiTime - protectionTime; // avoid protection
    }
    else
    {
        _dimStart = Glob.uiTime; // set protection
    }
}

void UIActions::Hide()
{
    _dimStart = Glob.uiTime - fadeTime;
}

float UIActions::GetAge() const
{
    return Glob.uiTime - _dimStart;
}

float UIActions::GetAlpha() const
{
    float age = GetAge();
    if (age <= dimTime)
    {
        return 1.0f;
    }
    if (age >= fadeTime)
    {
        return 0.0f;
    }
    return fadeCoef * (fadeTime - age);
}

int UIActions::Add(UIActionType type, TargetType* target, float priority, int param, bool showWindow, bool hideOnUse,
                   int param2, RString param3)
{
    int index = base::Add();
    UIAction& action = Set(index);
    action.type = type;
    action.target = target;
    action.param = param;
    action.param2 = param2;
    action.param3 = param3;
    action.priority = priority;
    action.showWindow = showWindow;
    action.hideOnUse = hideOnUse;
    return index;
}

int UIActions::FindSelected()
{
    int n = Size();
    if (n == 0)
    {
        return -1;
    }
    if (_selected.type == ATNone)
    {
        return 0; // default
    }
    for (int i = 0; i < n; i++)
    {
        const UIAction& action = Get(i);
        if (action.type == _selected.type && action.target == _selected.target && action.param == _selected.param &&
            action.param2 == _selected.param2 && action.param3 == _selected.param3)
        {
            return i;
        }
    }
    _selected.type = ATNone;
    return 0; // default
}

void UIActions::SelectPrev(bool cycle)
{
    if (Size() == 0)
    {
        return;
    }
    int i = FindSelected() - 1;
    if (i < 0)
    {
        if (cycle)
        {
            i = Size() - 1;
        }
        else
        {
            i = 0;
        }
    }
    _selected = Get(i);
}

void UIActions::SelectNext(bool cycle)
{
    if (Size() == 0)
    {
        return;
    }
    int i = FindSelected() + 1;
    if (i >= Size())
    {
        if (cycle)
        {
            i = 0;
        }
        else
        {
            i = Size() - 1;
        }
    }
    _selected = Get(i);
}

bool CheckSupply(EntityAI* vehicle, EntityAI* parent, SupportCheckF check, float limit, bool now);

void UIAction::Process(AIUnit* unit) const
{
    if (!unit || unit->GetLifeState() != AIUnit::LSAlive)
    {
        return;
    }

    EntityAI* veh = target;
    switch (type)
    {
        case ATGetInCommander:
        case ATGetInDriver:
        case ATGetInGunner:
        case ATGetInCargo:
        {
            if (!unit->IsFreeSoldier())
            {
                return;
            }
            Transport* trans = dyn_cast<Transport>(veh);
            if (!trans)
            {
                return;
            }
            if (unit->IsPlayer() && trans->GetLock() == LSLocked)
            {
                return;
            }
            AIGroup* grp = unit->GetGroup();
            PoseidonAssert(grp);
            AICenter* center = grp->GetCenter();
            PoseidonAssert(center);
            // avoid get in enemy vehicle
            if (trans->Commander() && center->IsEnemy(trans->Commander()->GetTargetSide()))
            {
                return;
            }
            if (trans->Driver() && center->IsEnemy(trans->Driver()->GetTargetSide()))
            {
                return;
            }
            if (trans->Gunner() && center->IsEnemy(trans->Gunner()->GetTargetSide()))
            {
                return;
            }
            for (int i = 0; i < trans->GetManCargo().Size(); i++)
            {
                Person* man = trans->GetManCargo()[i];
                if (!man)
                {
                    continue;
                }
                if (center->IsEnemy(man->GetTargetSide()))
                {
                    return;
                }
            }
            switch (type)
            {
                case ATGetInCommander:
                    if (!trans->GetGroupAssigned())
                    {
                        unit->GetGroup()->AddVehicle(trans);
                    }
                    unit->AssignAsCommander(trans);
                    break;
                case ATGetInDriver:
                    if (!trans->GetGroupAssigned())
                    {
                        unit->GetGroup()->AddVehicle(trans);
                    }
                    unit->AssignAsDriver(trans);
                    break;
                case ATGetInGunner:
                    if (!trans->GetGroupAssigned())
                    {
                        unit->GetGroup()->AddVehicle(trans);
                    }
                    unit->AssignAsGunner(trans);
                    break;
                case ATGetInCargo:
                    unit->AssignAsCargo(trans);
                    break;
            }
            unit->AllowGetIn(true);
            unit->OrderGetIn(true);
            DoAssert(unit->VehicleAssigned() == trans);
            void MoveToGetInPos(AIUnit * unit, Transport & veh);
            MoveToGetInPos(unit, *trans);
            ActionContextBase* CreateGetInActionContext(Transport * veh, UIActionType pos);
            Ref<ActionContextBase> context = CreateGetInActionContext(trans, type);
            context->function = MFGetIn;
            unit->GetPerson()->PlayAction(trans->Type()->GetGetInAction(), context);
            // unit->ProcessGetIn2();
        }
            return;
        case ATTakeFlag:
            if (veh && unit->IsFreeSoldier())
            {
                // FIX
                AIGroup* grp = unit->GetGroup();
                AICenter* center = grp ? grp->GetCenter() : nullptr;

                Person* person = dyn_cast<Person>(veh);
                EntityAI* flag = person ? person->GetFlagCarrier() : veh;
                // test conditions once more (action can be launched through procedure)
                if (flag &&
                    (!person && !flag->GetFlagOwner() || flag->GetFlagOwner() == person && !person->IsAbleToMove()) &&
                    center && center->IsEnemy(flag->GetFlagSide()) &&
                    CheckSupply(unit->GetPerson(), veh, nullptr, 0, true))
                {
                    if (flag->IsLocal())
                    {
                        flag->SetFlagOwner(unit->GetPerson());
                    }
                    else
                    {
                        GetNetworkManager().SetFlagOwner(unit->GetPerson(), flag);
                    }
                }
            }
            return;

        case ATReturnFlag:
            if (veh && unit->IsFreeSoldier())
            {
                // FIX
                AIGroup* grp = unit->GetGroup();
                AICenter* center = grp ? grp->GetCenter() : nullptr;

                Person* person = dyn_cast<Person>(veh);
                EntityAI* flag = person ? person->GetFlagCarrier() : veh;
                // test conditions once more (action can be launched through procedure)
                if (flag && person && (flag->GetFlagOwner() == person && !person->IsAbleToMove()) && center &&
                    center->IsFriendly(flag->GetFlagSide()) && CheckSupply(unit->GetPerson(), veh, nullptr, 0, true))
                {
                    if (flag->IsLocal())
                    {
                        flag->SetFlagOwner(nullptr);
                    }
                    else
                    {
                        GetNetworkManager().SetFlagOwner(nullptr, flag);
                    }
                }
            }
            return;
        case ATLoadMagazine:
            if (veh && unit->GetVehicle() == veh && !veh->IsActionInProgress(MFReload))
            {
                int iMagazine = -1;
                for (int i = 0; i < veh->NMagazines(); i++)
                {
                    const Magazine* mag = veh->GetMagazine(i);
                    if (!mag)
                    {
                        continue;
                    }
                    if (mag->_creator == param && mag->_id == param2)
                    {
                        iMagazine = i;
                        break;
                    }
                }
                if (iMagazine < 0)
                {
                    return;
                }

                const char* separator = strchr(param3, '|');
                if (!separator)
                {
                    return;
                }
                int separatorPos = separator - param3;
                RString weapon = param3.Substring(0, separatorPos);
                RString muzzle = param3.Substring(separatorPos + 1, INT_MAX);
                int iSlot = -1;
                for (int i = 0; i < veh->NMagazineSlots(); i++)
                {
                    const MagazineSlot& slot = veh->GetMagazineSlot(i);
                    if (slot._weapon->GetName() == weapon && slot._muzzle->GetName() == muzzle)
                    {
                        iSlot = i;
                        break;
                    }
                }
                if (iSlot < 0)
                {
                    return;
                }

                veh->ReloadMagazine(iSlot, iMagazine);

                // FIX
                if (!veh->IsLocal())
                {
                    GetNetworkManager().UpdateWeapons(veh);
                }

                void UpdateWeaponsInBriefing();
                UpdateWeaponsInBriefing();
            }
            return;
        case ATDropWeapon:
            if (target && target != unit->GetPerson())
            {
                target->PerformAction(*this, unit);
            }
            else if (unit->IsFreeSoldier())
            {
                // find weapon
                Ref<WeaponType> weapon = WeaponTypes.New(param3);
                if (!weapon || !weapon->_canDrop)
                {
                    return;
                }

                Person* person = unit->GetPerson();
                if (person->FindWeapon(weapon))
                {
                    // create container
                    Ref<VehicleSupply> container;
                    int slots = weapon->_weaponType;
                    if ((slots & MaskSlotSecondary) != 0 && (slots & MaskSlotPrimary) == 0)
                    {
                        container = dyn_cast<VehicleSupply>(NewVehicle("SecondaryWeaponHolder"));
                    }
                    else
                    {
                        container = dyn_cast<VehicleSupply>(NewVehicle("WeaponHolder"));
                    }
                    if (!container)
                    {
                        return;
                    }

                    Vector3 pos = person->Position() + 0.5f * person->Direction() + VUp * 0.5f;
                    Matrix3 dir;
                    dir.SetUpAndDirection(VUp, person->Direction());
                    Matrix4 transform;
                    transform.SetPosition(pos);
                    transform.SetOrientation(dir);

                    container->PlaceOnSurface(transform);
                    container->SetTransform(transform);
                    container->Init(transform);

                    // add container to world
                    GWorld->AddBuilding(container);
                    if (GWorld->GetMode() == GModeNetware)
                    {
                        GetNetworkManager().CreateVehicle(container, VLTBuilding, "", -1);
                    }
                    if (unit->GetGroup())
                    {
                        unit->GetGroup()->AddTarget(container, 4.0f, 4.0f, 0);
                    }

                    // remove weapon
                    person->RemoveWeapon(weapon);
                    if (GWorld->FocusOn() && GWorld->FocusOn()->GetVehicle() == person)
                    {
                        GWorld->UI()->ResetVehicle(person);
                    }

                    container->AddWeaponCargo(weapon, 1);
                    if (GWorld->GetMode() == GModeNetware)
                    {
                        GetNetworkManager().AddWeaponCargo(container, weapon->GetName());
                    }

                    // remove unusable magazines
                    for (int i = 0; i < person->NMagazines();)
                    {
                        Ref<Magazine> magazine = person->GetMagazine(i);
                        if (!magazine || person->IsMagazineUsable(magazine->_type))
                        {
                            i++;
                            continue;
                        }
                        person->RemoveMagazine(magazine);
                        if (magazine->_ammo > 0)
                        {
                            container->AddMagazineCargo(magazine);
                            if (GWorld->GetMode() == GModeNetware)
                            {
                                GetNetworkManager().AddMagazineCargo(container, magazine);
                            }
                        }
                    }
                }
                void UpdateWeaponsInBriefing();
                UpdateWeaponsInBriefing();
            }
            return;
        case ATDropMagazine:
            if (target && target != unit->GetPerson())
            {
                target->PerformAction(*this, unit);
            }
            else if (unit->IsFreeSoldier())
            {
                Person* person = unit->GetPerson();

                // find magazine
                Ref<MagazineType> type = MagazineTypes.New(param3);
                if (!type)
                {
                    return;
                }

                Ref<const Magazine> magazine;
                int minCount = INT_MAX;
                // find in nonused magazines
                for (int i = 0; i < person->NMagazines(); i++)
                {
                    const Magazine* m = person->GetMagazine(i);
                    if (!m)
                    {
                        continue;
                    }
                    if (m->_type != type)
                    {
                        continue;
                    }
                    if (person->IsMagazineUsed(m))
                    {
                        continue;
                    }
                    if (m->_ammo < minCount)
                    {
                        magazine = m;
                        minCount = m->_ammo;
                    }
                }
                // find in all magazines
                if (!magazine)
                {
                    for (int i = 0; i < person->NMagazines(); i++)
                    {
                        const Magazine* m = person->GetMagazine(i);
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
                    // remove magazine
                    person->RemoveMagazine(magazine);
                    if (minCount > 0)
                    {
                        // create container
                        Ref<VehicleSupply> container = dyn_cast<VehicleSupply>(NewVehicle("WeaponHolder"));
                        if (!container)
                        {
                            return;
                        }

                        Vector3 pos = person->Position() + 0.5f * person->Direction() + VUp * 0.5f;
                        Matrix3 dir;
                        dir.SetUpAndDirection(VUp, person->Direction());
                        Matrix4 transform;
                        transform.SetPosition(pos);
                        transform.SetOrientation(dir);

                        container->PlaceOnSurface(transform);
                        container->SetTransform(transform);
                        container->Init(transform);

                        // add container to world
                        GWorld->AddBuilding(container);
                        if (GWorld->GetMode() == GModeNetware)
                        {
                            GetNetworkManager().CreateVehicle(container, VLTBuilding, "", -1);
                        }
                        if (unit->GetGroup())
                        {
                            unit->GetGroup()->AddTarget(container, 4.0f, 4.0f, 0);
                        }

                        // add magazine to container
                        container->AddMagazineCargo(const_cast<Magazine*>(magazine.GetRef()));
                        if (GWorld->GetMode() == GModeNetware)
                        {
                            GetNetworkManager().AddMagazineCargo(container, magazine);
                        }
                    }
                }
                void UpdateWeaponsInBriefing();
                UpdateWeaponsInBriefing();
            }
            return;
        case ATNVGoggles:
        {
            Person* person = unit->GetPerson();
            person->SetNVWanted(!person->IsNVWanted());
        }
        break;
        default:
            if (target)
            {
                target->PerformAction(*this, unit);
                return;
            }
            Fail("Bad action type - no target");
            return;
    }
}

void UIActions::ProcessAction(AIUnit* unit)
{
    PoseidonAssert(unit);

    int i = FindSelected();
    if (i < 0)
    {
        return;
    }
    const UIAction& action = Get(i);
    if (action.hideOnUse)
    {
        Hide();
        _selected.type = ATNone;
    }
    unit->GetVehicle()->StartActionProcessing(action, unit);
}

int CmpActions(const UIAction* action1, const UIAction* action2)
{
    int s = sign(action2->priority - action1->priority);
    if (s != 0)
    {
        return s;
    }
    s = (int)((intptr_t)action1->target.GetLink() - (intptr_t)action2->target.GetLink());
    if (s != 0)
    {
        return s;
    }
    s = action1->param - action2->param;
    if (s != 0)
    {
        return s;
    }
    s = action1->param2 - action2->param2;
    if (s != 0)
    {
        return s;
    }
    return strcmp(action1->param3, action2->param3);
}

void UIActions::Sort()
{
    QSort(Data(), Size(), CmpActions);
}

static float ActionSourceCost(EntityAI* veh, Target* tgt)
{
    // check target focus
    PoseidonAssert(tgt->idExact);
    Vector3 relPos = veh->PositionWorldToModel(tgt->idExact->AimingPosition());
    float dist2 = relPos.SquareSize();
    if (relPos.Z() < 0)
    {
        dist2 *= 8;
    }
    if (fabs(relPos.X()) > relPos.Z())
    {
        dist2 *= 4;
    }
    return dist2;
}

void AddDropActions(AIUnit* unit, UIActions& actions)
{
    if (unit->IsFreeSoldier())
    {
        int n = actions.Size();
        EntityAI* veh = unit->GetVehicle();

        bool findWeapons = false;
        bool findMagazines = false;
        for (int i = 0; i < n; i++)
        {
            if (actions[i].type == ATDropWeapon)
            {
                findWeapons = true;
            }
            else if (actions[i].type == ATDropMagazine)
            {
                findMagazines = true;
            }
        }

        if (!findWeapons)
        {
            int index = veh->SelectedWeapon();
            if (index >= 0 && index < veh->NMagazineSlots())
            {
                const MagazineSlot& slot = veh->GetMagazineSlot(index);
                if (slot._weapon && slot._weapon->_canDrop)
                {
                    actions.Add(ATDropWeapon, nullptr, -0.01, 0, false, false, 0, slot._weapon->GetName());
                }
            }
        }
        if (!findMagazines)
        {
            int index = veh->SelectedWeapon();
            if (index >= 0 && index < veh->NMagazineSlots())
            {
                const MagazineSlot& slot = veh->GetMagazineSlot(index);
                if (slot._magazine)
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
                        // if (veh->IsMagazineUsed(magazine)) continue;
                        actions.Add(ATDropMagazine, nullptr, -0.02, 0, false, false, 0, type->GetName());
                        break;
                    }
                }
            }
        }
    }
}

void AddDropAllActions(AIUnit* unit, UIActions& actions)
{
    if (unit->IsFreeSoldier())
    {
        EntityAI* veh = unit->GetVehicle();

        for (int i = 0; i < veh->NWeaponSystems(); i++)
        {
            const WeaponType* weapon = veh->GetWeaponSystem(i);
            if (weapon && weapon->_canDrop)
            {
                actions.Add(ATDropWeapon, nullptr, -0.01, 0, false, false, 0, weapon->GetName());
            }
        }
        AUTO_STATIC_ARRAY(const MagazineType*, types, 32);
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
            // if (veh->IsMagazineUsed(magazine)) continue;
            const MagazineType* type = magazine->_type;
            bool found = false;
            for (int j = 0; j < types.Size(); j++)
            {
                if (types[j] == type)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                actions.Add(ATDropMagazine, nullptr, -0.02, 0, false, false, 0, type->GetName());
                types.Add(type);
            }
        }
    }
}
