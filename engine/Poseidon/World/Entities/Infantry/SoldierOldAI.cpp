#include <Poseidon/World/Entities/Infantry/SoldierOldCommon.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
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
#include <Random/randomGen.hpp>

namespace Poseidon
{
using namespace Foundation;

void Man::AimHeadForward()
{
    _headYRotWanted = 0;
    _headXRotWanted = 0;
}

bool Man::AimObserver(Vector3Par direction)
{
    AimHead(direction);
    return false;
}

void Man::AimHead(Vector3Par direction)
{
    Vector3 relDir(VMultiply, DirWorldToModel(), direction);
    // compensate for aiming (is applied before head)
    relDir = GunTransform().InverseRotation().Rotate(relDir);

    float yRotWanted = -atan2(relDir.X(), relDir.Z());
    float sizeXZ = sqrt(Square(relDir.X()) + Square(relDir.Z()));
    float xRotWanted = atan2(relDir.Y(), sizeXZ);

    // limit with natural head limits
    // the more y rot, the less x rot enabled
    const float maxYRot = H_PI / 2;
    if (!QIsManual())
    {
        Limit(yRotWanted, Type()->_minHeadTurnAI, Type()->_maxHeadTurnAI);
    }
    Limit(yRotWanted, -maxYRot, +maxYRot);
    float yRotF = fabs(yRotWanted) * (1.0f / maxYRot);
    float maxXRot = (1 - yRotF) * H_PI / 4;
    Limit(xRotWanted, -maxXRot, +maxXRot);

    _headYRotWanted = yRotWanted;
    _headXRotWanted = xRotWanted;
}

void Man::AimHeadAI(Vector3Par direction, float deltaT)
{
    if (_lookTargetTimeLeft > 0)
    {
        AimHead(direction);
    }
    else
    {
        if (direction.Distance2(Direction()) < Square(0.05f))
        {
            // when nearly forward, shorten the forward-look phase
            _lookForwardTimeLeft -= 2 * deltaT;
        }
        AimHeadForward();
    }
}

const static Vector3 GrenadeDir = Vector3(0, 0.4f, 1).Normalized();
const float GrenadeAtan2 = atan2(GrenadeDir.Y(), GrenadeDir.SizeXZ());

void Man::AimWeaponAI(int weapon, Vector3Par direction, float deltaT)
{
    AimHeadAI(direction, deltaT);

    SelectWeapon(weapon);
    Vector3 relDir(VMultiply, DirWorldToModel(), direction);

    const Magazine* magazine = GetMagazineSlot(weapon)._magazine;
    if (!magazine)
    {
        return;
    }

    // calculate current gun direction
    // compensate for neutral gun position

    float inacc = RifleInaccuracy() * GetInvAbility();

    // calculate actual neutral rotation
    // for this: animate gun points as necessary
    float yRotWanted = -atan2(relDir.X(), relDir.Z());
    float sizeXZ = sqrt(Square(relDir.X()) + Square(relDir.Z()));
    float xRotWanted = atan2(relDir.Y(), sizeXZ);

    const WeaponModeType* mode = GetWeaponMode(weapon);
    const AmmoType* ammo = mode ? mode->_ammo : nullptr;
    const MagazineType* mag = magazine->_type;

    if (ammo->_simulation == AmmoShotShell && mag->_initSpeed < 30)
    {
        xRotWanted -= GrenadeAtan2;
    }

    float ability = GetAbility();
    float weaponDexterity = 1;

    if (weapon >= 0 && weapon < NMagazineSlots())
    {
        const WeaponType* info = GetMagazineSlot(weapon)._weapon;
        if (info)
        {
            weaponDexterity = info->_dexterity;
        }
    }

    float maxSpeed = ability * 0.2f * deltaT * weaponDexterity;
    float delta;

    float coef = 0.5f;
    xRotWanted += _aimInaccuracyX * coef;
    yRotWanted += _aimInaccuracyY * coef;

    Limit(xRotWanted, Type()->_minGunElev * 1.5f, Type()->_maxGunElev * 1.5f);
    Limit(yRotWanted, Type()->_minGunTurnAI * 1.5f, Type()->_maxGunTurnAI * 1.5f);

    delta = yRotWanted - _gunYRotWanted;
    saturate(delta, -maxSpeed, +maxSpeed);
    _gunYRotWanted += delta;

    delta = xRotWanted - _gunXRotWanted;
    saturate(delta, -maxSpeed, +maxSpeed);
    _gunXRotWanted += delta;

    if (IsFirePrepare())
    {
        // prepare mode skips _gunTrans recalc and keeps max error to avoid
        // accurate aiming that would break the transition to fire mode
        float maxAimInacc = floatMin(inacc, 0.2f);
        float maxDistIA = (GetInvAbility() - 1) * 0.1f;

        _aimInaccuracyX = maxAimInacc;
        _aimInaccuracyY = maxAimInacc;
        _aimInaccuracyDist = maxDistIA;
    }
    else
    {
        float iTime = Glob.time - _lastInaccuracyTime;
        saturateMin(iTime, 2);
        while ((iTime -= 0.1f) >= 0)
        {
            float maxAimInacc = floatMin(inacc, 0.2f);
            float randX = GRandGen.RandomValue() - 0.5f;
            float randY = GRandGen.RandomValue() - 0.5f;
            _aimInaccuracyX += randX * inacc * 2;
            _aimInaccuracyY += randY * inacc * 2;
            saturate(_aimInaccuracyX, -maxAimInacc, +maxAimInacc);
            saturate(_aimInaccuracyY, -maxAimInacc, +maxAimInacc);
            _lastInaccuracyTime = Glob.time;
        }

        float dTime = Glob.time - _lastInaccuracyDistTime;
        while ((dTime -= 1.0f) >= 0)
        {
            float maxDistIA = (GetInvAbility() - 1) * 0.1f;
            float randZ = GRandGen.RandomValue() - 0.5f;
            _aimInaccuracyDist += randZ * 0.25f;
            saturate(_aimInaccuracyDist, -maxDistIA, +maxDistIA);
            _lastInaccuracyDistTime = Glob.time;
        }
    }
}

void Man::AimWeaponAI(int weapon, Target* target, float deltaT)
{
    if (weapon < 0)
    {
        if (NMagazineSlots() <= 0)
        {
            return;
        }
        weapon = 0;
    }

    Vector3 dir;
    if (!CalculateAimWeapon(weapon, dir, target))
    {
        return;
    }
    _fire.SetTarget(CommanderUnit(), target);
    AimWeaponAI(weapon, dir, deltaT);
}

void Man::AimWeaponManDir(int weapon, Vector3Par direction)
{
    AimWeapon(weapon, direction);
}

void Man::SimulateHUD(CameraType camType, float deltaT) {}

void Man::AimWeaponManSpeed(int weapon, float moveX, float moveY) {}

bool Man::AimWeapon(int weapon, Vector3Par direction)
{
    AimHead(direction);

    if (QIsManual() && InputSubsystem::Instance().IsLookAroundEnabled())
    {
        // no weapon aiming when looking around
        return false;
    }
    SelectWeapon(weapon);
    Vector3 relDir(VMultiply, DirWorldToModel(), direction);

    const WeaponModeType* mode = weapon >= 0 ? GetWeaponMode(weapon) : nullptr;
    const AmmoType* aType = mode ? mode->_ammo : nullptr;

    float inacc = RifleInaccuracy() * GetInvAbility();

    float yRotWanted = -atan2(relDir.X(), relDir.Z());
    float sizeXZ = sqrt(Square(relDir.X()) + Square(relDir.Z()));
    float xRotWanted = atan2(relDir.Y(), sizeXZ);

    const float coef = 0.25f;
    const float invCoef = 1 / coef;

    float minInaccY = floatMin(0, (Type()->_minGunTurn - yRotWanted) * invCoef);
    float maxInaccY = floatMax(0, (Type()->_maxGunTurn - yRotWanted) * invCoef);

    saturate(_aimInaccuracyY, minInaccY, maxInaccY);

    float inaccX = _aimInaccuracyX * coef;
    float inaccY = _aimInaccuracyY * coef;

    _gunXRotWanted = xRotWanted + inaccX;
    _gunYRotWanted = yRotWanted + inaccY;

    float iTime = Glob.time - _lastInaccuracyTime;
    while ((iTime -= 0.2f) >= 0)
    {
        float randX = GRandGen.RandomValue() - 0.5f;
        float randY = GRandGen.RandomValue() - 0.5f;
        _aimInaccuracyX += randX * inacc;
        _aimInaccuracyY += randY * inacc;
        saturate(_aimInaccuracyX, -inacc, +inacc);
        saturate(_aimInaccuracyY, -inacc, +inacc);
        _lastInaccuracyTime = Glob.time;
    }
    _aimInaccuracyDist = 0;
    if (_forceFireWeapon >= 0)
    {
        // Note: create different function for forceFire path

        if (aType)
        {
            switch (aType->_simulation)
            {
                case AmmoShotPipeBomb:
                case AmmoShotTimeBomb:
                case AmmoShotMine:
                case AmmoShotLaser:
                    return true;
            }
        }
        float f = GetLimitGunMovement();
        Limit(_gunXRotWanted, Type()->_minGunElev * f, Type()->_maxGunElev * f);
        Limit(_gunYRotWanted, Type()->_minGunTurnAI * f, Type()->_maxGunTurnAI * f);
        return (fabs(_gunXRotWanted - _gunXRot) < 0.01f && fabs(_gunYRotWanted - _gunYRot) < 0.01f);
    }
    return true;
}

bool Man::AimWeaponForceFire(int weapon)
{
    Vector3 dir = Direction();
    dir[1] = 10;
    // different direction depending on weapon type
    const WeaponModeType* mode = weapon >= 0 ? GetWeaponMode(weapon) : nullptr;
    if (!mode)
    {
        return false;
    }
    const MagazineSlot& slot = GetMagazineSlot(weapon);
    const AmmoType* aType = mode->_ammo;
    const Magazine* magazine = slot._magazine;
    if (!magazine)
    {
        return false;
    }
    const MagazineType* aInfo = magazine->_type;
    if (aType)
    {
        switch (aType->_simulation)
        {
            case AmmoShotPipeBomb:
            case AmmoShotTimeBomb:
            case AmmoShotMine:
                return true;
            case AmmoShotLaser:
                return false;
            case AmmoShotShell:
                if (aInfo->_initSpeed < 30)
                {
                    dir[1] = 0.5f;
                }
                else
                {
                    dir[1] = 1;
                }
                break;
        }
    }
    dir.Normalize();
    return AimWeapon(_currentWeapon, dir);
}

bool Man::CalculateAimWeapon(int weapon, Vector3& dir, Target* target)
{
    if (weapon < 0)
    {
        return false;
    }

    const MagazineSlot& slot = GetMagazineSlot(weapon);

    const Magazine* magazine = slot._magazine;
    if (!magazine)
    {
        return false;
    }
    const MagazineType* aInfo = magazine->_type;
    if (!aInfo)
    {
        return false;
    }
    const WeaponModeType* mode = GetWeaponMode(weapon);
    if (!mode)
    {
        return false;
    }
    Vector3 tgtPos = target->LandAimingPosition();
    float dist = tgtPos.Distance(Position());
    float distFactor = dist < 40 ? 0 : dist - 40;

    dist += _aimInaccuracyDist * distFactor;
    saturateMax(dist, 10);
    float time = dist * aInfo->_invInitSpeed;
    float balFactor = 0.5f * G_CONST;
    float leadFactor = 1;
    if (mode && mode->_ammo)
    {
        switch (mode->_ammo->_simulation)
        {
            case AmmoShotBullet:
                break;
            case AmmoShotShell:
                if (aInfo->_initSpeed < 30)
                {
                    time *= 1.3f;
                }
                else
                {
                    time *= 1.1f;
                }
                break;
            case AmmoShotMissile:
                time = dist / mode->_ammo->maxSpeed + 0.1f;
                balFactor *= 0.37f;
                leadFactor = 1.4f;
                break;
            default:
                time = 0;
                break;
        }
    }
    else
    {
        time = 0;
    }
    saturateMin(time, 4);
    Vector3 myPos = PositionModelToWorld(GetWeaponPoint(weapon));

    float fall = Square(time) * balFactor;
    tgtPos[1] += fall;

    const float predTime = 0.2f;
    myPos += Speed() * predTime;

    Vector3 speedEst = target->speed;
    if (aInfo)
    {
        float maxLead = aInfo->_maxLeadSpeed * GetAbility();
        LimitLead(speedEst, maxLead);
        tgtPos += speedEst * time * leadFactor;
    }

    dir = tgtPos - myPos;
    return true;
}

bool Man::AimWeapon(int weapon, Target* target)
{
    Vector3 dir;
    if (!CalculateAimWeapon(weapon, dir, target))
    {
        return false;
    }
    _fire.SetTarget(CommanderUnit(), target);
    return AimWeapon(weapon, dir);
}

void Man::AdjustWeapon(int weapon, CameraType camType, float fov, Vector3& camDir)
{
    if (weapon < 0 || weapon >= NMagazineSlots())
    {
        return;
    }

    const MagazineSlot& mag = GetMagazineSlot(weapon);
    const Magazine* magazine = mag._magazine;
    if (!magazine)
    {
        return;
    }
    const MagazineType* aInfo = magazine->_type;
    if (!aInfo)
    {
        return;
    }

    const MuzzleType* muzzle = mag._muzzle;

    float distance = Interpolativ(fov, muzzle->_opticsZoomMin, muzzle->_opticsZoomMax, muzzle->_distanceZoomMin,
                                  muzzle->_distanceZoomMax);

    float time = distance * aInfo->_invInitSpeed;
    float fallPerM = time * aInfo->_invInitSpeed * 0.5f * G_CONST;
    Vector3 relDir = DirectionWorldToModel(camDir);
    relDir[1] += fallPerM;

    camDir = DirectionModelToWorld(relDir);
}

Vector3 Man::GetWeaponRelDirection(int weapon) const
{
    if (weapon >= 0 && weapon < NMagazineSlots())
    {
        int weaponIndex = -1;
        const WeaponType* info = GetMagazineSlot(weapon)._weapon;
        if (info->_weaponType & MaskSlotPrimary)
        {
            weaponIndex = Type()->_priWeaponIndex;
        }
        else if (info->_weaponType & MaskSlotSecondary)
        {
            weaponIndex = Type()->_secWeaponIndex;
        }
        else if (info->_weaponType & MaskSlotHandGun)
        {
            weaponIndex = Type()->_handGunIndex;
        }
        if (weaponIndex >= 0)
        {
            Shape* proxyContainer = _shape->MemoryLevel();
            const ProxyObject& proxy = proxyContainer->Proxy(weaponIndex);

            const MuzzleType* muzzle = GetMagazineSlot(weapon)._muzzle;
            Vector3Val dir = muzzle->_muzzleDir;

            Matrix4Val animTrans = AnimateProxyMatrix(_shape->FindMemoryLevel(), proxy);
            return animTrans.Rotate(dir);
        }
        // check for hand grenades
        const WeaponModeType* mode = GetWeaponMode(weapon);
        const AmmoType* ammo = mode ? mode->_ammo : nullptr;
        if (ammo && ammo->_simulation == AmmoShotShell)
        {
            return GunTransform().Rotate(GrenadeDir);
        }
    }

    return GunTransform().Direction();
}

Matrix4 Man::GetWeaponRelTransform(int weapon) const
{
    if (weapon >= 0 && weapon < NMagazineSlots())
    {
        int weaponIndex = -1;
        const WeaponType* info = GetMagazineSlot(weapon)._weapon;
        if (info->_weaponType & MaskSlotPrimary)
        {
            weaponIndex = Type()->_priWeaponIndex;
        }
        else if (info->_weaponType & MaskSlotSecondary)
        {
            weaponIndex = Type()->_secWeaponIndex;
        }
        else if (info->_weaponType & MaskSlotHandGun)
        {
            weaponIndex = Type()->_handGunIndex;
        }
        if (weaponIndex >= 0)
        {
            Shape* proxyContainer = _shape->MemoryLevel();
            const ProxyObject& proxy = proxyContainer->Proxy(weaponIndex);

            const MuzzleType* muzzle = GetMagazineSlot(weapon)._muzzle;
            Vector3Val dir = muzzle->_muzzleDir;

            ProxyWeapon* weaponObj = dyn_cast<ProxyWeapon, Object>(proxy.obj);
            if (weaponObj)
            {
                Vector3Val pos = weaponObj->Type()->GetCameraPos();

                Matrix4Val animTrans = AnimateProxyMatrix(_shape->FindMemoryLevel(), proxy);
                Matrix4 trans;
                trans.SetDirectionAndUp(animTrans.Rotate(dir), animTrans.DirectionUp());
                trans.SetPosition(animTrans.FastTransform(pos));
                return trans;
            }
        }
    }

    // fall back to eye position (binoculars, etc.)
    int level = _shape->FindMemoryLevel();
    Vector3 pos;
    int selIndex = Type()->_pilotPoint;
    if (selIndex < 0)
    {
        pos = VZero;
    }
    else
    {
        const Selection& sel = _shape->LevelOpaque(level)->NamedSel(selIndex);
        pos = AnimatePoint(level, sel[0]);
    }

    Matrix4 trans;
    trans.SetPosition(pos);
    trans.SetOrientation(GunTransform().Orientation());
    return trans;
}

Vector3 Man::GetEyeDirection() const
{
    Vector3 dir = _headTrans.Direction();
    dir = GunTransform().Rotate(dir);
    DirectionModelToWorld(dir, dir);
    return dir;
}

Matrix3 Man::GetHeadRelOrientation() const
{
    return GunTransform().Orientation();
}

Vector3 Man::GetWeaponDirection(int weapon) const
{
    Vector3 dir = GetWeaponRelDirection(weapon);
    DirectionModelToWorld(dir, dir);
    return dir;
}

Vector3 Man::GetHeadCenter() const
{
    int sel = Type()->_headAxisPoint;
    if (sel < 0)
    {
        return VZero;
    }
    int level = _shape->FindMemoryLevel();
    if (level < 0)
    {
        return VZero;
    }
    return AnimatePoint(level, sel);
}

Vector3 Man::GetWeaponCenter(int weapon) const
{
    int sel = Type()->_aimingAxisPoint;
    if (sel < 0)
    {
        return VZero;
    }
    int level = _shape->FindMemoryLevel();
    if (level < 0)
    {
        return VZero;
    }
    return AnimatePoint(level, sel);
}

Vector3 Man::GetWeaponPoint(int weapon) const
{
    if (weapon >= 0 && weapon < NMagazineSlots())
    {
        int weaponIndex = -1;
        const WeaponType* info = GetMagazineSlot(weapon)._weapon;
        if (info->_weaponType & MaskSlotPrimary)
        {
            weaponIndex = Type()->_priWeaponIndex;
        }
        else if (info->_weaponType & MaskSlotSecondary)
        {
            weaponIndex = Type()->_secWeaponIndex;
        }
        else if (info->_weaponType & MaskSlotHandGun)
        {
            weaponIndex = Type()->_handGunIndex;
        }
        if (weaponIndex >= 0)
        {
            Shape* proxyContainer = _shape->MemoryLevel();
            const ProxyObject& proxy = proxyContainer->Proxy(weaponIndex);
            Matrix4 animTrans = AnimateProxyMatrix(_shape->FindMemoryLevel(), proxy);

            const MuzzleType* muzzle = GetMagazineSlot(weapon)._muzzle;
            Vector3Val pos = muzzle->_muzzlePos;

            return animTrans.FastTransform(pos);
        }
    }
    return PositionWorldToModel(AimingPosition());
}

bool Man::GetWeaponCartridgePos(int weapon, Matrix4& pos, Vector3& vel) const
{
    if (weapon >= 0 && weapon < NMagazineSlots())
    {
        int weaponIndex = -1;
        const WeaponType* info = GetMagazineSlot(weapon)._weapon;
        if (info->_weaponType & MaskSlotPrimary)
        {
            weaponIndex = Type()->_priWeaponIndex;
        }
        else if (info->_weaponType & MaskSlotSecondary)
        {
            weaponIndex = Type()->_secWeaponIndex;
        }
        else if (info->_weaponType & MaskSlotHandGun)
        {
            weaponIndex = Type()->_handGunIndex;
        }
        if (weaponIndex >= 0)
        {
            Shape* proxyContainer = _shape->MemoryLevel();
            const ProxyObject& proxy = proxyContainer->Proxy(weaponIndex);

            Matrix4 animTrans = AnimateProxyMatrix(_shape->FindMemoryLevel(), proxy);

            const MuzzleType* muzzle = GetMagazineSlot(weapon)._muzzle;

            if (muzzle->_cartridgeOutPosIndex < 0 || muzzle->_cartridgeOutEndIndex < 0)
            {
                return false;
            }

            Vector3Val cDir = muzzle->_muzzleDir;
            Vector3Val cPos = muzzle->_cartridgeOutPos;
            Vector3Val cVel = muzzle->_cartridgeOutVel;

            float randomSpd = GRandGen.RandomValue() * 0.5f + 0.75f;

            Matrix4 trans;
            trans.SetDirectionAndUp(cDir, VUp);
            trans.SetPosition(cPos);
            // Note: consider precalculating trans in MuzzleType

            pos = animTrans * trans;
            vel = animTrans.Rotate(cVel * randomSpd);

            return true;
        }
    }
    LOG_DEBUG(Physics, "cartridge pos from no known weapon");
    return base::GetWeaponCartridgePos(weapon, pos, vel);
}

void Man::SelectPrimaryWeapon()
{
    int index = FindWeaponType(MaskSlotPrimary);
    if (index >= 0)
    {
        const WeaponType* weapon = GetWeaponSystem(index);
        for (int i = 0; i < NMagazineSlots(); i++)
        {
            if (GetMagazineSlot(i)._weapon == weapon)
            {
                SelectWeapon(i);
                break;
            }
        }
    }
}

void Man::SelectHandGunWeapon()
{
    int index = FindWeaponType(MaskSlotHandGun);
    if (index >= 0)
    {
        const WeaponType* weapon = GetWeaponSystem(index);
        for (int i = 0; i < NMagazineSlots(); i++)
        {
            if (GetMagazineSlot(i)._weapon == weapon)
            {
                SelectWeapon(i);
                break;
            }
        }
    }
}

void Man::FireAttemptWhenNotPossible()
{
    ActionMap* map = Type()->GetActionMap(_primaryMove.id);
    MoveId moveWanted = map->GetAction(ManActFireNotPossible);
    if (moveWanted != MoveIdNone)
    {
        if (IsHandGunSelected() && !IsHandGunInMove())
        {
            if (GetActUpDegree() == ManPosStand)
            {
                moveWanted = map->GetAction(ManActHandGunOn);
            }
            else
            {
                SelectPrimaryWeapon();
            }
        }
        _forceMove = MotionPathItem(moveWanted, nullptr);
    }
}

bool Soldier::FireWeapon(int weapon, TargetType* target)
{
    if (GetNetworkManager().IsControlsPaused())
    {
        return false;
    }
    if (weapon >= NMagazineSlots())
    {
        return false;
    }
    if (!IsAbleToFire())
    {
        return false;
    }

    if (_waterContact)
    {
        float maxFireDepth = 0.9f;
        if (IsDown())
        {
            maxFireDepth = 0.2f;
        }
        if (_waterDepth > maxFireDepth)
        {
            return false;
        }
    }
    const MagazineSlot& slot = GetMagazineSlot(weapon);
    Magazine* magazine = slot._magazine;
    if (!magazine)
    {
        if (WeaponsDisabled())
        {
            return false;
        }
        if (slot._weapon)
        {
            LOG_DEBUG(Physics, "Fire weapon {}", (const char*)slot._weapon->GetName());
        }
        if (slot._muzzle)
        {
            LOG_DEBUG(Physics, "Fire muzzle {}", (const char*)slot._muzzle->GetName());
        }
        if (slot._mode)
        {
            const WeaponModeType* mode = GetWeaponMode(weapon);
            if (mode)
            {
                LOG_DEBUG(Physics, "Fire mode {} : {}", slot._mode, (const char*)mode->GetName());
            }
            else
            {
                LOG_DEBUG(Physics, "Fire mode {}", slot._mode);
            }
        }
        PlayEmptyMagazineSound(weapon);
        return false;
    }
    const MagazineType* aInfo = magazine ? magazine->_type : nullptr;
    PoseidonAssert(aInfo);
    const WeaponModeType* mode = GetWeaponMode(weapon);
    PoseidonAssert(mode);

    if (!GetWeaponLoaded(weapon))
    {
        if (WeaponsDisabled())
        {
            return false;
        }

        bool playSound = false;
        if (mode->_ammo && mode->_ammo->_simulation != AmmoNone)
        {
            playSound = magazine->_ammo <= 0 || magazine->_reloadMagazine > 0 || IsActionInProgress(MFReload);
        }

        if (playSound)
        {
            PlayEmptyMagazineSound(weapon);
        }
        return false;
    }

    bool fired = false;

    if (!mode->_ammo)
    {
        return false;
    }

    switch (mode->_ammo->_simulation)
    {
        case AmmoShotMissile:
        {
            if (!EnableMissile())
            {
                return false;
            }
            // check relative missile direction
            // should be forward animated with GunTransform

            Vector3 dir = GetWeaponRelDirection(weapon);

            fired = FireMissile(weapon, GetWeaponPoint(weapon), dir, dir * aInfo->_initSpeed, target);
        }
        break;
        case AmmoShotStroke:
            if (!IsActionInProgress(MFThrowGrenade))
            {
                MoveId moveWanted = MoveId(MoveIdNone);
                ActionMap* map = Type()->GetActionMap(_primaryMove.id);
                moveWanted = map->GetAction(ManActStrokeGun);
                if (moveWanted == MoveIdNone)
                {
                    moveWanted = map->GetAction(ManActStrokeFist);
                }
                if (moveWanted != MoveIdNone)
                {
                    Ref<ActionContextDefault> context = new ActionContextDefault;
                    context->function = MFThrowGrenade;
                    context->param = weapon;
                    _forceMove = MotionPathItem(moveWanted, context);
                }
            }
            break;

        case AmmoShotShell:
        case AmmoShotSmoke:
        case AmmoShotIlluminating:
        {
            if (WeaponsDisabled())
            {
                return false;
            }
            if (aInfo->_initSpeed < 30)
            {
                if (!IsActionInProgress(MFThrowGrenade))
                {
                    ActionMap* map = Type()->GetActionMap(_primaryMove.id);
                    // get which action is it (based on action map)
                    ManAction selAction = ManActThrowGrenade;
                    MoveId moveWanted = map->GetAction(selAction);
                    if (moveWanted != MoveIdNone)
                    {
                        Ref<ActionContextDefault> context = new ActionContextDefault;
                        context->function = MFThrowGrenade;
                        context->param = weapon;
                        _forceMove = MotionPathItem(moveWanted, context);
                    }
                }
                break;
            }
            Matrix4Val shootTrans = GunTransform();
            fired = FireShell(weapon, GetWeaponPoint(weapon), shootTrans.Direction(), target);
        }
        break;
        case AmmoShotTimeBomb:
        case AmmoShotPipeBomb:
        case AmmoShotMine:
        {
            Vector3 pos = Position() + 0.5f * Direction() + VUp * 0.5f;
            pos[1] = GLandscape->RoadSurfaceY(pos);
            fired = FireShell(weapon, PositionWorldToModel(pos), Direction(), target);
            if (fired && mode->_ammo->_simulation == AmmoShotPipeBomb)
            {
                _pipeBombs.Add(_lastShot);
            }
        }
        break;
        case AmmoShotBullet:
        {
            if (DisableWeaponsLong())
            {
                FireAttemptWhenNotPossible();
                return false;
            }
            if (WeaponsDisabled())
            {
                return false;
            }
            Vector3Val pos = GetWeaponPoint(weapon);
            Vector3 dir = GetWeaponRelDirection(weapon);

            if (QIsManual())
            {
                float fov = GScene->GetCamera()->Left();
                CameraType camType = GWorld->GetCameraType();
                AdjustWeapon(weapon, camType, fov, dir);
            }

            fired = FireMGun(weapon, pos, dir, target);
        }
        break;
        case AmmoNone:
            break;
        case AmmoShotLaser:
            FireLaser(weapon, target);
            break;
        default:
            Fail("Unknown ammo used.");
            break;
    }

    if (fired)
    {
        base::FireWeapon(weapon, target);
        return true;
    }
    return false;
}

float Soldier::GetRecoilFactor() const
{
    return GetAimPrecision();
}

void Soldier::OnRecoilAbort()
{
    float rotX = _recoil->GetRecoilRotX(_recoilTime) * _recoilFactor;
    _gunXRot += rotX;
    _gunXRotWanted += rotX;
    // this does not happen very often
    RecalcGunTransform();
}

void Soldier::FireWeaponEffects(int weapon, const Magazine* magazine, EntityAI* target)
{
    if (weapon < 0 || weapon >= NMagazineSlots())
    {
        return;
    }
    const MagazineSlot& slot = GetMagazineSlot(weapon);
    if (!magazine || slot._magazine != magazine)
    {
        return;
    }

    const MagazineType* aInfo = magazine ? magazine->_type : nullptr;
    const WeaponModeType* mode = GetWeaponMode(weapon);
    if (!mode)
    {
        return;
    }
    if (!mode->_ammo)
    {
        return;
    }

    switch (mode->_ammo->_simulation)
    {
        case AmmoShotMissile:
            break;
        case AmmoShotShell:
        case AmmoShotSmoke:
        case AmmoShotIlluminating:
            if (aInfo->_initSpeed < 30)
            {
                break;
            }
            _gunClouds.Start(0.1f);
            break;
        case AmmoShotBullet:
        {
            _mGunFireWeapon = slot._weapon;
            _mGunClouds.Start(0.1f);
            _mGunFire.Start(0.05f, 0.004f, true);
            _mGunFireFrames = 1;
            _mGunFireTime = Glob.uiTime;
            int newPhase;
            while ((newPhase = toIntFloor(GRandGen.RandomValue() * 3)) == _mGunFirePhase)
            {
                ;
            }
            _mGunFirePhase = newPhase;
        }
        break;
    }

    base::FireWeaponEffects(weapon, magazine, target);
}

DEFINE_CASTING(Soldier)

Soldier::Soldier(VehicleType* name, bool fullCreate) : base(name, fullCreate)
{
    _brain = new AIUnit(this);
}

Soldier::~Soldier() = default;

static const float ObjPenalty[4][4] = {
    {1.0f, 1.05f, 1.2f, 1.5f}, // safe
    {1.0f, 0.7f, 0.8f, 1.2f},  // aware
    {1.2f, 0.8f, 0.5f, 1.2f},  // combat
    {1.2f, 0.8f, 0.5f, 1.2f}   // stealth
};

static const float RoadPenalty[4] = {1.0f / 1.5f, 1.0f / 1.2f, 1.5f, 1.5f};

static const float ForestPenalty[4] = {1.5f, 1.2f, 0.7f, 0.5f};

float Soldier::GetFieldCost(const GeographyInfo& info) const
{
    AIUnit* unit = Brain();
    int modeIndex = 0;
    if (unit)
    {
        modeIndex = unit->GetCombatMode() - CMSafe;
        saturate(modeIndex, 0, 3);
    }

    float cost = 1;
    // road fields are expected to be slightly faster
    if (info.u.road || info.u.track)
    {
        cost *= RoadPenalty[modeIndex];
    }

    if (info.u.forestOuter)
    {
        cost *= ForestPenalty[modeIndex];
    }
    else
    {
        // fields with objects will be passed through slower
        int nObj = info.u.howManyObjects;
        PoseidonAssert(nObj <= 3);
        cost *= ObjPenalty[modeIndex][nObj];
    }
    return cost;
}

float Soldier::GetCost(const GeographyInfo& geogr) const
{
    if (!(geogr.u.road || geogr.u.track))
    {
        if (geogr.u.waterDepth >= 2 || geogr.u.full && !geogr.u.forestOuter)
        {
            return 1e30f;
        }
    }
    float cost = InvFastRunSpeed;
    PoseidonAssert(geogr.u.howManyObjects <= 3);
    cost *= GradientPenalty(geogr.u.gradient);
    cost += (float)(int)geogr.u.waterDepth * 1.0f; // 1 m/s in water
    return cost;
}

float Soldier::GetCostTurn(int difDir) const
{
    if (difDir == 0)
    {
        return 0;
    }
    float aDir = fabs(difDir);
    float cost = aDir * 0.025f;
    if (difDir < 0)
    {
        return cost * 0.8f;
    }
    return cost;
}

float Soldier::GetTypeCost(OperItemType type) const
{
    switch (type)
    {
        case OITNormal:
        default:
            return 1;
        case OITSpaceBush:
            return 2;
        case OITSpaceTree:
        case OITSpace:
        case OITWater:
        case OITSpaceRoad:
            return SET_UNACCESSIBLE;
        case OITAvoidBush:
        case OITAvoidTree:
        case OITAvoid:
        {
            AIUnit* unit = Brain();
            if (unit)
            {
                int cmode = unit->GetCombatMode();
                PoseidonAssert(cmode >= CMCareless && cmode <= CMStealth);
                static const float costs[5] = {1.0, 1.0, 0.5, 0.25, 0.15};
                return costs[cmode - CMCareless];
            }
        }
    }
    return 1;
}

float Soldier::FireInRange(int weapon, float& timeToAim, const Target& target) const
{
    timeToAim = 0;
    Vector3 relDir = PositionWorldToModel(target.position);
    return FireAngleInRange(weapon, relDir);
}

void Soldier::Simulate(float deltaT, SimulationImportance prec)
{
    if (!_isDead)
    {
        if (!_ladderBuilding)
        {
            if (QIsManual())
            {
                if (GWorld->GetPlayerSuspended())
                {
                    SuspendedPilot(deltaT, prec);
                }
                else
                {
                    KeyboardPilot(deltaT, prec);
                }
            }
            else if (!IsLocal())
            {
                FakePilot(deltaT);
            }
            else if (Brain())
            {
                if (Brain()->GetAIDisabled() & AIUnit::DAAnim)
                {
                    DisabledPilot(deltaT, prec);
                }
                else
                {
                    AIPilot(deltaT, prec);
                }
            }
        }
    }
    else
    {
        // some actions are edge sensitive - must be executed
        if (_forceMove.id != MoveIdNone)
        {
            SetMoveQueue(MotionPathItem(_forceMove), prec <= SimulateVisibleFar);
        }
    }
    base::Simulate(deltaT, prec);
}

float Man::GetAimed(int weapon, Target* target) const
{
    if (weapon < 0 || weapon >= NMagazineSlots())
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
    if (WeaponsDisabled())
    {
        return 0;
    }
    float visible = _visTracker.Value(this, _currentWeapon, target->idExact, 0.9f);
    if (visible < 0.3f)
    {
        return 0;
    }

    if (target->lastSeen < Glob.time - 5)
    {
        return 0;
    }

    const Magazine* magazine = GetMagazineSlot(weapon)._magazine;
    const MagazineType* aInfo = magazine ? magazine->_type : nullptr;
    const WeaponModeType* mode = GetWeaponMode(weapon);
    const AmmoType* ammo = mode ? mode->_ammo : nullptr;

    if (target->posError.SquareSize() > Square(ammo->indirectHitRange * 2))
    {
        return 0;
    }

    Vector3 ap = target->AimingPosition();
    if (ammo && (ammo->_simulation != AmmoShotMissile || ammo->maxControlRange < 10))
    {
        switch (mode->_ammo->_simulation)
        {
            case AmmoShotTimeBomb:
            case AmmoShotMine:
            case AmmoShotPipeBomb:
            {
                // check if we are in good range
                float dist = ap.Distance(Position());
                float rad = target->idExact ? target->idExact->GetRadius() * 0.5f : 5;
                float range = (mode->_ammo->midRange * 0.7f + mode->_ammo->maxRange * 0.3f);
                return (dist < range + rad) * visible;
            }
        }

        // 0.6 visibility means 0.8 unaimed
        visible = 1 - (1 - visible) * 0.5f;

        // predict shot result
        Vector3 wPos = PositionModelToWorld(GetWeaponPoint(weapon));

        float dist = ap.Distance(wPos);
        float distFactor = dist < 40 ? 0 : dist - 40;
        dist += _aimInaccuracyDist * distFactor;
        saturateMax(dist, 20);

        float time = dist * aInfo->_invInitSpeed;
        float balFactor = 1;
        float leadFactor = 1;
        if (mode->_ammo->_simulation == AmmoShotMissile)
        {
            time = dist / mode->_ammo->maxSpeed + 0.1f;
            balFactor = 0.35f;
            leadFactor = 1.4f;
            if (mode->_ammo->maxControlRange > 10)
            {
                time += 0.3f;
            }
        }
        else if (mode->_ammo->_simulation == AmmoShotLaser)
        {
            balFactor = 0;
            leadFactor = 0;
            time = 0;
        }

        Vector3 leadSpeed = target->speed;
        LimitLead(leadSpeed, aInfo->_maxLeadSpeed * GetAbility());
        Vector3 estPos = ap + leadSpeed * time * leadFactor;

        Vector3 wDir = GetWeaponDirection(weapon);

        float eDist = estPos.Distance(wPos);

        Vector3 hit = wPos + wDir * eDist;
        hit[1] -= 0.5f * G_CONST * balFactor * time * time;

        Vector3 norm = (hit - wPos);
        hit = norm.Normalized() * eDist + wPos;

#if _ENABLE_CHEATS
        float distHit = hit.Distance(wPos);
#endif

        Vector3 hError = hit - estPos;

        if (aInfo && mode->_ammo->_simulation == AmmoShotShell)
        {
            if (aInfo->_initSpeed >= 30)
            {
                // grenade launcher - mistake in y-axis may have fatal results
                hError[1] *= 4;
            }
        }
        float error = hError.Size();
        float aimPrecision = (GetInvAbility() - 1) * 1.5f + 1;

        if (eDist < 70)
        {
            // when target is very near, increase ability
            float distAcc = 1 + floatMax(eDist * 1.0f / 70, 0.2f);
            // LOG_DEBUG(Physics, "aimPrecision {:.3f}, distAcc {:.3f}",aimPrecision,distAcc);
            saturateMin(aimPrecision, distAcc);
        }

        if (aInfo && mode->_ammo->_simulation == AmmoShotShell)
        {
            if (aInfo->_initSpeed < 30)
            {
                // hand grenade
                error *= 0.5f;
                // we are not able to throw grenade very precisely
                saturateMax(aimPrecision, 5);
            }
            // grenade launcher
        }
        else if (mode->_ammo->_simulation == AmmoShotMissile)
        {
            error *= 2;
        }
        else
        {
            error *= 0.75f;
        }

        float tgtSize = target->idExact->VisibleSize() * aimPrecision * 0.25f;
        const MuzzleType* muzzle = GetMagazineSlot(weapon)._muzzle;
        float dCoef = floatMax(muzzle->_aiDispersionCoefX, muzzle->_aiDispersionCoefY);
        tgtSize += dist * mode->_dispersion * dCoef * aimPrecision * 0.5f;

#if _ENABLE_CHEATS
        if ((Object*)this == GWorld->CameraOn() && CHECK_DIAG(DECombat))
        {
            GlobalShowMessage(2000,
                              "Error %.1f, tgtSize %.1f, time %.2f, "
                              "ePos %.1f,%.1f,%.1f, distErr %.1f",
                              error, tgtSize, time, hError[0], hError[1], hError[2], distHit - eDist);
        }
#endif

        if (error >= tgtSize * 2)
        {
            return 0;
        }
        if (!CheckFriendlyFire(weapon, target))
        {
            return 0;
        }
        if (error <= tgtSize)
        {
            return visible;
        }
        return (2 * tgtSize - error) / tgtSize * visible;
    }
    else
    {
        if (visible < 0.6f)
        {
            return 0;
        }

        // 0.6 visibility means 0.8 unaimed
        visible = 1 - (1 - visible) * 0.5f;
        Vector3 relPos = ap - Position();
        if (relPos.SquareSize() <= Square(30))
        {
            return 0; // minimum safe arming distance
        }
        Vector3Val wDir = GetWeaponDirection(weapon);

        float wepCos = relPos.CosAngle(wDir);
        float misAlignCoef = 30;
        float wepAimed = 1 - (1 - wepCos) * misAlignCoef;
        saturateMax(wepAimed, 0);

#if _ENABLE_CHEATS
        if ((Object*)this == GWorld->CameraOn() && CHECK_DIAG(DECombat))
        {
            GlobalShowMessage(2000, "AT visible %.3f, aimed %.3f", visible, wepAimed);
        }
#endif

        if (!CheckFriendlyFire(weapon, target))
        {
            return 0;
        }
        return visible * wepAimed;
    }
}

int Soldier::MissileIndex() const
{
    for (int i = 0; i < NMagazineSlots(); i++)
    {
        const WeaponModeType* mode = GetWeaponMode(i);
        const AmmoType* ammo = mode ? mode->_ammo : nullptr;
        if (ammo && ammo->_simulation == AmmoShotMissile)
        {
            return i;
        }
    }
    return -1;
}

void Soldier::AIFire(float deltaT)
{
#if _ENABLE_CHEATS
    extern bool disableUnitAI;
    if (disableUnitAI)
        return;
#endif
    AIUnit* unit = Brain();
    if (!unit)
    {
        return;
    }

    if (_lookTargetTimeLeft > 0)
    {
        _lookTargetTimeLeft -= deltaT;
    }
    else
    {
        _lookForwardTimeLeft -= deltaT;
        if (_lookForwardTimeLeft <= 0)
        {
            if (BinocularSelected())
            {
                _lookTargetTimeLeft = 4 + GRandGen.RandomValue() * 4;
                _lookForwardTimeLeft = 30 + GRandGen.RandomValue() * 5;
            }
            else if (unit->GetCombatMode() <= CMSafe || FindWeaponType(MaskSlotPrimary) < 0)
            {
                if (fabs(_walkSpeedWanted) < 0.1f)
                {
                    // safe and static - almost no need to look forward
                    _lookTargetTimeLeft = 4 + GRandGen.RandomValue() * 8;
                    _lookForwardTimeLeft = 2 + GRandGen.RandomValue() * 3;
                }
                else
                {
                    // safe, but walking - look forward often
                    _lookTargetTimeLeft = 4 + GRandGen.RandomValue() * 8;
                    _lookForwardTimeLeft = 3 + GRandGen.RandomValue() * 5;
                }
            }
            else
            {
                _lookTargetTimeLeft = 1 + GRandGen.RandomValue() * 2;
                _lookForwardTimeLeft = 15 + GRandGen.RandomValue() * 5;
            }
        }
    }

    SelectFireWeapon();
    if (!GetFireTarget() || !GetFireTarget()->idExact)
    {
        if (_forceFireWeapon < 0)
        {
            _gunXRotWanted = 0;
            _gunYRotWanted = 0;
            Vector3Val dir = unit->GetWatchHeadDirection();

            if (dir.SquareSize() > 0.1f)
            {
                AimHeadAI(dir, deltaT);
            }
            else
            {
                _headXRotWanted = 0;
                _headYRotWanted = 0;
            }
        }
        _laserTargetOn = false;
        return;
    }
    SelectWeapon(_fire._fireMode);
    AimWeaponAI(_fire._fireMode, GetFireTarget(), deltaT);
    if (_fire._fireMode >= 0)
    {
        if (_fire._fireMode >= NMagazineSlots())
        {
            LOG_ERROR(Physics, "{}: Bad weapon selected ({} of {})", (const char*)GetDebugName(), _fire._fireMode,
                      NMagazineSlots());
            _fire._fireMode = -1;
        }
        if (_fire._firePrepareOnly)
        {
            return;
        }
        if (GetWeaponLoaded(_fire._fireMode) && GetWeaponReady(_fire._fireMode, GetFireTarget()) &&
            GetAimed(_fire._fireMode, GetFireTarget()) >= 0.5f && !WeaponsDisabled())
        {
            if (!GetAIFireEnabled(GetFireTarget()))
            {
                ReportFireReady();
            }
            else
            {
                const WeaponModeType* mode = GetWeaponMode(_fire._fireMode);
                if (mode && mode->_useAction)
                {
                    UIAction action;
                    action.type = ATUseWeapon;
                    action.target = this;
                    action.param = _fire._fireMode;
                    action.param2 = 0;
                    action.priority = 0;
                    action.showWindow = false;
                    action.hideOnUse = false;
                    Ref<ActionContextUIAction> context = new ActionContextUIAction(action);
                    PlayAction(ManActPutDown, context);
                }
                else if (mode->_ammo && mode->_ammo->_simulation == AmmoShotLaser)
                {
                    Target* target = GetFireTarget();
                    if (!target->destroyed)
                    {
                        if (!_laserTargetOn)
                        {
                            FireWeapon(_fire._fireMode, target->idExact);
                        }
                        return;
                    }
                }
                else
                {
                    FireWeapon(_fire._fireMode, GetFireTarget()->idExact);
                }
            }
        }
    }
    _laserTargetOn = false;
}

bool RandomDecision(AIUnit* unit, float period, float ratio)
{
    int seed = unit->ID() * 2568;
    float timeMod = fastFmod(Glob.time.toFloat() + seed, period);
    return timeMod < ratio * period;
}

float SoldierStealthStanceExposure(AIGroup* group, bool holdingFire, int x, int z)
{
    AICenter* center = group ? group->GetCenter() : nullptr;
    if (!center)
        return 0.0f;
    return holdingFire ? center->GetExposurePessimistic(x, z) : center->GetExposureOptimistic(x, z);
}

#define DIAG_FIRE 0

void Soldier::DisabledPilot(float deltaT, SimulationImportance prec)
{
    _turnWanted = 0;
    _walkSpeedWanted = 0;

    AdvanceExternalQueue();

    if (_externalMove.id != MoveIdNone)
    {
        SetMoveQueue(_externalMove, prec <= SimulateVisibleFar);
    }
}

void Soldier::AIPilot(float deltaT, SimulationImportance prec)
{
    AdvanceExternalQueue();
    if (_externalMove.id != MoveIdNone)
    {
        _turnWanted = 0;
        _walkSpeedWanted = 0;
        SetMoveQueue(_externalMove, prec <= SimulateVisibleFar);
        return;
    }

#if _ENABLE_CHEATS
    if (this == GWorld->CameraOn())
    {
        __nop();
    }
    extern bool disableUnitAI;
    if (disableUnitAI)
        return;
#endif

    AIUnit* unit = Brain();
    if (!unit)
        return;
    if (!unit->GetSubgroup())
        return;
    bool isLeader = unit->IsSubgroupLeader();

    // Vector3Val speed=ModelSpeed();

    float headChange = 0;
    float speedWanted = 0;
    float turnPredict = 0;

    if (!_isDead)
    {
        AIFire(deltaT);
    }

    if (!_fire._fireTarget || _fire.GetTargetFinished(unit))
    {
        _fire._fireMode = -1;
        _fire._fireTarget = nullptr;
    }

#if DIAG_FIRE
    LOG_DEBUG(Physics, "Fire {} state {} prepare {}", _fireMode, _fireState, _firePrepareOnly);
#endif

    if (unit->GetState() == AIUnit::Stopping)
    {
        unit->SendAnswer(AI::StepCompleted);
        unit = Brain();
        speedWanted = 0;
        return;
    }
    else if (unit->GetState() == AIUnit::Stopped)
    {
        speedWanted = 0;
    }
    else if (_fire._fireMode >= 0 && (!_fire._firePrepareOnly || !unit->IsKeepingFormation()) &&
             _fireState != FireDone && _fire._fireTarget && _fire._fireTarget->IsKnownBy(unit) &&
             _fire._fireTarget->idExact && // ???
             unit->IsFireEnabled(_fire._fireTarget) && GetMagazineSlot(_fire._fireMode)._magazine &&
             GetMagazineSlot(_fire._fireMode)._magazine->_reloadMagazine <= 1)
    {
        // we must fire - stop
        // check if we can aim when we stop
        const WeaponModeType* mode = GetWeaponMode(_fire._fireMode);
        if (mode && mode->_ammo)
        {
            switch (mode->_ammo->_simulation)
            {
                case AmmoShotTimeBomb:
                case AmmoShotPipeBomb:
                    goto NormalMove;
            }
        }
        if (_fireState == FireInit)
        {
            if (Glob.time > _fireStateDelay)
            {
                float maxDist = 1000;
                if (mode && mode->_ammo)
                {
                    maxDist = mode->_ammo->midRange * 0.4f + mode->_ammo->maxRange * 0.6f;
                }
                float visVal = _visTracker.Value(this, _currentWeapon, _fire._fireTarget->idExact, 0.9f);
                if (visVal >= 0.5f && (Position() - _fire._fireTarget->AimingPosition()).SquareSize() < Square(maxDist))
                {
                    _fireState = FireAim;
                    _fireStateDelay = Glob.time + 20;
                    // LOG_DEBUG(Physics, "{}:    aim: visVal {:.3f}",(const char *)GetDebugName(),visVal);
                }
                else
                {
                    // retry init phase after some time
                    _fireStateDelay = Glob.time + 0.5;
                    // LOG_DEBUG(Physics, "{}: no aim: visVal {:.3f}",(const char *)GetDebugName(),visVal);
                    //_firePrepareOnly=true;
                }
            }
            goto NormalMove;
        }
        else if (_fireState == FireAim)
        {
            Vector3 aimDir = PositionWorldToModel(_fire._fireTarget->LandAimingPosition());
            headChange = atan2(aimDir.X(), aimDir.Z());
            speedWanted = 0;
            if (fabs(headChange) <= 0.05f)
            {
                float reloadTime = 10;
                if (_fire._fireMode >= 0 && mode)
                {
                    reloadTime = mode->_reloadTime;
                }
                _fireState = FireAimed, _fireStateDelay = Glob.time + reloadTime * 3 + 10;
                // LOG_DEBUG(Physics, "{}: Aimed",(const char *)GetDebugName());
            }
            else if (Glob.time > _fireStateDelay)
            {
                // fail
                _fireState = FireDone;
                _fireStateDelay = Glob.time + 10; // delay before next fire pause
                                                  // LOG_DEBUG(Physics, "{}: FireDone",(const char *)GetDebugName());
            }
        }
        else if (_fireState == FireAimed)
        {
            Vector3 aimDir = PositionWorldToModel(_fire._fireTarget->LandAimingPosition());
            headChange = atan2(aimDir.X(), aimDir.Z());
            speedWanted = 0;
            if (Glob.time > _fireStateDelay)
            {
#if DIAG_FIRE
                LOG_DEBUG(Physics, "{:x}: FireDone", (uintptr_t)this);
#endif
                _fireState = FireDone;
                _fireStateDelay = Glob.time + 5; // delay before next fire pause
                                                 // LOG_DEBUG(Physics, "{}: FireDone",(const char *)GetDebugName());
            }
        }
        else
        {
            Fail("FireState");
            goto NormalMove;
        }
        // if stopped and not aimed, continue
    }
    else
    {
        _fireState = FireDone;
    NormalMove:
        if (!isLeader)
        {
            FormationPilot(speedWanted, headChange, turnPredict);
        }
        else
        {
            LeaderPilot(speedWanted, headChange, turnPredict);
        }
        const Path& path = unit->GetPath();
        if (path.Size() > 0)
        {
            bool building = false;
            for (int i = 0; i < path.Size(); i++)
            {
                if (path[i]._house)
                {
                    building = true;
                    break;
                }
            }
            _inBuilding = building;
        }
    }

    AvoidCollision(deltaT, speedWanted, headChange);

    {
        float maxSpeed = Type()->GetMaxSpeedMs();
        float limitSpeed = Interpolativ(fabs(headChange), 0, H_PI / 4, maxSpeed, 0);
        saturate(speedWanted, -limitSpeed, +limitSpeed);
    }

    float turnWanted = headChange * 3;
    float delta = turnWanted - _turnWanted;
    saturate(delta, -5 * deltaT, +5 * deltaT);

    _turnWanted += delta;
    saturate(_turnWanted, -5, +5);

    ManPos minPos = ManPosLying;
    ManPos maxPos = ManPosStand;

    AIGroup* g = GetGroup();
    AISubgroup* sg = unit->GetSubgroup();

    float delay = 0.5f;

    if (IsLaunchDown())
    {
        delay = 3;
    }

    if (sg && sg->GetMode() == AISubgroup::DirectGo)
    {
        minPos = ManPosCombat; // disable lying - force run
    }
    else if (g && g->GetFlee())
    {
        minPos = ManPosCombat; // disable lying - force run
    }
    else if (LauncherFire())
    {
        // if RPG soldier is ready to fire, force weapon activation
        minPos = ManPosWeapon;
        maxPos = ManPosWeapon;
    }
    else if (LauncherSelected() && LauncherReady() && fabs(speedWanted) < 0.5f && unit->GetCombatMode() >= CMCombat &&
             LauncherWanted())
    {
        // check if we should have launcher prepared
        delay = 2;
        // then delay before giving it up
        minPos = ManPosWeapon;
        maxPos = ManPosWeapon;
    }
    else if (_unitPos == UPAuto)
    {
        if (unit->IsDanger() && !_inBuilding)
        {
            maxPos = ManPosLying; // force lying
        }
        else if (_hideBehind)
        {
            const Path& path = unit->GetPath();
            // check distance from target
            if (path.Size() >= 2)
            {
                float distance2 = path.End().Distance2(Position());
                if (distance2 > Square(5))
                {
                    if (distance2 > Square(30))
                    {
                        minPos = ManPosCombat;
                    }
                    maxPos = ManPosCombat; // enable lying, but do not force
                }
                else
                {
                    maxPos = ManPosLying; // force lying
                }
            }
            else if (speedWanted < 1.0f)
            {
                if (!IsDown() && RandomDecision(unit, 30, 0.7f))
                {
                    maxPos = ManPosCrouch;
                }
                else
                {
                    maxPos = ManPosLying;
                }
            }
        }
        else if (unit->GetCombatMode() == CMAware)
        {
            minPos = ManPosCombat;
            maxPos = ManPosCombat;
        }
        else if (unit->GetCombatMode() <= CMSafe)
        {
            minPos = ManPosStand;
            maxPos = ManPosStand;
        }
        else if (unit->GetCombatMode() == CMCombat)
        {
            // check speed wanted
            if (speedWanted < 2.0f && !_inBuilding)
            {
                // if we are not lying yet, consider crouching
                if (!IsDown() && RandomDecision(unit, 30, 0.3f))
                {
                    maxPos = ManPosCrouch;
                }
                else
                {
                    maxPos = ManPosLying;
                }
            }
            else
            {
                minPos = maxPos = ManPosCombat;
            }
        }
        else if (unit->GetCombatMode() == CMStealth)
        {
            // if there is no enemy known or expected, do not lay down

            int x = toIntFloor(Position().X() * InvLandGrid);
            int z = toIntFloor(Position().Z() * InvLandGrid);

            float exposure = SoldierStealthStanceExposure(GetGroup(), unit->IsHoldingFire(), x, z);

            if (exposure < 400 && speedWanted > 2.0f)
            {
                minPos = maxPos = ManPosCombat;
            }
            else
            {
                minPos = maxPos = ManPosLying;
            }
        }
    } // if (auto lay down)
    else if (_unitPos == UPUp)
    {
        minPos = ManPosCombat;
    }
    else if (_unitPos == UPDown)
    {
        maxPos = ManPosLying;
    }

    // check if we would like to use binocular

    if (maxPos == ManPosLying)
    {
        // LOG_DEBUG(Physics, "{}: force down",(const char *)GetDebugName());
        if (_fire._fireTarget && !_fire._firePrepareOnly)
        {
            // from some reason we are forced to lay down
            // we are aiming to some very near target - do not force lying down
            float dist2 = _fire._fireTarget->AimingPosition().Distance2(Position());
            if (dist2 < Square(60))
            {
                // we might crouch
                if (!_inBuilding && RandomDecision(unit, 60, 0.8f))
                {
                    maxPos = ManPosCrouch;
                }
                else
                {
                    maxPos = ManPosCombat;
                }
                // LOG_DEBUG(Physics, "  enable up");
            }
        }
    }

    bool forceStand = false;
    float slope = LandSlope(forceStand);
    if (!IsAbleToStand() || (!forceStand && !CanStand(slope)))
    {
        if (LauncherSelected())
        {
            minPos = ManPosWeapon;
        }
        else
        {
            minPos = ManPosLying;
        }
        maxPos = ManPosLying;
    }

    ActionMap* map = Type()->GetActionMap(_primaryMove.id);

    ManAction selAction = ManActN;

    int upDegree = map ? map->GetUpDegree() : ManPosStand;
    if (_posWanted < minPos || _posWantedTime > Glob.time + 30)
    {
        _posWanted = minPos;
        _posWantedTime = Glob.time + (GetInvAbility() * delay + delay * 0.5f) * GRandGen.RandomValue();
    }
    else if (_posWanted > maxPos)
    {
        _posWanted = maxPos;
        _posWantedTime = Glob.time + (GetInvAbility() * delay + delay * 0.5f) * GRandGen.RandomValue();
    }

    int primaryIndex = FindWeaponType(MaskSlotPrimary);
    int handGunIndex = FindWeaponType(MaskSlotHandGun);

    bool isWeapon = primaryIndex >= 0 || handGunIndex >= 0;
    if (!isWeapon)
    {
        if (_posWanted == ManPosCombat || _posWanted == ManPosStand)
        {
            _posWanted = ManPosNoWeapon;
            Time maxTime = Glob.time;
            if (_posWantedTime > maxTime)
            {
                _posWantedTime = maxTime;
            }
        }
        else if (_posWanted == ManPosLying)
        {
            _posWanted = ManPosLyingNoWeapon;
            Time maxTime = Glob.time;
            if (_posWantedTime > maxTime)
            {
                _posWantedTime = maxTime;
            }
        }
    }

    // LOG_DEBUG(Physics, "_posWanted {}",_posWanted);

    if (_posWanted == ManPosWeapon && _posWantedTime <= Glob.time)
    {
        selAction = ManActWeaponOn;
        // LOG_DEBUG(Physics, "{}: action weapon",(const char *)GetDebugName());
    }
    else if (BinocularSelected() && _lookTargetTimeLeft > 0)
    {
        selAction = ManActBinocOn;
    }
    else if (IsHandGunSelected() && IsPrimaryWeaponInMove())
    {
        selAction = ManActHandGunOn; // use hand gun
    }
    else if (!IsHandGunSelected() && IsHandGunInMove())
    {
        int actPos = GetActUpDegree();
        if (actPos == ManPosHandGunCrouch)
        {
            if (primaryIndex < 0)
            {
                selAction = ManActCivil;
            }
            else
            {
                selAction = ManActCrouch;
            }
        }
        else if (actPos == ManPosHandGunLying)
        {
            if (primaryIndex < 0)
            {
                selAction = ManActCivilLying;
            }
            else
            {
                selAction = ManActLying;
            }
        }
        else
        {
            PoseidonAssert(actPos == ManPosHandGunStand);
            if (primaryIndex < 0)
            {
                selAction = ManActCivil;
            }
            else
            {
                selAction = ManActCombat;
            }
        }
    }
    else if (upDegree != _posWanted && _posWantedTime <= Glob.time)
    {
        //_posWantedTime = TIME_MAX; // cancel request
        if (upDegree == ManPosWeapon)
        {
            if (_posWanted != ManPosWeapon)
            {
                selAction = IsHandGunSelected() ? ManActHandGunOn : ManActWeaponOff;
            }
            else
            {
                selAction = ManActWeaponOn;
            }
        }
        else if (_posWanted < ManPosNormalMin)
        {
        }
        else
        {
            if (_posWanted == ManPosLying)
            {
                if (upDegree != ManPosHandGunLying)
                {
                    if (IsHandGunSelected())
                    {
                        if (upDegree == ManPosHandGunCrouch || upDegree == ManPosHandGunStand)
                        {
                            selAction = ManActDown;
                        }
                        else
                        {
                            selAction = ManActHandGunOn;
                        }
                    }
                    else
                    {
                        selAction = ManActLying;
                    }
                }
            }
            else if (_posWanted == ManPosCrouch)
            {
                if (upDegree != ManPosWeapon && upDegree != ManPosHandGunCrouch)
                {
                    if (IsHandGunSelected())
                    {
                        if (upDegree == ManPosHandGunLying || upDegree == ManPosHandGunStand)
                        {
                            selAction = ManActDown;
                        }
                        else
                        {
                            selAction = ManActHandGunOn;
                        }
                    }
                    else
                    {
                        selAction = ManActCrouch;
                    }
                }
            }
            else if (_posWanted == ManPosCombat)
            {
                // note: some upDegrees may be valid
                if (upDegree != ManPosWeapon && upDegree != ManPosHandGunStand)
                {
                    if (IsHandGunSelected())
                    {
                        if (upDegree == ManPosHandGunLying)
                        {
                            selAction = ManActDown;
                        }
                        else if (upDegree == ManPosHandGunCrouch || upDegree == ManPosHandGunStand)
                        {
                            selAction = ManActUp;
                        }
                        else
                        {
                            selAction = ManActHandGunOn;
                        }
                    }
                    else
                    {
                        selAction = ManActCombat;
                    }
                }
            }
            else if (_posWanted == ManPosStand)
            {
                selAction = ManActStand;
            }
            else if (_posWanted == ManPosNoWeapon)
            {
                selAction = ManActCivil;
            }
            else if (_posWanted == ManPosLyingNoWeapon)
            {
                selAction = ManActCivilLying;
            }
            // LOG_DEBUG(Physics, "{}: action ch,   {}->{}",DNAME(),upDegree,_posWanted);
        }
    }
    if (selAction == ManActN)
    {
        selAction = ManActStop;
        // normal movement - check speedWanted (maybe also speedAside?)
        float limitFast = map ? map->GetLimitFast() : 5;
        float limitSlow = map ? map->GetLimitFast() * 0.6f : 1;

        if (!_canMoveFast || !CanSprint(slope) && !IsDown())
        {
            limitFast = 100; // never true
        }
        if (!CanRun(slope) && !IsDown())
        {
            limitSlow = 100; // never true
        }
        if (speedWanted > limitFast)
        {
            selAction = ManActFastF;
        }
        else if (speedWanted > limitSlow)
        {
            selAction = ManActSlowF;
        }
        else if (speedWanted > 0.1f)
        {
            selAction = ManActWalkF;
        }
        else if (speedWanted < -0.1f)
        {
            selAction = ManActWalkB;
        }
        else if (speedWanted < -limitSlow)
        {
            selAction = ManActSlowB;
        }
        if (speedWanted < -limitFast)
        {
            selAction = ManActFastB;
        }

        if (unit->GetCombatMode() == CMAware)
        {
            // when in aware, do not use walking - looks to much combat'ish
            if (selAction == ManActWalkF)
            {
                selAction = ManActSlowF;
            }
            if (selAction == ManActWalkB)
            {
                selAction = ManActSlowB;
            }
        }
    }

    if (selAction == ManActStop)
    {
        if (fabs(_turnWanted) > 0.5f)
        {
            selAction = _turnWanted < 0 ? ManActTurnL : ManActTurnR;
            if (unit->GetCombatMode() == CMAware)
            {
                ManAction relAction = _turnWanted < 0 ? ManActTurnLRelaxed : ManActTurnRRelaxed;
                if (map->GetAction(relAction) != MoveIdNone)
                {
                    selAction = relAction;
                }
            }
        }
        else if (unit->GetCombatMode() == CMAware)
        {
            if (map->GetAction(ManActStopRelaxed) != MoveIdNone)
            {
                selAction = ManActStopRelaxed;
            }
        }
    }

    MoveId moveWanted = map ? map->GetAction(selAction) : GetDefaultMove();

    if (_forceMove.id != MoveIdNone)
    {
        SetMoveQueue(_forceMove, prec <= SimulateVisibleFar);
    }
    else
    {
        SetMoveQueue(MotionPathItem(moveWanted), prec <= SimulateVisibleFar);
    }

    _walkSpeedWanted = speedWanted;
}

LSError Man::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar))

    if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
    {
        if (ar.IsSaving())
        {
            RString name = _primaryMove.id == MoveIdNone ? "" : Type()->GetMoveName(_primaryMove.id);
            PARAM_CHECK(ar.Serialize("primaryMove", name, 1, ""))
            name = _secondaryMove.id == MoveIdNone ? "" : Type()->GetMoveName(_secondaryMove.id);
            PARAM_CHECK(ar.Serialize("secondaryMove", name, 1, ""))
        }
        else if (ar.GetPass() == ParamArchive::PassFirst)
        {
            RString name;
            PARAM_CHECK(ar.Serialize("primaryMove", name, 1, ""))
            if (name.GetLength() == 0)
            {
                _primaryMove = MotionPathItem(MoveId(MoveIdNone));
            }
            else
            {
                _primaryMove = MotionPathItem(Type()->GetMoveId(name));
            }
            PARAM_CHECK(ar.Serialize("secondaryMove", name, 1, ""))
            if (name.GetLength() == 0)
            {
                _secondaryMove = MotionPathItem(MoveId(MoveIdNone));
            }
            else
            {
                _secondaryMove = MotionPathItem(Type()->GetMoveId(name));
            }
        }

        PARAM_CHECK(ar.Serialize("primaryFactor", _primaryFactor, 1, 1))
        PARAM_CHECK(ar.Serialize("moveTime", _primaryTime, 1, 1))
        PARAM_CHECK(ar.Serialize("secMoveTime", _secondaryTime, 1, 1))

        PARAM_CHECK(ar.Serialize("walkSpeedWanted", _walkSpeedWanted, 1, 0))

        PARAM_CHECK(ar.Serialize("whenScreamed", _whenScreamed, 1, TIME_MAX))

        PARAM_CHECK(ar.Serialize("tired", _tired, 1, 0))
        PARAM_CHECK(ar.Serialize("hideBody", _hideBody, 1, 0))
        PARAM_CHECK(ar.Serialize("hideBodyWanted", _hideBodyWanted, 1, 0))
        PARAM_CHECK(ar.SerializeEnum("unitPos", _unitPos, 1, (UnitPosition)UPAuto))

        PARAM_CHECK(ar.SerializeRef("flagCarrier", _flagCarrier, 1))
        PARAM_CHECK(ar.SerializeRef("laserTarget", _laserTarget, 1))
        PARAM_CHECK(ar.Serialize("laserTargetOn", _laserTargetOn, 1, false))

        PARAM_CHECK(ar.SerializeRef("ladderBuilding", _ladderBuilding, 1))
        PARAM_CHECK(ar.Serialize("ladderIndex", _ladderIndex, 1, -1))
        PARAM_CHECK(ar.Serialize("ladderPosition", _ladderPosition, 1, -1))

        PARAM_CHECK(ar.SerializeRefs("pipeBombs", _pipeBombs, 1))

        PARAM_CHECK(ar.Serialize("handGun", _handGun, 1, false))

        if (ar.IsLoading())
        {
            ScanNVG();
        }

        if (ar.IsLoading())
        {
            RecalcGunTransform();
            if (IsInLandscape())
            {
                RecalcPositions(*this);
            }
            else
            {
                _aimingPositionWorld = VZero;
                _cameraPositionWorld = VZero;
            }
        }
    }
    return LSOK;
}

NetworkMessageType Man::GetNMType(NetworkMessageClass cls) const
{
    switch (cls)
    {
        case NMCUpdateGeneric:
            return NMTUpdateMan;
        case NMCUpdatePosition:
            return NMTUpdatePositionMan;
        default:
            return base::GetNMType(cls);
    }
}

class IndicesUpdateMan : public IndicesUpdateVehicleBrain
{
    typedef IndicesUpdateVehicleBrain base;

  public:
    int hideBodyWanted;
    int tired;
    int walkSpeedWanted;
    int unitPos;
    int flagCarrier;
    int nvg;
    int ladderBuilding;
    int ladderIndex;
    int ladderPosition;

    IndicesUpdateMan();
    NetworkMessageIndices* Clone() const override { return new IndicesUpdateMan; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesUpdateMan::IndicesUpdateMan()
{
    hideBodyWanted = -1;
    tired = -1;
    walkSpeedWanted = -1;
    unitPos = -1;
    flagCarrier = -1;
    nvg = -1;
    ladderBuilding = -1;
    ladderIndex = -1;
    ladderPosition = -1;
}

void IndicesUpdateMan::Scan(NetworkMessageFormatBase* format)
{
    base::Scan(format);

    SCAN(hideBodyWanted)
    SCAN(tired)
    SCAN(walkSpeedWanted)
    SCAN(unitPos)
    SCAN(flagCarrier)
    SCAN(nvg)
    SCAN(ladderBuilding);
    SCAN(ladderIndex);
    SCAN(ladderPosition);
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesUpdateMan()
{
    using namespace Poseidon;
    return new IndicesUpdateMan();
}
namespace Poseidon
{

class IndicesUpdatePositionMan : public IndicesUpdatePositionVehicle
{
    typedef IndicesUpdatePositionVehicle base;

  public:
    int manUpdPos;
    int move;

    IndicesUpdatePositionMan();
    NetworkMessageIndices* Clone() const override { return new IndicesUpdatePositionMan; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesUpdatePositionMan::IndicesUpdatePositionMan()
{
    manUpdPos = -1;
    move = -1;
}

void IndicesUpdatePositionMan::Scan(NetworkMessageFormatBase* format)
{
    base::Scan(format);

    SCAN(manUpdPos)
    SCAN(move)
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesUpdatePositionMan()
{
    using namespace Poseidon;
    return new IndicesUpdatePositionMan();
}
namespace Poseidon
{

NetworkMessageFormat& Man::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    switch (cls)
    {
        case NMCUpdateGeneric:
            base::CreateFormat(cls, format);

            format.Add("hideBodyWanted", NDTFloat, NCTFloat0To1, DEFVALUE(float, 0),
                       DOC_MSG("Wanted state of body (1 .. fully hidden)"), ET_ABS_DIF, ERR_COEF_VALUE_MINOR);

            format.Add("tired", NDTFloat, NCTFloat0To1, DEFVALUE(float, 0), DOC_MSG("How man is tired"));
            format.Add("walkSpeedWanted", NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Wanted speed"));
            format.Add("unitPos", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, UPAuto), DOC_MSG("Up / down state"));
            format.Add("flagCarrier", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Carried flag"), ET_NOT_EQUAL,
                       ERR_COEF_MODE);
            format.Add("nvg", NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Night vision is active"), ET_ABS_DIF,
                       ERR_COEF_VALUE_MAJOR);

            format.Add("ladderBuilding", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Ladder ID"), ET_NOT_EQUAL,
                       ERR_COEF_MODE);
            format.Add("ladderIndex", NDTInteger, NCTSmallSigned, DEFVALUE(int, -1), DOC_MSG("Ladder ID"), ET_NOT_EQUAL,
                       ERR_COEF_VALUE_MAJOR);
            format.Add("ladderPosition", NDTFloat, NCTFloat0To1, DEFVALUE(float, 0), DOC_MSG("Position on ladder"),
                       ET_ABS_DIF, ERR_COEF_VALUE_MAJOR);

            break;
        case NMCUpdatePosition:
            base::CreateFormat(cls, format);
            format.Add("manUpdPos", NDTRawData, NCTNone, DEFVALUERAWDATA, DOC_MSG("Encoded position"), ET_UPD_MAN_POS,
                       1);
            format.Add("move", NDTString, NCTStringMove, DEFVALUE(RString, "Stand"), DOC_MSG("Current animation"),
                       ET_NOT_EQUAL, ERR_COEF_MODE);
            break;
        default:
            base::CreateFormat(cls, format);
            break;
    }
    return format;
}

TMError Man::TransferMsg(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
            TMCHECK(base::TransferMsg(ctx))
            {
                PoseidonAssert(dynamic_cast<const IndicesUpdateMan*>(ctx.GetIndices()))
                    const IndicesUpdateMan* indices = static_cast<const IndicesUpdateMan*>(ctx.GetIndices());

                ITRANSF(hideBodyWanted)
                ITRANSF(tired)
                ITRANSF(walkSpeedWanted)
                ITRANSF_ENUM(unitPos)
#if LOG_FLAG_CHANGES
                if (!ctx.IsSending())
                {
                    EntityAI* veh = _flagCarrier;
                    ITRANSF_REF(flagCarrier);
                    if (_flagCarrier != veh)
                        RptF("Flags: Remote %s: set flag carrier to %s", (const char*)GetDebugName(),
                             _flagCarrier ? (const char*)_flagCarrier->GetDebugName() : "nullptr");
                }
                else
                    ITRANSF_REF(flagCarrier);
#else
                ITRANSF_REF(flagCarrier)
#endif
                ITRANSF(nvg)
                ITRANSF_REF(ladderBuilding)
                ITRANSF(ladderIndex)
                ITRANSF(ladderPosition)
            }
            break;
        case NMCUpdatePosition:
            TMCHECK(base::TransferMsg(ctx))
            {
                PoseidonAssert(dynamic_cast<const IndicesUpdatePositionMan*>(ctx.GetIndices()))
                    const IndicesUpdatePositionMan* indices =
                        static_cast<const IndicesUpdatePositionMan*>(ctx.GetIndices());

                if (ctx.IsSending())
                {
                    NetworkUpdManPos pos;
                    pos.gunXRotWantedC = EncodeRot8b(_gunXRotWanted);
                    pos.gunYRotWantedC = EncodeRot8b(_gunYRotWanted);
                    pos.headXRotWantedC = EncodeRot8b(_headXRotWanted);
                    pos.headYRotWantedC = EncodeRot8b(_headYRotWanted);
                    TMCHECK(ctx.IdxSendRaw(indices->manUpdPos, &pos, sizeof(pos)));
                }
                else
                {
                    void* data;
                    int size;
                    TMCHECK(ctx.IdxGetRaw(indices->manUpdPos, data, size));
                    if (size == sizeof(NetworkUpdManPos))
                    {
                        const NetworkUpdManPos& pos = *(NetworkUpdManPos*)data;
                        _gunXRotWanted = DecodeRot8b(pos.gunXRotWantedC);
                        _gunYRotWanted = DecodeRot8b(pos.gunYRotWantedC);
                        _headXRotWanted = DecodeRot8b(pos.headXRotWantedC);
                        _headYRotWanted = DecodeRot8b(pos.headYRotWantedC);
                    }
                    else
                    {
                        Fail("Bad size of NetworkUpdManPos field");
                    }
                }

                if (ctx.IsSending())
                {
                    int n = _queueMove.Size();
                    MoveId id = (n == 0 ? _primaryMove.id : _queueMove[n - 1].id);
                    RString name = id < 0 ? "" : Type()->GetMoveName(id);
                    TMCHECK(ctx.IdxTransfer(indices->move, name))
                }
                else
                {
                    if (IsInLandscape())
                    {
                        RString name;
                        TMCHECK(ctx.IdxTransfer(indices->move, name))
                        if (name.GetLength() > 0)
                        {
                            ChangeMoveQueue(MotionPathItem(Type()->GetMoveId(name)));
                        }

                        RecalcPositions(*this);
                    }
                    else
                    {
                        _aimingPositionWorld = VZero;
                        _cameraPositionWorld = VZero;
                    }
                }
            }
            break;
        default:
            return base::TransferMsg(ctx);
    }
    return TMOK;
}

float Man::CalculateError(NetworkMessageContext& ctx)
{
    float error = 0;
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
        {
            error += base::CalculateError(ctx);

            PoseidonAssert(dynamic_cast<const IndicesUpdateMan*>(ctx.GetIndices())) const IndicesUpdateMan* indices =
                static_cast<const IndicesUpdateMan*>(ctx.GetIndices());

            ICALCERR_NEQREF(EntityAI, flagCarrier, ERR_COEF_MODE)
            ICALCERR_NEQREF(EntityAI, ladderBuilding, ERR_COEF_MODE)
            ICALCERRE_NEQ(int, ladderIndex, _ladderIndex, ERR_COEF_VALUE_MAJOR);
            ICALCERRE_NEQ(float, ladderPosition, _ladderPosition, ERR_COEF_VALUE_MAJOR);
        }
        break;
        case NMCUpdatePosition:
        {
            error += base::CalculateError(ctx);

            PoseidonAssert(dynamic_cast<const IndicesUpdatePositionMan*>(ctx.GetIndices()))
                const IndicesUpdatePositionMan* indices =
                    static_cast<const IndicesUpdatePositionMan*>(ctx.GetIndices());

            int n = _queueMove.Size();
            MoveId id = (n == 0 ? _primaryMove.id : _queueMove[n - 1].id);
            RString name = id < 0 ? "" : Type()->GetMoveName(id);
            ICALCERRE_NEQSTR(move, name, ERR_COEF_STRUCTURE)

            {
                AutoArray<char> temp;
                if (ctx.IdxTransfer(indices->manUpdPos, temp) == TMOK && temp.Size() == sizeof(NetworkUpdManPos))
                {
                    NetworkUpdManPos& d = *(NetworkUpdManPos*)temp.Data();
                    error += fabs(DecodeRot8b(d.headXRotWantedC) - _headXRotWanted) * ERR_COEF_VALUE_MINOR;
                    error += fabs(DecodeRot8b(d.headYRotWantedC) - _headYRotWanted) * ERR_COEF_VALUE_MINOR;
                    error += fabs(DecodeRot8b(d.gunXRotWantedC) - _gunXRotWanted) * ERR_COEF_VALUE_MINOR;
                    error += fabs(DecodeRot8b(d.gunYRotWantedC) - _gunYRotWanted) * ERR_COEF_VALUE_MINOR;
                }
            }
        }
        break;
        default:
            error += base::CalculateError(ctx);
            break;
    }
    return error;
}

void Man::SetFlagCarrier(EntityAI* veh)
{
    _flagCarrier = veh;

#if LOG_FLAG_CHANGES
    RptF("Flags: Local %s: set flag carrier to %s", (const char*)GetDebugName(),
         veh ? (const char*)veh->GetDebugName() : "nullptr");
#endif
}

void Man::ProcessGetIn()
{
    Fail("obsolete");
}

MovesTypeBank MovesTypes;
} // namespace Poseidon

namespace Poseidon::Foundation
{
template class Link<MovesType>;
template class Link<BlendAnimType>;
template class Ref<NetworkObject>;
} // namespace Poseidon::Foundation
