#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Core/Application.hpp>

#include <Poseidon/World/Entities/Vehicles/Air/Helicopter.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/Entities/Weapons/Shots.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/Dev/Diag/DiagModes.hpp>

#include <Poseidon/Game/UiActions.hpp>

#include <Random/randomGen.hpp>

#include <Poseidon/Network/Network.hpp>
#include <stdio.h>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Enums/EnumNames.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

namespace Poseidon
{
using namespace Foundation;
using Foundation::EnumName;

bool HelicopterAuto::AimObserver(Vector3Par direction)
{
    return true;
}

bool HelicopterAuto::AimWeapon(int weapon, Vector3Par direction)
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
    Vector3 relDir(VMultiply, DirWorldToModel(), direction);

    if (_turret.Aim(Type()->_turret, relDir))
    {
        CancelStop();
    }
    return true;
}

bool HelicopterAuto::AimWeapon(int weapon, Target* target)
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
    Vector3 tgtPos = target->AimingPosition();
    Vector3 weaponPos = Type()->_gunPos;

    const Magazine* magazine = GetMagazineSlot(weapon)._magazine;
    const MagazineType* aInfo = magazine ? magazine->_type : nullptr;
    const WeaponModeType* mode = GetWeaponMode(weapon);
    if (mode && mode->_ammo)
    {
        if (mode->_ammo->_simulation != AmmoShotMissile && mode->_ammo->_simulation != AmmoShotLaser)
        {
            float dist2 = tgtPos.Distance2(Position());
            float time2 = 0;
            if (aInfo)
            {
                time2 = dist2 * Square(aInfo->_invInitSpeed);
            }
            float time = sqrt(time2);
            float fall = 0.5 * G_CONST * time2;
            tgtPos[1] += fall;
            tgtPos += target->speed * (time + 0.25);
        }
    }

    const float predTime = 0.2;
    Vector3 myPos = PositionModelToWorld(weaponPos);
    tgtPos += target->speed * predTime;
    myPos += Speed() * predTime;

    return AimWeapon(weapon, tgtPos - myPos);
}

Vector3 HelicopterAuto::GetWeaponDirection(int weapon) const
{
    if (weapon < 0 || weapon >= NMagazineSlots())
    {
        return Direction();
    }
    const WeaponModeType* mode = GetWeaponMode(weapon);
    if (!mode || !mode->_ammo)
    {
        return Direction();
    }
    if (mode->_ammo->_simulation == AmmoShotMissile)
    {
        return Direction();
    }
    else if (mode->_ammo->_simulation == AmmoShotRocket)
    {
        return Direction();
    }
    else
    {
        return Transform().Rotate(GunTransform().Rotate(Type()->_gunDir));
    }
}

Vector3 HelicopterAuto::GetWeaponDirectionWanted(int weapon) const
{
    if (weapon < 0 || weapon >= NMagazineSlots())
    {
        return Direction();
    }
    const WeaponModeType* mode = GetWeaponMode(weapon);
    if (!mode || !mode->_ammo)
    {
        return Direction();
    }
    if (mode->_ammo->_simulation == AmmoShotMissile)
    {
        return Direction();
    }
    else if (mode->_ammo->_simulation == AmmoShotRocket)
    {
        return Direction();
    }
    else
    {
        Vector3 dir = Type()->_turret._dir;
        Matrix3Val aim = _turret.GetAimWanted();
        return Transform().Rotate(aim * dir);
    }
}

Vector3 HelicopterAuto::GetWeaponPoint(int weapon) const
{
    if (weapon < 0 || weapon >= NMagazineSlots())
    {
        return VZero;
    }
    const WeaponModeType* mode = GetWeaponMode(weapon);
    if (!mode)
    {
        return VZero;
    }
    if (!mode->_ammo)
    {
        return VZero;
    }
    switch (mode->_ammo->_simulation)
    {
        case AmmoShotRocket:
            return !_rocketLRToggle ? Type()->_rocketLPos : Type()->_rocketRPos;
        case AmmoShotMissile:
        {
            int count = GetMagazineSlot(weapon)._magazine->_ammo;
            bool found;
            Vector3 pos = FindMissilePos(count, found);
            if (!found)
            {
                pos = !_missileLRToggle ? Type()->_missileLPos : Type()->_missileRPos;
            }
            return pos;
        }
        case AmmoShotBullet:
        case AmmoShotLaser:
        {
            Matrix4Val shootTrans = GunTransform();
            return shootTrans.FastTransform(Type()->_gunPos);
        }
        default:
            return VZero;
    }
}

Vector3 HelicopterAuto::GetWeaponCenter(int weapon) const
{
    const WeaponModeType* mode = GetWeaponMode(weapon);
    if (!mode || !mode->_ammo)
    {
        return Direction();
    }
    if (mode->_ammo->_simulation == AmmoShotMissile)
    {
        return base::GetWeaponCenter(weapon);
    }
    else if (mode->_ammo->_simulation == AmmoShotRocket)
    {
        return base::GetWeaponCenter(weapon);
    }
    else
    {
        return _turret.GetCenter(Type()->_turret);
    }
}

float HelicopterAuto::GetAimed(int weapon, Target* target) const
{
    if (!target)
    {
        return 0;
    }
    if (!target->idExact)
    {
        return 0;
    }
    if (weapon < 0)
    {
        return 0;
    }
    float visible = _visTracker.Value(this, _currentWeapon, target->idExact);
    const WeaponModeType* mode = GetWeaponMode(weapon);
    if (!mode || !mode->_ammo)
    {
        return 0;
    }

    visible = 1 - (1 - visible) * 0.5f;

    if (mode->_ammo->_simulation == AmmoShotMissile)
    {
        if (mode->_ammo->maxControlRange > 10)
        {
            Vector3 relPos = PositionWorldToModel(target->AimingPosition());
            if (relPos.Z() <= 50)
            {
                return 0;
            }
            if (fabs(relPos.X()) > relPos.Z())
            {
                return 0;
            }
            if (fabs(relPos.Y()) > relPos.Z())
            {
                return 0;
            }
            float invRZ = 1.0 / relPos.Z();
            float lockX = 1 - fabs(relPos.X()) * invRZ;
            float lockY = 1 - fabs(relPos.Y()) * invRZ;
            float lock = floatMin(lockX, lockY);
            saturate(lock, 0, 1);
            lock *= visible;
            if (lock < 0.5)
            {
                lock = 0;
            }
            return lock;
        }
        else
        {
            Vector3 relPos = PositionWorldToModel(target->AimingPosition());
            if (relPos.Z() <= 30)
            {
                return 0;
            }
            float xError = fabs(relPos.X());
            float yError = fabs(relPos.Y());
            float tgtSize = target->idExact->GetShape()->GeometrySphere();
            float error = floatMax(xError, yError);
            float maxError = mode->_ammo->indirectHitRange * 4.5;
            if (error > tgtSize + maxError)
            {
                return 0;
            }
            float lock = 1 - (error - tgtSize) / (tgtSize + maxError);
            saturate(lock, 0, 1);
            return lock * visible;
        }
    }
    else
    {
        const Magazine* magazine = GetMagazineSlot(weapon)._magazine;
        const MagazineType* aInfo = magazine ? magazine->_type : nullptr;
        Vector3 ap = target->AimingPosition();
        float dist = ap.Distance(Position());
        float time = dist * aInfo->_invInitSpeed;
        Vector3 estPos = ap + target->speed * time;
        Vector3 wDir = GetWeaponDirection(weapon);
        Vector3 wPos = PositionModelToWorld(GetWeaponCenter(weapon));
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
        return (error < maxError) * visible;
    }
}

void HelicopterAuto::DammageCrew(EntityAI* killer, float howMuch, RString ammo)
{
    AIUnit* commander = CommanderUnit();
    if (commander)
    {
        if (GetRawTotalDammage() >= 0.7f && commander->GetCombatMode() >= CMCombat)
        {
            Time goTime = Glob.time + GRandGen.PlusMinus(2.0f, 1.0f);
            if (goTime < _getOutAfterDammage)
            {
                _getOutAfterDammage = goTime;
            }
        }
    }

    base::DammageCrew(killer, howMuch, ammo);
}

void HelicopterAuto::Eject(AIUnit* unit)
{
    // check height
    float surfaceY = GLOB_LAND->SurfaceYAboveWater(Position()[0], Position()[2]);
    float height = Position().Y() - surfaceY;
    bool parachute = (height > 30);
    unit->ProcessGetOut(parachute);
}

void HelicopterAuto::FakePilot(float deltaT)
{
    _forceDive = 1;
}

void HelicopterAuto::JoystickDirPilot(float deltaT)
{
    auto& input = InputSubsystem::Instance();
    _pilotSpeedHelper = false;
    _pilotDirHelper = false;
    _avoidBankJitter = false;

    _bankWanted = input.GetStickLeft();
    _diveWanted = -input.GetStickForward();
    _backRotorWanted = -input.GetStickRudder();

    Limit(_bankWanted, -0.4, 0.4);
    Limit(_diveWanted, -0.7, 0.45);
    saturate(_backRotorWanted, -1, +1);
}

void HelicopterAuto::JoystickHeightPilot(float deltaT)
{
    auto& input = InputSubsystem::Instance();
    _pilotHeightHelper = false;
    float collective0 = input.GetStickThrust();
    float collective = collective0;
    // range: -1..1 in → -0.5..1 out
    _mainRotorWanted = collective * (1.5f / 2) + 0.5f;
    saturate(_mainRotorWanted, -0.5f, 1);

    float surfaceY = GLOB_LAND->SurfaceYAboveWater(Position()[0], Position()[2]);
    _pilotHeight = Position().Y() - surfaceY;
}

void HelicopterAuto::SuspendedPilot(AIUnit* unit, float deltaT)
{
    float surfaceY = GLOB_LAND->SurfaceYAboveWater(Position()[0], Position()[2]);
    _pilotHeight = Position().Y() - surfaceY;
    _pilotSpeed = VZero;
}

void HelicopterAuto::DetectControlMode() const
{
    static const UserAction moveActions[] = {UAMoveForward, UAMoveBack, UAMoveFastForward};
    static const UserAction turnActions[] = {UAMoveForward, UAMoveBack, UAMoveFastForward, UAMoveLeft,
                                             UAMoveRight,   UATurnLeft, UATurnRight};
    static const UserAction cursorActions[] = {UALookLeftDown, UALookDown,   UALookRightDown,
                                               UALookLeft,     UALookCenter, UALookRight,
                                               UALookLeftUp,   UALookUp,     UALookRightUp};
    static const UserAction thrustActions[] = {
        UAMoveUp,
        UAMoveDown,
    };

    const int nMoveActions = sizeof(moveActions) / sizeof(*moveActions);
    const int nTurnActions = sizeof(turnActions) / sizeof(*turnActions);
    const int nCursorActions = sizeof(cursorActions) / sizeof(*cursorActions);
    const int nThrustActions = sizeof(thrustActions) / sizeof(*thrustActions);
    DetectControlModeActions(moveActions, nMoveActions, turnActions, nTurnActions, cursorActions, nCursorActions,
                             thrustActions, nThrustActions);
}

void HelicopterAuto::KeyboardPilot(AIUnit* unit, float deltaT)
{
    auto& input = InputSubsystem::Instance();
    constexpr InputContext ctx = InputContext::HeliPilot;
    _dirCompensate = 0; // low heading compensation
    _forceDive = 1;
    _forceBank = 1;

    if (input.IsJoystickThrustActive())
    {
        JoystickHeightPilot(deltaT);
    }
    else
    {
        _pilotHeightHelper = true;

        Vector3Val position = Position();
        float surfaceY = GLOB_LAND->SurfaceYAboveWater(position[0], position[2]);
        float curHeight = position.Y() - surfaceY;
        const float predictTime = 2.0;
        float moveUp = input.GetAction(ctx, UAMoveUp);
        if (moveUp)
        {
            if (!EngineIsOn())
            {
                EngineOn();
            }

            if (!_pressedUp)
            {
                _pilotHeight = curHeight, _pressedUp = true;
            }
            _pilotHeight += deltaT * 20 * moveUp;
        }
        else
        {
            if (_pressedUp)
            {
                _pilotHeight = curHeight + _speed[1] * predictTime, _pressedUp = false;
            }
        }
        float moveDown = input.GetAction(ctx, UAMoveDown);
        if (moveDown)
        {
            if (!_pressedDown)
            {
                _pilotHeight = curHeight, _pressedDown = true;
            }
            _pilotHeight -= deltaT * 20 * moveDown;
        }
        else
        {
            if (_pressedDown)
            {
                _pilotHeight = curHeight + _speed[1] * predictTime, _pressedDown = false;
            }
        }
        float canLand = 1;
        saturateMin(canLand, 2 - _speed.SizeXZ() * 0.1); // no landing in high speed
        // saturateMin(canLand,1.5-_rotorSpeed); // no landing with rotor on
        saturateMax(canLand, 0);
        // Limit(_pilotHeight,4-canLand*3,150);
        // Limit(_pilotHeight,4-canLand*3,150);
        saturateMax(_pilotHeight, 4 - canLand * 3);
        if (USER_CONFIG.easyMode && GWorld->GetMode() != GModeNetware)
        {
            saturateMin(_pilotHeight, 150);
        }
        else
        {
            saturateMin(_pilotHeight, 10000);
        }
    }

    _pilotSpeed[1] = 0;
    _pilotSpeed[0] = 0;

    if (input.IsJoystickActive())
    {
        JoystickDirPilot(deltaT);
    }
    else
    {
        bool internalCamera = IsGunner(GWorld->GetCameraType());
        if (internalCamera && input.IsMouseTurnActive() && !input.IsLookAroundEnabled())
        {
            _pilotHeading = atan2(_mouseDirWanted[0], _mouseDirWanted[2]);

            _pilotDirHelper = true;
            _avoidBankJitter = false;

            if (!_hoveringAutopilot)
            {
                _pilotDive = _mouseDirWanted[1];
                saturate(_pilotDive, -0.7, +0.7);
                _diveWanted = _pilotDive;
                _pilotSpeedHelper = false;
            }
            else
            {
                _pilotSpeedHelper = true;
                _pilotDive = 0;
                _pilotSpeed[0] = (input.GetAction(ctx, UATurnRight) - input.GetAction(ctx, UATurnLeft)) * 3;
                _pilotSpeed[2] = -_mouseDirWanted[1] * 20;
                saturate(_pilotSpeed[2], -5, +7);
            }
        }
        else
        {
            _avoidBankJitter = true;

            _backRotorWanted = input.GetAction(ctx, UAMoveLeft) - input.GetAction(ctx, UAMoveRight);
            saturate(_backRotorWanted, -1, +1);

            if (!_hoveringAutopilot)
            {
                _pilotDirHelper = false;
                _pilotSpeedHelper = false;
                _bankWanted = -0.5 * (input.GetAction(ctx, UATurnRight) - input.GetAction(ctx, UATurnLeft));

                float forward = input.GetAction(ctx, UAMoveForward) + input.GetAction(ctx, UAMoveFastForward) * 2 -
                                input.GetAction(ctx, UAMoveBack);

                float dive = Direction().Y() - _rotorDive;

                if (fabs(forward) > 0.1)
                {
                    _pilotDive = dive - forward;
                    _pilotDiveSet = false;
                }
                else if (!_pilotDiveSet)
                {
                    _pilotDiveSet = true;
                    _pilotDive = dive;
                }
                saturate(_pilotDive, -0.7, +0.7);

                _diveWanted = _pilotDive;
            }
            else
            {
                _pilotDirHelper = fabs(_backRotorWanted) < 0.1;

                if (!_pilotDirHelper)
                {
                    _pilotHeading = atan2(Direction()[0], Direction()[2]);
                }
                _pilotSpeedHelper = true;

                float forward = input.GetAction(ctx, UAMoveForward) * 0.5f + input.GetAction(ctx, UAMoveFastForward) -
                                input.GetAction(ctx, UAMoveBack) * 0.5f;

                _pilotSpeed[0] = (input.GetAction(ctx, UATurnRight) - input.GetAction(ctx, UATurnLeft)) * 3;
                _pilotSpeed[2] = forward * 7;
                _pilotDive = 0;
                _bankWanted = 0;
            }
        }

        Limit(_pilotSpeed[2], -10, +Type()->GetMaxSpeedMs());
    }
}

void HelicopterAuto::AvoidGround(float minHeight)
{
    Point3 estimate = Position();
    float maxUnder = 0;
    for (int i = 0; i < 2; i++)
    {
        float estY = GLOB_LAND->SurfaceYAboveWater(estimate.X(), estimate.Z());
        estY += minHeight;
        float estUnder = estY - estimate.Y();
        saturateMax(maxUnder, estUnder);
        estimate += _speed * 3.0;
    }
    if (maxUnder > 0)
    {
        float maxSpeed = Interpolativ(maxUnder, 0, 10, Type()->GetMaxSpeedMs(), 0);
        Limit(_pilotSpeed[2], -maxSpeed, maxSpeed);
    }
}

void HelicopterAuto::BrakingManeuver()
{
    _pilotHeading = atan2(-Speed().X(), -Speed().Z());
    float curHeading = atan2(Direction().X(), Direction().Z());
    float headChange = AngleDifference(_pilotHeading, curHeading);

    // once chopper is turned is desired direction, it may start braking
    float brakeDive = floatMax(0, 1 - headChange * (0.5f / H_PI));
    _forceDive = brakeDive;
    saturate(_forceDive, -0.6f, -0.1f);
}

void HelicopterAuto::Autopilot(float deltaT, Vector3Par target, Vector3Par tgtSpeed, // target
                               Vector3Par direction, Vector3Par speed                // wanted values
)
{
    float avoidGround = 0;
    Vector3Val position = Position();
    Vector3 absDistance = target - position;
    Vector3 distance = DirectionWorldToModel(absDistance);

    float bank = DirectionAside().Y();
    float dive = Direction().Y();

    float sizeXZ2 = distance.SquareSizeXZ();
    switch (_state)
    {
        default: // case AutopilotFar:
        {
            Vector3 absDirection = absDistance + direction * (tgtSpeed.Size() * 20);
            _pilotHeading = atan2(absDirection.X(), absDirection.Z());
            avoidGround = 30;
            _pilotHeight = avoidGround;
            Vector3 relSpeed = DirectionWorldToModel(_speed - tgtSpeed);
#define BRAKE_SEC 3.0
            if (tgtSpeed.SquareSize() < 20 * 30                         // target is slow
                && (_speed.SquareSize() - speed.SquareSize()) > 40 * 40 // we will need to brake
            )
            {
                if (distance[2] - relSpeed[2] * BRAKE_SEC < 0 && sizeXZ2 < Square(450))
                {
                    // we are aproaching the target, start braking
                    _state = AutopilotBrake;
                }
            }
            else if (sizeXZ2 < 200 * 200)
            {
                _state = AutopilotNear;
            }
            _pilotSpeed[0] = 0;
            _pilotSpeed[1] = 0;
            _pilotSpeed[2] = Type()->GetMaxSpeedMs() * 0.8;
        }
        break;
        case AutopilotBrake:
        {
            Vector3 relSpeed = DirectionWorldToModel(_speed - tgtSpeed);
            _pilotSpeed[0] = 0;
            _pilotSpeed[1] = 0;
            avoidGround = 30;
            _pilotHeight = avoidGround;
            BrakingManeuver();
            if (sizeXZ2 > Square(550))
            {
                _state = AutopilotFar;
            }
            else if (tgtSpeed.SquareSize() > 40 * 40) // target is too fast
            {
                _state = AutopilotNear;
            }
            else if (Speed().SquareSize() < Square(Type()->GetMaxSpeedMs() * 0.25f))
            {
                // if we are moving slow, we may stop braking
                _state = AutopilotNear;
            }
        }
        break;
        case AutopilotNear:
        {
            Vector3 absDirection =
                absDistance + direction * (tgtSpeed.Size() * 10); // "lead target" - est. target position
            _pilotHeading = atan2(absDirection.X(), absDirection.Z());

            float targetAbove = target.Y() - GLOB_LAND->SurfaceYAboveWater(position[0], position[2]);
            avoidGround = floatMax(30, targetAbove);
            _pilotHeight = avoidGround;
            _pilotSpeed[0] = 0;
            _pilotSpeed[1] = 0;
            if (tgtSpeed.SquareSize() < Square(5))
            {
                float fast = sqrt(sizeXZ2) * (1.0 / 500);
                Limit(fast, 0, 1);
                float wantedSpeed = Type()->GetMaxSpeedMs() * fast * 0.5 + 1;
                float currentSpeed = ModelSpeed()[2];
                _pilotSpeed[2] = wantedSpeed;
                if (sizeXZ2 < Square(50)                     // near enough
                    && speed.Distance2(tgtSpeed) < Square(5) // there is a chance to align
                                                             // never assume aligned when target is moving fast
                )
                {
                    _state = AutopilotAlign;
                }
                else if (currentSpeed > wantedSpeed * 1.5 && currentSpeed > 45)
                {
                    // moving too fast - initiate braking maneuver
                    _state = AutopilotBrake;
                }
            }
            else
            {
                Vector3 relTgtPos = PositionWorldToModel(target);
                const float timeToReach = 10;
                _pilotSpeed[2] = relTgtPos.Z() * (1.0 / timeToReach);
                if (distance[2] > fabs(distance[0]) * 2 && sizeXZ2 > 300 * 300)
                {
                    // far away, heading to target, target is moving slow
                    _state = AutopilotFar;
                }
            }
        }
        break;
        case AutopilotAlign:
        case AutopilotReached:
        {
            _pilotHeading = atan2(direction.X(), direction.Z());
            float sizeXZ = sqrt(sizeXZ2);
            float highX = Interpolativ(sizeXZ, 5, 50, 2, 30);
            float highZ = highX;
            // control to be there in estT sec
            const float estT = 4.0;
            Vector3Val estSpeed = DirectionWorldToModel(_speed + 2.0 * _acceleration);
            float estSpeedZ = estSpeed.Z();
            float estSpeedX = estSpeed.X();
            Vector3Val estPosA = Position() + _speed * estT + 0.5 * estT * estT * _acceleration;
            Vector3Val estTgt = target + tgtSpeed * estT;

            // Vector3 tgtPos=DirectionWorldToModel(estTgt-estPos);
            Vector3 tgtPos = DirectionWorldToModel(estTgt - estPosA);

#if _ENABLE_CHEATS
            if (CHECK_DIAG(DEPath))
            {
                Vector3Val estPos = Position() + _speed * estT; //+0.5*estT*estT*_acceleration;
                {
                    Ref<Object> obj = new ObjectColored(GScene->Preloaded(SphereModel), -1);
                    obj->SetPosition(estPos);
                    obj->SetScale(1);
                    obj->SetConstantColor(PackedColor(Color(1, 0, 0)));
                    GLandscape->ShowObject(obj);
                }
                {
                    Ref<Object> obj = new ObjectColored(GScene->Preloaded(SphereModel), -1);
                    obj->SetPosition(estPosA);
                    obj->SetScale(0.75);
                    obj->SetConstantColor(PackedColor(Color(1, 1, 0)));
                    GLandscape->ShowObject(obj);
                }
            }
#endif

            if (_speed.SquareSizeXZ() < 2 && fabs(dive) < 0.1 && fabs(bank) < 0.1)
            {
                if (fabs(tgtPos[2]) < 2)
                {
                    tgtPos[2] = 0, highZ = 0;
                }
                if (fabs(tgtPos[0]) < 2)
                {
                    tgtPos[0] = 0, highX = 0;
                }
            }

            float high = floatMax(highX, highZ);
            if (high < 4.0f)
            {
                _state = AutopilotReached;
            }

            if (tgtSpeed.SquareSize() < Square(8))
            {
                float high = floatMax(highX, highZ);
                float highSpeed = Interpolativ(_speed.Distance(speed), 0.2, 20, 2, 30);
                saturateMax(high, highSpeed);
            }
            else
            {
                _state = AutopilotNear;
            }

            float targetAbove = target.Y() - GLOB_LAND->SurfaceYAboveWater(position[0], position[2]);

            if (_state == AutopilotReached)
            {
                avoidGround = -0.5;
                _pilotHeight = targetAbove;
            }
            else
            {
                avoidGround = high;
                _pilotHeight = floatMax(high, targetAbove);
            }

            float apDive = dive - tgtPos.Z() * 0.05;
            float apBank = bank - tgtPos.X() * 0.05;

            // if speed is high enough, do not accelerate
            const float maxZSpd = 1;
            const float maxXSpd = 1;
            if (ModelSpeed().Z() > +maxZSpd)
            {
                saturateMax(apDive, 0); // no negative
            }
            if (ModelSpeed().Z() < -maxZSpd)
            {
                saturateMin(apDive, 0); // no positive
            }
            if (ModelSpeed().X() > +maxXSpd)
            {
                saturateMax(apBank, 0);
            }
            if (ModelSpeed().X() < -maxXSpd)
            {
                saturateMin(apBank, 0);
            }

            if (estSpeedZ > 0)
            {
                saturateMax(apDive, -0.1); // little negative
            }
            if (estSpeedZ < 0)
            {
                saturateMin(apDive, +0.1); // little positive
            }
            if (estSpeedX > 0)
            {
                saturateMax(apBank, -0.1);
            }
            if (estSpeedX < 0)
            {
                saturateMin(apBank, +0.1);
            }

            float posLimit = floatMax(tgtPos.SizeXZ() * (1.0f / 4) - 0.2f, 0);
            float spdLimit = floatMax(speed.SizeXZ() * (1.0f / 4) - 0.2f, 0);
            float maxDive = spdLimit * 0.3f + posLimit * 0.2f;
            saturateMin(maxDive, 0.5f);

            saturate(apDive, -maxDive, +maxDive);
            saturate(apBank, -maxDive, +maxDive);

            _forceDive = apDive;
            _forceBank = apBank;

            // AutopilotAlign/Reached uses forceDive/forceBank; pilotSpeed is zeroed for MP prediction
            _pilotSpeed = VZero;

#if _ENABLE_CHEATS
            if (CHECK_DIAG(DEPath) && GLOB_WORLD->CameraOn() == this)
            {
                GlobalShowMessage(500,
                                  "spd %.1f,%.1f, mspd %.1f,%.1f espd %.1f,%.1f, tPos %.1f,%.1f "
                                  "d %.2f->ad %.2f, b %.2f->ab %.2f",
                                  _pilotSpeed[0], _pilotSpeed[2], ModelSpeed()[0], ModelSpeed()[2], estSpeed[0],
                                  estSpeed[2], tgtPos.X(), tgtPos.Z(), dive, apDive, bank, apBank);
            }
#endif

            if (sizeXZ2 > Square(90) || tgtSpeed.Distance2(speed) >= 10 * 10)
            {
                _state = AutopilotNear;
            }
        }
        break;
    }
    _pilotSpeed += DirectionWorldToModel(speed);
    Limit(_pilotSpeed[2], -10, Type()->GetMaxSpeedMs());
    if (avoidGround > 0)
    {
        AvoidGround(avoidGround);
    }
}

void HelicopterAuto::ResetAutopilot()
{
    _state = AutopilotNear;
}

bool HelicopterAuto::FireWeapon(int weapon, TargetType* target)
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

    const Magazine* magazine = GetMagazineSlot(weapon)._magazine;
    if (!magazine)
    {
        return false;
    }
    const MagazineType* aInfo = magazine ? magazine->_type : nullptr;
    const WeaponModeType* mode = GetWeaponMode(weapon);
    PoseidonAssert(mode);
    if (!mode->_ammo)
    {
        return false;
    }
    bool fired = false;
    switch (mode->_ammo->_simulation)
    {
        case AmmoShotRocket:
        {
            _rocketLRToggle = !_rocketLRToggle;
            Vector3Val pos = (_rocketLRToggle ? Type()->_rocketLPos : Type()->_rocketRPos);
            fired = FireMissile(weapon, pos, VForward, Vector3(0, 0, aInfo->_initSpeed), target);
        }
        break;
        case AmmoShotMissile:
        {
            _missileLRToggle = !_missileLRToggle;

            int count = GetMagazineSlot(weapon)._magazine->_ammo;
            bool found;
            Vector3 pos = FindMissilePos(count, found);
            if (!found)
            {
                pos = (_missileLRToggle ? Type()->_missileLPos : Type()->_missileRPos);
            }
            fired = FireMissile(weapon, pos, VForward, Vector3(0, 0, aInfo->_initSpeed), target);
        }
        break;
        case AmmoShotBullet:
        {
            Matrix4Val shootTrans = GunTransform();
            fired =
                FireMGun(weapon, shootTrans.FastTransform(Type()->_gunPos), shootTrans.Rotate(Type()->_gunDir), target);
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
        VehicleWithAI::FireWeapon(weapon, target);
        return true;
    }
    return false;
}

void HelicopterAuto::FireWeaponEffects(int weapon, const Magazine* magazine, EntityAI* target)
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
            case AmmoShotRocket:
            case AmmoShotMissile:
            case AmmoNone:
                break;
            case AmmoShotBullet:
                _mGunClouds.Start(0.1);
                _mGunFire.Start(0.1, 0.4, true);
                _mGunFireFrames = 1;
                _mGunFireTime = Glob.uiTime;
                int newPhase;
                while ((newPhase = toIntFloor(GRandGen.RandomValue() * 3)) == _mGunFirePhase)
                {
                    ;
                }
                _mGunFirePhase = newPhase;
                break;
        }
    }

    base::FireWeaponEffects(weapon, magazine, target);
}

float HelicopterAuto::GetFieldCost(const GeographyInfo& info) const
{
    return 1;
}

float HelicopterAuto::GetCost(const GeographyInfo& geogr) const
{
    float cost = Type()->GetMinCost();
    if (geogr.u.waterDepth >= 2)
    {
        cost *= 0.8;
    }
    else if (geogr.u.waterDepth >= 1)
    {
        cost *= 0.9;
    }
    int grad = geogr.u.gradient;
    PoseidonAssert(grad <= 7);
    static const float gradPenalty[8] = {1.0, 1.02, 1.05, 1.2, 1.3, 1.5, 1.7, 2.0};
    cost *= gradPenalty[grad];
    return cost;
}

float HelicopterAuto::GetCostTurn(int difDir) const
{ // in sec
    if (difDir == 0)
    {
        return 0;
    }
    float aDir = fabs(difDir);
    float cost = aDir * 0.15 + aDir * aDir * 0.02;
    if (difDir < 0)
    {
        return cost * 0.8;
    }
    return cost;
}

float HelicopterAuto::FireInRange(int weapon, float& timeToAim, const Target& target) const
{
    timeToAim = 0;
    return 1;
}

float HelicopterAuto::FireAngleInRange(int weapon, Vector3Par rel) const
{
    if (rel.Y() > 0)
    {
        return 0;
    }
    float size2 = rel.SquareSizeXZ();
    float y2 = Square(rel.Y());
    const float maxY = 0.25;
    if (y2 > size2 * Square(maxY))
    {
        return 0;
    }
    // nearly same level
    float invSize = InvSqrt(size2);
    return 1 - rel.Y() * invSize * (1 / maxY);
}

void HelicopterAuto::AIGunner(AIUnit* unit, float deltaT)
{
    PoseidonAssert(unit);

    if (!GetFireTarget())
    {
        return;
    }

    if (!_fire._fireTarget || _fire.GetTargetFinished(unit))
    {
        _fire._fireMode = -1;
        _fire._fireTarget = nullptr;
        return;
    }

    AimWeapon(_currentWeapon, GetFireTarget());

    if (_currentWeapon < 0)
    {
        return;
    }
    if (_fire._firePrepareOnly)
    {
        return;
    }

    if (GetWeaponLoaded(_currentWeapon) && GetAimed(_currentWeapon, GetFireTarget()) >= 0.7 &&
        GetWeaponReady(_currentWeapon, GetFireTarget()))
    {
        if (!GetAIFireEnabled(GetFireTarget()))
        {
            ReportFireReady();
        }
        else
        {
            FireWeapon(_currentWeapon, GetFireTarget()->idExact);
            _fireState = FireDone;
            _fireStateDelay = Glob.time + 5; // leave some time to recover
        }
    }
}

void HelicopterAuto::MoveWeapons(float deltaT)
{
    AIUnit* unit = GunnerUnit();
    if (!unit)
    {
        _turret.Stop(Type()->_turret);
    }
    else
    {
        _turret._gunStabilized = true;
        _turret.MoveWeapons(Type()->_turret, unit, deltaT);
    }
}

void HelicopterAuto::UpdateStopMode(AIUnit* unit)
{
    if (unit->GetState() == AIUnit::Stopping || unit->GetState() == AIUnit::Stopped)
    {
        _getinUnits.Compact();
        _getoutUnits.Compact();
        bool getin = _getinUnits.Size() > 0;
        bool getout = _getoutUnits.Size() > 0;
        bool supply = _supplyUnits.Size() > 0;
        bool pilot = false;
        for (int i = 0; i < _getoutUnits.Size(); i++)
        {
            if (_getoutUnits[i] == unit)
            {
                pilot = true;
                break;
            }
        }
        StopMode mode = SMNone;
        if (pilot || _landing == LMLand)
        {
            mode = SMLand;
        }
        else if (getin || supply || _landing == LMGetIn)
        {
            mode = SMGetIn;
        }
        else if (getout || _landing == LMGetOut)
        {
            mode = SMGetOut;
        }

        if (mode != _stopMode || unit->GetState() == AIUnit::Stopping && !unit->CheckEmpty(_stopPosition))
        {
            _stopMode = mode;
            FindStopPosition();
        }
    }
    else
    {
        _stopMode = SMNone;
    }
}

void HelicopterAuto::FindStopPosition()
{
    AIUnit* unit = DriverBrain();
    if (!unit)
    {
        Fail("No pilot");
        return;
    }

    bool found = false;

    Vector3 bestPos = Position();

    // preferred position lies in front of us
    Vector3 preferredPos = Position() + Speed() * 6;
    {
        VehicleNonAIType* heliHType = VehicleTypes.New("HeliH");
        float minDist2 = Square(500);

        for (int i = 0; i < GWorld->NBuildings(); i++)
        {
            Vehicle* veh = GWorld->GetBuilding(i);
            if (!veh)
            {
                continue;
            }
            const VehicleNonAIType* type = veh->GetNonAIType();
            if (!type)
            {
                continue;
            }
            if (!type->IsKindOf(heliHType))
            {
                continue;
            }
            float dist2 = Position().Distance2(veh->Position());
            if (dist2 >= minDist2)
            {
                continue;
            }
            // check if H is free
            if (!unit->CheckEmpty(veh->Position()))
            {
                continue;
            }
            // ok
            minDist2 = dist2;
            bestPos = veh->Position();
            found = true;
        }
    }

    if (!found)
    {
        // unlock while searching
        PerformUnlock();

        float bestCost = 1e10;
        int range = 2;
        do
        {
            // check neighbourghodd
            // increase range if necessary
            int x = toIntFloor(preferredPos.X() * InvLandGrid);
            int z = toIntFloor(preferredPos.Z() * InvLandGrid);
            for (int xx = x - range; xx <= x + range; xx++)
            {
                for (int zz = z - range; zz <= z + range; zz++)
                {
                    GeographyInfo info = GLOB_LAND->GetGeography(xx, zz);
                    float cost = 0;
                    if (info.u.waterDepth > 0)
                    {
                        continue;
                    }
                    if (info.u.full)
                    {
                        continue;
                    }
                    if (info.u.howManyObjects)
                    {
                        continue;
                    }
                    int grad = info.u.gradient;
                    if (_stopMode == SMLand)
                    {
                        if (grad >= 4)
                        {
                            continue;
                        }
                        const static int gradPenalty[4] = {0, 5, 10, 30};
                        cost += gradPenalty[grad];
                        if (info.u.road)
                        {
                            cost += 10;
                        }
                    }
                    else
                    {
                        if (grad >= 5)
                        {
                            continue;
                        }
                        const static int gradPenalty[5] = {0, 0, 2, 5, 10};
                        cost += gradPenalty[grad];
                        if (info.u.road)
                        {
                            cost += 2;
                        }
                    }
                    cost += sqrt((xx - x) * (xx - x) + (zz - z) * (zz - z)) * 0.5; // small penalty for distance
                    Vector3 pos, normal;
                    pos.Init();
                    normal.Init();
                    pos[0] = xx * LandGrid + LandGrid * 0.5, pos[2] = zz * LandGrid + LandGrid * 0.5;
                    pos[1] = GLOB_LAND->RoadSurfaceYAboveWater(pos[0], pos[2]);
                    // check for object collision at given place
                    if (bestCost > cost)
                    {
                        // AIUnit *unit=DriverBrain();
                        if (!AIUnit::FindFreePosition(pos, normal, false, this))
                        {
                            continue;
                        }
                        bestPos = pos;
                        bestCost = cost;
                    }
                }
            }
        } while (bestCost > 10 && (range += 2) < 8);

        PerformLock();
    }

    if ((_stopPosition - bestPos).SquareSize() > Square(4))
    {
        // reset only when there is significant change
        ResetAutopilot();
    }
    _stopPosition = bestPos;
}

void HelicopterAuto::AIPilot(AIUnit* unit, float deltaT)
{
    PoseidonAssert(unit);
    PoseidonAssert(unit->GetSubgroup());
    if (!unit->GetSubgroup())
    {
        return;
    }
    bool isLeader = unit->IsSubgroupLeader();

    _pilotSpeedHelper = true;
    _pilotDirHelper = true;
    _pilotHeightHelper = true;

    _dirCompensate = 1;
    _avoidBankJitter = false;

    Vector3 steerPos = SteerPoint(2.0, 4.0);

    Vector3 steerWant = PositionWorldToModel(steerPos);

    float headChange = atan2(steerWant.X(), steerWant.Z());
    float speedWanted = 0;

    float inCombat = 2 - _nearestEnemy * (1.0 / 400);
    saturate(inCombat, 0, 1);
    speedWanted = Type()->GetMaxSpeedMs() * inCombat * 0.5;

    if (unit->GetCombatMode() <= CMSafe)
    {
        inCombat = 0;
    }

    Target* assigned = unit->GetTargetAssigned();
    if (assigned && _sweepTarget != assigned && Type()->_enableSweep)
    {
        _sweepTarget = assigned;
        _sweepState = SweepDisengage;
        _sweepDelay = Glob.time + 10;
    }

    if (_sweepTarget)
    {
        EntityAI* swAI = _sweepTarget->idExact;
        if (unit->GetCombatMode() <= CMSafe || !unit->IsFireEnabled(_sweepTarget))
        {
            _sweepTarget = nullptr;
        }
        // check if target is alive
        else if (
            // destroyed
            !swAI ||
            swAI->IsDammageDestroyed()
            // or not enemy and not ordered to fire
            || _sweepTarget->State(unit) < TargetEnemy && unit->GetEnableFireTarget() != _sweepTarget)
        {
            _sweepTarget = nullptr;
        }
    }

    _forceDive = 1;
    _forceBank = 1;

    UpdateStopMode(unit);

    bool autopilot = false;
    if (unit->GetState() == AIUnit::Stopping || unit->GetState() == AIUnit::Stopped)
    {
        Vector3 sPos = _stopPosition;
        if (_stopMode == SMGetIn)
        {
            sPos[1] += 1.0;
        }
        else if (_stopMode == SMGetOut)
        {
            sPos[1] += 2.0;
        }

        // direction - opposite to wind
        Vector3Val windDir = GLandscape->GetWind();

        float windSize = windDir.Size();

        Vector3 landDir = Direction();
        if (windSize > 0.5)
        {
            float windFactor = windSize * 0.3;
            saturate(windFactor, 0, 0.5);

            landDir = windDir * windFactor + Direction() * (1 - windFactor);
            landDir[1] = 0;
            landDir.Normalize();
        }

        Autopilot(deltaT, _stopPosition, VZero, landDir, VZero);
        speedWanted = 0;

        if (_landContact)
        {
            if (_stopMode == SMLand)
            {
                StopRotor();
                if (_rotorSpeed < 0.7)
                {
                    UpdateStopTimeout();
                    // note: Pilot may get out - Brain may be nullptr
                    unit->SendAnswer(AI::StepCompleted);
                    if (unit->IsFreeSoldier())
                    {
                        return;
                    }
                }
            }
            if (unit->GetState() == AIUnit::Stopping)
            {
                UpdateStopTimeout();
                // note: Pilot may get out - Brain may be nullptr
                unit->SendAnswer(AI::StepCompleted);
                if (unit->IsFreeSoldier())
                {
                    return;
                }
            }
        }

        float bottomY = Position().Y() + _shape->GeometryLevel()->Min().Y();
        switch (_stopMode)
        {
            case SMLand:
                autopilot = true;
                if (_state == AutopilotReached)
                {
                    float curSurfaceY = GLOB_LAND->RoadSurfaceYAboveWater(Position().X(), Position().Z());
                    if (Position().Y() <= curSurfaceY + 2.5f || bottomY < curSurfaceY + 0.5f)
                    {
                        StopRotor();
                    }
                }
                break;
            case SMGetIn:
            case SMGetOut:
                if (_state == AutopilotReached)
                {
                    if (Position().Y() <= _stopPosition.Y() + 2.5f || bottomY < _stopPosition.Y() + 0.5f)
                    {
                        if (unit->GetState() == AIUnit::Stopping)
                        {
                            UpdateStopTimeout();
                            unit->SendAnswer(AI::StepCompleted);
                            PoseidonAssert(!unit->IsFreeSoldier());
                        }
                    }
                }
                autopilot = true;
                break;
        }
    }
    else if (_sweepTarget && Type()->_enableSweep)
    {
        bool laserTarget = _sweepTarget->idExact->GetType()->GetLaserTarget();
        speedWanted = laserTarget ? 0 : Type()->GetMaxSpeedMs() * 0.5f;
        Vector3 relPos = _sweepTarget->AimingPosition() - Position();
        float distXZ = relPos.SizeXZ();
        const float safeDistance = laserTarget ? 400 : 250;
        if (_sweepState == SweepDisengage)
        {
            Vector3 aimDir = PositionWorldToModel(_sweepTarget->AimingPosition());
            headChange = atan2(aimDir.X(), aimDir.Z());

            // move - to be hard target
            if (distXZ > safeDistance || fabs(headChange) < 0.5f)
            {
                _sweepState = SweepEngage;
                _sweepDelay = Glob.time + 25;
            }
            headChange = 0;
        }
        else if (_sweepState == SweepEngage)
        {
            Vector3 aimDir = PositionWorldToModel(_sweepTarget->AimingPosition());
            headChange = atan2(aimDir.X(), aimDir.Z());
            // move - to be hard target
            if (distXZ < 100)
            {
                _sweepState = SweepFire;
                _sweepDelay = Glob.time + 5; // start immediatelly
                _sweepDir = _sweepTarget->AimingPosition() - Position();
            }

            if (aimDir.Z() > 20 && fabs(headChange) < 0.2f)
            { // if the target is horizontally aimed, make vertical adjust
                float minSpeed = laserTarget ? Type()->GetMaxSpeedMs() * 0.3f : 0;
                float speed = ModelSpeed()[2];
                if (speed > minSpeed)
                { // we are flying fast enough
                    // actual aiming (using speed)
                    Vector3 relPos = _sweepTarget->AimingPosition() - Position();
                    _forceDive = (relPos.Y() - 3) * relPos.InvSizeXZ();
                    saturate(_forceDive, -0.7f, 0);
                }
            }
        }
        else if (_sweepState == SweepFire)
        {
            Vector3 relSweepDir = DirectionWorldToModel(_sweepDir);
            headChange = atan2(relSweepDir.X(), relSweepDir.Z());
            if (fabs(headChange) > 0.6 && (distXZ < 50 || distXZ > 150))
            {
                _sweepState = SweepDisengage;
                _sweepDelay = Glob.time + 10; // start immediatelly
            }
        }

        // sweep target should be slightly below
        float wantAbove = distXZ * 0.1;
        saturate(wantAbove, 30, 50);
        float wantY = _sweepTarget->AimingPosition().Y() + wantAbove;

        float curSurfaceY = GLOB_LAND->SurfaceYAboveWater(Position().X(), Position().Z());
        _pilotHeight = wantY - curSurfaceY;

        // when firing at laser target, do not fly too high
        saturate(_pilotHeight, 30, laserTarget ? 60 : 200);

        if (Glob.time > _sweepDelay)
        {
            _sweepTarget = nullptr;
        }
    }
    else if (!isLeader)
    {
        AIUnit* leader = unit->GetSubgroup()->Leader();
        if (!EngineIsOn() && leader)
        {
            VehicleWithAI* veh = leader->GetVehicle();
            if (veh->Airborne() || veh->Position().Distance2(Position()) > Square(200))
            {
                EngineOn();
            }
        }
        unit->ForceReplan();
        _limitSpeed = GetType()->GetMaxSpeedMs() * 1.5;

        if (leader)
        {
            Vector3Val relFormWanted = unit->GetFormationRelative() - leader->GetFormationRelative();

            VehicleWithAI* leaderVeh = leader->GetVehicle();

            Matrix4 formTransform;
            formTransform.SetDirectionAndUp(leaderVeh->Direction(), VUp);
            formTransform.SetPosition(leaderVeh->Position());

            Vector3 formPos = formTransform.FastTransform(relFormWanted);

            Autopilot(deltaT, formPos, leaderVeh->Speed(), leaderVeh->Direction(), leaderVeh->Speed());
            autopilot = true;

            Vector3Val leaderPos = leaderVeh->Position();
            float leaderSurfY = GLandscape->SurfaceYAboveWater(leaderPos.X(), leaderPos.Z());
            _pilotHeight = leaderPos.Y() - leaderSurfY;
            saturateMax(_pilotHeight, _defPilotHeight);
        }
        else
        {
            Autopilot(deltaT, Position(), VZero, Direction(), VZero);
        }
    }
    else
    {
        speedWanted = _limitSpeed;

#if DIAG_SPEED
        if (this == GWorld->CameraOn())
        {
            LOG_DEBUG(Physics, "Basic speed {:.1f}", speedWanted * 3.6);
        }
#endif

        const Path& path = unit->GetPath();
        if (path.Size() >= 2)
        {
            EngineOn();

            float precision = GetPrecision();
            //_moveMode=gotoNormal;
            float cost = path.CostAtPos(Position());
            Vector3 pos = path.PosAtCost(cost, Position());

            float distEnd2 = Position().Distance2(path.End());
            float dist2 = (Position() - pos).SquareSizeXZ();
            // check if we have first point of Plan complete
            if (distEnd2 < Square(precision) || cost > path.EndCost())
            {
                unit->SendAnswer(AI::StepCompleted);
                //				unit->ForceReplan();
            }
            if (dist2 > Square(precision * 3))
            {
                unit->SendAnswer(AI::StepTimeOut);
            }
            _pilotHeight = _defPilotHeight;
        }
        else
        {
            speedWanted = 0;
            saturateMin(_pilotHeight, _defPilotHeight);
        }

        if (_stratGoToPos.SquareSize() > 0.1f)
        {
            // strategic target known
            float finalDist2 = (_stratGoToPos - Position()).SquareSizeXZ();
            if (finalDist2 < Square(1000))
            {
                float maxSpd = 60;
                if (finalDist2 < Square(500))
                {
                    maxSpd = 40;
                    if (finalDist2 < Square(300))
                    {
                        maxSpd = 20;
                        if (finalDist2 < Square(150))
                        {
                            maxSpd = 10;
                        }
                    }
                }
                saturateMin(speedWanted, maxSpd);
            }
        }

        saturate(speedWanted, -_limitSpeed, +_limitSpeed); // move max. by given speed
        if (inCombat >= 0.3f)
        {
            float combatSpeed = Type()->GetMaxSpeedMs() * inCombat * 0.5f;
            saturateMax(speedWanted, combatSpeed);
        }
    }

    PoseidonAssert(unit);

    AvoidCollision(deltaT, speedWanted, headChange);

    if (!autopilot)
    {
        float avoidGround = 0.5f;
        float speedSize = fabs(ModelSpeed().Z());
        saturateMax(avoidGround, speedSize * 0.35f);

        _pilotSpeed = Vector3(0, 0, speedWanted);

        if (avoidGround > 0)
        {
            AvoidGround(avoidGround);
        }

        if (ModelSpeed().Z() - speedWanted > Type()->GetMaxSpeedMs() * 0.5f && speedWanted >= 0)
        {
            BrakingManeuver();
        }
        else
        {
            float maxTurn = H_PI;
            if (inCombat > 0.1)
            {
                const float maxTurnInCombat = 0.3f;
                const float maxTurnNoCombat = 0.9f;
                maxTurn = (maxTurnInCombat - maxTurnNoCombat) * inCombat + maxTurnNoCombat;
            }
            saturate(headChange, -maxTurn, +maxTurn);
            float curHeading = atan2(Direction()[0], Direction()[2]);
            _pilotHeading = curHeading + headChange;
        }
    }
}

void HelicopterAuto::DrawDiags()
{
    if (CommanderUnit())
    {
        LODShapeWithShadow* forceArrow = GScene->ForceArrow();

        if (_stopPosition.SquareSize() > 0.5)
        {
            Ref<Object> obj = new ObjectColored(GScene->Preloaded(SphereModel), -1);
            obj->SetPosition(_stopPosition);
            obj->SetScale(2);
            obj->SetConstantColor(PackedColor(Color(1, 0, 1)));
            GLandscape->ShowObject(obj);
        }

#if 1
        {
            Matrix3 rotY(MRotationY, -_pilotHeading);
            Vector3 pilotDir = rotY.Direction();
            Ref<Object> arrow = new Object(forceArrow, -1);
            Point3 pos = Position() + Vector3(0, 5, 0);

            float size = 0.6;
            arrow->SetPosition(pos);
            arrow->SetOrient(pilotDir, VUp);
            arrow->SetPosition(arrow->PositionModelToWorld(forceArrow->BoundingCenter() * size));
            arrow->SetScale(size);
            arrow->SetConstantColor(PackedColor(Color(1, 1, 0)));

            GScene->ObjectForDrawing(arrow);
        }
#endif
    }

    base::DrawDiags();
}

RString HelicopterAuto::DiagText() const
{
    RString text = base::DiagText();
    char buf[256];
    snprintf(buf, sizeof(buf), " pSpd=%.1f, MRW=%.2f, ", _pilotSpeed[2] * 3.6, _mainRotorWanted);
    if (_state != AutopilotNear)
    {
        text = text + FindEnumName(_state);
    }
    return text + RString(buf);
}

LSError Helicopter::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar))

#define SERIAL(name) PARAM_CHECK(ar.Serialize(#name, _##name, 1))
#define SERIAL_DEF(name, value) PARAM_CHECK(ar.Serialize(#name, _##name, 1, value))

    if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
    {
        SERIAL_DEF(rotorPosition, 0);
        SERIAL_DEF(rotorSpeed, 1);
        SERIAL_DEF(rotorSpeedWanted, 1);

        SERIAL_DEF(backRotor, 0);
        SERIAL_DEF(backRotorWanted, 0);
        SERIAL_DEF(mainRotor, 0);
        SERIAL_DEF(mainRotorWanted, 0);
        SERIAL_DEF(cyclicForward, 0);
        SERIAL_DEF(cyclicForwardWanted, 0);
        SERIAL_DEF(cyclicAside, 0);
        SERIAL_DEF(cyclicAsideWanted, 0);
        SERIAL_DEF(rotorDive, 0);
        SERIAL_DEF(rotorDiveWanted, 0);

        PARAM_CHECK(_turret.Serialize(ar))
    }
    /*
        SERIAL_DEF(gunYRot,0);SERIAL_DEF(gunYRotWanted,0);
        SERIAL_DEF(gunXRot,0);SERIAL_DEF(gunXRotWanted,0);
        SERIAL_DEF(gunXSpeed,0);SERIAL_DEF(gunYSpeed,0);
    */

    return LSOK;
}

template <>
const EnumName* Foundation::GetEnumNames(AutopilotState dummy)
{
    static const EnumName AutopilotStateNames[] = {
        EnumName(AutopilotFar, "FAR"),     EnumName(AutopilotBrake, "BRAKE"),     EnumName(AutopilotNear, "NEAR"),
        EnumName(AutopilotAlign, "ALIGN"), EnumName(AutopilotReached, "REACHED"), EnumName()};
    return AutopilotStateNames;
}

RString HelicopterAuto::GetActionName(const UIAction& action)
{
    switch (action.type)
    {
        case ATAutoHover:
            if (_hoveringAutopilot)
            {
                return LocalizeString(IDS_ACTION_HOVER_CANCEL);
            }
            else
            {
                return LocalizeString(IDS_ACTION_HOVER);
            }
    }
    return base::GetActionName(action);
}

void HelicopterAuto::PerformAction(const UIAction& action, AIUnit* unit)
{
    switch (action.type)
    {
        case ATAutoHover:
            _hoveringAutopilot = !_hoveringAutopilot;
            return;
    }
    base::PerformAction(action, unit);
}

void HelicopterAuto::GetActions(UIActions& actions, AIUnit* unit, bool now)
{
    if (unit && unit == DriverBrain() && QIsManual())
    {
        actions.Add(ATAutoHover, this, 0.9);
    }

    base::GetActions(actions, unit, now);
}

bool HelicopterAuto::IsStopped() const
{
    // check manual heli stopped
    // check height above surface
    if (_landContact || _objectContact)
    {
        return base::IsStopped();
    }

    // high flying heli cannot be stopped
    float surfaceY = GLOB_LAND->SurfaceYAboveWater(Position()[0], Position()[2]);
    float height = Position().Y() - surfaceY;
    if (height > 5)
    {
        return false;
    }

    return base::IsStopped();
}

LSError HelicopterAuto::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar))
    if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
    {
        PARAM_CHECK(ar.Serialize("defPilotHeight", _defPilotHeight, 1, 50))
        PARAM_CHECK(ar.Serialize("_pilotHeight", _pilotHeight, 1, 2.5))
        PARAM_CHECK(ar.Serialize("_pilotSpeed", _pilotSpeed, 1, VZero))
        PARAM_CHECK(ar.Serialize("_stopPosition", _stopPosition, 1, VZero))
        PARAM_CHECK(ar.SerializeEnum("state", _state, 1, AutopilotNear))
    }

    return LSOK;
}

NetworkMessageType HelicopterAuto::GetNMType(NetworkMessageClass cls) const
{
    switch (cls)
    {
        case NMCCreate:
            return NMTCreateHelicopter;
        case NMCUpdateGeneric:
            return NMTUpdateHelicopter;
        case NMCUpdatePosition:
            return NMTUpdatePositionHelicopter;
        default:
            return base::GetNMType(cls);
    }
}

class IndicesCreateHelicopter : public IndicesCreateVehicle
{
    typedef IndicesCreateVehicle base;

  public:
    int rotorSpeed;

    IndicesCreateHelicopter();
    NetworkMessageIndices* Clone() const override { return new IndicesCreateHelicopter; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesCreateHelicopter::IndicesCreateHelicopter()
{
    rotorSpeed = -1;
}

void IndicesCreateHelicopter::Scan(NetworkMessageFormatBase* format)
{
    base::Scan(format);

    SCAN(rotorSpeed)
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesCreateHelicopter()
{
    using namespace Poseidon;
    return new IndicesCreateHelicopter();
}
namespace Poseidon
{

class IndicesUpdateHelicopter : public IndicesUpdateTransport
{
    typedef IndicesUpdateTransport base;

  public:
    int rotorSpeedWanted;
    int state;
    int pilotHeight;
    int pilotSpeed;
    int stopMode;
    int stopPosition;
    int pilotSpeedHelper;
    int pilotHeightHelper;
    int pilotDirHelper;

    IndicesUpdateHelicopter();
    NetworkMessageIndices* Clone() const override { return new IndicesUpdateHelicopter; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesUpdateHelicopter::IndicesUpdateHelicopter()
{
    rotorSpeedWanted = -1;
    state = -1;
    pilotHeight = -1;
    pilotSpeed = -1;
    stopMode = -1;
    stopPosition = -1;
    pilotSpeedHelper = -1;
    pilotHeightHelper = -1;
    pilotDirHelper = -1;
}

void IndicesUpdateHelicopter::Scan(NetworkMessageFormatBase* format)
{
    base::Scan(format);

    SCAN(rotorSpeedWanted)
    SCAN(state)
    SCAN(pilotHeight)
    SCAN(pilotSpeed)
    SCAN(stopMode)
    SCAN(stopPosition)

    SCAN(pilotSpeedHelper)
    SCAN(pilotHeightHelper)
    SCAN(pilotDirHelper)
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesUpdateHelicopter()
{
    using namespace Poseidon;
    return new IndicesUpdateHelicopter();
}
namespace Poseidon
{

class IndicesUpdatePositionHelicopter : public IndicesUpdatePositionVehicle
{
    typedef IndicesUpdatePositionVehicle base;

  public:
    int turret;
    int backRotorWanted;
    int mainRotorWanted;
    int cyclicForwardWanted;
    int cyclicAsideWanted;
    int rotorDiveWanted;
    int bankWanted;
    int diveWanted;
    int pilotHeading;
    int pilotDive;

    IndicesUpdatePositionHelicopter();
    NetworkMessageIndices* Clone() const override { return new IndicesUpdatePositionHelicopter; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesUpdatePositionHelicopter::IndicesUpdatePositionHelicopter()
{
    turret = -1;
    backRotorWanted = -1;
    mainRotorWanted = -1;
    cyclicForwardWanted = -1;
    cyclicAsideWanted = -1;
    rotorDiveWanted = -1;
    bankWanted = -1;
    diveWanted = -1;
    pilotHeading = -1;
    pilotDive = -1;
}

void IndicesUpdatePositionHelicopter::Scan(NetworkMessageFormatBase* format)
{
    base::Scan(format);
    SCAN(turret)
    SCAN(backRotorWanted)
    SCAN(mainRotorWanted)
    SCAN(cyclicForwardWanted)
    SCAN(cyclicAsideWanted)
    SCAN(rotorDiveWanted)
    SCAN(bankWanted)
    SCAN(diveWanted)
    SCAN(pilotHeading)
    SCAN(pilotDive)
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesUpdatePositionHelicopter()
{
    using namespace Poseidon;
    return new IndicesUpdatePositionHelicopter();
}
namespace Poseidon
{

NetworkMessageFormat& HelicopterAuto::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    switch (cls)
    {
        case NMCCreate:
            base::CreateFormat(cls, format);

            format.Add("rotorSpeed", NDTFloat, NCTFloat0To1, DEFVALUE(float, 0), DOC_MSG("Initial rotor speed"));
            break;
        case NMCUpdateGeneric:
            base::CreateFormat(cls, format);

            format.Add("rotorSpeedWanted", NDTFloat, NCTFloat0To1, DEFVALUE(float, 0), DOC_MSG("Wanted rotor speed"),
                       ET_ABS_DIF, ERR_COEF_VALUE_MAJOR);
            format.Add("state", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Autopilot state"),
                       ET_NOT_EQUAL, ERR_COEF_MODE);
            format.Add("pilotHeight", NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Height, wanted by pilot"),
                       ET_ABS_DIF, ERR_COEF_VALUE_MAJOR);
            format.Add("pilotSpeed", NDTVector, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Speed, wanted by pilot"),
                       ET_ABS_DIF, 1);
            format.Add("stopMode", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Landing type"),
                       ET_NOT_EQUAL, ERR_COEF_MODE);
            format.Add("stopPosition", NDTVector, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Landing position"),
                       ET_ABS_DIF, 0.1);

            format.Add("pilotSpeedHelper", NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("pilotSpeed is valid"),
                       ET_NOT_EQUAL, ERR_COEF_VALUE_MAJOR);
            format.Add("pilotHeightHelper", NDTBool, NCTNone, DEFVALUE(bool, true), DOC_MSG("pilotHeight is valid"),
                       ET_NOT_EQUAL, ERR_COEF_VALUE_MAJOR);
            format.Add("pilotDirHelper", NDTBool, NCTNone, DEFVALUE(bool, true), DOC_MSG("pilotHeading is valid"),
                       ET_NOT_EQUAL, ERR_COEF_VALUE_MAJOR);

            break;
        case NMCUpdatePosition:
            base::CreateFormat(cls, format);
            format.Add("turret", NDTObject, NCTNone, DEFVALUE_MSG(NMTUpdateTurret), DOC_MSG("Turret object"),
                       ET_ABS_DIF, 1);
            format.Add("backRotorWanted", NDTFloat, NCTFloatM1ToP1, DEFVALUE(float, 0),
                       DOC_MSG("Wanted back rotor control"), ET_ABS_DIF, ERR_COEF_VALUE_MAJOR);
            format.Add("mainRotorWanted", NDTFloat, NCTFloatM1ToP1, DEFVALUE(float, 0),
                       DOC_MSG("Wanted main rotor control"), ET_ABS_DIF, ERR_COEF_VALUE_MAJOR);
            format.Add("cyclicForwardWanted", NDTFloat, NCTFloatM1ToP1, DEFVALUE(float, 0),
                       DOC_MSG("Wanted forward cyclic position"), ET_ABS_DIF, ERR_COEF_VALUE_MAJOR);
            format.Add("cyclicAsideWanted", NDTFloat, NCTFloatM1ToP1, DEFVALUE(float, 0),
                       DOC_MSG("Wanted aside cyclic position"), ET_ABS_DIF, ERR_COEF_VALUE_MAJOR);
            format.Add("rotorDiveWanted", NDTFloat, NCTFloatM1ToP1, DEFVALUE(float, 0), DOC_MSG("Wanted rotor dive"),
                       ET_ABS_DIF, ERR_COEF_VALUE_MAJOR);
            format.Add("bankWanted", NDTFloat, NCTFloatM1ToP1, DEFVALUE(float, 0), DOC_MSG("Wanted bank position"),
                       ET_ABS_DIF, ERR_COEF_VALUE_MAJOR);
            format.Add("diveWanted", NDTFloat, NCTFloatM1ToP1, DEFVALUE(float, 0), DOC_MSG("Wanted dive"), ET_ABS_DIF,
                       ERR_COEF_VALUE_MAJOR);

            format.Add("pilotHeading", NDTFloat, NCTFloatAngle, DEFVALUE(float, 0), DOC_MSG("Heading, wanted by pilot"),
                       ET_ABS_DIF, ERR_COEF_VALUE_MAJOR);
            format.Add("pilotDive", NDTFloat, NCTFloatM1ToP1, DEFVALUE(float, 0), DOC_MSG("Dive, wanted by pilot"),
                       ET_ABS_DIF, ERR_COEF_VALUE_MAJOR);

            break;
        default:
            base::CreateFormat(cls, format);
            break;
    }
    return format;
}

HelicopterAuto* HelicopterAuto::CreateObject(NetworkMessageContext& ctx)
{
    Entity* veh = Entity::CreateObject(ctx);
    HelicopterAuto* heli = dyn_cast<HelicopterAuto>(veh);
    if (!heli)
    {
        return nullptr;
    }
    heli->TransferMsg(ctx);
    return heli;
}

TMError HelicopterAuto::TransferMsg(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCCreate:
            if (ctx.IsSending())
            {
                TMCHECK(base::TransferMsg(ctx))
            }
            {
                PoseidonAssert(dynamic_cast<const IndicesCreateHelicopter*>(ctx.GetIndices()))
                    const IndicesCreateHelicopter* indices =
                        static_cast<const IndicesCreateHelicopter*>(ctx.GetIndices());

                ITRANSF(rotorSpeed)
            }
            break;
        case NMCUpdateGeneric:
            TMCHECK(base::TransferMsg(ctx))
            {
                PoseidonAssert(dynamic_cast<const IndicesUpdateHelicopter*>(ctx.GetIndices()))
                    const IndicesUpdateHelicopter* indices =
                        static_cast<const IndicesUpdateHelicopter*>(ctx.GetIndices());

                ITRANSF(rotorSpeedWanted)

                ITRANSF_ENUM(state)
                ITRANSF(pilotHeight)
                ITRANSF(pilotSpeed)
                ITRANSF_ENUM(stopMode)
                ITRANSF(stopPosition)
                ITRANSF(pilotSpeedHelper)
                ITRANSF(pilotHeightHelper)
                ITRANSF(pilotDirHelper)

                if (!ctx.IsSending() && ctx.GetInitialUpdate())
                {
                    _rotorSpeed = _rotorSpeedWanted;
                }
            }
            break;
        case NMCUpdatePosition:
        {
            PoseidonAssert(dynamic_cast<const IndicesUpdatePositionHelicopter*>(ctx.GetIndices()))
                const IndicesUpdatePositionHelicopter* indices =
                    static_cast<const IndicesUpdatePositionHelicopter*>(ctx.GetIndices());

            Matrix3 oldTrans = Orientation();
            TMCHECK(base::TransferMsg(ctx))
            if (ctx.IsSending() || !(GunnerUnit() && GunnerUnit()->GetPerson()->IsLocal()))
                TMCHECK(ctx.IdxTransferObject(indices->turret, _turret))
            _turret.Stabilize(this, Type()->_turret, oldTrans, Orientation());
            ITRANSF(backRotorWanted)
            ITRANSF(mainRotorWanted)
            ITRANSF(cyclicForwardWanted)
            ITRANSF(cyclicAsideWanted)
            ITRANSF(rotorDiveWanted)
            ITRANSF(bankWanted)
            ITRANSF(diveWanted)
            ITRANSF(pilotHeading)
            ITRANSF(pilotDive)
            if (!ctx.IsSending() && ctx.GetInitialUpdate())
            {
                _mainRotor = _mainRotorWanted;
            }
        }
        break;
        default:
            return base::TransferMsg(ctx);
    }
    return TMOK;
}

#define MAKE_FINITE(x)                                                     \
    if (!_finite(x))                                                       \
    {                                                                      \
        RptF("%s(%d) : Infinite value %s==%g", __FILE__, __LINE__, #x, x); \
        x = 1000;                                                          \
    }

float HelicopterAuto::CalculateError(NetworkMessageContext& ctx)
{
    float error = 0;
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
            error += base::CalculateError(ctx);
            {
                PoseidonAssert(dynamic_cast<const IndicesUpdateHelicopter*>(ctx.GetIndices()))
                    const IndicesUpdateHelicopter* indices =
                        static_cast<const IndicesUpdateHelicopter*>(ctx.GetIndices());

                ICALCERR_ABSDIF(float, rotorSpeedWanted, ERR_COEF_VALUE_MAJOR)
                ICALCERR_NEQ(int, state, ERR_COEF_MODE)
                ICALCERR_ABSDIF(float, pilotHeight, ERR_COEF_VALUE_MAJOR)
                ICALCERR_DIST(pilotSpeed, 1)
                ICALCERR_NEQ(int, stopMode, ERR_COEF_MODE)
                ICALCERR_DIST(stopPosition, 0.1)

                ICALCERR_NEQ(bool, pilotSpeedHelper, ERR_COEF_VALUE_MAJOR)
                ICALCERR_NEQ(bool, pilotHeightHelper, ERR_COEF_VALUE_MAJOR)
                ICALCERR_NEQ(bool, pilotDirHelper, ERR_COEF_VALUE_MAJOR)
            }
            break;
        case NMCUpdatePosition:
        {
            error += base::CalculateError(ctx);
            PoseidonAssert(dynamic_cast<const IndicesUpdatePositionHelicopter*>(ctx.GetIndices()))
                const IndicesUpdatePositionHelicopter* indices =
                    static_cast<const IndicesUpdatePositionHelicopter*>(ctx.GetIndices());

            int index = indices->turret;
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
                    error += _turret.CalculateError(subctx);
                }
            }

            MAKE_FINITE(_backRotorWanted)
            MAKE_FINITE(_mainRotorWanted)
            MAKE_FINITE(_cyclicForwardWanted)
            MAKE_FINITE(_cyclicAsideWanted)
            MAKE_FINITE(_bankWanted)
            MAKE_FINITE(_diveWanted)
            MAKE_FINITE(_pilotHeading)
            MAKE_FINITE(_pilotDive)

            ICALCERR_ABSDIF(float, backRotorWanted, ERR_COEF_VALUE_MAJOR)
            ICALCERR_ABSDIF(float, mainRotorWanted, ERR_COEF_VALUE_MAJOR)
            ICALCERR_ABSDIF(float, cyclicForwardWanted, ERR_COEF_VALUE_MAJOR)
            ICALCERR_ABSDIF(float, cyclicAsideWanted, ERR_COEF_VALUE_MAJOR)
            ICALCERR_ABSDIF(float, rotorDiveWanted, ERR_COEF_VALUE_MAJOR)
            ICALCERR_ABSDIF(float, bankWanted, ERR_COEF_VALUE_MAJOR)
            ICALCERR_ABSDIF(float, diveWanted, ERR_COEF_VALUE_MAJOR)

            ICALCERR_ABSDIF(float, pilotHeading, ERR_COEF_VALUE_MAJOR)
            ICALCERR_ABSDIF(float, pilotDive, ERR_COEF_VALUE_MAJOR)
        }
        break;
        default:
            error += base::CalculateError(ctx);
            break;
    }
    DoAssert(_finite(error));
    return error;
}

} // namespace Poseidon
