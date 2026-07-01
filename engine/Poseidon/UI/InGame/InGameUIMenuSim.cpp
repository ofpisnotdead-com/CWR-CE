#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/Entities/Infantry/Person.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/UI/InGame/InGameUIImpl.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/World/Detection/Detector.hpp>
#include <Poseidon/World/Entities/Vehicles/House.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/Network/Network.hpp>
#include <Random/randomGen.hpp>
#include <Poseidon/World/Scene/Camera/CameraHold.hpp>
#include <Poseidon/Dev/Diag/DiagModes.hpp>
#include <Poseidon/World/Entities/Infantry/MoveActions.hpp>
#include <Poseidon/World/Entities/Infantry/ManActs.hpp>
#include <Poseidon/IO/ParamFile/ParamFile.hpp>
#include <Poseidon/Core/resincl.hpp>
#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include <cmath>
#include <utility>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
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
#include <Poseidon/Foundation/platform.hpp>
#ifdef _WIN32
#include <io.h>
#endif
#include <SDL3/SDL_scancode.h>
#include <Poseidon/Foundation/Algorithms/Qsort.hpp>
#include <Poseidon/Game/Chat.hpp>

using namespace Poseidon;
namespace Poseidon
{

} // namespace Poseidon
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/Foundation/Strings/Mbcs.hpp>

namespace Poseidon
{
RString GetUserDirectory();
}

// Global-scope symbols (defined outside namespace Poseidon); declared here so the
// in-namespace calls below resolve to the true global definitions via fall-through lookup.
void AddDropAllActions(AIUnit* unit, UIActions& actions);
void ActivateSensor(ArcadeSensorActivation activ);
Poseidon::RadioChannel* FindChannel(AIUnit* unit, int channel);
extern int MaxCustomSoundSize;

namespace Poseidon
{

// Forward declarations from inGameUI.cpp
enum IDS : int;
RString LocalizeString(IDS ids);
RString LocalizeString(int ids);
RString LocalizeString(const char* str);
RString Localize(RString str);
AIUnit* GetSelectedUnit(int i);
void SetSelectedUnit(int i, AIUnit* unit);
void ClearSelectedUnits();
bool IsEmptySelectedUnits();
PackedBoolArray ListSelectedUnits();
int ValidateWeapon(EntityAI* vehicle, int weapon);
bool CheckJoin(AIGroup* grp);
OLink<AIUnit>* GetGSelectedUnits();
Team GetTeam(int i);
#define COMMAND_TIMEOUT 480.0 // 8 min

static bool IsManual(EntityAI* veh, AIUnit* unit)
{
    if (!unit->IsPlayer())
    {
        return false;
    }

    if (!GWorld->PlayerManual())
    {
        return false;
    }
    if (GWorld->GetPlayerSuspended())
    {
        return false;
    }
    return true;
}

void InGameUI::RevealTarget(Target* tgt, float spot)
{
    AIUnit* unit = GWorld->FocusOn();
    if (!unit)
    {
        return;
    }
    EntityAI* vehicle = unit->GetVehicle();
    if (!vehicle)
    {
        return;
    }
    AIGroup* grp = unit->GetGroup();
    if (!grp)
    {
        return;
    }

    if (!tgt->isKnown || tgt->delay > Glob.time + 1 || tgt->FadingSpotability() < spot)
    {
        // report only targets that are not known

        tgt->isKnown = true;
        if (tgt->delay > Glob.time + 1)
        {
            tgt->delay = Glob.time + 1;
        }
        tgt->delaySensor = Glob.time - 5;
        tgt->idSensor = unit->GetPerson();
        tgt->lastSeen = Glob.time;
        // use current properties
        EntityAI* exact = tgt->idExact;
        tgt->position = exact->AimingPosition();
        tgt->speed = exact->Speed();
        tgt->posError = VZero;
        if (tgt->FadingSpotability() < spot)
        {
            tgt->spotability = spot;
            tgt->spotabilityTime = Glob.time;
        }
    }
    // if it is dead body of my group unit, report it
    EntityAI* obj = tgt->idExact;
    if (obj)
    {
        AIUnit* tgtUnit = obj->CommanderUnit();
        if (tgtUnit && tgtUnit->GetLifeState() != AIUnit::LSAlive && tgtUnit->GetGroup() == grp)
        {
            grp->SendUnitDown(unit, tgtUnit);
        }
        else if (obj->IsDammageDestroyed())
        {
            // check taregt side
            if (grp->GetCenter()->IsEnemy(obj->Vehicle::GetTargetSide()))
            {
                // if it is dead enemy, you may report it
                // but only when I killed him
                if ((tgt->idKiller == vehicle || tgt->idKiller == unit->GetPerson()) && tgt->timeReported <= TIME_MIN)
                {
                    if (grp->NUnits() > 1)
                    {
                        // send radio message
                        grp->SendObjectDestroyed(unit, tgt->type);
                    }
                    // mark as reported
                    tgt->timeReported = Glob.time;
                    tgt->posReported = tgt->position;
                }
            }
        }
    }
}

Target* InGameUI::CheckCursorTarget(Vector3& itPos, Vector3Par cursorDir, const Camera& camera, CameraType cam,
                                    bool knownOnly)
{
    AIUnit* unit = GWorld->FocusOn();
    AISubgroup* subgroup = unit->GetSubgroup();
    AIGroup* group = subgroup->GetGroup();
    EntityAI* vehicle = unit->GetVehicle();

    float maxVisDist = floatMax(vehicle->GetType()->GetIRScanRange(), TACTICAL_VISIBILITY);

    CollisionBuffer retVal;
    GLandscape->ObjectCollision(retVal, vehicle, nullptr, camera.Position(), camera.Position() + cursorDir * maxVisDist,
                                0, ObjIntersectView);
    // select first intersected object
    int minI = -1;
    float minT = 1e10;
    for (int i = 0; i < retVal.Size(); i++)
    {
        const CollisionInfo& info = retVal[i];
        if (!info.object)
        {
            continue;
        }
        if (info.under < minT)
        {
            minT = info.under;
            minI = i;
        }
    }
    Target* iTarget = nullptr;

    itPos = camera.Position();

#if 1
    if (minI >= 0)
    {
        // check intersection with land
        const CollisionInfo& info = retVal[minI];
        EntityAI* ai = dyn_cast<EntityAI, Object>(info.object);
        if (ai)
        {
            Vector3 iPos = info.object->WorldTransform().FastTransform(info.pos);
            Vector3 iDir = iPos - camera.Position();
            Vector3 iDirN = iDir.Normalized();
            float iDirSize = iDir * iDirN;
            Vector3 lPos;
            float isect = GLandscape->IntersectWithGroundOrSea(&lPos, camera.Position(), iDirN, 0, iDirSize * 1.1);
            // we know what is

            if (isect > iDirSize)
            {
                // no ground obstacle
                // check if we know the target
                if (knownOnly)
                {
                    iTarget = group->FindTarget(ai);
                }
                else
                {
                    iTarget = group->FindTargetAll(ai);
                }
                if (iTarget)
                {
                    itPos = iPos;
                }
                // GScene->DrawCollisionStar(iPos,1,PackedColor(Color(0,1,0)));
            }
            else
            {
                // ground occluding
                // GScene->DrawCollisionStar(lPos,0.5,PackedColor(Color(1,0,0)));
            }
        }
    }

    // if we are aiming at something directly, return it
    if (iTarget)
    {
        return iTarget;
    }
#endif

    // if not, we should use cursor range look-up

    float distNearest = 1e10;
    const TargetList& visibleList = *VisibleList();
    int i, n = visibleList.Size();
    for (i = 0; i < n; i++)
    {
        Target* tar = visibleList[i];

        TargetType* veh = tar->idExact;
        if (!veh)
        {
            continue; // invisible
        }

        if (tar->vanished
#if _ENABLE_CHEATS
            && !_showAll
#endif
        )
            continue; // invisible

        if (vehicle == veh)
        {
            if (_mode != UIStrategy)
            {
                continue;
            }
            if (GWorld->GetCameraType() < CamExternal)
            {
                continue;
            }
        }

        // static (mostly large) objects do not use range look-up
        if (veh->GetType()->IsKindOf(GWorld->Preloaded(VTypeStatic)))
        {
            continue;
        }

        if (knownOnly)
        {
            if (!tar->IsKnownBy(unit)
#if _ENABLE_CHEATS
                && !_showAll
#endif
            )
                continue;
        }
        else
        {
            // visible only
            // check distance to target
            float irRange = vehicle->GetType()->GetIRScanRange();
            if (!veh || !veh->GetType()->GetIRTarget())
            {
                irRange = 0;
            }
            float maxDistance = floatMax(irRange, TACTICAL_VISIBILITY);

            float dist2 = veh->Position().Distance2(vehicle->Position());
            if (dist2 > Square(maxDistance))
            {
                continue;
            }
            // check LOS to target
            float vis = GLandscape->Visible(vehicle, veh);
            if (vis < 0.25)
            {
                continue;
            }
        }

        Vector3Val pos = (
#if _ENABLE_CHEATS
            (_showAll || !knownOnly)
#else
            !knownOnly
#endif
                ? tar->idExact->AimingPosition()
                : tar->AimingPosition());
        Vector3Val dir = pos - camera.Position();

        float bounding = veh->GetShape()->GeometrySphere() * 0.5;

        // check nearest point on line camPos,cursorDir to target aiming position
        Vector3 cDir = cursorDir.Normalized();
        float nearestT = cDir * dir;
        saturateMax(nearestT, 0);
        Vector3 nearP = camera.Position() + cDir * nearestT;

        float distToObj = nearP.Distance(pos);
        // check if object is within cursor range
        float maxDist = bounding + 0.03 * camera.Left() * nearestT;
        if (distToObj < maxDist)
        {
            // GScene->DrawCollisionStar(nearP);
            if (distToObj < distNearest)
            {
                distNearest = distToObj;
                iTarget = tar;
                itPos = nearP;
            }
        }
    }
    return iTarget;
}

void InGameUI::SimulateHUDNonAI(const Camera& camera, Entity* vehicle, CameraType cam, float deltaT)
{
    auto& input = InputSubsystem::Instance();
    if (input.IsMouseCursorActive())
    {
        _worldCursorTime = Glob.uiTime;
    }

    _modeAuto = UIFire;
    _mode = UIFire;

    CameraHolder* camHolder = dyn_cast<CameraHolder, Object>(vehicle);
    if (camHolder && camHolder->GetManual())
    {
        // driver's vehicle is always local
        Vector3 weaponDir = GetCursorDirection();
        camHolder->AimDriver(weaponDir);
    }
}

void InGameUI::SimulateHUD(const Camera& camera, EntityAI* vehicle, CameraType cam, float deltaT)
{
    auto& input = InputSubsystem::Instance();
    if (input.GetActionToDo(UAHelp))
    {
        ShowLastHint();
    }

    AIUnit* unit = GWorld->FocusOn();
    if (!unit)
    {
        return;
    }
    AISubgroup* subgroup = unit->GetSubgroup();
    if (!subgroup)
    {
        return;
    }
    AIGroup* group = subgroup->GetGroup();
    if (!group)
    {
        return;
    }
    vehicle = unit->GetVehicle();
    PoseidonAssert(vehicle);
    if (!vehicle)
    {
        return;
    }

    if (!ShouldShowGameplayHUD())
    {
        _actions.Clear();
        return;
    }

    ProcessMenu(camera, vehicle);

    if (GWorld->HasMap())
    {
        return;
    }

    if (!GWorld->Chat() && !GWorld->GetPlayerSuspended())
    {
        ProcessActions(unit);
    }

    // focus may changed !!!
    unit = GWorld->FocusOn();
    if (!unit)
    {
        return;
    }
    subgroup = unit->GetSubgroup();
    if (!subgroup)
    {
        return;
    }
    group = subgroup->GetGroup();
    if (!group)
    {
        return;
    }
    vehicle = unit->GetVehicle();
    PoseidonAssert(vehicle);
    if (!vehicle)
    {
        return;
    }

    bool isCommander = unit == vehicle->CommanderUnit();
    bool isPilot = unit == vehicle->PilotUnit();
    bool isGunner = unit == vehicle->GunnerUnit() && vehicle->IsGunner(cam);
    bool isObserver = unit == vehicle->ObserverUnit() && vehicle->IsGunner(cam);

    bool isGunnerFire = unit == vehicle->GunnerUnit();

    Transport* transport = unit->GetVehicleIn();
    bool vehicleCommander = (transport && transport->Commander() == unit->GetPerson());

    // bool commandsToGunner = transport->GunnerUnit()!=unit;
    // bool commandsToPilot = transport->PilotUnit()!=unit;

    if (input.IsMouseCursorActive())
    {
        _worldCursorTime = Glob.uiTime;
    }

    int nUnits = group->NUnits();
#if _ENABLE_CHEATS
    if (CHECK_DIAG(DECombat))
        SwitchToStrategy(vehicle);
    else
#endif
        if (vehicleCommander)
    {
        SwitchToStrategy(vehicle);
    }
    else if (!unit->IsGroupLeader() || nUnits <= 1)
    {
        SwitchToFire(vehicle);
    }
    else if (!vehicle->IsCommander(cam))
    {
        SwitchToFire(vehicle);
    }
    else if (!vehicle->IsGunner(cam))
    {
        SwitchToStrategy(vehicle);
    }
    else if (IsEmptySelectedUnits())
    {
        SwitchToFire(vehicle);
    }
    else
    {
        SwitchToStrategy(vehicle);
    }

#if _ENABLE_CHEATS
    if (input.GetCheat1ToDo(SDL_SCANCODE_A))
    {
        _showAll = !_showAll;
        GlobalShowMessage(500, "ShowAll %s", _showAll ? "On" : "Off");
    }

    // show / hide status displays
    if (input.GetCheat1ToDo(SDL_SCANCODE_SEMICOLON))
    {
        bool show = !_showUnitInfo;
        ShowAll(show);
        GChatList.Enable(show);
    }

    if (input.GetCheat1ToDo(SDL_SCANCODE_APOSTROPHE))
    {
        ShowCursors(!_showCursors);
    }
#endif

    if (IsManual(vehicle, unit) && (isCommander || isGunnerFire))
    {
        // only commander can toggle weapons
        int weapon = vehicle->SelectedWeapon();
        bool weaponChanged = false;
        if (input.GetActionToDo(UAToggleWeapons))
        {
            weapon = vehicle->NextWeapon(weapon);
            weaponChanged = true;
        }
        else if (weapon >= 0 && weapon < vehicle->NMagazineSlots())
        {
            const MagazineSlot& slot = vehicle->GetMagazineSlot(weapon);
            if (!slot._muzzle->_showEmpty && vehicle->EmptySlot(slot))
            {
                weapon = vehicle->NextWeapon(weapon);
                weaponChanged = true;
            }
        }

        if (weaponChanged)
        {
            weapon = ValidateWeapon(vehicle, weapon);
            SelectWeapon(unit, weapon);
        }
    }

    // list of visible objects
    const TargetList& visibleList = *VisibleList();
    int i, n = visibleList.Size();

    // find visible target with cursor on
    _target = nullptr;
    _housePos = -1;
    Vector3 housePosition = VZero;

    Vector3 cursorDir = GetCursorDirection();

    vehicle->LimitCursor(cam, cursorDir);

    if (input.GetActionToDo(UARevealTarget) && !GWorld->HasMap())
    { // no reset - some processing needs to be done yet
        Vector3 itPos;
        Target* manualTarget = CheckCursorTarget(itPos, cursorDir, camera, cam, false);

        if (manualTarget)
        {
            RevealTarget(manualTarget, 0.3);

            _target = manualTarget;

            // if some units are selected, xmit Target

            if (!IsEmptySelectedUnits())
            {
                if (group->GetCenter()->IsEnemy(manualTarget->side))
                {
                    group->SendTarget(manualTarget, false, false, ListSelectedUnits());
                    ClearSelectedUnits();
                }
            }
        }
    }

    if (!_target)
    {
        // check which object is showed by cursor
        Vector3 itPos;
        Target* iTarget = CheckCursorTarget(itPos, cursorDir, camera, cam, true);
        if (iTarget)
        {
            _target = iTarget;
            // check if we should look-up in-house positions

            _housePos = -1;

            // find nearest in-house position to the point we are aiming at
            EntityAI* veh = iTarget->idExact;
            const IPaths* house = veh->GetIPaths();
            float minDist2 = 1e10;
            if (house && house->NPos() > 0)
            {
                for (int j = 0; j < house->NPos(); j++)
                {
                    Vector3Val pos = house->GetPosition(house->GetPos(j));
                    float dist2 = pos.Distance2(itPos);
                    if (minDist2 > dist2)
                    {
                        minDist2 = dist2;
                        _housePos = j;
                        housePosition = pos;
                    }
                }
            }
        }
    }

    // check list of selected units
    bool notEmpty = false;
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* u = GetSelectedUnit(i);
        if (u)
        {
            notEmpty = true;
            break;
        }
    }

    _modeAuto = _mode;
    if (_mode == UIStrategy && input.IsKeyDown(SDL_SCANCODE_LALT))
    {
        _modeAuto = UIStrategyWatch;
        if (_target)
        {
            if (_housePos >= 0)
            {
                _groundPoint = housePosition;
#if _ENABLE_CHEATS
                else if (_showAll && _target->idExact) _groundPoint = _target->idExact->AimingPosition();
#endif
            }
            else
            {
                _groundPoint = _target->AimingPosition();
            }
        }
        else
        {
            _groundPoint = GLOB_LAND->IntersectWithGroundOrSea(camera.Position(), cursorDir);
        }
    }
    else if (_target)
    {
        if (_housePos >= 0)
        {
            _groundPoint = housePosition;
#if _ENABLE_CHEATS
            else if (_showAll && _target->idExact) _groundPoint = _target->idExact->AimingPosition();
#endif
        }
        else
        {
            _groundPoint = _target->AimingPosition();
        }
        if (_mode == UIStrategy)
        {
            EntityAI* targetAI = _target.IdExact();
            if (targetAI)
            {
                if (!notEmpty)
                {
                    AIUnit* u = targetAI->CommanderUnit();
                    if (unit->IsGroupLeader() && u && u->GetGroup() == unit->GetGroup())
                    { // inside my group / subgroup
                        _modeAuto = UIStrategySelect;
                    }
                    if (vehicleCommander)
                    {
                        if (!group->GetCenter()->IsFriendly(_target->side))
                        {
                            // Enemy target - attack
                            _modeAuto = UIStrategyFire;
                        }
                        else
                        {
                            // no target - move
                            _modeAuto = UIStrategyMove;
                        }
                    }
                }
                else
                {
                    const VehicleType* target = GWorld->Preloaded(VTypeTarget);
                    if (_target->type->IsKindOf(target))
                    {
                        _modeAuto = UIStrategyAttack;
                    }
                    else if (group->GetCenter()->IsFriendly(_target->side))
                    { // Friendly target
                        Transport* targetTransport = dyn_cast<Transport>(targetAI);
                        if (targetTransport &&
                            (targetTransport->QCanIGetIn() || targetTransport->QCanIGetInCommander() ||
                             targetTransport->QCanIGetInGunner() || targetTransport->QCanIGetInCargo()))
                        { // empty vehicle
                            _modeAuto = UIStrategyGetIn;
                        }
                        else
                        {
                            AIUnit* u = targetAI->CommanderUnit();
                            if (unit->IsGroupLeader() && u && u->GetGroup() == unit->GetGroup())
                            { // inside my group / subgroup
                                _modeAuto = UIStrategySelect;
                            }
                            else
                            {
                                // outside my group
                                if (notEmpty)
                                {
                                    _modeAuto = UIStrategyMove;
                                }
                            }
                        }
                    }
                    else
                    { // enemy / neutral / unknown target
                        _modeAuto = UIStrategyAttack;
                    }
                }
            }
        }
        else if (_mode == UIFire)
        {
            _modeAuto = UIFirePosLock;
        }
    }
    else if (_lockTarget)
    {
        if (_mode == UIStrategy && !notEmpty)
        {
            _modeAuto = UIStrategyFire;
        }
    }
    else
    {
        _groundPoint = GLOB_LAND->IntersectWithGroundOrSea(camera.Position(), cursorDir);
    }
    _groundPointValid = camera.Position().Distance2(_groundPoint) < Square(2000);
    if (_groundPoint.Y() < -5)
    {
        _groundPointValid = false;
    }
    if (_groundPointValid)
    {
        _groundPointDistance = _groundPoint.Distance(vehicle->Position());
        if (_modeAuto == UIStrategy && (notEmpty || vehicleCommander))
        {
            _modeAuto = UIStrategyMove;
        }
    }

#if _ENABLE_CHEATS
    if (input.GetCheat1ToDo(SDL_SCANCODE_W))
    {
        if (_target.IdExact())
        {
            GWorld->SwitchCameraTo(_target.IdExact(), GWorld->GetCameraType());
            GWorld->UI()->ResetHUD();
        }
    }
#endif

    // Commander

    if (isCommander && IsManual(vehicle, unit))
    {
        if (!isPilot)
        {
            PoseidonAssert(transport);
            bool left = false, right = false;
            bool leftUp = false, rightUp = false;
            bool back = false;
            bool slow = false, forw = false, fast = false;
            if (input.GetActionToDo(UATurnLeft))
            {
                left = true;
            }
            if (_leftPressed && !input.GetAction(UATurnLeft))
            {
                _leftPressed = false;
                leftUp = true;
            }
            if (input.GetActionToDo(UATurnRight))
            {
                right = true;
            }
            if (_rightPressed && !input.GetAction(UATurnRight))
            {
                _rightPressed = false;
                rightUp = true;
            }
            if (input.GetActionToDo(UAMoveSlowForward))
            {
                slow = true;
            }
            if (input.GetActionToDo(UAMoveFastForward))
            {
                fast = true;
            }
            if (input.GetActionToDo(UAMoveForward))
            {
                if (input.GetAction(UATurbo))
                {
                    fast = true;
                }
                else
                {
                    forw = true;
                }
            }
            if (input.GetActionToDo(UAMoveBack))
            {
                back = true;
            }

            if (left)
            {
                if (transport->IsLocal())
                {
                    transport->SendSimpleCommand(SCLeft);
                }
                else
                {
                    RadioMessageVSimpleCommand msg(transport, SCLeft);
                    GetNetworkManager().SendRadioMessage(&msg);
                }
                _leftPressed = true;
            }
            if (right)
            {
                if (transport->IsLocal())
                {
                    transport->SendSimpleCommand(SCRight);
                }
                else
                {
                    RadioMessageVSimpleCommand msg(transport, SCRight);
                    GetNetworkManager().SendRadioMessage(&msg);
                }
                _rightPressed = true;
            }
            if (leftUp || rightUp)
            {
                // Vector3Val dir = transport->Direction();

                // predict orientation in estT
                const float estT = 0.35f;
                const Matrix3& orientation = transport->Orientation();
                Matrix3Val derOrientation = transport->AngVelocity().Tilda() * orientation;
                Matrix3Val estOrientation = orientation + derOrientation * estT;

                Vector3Val estDirection = estOrientation.Direction().Normalized();
                float azimut = atan2(estDirection.X(), estDirection.Z());

                if (transport->IsLocal())
                {
                    transport->SendStopTurning(azimut);
                }
                else
                {
                    RadioMessageVStopTurning msg(transport, azimut);
                    GetNetworkManager().SendRadioMessage(&msg);
                }
            }
            if (back)
            {
                if (transport->IsLocal())
                {
                    transport->SendSimpleCommand(SCKeyDown);
                }
                else
                {
                    RadioMessageVSimpleCommand msg(transport, SCKeyDown);
                    GetNetworkManager().SendRadioMessage(&msg);
                }
            }
            if (slow)
            {
                if (transport->IsLocal())
                {
                    transport->SendSimpleCommand(SCKeySlow);
                }
                else
                {
                    RadioMessageVSimpleCommand msg(transport, SCKeySlow);
                    GetNetworkManager().SendRadioMessage(&msg);
                }
            }
            if (forw)
            {
                if (transport->IsLocal())
                {
                    transport->SendSimpleCommand(SCKeyUp);
                }
                else
                {
                    RadioMessageVSimpleCommand msg(transport, SCKeyUp);
                    GetNetworkManager().SendRadioMessage(&msg);
                }
            }
            if (fast)
            {
                if (transport->IsLocal())
                {
                    transport->SendSimpleCommand(SCKeyFast);
                }
                else
                {
                    RadioMessageVSimpleCommand msg(transport, SCKeyFast);
                    GetNetworkManager().SendRadioMessage(&msg);
                }
            }
        }

        if (!isGunner && transport)
        {
            if (transport->IsManualFire())
            {
                int weapon = vehicle->SelectedWeapon();
                weapon = ValidateWeapon(vehicle, weapon);
                if (weapon >= 0)
                {
                    const WeaponModeType* mode = vehicle->GetWeaponMode(weapon);
                    bool fire = false;
                    const Magazine* mag = vehicle->GetMagazineSlot(weapon)._magazine;

                    if (mode && mode->_autoFire && mag->_ammo > 0)
                    {
                        if (input.GetAction(UAFire) > 0 || input.GetFire())
                        {
                            fire = true;
                        }
                        if (
                            // in not driver, click can be command to driver
                            // rule is to fire only when target is locked
                            (_lockTarget || isPilot) && !GWorld->HasMap() &&
                            (!ModeIsStrategy(_mode) || _modeAuto == UIStrategyFire) && input.GetMouseLToDo())
                        {
                            fire = true;
                        }
                    }
                    else
                    {
                        fire = input.GetActionToDo(UAFire) || input.GetFireToDo();
                        if (
                            // in not driver, click can be command to driver
                            // rule is to fire only when target is locked
                            (_lockTarget || isPilot) && !GWorld->HasMap() && input.GetMouseL())
                        {
                            fire = true;
                        }
                    }
                    if (fire)
                    {
                        vehicle->FireWeapon(weapon, _lockTarget.IdExact());
                        if (transport)
                        {
                            const WeaponModeType* mode = transport->GetWeaponMode(weapon);
                            if (!mode || !mode->_autoFire)
                            {
                                transport->SetFirePrepare(true);
                            }
                        }
                    }
                }
            }
            else
            {
                bool fire = false;
                if (input.GetActionToDo(UAFire) || input.GetFireToDo())
                {
                    fire = true;
                }
                if (
                    // in not driver, click can be command to driver
                    // rule is to fire only when target is locked
                    (_lockTarget || isPilot) && !GWorld->HasMap() &&
                    (!ModeIsStrategy(_mode) || _modeAuto == UIStrategyFire) && input.GetMouseLToDo())
                {
                    fire = true;
                }
                if (fire)
                {
                    if (transport->IsLocal())
                    {
                        transport->SendSimpleCommand(SCKeyFire);
                    }
                    else
                    {
                        RadioMessageVSimpleCommand msg(transport, SCKeyFire);
                        GetNetworkManager().SendRadioMessage(&msg);
                    }
                }
            }

            if (input.GetActionToDo(UALockTarget) && !GWorld->HasMap())
            {
                if (_target.IdExact() && _target.IdExact() != vehicle && !_target->vanished)
                {
                    if (transport->IsLocal())
                    {
                        transport->SendTarget(_target);
                    }
                    else
                    {
                        RadioMessageVTarget msg(transport, _target);
                        GetNetworkManager().SendRadioMessage(&msg);
                    }
                    _lockTarget = _target;
                    _timeSendTarget = UITIME_MAX;
                    _lockAimValidUntil = Glob.uiTime - 60;
                }
                else
                {
                    if (transport->IsLocal())
                    {
                        transport->SendTarget(nullptr);
                    }
                    else
                    {
                        RadioMessageVTarget msg(transport, nullptr);
                        GetNetworkManager().SendRadioMessage(&msg);
                    }
                    _lockTarget = nullptr;
                    _timeSendTarget = UITIME_MAX;
                    _lockAimValidUntil = Glob.uiTime - 60;
                }
            }

            if (_lockTarget && Glob.uiTime >= _timeSendTarget)
            {
                if (transport->IsLocal())
                {
                    transport->SendTarget(_lockTarget);
                }
                else
                {
                    RadioMessageVTarget msg(transport, _lockTarget);
                    GetNetworkManager().SendRadioMessage(&msg);
                }
                _timeSendTarget = UITIME_MAX;
            }
        }
    }

    // Gunner

    if (isGunnerFire && IsManual(vehicle, unit))
    {
        if (vehicle->CanFire())
        {
            int weapon = vehicle->SelectedWeapon();
            weapon = ValidateWeapon(vehicle, weapon);
            if (weapon >= 0)
            {
                const WeaponModeType* mode = vehicle->GetWeaponMode(weapon);
                bool fire = false;
                const Magazine* mag = vehicle->GetMagazineSlot(weapon)._magazine;

                if (mode && mode->_autoFire && mag->_ammo > 0)
                {
                    fire = input.GetAction(UAFire) > 0 || input.GetFire();
                }
                else
                {
                    fire = input.GetActionToDo(UAFire) || input.GetFireToDo();
                }
                if (fire)
                {
                    vehicle->FireWeapon(weapon, _lockTarget.IdExact());
                    if (transport)
                    {
                        const WeaponModeType* mode = transport->GetWeaponMode(weapon);
                        if (!mode || !mode->_autoFire)
                        {
                            transport->SetFirePrepare(true);
                        }
                    }
                }
            }
        }
    }

    if (IsManual(vehicle, unit))
    {
        if (input.GetActionToDo(UALockTargets))
        {
            // switch target - use target list (tactical display)
            if (input.IsKeyDown(SDL_SCANCODE_LSHIFT) || input.GetKey(SDL_SCANCODE_RSHIFT))
            {
                PrevTarget(vehicle);
            }
            else
            {
                NextTarget(vehicle);
            }
        }
    }
    else
    {
        Target* tgt = vehicle->GetFireTarget();
        if (!tgt)
        {
            tgt = unit->GetTargetAssigned();
        }
        if (tgt)
        {
            for (int i = 0; i < visibleList.Size(); i++)
            {
                Target* tar = visibleList[i];
                if (tar == tgt)
                {
                    _lockTarget = tar;
                    _timeSendTarget = UITIME_MAX;
                    break;
                }
            }
        }
        else
        {
            _lockTarget = nullptr;
        }
    }
    // react to mouse buttons
    if (ModeIsStrategy(_mode))
    {
        if (input.GetMouseLToDo())
        {
            _mouseDown = true;
            _mouseDownTime = Glob.uiTime + 0.2;
            _startSelection = GetCursorDirection();
            _fireEnabled = false;
        }
        else
        {
            if (_mouseDown)
            {
                Vector3 dir = GetCursorDirection();
                if (dir.Distance2(_startSelection) > Square(0.01) && Glob.uiTime >= _mouseDownTime)
                {
                    _mouseDown = false;
                    _dragging = true;
                }
                else if (!input.GetMouseL())
                {
                    _mouseDown = false;
                    IssueCommand(vehicle);
                    _menuType = MTNone;
                }
            }
            if (_dragging)
            {
                _endSelection = GetCursorDirection();
                if (!input.GetMouseL())
                {
                    AUTO_STATIC_ARRAY(OLink<AIUnit>, units, 64);
                    AUTO_STATIC_ARRAY(LinkTarget, enemies, 64);

                    Matrix4Val camInvTransform = camera.GetInvTransform();
                    Vector3 posStart = camInvTransform.Rotate(_startSelection);
                    Vector3 posEnd = camInvTransform.Rotate(_endSelection);
                    if (posStart.Z() > 0 && posEnd.Z() > 0)
                    {
                        float invZ = 1.0 / posStart.Z();
                        float xs = 0.5 * (1.0 + posStart.X() * invZ * camera.InvLeft());
                        float ys = 0.5 * (1.0 - posStart.Y() * invZ * camera.InvTop());
                        invZ = 1.0 / posEnd.Z();
                        float xe = 0.5 * (1.0 + posEnd.X() * invZ * camera.InvLeft());
                        float ye = 0.5 * (1.0 - posEnd.Y() * invZ * camera.InvTop());
                        if (xs > xe)
                        {
                            swap(xs, xe);
                        }
                        if (ys > ye)
                        {
                            swap(ys, ye);
                        }
                        int n = visibleList.Size();
                        for (int i = 0; i < n; i++)
                        {
                            Target& tar = *visibleList[i];
                            if (!tar.IsKnownBy(unit) || tar.vanished)
                            {
                                continue;
                            }
                            EntityAI* veh = tar.idExact;
                            if (!veh)
                            {
                                continue;
                            }
                            // check position
                            Vector3 dir = tar.AimingPosition() - camera.Position();
                            Vector3 pos = camInvTransform.Rotate(dir);
                            if (pos.Z() <= 0)
                            {
                                continue;
                            }
                            invZ = 1.0 / pos.Z();
                            float x = 0.5 * (1.0 + pos.X() * invZ * camera.InvLeft());
                            float y = 0.5 * (1.0 - pos.Y() * invZ * camera.InvTop());
                            if (x < xs)
                            {
                                continue;
                            }
                            if (x > xe)
                            {
                                continue;
                            }
                            if (y < ys)
                            {
                                continue;
                            }
                            if (y > ye)
                            {
                                continue;
                            }
                            AIUnit* u = veh->CommanderUnit();
                            if (u && u->GetGroup() == group)
                            {
                                if (u != unit)
                                {
                                    units.Add(u);
                                }
                            }
                            else if (group->GetCenter()->IsEnemy(tar.side))
                            {
                                enemies.Add(&tar);
                            }
                        }
                    }

                    if (input.IsKeyDown(SDL_SCANCODE_LSHIFT) || input.GetKey(SDL_SCANCODE_RSHIFT))
                    {
                        for (int i = 0; i < units.Size(); i++)
                        {
                            AIUnit* u = units[i];
                            int id = u->ID();
                            if (GetSelectedUnit(id - 1))
                            {
                                SetSelectedUnit(id - 1, nullptr);
                            }
                            else
                            {
                                SetSelectedUnit(id - 1, u);
                                _lastSelTime[id - 1] = Glob.uiTime;
                            }
                        }
                    }
                    else if (IsEmptySelectedUnits() || enemies.Size() == 0)
                    {
                        ClearSelectedUnits();
                        for (int i = 0; i < units.Size(); i++)
                        {
                            AIUnit* u = units[i];
                            int id = u->ID();
                            SetSelectedUnit(id - 1, u);
                            _lastSelTime[id - 1] = Glob.uiTime;
                        }
                    }
                    else
                    {
#if 1
                        Command cmd;
                        cmd._message = Command::AttackAndFire;
                        cmd._targetE = enemies[0];
                        cmd._destination = vehicle->Position();

                        for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
                        {
                            AIUnit* u = GetSelectedUnit(i);
                            if (u)
                            {
                                u->AssignTarget(enemies[0]);
                            }
                        }

                        if (CheckJoin(group))
                        {
                            cmd._context = Command::CtxUIWithJoin;
                            cmd._joinToSubgroup = group->MainSubgroup();
                        }
                        else
                        {
                            cmd._context = Command::CtxUI;
                        }

                        group->SendCommand(cmd, ListSelectedUnits());
#else
                        group->SendTarget(enemies[0], false, false, ListSelectedUnits());
                        group->SendTarget(enemies[0], false, true, ListSelectedUnits());
#endif
                        ClearSelectedUnits();
                    }
                    _dragging = false;
                }
            }
        }
    }
    else
    {
        bool fire = false;
        if (input.GetMouseLToDo())
        {
            _fireEnabled = true;
            fire = true;
        }
        if (IsManual(vehicle, unit) && _fireEnabled && vehicle->CanFire() && isGunnerFire)
        {
            int weapon = vehicle->SelectedWeapon();
            weapon = ValidateWeapon(vehicle, weapon);
            if (weapon >= 0)
            {
                const WeaponModeType* mode = vehicle->GetWeaponMode(weapon);
                const Magazine* mag = vehicle->GetMagazineSlot(weapon)._magazine;
                if (mode && mode->_autoFire && mag->_ammo > 0)
                {
                    fire = input.GetMouseL() && !GWorld->HasMap();
                }
                if (fire)
                {
                    vehicle->FireWeapon(weapon, _lockTarget.IdExact());
                    if (transport)
                    {
                        const WeaponModeType* mode = transport->GetWeaponMode(weapon);
                        if (!mode || !mode->_autoFire)
                        {
                            transport->SetFirePrepare(true);
                        }
                    }
                }
            }
        }
    }

    if (IsManual(vehicle, unit) && isGunnerFire && input.GetActionToDo(UALockTarget))
    {
        int weapon = vehicle->SelectedWeapon();
        weapon = ValidateWeapon(vehicle, weapon);
        if (weapon >= 0 && weapon < vehicle->NMagazineSlots())
        {
            if (_target.IdExact() && _target.IdExact() != vehicle && !_target->vanished)
            {
                const MagazineSlot& slot = vehicle->GetMagazineSlot(weapon);

                bool canLock = slot._muzzle->_canBeLocked == 2 ||
                               slot._muzzle->_canBeLocked == 1 && USER_CONFIG.IsEnabled(DTAutoGuideAT);

                if (canLock && vehicle->CanLock(_target.IdExact()))
                {
                    _lockTarget = _target;
                    _timeSendTarget = UITIME_MAX;
                }
            }
            else
            {
                _lockTarget = nullptr;
                _timeSendTarget = UITIME_MAX;
            }
        }
    }

    // follow selected target
    if (_lockTarget)
    {
        bool lockFound = false; //,targetFound=false;
        Target* oldTarget = _lockTarget;
        int weapon = vehicle->SelectedWeapon();
        weapon = ValidateWeapon(vehicle, weapon);
        if (weapon >= 0 && weapon < vehicle->NMagazineSlots())
        {
            const MagazineSlot& slot = vehicle->GetMagazineSlot(weapon);
            const Magazine* magazine = slot._magazine;
            const AmmoType* ammo = nullptr;
            if (magazine)
            {
                const MagazineType* type = magazine->_type;
                if (type && slot._mode >= 0 && slot._mode < type->_modes.Size())
                {
                    ammo = type->_modes[slot._mode]->_ammo;
                }
            }

            bool canLock = slot._muzzle->_canBeLocked == 2 ||
                           slot._muzzle->_canBeLocked == 1 && USER_CONFIG.IsEnabled(DTAutoGuideAT);

            if (ammo && (canLock || !IsManual(vehicle, unit)))
            {
                for (i = 0; i < n; i++)
                {
                    Target* tar = visibleList[i];
                    PoseidonAssert(tar->type);
                    if (!tar->type)
                    {
                        continue;
                    }
                    if (!tar->idExact)
                    {
                        continue;
                    }
                    if (!tar->idExact->LockPossible(ammo))
                    {
                        continue;
                    }
                    if (tar->idExact == _lockTarget.IdExact() && tar->IsKnownBy(unit))
                    {
                        _lockTarget = tar;
                        lockFound = true;
                        //_wantLock = true;
                    }
                }
            }
        }
        if (!lockFound || _lockTarget.IdExact() == vehicle)
        {
            if (oldTarget && oldTarget->idExact != vehicle)
            {
                if (!ModeIsStrategy(_mode))
                {
                    Vector3 norm = (oldTarget->AimingPosition() - vehicle->Position());
                    SetCursorDirection(norm.Normalized());
                }
            }
            _lockTarget = nullptr;
        }
    }

    // in aiming mode adjust weapons accordingly to aiming
    if (IsManual(vehicle, unit))
    {
        const float threshold2 = Square(0.0002);

        if (isGunner)
        {
            int weapon = vehicle->SelectedWeapon();
            weapon = ValidateWeapon(vehicle, weapon);

            Vector3 weaponDir = GetCursorDirection();
            if (weaponDir.Distance2(vehicle->GetWeaponDirectionWanted(weapon)) > threshold2)
            {
                if (vehicle->IsLocal())
                {
                    vehicle->AimWeaponManDir(weapon, weaponDir);
                }
                else
                {
                    vehicle->AskForAimWeapon(weapon, weaponDir);
                    vehicle->AimWeaponManDir(weapon, weaponDir); // simulate directly
                                                                 // vehicle->WeaponTouched();
                }
            }
        } // if (isGunner)
        if (isObserver)
        {
            Vector3 weaponDir = GetCursorDirection();
            // apply weapon adjustment to cursor direction
            // this depends on weapon type and current fov
            // if we look through optics, get current fov
            // soldier aims always
            if (weaponDir.Distance2(vehicle->GetEyeDirectionWanted()) > threshold2)
            {
                if (vehicle->IsLocal())
                {
                    vehicle->AimObserver(weaponDir);
                }
                else
                {
                    GetNetworkManager().AskForAimObserver(vehicle, weaponDir);
                    vehicle->AimObserver(weaponDir); // simulate directly
                }
            }
        } // if (isObserver)
        if (isPilot)
        {
            // driver's vehicle is always local
            Vector3 weaponDir = GetCursorDirection();
            // apply weapon adjustment to cursor direction
            // this depends on weapon type and current fov
            // if we look through optics, get current fov
            // soldier aims always
            vehicle->AimDriver(weaponDir);
        }
    }
    //_wantLock=( _lockTarget.idExact );

    if (input.GetActionToDo(UANightVision))
    {
        Person* person = unit->GetPerson();
        if (person->IsNVEnabled())
        {
            person->SetNVWanted(!person->IsNVWanted());
        }
    }
}

void InGameUI::SwitchToStrategy(EntityAI* vehicle)
{
    if (!ModeIsStrategy(_mode))
    {
        _modeAuto = _mode = UIStrategy;
        _worldCursorTime = Glob.uiTime;

        _dragging = false;
        _mouseDown = false;
    }
}

void InGameUI::SwitchToFire(EntityAI* vehicle)
{
    if (ModeIsStrategy(_mode))
    {
        _modeAuto = _mode = UIFire;
        // set cursor to current weapon direction
        int weapon = vehicle->SelectedWeapon();
        weapon = ValidateWeapon(vehicle, weapon);

        if (vehicle)
        {
            AIUnit* unit = GWorld->FocusOn();
            bool isObserver = unit && unit == vehicle->ObserverUnit();
            bool isGunner = unit && unit == vehicle->GunnerUnit();

            if (isObserver)
            {
                SetCursorDirection(vehicle->GetEyeDirection());
            }
            else if (isGunner && weapon >= 0)
            {
                SetCursorDirection(vehicle->GetWeaponDirection(weapon));
            }
            else
            {
                SetCursorDirection(vehicle->Direction());
            }
            _worldCursorTime = Glob.uiTime;
        }

        _dragging = false;
        _mouseDown = false;
    }
}

void InGameUI::SelectWeapon(AIUnit* unit, int weapon)
{
    EntityAI* vehicle = unit->GetVehicle();
    vehicle->SelectWeaponCommander(unit, weapon);
}

int CmpStringI(const RString* str1, const RString* str2)
{
    return stricmp(*str1, *str2);
}

// RscMainMenu can be missing the movement command sub-menu when a mission's
// description.ext overrides/breaks the in-game command menu config — FindMenu then
// returns null. PoseidonAssert does not halt a release build, so guard the deref and
// skip the movement-command wiring instead of crashing on a null menu. Extracted as a
// static helper so the null path is unit-testable without a full InGameUI.
void InGameUI::WireMovementCommandMenu(Menu* menuMain)
{
    Menu* menuDist = menuMain->FindMenu(CMD_MOVE_FIRST, true);
    PoseidonAssert(menuDist);
    Menu* menuDir = menuDist ? menuDist->_parent : nullptr;
    PoseidonAssert(menuDir);
    if (!menuDir)
    {
        LOG_WARN(UI, "InGameUI::InitMenu: RscMainMenu has no movement command menu — skipping move-menu wiring");
        return;
    }
    int offset = CMD_MOVE_FIRST;
    for (int i = 0; i < N_MOVE_DIR; i++)
    {
        Menu* menuDistInner = menuDir->_items[i]->_submenu;
        for (int j = 0; j < N_MOVE_DIST; j++)
        {
            MenuItem* item = menuDistInner->_items[j];
            item->_cmd = offset++;
            menuDistInner->NotifySubmenuCommandAdded(item->_cmd);
        }
    }
}

void InGameUI::InitMenu()
{
    _menuMain = new Menu();
    _menuMain->Load(&(Res >> "RscMainMenu"));
    _menuType = MTNone;
    _menuCurrent = _menuMain;

    WireMovementCommandMenu(_menuMain);

    AUTO_STATIC_ARRAY(RString, sounds, 32);
    _finddata_t info;
    intptr_t h = _findfirst(Poseidon::GetUserDirectory() + RString("Sound/*.*"), &info);
    if (h != -1)
    {
        do
        {
            if ((info.attrib & _A_SUBDIR) == 0)
            {
                // ignore sounds larger than 40 KB
                if (info.size <= static_cast<unsigned>(MaxCustomSoundSize)) // info.size is unsigned (_fsize_t)
                {
                    sounds.Add(info.name);
                }
            }
        } while (0 == _findnext(h, &info));
        _findclose(h);
    }
    int n = sounds.Size();
    if (n > 0)
    {
        QSort(sounds.Data(), n, CmpStringI);
        saturateMin(n, 10);
        MenuItem* item = _menuMain->Find(CMD_RADIO_CUSTOM, false);
        PoseidonAssert(item);
        Menu* menuRadio = item->_submenu;
        PoseidonAssert(menuRadio);
        _customRadio.Realloc(n);
        _customRadio.Resize(n);
        for (int i = 0; i < n; i++)
        {
            _customRadio[i] = sounds[i];
            char buffer[256];
            strncpy(buffer, sounds[i], 256);
            buffer[255] = 0;
            char* ext = strrchr(buffer, '.');
            if (ext)
            {
                *ext = 0;
            }
            int key = SDL_SCANCODE_1 + i;
            int cmd = CMD_RADIO_CUSTOM_1 + i;
            char ch[2];
            ch[0] = i == 9 ? '0' : '1' + i;
            ch[1] = 0;
            menuRadio->_items.Insert(i, new MenuItem(buffer, key, ch, (Menu*)nullptr, cmd));
            menuRadio->NotifySubmenuCommandAdded(cmd);
        }
    }
    else
    {
        _menuMain->EnableCommand(CMD_RADIO_CUSTOM, false);
    }
}

void InGameUI::ToggleSelection(AIGroup* grp, int id)
{
    // check if there is unit id in grp
    AIUnit* u = grp->UnitWithID(id);

    if (!u || u == grp->Leader())
    {
        // make menu visible
        _menuType = MTMain;
        ShowMenu();
    }

    if (GetSelectedUnit(id - 1))
    {
        SetSelectedUnit(id - 1, nullptr);
    }
    else
    {
        SetSelectedUnit(id - 1, u);
    }
}

void InGameUI::SetSemaphore(AIUnit* unit, AI::Semaphore status)
{
    if (!unit)
    {
        return;
    }
    AIGroup* group = unit->GetGroup();
    if (!group)
    {
        return;
    }
    if (unit->IsGroupLeader())
    {
        group->SendSemaphore(status, ListSelectedUnits());
    }
}

void InGameUI::SetBehaviour(CombatMode mode)
{
    OLink<AIUnit>* GSelectedUnits = GetGSelectedUnits();
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* u = GSelectedUnits[i];
        if (u)
        {
            u->SetCombatModeMajor(mode);
        }
    }
}

void InGameUI::SetUnitPosition(AIUnit* unit, UnitPosition status)
{
    if (!unit)
    {
        return;
    }
    AIGroup* group = unit->GetGroup();
    if (!group)
    {
        return;
    }
    if (unit->IsGroupLeader())
    {
        group->SendState(new RadioMessageUnitPos(group, ListSelectedUnits(), status));
    }
    ClearSelectedUnits();
}

void InGameUI::SetFormationPos(AIUnit* unit, AI::FormationPos status)
{
    if (!unit)
    {
        return;
    }
    AIGroup* group = unit->GetGroup();
    if (!group)
    {
        return;
    }
    if (unit->IsGroupLeader())
    {
        group->SendState(new RadioMessageFormationPos(group, ListSelectedUnits(), status));
    }
    ClearSelectedUnits();
}

void InGameUI::ShowHint(RString hint)
{
    _hint->SetHint(hint);
    _hintTime = Glob.uiTime;
    if (hint.GetLength() > 0 && _hintSound.name.GetLength() > 0)
    {
        IWave* wave = GSoundScene->OpenAndPlayOnce2D(_hintSound.name, _hintSound.vol, _hintSound.freq, false);
        if (wave)
        {
            wave->SetKind(WaveMusic); // UI sounds considered music???
            GSoundScene->AddSound(wave);
        }
    }
}

} // namespace Poseidon
