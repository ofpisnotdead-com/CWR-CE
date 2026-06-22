#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>

#include <Poseidon/World/Entities/Vehicles/Air/Airplane.hpp>
#include <Poseidon/World/Entities/Weapons/Shots.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Random/randomGen.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/World/Simulation/FrameInv.hpp>
#include <Poseidon/Dev/Diag/DiagModes.hpp>

#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Game/UiActions.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>
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
#include <Poseidon/Foundation/Types/Pointers.hpp>

namespace Poseidon
{
using namespace Foundation;
using Foundation::EnumName;

#if _ENABLE_CHEATS
#define LOG_SWEEP 1
#endif

bool AirplaneAuto::AimWeapon(int weapon, Vector3Par direction)
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
    _gunYRotWanted = -atan2(relDir.X(), relDir.Z());
    float sizeXZ = sqrt(Square(relDir.X()) + Square(relDir.Z()));
    _gunXRotWanted = atan2(relDir.Y(), sizeXZ);

    float gunXRotWanted = _gunXRotWanted;
    float gunYRotWanted = _gunYRotWanted;

    Limit(_gunXRotWanted, Type()->_minGunElev, Type()->_maxGunElev);
    Limit(_gunYRotWanted, Type()->_minGunTurn, Type()->_maxGunTurn);
    float xOffRange = fabs(gunXRotWanted - _gunXRotWanted);
    float yOffRange = fabs(gunYRotWanted - _gunYRotWanted);
    float xToAim = fabs(_gunXRotWanted - _gunXRot);
    float yToAim = fabs(_gunYRotWanted - _gunYRot);
    if (xToAim + yToAim > 1e-6)
    {
        CancelStop(); // enable simulation
    }

    const WeaponModeType* mode = GetWeaponMode(weapon);
    if (!mode)
    {
        return false;
    }
    if (!mode->_ammo)
    {
        return false;
    }
    if (mode->_ammo->_simulation == AmmoShotMissile)
    {
        return true;
    }
    _targetOutOfAim = false;
    if (xOffRange + yOffRange < 0.001)
    {
        return true;
    }
    _targetOutOfAim = true;
    return false;
}

bool AirplaneAuto::CalculateAimWeapon(int weapon, Vector3& dir, Target* target)
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

    const WeaponModeType* mode = GetWeaponMode(weapon);
    const Magazine* magazine = GetMagazineSlot(weapon)._magazine;
    const MagazineType* aInfo = magazine ? magazine->_type : nullptr;
    if (mode && mode->_ammo && mode->_ammo->_simulation != AmmoShotMissile)
    {
        float dist2 = tgtPos.Distance2(Position());
        float time2 = 0;
        if (aInfo)
        {
            // relative speed along the shot axis shortens flight time
            Vector3 relSpeed = DirectionWorldToModel(Speed() - target->speed);
            time2 = dist2 / Square(aInfo->_initSpeed + relSpeed.Z());
        }
        float time = sqrt(time2);
        float fall = 0.5 * G_CONST * time2;
        tgtPos[1] += fall;
        tgtPos += (target->speed - Speed()) * (time + 0.25);
    }

    const float predTime = 0.1;
    Vector3 myPos = PositionModelToWorld(weaponPos);
    tgtPos += target->speed * predTime;
    myPos += Speed() * predTime;

    dir = tgtPos - myPos;
    return true;
}

bool AirplaneAuto::AimWeapon(int weapon, Target* target)
{
    Vector3 dir;
    if (!CalculateAimWeapon(weapon, dir, target))
    {
        return false;
    }
    return AimWeapon(weapon, dir);
}

Matrix4 AirplaneAuto::GunTransform() const
{
    return MIdentity;
}

void AirplaneAuto::LimitCursor(CameraType camType, Vector3& dir) const {}

Vector3 AirplaneAuto::GetWeaponDirection(int weapon) const
{
    Matrix4Val shootTrans = GunTransform();
    Vector3 wepDir = shootTrans.Rotate(Type()->_gunDir);

    return DirectionModelToWorld(wepDir);
}

float AirplaneAuto::GetAimed(int weapon, Target* target) const
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
    float visible = _visTracker.Value(this, _currentWeapon, target->idExact, 0.9);
    const WeaponModeType* mode = GetWeaponMode(weapon);
    if (!mode || !mode->_ammo)
    {
        return 0;
    }

    const Magazine* magazine = GetMagazineSlot(weapon)._magazine;
    const MagazineType* aInfo = magazine ? magazine->_type : nullptr;

    // 0.6 visibility means 0.8 unaimed
    visible = 1 - (1 - visible) * 0.5f;

    Vector3 ap = target->AimingPosition();

    const AmmoType* ammo = mode->_ammo;
    if (ammo->_simulation == AmmoShotMissile)
    {
        if (ammo->thrustTime > 0)
        {
            Vector3 relPos = PositionWorldToModel(ap);
            if (relPos.Z() <= 100)
            {
                return 0;
            }
            float rYPos = relPos.Y();
            if (fabs(relPos.X()) > relPos.Z())
            {
                return 0;
            }
            if (fabs(rYPos) > relPos.Z())
            {
                return 0;
            }
            float invRZ = 0.5 / relPos.Z();
            float lockX = 1 - fabs(relPos.X()) * invRZ;
            float lockY = 1 - fabs(rYPos) * invRZ;
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
            // free fall bomb
            // predict where the bomb will fall
            // assume it is guided - larger error is acceptable

            Vector3 wPos = PositionModelToWorld(GetWeaponCenter(weapon));
            Vector3 wDir = GetWeaponDirection(weapon);

            // 0.9 factor approximates drag reducing effective gravity
            float a = G_CONST * 0.9f;

            float dist = ap.Distance(wPos);

            float surfaceY = GLandscape->SurfaceYAboveWater(Position().X(), Position().Z());
            float aboveSurface = Position().Y() - surfaceY;

            float t = sqrt(2 * aboveSurface / a);

            Vector3 hit = wPos + _speed * t - Vector3(0, a * 0.5f, 0) * t * t;
            Vector3 estPos = ap + target->speed * t;

            Vector3 hError = hit - estPos;
#if _ENABLE_CHEATS
            if (CHECK_DIAG(DECombat) /*&& this==GWorld->CameraOn()*/)
            {
                GlobalShowMessage(100, "Time to impact %.2f, error %.1f (%.1f,%.1f,%.1f)", t, hError.Size(), hError[0],
                                  hError[1], hError[2]);
            }
#endif

            hError[1] *= 0.3f;

            float error = hError.Size();
            float aimPrecision = (GetInvAbility() - 1) * 1.5f + 1;

            float tgtSize = target->idExact->GetShape()->GeometrySphere() * aimPrecision * 0.25f;
            tgtSize += dist * aimPrecision * 0.02f + ammo->indirectHitRange;

            // assume Laser guiding can correct a lot
            tgtSize += t * 10;

            if (error <= tgtSize)
            {
                return visible;
            }
            if (error >= tgtSize * 2)
            {
                return 0;
            }
            return (2 * tgtSize - error) / tgtSize * visible;
        }
    }
    else
    {
        Vector3 wPos = PositionModelToWorld(GetWeaponCenter(weapon));

        float dist = ap.Distance(wPos);

        float time = dist * aInfo->_invInitSpeed;
        Vector3 leadSpeed = target->speed - Speed();
        Vector3 estPos = ap + leadSpeed * time;

        Vector3 wDir = GetWeaponDirection(weapon);

        float eDist = estPos.Distance(wPos);

        Vector3 hit = wPos + wDir * eDist;
        hit[1] -= 0.5f * G_CONST * time * time;
        Vector3 hError = hit - estPos;
#if _ENABLE_CHEATS
        float distHit = hit.Distance(wPos);
#endif

        hError[1] *= 3;

        float error = hError.Size();
        float aimPrecision = (GetInvAbility() - 1) * 1.5f + 1;

        float tgtSize =
            (target->idExact->GetShape()->GeometrySphere() * aimPrecision * 0.25f + ammo->indirectHitRange * 3);
        const MuzzleType* muzzle = GetMagazineSlot(weapon)._muzzle;
        float dCoef = floatMax(muzzle->_aiDispersionCoefX, muzzle->_aiDispersionCoefY);
        tgtSize += dist * mode->_dispersion * dCoef * aimPrecision * 0.5f;

        if (leadSpeed.SquareSize() > Square(10))
        {
            tgtSize *= 2;
        }

#if _ENABLE_CHEATS
        if ((Object*)this == GWorld->CameraOn() && CHECK_DIAG(DECombat))
        {
            GlobalShowMessage(2000,
                              "Error %.1f, tgtSize %.1f, time %.2f, "
                              "ePos %.1f,%.1f,%.1f, distErr %.1f",
                              error, tgtSize, time, hError[0], hError[1], hError[2], distHit - eDist);
        }
#endif

        if (error <= tgtSize)
        {
            return visible;
        }
        if (error >= tgtSize * 2)
        {
            return 0;
        }
        return (2 * tgtSize - error) / tgtSize * visible;
    }
}

void AirplaneAuto::GetActions(UIActions& actions, AIUnit* unit, bool now)
{
    base::GetActions(actions, unit, now);

    if (unit && unit->GetVehicleIn() == this && DriverBrain() == unit)
    {
        switch (_planeState)
        {
            case Flight:
                actions.Add(ATLand, this, 0.9);
                break;
            case Marshall:
            case Approach:
            case Final:
            case Landing:
                actions.Add(ATCancelLand, this, 0.9);
                break;
        }
        if (QIsManual())
        {
            if (!_gearDammage && Type()->_gearRetracting)
            {
                actions.Add(ATLandGear, this, 0.8, 0, false, false);
            }
            if (_flaps > 0.25)
            {
                actions.Add(ATFlapsUp, this, 0.7, 0, false, false);
            }
            if (_flaps < 0.75)
            {
                actions.Add(ATFlapsDown, this, 0.7, 0, false, false);
            }
        }
    }
}

RString AirplaneAuto::GetActionName(const UIAction& action)
{
    switch (action.type)
    {
        case ATLand:
            return LocalizeString(IDS_ACTION_LAND);
        case ATCancelLand:
            return LocalizeString(IDS_ACTION_CANCEL_LAND);
        case ATLandGear:
            if (_gearsUp > 0.5)
            {
                return LocalizeString(IDS_ACTION_GEAR_DOWN);
            }
            else
            {
                return LocalizeString(IDS_ACTION_GEAR_UP);
            }
        case ATFlapsDown:
            return LocalizeString(IDS_ACTION_FLAPS_DOWN);
        case ATFlapsUp:
            return LocalizeString(IDS_ACTION_FLAPS_UP);
        default:
            return base::GetActionName(action);
    }
}

void AirplaneAuto::PerformAction(const UIAction& action, AIUnit* unit)
{
    switch (action.type)
    {
        case ATLandGear:
            _pilotGear = _gearsUp > 0.5;
            break;
        case ATFlapsDown:
            if (_pilotFlaps < 2)
            {
                _pilotFlaps++;
            }
            break;
        case ATFlapsUp:
            if (_pilotFlaps > 0)
            {
                _pilotFlaps--;
            }
            break;
        default:
            base::PerformAction(action, unit);
            break;
    }
}

bool AirplaneAuto::IsStopped() const
{
    if (!_landContact && !_objectContact)
    {
        return false;
    }
    if (_speed.SquareSize() > Square(2))
    {
        return false;
    }
    if (QIsManual() && EngineIsOn())
    {
        return false;
    }
    return base::IsStopped();
}

void AirplaneAuto::DammageCrew(EntityAI* killer, float howMuch, RString ammo)
{
    AIUnit* commander = CommanderUnit();
    if (commander)
    {
        if (GetRawTotalDammage() >= 0.45f && commander->GetCombatMode() >= CMCombat)
        {
            Time goTime = Glob.time + GRandGen.PlusMinus(1.0f, 0.5f);
            if (goTime < _getOutAfterDammage)
            {
                _getOutAfterDammage = goTime;
            }
        }
    }

    base::DammageCrew(killer, howMuch, ammo);
}

void AirplaneAuto::Eject(AIUnit* unit)
{
    // check height
    float surfaceY = GLOB_LAND->SurfaceYAboveWater(Position()[0], Position()[2]);
    float height = Position().Y() - surfaceY;
    bool parachute = (height > 30 || Type()->_ejectSpeed.SquareSize() > Square(2));
    if (parachute)
    {
        unit->GetPerson()->SetSpeed(DirectionModelToWorld(Type()->_ejectSpeed));
    }
    unit->ProcessGetOut(parachute);
}

void AirplaneAuto::Land()
{
    if (_planeState == Flight)
    {
        _planeState = Marshall;
    }
}

void AirplaneAuto::CancelLand()
{
    switch (_planeState)
    {
        case Marshall:
        case Approach:
        case Final:
            _planeState = Flight;
            break;
        case Landing:
            _planeState = WaveOff;
            break;
    }
}

void AirplaneAuto::SetFlyingHeight(float val)
{
    _defPilotHeight = val;
}

void AirplaneAuto::FakePilot(float deltaT) {}

inline float JAdj(float x)
{
    return x * fabs(x);
}

void AirplaneAuto::JoystickDirPilot(float deltaT)
{
    auto& input = InputSubsystem::Instance();
    _forceDive = 1;

    //_pilotHelper = true;
    _pilotHelperDir = false;
    _pilotHelperHeight = false;
    _pilotHelperBankDive = false;

    _rudderWanted = -input.GetStickRudder();
    _aileronWanted = JAdj(input.GetStickLeft());
    _elevatorWanted = JAdj(input.GetStickForward());

    _pilotDive = 0;
    _pilotBank = 0;

    if (_pilotGear)
    {
        // auto-gear up
        Vector3Val position = Position();
        float surfaceY = GLOB_LAND->SurfaceYAboveWater(position[0], position[2]);
        float above = position[1] - surfaceY;
        if (above > 80 && ModelSpeed().Z() > GetType()->GetMaxSpeedMs() * 0.5f)
        {
            _pilotGear = false;
        }
    }
}

void AirplaneAuto::JoystickThrustPilot(float deltaT)
{
    auto& input = InputSubsystem::Instance();
    _pilotHelperThrust = false;
    _thrustWanted = -input.GetStickThrust();
    _pilotBrake = floatMax(-_thrustWanted, 0);
    saturate(_thrustWanted, 0, 1);
}

void AirplaneAuto::KeyboardAny(AIUnit* unit, float deltaT) {}

void AirplaneAuto::DetectControlMode() const
{
    static const UserAction moveActions[] = {UAMoveUp, UAMoveDown, UAMoveLeft, UAMoveRight, UATurnLeft, UATurnRight};
    static const UserAction turnActions[] = {UAMoveForward, UAMoveBack, UAMoveFastForward, UAMoveLeft,
                                             UAMoveRight,   UATurnLeft, UATurnRight};
    static const UserAction cursorActions[] = {UALookLeftDown, UALookDown,   UALookRightDown,
                                               UALookLeft,     UALookCenter, UALookRight,
                                               UALookLeftUp,   UALookUp,     UALookRightUp};
    static const UserAction thrustActions[] = {
        UAMoveForward,
        UAMoveBack,
        UAMoveFastForward,
        // UAMoveUp,UAMoveDown,
    };

    const int nMoveActions = sizeof(moveActions) / sizeof(*moveActions);
    const int nTurnActions = sizeof(turnActions) / sizeof(*turnActions);
    const int nCursorActions = sizeof(cursorActions) / sizeof(*cursorActions);
    const int nThrustActions = sizeof(thrustActions) / sizeof(*thrustActions);
    DetectControlModeActions(moveActions, nMoveActions, turnActions, nTurnActions, cursorActions, nCursorActions,
                             thrustActions, nThrustActions);
}

void AirplaneAuto::KeyboardPilot(AIUnit* unit, float deltaT)
{
    auto& input = InputSubsystem::Instance();
    constexpr InputContext ctx = InputContext::PlanePilot;
    switch (_planeState)
    {
        case Flight:
        case Takeoff:
        case TaxiIn:
        case TaxiOff:
            // no keyboard controls when autopilot is on
            break;
        default:
            // same parameters as AIPilot
            _dirCompensate = 0.9;

            //_pilotHelper = true;
            _pilotHelperHeight = true;
            _pilotHelperDir = true;
            _pilotHelperBankDive = true;
            _rudderWanted = 0;

            _forceDive = 1;

            return;
    }
    _dirCompensate = 0.5; // low heading compensation

    _forceDive = 1;

    CancelStop();

    if (input.IsJoystickActive())
    {
        JoystickDirPilot(deltaT);
    }
    else
    {
        //_pilotHelper=true;
        _pilotHelperHeight = false;

        _forceDive = 1;

        // keyboard driving

        float bank = DirectionAside().Y();
        float dive = Direction().Y();

        if (_landContact)
        {
            const float takeOffSpeed = Type()->_takeOffSpeed;
            if (ModelSpeed()[2] >= takeOffSpeed)
            {
                // help auto takeoff to avoid gear dammage
                dive = 9 * H_PI / 180;
            }
            else
            {
                dive = 0;
            }
            _pilotDiveSet = false;
        }

        bool internalCamera = IsGunner(GWorld->GetCameraType());
        if (internalCamera && input.IsMouseTurnActive() && !input.IsLookAroundEnabled())
        {
            // last input from mouse - use mouse controls
            _pilotHelperDir = true;
            _pilotHelperBankDive = true;
            _pilotHeading = atan2(_mouseDirWanted[0], _mouseDirWanted[2]);
            _rudderWanted = 0;
            // dive controlled directly
            _pilotDive = _mouseDirWanted[1];
        }
        else
        {
            _pilotHelperDir = false;
            _pilotHelperBankDive = true;
            _rudderWanted = input.GetAction(ctx, UAMoveLeft) - input.GetAction(ctx, UAMoveRight);
            float turnBank = input.GetAction(ctx, UATurnRight) - input.GetAction(ctx, UATurnLeft);
            if (fabs(turnBank) < 0.2 && fabs(bank) < 0.1)
            {
                // auto-level plane
                _pilotBank = 0;
            }
            else
            {
                _pilotBank = bank - turnBank * 0.3;
            }

            float turnDive = input.GetAction(ctx, UAMoveUp) - input.GetAction(ctx, UAMoveDown);
            if (fabs(turnDive) > 0.2)
            {
                _pilotDive = dive + turnDive * 0.3;
                _pilotDiveSet = false;
            }
            else if (!_pilotDiveSet)
            {
                _pilotDive = dive;
                _pilotDiveSet = true;
            }
        }
    }

    if (input.IsJoystickThrustActive())
    {
        JoystickThrustPilot(deltaT);
    }
    else
    {
        _pilotHelperThrust = true;

        Vector3Val relSpeed = ModelSpeed();

        float forward = input.GetAction(ctx, UAMoveForward) * 0.5f + input.GetAction(ctx, UAMoveFastForward) -
                        input.GetAction(ctx, UAMoveBack) * 0.5f;

        if (forward < 0.1)
        {
            // automatically stop when moving very slow
            if (_pilotSpeed < 5)
            {
                _pilotSpeed = 0;
            }
        }
        if (forward > 0)
        {
            EngineOn();
            if (!_pressedForward)
            {
                _pilotSpeed = relSpeed[2], _pressedForward = true;
            }
            _pilotSpeed += deltaT * 20 * forward;
        }
        else
        {
            if (_pressedForward)
            {
                _pilotSpeed = relSpeed[2], _pressedForward = false;
            }
        }
        if (forward < 0)
        {
            if (!_pressedBack)
            {
                _pilotSpeed = relSpeed[2], _pressedBack = true;
            }
            _pilotSpeed += deltaT * 20 * forward;
        }
        else
        {
            if (_pressedBack)
            {
                _pilotSpeed = relSpeed[2], _pressedBack = false;
            }
        }
        Limit(_pilotSpeed, 0, GetType()->GetMaxSpeedMs() * 1.5f);
    }

    Limit(_pilotHeight, 0, 250);

    // manual plane does not have taxiing autopilot
    if (_planeState == TaxiIn || _planeState == TaxiOff)
    {
        _planeState = Takeoff;
    }
}

class Vector3Path
{
    const Vector3* _path;
    int _nPath;

  public:
    Vector3Path(const Vector3* path, int nPath) : _path(path), _nPath(nPath) {}
    float GetCost(Vector3Val pos) const;
    Vector3 GetPos(float cost, bool& end) const;
};

float Vector3Path::GetCost(Vector3Val pos) const
{
    if (_nPath < 2)
    {
        return 0;
    }

    float minDist2 = 1e10;
    Vector3 nearestPos = pos;
    int nearestI = 0;
    for (int i = 1; i < _nPath; i++)
    {
        Vector3Val b = _path[i - 1];
        Vector3Val e = _path[i] - b;

        Vector3 p = pos - b;
        float t = (e * p) / e.SquareSize();
        saturate(t, 0, 1);
        Vector3 nearest = b + t * e;
        float dist2 = nearest.Distance2(pos);
        if (minDist2 > dist2)
        {
            minDist2 = dist2;
            nearestPos = nearest;
            nearestI = i;
        }
    }
    float cost = 0;
    for (int i = 0; i < nearestI - 1; i++)
    {
        Vector3Val b = _path[i];
        Vector3Val e = _path[i + 1];
        float dist = b.Distance(e);
        cost += dist;
    }
    Vector3Val prevPoint = _path[nearestI - 1];
    cost += nearestPos.Distance(prevPoint);
    return cost;
}

Vector3 Vector3Path::GetPos(float cost, bool& end) const
{
    for (int i = 1; i < _nPath; i++)
    {
        Vector3Val b = _path[i - 1];
        Vector3Val e = _path[i];
        float dist = e.Distance(b);
        if (cost < dist)
        {
            end = false;
            return b + (e - b) * (cost / dist);
        }
        cost -= dist;
    }
    end = true;
    return _path[_nPath - 1];
}

AirplaneAuto::PathResult AirplaneAuto::PathAutopilot(const Vector3* path, int nPath)
{
    _pilotGear = true;

    if (nPath < 2)
    {
        return PathFinished;
    }

    Vector3Path vpath(path, nPath);

#if _ENABLE_CHEATS
    if (CHECK_DIAG(DEPath))
    {
        for (int i = 0; i < nPath; i++)
        {
            Ref<Object> obj = new ObjectColored(GScene->Preloaded(SphereModel), -1);
            obj->SetPosition(path[i]);
            obj->SetScale(0.2);
            obj->SetConstantColor(PackedColor(Color(1, 1, 0)));
            GLandscape->ShowObject(obj);
        }
    }
#endif

    float cost = vpath.GetCost(Position());

    bool endOfPath = false;

    Vector3 pos = vpath.GetPos(cost + 22, endOfPath);

    Vector3 predTurnPos = vpath.GetPos(cost + 220, endOfPath);

    // advance in direction of

#if _ENABLE_CHEATS
    if (CHECK_DIAG(DECombat))
    {
        {
            Ref<Object> obj = new ObjectColored(GScene->Preloaded(SphereModel), -1);
            obj->SetPosition(pos);
            obj->SetScale(0.5);
            obj->SetConstantColor(PackedColor(Color(0, 1, 1)));
            GLandscape->ShowObject(obj);
        }

        {
            Ref<Object> obj = new ObjectColored(GScene->Preloaded(SphereModel), -1);
            obj->SetPosition(predTurnPos);
            obj->SetScale(0.5);
            obj->SetConstantColor(PackedColor(Color(0, 1, 0)));
            GLandscape->ShowObject(obj);
        }
    }
#endif

    Vector3 relPos = pos - Position();

    float distance = predTurnPos.Distance(Position());

    PathResult ret = PathGoing;
    if (endOfPath && distance < 20)
    {
        _pilotSpeed = 0;
        _thrustWanted = 0;
        _pilotBrake = 1;
        ret = PathFinished;
    }
    else
    {
        _pilotHeading = atan2(relPos.X(), relPos.Z());

        float distFactor = distance > 210 ? 1 : distance * (1.0 / 210);
        _pilotSpeed = 28 * distFactor;

        Vector3 relPredPos = PositionWorldToModel(predTurnPos);

        float turn = 0.9;
        if (relPredPos.Z() > 1)
        {
            turn = fabs(relPredPos.X() * 0.1);

            // predicted turn never requires really low speed
            saturateMin(turn, 0.9);
        }

        // actual turn may required very low speed
        float curHeading = atan2(Direction().X(), Direction().Z());
        saturateMax(turn, fabs(_pilotHeading - curHeading) * 4);

        saturateMin(turn, 1);

        saturateMin(_pilotSpeed, turn * 3 + (1 - turn) * 30);
    }

    _elevatorWanted = 0;
    _pilotFlaps = 0;

    float mySize = CollisionSize();
    float mySpeedSize = fabs(ModelSpeed()[2]);
    VehicleCollisionBuffer col;
    float gap = mySize * 3;
    float maxDist = Square(mySpeedSize) * 0.5 + gap;
    float maxTime = 3.5 + mySpeedSize * 0.2;

    GLOB_LAND->PredictCollision(col, this, maxTime, gap, maxDist);
    if (col.Size() > 0)
    {
        bool stop = false;

        for (int i = 0; i < col.Size(); i++)
        {
            const VehicleCollision& info = col[i];
            const VehicleWithAI* who = info.who;
            if (!who)
            {
                continue;
            }
#if COL_DIAG
            LOG_DEBUG(Physics, "{}: col {}", (const char*)GetDebugName(), (const char*)who->GetDebugName());
#endif
            float relDist = PositionWorldToModel(who->Position()).Z();
            if (relDist <= 0)
            {
#if COL_DIAG
                LOG_DEBUG(Physics, "  not in front");
#endif
                continue;
            }
            if (!who->EngineIsOn())
            {
#if COL_DIAG
                LOG_DEBUG(Physics, "  engine off");
                LOG_DEBUG(Physics, "  my speed {:.1f}", ModelSpeed().Z());
                LOG_DEBUG(Physics, "  tw {:.2f}, t {:.2f}", _thrustWanted, _thrust);
#endif
                stop = true;
                break;
            }
            float relSpeed = DirectionWorldToModel(who->Speed()).Z() - 2;

            float slower = floatMax((relDist - gap) * 0.5, 0);
            if (relSpeed > 0)
            {
                relSpeed = 0;
            }

#if COL_DIAG
            LOG_DEBUG(Physics, "  relSpeed {:.1f}, slower {:.1f}", relSpeed, slower);
#endif
            relSpeed -= slower;
            saturateMax(relSpeed, 0);
            if (relSpeed < 5)
            {
                relSpeed = 0;
            }

            if (relSpeed < _avoidSpeed || Glob.time > _avoidSpeedTime)
            {
                _avoidSpeed = relSpeed;
                _avoidSpeedTime = Glob.time + 1;
#if COL_DIAG
                LOG_DEBUG(Physics, "  avoid speed {:.2f}", _avoidSpeed);
#endif
            }
        }
        if (stop)
        {
            ret = PathAborted;
            _pilotSpeed = 0;
            _thrustWanted = 0;
            _pilotBrake = 1;

            _avoidSpeed = 0;
            _avoidSpeedTime = Glob.time + 3;
        }
    }

    if (Glob.time < _avoidSpeedTime)
    {
        saturateMin(_pilotSpeed, _avoidSpeed);
    }

    return ret;
}

bool AirplaneAuto::TaxiOffAutopilot()
{
    const AutoArray<Vector3>& path = GWorld->GetTaxiOffPath(_targetSide);

    return PathAutopilot(path.Data(), path.Size()) <= PathAborted;
}

bool AirplaneAuto::TaxiInAutopilot()
{
    const AutoArray<Vector3>& path = GWorld->GetTaxiInPath(_targetSide);

    return PathAutopilot(path.Data(), path.Size()) <= PathFinished;
}

void AirplaneAuto::Autopilot(Vector3Par target, Vector3Par tgtSpeed, // target
                             Vector3Par direction, Vector3Par speed  // wanted values
)
{
}

void AirplaneAuto::ResetAutopilot() {}

float AirplaneAuto::MakeAirborne()
{
    _pilotSpeed = GetType()->GetMaxSpeedMs() * 0.66;
    _pilotHeight = _defPilotHeight;
    _rpm = 1;
    _thrust = _thrustWanted = 0.5;

    _speed = Direction() * _pilotSpeed;
    _flaps = 0;
    _gearsUp = 1;
    _planeState = Flight;
    _pilotGear = false;
    _pilotFlaps = 0;
    _landContact = false;

    EngineOn();

    return _pilotHeight;
}

bool AirplaneAuto::FireWeapon(int weapon, TargetType* target)
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
    if (!mode || !mode->_ammo)
    {
        return false;
    }
    bool fired = false;
    switch (mode->_ammo->_simulation)
    {
        case AmmoShotRocket:
        case AmmoShotMissile:
            if (mode->_ammo->maxControlRange < 10)
            {
                _rocketLRToggle = !_rocketLRToggle;
                Vector3Val pos = (_rocketLRToggle ? Type()->_rocketLPos : Type()->_rocketRPos);
                fired = FireMissile(weapon, pos, VForward, Vector3(0, 0, aInfo->_initSpeed), target);
            }
            else
            {
                // find corresponding proxy position
                int count = GetMagazineSlot(weapon)._magazine->_ammo;
                bool found;
                Vector3Val pos = FindMissilePos(count, found);
                fired = FireMissile(weapon, pos, VForward, VZero, target);
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

void AirplaneAuto::FireWeaponEffects(int weapon, const Magazine* magazine, EntityAI* target)
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

float AirplaneAuto::GetFieldCost(const GeographyInfo& info) const
{
    float cost = 1;

    if (info.u.road)
    {
        cost *= 1 / 1.05f;
    }
    else if (info.u.track)
    {
        cost *= 1 / 1.02f;
    }
    return cost;
}

float AirplaneAuto::GetCost(const GeographyInfo& geogr) const
{
    float cost = Type()->GetMinCost();
    if (geogr.u.waterDepth >= 2)
    {
        cost *= 0.8f;
    }
    else if (geogr.u.waterDepth >= 1)
    {
        cost *= 0.9f;
    }
    int grad = geogr.u.gradient;
    if (grad > 5)
    {
        grad = 5;
    }
    static const float gradPenalty[6] = {1.0, 1.1, 1.2, 1.3, 1.6, 2.5};
    cost *= gradPenalty[grad];
    return cost;
}

float AirplaneAuto::GetCostTurn(int difDir) const
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

float AirplaneAuto::FireInRange(int weapon, float& timeToAim, const Target& target) const
{
    timeToAim = 0;
    Vector3 relPos = target.AimingPosition() - Position();
    float cosAngle = relPos.CosAngle(Direction());
    float distance = relPos.Size();
    // distance-to-aim: 0m=30s, 1000m=0s, >1000m adds time again
    float timeToAimDistance = distance > 1000 ? (distance - 1000) * (10.0f / 1000) : (1000 - distance) * (30.0f / 1000);
    saturate(timeToAimDistance, 0, 30);
    float timeToAimAngle = (1 - floatMax(cosAngle, 0)) * 15;
    timeToAim = timeToAimAngle + timeToAimDistance;

    return 1;
}

void AirplaneAuto::AIGunner(AIUnit* unit, float deltaT)
{
    PoseidonAssert(unit);

    if (!GetFireTarget())
    {
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

void AirplaneAuto::MoveWeapons(float deltaT)
{
    float delta;
    float speed;
    speed = (_gunXRotWanted - _gunXRot) * 8;
    const float maxA = 10;
    const float maxV = 5;
    delta = speed - _gunXSpeed;
    Limit(delta, -maxA * deltaT, +maxA * deltaT);
    _gunXSpeed += delta;
    Limit(_gunXSpeed, -maxV, +maxV);
    _gunXRot += _gunXSpeed * deltaT;
    Limit(_gunXRot, Type()->_minGunElev, Type()->_maxGunElev);

    speed = AngleDifference(_gunYRotWanted, _gunYRot) * 6;
    delta = speed - _gunYSpeed;
    Limit(delta, -maxA * deltaT, +maxA * deltaT);
    _gunYSpeed += delta;
    Limit(_gunYSpeed, -maxV, +maxV);
    _gunYRot += _gunYSpeed * deltaT;
    _gunYRot = AngleDifference(_gunYRot, 0);
    Limit(_gunYRot, Type()->_minGunTurn, Type()->_maxGunTurn);
}

#define DIAG_COL 0

void AirplaneAuto::AvoidCollision()
{
    if (!Airborne())
    {
        return;
    }
    AIUnit* unit = PilotUnit();
    if (!unit)
    {
        return;
    }

    AISubgroup* mySubgrp = unit->GetSubgroup();

    if (mySubgrp && mySubgrp->GetMode() == AISubgroup::DirectGo)
    {
        return;
    }
    const float maxSpeedStopped = 0.05;
    if (fabs(_pilotSpeed) <= maxSpeedStopped)
    {
        return;
    }

    float mySize = floatMax(15, CollisionSize() * 2);
    float gap = mySize * 4;
    VehicleCollisionBuffer ret;

    const float maxSpeed = GetType()->GetMaxSpeedMs();
    const float mySpeed = ModelSpeed().Z();
    const float maxTime = 4;
    const float maxDist = floatMax(mySize * 4, floatMax(mySpeed, maxSpeed * 0.2) * maxTime);
    GLOB_LAND->PredictCollision(ret, this, maxTime, gap, maxDist);
    if (ret.Size() <= 0)
    {
        return;
    }

    for (int i = 0; i < ret.Size(); i++)
    {
        const VehicleCollision& info = ret[i];
        const EntityAI* who = info.who;

        if (!who->Airborne())
        {
            continue;
        }

#if _ENABLE_CHEATS
        if (CHECK_DIAG(DECombat))
        {
            Ref<Object> obj = new ObjectColored(GScene->Preloaded(SphereModel), -1);
            obj->SetPosition(info.pos);
            Color color(1, 1, 0, 0.3);
            // color=Color(1,-1,0)*danger+Color(0,1,0);
            obj->SetScale(info.distance < gap ? 1 : 0.3);
            obj->SetConstantColor(PackedColor(color));
            GLandscape->ShowObject(obj);
        }
#endif

#if DIAG_COL
        // if( this==GWorld->CameraOn() )
        {
            LOG_DEBUG(Physics, "{} vs {}", (const char*)GetDebugName(), (const char*)who->GetDebugName());
        }
#endif

        Vector3 relPos = PositionWorldToModel(who->Position());
        bool iAmInFrontOfHim = who->PositionWorldToModel(Position()).Z() > 0;
        bool heIsInFrontOfMe = relPos.Z() > 0;

        if (heIsInFrontOfMe && iAmInFrontOfHim)
        {
            // who is higher should climb
            float heIsHigher = who->Position().Y() - Position().Y();
            if (heIsHigher < 0)
            {
                float surfaceY = GLandscape->SurfaceYAboveWater(Position().X(), Position().Z());
                saturateMax(_pilotAvoidHighHeight, who->Position().Y() - surfaceY + mySize);
                Time until = Glob.time + 10;
                if (_pilotAvoidHigh < until)
                {
                    _pilotAvoidHigh = until;
                }

#if DIAG_COL
                // if( this==GWorld->CameraOn() )
                {
                    LOG_DEBUG(Physics, "  avoid hi {:.2f}", _pilotAvoidHighHeight);
                }
#endif
            }
            else if (heIsHigher > 0)
            {
                float surfaceY = GLandscape->SurfaceYAboveWater(Position().X(), Position().Z());
                saturateMin(_pilotAvoidLowHeight, who->Position().Y() - surfaceY - mySize);
                Time until = Glob.time + 10;
                if (_pilotAvoidLow < until)
                {
                    _pilotAvoidLow = until;
                }

#if DIAG_COL
                // if( this==GWorld->CameraOn() )
                {
                    LOG_DEBUG(Physics, "  avoid lo {:.2f}", _pilotAvoidLowHeight);
                }
#endif
            }
        }

        if (info.distance > gap)
        {
            continue;
        }

        if (info.distance < mySize)
        {
            // collission imminent - who is back needs to brake
            // check if he is in front of me
            if (heIsInFrontOfMe)
            {
                // slow down as much as possible
                _pilotSpeed = Type()->_landingSpeed;
            }
        }
    }
}

static int RandomID(AIUnit* unit)
{
    int idSeed = unit->ID() * 25689;
    AIGroup* grp = unit->GetGroup();
    if (!grp)
    {
        return idSeed;
    }
    int grpSeed = grp->ID() * 13;
    AICenter* cnt = grp->GetCenter();
    if (!cnt)
    {
        return idSeed + grpSeed;
    }
    int cntSeed = cnt->GetSide() * 5;
    return idSeed + grpSeed + cntSeed;
}

static int RandomManeuver(AIUnit* unit, int nManeuvers, float duration, float& manPhase)
{
    int seed = RandomID(unit);
    const float loopDuration = duration * nManeuvers;
    float phase = GRandGen.RandomValue(seed) * loopDuration;
    float t = (Glob.time.toFloat() + phase) * (1 / duration);
    PoseidonAssert(t >= 0);
    int tFloor = toIntFloor(t);
    manPhase = t - tFloor;
    return toInt(GRandGen.RandomValue(tFloor) * 100000 + tFloor) % nManeuvers;
}

void AirplaneAuto::AIPilot(AIUnit* unit, float deltaT)
{
    _dirCompensate = 0.9;

    _pilotHelperHeight = true;
    _pilotHelperBankDive = true;
    _rudderWanted = 0;
    _pilotHelperDir = true;
    _pilotHelperThrust = true;

    _forceDive = 1;

    if (unit->GetState() == AIUnit::Stopping)
    {
        if (_landContact && _speed.SquareSize() < 0.1 && !EngineIsOn())
        {
            UpdateStopTimeout();
            unit->SendAnswer(AI::StepCompleted);
            // note: Brain may be nullptr now
            return;
        }
        if (_planeState == Flight)
        {
            _planeState = Marshall;
        }
    }
    else if (unit->GetState() == AIUnit::Stopped)
    {
        EngineOff();
    }
    else if (_planeState == Takeoff)
    {
        Vector3 ilsPos, ilsDir;
        GWorld->GetILS(ilsPos, ilsDir, _targetSide);

        _pilotSpeed = 100;

        Vector3 ilsCPos = ilsPos - ilsDir * 800;
        Vector3 relPos = ilsCPos - Position();
        _pilotHeading = atan2(relPos.X(), relPos.Z());
    }
    else if (_planeState == TaxiIn || _planeState == TaxiOff)
    {
        EngineOn();
        _pilotGear = true;
    }
    else
    {
        _pilotGear = false;

        Target* assigned = unit->GetTargetAssigned();
        if (assigned && _sweepTarget != assigned && assigned->State(unit) >= TargetAlive)
        {
#if LOG_SWEEP
            if (assigned->idExact)
            {
                LOG_DEBUG(Physics, "{}: {:.1f} Switch sweep (disengage) from {} to {}", (const char*)GetDebugName(),
                          Glob.time - Time(0),
                          _sweepTarget.IdExact() ? (const char*)_sweepTarget.IdExact()->GetDebugName() : "<null>",
                          (const char*)assigned->idExact->GetDebugName());
            }
#endif
            _sweepTarget = assigned;
            _sweepState = SweepDisengage;
            _sweepDelay = Glob.time + 10;
        }

        if (_sweepTarget)
        {
            EntityAI* sweepTargetAI = _sweepTarget->idExact;
            bool laserTarget = (sweepTargetAI && sweepTargetAI->GetType()->GetLaserTarget());

            float surfaceY = GLOB_LAND->SurfaceYAboveWater(Position().X(), Position().Z());
            float height = Position().Y() - surfaceY;

            _pilotHeight = height;
            saturate(_pilotHeight, 50, 200);

            _pilotSpeed = Type()->GetMaxSpeedMs() * 0.6f;

            Vector3 relPos;
            // use exact weapon aiming calculation
            if (!CalculateAimWeapon(_currentWeapon, relPos, _sweepTarget))
            {
                // if exact calculation failed, used approxiate calculation
                float tgtDist = _sweepTarget->AimingPosition().Distance(Position());
                float tgtTime = tgtDist * (1.0f / 100);
                Vector3 tgtPos = _sweepTarget->AimingPosition() + _sweepTarget->speed * tgtTime;
                relPos = tgtPos - Position();
            }
            float distXZ = relPos.SizeXZ();

#if _ENABLE_CHEATS
            if (GWorld->CameraOn() == this)
            {
                __nop();
            }
#endif

            bool evade = false;
            bool dogfight = sweepTargetAI && sweepTargetAI->Speed().SquareSize() >= Speed().SquareSize() * Square(0.5);
            float minHeight = dogfight ? 30 : 50;
            if (dogfight)
            {
                // he is on my six
                Vector3 myPosToHim = Position() - sweepTargetAI->Position();
                float hisCosAngle = sweepTargetAI->Direction().CosAngle(myPosToHim);
                if (hisCosAngle > 0.75f && myPosToHim.SquareSizeXZ() < Square(300))
                {
                    evade = true;
                }
            }

            bool specManeuver = false;
            float headChange = 0;
            if (evade && distXZ < 600)
            {
                float manPhase;
                int maneuver = RandomManeuver(unit, 10, 10, manPhase);
#if _ENABLE_CHEATS
                if (GWorld->CameraOn() == this)
                {
                    static const char* name[] = {"Break climb", "Break climb", "Break dive",  "Break dive", "Turn dive",
                                                 "Turn dive",   "Climb stall", "Climb stall", "Run away",   ""};
                    GlobalShowMessage(500, "Man %d (%s): %.3f", maneuver, name[maneuver], manPhase);
                }
#endif

                _pilotSpeed = Type()->GetMaxSpeedMs() * 1.5f;
                switch (maneuver)
                {
                    case 0:
                    case 1: // climb and break
                        _pilotHeight = floatMin(height + 10, 500);
                        headChange = (maneuver & 1) ? +1.3f : -1.3f;
                        specManeuver = true;
                        break;
                    case 2:
                    case 3: // dive and break
                        _pilotHeight = floatMax(minHeight, height - 10);
                        headChange = (maneuver & 1) ? +1.5f : -1.5f;
                        specManeuver = true;
                        break;
                    case 4:
                    case 5: // dive and turn
                        _pilotHeight = floatMax(minHeight, height - 10);
                        headChange = (maneuver & 1) ? +0.5f : -0.5f;
                        specManeuver = true;
                        break;
                    case 6:
                    case 7: // climb, when near stall, turn
                        _forceDive = 0.95f;
                        _pilotHeight = floatMin(height + 10, 500);
                        if (ModelSpeed().Z() > Type()->_stallSpeed && manPhase < 0.6f)
                        {
                            headChange = (maneuver & 1) ? +0.7f : -0.7f;
                        }
                        else
                        {
                            headChange = (maneuver & 1) ? +2.0f : -2.0f;
                        }
                        specManeuver = true;
                        break;
                    case 8: // dive and change direction (run away)
                        _pilotHeight = floatMax(minHeight, height - 30);
                        headChange = sin(manPhase * H_PI * 6) * 0.3f;
                        specManeuver = true;
                        break;
                }
            }

            if (!specManeuver)
            {
                const float safeDistance = dogfight ? 300 : 900;
                if (_sweepState == SweepDisengage)
                {
                    _pilotSpeed = floatMin(180 / 3.6, Type()->GetMaxSpeedMs());
                    Vector3 tgtDir = PositionWorldToModel(_sweepTarget->AimingPosition());
                    float headChangeToTarget = atan2(tgtDir.X(), tgtDir.Z());
                    if (laserTarget)
                    {
                        // in case of laser target disenagage in the direction of source
                        // laser target faces away from laser source
                        Vector3 aimDir = DirectionWorldToModel(-sweepTargetAI->Direction());
                        headChange = atan2(aimDir.X(), aimDir.Z());
                    }
                    else
                    {
                        if (dogfight)
                        {
                            // disengage to his left
                            Vector3 aimDir = DirectionWorldToModel(-sweepTargetAI->DirectionAside());
                            headChange = atan2(aimDir.X(), aimDir.Z());
                        }
                        else
                        {
                            headChange = 0;
                        }
                    }

                    const float predictTime = 2;
                    Vector3 predictedPos = _sweepTarget->position + _sweepTarget->speed * predictTime;
                    float predictedDistXZ2 = predictedPos.DistanceXZ2(Position());

                    _pilotHeight = floatMin(minHeight + distXZ * (1.0f / 500) * (130 - minHeight), 500);
                    if (predictedDistXZ2 > Square(safeDistance) || fabs(headChangeToTarget) < 0.5f || evade)
                    {
                        if (!dogfight || evade || distXZ >= 200 || fabs(headChangeToTarget) < 0.5f)
                        {
                            _sweepState = SweepEngage;
                            _sweepDelay = Glob.time + 25;
                            if (evade)
                            {
                                LOG_DEBUG(Physics, "{}: evading {}", (const char*)GetDebugName(),
                                          (const char*)sweepTargetAI->GetDebugName());
                            }
#if LOG_SWEEP
                            LOG_DEBUG(Physics, "{}: {:.1f} sweep engage ({}), dist {:.0f}, dogfight {}, evade {}",
                                      (const char*)GetDebugName(), Glob.time - Time(0),
                                      sweepTargetAI ? (const char*)sweepTargetAI->GetDebugName() : "<null>", distXZ,
                                      dogfight, evade);
#endif
                        }
                    }
                }
                else if (_sweepState == SweepEngage)
                {
                    float speedAdd = Interpolativ(distXZ, 0, 2000, -1, Type()->GetMaxSpeedMs() * 0.5f);
                    _pilotSpeed = _sweepTarget->speed.Size() + speedAdd;
                    saturateMax(_pilotSpeed, Type()->GetMaxSpeedMs() * 0.5f);

                    _sweepDir = relPos;
                    Vector3 wantedDir = _sweepDir;
                    Vector3 diveDir = _sweepDir;
                    if (dogfight)
                    {
                        _pilotHeight = _sweepTarget->position.Y() - surfaceY;
                        saturateMax(_pilotHeight, minHeight);
                    }

                    Matrix3 orientUp;
                    orientUp.SetUpAndDirection(VUp, Direction());
                    Vector3Val aimDir = orientUp.InverseRotation() * wantedDir;

                    headChange = atan2(aimDir.X(), aimDir.Z());

                    // check if target is in front  of us
                    const float offTreshold = H_PI * 0.1f;
                    if (dogfight && fabs(headChange) > offTreshold)
                    {
                        // target not in front of us - try some offensive maneuver
                        float manPhase;
                        int maneuver = RandomManeuver(unit, 4, 6, manPhase);
#if _ENABLE_CHEATS
                        if (GWorld->CameraOn() == this)
                        {
                            static const char* name[] = {"High yoyo", "Low yoyo", "Turn", ""};
                            GlobalShowMessage(500, "Off %d (%s): %.3f", maneuver, name[maneuver], manPhase);
                        }
#endif
                        switch (maneuver)
                        {
                            case 0: // high yoyo
                                headChange += fSign(headChange) * floatMin(1, (fabs(headChange) - offTreshold) * 4);
                                _pilotHeight = height + 15;
                                break;
                            case 1: // low yoyo
                                headChange += fSign(headChange) * floatMin(1, (fabs(headChange) - offTreshold) * 4);
                                _pilotHeight = floatMax(minHeight, height - 15);
                                break;
                            case 2: // level overturn
                                headChange += fSign(headChange) * floatMin(1, (fabs(headChange) - offTreshold) * 4);
                                break;
                        }
                    }
                    saturate(headChange, -H_PI * 0.8f, +H_PI * 0.8f);

                    if (distXZ < 200 && !dogfight)
                    {
                        _sweepState = SweepFire;
                        _sweepDelay = Glob.time + 5; // start immediatelly

#if LOG_SWEEP
                        LOG_DEBUG(Physics, "{}: {:.1f} sweep fire ({})", (const char*)GetDebugName(),
                                  Glob.time - Time(0),
                                  sweepTargetAI ? (const char*)sweepTargetAI->GetDebugName() : "<null>");
#endif
                    }

                    float minZDist = dogfight ? 30 : 100;
                    if (laserTarget)
                    {
                        // force laser sweep target as fire target
                        _fire.SetTarget(CommanderUnit(), _sweepTarget);
                    }
                    else if (aimDir.Z() > minZDist && (fabs(aimDir.X()) < 25 || fabs(aimDir.X()) < aimDir.Z() * 0.07f))
                    {
                        if (dogfight && evade && distXZ < 200)
                        {
                            // need to evade - otherwise crash or being hit is possible
                            _sweepState = SweepDisengage;
                            _sweepDelay = Glob.time + 10; // start immediatelly
                        }
                        float minFireDist = floatMin(1200, ENGINE_CONFIG.horizontZ);
                        if (_currentWeapon >= 0)
                        {
                            const WeaponModeType* mode = GetWeaponMode(_currentWeapon);
                            if (mode && mode->_ammo)
                            {
                                saturateMin(minFireDist, mode->_ammo->maxRange);
                            }
                        }

                        if (aimDir.Z() > minFireDist)
                        {
                            _pilotHeight = floatMin(minHeight + 20 + distXZ * (1.0f / 500) * (130 - minHeight), 500);
                        }
                        else if (height > minHeight && sweepTargetAI)
                        { // we are flying high enough
                            // actual aiming
                            float visible = _visTracker.KnownValue(this, _currentWeapon, sweepTargetAI);
                            if (visible > 0.7f)
                            {
                                float rawDive = diveDir.Y() * diveDir.InvSizeXZ();
                                _forceDive = rawDive - Type()->_gunDir.Y();

                                // empirical fix: plane almost always fired too low
                                // might be caused by some speed estimation bug
                                _forceDive += 0.001f;

                                // LOG_DEBUG(Physics, "Dive raw {:.3f}, act {:.3f}",rawDive,_forceDive);

                                float maxDive = dogfight ? 0.3f : 0.1f;
                                // positive dive is up
                                saturate(_forceDive, -0.7f, maxDive);

                                // LOG_DEBUG(Physics, "  clip {:.3f}",_forceDive);

                                // LOG_DEBUG(Physics, "  force dive {:.2f}, {:.2f}",_forceDive,Direction().Y());
                                _fire.SetTarget(CommanderUnit(), _sweepTarget);
                            }
                            _pilotHeight = height;
                        }
                    }
                    else
                    {
                        if (distXZ > 1200)
                        {
                            // climb while you can
                            _pilotHeight = 200;
                        }
                        else if (height > minHeight)
                        {
                            // avoid diving  - we need elevator for smooth turn
                            if (!dogfight)
                            {
                                _forceDive = 0;
                            }
                        }
                    }
                }
                else if (_sweepState == SweepFire)
                {
                    Matrix3 orientUp;
                    orientUp.SetUpAndDirection(VUp, Direction());
                    Vector3Val aimDir = orientUp.InverseRotation() * _sweepDir;

                    headChange = atan2(aimDir.X(), aimDir.Z());

                    if (!evade && fabs(headChange) > 0.6f && (distXZ < 100 || distXZ > 300))
                    {
                        _sweepState = SweepDisengage;
                        _sweepDelay = Glob.time + 10; // start immediatelly

#if LOG_SWEEP
                        LOG_DEBUG(Physics, "{}: {:.1f} sweep disengage ({})", (const char*)GetDebugName(),
                                  Glob.time - Time(0),
                                  sweepTargetAI ? (const char*)sweepTargetAI->GetDebugName() : "<null>");
#endif
                    }
                }
            }

            float actHeading = atan2(Direction().X(), Direction().Z());
            _pilotHeading = actHeading + headChange;

            _limitSpeed = GetType()->GetMaxSpeedMs() * 1.5f;
            if (!sweepTargetAI || _sweepTarget->State(unit) < TargetAlive)
            {
                _sweepTarget = nullptr;
            }
            else if (laserTarget)
            {
                saturateMax(_pilotHeight, 200);
            }
            if (Glob.time > _sweepDelay)
            {
                _sweepTarget = nullptr;
            }
        } // sweep
        else
        {
            float speedWanted = 0;
            Vector3 destination = VZero;
            if (!unit->IsSubgroupLeader())
            {
                EngineOn();

                unit->ForceReplan();
                _limitSpeed = GetType()->GetMaxSpeedMs() * 1.5f;
                const float estTTurn = 5.0;
                const float estTSpeed = 5.0;
                AIUnit* leader = unit->GetSubgroup()->Leader();

                Vector3Val relFormWanted = unit->GetFormationRelative() - leader->GetFormationRelative();

                VehicleWithAI* leaderVeh = leader->GetVehicle();

                Matrix4 formTransform;
                float orientTime = 0.5;
                Matrix3Val leaderOrient = leaderVeh->Orientation();
                Matrix3Val leaderDerOrientation = leaderVeh->AngVelocity().Tilda() * leaderOrient;
                Matrix3Val leaderEstOrientation = leaderOrient + leaderDerOrientation * orientTime;
                Vector3 leaderEstDir = leaderEstOrientation.Direction().Normalized();
                formTransform.SetUpAndDirection(VUp, leaderEstDir);
                formTransform.SetPosition(leaderVeh->Position());

                Vector3 formPos = formTransform.FastTransform(relFormWanted);

                destination = formPos + leaderVeh->Speed() * estTTurn;

                Vector3 relForm = PositionWorldToModel(formPos);
                float destinationZ = relForm.Z();
                float leaderSpeedZ = leaderVeh->Speed().Size();
                // adjust speed based on formation position

                Vector3 relDest = destination - Position();
                _pilotHeading = atan2(relDest.X(), relDest.Z());

                float leaderHeading = atan2(leaderEstDir.X(), leaderEstDir.Z());
                float curHeading = atan2(Direction().X(), Direction().Z());

                float distanceToForm = relForm.Size();
                float inFormFactor = 1 - distanceToForm * (1.0 / 1000);
                if (inFormFactor > 0)
                {
                    _pilotHeading = inFormFactor * AngleDifference(leaderHeading, _pilotHeading) + _pilotHeading;
                }

                float leaderInTurn = fabs(leaderHeading - curHeading) * (1.0 / 0.2);

                float maxSlowDown = floatMax(1 - leaderInTurn, -1) * 10;

                float reachSpeed = destinationZ * (1 / estTSpeed);
                saturate(reachSpeed, -maxSlowDown, 200);
                speedWanted = leaderSpeedZ + reachSpeed;

                Vector3Val leaderPos = leaderVeh->Position();
                float leaderSurfY = GLandscape->SurfaceYAboveWater(leaderPos.X(), leaderPos.Z());
                _pilotHeight = leaderPos.Y() - leaderSurfY;
                saturateMax(_pilotHeight, _defPilotHeight);

                saturate(speedWanted, Type()->_landingSpeed * 1.3f, Type()->GetMaxSpeedMs() * 1.5f);
                _pilotSpeed = speedWanted;
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

                destination = SteerPoint(4.0, 6.0);

                const Path& path = unit->GetPath();
                if (path.Size() >= 2)
                {
                    EngineOn();

                    float cost = path.CostAtPos(Position());
                    Vector3 pos = path.PosAtCost(cost, Position());

                    float dist2 = Position().DistanceXZ2(pos);
                    float distEnd2 = Position().DistanceXZ2(path.End());
                    float precision = GetPrecision();
                    // check if we have first point of Plan complete
                    if (distEnd2 < Square(precision) || cost > path.EndCost())
                    {
                        unit->SendAnswer(AI::StepCompleted);
                        //						unit->ForceReplan();
                    }
                    if (dist2 > Square(600))
                    {
                        unit->SendAnswer(AI::StepTimeOut);
                    }
                }
                saturate(speedWanted, Type()->_landingSpeed * 1.3f, Type()->GetMaxSpeedMs() * 1.5f);
                _pilotSpeed = speedWanted;
                Vector3 relDest = destination - Position();
                _pilotHeading = atan2(relDest.X(), relDest.Z());
                _pilotHeight = _defPilotHeight;
            }
        }
        AvoidCollision();
        if (Glob.time < _pilotAvoidHigh)
        {
            saturateMax(_pilotHeight, _pilotAvoidHighHeight);
        }
        else
        {
            _pilotAvoidHighHeight = 0;
        }
        if (Glob.time < _pilotAvoidLow)
        {
            saturateMin(_pilotHeight, _pilotAvoidLowHeight);
            saturateMax(_pilotHeight, 30);
        }
        else
        {
            _pilotAvoidLowHeight = 100000;
        }
    }
}

LSError Airplane::Serialize(ParamArchive& ar)
{
    SERIAL_BASE

    if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
    {
        SERIAL_DEF(rpm, 1); // on/off
        SERIAL_DEF(thrust, 0);
        SERIAL_DEF(thrustWanted, 0); // turning motor on/off
        SERIAL_DEF(rotorSpeed, 0);
        SERIAL_DEF(rotorPosition, 0);
        SERIAL_DEF(elevator, 0);
        SERIAL_DEF(elevatorWanted, 0);
        SERIAL_DEF(rudder, 0);
        SERIAL_DEF(rudderWanted, 0);
        SERIAL_DEF(aileron, 0);
        SERIAL_DEF(aileronWanted, 0);

        SERIAL_DEF(flaps, 0);   // actual flap position
        SERIAL_DEF(gearsUp, 0); // actual gear position
        SERIAL_DEF(brake, 0);

        SERIAL_DEF(pilotBrake, false);
        SERIAL_DEF(pilotGear, true);
    }

    return LSOK;
}

LSError AirplaneAuto::Serialize(ParamArchive& ar)
{
    SERIAL_BASE
    if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
    {
        PARAM_CHECK(ar.Serialize("planeState", *(int*)&_planeState, 1, 1))
        SERIAL_DEF(pilotAvoidHigh, TIME_MIN);
        SERIAL_DEF(pilotAvoidHighHeight, 0);
        SERIAL_DEF(pilotAvoidLow, TIME_MIN);
        SERIAL_DEF(pilotAvoidLowHeight, 100000);
    }

    return LSOK;
}

NetworkMessageType AirplaneAuto::GetNMType(NetworkMessageClass cls) const
{
    switch (cls)
    {
        case NMCUpdateGeneric:
            return NMTUpdateAirplane;
        case NMCUpdatePosition:
            return NMTUpdatePositionAirplane;
        default:
            return base::GetNMType(cls);
    }
}

class IndicesUpdateAirplane : public IndicesUpdateTransport
{
    typedef IndicesUpdateTransport base;

  public:
    int pilotFlaps;
    int gearDammage;
    int pilotGear;

    IndicesUpdateAirplane();
    NetworkMessageIndices* Clone() const override { return new IndicesUpdateAirplane; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesUpdateAirplane::IndicesUpdateAirplane()
{
    pilotFlaps = -1;

    gearDammage = -1;
    pilotGear = -1;
}

void IndicesUpdateAirplane::Scan(NetworkMessageFormatBase* format)
{
    base::Scan(format);

    SCAN(pilotFlaps);

    SCAN(gearDammage);
    SCAN(pilotGear);
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesUpdateAirplane()
{
    using namespace Poseidon;
    return new IndicesUpdateAirplane();
}
namespace Poseidon
{

class IndicesUpdatePositionAirplane : public IndicesUpdatePositionVehicle
{
    typedef IndicesUpdatePositionVehicle base;

  public:
    int thrustWanted;
    int elevatorWanted;
    int rudderWanted;
    int aileronWanted;
    int pilotBrake;

    IndicesUpdatePositionAirplane();
    NetworkMessageIndices* Clone() const override { return new IndicesUpdatePositionAirplane; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesUpdatePositionAirplane::IndicesUpdatePositionAirplane()
{
    thrustWanted = -1;
    elevatorWanted = -1;
    rudderWanted = -1;
    aileronWanted = -1;
    pilotBrake = -1;
}

void IndicesUpdatePositionAirplane::Scan(NetworkMessageFormatBase* format)
{
    base::Scan(format);
    SCAN(thrustWanted);
    SCAN(elevatorWanted);
    SCAN(rudderWanted);
    SCAN(aileronWanted);
    SCAN(pilotBrake);
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesUpdatePositionAirplane()
{
    using namespace Poseidon;
    return new IndicesUpdatePositionAirplane();
}
namespace Poseidon
{

NetworkMessageFormat& AirplaneAuto::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    switch (cls)
    {
        case NMCUpdateGeneric:
            base::CreateFormat(cls, format);

            format.Add("pilotFlaps", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0),
                       DOC_MSG("Position of flaps, wanted by pilot"), ET_ABS_DIF, ERR_COEF_MODE);
            format.Add("gearDammage", NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Gear is damaged"), ET_ABS_DIF,
                       ERR_COEF_MODE);
            format.Add("pilotGear", NDTBool, NCTNone, DEFVALUE(bool, false),
                       DOC_MSG("Position of gear, wanted by pilot"), ET_ABS_DIF, ERR_COEF_MODE);

            break;
        case NMCUpdatePosition:
            base::CreateFormat(cls, format);
            format.Add("thrustWanted", NDTFloat, NCTFloat0To1, DEFVALUE(float, 0), DOC_MSG("Wanted engine thrust"),
                       ET_ABS_DIF, ERR_COEF_VALUE_MAJOR);
            format.Add("elevatorWanted", NDTFloat, NCTFloatM1ToP1, DEFVALUE(float, 0),
                       DOC_MSG("Wanted elevator position"), ET_ABS_DIF, ERR_COEF_VALUE_MAJOR);
            format.Add("rudderWanted", NDTFloat, NCTFloatM1ToP1, DEFVALUE(float, 0), DOC_MSG("Wanted rudder position"),
                       ET_ABS_DIF, ERR_COEF_VALUE_MAJOR);
            format.Add("aileronWanted", NDTFloat, NCTFloatM1ToP1, DEFVALUE(float, 0),
                       DOC_MSG("Wanted aileron position"), ET_ABS_DIF, ERR_COEF_VALUE_MAJOR);
            format.Add("pilotBrake", NDTFloat, NCTFloat0To1, DEFVALUE(float, 0),
                       DOC_MSG("State of brake, wanted by pilot"), ET_ABS_DIF, ERR_COEF_VALUE_MAJOR);

            break;
        default:
            base::CreateFormat(cls, format);
            break;
    }
    return format;
}

TMError AirplaneAuto::TransferMsg(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
            TMCHECK(base::TransferMsg(ctx))
            {
                PoseidonAssert(dynamic_cast<const IndicesUpdateAirplane*>(ctx.GetIndices()))
                    const IndicesUpdateAirplane* indices = static_cast<const IndicesUpdateAirplane*>(ctx.GetIndices());

                ITRANSF(pilotFlaps);

                ITRANSF(gearDammage);
                ITRANSF(pilotGear);
            }
            break;
        case NMCUpdatePosition:
        {
            PoseidonAssert(dynamic_cast<const IndicesUpdatePositionAirplane*>(ctx.GetIndices()))
                const IndicesUpdatePositionAirplane* indices =
                    static_cast<const IndicesUpdatePositionAirplane*>(ctx.GetIndices());
            TMCHECK(base::TransferMsg(ctx))
            ITRANSF(thrustWanted);
            ITRANSF(elevatorWanted);
            ITRANSF(rudderWanted);
            ITRANSF(aileronWanted);
            ITRANSF(pilotBrake);
        }
        break;
        default:
            return base::TransferMsg(ctx);
    }
    return TMOK;
}

float AirplaneAuto::CalculateError(NetworkMessageContext& ctx)
{
    float error = 0;
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
            error += base::CalculateError(ctx);
            {
                PoseidonAssert(dynamic_cast<const IndicesUpdateAirplane*>(ctx.GetIndices()))
                    const IndicesUpdateAirplane* indices = static_cast<const IndicesUpdateAirplane*>(ctx.GetIndices());

                ICALCERR_ABSDIF(int, pilotFlaps, ERR_COEF_MODE)
                ICALCERR_NEQ(bool, gearDammage, ERR_COEF_MODE)
                ICALCERR_NEQ(bool, pilotGear, ERR_COEF_MODE)
            }
            break;
        case NMCUpdatePosition:
        {
            error += base::CalculateError(ctx);
            PoseidonAssert(dynamic_cast<const IndicesUpdatePositionAirplane*>(ctx.GetIndices()))
                const IndicesUpdatePositionAirplane* indices =
                    static_cast<const IndicesUpdatePositionAirplane*>(ctx.GetIndices());

            ICALCERR_ABSDIF(float, thrustWanted, ERR_COEF_VALUE_MAJOR)

            ICALCERR_ABSDIF(float, elevatorWanted, ERR_COEF_VALUE_MAJOR)
            ICALCERR_ABSDIF(float, rudderWanted, ERR_COEF_VALUE_MAJOR)
            ICALCERR_ABSDIF(float, aileronWanted, ERR_COEF_VALUE_MAJOR)
            ICALCERR_ABSDIF(float, pilotBrake, ERR_COEF_VALUE_MAJOR)
        }
        break;
        default:
            error += base::CalculateError(ctx);
            break;
    }
    return error;
}

} // namespace Poseidon
