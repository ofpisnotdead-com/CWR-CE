#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Core/Application.hpp>

#include <Poseidon/World/Entities/Vehicles/Ground/Tank.hpp>
#include <Poseidon/World/Entities/Vehicles/Ground/TankNetworkStabilization.hpp>
#include <Poseidon/AI/AI.hpp>

#include <Poseidon/World/Entities/Weapons/Shots.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Dev/Diag/DiagModes.hpp>

#include <Random/randomGen.hpp>

#include <Poseidon/IO/ParamFile/ParamFile.hpp>

#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Game/OperMap.hpp>

#include <Poseidon/Graphics/Rendering/Draw/SpecLods.hpp>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

#define ARROWS 0

namespace Poseidon
{
TankWithAI::TankWithAI(VehicleType* name, Person* driver) : Tank(name, driver) {}

TankWithAI::~TankWithAI() = default;

bool Tank::FireWeapon(int weapon, TargetType* target)
{
    if (GetNetworkManager().IsControlsPaused())
    {
        return false;
    }
    if (weapon >= NMagazineSlots())
    {
        return false;
    }
    if (!GetWeaponLoaded(weapon))
    {
        return false;
    }
    if (!IsFireEnabled())
    {
        return false;
    }

    const WeaponModeType* mode = GetWeaponMode(weapon);
    if (!mode || !mode->_ammo)
    {
        return false;
    }
    bool fired = false;
    switch (mode->_ammo->_simulation)
    {
        case AmmoShotMissile:
        {
            Matrix4Val shootTrans = GunTurretTransform();
            fired = FireMissile(weapon, shootTrans.FastTransform(Type()->_missilePos),
                                shootTrans.Rotate(Type()->_missileDir), shootTrans.Rotate(Vector3(0, 1, 20)), target);
        }
        break;
        case AmmoShotShell:
        {
            Matrix4Val shootTrans = GunTurretTransform();
            Vector3Val pos = shootTrans.FastTransform(Type()->_mainTurret._pos);
            Vector3Val dir = shootTrans.Rotate(Type()->_mainTurret._dir);
            fired = FireShell(weapon, pos, dir, target);

            // add impulse
            Vector3 force = DirectionModelToWorld(-dir * GetMass() * 2);
            Vector3 forcePos = DirectionModelToWorld(pos - _shape->CenterOfMass());
            AddImpulseNetAware(force, forcePos.CrossProduct(force));
        }
        break;
        case AmmoShotBullet:
        {
            Matrix4Val shootTrans = GunTurretTransform();
            Vector3 pos;
            Vector3 dir;
            if (GetMagazineSlot(weapon)._weapon->_shotFromTurret)
            {
                pos = shootTrans.FastTransform(Type()->_mainTurret._pos);
                dir = shootTrans.Rotate(Type()->_mainTurret._dir);
            }
            else
            {
                pos = shootTrans.FastTransform(Type()->_gunPos);
                dir = shootTrans.Rotate(Type()->_gunDir);
            }

            fired = FireMGun(weapon, pos, dir, target);
        }
        break;
        case AmmoNone:
            break;
        default:
            Fail("Unknown ammo used.");
            break;
    }
    if (fired)
    {
        VehicleWithAI::FireWeapon(weapon, target);
        return true;
    }
    return false;
}

void Tank::FireWeaponEffects(int weapon, const Magazine* magazine, EntityAI* target)
{
    const MagazineSlot& slot = GetMagazineSlot(weapon);
    if (!magazine || slot._magazine != magazine)
    {
        return;
    }

    const WeaponModeType* mode = GetWeaponMode(weapon);
    if (!mode)
    {
        return;
    }
    if (!mode->_ammo)
    {
        return;
    }

    if (EnableVisualEffects(SimulateVisibleNear))
    {
        switch (mode->_ammo->_simulation)
        {
            case AmmoShotMissile:
            case AmmoNone:
                break;
            case AmmoShotShell:
            {
                _gunClouds.Start(1.5f);
                _gunFire.Start(0.1f, 1, false);
                const float time = 1;
                _fireDustTimeLeft = time;
                _invFireDustTimeTotal = 1 / time;
            }
            break;
            case AmmoShotBullet:
            {
                if (GetMagazineSlot(weapon)._weapon->_shotFromTurret)
                {
                    _gunClouds.Start(0.1f);
                }
                else
                {
                    _mGunClouds.Start(0.1f);
                    _mGunFire.Start(0.1f, 0.4f, true);
                    _mGunFireFrames = 1;
                    _mGunFireTime = Glob.uiTime;
                    int newPhase;
                    while ((newPhase = toIntFloor(GRandGen.RandomValue() * 3)) == _mGunFirePhase)
                    {
                        ;
                    }
                    _mGunFirePhase = newPhase;
                }
            }
            break;
        }
    }

    base::FireWeaponEffects(weapon, magazine, target);
}

bool Tank::AimWeapon(int weapon, Vector3Par direction)
{
    if (weapon < 0)
    {
        if (NMagazineSlots() <= 0)
        {
            return false;
        }
        weapon = 0;
    }
    SelectWeapon(weapon);
    // move turret/gun accordingly to direction
    Vector3 relDir(VMultiply, DirWorldToModel(), direction);
    // calculate current gun direction
    // compensate for neutral gun position

    _mainTurret.Aim(Type()->_mainTurret, relDir);

    if (Gunner() && Gunner()->QIsManual())
    {
        // advanced missile control
        // check if last shot is missile
        // check if we have still selected missile
        // const MagazineSlot &slot = GetMagazineSlot(weapon);
        // if (slot._magazine && slot._m
        Missile* missile = dyn_cast<Missile, Vehicle>(_lastShot);
        if (missile)
        {
            // check cursor position
            Vector3 dir = Position() + GetWeaponDirection(weapon) * 1500;
            missile->SetControlDirection(dir);
        }
    }

    return true;
}

bool Tank::CalculateAimObserver(Vector3& dir, Target* target)
{
    Vector3 weaponPos = Type()->_comTurret._pos;
    Vector3 tgtPos = target->LandAimingPosition();
    // predict his and my movement

    weaponPos = ObsGunTurretTransform().FastTransform(Type()->_mainTurret._pos);

    const float minPredTime = 0.25f;
    Vector3 myPos = PositionModelToWorld(weaponPos);
    myPos += Speed() * minPredTime;
    dir = tgtPos - myPos;
    return true;
}

bool Tank::CalculateAimWeapon(int weapon, Vector3& dir, Target* target)
{
    if (weapon < 0)
    {
        if (NMagazineSlots() <= 0)
        {
            return false;
        }
        weapon = 0;
    }
    _fire.SetTarget(CommanderUnit(), target);
    const Magazine* magazine = GetMagazineSlot(weapon)._magazine;
    const MagazineType* aInfo = magazine ? magazine->_type : nullptr;
    const WeaponModeType* mode = GetWeaponMode(weapon);
    const AmmoType* ammo = mode ? mode->_ammo : nullptr;
    Vector3 weaponPos = Type()->_mainTurret._pos;
    Vector3 tgtPos = target->LandAimingPosition();
    // predict his and my movement
    float dist2 = tgtPos.Distance2(Position());
    float time2 = 0;
    if (aInfo)
    {
        time2 = dist2 * Square(aInfo->_invInitSpeed * 1.2f);
    }
    if (ammo)
    {
        switch (ammo->_simulation)
        {
            case AmmoShotBullet:
                weaponPos = GunTurretTransform().FastTransform(Type()->_gunPos);
                break;
            case AmmoShotMissile:
                weaponPos = GunTurretTransform().FastTransform(Type()->_missilePos);
                time2 = 0;
                break;
            default:
                if (target->idExact && target->idExact->GetShape())
                {
                    if (target->idExact->GetShape()->GeometrySphere() < 1.5f)
                    {
                        // we do not expect direct hit on small target: hit ground instead
                        tgtPos[1] = GLOB_LAND->SurfaceY(tgtPos[0], tgtPos[2]);
                    }
                }
                weaponPos = GunTurretTransform().FastTransform(Type()->_mainTurret._pos);
                break;
        }
    }
    float time = sqrt(time2);
    const float minPredTime = 0.25f;
    float predTime = floatMax(time + 0.1f, minPredTime);
    Vector3 myPos = PositionModelToWorld(weaponPos);
    // tgtPos+=target->ObjectSpeed()*predTime;
    myPos += Speed() * minPredTime;
    float fall = 0.5f * G_CONST * time2;
    // calculate balistics
    tgtPos[1] += fall; // consider balistics
    if (aInfo)
    {
        Vector3 speedEst = target->speed;
        const float maxSpeedEst = aInfo->_maxLeadSpeed;
        if (speedEst.SquareSize() > Square(maxSpeedEst))
        {
            speedEst = speedEst.Normalized() * maxSpeedEst;
        }
        tgtPos += speedEst * predTime;
    }
    dir = tgtPos - myPos;
    return true;
}

bool Tank::AimWeapon(int weapon, Target* target)
{
    Vector3 dir;
    if (!CalculateAimWeapon(weapon, dir, target))
    {
        return false;
    }
    return AimWeapon(weapon, dir);
}

bool Tank::AimObserver(Vector3Val dir)
{
    // move turret/gun accordingly to direction
    if (Type()->_comTurretOnMainTurret)
    {
        Matrix3 invTurretOrient = TurretTransform().Orientation().InverseRotation();
        Vector3 relDir = invTurretOrient * (DirWorldToModel() * dir);
        // calculate current gun direction
        // compensate for neutral gun position

        _comTurret.Aim(Type()->_comTurret, relDir);
        return true;
    }
    else
    {
        Vector3 relDir = DirWorldToModel() * dir;
        // calculate current gun direction
        // compensate for neutral gun position
        _comTurret.Aim(Type()->_comTurret, relDir);
        return true;
    }
}

Vector3 Tank::GetEyeDirection() const
{
    if (!Type()->HasCommander())
    {
        return GetWeaponDirection(0);
    }
    return Transform().Rotate(ObsGunTurretTransform().Direction());
}

Vector3 Tank::GetWeaponDirection(int weapon) const
{
    Vector3 dir = Type()->_mainTurret._dir;
    if (weapon < 0 || weapon >= NMagazineSlots())
    {
        return Direction();
    }
    const WeaponModeType* mode = GetWeaponMode(weapon);
    const AmmoType* ammo = mode ? mode->_ammo : nullptr;
    if (ammo)
    {
        switch (ammo->_simulation)
        {
            case AmmoShotMissile:
                dir = Type()->_missileDir;
                break;
            case AmmoShotBullet:
                dir = Type()->_gunDir;
                break;
        }
    }
    return Transform().Rotate(GunTurretTransform().Rotate(dir));
}

Vector3 Tank::GetWeaponDirectionWanted(int weapon) const
{
    Vector3 dir = Type()->_mainTurret._dir;
    if (weapon < 0 || weapon >= NMagazineSlots())
    {
        return Direction();
    }
    const WeaponModeType* mode = GetWeaponMode(weapon);
    const AmmoType* ammo = mode ? mode->_ammo : nullptr;
    if (ammo)
    {
        switch (ammo->_simulation)
        {
            case AmmoShotMissile:
                dir = Type()->_missileDir;
                break;
            case AmmoShotBullet:
                dir = Type()->_gunDir;
                break;
        }
    }
    Matrix3Val aim = _mainTurret.GetAimWanted();
    return Transform().Rotate(aim * dir);
}

Vector3 Tank::GetEyeDirectionWanted() const
{
    Vector3 dir = Direction();
    if (!Type()->HasCommander())
    {
        return GetWeaponDirection(0);
    }
    if (Type()->_comTurretOnMainTurret)
    {
        return Transform().Rotate(_mainTurret.GetAimWanted() * _comTurret.GetAimWanted().Direction());
    }
    return Transform().Rotate(_comTurret.GetAimWanted().Direction());
}

// get absolute 3D-direction of turret
Vector3P Tank::GetTurretAbsDirection() const
{
    // calculating turret direction:
    // 1. rotate the vehicle (orientation of vehicle)
    // 2. rotate the turret (_yRot)
    // 3. rotate the gun (- _xRot)
    // step 2 and 3 refer to Turret::TurretTransform and Turret::GunTransform
    const Matrix3P& ori = Orientation();

    Matrix3P rotY;
    rotY.SetRotationY(_mainTurret._yRot);
    Matrix3P rotX;
    rotX.SetRotationX(-_mainTurret._xRot);

    return (ori * rotY * rotX).Direction();
}

Vector3 Tank::GetWeaponCenter(int weapon) const
{
    return _mainTurret.GetCenter(Type()->_mainTurret);
}

static const float ObjPenalty1[] = {1.0, 1.05, 1.1, 1.4};

static const float ObjPenalty2[] = {1.0, 1.10, 1.25, 1.5};

static const float ObjRoadPenalty2[] = {1.0, 1.05, 1.1, 1.2};
static const float ObjRoadHardPenalty[] = {1.0, 1.1, 1.2, 1.6};

static const float ObjHardPenalty[] = {1.0, 1.5, 2.0, 4.0};

const float RoadFaster = 1.2;
float TankWithAI::GetFieldCost(const GeographyInfo& info) const
{
    // road fields are expected to be faster
    // fields with objects will be passed through slower
    int nObj = info.u.howManyObjects;
    int hObj = info.u.howManyHardObjects;
    PoseidonAssert(nObj <= 3);
    if (info.u.road || info.u.track)
    {
        return (1.0f / RoadFaster) * ObjRoadPenalty2[nObj] * ObjRoadHardPenalty[hObj];
    }
    else
    {
        return ObjPenalty2[nObj] * ObjHardPenalty[hObj];
    }
}

float TankWithAI::GetCost(const GeographyInfo& geogr) const
{
    float cost = Type()->GetMinCost() * RoadFaster;
    if (geogr.u.waterDepth >= 2 && !(geogr.u.road || geogr.u.track))
    {
        return (Type()->_canFloat ? 1.0f / 2 : 1e30f); // in water - speed 2 m/s
    }
    // avoid forests
    if (geogr.u.full)
    {
        return 1e30;
    }
    // penalty for objects
    int nObj = geogr.u.howManyObjects;
    PoseidonAssert(nObj <= 3);
    cost *= ObjPenalty1[nObj];
    // avoid steep hills
    // penalty for hills
    int grad = geogr.u.gradient;
    if (grad >= 6)
    {
        return 1e30;
    }
    static const float gradPenalty[6] = {1.0, 1.0, 1.05, 1.1, 2.0, 3.0};
    cost *= gradPenalty[grad];
    // penalty for water
    if (geogr.u.waterDepth == 1 && !(geogr.u.road || geogr.u.track))
    {
        cost += (1.0f / 6); // ford speed expected 6 m/s
    }
    return cost;
}

float TankWithAI::GetCostTurn(int difDir) const
{ // in sec
    if (difDir == 0)
    {
        return 0;
    }
    float aDir = fabs(difDir);
    float cost = aDir * 0.25f + aDir * aDir * 0.04f;
    if (difDir < 0)
    {
        return cost * 0.8f;
    }
    return cost;
}

float TankWithAI::GetTypeCost(OperItemType type) const
{
#define COST_BIG 50
    static const float costsSafe[] = {
        1.0,              // OITNormal,
        1.0,              // OITAvoidBush,
        3.0,              // OITAvoidTree,
        3.0,              // OITAvoid,
        SET_UNACCESSIBLE, // OITWater,
        SET_UNACCESSIBLE, // OITSpaceRoad,
        0.5,              // OITRoad
        1.0,              // OITSpaceBush,
        COST_BIG,         // OITSpaceTree,
        SET_UNACCESSIBLE, // OITSpace,
        1.0               // OITRoadForced,
    };
    static const float costsAware[] = {
        1.0,              // OITNormal,
        1.0,              // OITAvoidBush,
        3.0,              // OITAvoidTree,
        3.0,              // OITAvoid,
        SET_UNACCESSIBLE, // OITWater,
        SET_UNACCESSIBLE, // OITSpaceRoad,
        0.75,             // OITRoad
        1.0,              // OITSpaceBush,
        COST_BIG,         // OITSpaceTree,
        SET_UNACCESSIBLE, // OITSpace,
        1.0               // OITRoadForced,
    };
    static const float costsCombat[] = {
        1.0,              // OITNormal,
        1.0,              // OITAvoidBush,
        1.2,              // OITAvoidTree,
        3.0,              // OITAvoid,
        SET_UNACCESSIBLE, // OITWater,
        SET_UNACCESSIBLE, // OITSpaceRoad,
        1.0,              // OITRoad
        1.0,              // OITSpaceBush,
        1.5,              // OITSpaceTree,
        SET_UNACCESSIBLE, // OITSpace,
        1.0               // OITRoadForced,
    };
    static const float costsStealth[] = {
        1.0,              // OITNormal,
        1.0,              // OITAvoidBush,
        3.0,              // OITAvoidTree,
        3.0,              // OITAvoid,
        SET_UNACCESSIBLE, // OITWater,
        SET_UNACCESSIBLE, // OITSpaceRoad,
        1.2,              // OITRoad
        1.0,              // OITSpaceBush,
        SET_UNACCESSIBLE, // OITSpaceTree,
        SET_UNACCESSIBLE, // OITSpace,
        1.0               // OITRoadForced,
    };
    PoseidonAssert(sizeof(costsSafe) / sizeof(*costsSafe) == NOperItemType);
    PoseidonAssert(sizeof(costsAware) / sizeof(*costsAware) == NOperItemType);
    PoseidonAssert(sizeof(costsCombat) / sizeof(*costsCombat) == NOperItemType);
    PoseidonAssert(sizeof(costsStealth) / sizeof(*costsStealth) == NOperItemType);
    static const float* costs[] = {costsSafe, costsSafe, costsAware, costsCombat, costsStealth};

    if (type == OITWater && Type()->_canFloat)
    {
        return 1;
    }
    CombatMode cmode = CMCombat;
    AIUnit* unit = PilotUnit();
    if (unit)
    {
        cmode = unit->GetCombatMode();
        PoseidonAssert(cmode >= CMCareless && cmode <= CMStealth);
    }
    return costs[cmode - CMCareless][type];
}

NetworkMessageType TankWithAI::GetNMType(NetworkMessageClass cls) const
{
    switch (cls)
    {
        case NMCUpdateGeneric:
            return NMTUpdateTank;
        case NMCUpdatePosition:
            return NMTUpdatePositionTank;
        default:
            return base::GetNMType(cls);
    }
}

class IndicesUpdateTank : public IndicesUpdateTankOrCar
{
    typedef IndicesUpdateTankOrCar base;

  public:
    IndicesUpdateTank();
    NetworkMessageIndices* Clone() const override { return new IndicesUpdateTank; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesUpdateTank::IndicesUpdateTank() = default;

void IndicesUpdateTank::Scan(NetworkMessageFormatBase* format)
{
    base::Scan(format);
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesUpdateTank()
{
    using namespace Poseidon;
    return new IndicesUpdateTank();
}
namespace Poseidon
{

class IndicesUpdatePositionTank : public IndicesUpdatePositionVehicle
{
    typedef IndicesUpdatePositionVehicle base;

  public:
    int mainTurret;
    int comTurret;
    int rpmWanted;
    int thrustLWanted;
    int thrustRWanted;

    IndicesUpdatePositionTank();
    NetworkMessageIndices* Clone() const override { return new IndicesUpdatePositionTank; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesUpdatePositionTank::IndicesUpdatePositionTank()
{
    mainTurret = -1;
    comTurret = -1;
    rpmWanted = -1;
    thrustLWanted = -1;
    thrustRWanted = -1;
}

void IndicesUpdatePositionTank::Scan(NetworkMessageFormatBase* format)
{
    base::Scan(format);

    SCAN(mainTurret)
    SCAN(comTurret)
    SCAN(rpmWanted)
    SCAN(thrustLWanted)
    SCAN(thrustRWanted)
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesUpdatePositionTank()
{
    using namespace Poseidon;
    return new IndicesUpdatePositionTank();
}
namespace Poseidon
{

NetworkMessageFormat& TankWithAI::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    switch (cls)
    {
        case NMCUpdateGeneric:
            base::CreateFormat(cls, format);
            break;
        case NMCUpdatePosition:
            base::CreateFormat(cls, format);
            format.Add("mainTurret", NDTObject, NCTNone, DEFVALUE_MSG(NMTUpdateTurret), DOC_MSG("Main turret object"),
                       ET_ABS_DIF, 1);
            format.Add("comTurret", NDTObject, NCTNone, DEFVALUE_MSG(NMTUpdateTurret),
                       DOC_MSG("Commander turret object"), ET_ABS_DIF, 1);
            format.Add("rpmWanted", NDTFloat, NCTFloat0To2, DEFVALUE(float, 0), DOC_MSG("Wanted value of RPM"),
                       ET_ABS_DIF, ERR_COEF_VALUE_MAJOR);
            format.Add("thrustLWanted", NDTFloat, NCTFloatM1ToP1, DEFVALUE(float, 0), DOC_MSG("Wanted left thrust"),
                       ET_ABS_DIF, ERR_COEF_VALUE_MAJOR);
            format.Add("thrustRWanted", NDTFloat, NCTFloatM1ToP1, DEFVALUE(float, 0), DOC_MSG("Wanted right thrust"),
                       ET_ABS_DIF, ERR_COEF_VALUE_MAJOR);
            break;
        default:
            base::CreateFormat(cls, format);
            break;
    }
    return format;
}

TMError TankWithAI::TransferMsg(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
            TMCHECK(base::TransferMsg(ctx))
            break;
        case NMCUpdatePosition:
        {
            PoseidonAssert(dynamic_cast<const IndicesUpdatePositionTank*>(ctx.GetIndices()))
                const IndicesUpdatePositionTank* indices =
                    static_cast<const IndicesUpdatePositionTank*>(ctx.GetIndices());

            Matrix3 oldTrans = Orientation();
            Matrix3 oldTurretTrans = Orientation() * TurretTransform().Orientation();
            TMCHECK(base::TransferMsg(ctx))
            if (Poseidon::ShouldTransferNetworkTankTurretState(ctx.IsSending(),
                                                               GunnerUnit() && GunnerUnit()->GetPerson()->IsLocal()))
                TMCHECK(ctx.IdxTransferObject(indices->mainTurret, _mainTurret))
            if (Poseidon::ShouldTransferNetworkTankTurretState(
                    ctx.IsSending(), ObserverUnit() && ObserverUnit()->GetPerson()->IsLocal()))
                TMCHECK(ctx.IdxTransferObject(indices->comTurret, _comTurret))
            StabilizeTurrets(oldTrans, Orientation(), oldTurretTrans);

            ITRANSF(rpmWanted)
            ITRANSF(thrustLWanted)
            ITRANSF(thrustRWanted)
        }
        break;
        default:
            TMCHECK(base::TransferMsg(ctx))
            break;
    }
    return TMOK;
}

float TankWithAI::CalculateError(NetworkMessageContext& ctx)
{
    float error = 0;
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
            error += base::CalculateError(ctx);
            break;
        case NMCUpdatePosition:
        {
            error += base::CalculateError(ctx);

            PoseidonAssert(dynamic_cast<const IndicesUpdatePositionTank*>(ctx.GetIndices()))
                const IndicesUpdatePositionTank* indices =
                    static_cast<const IndicesUpdatePositionTank*>(ctx.GetIndices());

            int index = indices->mainTurret;
            if (index >= 0)
            {
                NetworkMessageFormatBase* format = const_cast<NetworkMessageFormatBase*>(ctx.GetFormat());
                NetworkMessageFormatItem& item = format->GetItem(index);
                CHECK_ASSIGN(typeVal, item.defValue, const RefNetworkDataTyped<int>);
                int type = typeVal.GetVal();
                NetworkMessageFormatBase* subformat = ctx.GetComponent()->GetFormat((NetworkMessageType)type);
                if (subformat)
                {
                    const RefNetworkData& val = ctx.GetMessage()->values[index];
                    CHECK_ASSIGN(msgVal, val, const RefNetworkDataTyped<NetworkMessage>);
                    NetworkMessage& submsg = msgVal.GetVal();
                    NetworkMessageContext subctx(&submsg, subformat, ctx);
                    error += _mainTurret.CalculateError(subctx);
                }
            }
            index = indices->comTurret;
            if (index >= 0)
            {
                NetworkMessageFormatBase* format = const_cast<NetworkMessageFormatBase*>(ctx.GetFormat());
                NetworkMessageFormatItem& item = format->GetItem(index);
                CHECK_ASSIGN(typeVal, item.defValue, const RefNetworkDataTyped<int>);
                int type = typeVal.GetVal();
                NetworkMessageFormatBase* subformat = ctx.GetComponent()->GetFormat((NetworkMessageType)type);
                if (subformat)
                {
                    const RefNetworkData& val = ctx.GetMessage()->values[index];
                    CHECK_ASSIGN(msgVal, val, const RefNetworkDataTyped<NetworkMessage>);
                    NetworkMessage& submsg = msgVal.GetVal();
                    NetworkMessageContext subctx(&submsg, subformat, ctx);
                    error += _comTurret.CalculateError(subctx);
                }
            }
            ICALCERR_ABSDIF(float, rpmWanted, ERR_COEF_VALUE_MAJOR)
            ICALCERR_ABSDIF(float, thrustLWanted, ERR_COEF_VALUE_MAJOR)
            ICALCERR_ABSDIF(float, thrustRWanted, ERR_COEF_VALUE_MAJOR)
        }
        break;
        default:
            error += base::CalculateError(ctx);
            break;
    }
    return error;
}

} // namespace Poseidon
