#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/Config.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Core/ModSystem.hpp>
#include <Poseidon/Network/NetworkImpl.hpp>
#include <Poseidon/Core/Global.hpp>
// #include "strIncl.hpp"
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/UI/Locale/Languages.hpp>

#include <Poseidon/AI/ArcadeTemplate.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/Network/XML/Xml.hpp>
#include <Poseidon/Foundation/Strings/StrFormat.hpp>

#include <Poseidon/Foundation/Platform/GamePaths.hpp>
#include <Poseidon/Foundation/Algorithms/Qsort.hpp>

#include <Poseidon/World/Entities/Vehicles/AllAIVehicles.hpp>
#include <Poseidon/World/Entities/Vehicles/SeaGull.hpp>
#include <Poseidon/World/Detection/Detector.hpp>
#include <Poseidon/World/Entities/Weapons/Shots.hpp>

#include <Poseidon/Dev/Debug/DebugTrap.hpp>

#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/IO/PackFiles.hpp>
#include <Poseidon/IO/FileServer.hpp>
#include <Poseidon/IO/Filesystem/FileOps.hpp>

#include <Random/randomGen.hpp>
#include <Poseidon/Game/Commands/GameStateExt.hpp>

#include <Poseidon/Core/Progress.hpp>

#include <Poseidon/Game/UiActions.hpp>

#include <Poseidon/Foundation/Algorithms/Crc.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

#ifdef _WIN32
#include <io.h>
#endif

#include <Poseidon/World/Scene/Camera/Camera.hpp>

#include <Poseidon/Foundation/Strings/Mbcs.hpp>

using Poseidon::Foundation::Time;

// Use private memory heap for system messages
#define USE_PRIVATE_HEAP 1

using namespace Poseidon;

DEFINE_NET_MESSAGE(AskForDammage, ASK_FOR_DAMMAGE_MSG)

DEFINE_NET_MESSAGE(AskForSetDammage, ASK_FOR_SET_DAMMAGE_MSG)

DEFINE_NET_MESSAGE(AskForGetIn, ASK_FOR_GET_IN_MSG)

DEFINE_NET_MESSAGE(AskForGetOut, ASK_FOR_GET_OUT_MSG)

DEFINE_NET_MESSAGE(AskForChangePosition, ASK_FOR_CHANGE_POSITION_MSG)

DEFINE_NET_MESSAGE(AskForAimWeapon, ASK_FOR_AIM_WEAPON_MSG)

DEFINE_NET_MESSAGE(AskForAimObserver, ASK_FOR_AIM_OBSERVER_MSG)

DEFINE_NET_MESSAGE(AskForSelectWeapon, ASK_FOR_SELECT_WEAPON_MSG)

IndicesAskForAddImpulse::IndicesAskForAddImpulse()
{
    vehicle = -1;
    force = -1;
    torque = -1;
}

void IndicesAskForAddImpulse::Scan(NetworkMessageFormatBase* format){SCAN(vehicle) SCAN(force) SCAN(torque)}

// Create network message indices for AskForAddImpulseMessage class
NetworkMessageIndices* GetIndicesAskForAddImpulse()
{
    return new IndicesAskForAddImpulse();
}

NetworkMessageFormat& AskForAddImpulseMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("vehicle", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Vehicle impulse is applied to"));
    format.Add("force", NDTVector, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Applied force"));
    format.Add("torque", NDTVector, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Applied torque"));
    return format;
}

TMError AskForAddImpulseMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesAskForAddImpulse*>(ctx.GetIndices()))
    const IndicesAskForAddImpulse* indices = static_cast<const IndicesAskForAddImpulse*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransferRef(indices->vehicle, vehicle))
    TMCHECK(ctx.IdxTransfer(indices->force, force))
    TMCHECK(ctx.IdxTransfer(indices->torque, torque))
    return TMOK;
}

IndicesAskForMoveVector::IndicesAskForMoveVector()
{
    vehicle = -1;
    pos = -1;
}

void IndicesAskForMoveVector::Scan(NetworkMessageFormatBase* format){SCAN(vehicle) SCAN(pos)}

// Create network message indices for AskForMoveVectorMessage class
NetworkMessageIndices* GetIndicesAskForMoveVector()
{
    return new IndicesAskForMoveVector();
}

NetworkMessageFormat& AskForMoveVectorMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("vehicle", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Moving object"));
    format.Add("pos", NDTVector, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("New position"));
    return format;
}

TMError AskForMoveVectorMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesAskForMoveVector*>(ctx.GetIndices()))
    const IndicesAskForMoveVector* indices = static_cast<const IndicesAskForMoveVector*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransferRef(indices->vehicle, vehicle))
    TMCHECK(ctx.IdxTransfer(indices->pos, pos))
    return TMOK;
}

IndicesAskForMoveMatrix::IndicesAskForMoveMatrix()
{
    vehicle = -1;
    pos = -1;
    orient = -1;
}

void IndicesAskForMoveMatrix::Scan(NetworkMessageFormatBase* format){SCAN(vehicle) SCAN(pos) SCAN(orient)}

// Create network message indices for AskForMoveMatrixMessage class
NetworkMessageIndices* GetIndicesAskForMoveMatrix()
{
    return new IndicesAskForMoveMatrix();
}

NetworkMessageFormat& AskForMoveMatrixMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("vehicle", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Moving object"));
    format.Add("pos", NDTVector, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("New position"));
    format.Add("orient", NDTMatrix, NCTNone, DEFVALUE(Matrix3, M3Identity), DOC_MSG("New orientation"));
    return format;
}

TMError AskForMoveMatrixMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesAskForMoveMatrix*>(ctx.GetIndices()))
    const IndicesAskForMoveMatrix* indices = static_cast<const IndicesAskForMoveMatrix*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransferRef(indices->vehicle, vehicle))
    TMCHECK(ctx.IdxTransfer(indices->pos, pos))
    TMCHECK(ctx.IdxTransfer(indices->orient, orient))
    return TMOK;
}

IndicesAskForJoinGroup::IndicesAskForJoinGroup()
{
    join = -1;
    group = -1;
}

void IndicesAskForJoinGroup::Scan(NetworkMessageFormatBase* format){SCAN(join) SCAN(group)}

// Create network message indices for AskForJoinGroupMessage class
NetworkMessageIndices* GetIndicesAskForJoinGroup()
{
    return new IndicesAskForJoinGroup();
}

NetworkMessageFormat& AskForJoinGroupMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("join", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Joined group"));
    format.Add("group", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Joining group"));
    return format;
}

TMError AskForJoinGroupMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesAskForJoinGroup*>(ctx.GetIndices()))
    const IndicesAskForJoinGroup* indices = static_cast<const IndicesAskForJoinGroup*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransferRef(indices->join, join))
    TMCHECK(ctx.IdxTransferRef(indices->group, group))
    return TMOK;
}

IndicesAskForJoinUnits::IndicesAskForJoinUnits()
{
    join = -1;
    units = -1;
}

void IndicesAskForJoinUnits::Scan(NetworkMessageFormatBase* format){SCAN(join) SCAN(units)}

// Create network message indices for AskForMoveMatrixMessage class
NetworkMessageIndices* GetIndicesAskForJoinUnits()
{
    return new IndicesAskForJoinUnits();
}

NetworkMessageFormat& AskForJoinUnitsMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("join", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Joined group"));
    format.Add("units", NDTRefArray, NCTNone, DEFVALUEREFARRAY, DOC_MSG("Joining units"));
    return format;
}

TMError AskForJoinUnitsMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesAskForJoinUnits*>(ctx.GetIndices()))
    const IndicesAskForJoinUnits* indices = static_cast<const IndicesAskForJoinUnits*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransferRef(indices->join, join))
    TMCHECK(ctx.IdxTransferRefs(indices->units, units))
    return TMOK;
}

IndicesAskForHideBody::IndicesAskForHideBody()
{
    vehicle = -1;
}

void IndicesAskForHideBody::Scan(NetworkMessageFormatBase* format){SCAN(vehicle)}

// Create network message indices for AskForHideBodyMessage class
NetworkMessageIndices* GetIndicesAskForHideBody()
{
    return new IndicesAskForHideBody();
}

NetworkMessageFormat& AskForHideBodyMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("vehicle", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Body to hide"));
    return format;
}

TMError AskForHideBodyMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesAskForHideBody*>(ctx.GetIndices()))
    const IndicesAskForHideBody* indices = static_cast<const IndicesAskForHideBody*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransferRef(indices->vehicle, vehicle))
    return TMOK;
}

// network message indices for ExplosionDammageEffectsMessage class
class IndicesExplosionDammageEffects : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int owner;
    int shot;
    int directHit;
    int pos;
    int dir;
    int type;
    int enemyDammage;

    IndicesExplosionDammageEffects();
    NetworkMessageIndices* Clone() const override { return new IndicesExplosionDammageEffects; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesExplosionDammageEffects::IndicesExplosionDammageEffects()
{
    owner = -1;
    shot = -1;
    directHit = -1;
    pos = -1;
    dir = -1;
    type = -1;
    enemyDammage = -1;
}

void IndicesExplosionDammageEffects::Scan(NetworkMessageFormatBase* format){
    SCAN(owner) SCAN(shot) SCAN(directHit) SCAN(pos) SCAN(dir) SCAN(type) SCAN(enemyDammage)}

// Create network message indices for ExplosionDammageEffectsMessage class
NetworkMessageIndices* GetIndicesExplosionDammageEffects()
{
    return new IndicesExplosionDammageEffects();
}

NetworkMessageFormat& ExplosionDammageEffectsMessage::CreateFormat(NetworkMessageClass cls,
                                                                   NetworkMessageFormat& format)
{
    format.Add("owner", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Shot owner (who is responsible for explosion)"));
    format.Add("shot", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Shot"));
    format.Add("directHit", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Hitted object"));
    format.Add("pos", NDTVector, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Explosion position"));
    format.Add("dir", NDTVector, NCTNone, DEFVALUE(Vector3, VForward), DOC_MSG("Explosion direction"));
    format.Add("type", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Ammunition type"));
    format.Add("enemyDammage", NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Some enemy was damaged"));
    return format;
}

TMError ExplosionDammageEffectsMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesExplosionDammageEffects*>(ctx.GetIndices()))
    const IndicesExplosionDammageEffects* indices =
        static_cast<const IndicesExplosionDammageEffects*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransferRef(indices->owner, owner))
    TMCHECK(ctx.IdxTransferRef(indices->shot, shot))
    TMCHECK(ctx.IdxTransferRef(indices->directHit, directHit))
    TMCHECK(ctx.IdxTransfer(indices->pos, pos))
    TMCHECK(ctx.IdxTransfer(indices->dir, dir))
    TMCHECK(ctx.IdxTransfer(indices->type, type))
    TMCHECK(ctx.IdxTransfer(indices->enemyDammage, enemyDammage))
    return TMOK;
}

DEFINE_NET_MESSAGE(DeleteObject, DELETE_OBJECT_MSG)

DEFINE_NET_MESSAGE(DeleteCommand, DELETE_COMMAND_MSG)

IndicesAskForAmmo::IndicesAskForAmmo()
{
    vehicle = -1;
    weapon = -1;
    burst = -1;
}

void IndicesAskForAmmo::Scan(NetworkMessageFormatBase* format){SCAN(vehicle) SCAN(weapon) SCAN(burst)}

// Create network message indices for AskForAmmoMessage class
NetworkMessageIndices* GetIndicesAskForAmmo()
{
    return new IndicesAskForAmmo();
}

NetworkMessageFormat& AskForAmmoMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("vehicle", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Vehicle which ammo is changing"));
    format.Add("weapon", NDTInteger, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Weapon index"));
    format.Add("burst", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 1), DOC_MSG("Amount of ammo to decrease"));
    return format;
}

TMError AskForAmmoMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesAskForAmmo*>(ctx.GetIndices()))
    const IndicesAskForAmmo* indices = static_cast<const IndicesAskForAmmo*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransferRef(indices->vehicle, vehicle))
    TMCHECK(ctx.IdxTransfer(indices->weapon, weapon))
    TMCHECK(ctx.IdxTransfer(indices->burst, burst))
    return TMOK;
}

// network message indices for FireWeaponMessage class
class IndicesFireWeapon : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int vehicle;
    int target;
    int weapon;
    int magazineCreator;
    int magazineId;

    IndicesFireWeapon();
    NetworkMessageIndices* Clone() const override { return new IndicesFireWeapon; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesFireWeapon::IndicesFireWeapon()
{
    vehicle = -1;
    target = -1;
    weapon = -1;
    magazineCreator = -1;
    magazineId = -1;
}

void IndicesFireWeapon::Scan(NetworkMessageFormatBase* format){SCAN(vehicle) SCAN(target) SCAN(weapon)
                                                                   SCAN(magazineCreator) SCAN(magazineId)}

// Create network message indices for FireWeaponMessage class
NetworkMessageIndices* GetIndicesFireWeapon()
{
    return new IndicesFireWeapon();
}

NetworkMessageFormat& FireWeaponMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("vehicle", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Firing vehicle"));
    format.Add("target", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Aimed target"));
    format.Add("weapon", NDTInteger, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Firing weapon index"));
    format.Add("magazineCreator", NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("Fired magazine id"));
    format.Add("magazineId", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Fired magazine id"));
    return format;
}

TMError FireWeaponMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesFireWeapon*>(ctx.GetIndices()))
    const IndicesFireWeapon* indices = static_cast<const IndicesFireWeapon*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransferRef(indices->vehicle, vehicle))
    TMCHECK(ctx.IdxTransferRef(indices->target, target))
    TMCHECK(ctx.IdxTransfer(indices->weapon, weapon))
    TMCHECK(ctx.IdxTransfer(indices->magazineCreator, magazineCreator))
    TMCHECK(ctx.IdxTransfer(indices->magazineId, magazineId))
    return TMOK;
}

IndicesUpdateWeapons::IndicesUpdateWeapons()
{
    vehicle = -1;
    IndicesUpdateEntityAIWeapons* GetIndicesUpdateEntityAIWeapons();
    weapons = GetIndicesUpdateEntityAIWeapons();
}

IndicesUpdateWeapons::~IndicesUpdateWeapons()
{
    void DeleteIndicesUpdateEntityAIWeapons(IndicesUpdateEntityAIWeapons * weapons);
    DeleteIndicesUpdateEntityAIWeapons(weapons);
}

void IndicesUpdateWeapons::Scan(NetworkMessageFormatBase* format)
{
    SCAN(vehicle)
    void ScanIndicesUpdateEntityAIWeapons(IndicesUpdateEntityAIWeapons * weapons, NetworkMessageFormatBase * format);
    ScanIndicesUpdateEntityAIWeapons(weapons, format);
}

// Create network message indices for UpdateWeaponsMessage class
NetworkMessageIndices* GetIndicesUpdateWeapons()
{
    return new IndicesUpdateWeapons();
}

NetworkMessageFormat& UpdateWeaponsMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("vehicle", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Vehicle to update"));
    EntityAI::CreateFormatWeapons(format);
    return format;
}

TMError UpdateWeaponsMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesUpdateWeapons*>(ctx.GetIndices()))
    const IndicesUpdateWeapons* indices = static_cast<const IndicesUpdateWeapons*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransferRef(indices->vehicle, vehicle))
    if (vehicle)
        TMCHECK(vehicle->TransferMsgWeapons(ctx, indices->weapons))
    return TMOK;
}

// network message indices for AddWeaponCargoMessage class
class IndicesAddWeaponCargo : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int vehicle;
    int weapon;

    IndicesAddWeaponCargo();
    NetworkMessageIndices* Clone() const override { return new IndicesAddWeaponCargo; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesAddWeaponCargo::IndicesAddWeaponCargo()
{
    vehicle = -1;
    weapon = -1;
}

void IndicesAddWeaponCargo::Scan(NetworkMessageFormatBase* format){SCAN(vehicle) SCAN(weapon)}

// Create network message indices for AddWeaponCargoMessage class
NetworkMessageIndices* GetIndicesAddWeaponCargo()
{
    return new IndicesAddWeaponCargo();
}

NetworkMessageFormat& AddWeaponCargoMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("vehicle", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Asked vehicle"));
    format.Add("weapon", NDTString, NCTDefault, DEFVALUE(RString, ""), DOC_MSG("Name of weapon type to add"));
    return format;
}

TMError AddWeaponCargoMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesAddWeaponCargo*>(ctx.GetIndices()))
    const IndicesAddWeaponCargo* indices = static_cast<const IndicesAddWeaponCargo*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransferRef(indices->vehicle, vehicle))
    TMCHECK(ctx.IdxTransfer(indices->weapon, weapon))
    return TMOK;
}

// network message indices for RemoveWeaponCargoMessage class
class IndicesRemoveWeaponCargo : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int vehicle;
    int weapon;

    IndicesRemoveWeaponCargo();
    NetworkMessageIndices* Clone() const override { return new IndicesRemoveWeaponCargo; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesRemoveWeaponCargo::IndicesRemoveWeaponCargo()
{
    vehicle = -1;
    weapon = -1;
}

void IndicesRemoveWeaponCargo::Scan(NetworkMessageFormatBase* format){SCAN(vehicle) SCAN(weapon)}

// Create network message indices for RemoveWeaponCargoMessage class
NetworkMessageIndices* GetIndicesRemoveWeaponCargo()
{
    return new IndicesRemoveWeaponCargo();
}

NetworkMessageFormat& RemoveWeaponCargoMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("vehicle", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Asked vehicle"));
    format.Add("weapon", NDTString, NCTDefault, DEFVALUE(RString, ""), DOC_MSG("Name of weapon type to remove"));
    return format;
}

TMError RemoveWeaponCargoMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesRemoveWeaponCargo*>(ctx.GetIndices()))
    const IndicesRemoveWeaponCargo* indices = static_cast<const IndicesRemoveWeaponCargo*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransferRef(indices->vehicle, vehicle))
    TMCHECK(ctx.IdxTransfer(indices->weapon, weapon))
    return TMOK;
}

// network message indices for AddMagazineCargoMessage class
class IndicesAddMagazineCargo : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int vehicle;
    int magazine;

    IndicesAddMagazineCargo();
    NetworkMessageIndices* Clone() const override { return new IndicesAddMagazineCargo; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesAddMagazineCargo::IndicesAddMagazineCargo()
{
    vehicle = -1;
    magazine = -1;
}

void IndicesAddMagazineCargo::Scan(NetworkMessageFormatBase* format){SCAN(vehicle) SCAN(magazine)}

// Create network message indices for AddMagazineCargoMessage class
NetworkMessageIndices* GetIndicesAddMagazineCargo()
{
    return new IndicesAddMagazineCargo();
}

NetworkMessageFormat& AddMagazineCargoMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("vehicle", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Asked vehicle"));
    format.Add("magazine", NDTObject, NCTNone, DEFVALUE_MSG(NMTMagazine), DOC_MSG("Magazine to add"));
    return format;
}

TMError AddMagazineCargoMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesAddMagazineCargo*>(ctx.GetIndices()))
    const IndicesAddMagazineCargo* indices = static_cast<const IndicesAddMagazineCargo*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransferRef(indices->vehicle, vehicle))
    TMCHECK(ctx.IdxTransferContent(indices->magazine, magazine))

    // IdxTransferObject not implemented for Ref<Type>
    /*
    if (ctx.IsSending())
    {
        NET_ERROR(magazine);
        TMCHECK(ctx.IdxTransferObject(indices->magazine, *magazine))
    }
    else
    {
        magazine = Magazine::CreateObject(ctx);
        TMCHECK(ctx.IdxTransferObject(indices->magazine, *magazine))
    }
    */
    return TMOK;
}

// network message indices for RemoveMagazineCargoMessage class
class IndicesRemoveMagazineCargo : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int vehicle;
    int creator;
    int id;

    IndicesRemoveMagazineCargo();
    NetworkMessageIndices* Clone() const override { return new IndicesRemoveMagazineCargo; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesRemoveMagazineCargo::IndicesRemoveMagazineCargo()
{
    vehicle = -1;
    creator = -1;
    id = -1;
}

void IndicesRemoveMagazineCargo::Scan(NetworkMessageFormatBase* format){SCAN(vehicle) SCAN(creator) SCAN(id)}

// Create network message indices for RemoveMagazineCargoMessage class
NetworkMessageIndices* GetIndicesRemoveMagazineCargo()
{
    return new IndicesRemoveMagazineCargo();
}

NetworkMessageFormat& RemoveMagazineCargoMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("vehicle", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Asked vehicle"));
    format.Add("creator", NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("ID of magazine to remove"));
    format.Add("id", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("ID of magazine to remove"));
    return format;
}

TMError RemoveMagazineCargoMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesRemoveMagazineCargo*>(ctx.GetIndices()))
    const IndicesRemoveMagazineCargo* indices = static_cast<const IndicesRemoveMagazineCargo*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransferRef(indices->vehicle, vehicle))
    TMCHECK(ctx.IdxTransfer(indices->creator, creator))
    TMCHECK(ctx.IdxTransfer(indices->id, id))
    return TMOK;
}

NetworkMessageType VehicleInitCmd::GetNMType(NetworkMessageClass cls) const
{
    return NMTVehicleInit;
}

// network message indices for VehicleInitCmd class
class IndicesVehicleInit : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int vehicle;
    int init;

    IndicesVehicleInit();
    NetworkMessageIndices* Clone() const override { return new IndicesVehicleInit; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesVehicleInit::IndicesVehicleInit()
{
    vehicle = -1;
    init = -1;
}

void IndicesVehicleInit::Scan(NetworkMessageFormatBase* format){SCAN(vehicle) SCAN(init)}

// Create network message indices for VehicleInitCmd class
NetworkMessageIndices* GetIndicesVehicleInit()
{
    return new IndicesVehicleInit();
}

NetworkMessageFormat& VehicleInitCmd::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("vehicle", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Vehicle which is initialized"));
    format.Add("init", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Initialization statement"));
    return format;
}

TMError VehicleInitCmd::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesVehicleInit*>(ctx.GetIndices()))
    const IndicesVehicleInit* indices = static_cast<const IndicesVehicleInit*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransferRef(indices->vehicle, vehicle))
    TMCHECK(ctx.IdxTransfer(indices->init, init))
    return TMOK;
}

// network message indices for VehicleDestroyedMessage class
class IndicesVehicleDestroyed : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int killed;
    int killer;

    IndicesVehicleDestroyed();
    NetworkMessageIndices* Clone() const override { return new IndicesVehicleDestroyed; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesVehicleDestroyed::IndicesVehicleDestroyed()
{
    killed = -1;
    killer = -1;
}

void IndicesVehicleDestroyed::Scan(NetworkMessageFormatBase* format){SCAN(killed) SCAN(killer)}

// Create network message indices for VehicleDestroyedMessage class
NetworkMessageIndices* GetIndicesVehicleDestroyed()
{
    return new IndicesVehicleDestroyed();
}

NetworkMessageFormat& VehicleDestroyedMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("killed", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Destroyed vehicle"));
    format.Add("killer", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Who is responsible for destroying"));
    return format;
}

TMError VehicleDestroyedMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesVehicleDestroyed*>(ctx.GetIndices()))
    const IndicesVehicleDestroyed* indices = static_cast<const IndicesVehicleDestroyed*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransferRef(indices->killed, killed))
    TMCHECK(ctx.IdxTransferRef(indices->killer, killer))
    return TMOK;
}

// network message indices for MarkerDeleteMessage class
class IndicesMarkerDelete : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int name;

    IndicesMarkerDelete();
    NetworkMessageIndices* Clone() const override { return new IndicesMarkerDelete; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesMarkerDelete::IndicesMarkerDelete()
{
    name = -1;
}

void IndicesMarkerDelete::Scan(NetworkMessageFormatBase* format){SCAN(name)}

// Create network message indices for MarkerDeleteMessage class
NetworkMessageIndices* GetIndicesMarkerDelete()
{
    return new IndicesMarkerDelete();
}

NetworkMessageFormat& MarkerDeleteMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("name", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Name of marker"));
    return format;
}

TMError MarkerDeleteMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesMarkerDelete*>(ctx.GetIndices()))
    const IndicesMarkerDelete* indices = static_cast<const IndicesMarkerDelete*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransfer(indices->name, name))
    return TMOK;
}

IndicesMarkerCreate::IndicesMarkerCreate()
{
    channel = -1;
    sender = -1;
    units = -1;

    IndicesMarker* GetIndicesMarker();
    marker = GetIndicesMarker();
}

IndicesMarkerCreate::~IndicesMarkerCreate()
{
    void DeleteIndicesMarker(IndicesMarker * marker);
    DeleteIndicesMarker(marker);
}

void IndicesMarkerCreate::Scan(NetworkMessageFormatBase* format)
{
    SCAN(channel)
    SCAN(sender)
    SCAN(units)

    void ScanIndicesMarker(IndicesMarker * marker, NetworkMessageFormatBase * format);
    ScanIndicesMarker(marker, format);
}

// Create network message indices for MarkerCreateMessage class
NetworkMessageIndices* GetIndicesMarkerCreate()
{
    return new IndicesMarkerCreate();
}

NetworkMessageFormat& MarkerCreateMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("channel", NDTInteger, NCTSmallSigned, DEFVALUE(int, 0),
               DOC_MSG("Chat channel (who will see the marker)"));
    format.Add("sender", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Sender unit"));
    format.Add("units", NDTRefArray, NCTNone, DEFVALUEREFARRAY, DOC_MSG("List of receiving units"));
    ArcadeMarkerInfo::CreateFormat(format);
    return format;
}

TMError MarkerCreateMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesMarkerCreate*>(ctx.GetIndices()))
    const IndicesMarkerCreate* indices = static_cast<const IndicesMarkerCreate*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransfer(indices->channel, channel))
    TMCHECK(ctx.IdxTransferRef(indices->sender, sender))
    TMCHECK(ctx.IdxTransferRefs(indices->units, units))
    return marker.TransferMsg(ctx, indices->marker);
}

// network message indices for NetworkCommandMessage class
class IndicesNetworkCommand : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int type;
    int content;

    IndicesNetworkCommand();
    NetworkMessageIndices* Clone() const override { return new IndicesNetworkCommand; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesNetworkCommand::IndicesNetworkCommand()
{
    type = -1;
    content = -1;
}

void IndicesNetworkCommand::Scan(NetworkMessageFormatBase* format){SCAN(type) SCAN(content)}

// Create network message indices for NetworkCommandMessage class
NetworkMessageIndices* GetIndicesNetworkCommand()
{
    return new IndicesNetworkCommand();
}

NetworkMessageFormat& NetworkCommandMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("type", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Type of command"));
    format.Add("content", NDTRawData, NCTNone, DEFVALUERAWDATA, DOC_MSG("Parameters of command"));
    return format;
}

TMError NetworkCommandMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesNetworkCommand*>(ctx.GetIndices()))
    const IndicesNetworkCommand* indices = static_cast<const IndicesNetworkCommand*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransfer(indices->type, (int&)type))
    TMCHECK(ctx.IdxTransfer(indices->content, content))
    return TMOK;
}

// network message indices for IntegrityQuestionMessage class
class IndicesIntegrityQuestion : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int id;
    int type;
    int name;
    int offset;
    int size;

    IndicesIntegrityQuestion();
    NetworkMessageIndices* Clone() const override { return new IndicesIntegrityQuestion; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesIntegrityQuestion::IndicesIntegrityQuestion()
{
    id = -1;
    type = -1;
    name = -1;
    offset = -1;
    size = -1;
}

void IndicesIntegrityQuestion::Scan(NetworkMessageFormatBase* format){SCAN(id) SCAN(type) SCAN(name) SCAN(offset)
                                                                          SCAN(size)}

// Create network message indices for IntegrityQuestionMessage class
NetworkMessageIndices* GetIndicesIntegrityQuestion()
{
    return new IndicesIntegrityQuestion();
}

NetworkMessageFormat& IntegrityQuestionMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("id", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Unique id of question"));
    format.Add("type", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Type of question"));
    format.Add("name", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Question name"));
    format.Add("offset", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Region in question"));
    format.Add("size", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Region in question"));
    return format;
}

TMError IntegrityQuestionMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesIntegrityQuestion*>(ctx.GetIndices()))
    const IndicesIntegrityQuestion* indices = static_cast<const IndicesIntegrityQuestion*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransfer(indices->id, id))
    TMCHECK(ctx.IdxTransfer(indices->type, (int&)type))
    TMCHECK(ctx.IdxTransfer(indices->name, q.name))
    TMCHECK(ctx.IdxTransfer(indices->offset, q.offset))
    TMCHECK(ctx.IdxTransfer(indices->size, q.size))
    return TMOK;
}

// network message indices for IntegrityAnswerMessage class
class IndicesIntegrityAnswer : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int answer;
    int id;
    int type;

    IndicesIntegrityAnswer();
    NetworkMessageIndices* Clone() const override { return new IndicesIntegrityAnswer; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesIntegrityAnswer::IndicesIntegrityAnswer()
{
    answer = -1;
    id = -1;
    type = -1;
}

void IndicesIntegrityAnswer::Scan(NetworkMessageFormatBase* format){SCAN(answer) SCAN(id) SCAN(type)}

// Create network message indices for IntegrityAnswerMessage class
NetworkMessageIndices* GetIndicesIntegrityAnswer()
{
    return new IndicesIntegrityAnswer();
}

NetworkMessageFormat& IntegrityAnswerMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("id", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Unique id of question"));
    format.Add("type", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Type of question"));
    format.Add("answer", NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("Answer value (CRC of selected data)"));
    return format;
}

TMError IntegrityAnswerMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesIntegrityAnswer*>(ctx.GetIndices()))
    const IndicesIntegrityAnswer* indices = static_cast<const IndicesIntegrityAnswer*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransfer(indices->id, id))
    TMCHECK(ctx.IdxTransfer(indices->type, (int&)type))
    TMCHECK(ctx.IdxTransfer(indices->answer, answer))
    return TMOK;
}

// network message indices for PlayerStateMessage class
class IndicesPlayerState : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int player;
    int state;

    IndicesPlayerState();
    NetworkMessageIndices* Clone() const override { return new IndicesPlayerState; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesPlayerState::IndicesPlayerState()
{
    player = -1;
    state = -1;
}

void IndicesPlayerState::Scan(NetworkMessageFormatBase* format){SCAN(player) SCAN(state)}

// Create network message indices for PlayerStateMessage class
NetworkMessageIndices* GetIndicesPlayerState()
{
    return new IndicesPlayerState();
}

NetworkMessageFormat& PlayerStateMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("player", NDTInteger, NCTNone, DEFVALUE(int, AI_PLAYER), DOC_MSG("Client (player) ID"));
    format.Add("state", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, NGSNone), DOC_MSG("New state of player"));
    return format;
}

TMError PlayerStateMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesPlayerState*>(ctx.GetIndices()))
    const IndicesPlayerState* indices = static_cast<const IndicesPlayerState*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransfer(indices->player, (int&)player))
    TMCHECK(ctx.IdxTransfer(indices->state, (int&)state))
    return TMOK;
}

AttachPersonMessage::AttachPersonMessage(Person* p)
{
    NET_ERROR(p);
    person = p;
    unit = person->Brain();
}

IndicesAttachPerson::IndicesAttachPerson()
{
    person = -1;
    unit = -1;
}

void IndicesAttachPerson::Scan(NetworkMessageFormatBase* format){SCAN(person) SCAN(unit)}

// Create network message indices for AttachPersonMessage class
NetworkMessageIndices* GetIndicesAttachPerson()
{
    return new IndicesAttachPerson();
}

NetworkMessageFormat& AttachPersonMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("person", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Person to attach"));
    format.Add("unit", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Unit to attach"));
    return format;
}

TMError AttachPersonMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesAttachPerson*>(ctx.GetIndices()))
    const IndicesAttachPerson* indices = static_cast<const IndicesAttachPerson*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransferRef(indices->person, person))
    TMCHECK(ctx.IdxTransferRef(indices->unit, unit))
    return TMOK;
}
/*
NetworkMessageFormat &RespawnQueueItem::CreateFormat
(
    NetworkMessageClass cls,
    NetworkMessageFormat &format
)
{
    format.Add("type", NDTString, NCTNone, DEFVALUE(RString, ""));
    format.Add("side", NDTInteger, NCTNone, DEFVALUE(int, TSideUnknown));
    format.Add("id", NDTInteger, NCTNone, DEFVALUE(int, -1));
    format.Add("varname", NDTString, NCTNone, DEFVALUE(RString, ""));
    // info
    format.Add("firstname", NDTString, NCTNone, DEFVALUE(RString, ""));
    format.Add("name", NDTString, NCTNone, DEFVALUE(RString, ""));
    format.Add("rank", NDTInteger, NCTNone, DEFVALUE(int, RankPrivate));
    format.Add("experience", NDTFloat, NCTNone, DEFVALUE(float, 0));

    format.Add("group", NDTRef, NCTNone, DEFVALUENULL);
    format.Add("position", NDTVector, NCTNone, DEFVALUE(Vector3, VZero));
    format.Add("time", NDTTime, NCTNone, DEFVALUE(Time, Time(0)));
    format.Add("player", NDTInteger, NCTNone, DEFVALUE(int, 0));
    return format;
}

TMError RespawnQueueItem::TransferMsg(NetworkMessageContext &ctx)
{
    if (ctx.IsSending())
    {
        RString typeName = type ? type->GetName() : "";
        TMCHECK(ctx.Transfer("type", typeName))
    }
    else
    {
        RString typeName;
        TMCHECK(ctx.Transfer("type", typeName))
        if (typeName.GetLength() > 0)
            type = static_cast<const VehicleType *>(VehicleTypes.New(typeName));
        else
            type = nullptr;
    }
    TMCHECK(ctx.Transfer("side", (int &)side))
    TMCHECK(ctx.Transfer("id", id))
    TMCHECK(ctx.Transfer("varname", varname))

    TMCHECK(ctx.Transfer("firstname", info._firstname))
    TMCHECK(ctx.Transfer("name", info._name))
    TMCHECK(ctx.Transfer("rank", (int &)info._rank))
    TMCHECK(ctx.Transfer("experience", info._experience))

    TMCHECK(ctx.TransferRef("group", group))
    TMCHECK(ctx.Transfer("position", position))
    TMCHECK(ctx.Transfer("time", time))
    TMCHECK(ctx.Transfer("player", player))
    return TMOK;
}
*/

IndicesSetFlagOwner::IndicesSetFlagOwner()
{
    owner = -1;
    carrier = -1;
}

void IndicesSetFlagOwner::Scan(NetworkMessageFormatBase* format){SCAN(owner) SCAN(carrier)}

// Create network message indices for SetFlagOwnerMessage class
NetworkMessageIndices* GetIndicesSetFlagOwner()
{
    return new IndicesSetFlagOwner();
}

// Create network message indices for SetFlagCarrierMessage class
NetworkMessageIndices* GetIndicesSetFlagCarrier()
{
    return new IndicesSetFlagOwner();
}

NetworkMessageFormat& SetFlagOwnerMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("owner", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Flag owner"));
    format.Add("carrier", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Flag carrier"));
    return format;
}

TMError SetFlagOwnerMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesSetFlagOwner*>(ctx.GetIndices()))
    const IndicesSetFlagOwner* indices = static_cast<const IndicesSetFlagOwner*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransferRef(indices->owner, owner))
    TMCHECK(ctx.IdxTransferRef(indices->carrier, carrier))
    return TMOK;
}

// network message indices for PlayerIdentity class
class IndicesLogin : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int dpnid;
    int playerid;
    int id;
    int name;
    int face;
    int glasses;
    int speaker;
    int pitch;
    int squad;
    int fullname;
    int email;
    int icq;
    int remark;
    int state;
    int version;

    IndicesLogin();
    NetworkMessageIndices* Clone() const override { return new IndicesLogin; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesLogin::IndicesLogin()
{
    dpnid = -1;
    playerid = -1;
    id = -1;
    name = -1;
    face = -1;
    glasses = -1;
    speaker = -1;
    pitch = -1;
    squad = -1;
    fullname = -1;
    email = -1;
    icq = -1;
    remark = -1;
    state = -1;
    version = -1;
}

void IndicesLogin::Scan(NetworkMessageFormatBase* format){
    SCAN(dpnid) SCAN(playerid) SCAN(id) SCAN(name) SCAN(face) SCAN(glasses) SCAN(speaker) SCAN(pitch) SCAN(squad)
        SCAN(fullname) SCAN(email) SCAN(icq) SCAN(remark) SCAN(state) SCAN(version)}

IndicesPlayerUpdate::IndicesPlayerUpdate()
{
    dpnid = -1;
    minPing = -1;
    avgPing = -1;
    maxPing = -1;
    minBandwidth = -1;
    avgBandwidth = -1;
    maxBandwidth = -1;
    desync = -1;
    rights = -1;
}

void IndicesPlayerUpdate::Scan(NetworkMessageFormatBase* format)
{
    SCAN(dpnid);
    SCAN(minPing)
    SCAN(avgPing)
    SCAN(maxPing)
    SCAN(minBandwidth)
    SCAN(avgBandwidth)
    SCAN(maxBandwidth)
    SCAN(desync)
    SCAN(rights)
}

// Create network message indices for IndicesPlayerUpdate class
NetworkMessageIndices* GetIndicesLogin()
{
    return new IndicesLogin();
}

// Create network message indices for IndicesPlayerUpdate class position update
NetworkMessageIndices* GetIndicesPlayerUpdate()
{
    return new IndicesPlayerUpdate();
}

PlayerIdentity::PlayerIdentity()
{
    _minPing = 0, _avgPing = 0, _maxPing = 0;
    _minBandwidth = 0, _avgBandwidth = 0, _maxBandwidth = 0;
    _desync = 0;

    _rights = PRNone;

    destroy = false;
    failedLogin = 0;

    kickOffTime = UITIME_MAX;
    kickOffState = KOWait;
}

PlayerIdentity::~PlayerIdentity() = default;

NetworkMessageType PlayerIdentity::GetNMType(NetworkMessageClass cls) const
{
    if (cls == NMCUpdatePosition)
    {
        return NMTPlayerUpdate;
    }
    return NMTLogin;
}

NetworkMessageFormat& PlayerIdentity::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    switch (cls)
    {
        case NMCUpdatePosition:
            format.Add("dpnid", NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("Client (player) ID"));
            format.Add("minPing", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 10), DOC_MSG("Ping range estimation"),
                       ET_ABS_DIF, ERR_COEF_VALUE_MINOR);
            format.Add("avgPing", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 100), DOC_MSG("Ping range estimation"),
                       ET_ABS_DIF, ERR_COEF_VALUE_MAJOR);
            format.Add("maxPing", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 1000), DOC_MSG("Ping range estimation"),
                       ET_ABS_DIF, ERR_COEF_VALUE_MINOR);
            format.Add("minBandwidth", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 2),
                       DOC_MSG("Bandwidth estimation (in kbps)"), ET_ABS_DIF, ERR_COEF_VALUE_MINOR);
            format.Add("avgBandwidth", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 14),
                       DOC_MSG("Bandwidth estimation (in kbps)"), ET_ABS_DIF, ERR_COEF_VALUE_MINOR);
            format.Add("maxBandwidth", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 28),
                       DOC_MSG("Bandwidth estimation (in kbps)"), ET_ABS_DIF, ERR_COEF_VALUE_MINOR);
            format.Add("desync", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0),
                       DOC_MSG("Current desync level (max. error of unsent messages)"), ET_ABS_DIF,
                       ERR_COEF_VALUE_MAJOR);
            format.Add("rights", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0),
                       DOC_MSG("Special rights of given player"), ET_ABS_DIF, ERR_COEF_VALUE_MAJOR);
            return format;
        default:
            format.Add("dpnid", NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("Client (player) ID"));
            format.Add("playerid", NDTInteger, NCTNone, DEFVALUE(int, 0),
                       DOC_MSG("ID unique in session (shorter than dpnid)"));
            format.Add("id", NDTString, NCTNone, DEFVALUE(RString, ""),
                       DOC_MSG("Unique id of player (derivated from CD key)"));
            format.Add("name", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Nick (short) name of player"));
            format.Add("face", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Selected face"));
            format.Add("glasses", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Selected glasses"));
            format.Add("speaker", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Selected speaker"));
            format.Add("pitch", NDTFloat, NCTNone, DEFVALUE(float, 1.0f), DOC_MSG("Selected voice pitch"));
            format.Add("squad", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("unique id (URL) of squad"));
            format.Add("fullname", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Full name of player"));
            format.Add("email", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("E-mail of player"));
            format.Add("icq", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("ICQ of player"));
            format.Add("remark", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Remark about player"));
            format.Add("state", NDTInteger, NCTNone, DEFVALUE(int, NGSNone),
                       DOC_MSG("State of player's network client"));
            format.Add("version", NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("Version player is using"));
            return format;
    }
}

TMError PlayerIdentity::TransferMsg(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCUpdatePosition:
        {
            NET_ERROR(dynamic_cast<const IndicesPlayerUpdate*>(ctx.GetIndices()))
            const IndicesPlayerUpdate* indices = static_cast<const IndicesPlayerUpdate*>(ctx.GetIndices());

            ITRANSF(minPing);
            ITRANSF(avgPing);
            ITRANSF(maxPing);
            ITRANSF(minBandwidth);
            ITRANSF(avgBandwidth);
            ITRANSF(maxBandwidth);
            ITRANSF(desync);
            ITRANSF(rights);

            return TMOK;
        }
        default:
        {
            NET_ERROR(dynamic_cast<const IndicesLogin*>(ctx.GetIndices()))
            const IndicesLogin* indices = static_cast<const IndicesLogin*>(ctx.GetIndices());

            TMCHECK(ctx.IdxTransfer(indices->dpnid, (int&)dpnid))
            TMCHECK(ctx.IdxTransfer(indices->playerid, (int&)playerid))
            TMCHECK(ctx.IdxTransfer(indices->id, id))
            TMCHECK(ctx.IdxTransfer(indices->name, name))
            TMCHECK(ctx.IdxTransfer(indices->face, face))
            TMCHECK(ctx.IdxTransfer(indices->glasses, glasses))
            TMCHECK(ctx.IdxTransfer(indices->speaker, speaker))
            TMCHECK(ctx.IdxTransfer(indices->pitch, pitch))
            TMCHECK(ctx.IdxTransfer(indices->squad, squadId))
            TMCHECK(ctx.IdxTransfer(indices->fullname, fullname))
            TMCHECK(ctx.IdxTransfer(indices->email, email))
            TMCHECK(ctx.IdxTransfer(indices->icq, icq))
            TMCHECK(ctx.IdxTransfer(indices->remark, remark))
            TMCHECK(ctx.IdxTransfer(indices->state, (int&)state))
            TMCHECK(ctx.IdxTransfer(indices->version, version))

            // Original copy protection CD key check disabled — not applicable to CWR.
            return TMOK;
        }
    }
}

RString PlayerIdentity::GetName() const
{
    if (squad)
    {
        return name + RString(" [") + squad->nick + RString("]");
    }
    else
    {
        return name;
    }
}

// network message indices for SquadIdentity class
class IndicesSquad : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int id;
    int nick;
    int name;
    int email;
    int web;
    int picture;
    int title;

    IndicesSquad();
    NetworkMessageIndices* Clone() const override { return new IndicesSquad; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesSquad::IndicesSquad()
{
    id = -1;
    nick = -1;
    name = -1;
    email = -1;
    web = -1;
    picture = -1;
    title = -1;
}

void IndicesSquad::Scan(NetworkMessageFormatBase* format){SCAN(id) SCAN(nick) SCAN(name) SCAN(email) SCAN(web)
                                                              SCAN(picture) SCAN(title)}

// Create network message indices for SquadIdentity class
NetworkMessageIndices* GetIndicesSquad()
{
    return new IndicesSquad();
}

NetworkMessageFormat& SquadIdentity::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("id", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Unique id of squad (URL of XML page)"));
    format.Add("nick", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Nick (short) name of squad"));
    format.Add("name", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Full name of squad"));
    format.Add("email", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("E-mail of squad administrator"));
    format.Add("web", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Web page of squad"));
    format.Add("picture", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Picture of squad (shown on vehicles)"));
    format.Add("title", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Title of squad (shown on vehicles)"));
    return format;
}

TMError SquadIdentity::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesSquad*>(ctx.GetIndices()))
    const IndicesSquad* indices = static_cast<const IndicesSquad*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransfer(indices->id, id))
    TMCHECK(ctx.IdxTransfer(indices->nick, nick))
    TMCHECK(ctx.IdxTransfer(indices->name, name))
    TMCHECK(ctx.IdxTransfer(indices->email, email))
    TMCHECK(ctx.IdxTransfer(indices->web, web))
    TMCHECK(ctx.IdxTransfer(indices->picture, picture))
    TMCHECK(ctx.IdxTransfer(indices->title, title))
    return TMOK;
}

IndicesShowTarget::IndicesShowTarget()
{
    vehicle = -1;
    target = -1;
}

void IndicesShowTarget::Scan(NetworkMessageFormatBase* format){SCAN(vehicle) SCAN(target)}

// Create network message indices for ShowTargetMessage class
NetworkMessageIndices* GetIndicesShowTarget()
{
    return new IndicesShowTarget();
}

NetworkMessageFormat& ShowTargetMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("vehicle", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Player person"));
    format.Add("target", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Target to show"));
    return format;
}

TMError ShowTargetMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesShowTarget*>(ctx.GetIndices()))
    const IndicesShowTarget* indices = static_cast<const IndicesShowTarget*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransferRef(indices->vehicle, vehicle))
    TMCHECK(ctx.IdxTransferRef(indices->target, target))
    return TMOK;
}

IndicesShowGroupDir::IndicesShowGroupDir()
{
    vehicle = -1;
    dir = -1;
}

void IndicesShowGroupDir::Scan(NetworkMessageFormatBase* format){SCAN(vehicle) SCAN(dir)}

// Create network message indices for ShowGroupDirMessage class
NetworkMessageIndices* GetIndicesShowGroupDir()
{
    return new IndicesShowGroupDir();
}

NetworkMessageFormat& ShowGroupDirMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("vehicle", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Player person"));
    format.Add("dir", NDTVector, NCTNone, DEFVALUE(Vector3, VForward), DOC_MSG("Direction to show"));
    return format;
}

TMError ShowGroupDirMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesShowGroupDir*>(ctx.GetIndices()))
    const IndicesShowGroupDir* indices = static_cast<const IndicesShowGroupDir*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransferRef(indices->vehicle, vehicle))
    TMCHECK(ctx.IdxTransfer(indices->dir, dir))
    return TMOK;
}

// Declare static variable for message format
#define DECLARE_FORMAT(macro, class, name, description, group) \
    static NetworkMessageFormat items##name;                   \
    NetworkMessageIndices* GetIndices##name();

NETWORK_MESSAGE_TYPES(DECLARE_FORMAT)

// Add (create) format to static array of formats
#define FORMAT_SIMPLE(dummy, name) \
    GMsgFormats[curMsgFormat++] = name##Message::CreateFormat(NMCCreate, items##name).Init(GetIndices##name())
// Add (create) format to static array of formats
#define FORMAT_CREATE(type, format) \
    GMsgFormats[curMsgFormat++] = type::CreateFormat(NMCCreate, items##format).Init(GetIndices##format())
// Add (update) format to static array of formats
#define FORMAT_UPDATE(type, format) \
    GMsgFormats[curMsgFormat++] = type::CreateFormat(NMCUpdateGeneric, items##format).Init(GetIndices##format())
// Add (update position) format to static array of formats
#define FORMAT_UPDATE_POSITION(type, format) \
    GMsgFormats[curMsgFormat++] = type::CreateFormat(NMCUpdatePosition, items##format).Init(GetIndices##format())

// Add (update dammage) format to static array of formats
#define FORMAT_UPDATE_DAMMAGE(type, format) \
    GMsgFormats[curMsgFormat++] = type::CreateFormat(NMCUpdateDammage, items##format).Init(GetIndices##format())

// number of registered local (static) message formats
static int curMsgFormat = 0;

// local (static) message formats
NetworkMessageFormat* GMsgFormats[NMTN];

#if DOCUMENT_MSG_FORMATS

#define NDT_DEFINE_ENUM_NAME(type, name, description) #name,

static const char* ItemTypeNames[] = {NETWORK_DATA_TYPES(NDT_DEFINE_ENUM_NAME)};

#define NDT_ENUM_DESCRIPTION(type, name, description) description,

static const char* ItemTypeDescriptions[] = {NETWORK_DATA_TYPES(NDT_ENUM_DESCRIPTION)};

#define NMT_ENUM_DESCRIPTION(macro, class, name, description, group) description,

static const char* MessageTypeDescriptions[] = {NETWORK_MESSAGE_TYPES(NMT_ENUM_DESCRIPTION)};

#define NMT_ENUM_GROUP(macro, class, name, description, group) #group,

static const char* MessageTypeGroups[] = {NETWORK_MESSAGE_TYPES(NMT_ENUM_GROUP)};

#include <Strings/bstring.hpp>

void WriteF(QOStream& out, int indent, const char* format, ...)
{
    va_list arglist;
    va_start(arglist, format);

    for (int i = 0; i < indent; i++)
        out.put('\t');

    BString<512> buffer;
    vsprintf(buffer, format, arglist);
    strcat(buffer, "\r\n");
    out.write(buffer, strlen(buffer));

    va_end(arglist);
}

void DocumentFormat(QOStream& out, int index)
{
    WriteF(out, 2, "<message name=\"%s\" id=\"%d\" group=\"%s\">", NetworkMessageTypeNames[index], index,
           MessageTypeGroups[index]);
    WriteF(out, 2, MessageTypeDescriptions[index]);
    const NetworkMessageFormat& format = *GMsgFormats[index];
    WriteF(out, 3, "<items>");
    for (int i = 0; i < format.NItems(); i++)
    {
        const NetworkMessageFormatItem& item = format.GetItem(i);
        WriteF(out, 4, "<item name=\"%s\" type=\"%s\">", (const char*)item.name, ItemTypeNames[item.type]);
        WriteF(out, 4, format._descriptions[i]);
        WriteF(out, 4, "</item>");
    }
    WriteF(out, 3, "</items>");
    WriteF(out, 2, "</message>");
}
#endif

// Initialization of local (static) message formats

#define NMT_DEFINE_FORMAT(macro, class, name, description, group) macro(class, name);

void InitMsgFormats()
{
    if (curMsgFormat > 0)
    {
        return;
    }
    NETWORK_MESSAGE_TYPES(NMT_DEFINE_FORMAT)
    NET_ERROR(curMsgFormat == NMTN);

#if DOCUMENT_MSG_FORMATS
    QOFStream out("messages.xml");
    WriteF(out, 0, "<?xml version=\"1.0\"?>");
    WriteF(out, 0, "<?xml-stylesheet href=\"messages.xsl\" type=\"text/xsl\"?>");

    WriteF(out, 0, "");
    WriteF(out, 0, "<root>");

    WriteF(out, 1, "<types>");
    for (int i = 0; i < sizeof(ItemTypeNames) / sizeof(*ItemTypeNames); i++)
    {
        WriteF(out, 2, "<type name=\"%s\" id=\"%d\">", ItemTypeNames[i], i);
        WriteF(out, 2, ItemTypeDescriptions[i]);
        WriteF(out, 2, "</type>");
    }
    WriteF(out, 1, "</types>");

    WriteF(out, 1, "<messages>");
    for (int i = 0; i < NMTN; i++)
        DocumentFormat(out, i);
    WriteF(out, 1, "</messages>");

    WriteF(out, 0, "</root>");
    out.close();
#endif
}

// Destroy all local (static) message formats
void DestroyMsgFormats()
{
    for (int i = 0; i < NMTN; i++)
    {
        if (GMsgFormats[i])
        {
            GMsgFormats[i]->Clear();
        }
    }
    // Reset the init guard so the next InitMsgFormats() repopulates the items. Without
    // this, a teardown (e.g. an in-process re-mount) that follows a prior network init
    // (e.g. a server-browser enumeration) leaves the formats cleared but curMsgFormat at
    // NMTN, so InitMsgFormats() early-returns and every message decodes to zero values.
    curMsgFormat = 0;
}
