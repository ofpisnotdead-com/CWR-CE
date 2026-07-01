
#include <Poseidon/World/Entities/Vehicles/Transport.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/Entities/Infantry/Person.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Game/UiActions.hpp>

#include <Poseidon/Foundation/Enums/EnumNames.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>

namespace Poseidon
{
using namespace Foundation;
DEFINE_CASTING(Person)
Person::Person(VehicleType* name, bool fullCreate) : base(name, fullCreate), _sensorRowID(-1), _remotePlayer(1)
{
    _info._rank = RankPrivate;
    _info._experience = 0;
    _info._initExperience = 0;
}

Person::~Person() {}

void Person::SetBrain(AIUnit* brain)
{
    _brain = brain;
}

LODShapeWithShadow* Person::GetOpticsModel(Person* person)
{
    PoseidonAssert(person == this);

    if (_currentWeapon < 0)
    {
        return nullptr;
    }
    MuzzleType* muzzle = GetMagazineSlot(_currentWeapon)._muzzle;
    if (!muzzle)
    {
        return nullptr;
    }
    return muzzle->_opticsModel;
}

bool Person::GetForceOptics(Person* person) const
{
    PoseidonAssert(person == this);

    if (_currentWeapon < 0)
    {
        return false;
    }
    MuzzleType* muzzle = GetMagazineSlot(_currentWeapon)._muzzle;
    if (!muzzle)
    {
        return false;
    }
    return muzzle->_forceOptics;
}

PackedColor Person::GetOpticsColor(Person* person)
{
    return PackedWhite; // Note: get from muzzle
}

bool Person::IsNVEnabled() const
{
    return false;
}

bool Person::IsNVWanted() const
{
    return false;
}

void Person::SetNVWanted(bool set) {}

void Person::DrawNVOptics() {}

void Person::Simulate(float deltaT, SimulationImportance prec)
{
    base::Simulate(deltaT, prec);
}

TargetSide Person::GetTargetSide() const
{
    return base::GetTargetSide();
}

bool Person::QIsManual() const
{
    if (!GLOB_WORLD->PlayerManual())
    {
        return false;
    }
    if (!GLOB_WORLD->PlayerOn())
    {
        return false;
    }
    return GLOB_WORLD->PlayerOn() == this;
}

void Person::SetFace(RString name, RString player) {}

float Person::GetLegPhase() const
{
    return 0;
}

void Person::SetGlasses(RString name) {}

void Person::ResetStatus()
{
    _sensorRowID = SensorRowID(-1);
    base::ResetStatus();
}

UnitPosition Person::GetUnitPosition() const
{
    return UPAuto;
}

void Person::SetUnitPosition(UnitPosition status) {}

ActionContextBase::ActionContextBase()
{
    function = MoveFinishF(0);
}

bool Person::PlayAction(ManAction action, ActionContextBase* context)
{
    return false;
}

bool Person::SwitchAction(ManAction action, ActionContextBase* context)
{
    return false;
}

void Person::SwitchVehicleAction(ManVehAction action) {}

void Person::CheckAmmo(const MuzzleType*& muzzle1, const MuzzleType*& muzzle2, int& slots1, int& slots2, int& slots3)
{
    Person* man = this;
    int slotsMax = GetItemSlotsCount(man->GetType()->_weaponSlots);

    // primary and secondary weapon
    int index = man->FindWeaponType(MaskSlotPrimary);
    const WeaponType* primary = index >= 0 ? man->GetWeaponSystem(index) : nullptr;
    index = man->FindWeaponType(MaskSlotSecondary);
    const WeaponType* secondary = index >= 0 ? man->GetWeaponSystem(index) : nullptr;

    // calculate maximal usable slots
    muzzle1 = nullptr;
    if (primary && primary->_muzzles.Size() > 0)
    {
        muzzle1 = primary->_muzzles[0];
    }
    slots1 = 0;
    if (muzzle1)
    {
        slots1 = slotsMax < 4 ? slotsMax : 4;
    }

    muzzle2 = nullptr;
    if (secondary && secondary->_muzzles.Size() > 0)
    {
        muzzle2 = secondary->_muzzles[0];
    }
    else if (primary && primary->_muzzles.Size() > 1)
    {
        muzzle2 = primary->_muzzles[1];
    }
    slots2 = 0;
    if (muzzle2)
    {
        slots2 = slotsMax - slots1;
    }

    slots3 = slotsMax - slots1 - slots2;

    // remove used slots
    for (int i = 0; i < man->NMagazines(); i++)
    {
        const Magazine* magazine = man->GetMagazine(i);
        if (!magazine)
        {
            continue;
        }
        if (magazine->_ammo == 0)
        {
            continue;
        }
        const MagazineType* type = magazine->_type;
        int slots = GetItemSlotsCount(type->_magazineType);
        if (slots == 0)
        {
            continue;
        }
        // muzzle1
        if (muzzle1)
        {
            for (int j = 0; j < muzzle1->_magazines.Size(); j++)
            {
                if (muzzle1->_magazines[j] == type)
                {
                    if (slots1 >= slots)
                    {
                        slots1 -= slots;
                        slots = 0;
                    }
                    else
                    {
                        slots -= slots1;
                        slots1 = 0;
                        if (slots3 >= slots)
                        {
                            slots3 -= slots;
                            slots = 0;
                        }
                        else
                        {
                            slots -= slots3;
                            slots3 = 0;
                            if (slots2 >= slots)
                            {
                                slots2 -= slots;
                                slots = 0;
                            }
                            else
                            {
                                return; // no empty slots
                            }
                        }
                    }
                }
            }
        }
        if (slots == 0)
        {
            continue;
        }
        // muzzle2
        if (muzzle2)
        {
            for (int j = 0; j < muzzle2->_magazines.Size(); j++)
            {
                if (muzzle2->_magazines[j] == type)
                {
                    if (slots2 >= slots)
                    {
                        slots2 -= slots;
                        slots = 0;
                    }
                    else
                    {
                        slots -= slots2;
                        slots2 = 0;
                        if (slots3 >= slots)
                        {
                            slots3 -= slots;
                            slots = 0;
                        }
                        else
                        {
                            slots -= slots3;
                            slots3 = 0;
                            if (slots1 >= slots)
                            {
                                slots1 -= slots;
                                slots = 0;
                            }
                            else
                            {
                                return; // no empty slots
                            }
                        }
                    }
                }
            }
        }
        if (slots == 0)
        {
            continue;
        }
        // other
        if (slots3 >= slots)
        {
            slots3 -= slots;
            slots = 0;
        }
        else
        {
            slots -= slots3;
            slots3 = 0;
            if (slots2 >= slots)
            {
                slots2 -= slots;
                slots = 0;
            }
            else
            {
                slots -= slots2;
                slots2 = 0;
                if (slots1 >= slots)
                {
                    slots1 -= slots;
                    slots = 0;
                }
                else
                {
                    return; // no empty slots
                }
            }
        }
    }
}

EntityAI* Person::GetFlagCarrier()
{
    return nullptr;
}

void Person::SetFlagCarrier(EntityAI* veh) {}

void Person::CatchLadder(Building* obj, int ladder, bool up) {}

void Person::DropLadder(Building* obj, int ladder) {}

bool Person::IsOnLadder(Building* obj, int ladder) const
{
    return false;
}

LSError Person::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar))
    if (IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
    {
        PARAM_CHECK(SerializeIdentity(ar))
    }
    else
    {
        // get / set brain from AI structure
        PARAM_CHECK(ar.SerializeRef("Brain", _brain, 1))
        PARAM_CHECK(ar.Serialize("sensorRowID", _sensorRowID, 1))
    }
    return LSOK;
}

LSError Person::SerializeIdentity(ParamArchive& ar)
{
    PARAM_CHECK(ar.Serialize("Info", _info, 1))
    if (ar.IsSaving())
    {
        float skill = _brain ? _brain->GetAbility() : 1.0;
        PARAM_CHECK(ar.Serialize("skill", skill, 1, 1.0))
    }
    else if (ar.GetPass() == ParamArchive::PassSecond)
    {
        ar.FirstPass();
        if (_brain)
        {
            float skill;
            PARAM_CHECK(ar.Serialize("skill", skill, 1, 1.0))
            _brain->SetAbility(skill);
        }
        ar.SecondPass();

        if (_info._face.GetLength() > 0)
        {
            SetFace(_info._face);
        }

        if (_info._glasses.GetLength() > 0)
        {
            SetGlasses(_info._glasses);
        }

        if (_info._speaker.GetLength() > 0 && _brain)
        {
            _brain->SetSpeaker(_info._speaker, _info._pitch);
        }
    }

    return LSOK;
}

NetworkMessageType Person::GetNMType(NetworkMessageClass cls) const
{
    switch (cls)
    {
        case NMCUpdateGeneric:
            return NMTUpdateVehicleBrain;
        default:
            return base::GetNMType(cls);
    }
}

DEFINE_NET_INDICES_EX_ERR(UpdateVehicleBrain, UpdateVehicleSupply, UPDATE_VEHICLE_BRAIN_MSG)

} // namespace Poseidon

DEFINE_GET_INDICES(UpdateVehicleBrain)

namespace Poseidon
{

NetworkMessageFormat& Person::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    switch (cls)
    {
        case NMCUpdateGeneric:
            base::CreateFormat(cls, format);
            UPDATE_VEHICLE_BRAIN_MSG(MSG_FORMAT_ERR)
            break;
        default:
            base::CreateFormat(cls, format);
            break;
    }
    return format;
}

TMError Person::TransferMsg(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
            TMCHECK(base::TransferMsg(ctx))
            {
                PoseidonAssert(dynamic_cast<const IndicesUpdateVehicleBrain*>(ctx.GetIndices()))
                    const IndicesUpdateVehicleBrain* indices =
                        static_cast<const IndicesUpdateVehicleBrain*>(ctx.GetIndices());
                if (ctx.IsSending())
                {
                    if (this == GWorld->GetRealPlayer())
                    {
                        _remotePlayer = GetNetworkManager().GetPlayer();
                    }
                }
                ITRANSF(remotePlayer)
            }
            break;
        default:
            return base::TransferMsg(ctx);
    }
    return TMOK;
}

float Person::CalculateError(NetworkMessageContext& ctx)
{
    float error = 0;
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
            error += base::CalculateError(ctx);
            {
                PoseidonAssert(dynamic_cast<const IndicesUpdateVehicleBrain*>(ctx.GetIndices()))
                    const IndicesUpdateVehicleBrain* indices =
                        static_cast<const IndicesUpdateVehicleBrain*>(ctx.GetIndices());
                ICALCERR_NEQ(int, remotePlayer, ERR_COEF_MODE)
            }
            break;
        default:
            error += base::CalculateError(ctx);
            break;
    }
    return error;
}

bool Person::IsNetworkPlayer() const
{
    return _remotePlayer != 1 || this == GWorld->GetRealPlayer();
}

void Person::KilledBy(EntityAI* owner) {}
} // namespace Poseidon

namespace Poseidon::Foundation
{
template class Ref<LightReflectorOnVehicle>;
} // namespace Poseidon::Foundation
