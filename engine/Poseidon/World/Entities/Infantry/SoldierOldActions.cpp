#include <Poseidon/World/Entities/Infantry/SoldierOldCommon.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <Poseidon/Input/TouchInput.hpp>
#include <stdio.h>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/BankArray.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
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

using namespace Poseidon;
void UpdateWeaponsInBriefing();

bool CheckSupply(EntityAI* vehicle, EntityAI* parent, SupportCheckF check, float limit, bool now);

namespace Poseidon
{
void Soldier::FakePilot(float deltaT)
{
    _turnWanted = 0;
    _walkSpeedWanted = 0;
}

void Soldier::SuspendedPilot(float deltaT, SimulationImportance prec)
{
    _turnWanted = 0;
    _walkSpeedWanted = 0;

    AdvanceExternalQueue();

    if (_externalMove.id != MoveIdNone)
    {
        if (SetMoveQueue(_externalMove, prec <= SimulateVisibleFar))
        {
        }
    }
    else
    {
        // stand still
        ActionMap* map = Type()->GetActionMap(_primaryMove.id);
        MoveId moveWanted = map ? map->GetAction(ManActStop) : GetDefaultMove();
        SetMoveQueue(MotionPathItem(moveWanted), prec <= SimulateVisibleFar);
    }
}

CursorMode Man::GetCursorRelMode(CameraType camType) const
{
    // relative cursor only in virtual cockpit/view, and only when mouse is not active
    if (camType == CamInternal || camType == CamExternal)
    {
        auto& input = InputSubsystem::Instance();
        if (IsVirtual(camType) && !input.IsLookAroundEnabled() && !input.IsMouseTurnActive())
        {
            return CKeyboard;
        }
        else
        {
            return CMouseAbs;
        }
    }
    else if (camType == CamGunner)
    {
        return CMouseAbs;
    }
    else
    {
        return base::GetCursorRelMode(camType);
    }
}

bool Man::IsWeaponInMove() const
{
    int actPos = GetActUpDegree();
    return (actPos != ManPosNoWeapon && actPos != ManPosLyingNoWeapon);
}

bool Man::IsBinocularInMove() const
{
    int actPos = GetActUpDegree();
    return (actPos == ManPosBinoc || actPos == ManPosBinocLying || actPos == ManPosBinocStand);
}

void Man::UnselectLauncher()
{
    if (!LauncherSelected())
    {
        return;
    }
    int weapon = FirstWeapon();
    if (weapon == SelectedWeapon())
    {
        weapon = -1;
    }
    SelectWeapon(weapon);
}

void Soldier::KeyboardPilot(float deltaT, SimulationImportance prec)
{
    AIUnit* unit = Brain();
    auto& input = InputSubsystem::Instance();
    float turnWanted = 0;
    if (!_isDead)
    {
        bool internalCamera = IsGunner(GWorld->GetCameraType()) && GWorld->CameraOn() == this;
        if (internalCamera && input.IsMouseTurnActive() && !input.IsLookAroundEnabled())
        {
            _turnWanted = 0;
            if (_gunYRotWanted < Type()->_minGunTurn)
            {
                // force turn right
                _turnToDo = Type()->_minGunTurn - _gunYRotWanted;
                _gunYRotWanted = Type()->_minGunTurn;
            }
            else if (_gunYRotWanted > Type()->_maxGunTurn)
            {
                // force turn left
                _turnToDo = Type()->_maxGunTurn - _gunYRotWanted;
                _gunYRotWanted = Type()->_maxGunTurn;
            }
        }
        else
        {
            turnWanted = (input.GetAction(UAMoveRight) - input.GetAction(UAMoveLeft)) * 1.5f;

            if (input.IsLookAroundEnabled())
            {
                saturate(_gunYRotWanted, Type()->_minGunTurnAI, Type()->_maxGunTurnAI);
            }
            else
            {
                saturate(_gunYRotWanted, Type()->_minGunTurn, Type()->_maxGunTurn);
            }
        }
    }

    AdvanceExternalQueue();
    if (_externalMove.id != MoveIdNone)
    {
        _turnWanted = 0;
        if (SetMoveQueue(_externalMove, prec <= SimulateVisibleFar))
        {
        }
        return;
    }

    if (!unit)
    {
        return;
    }

    if (input.GetActionToDo(UASlow))
    {
        _walkToggle = !_walkToggle;
    }

    float fbWanted = ((input.GetMoveFastForward() > 0) * 2.0f + (input.GetMoveForward() > 0) * 1.0f +
                      (input.GetMoveSlowForward() > 0) * 0.5f + (input.GetMoveBack() > 0) * -1.0f);

    bool turbo = input.GetAction(UATurbo) > 0.5f;

    float rlWanted = input.GetAction(UATurnRight) - input.GetAction(UATurnLeft);

    {
        float delta = turnWanted - _turnWanted;
        saturate(delta, -3 * deltaT, +3 * deltaT);
        _turnWanted += delta;

        if (input.IsLookAroundEnabled())
        {
            saturate(_gunYRotWanted, Type()->_minGunTurnAI, Type()->_maxGunTurnAI);
        }
        else
        {
            saturate(_gunYRotWanted, Type()->_minGunTurn, Type()->_maxGunTurn);
        }

        saturate(_turnWanted, -3, +3);
    }

    ManAction selAction = ManActStop;
    bool forceAction = false;

    if (input.GetActionToDo(UAReloadMagazine))
    {
        if (DisableWeaponsLong())
        {
            if (_currentWeapon < 0)
            {
                SelectWeapon(FirstWeapon());
            }
            if (_currentWeapon >= 0)
            {
                FireAttemptWhenNotPossible();
            }
        }
        else if (_currentWeapon >= 0 && EnableWeaponManipulation())
        {
            const MagazineSlot& slot = GetMagazineSlot(_currentWeapon);
            const Magazine* magazine = slot._magazine;
            const MagazineType* mType = magazine ? magazine->_type : nullptr;
            int best = FindMagazineByType(slot._muzzle, mType);
            if (best >= 0)
            {
                const Magazine* magazine = GetMagazine(best);
                RString muzzleID = slot._weapon->GetName() + RString("|") + slot._muzzle->GetName();
                Ref<ActionContextDefault> context = new ActionContextDefault;
                context->function = MFReload;
                context->param = magazine->_creator;
                context->param2 = magazine->_id;
                context->param3 = muzzleID;
                if (PlayAction(magazine->_type->_reloadAction, context))
                {
                    PlayReloadMagazineSound(_currentWeapon, slot._muzzle);
                }
                else
                {
                    ReloadMagazineTimed(_currentWeapon, best, false);
                }
            }
        }
    }

    if (input.GetActionToDo(UABinocular))
    {
        if (BinocularSelected())
        {
            int slot = IsHandGunSelected() ? MaskSlotHandGun : MaskSlotPrimary;
            bool found = false;
            for (int i = 0; i < _magazineSlots.Size(); i++)
            {
                const WeaponType* type = _magazineSlots[i]._weapon;
                if (!type)
                {
                    continue;
                }
                if (type->_weaponType & slot)
                {
                    SelectWeapon(i);
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                SelectWeapon(-1);
            }
        }
        else
        {
            Ref<WeaponType> binocular = WeaponTypes.New("Binocular");
            PoseidonAssert(binocular);
            for (int i = 0; i < _magazineSlots.Size(); i++)
            {
                const WeaponType* type = _magazineSlots[i]._weapon;
                if (type == binocular)
                {
                    SelectWeapon(i);
                    break;
                }
            }
        }
    }

    if (input.GetActionToDo(UAHandgun))
    {
        if (IsHandGunSelected() && !IsHandGunInMove())
        {
            PlayAction(ManActHandGunOn);
        }
        else
        {
            int index = -1;
            if (IsHandGunSelected())
            {
                index = FindWeaponType(MaskSlotPrimary);
            }
            else
            {
                index = FindWeaponType(MaskSlotHandGun);
                if (GetActUpDegree() == ManPosStand)
                {
                    PlayAction(ManActHandGunOn);
                }
            }

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
    }

    bool forceStand = false;
    float slope = LandSlope(forceStand);

    ActionMap* map = Type()->GetActionMap(_primaryMove.id);

    int primaryIndex = FindWeaponType(MaskSlotPrimary);
    int handGunIndex = FindWeaponType(MaskSlotHandGun);

    bool isWeapon = primaryIndex >= 0 || handGunIndex >= 0;
    int actPos = GetActUpDegree();

    bool wantSlow = false;
    if (GWorld->FocusOn() == unit)
    {
        wantSlow = _walkToggle || GWorld->GetCameraTypeWanted() == CamGunner;
    }

    if (input.GetMoveUp() > 0.1f)
    {
        selAction = ManActUp;
        forceAction = true;
        UnselectLauncher();
        if (IsHandGunSelected() && !IsHandGunInMove())
        {
            if (actPos == ManPosStand)
            {
                selAction = ManActHandGunOn;
            }
            else
            {
                SelectPrimaryWeapon();
            }
        }
    }
    else if (input.GetMoveDown() > 0.1f)
    {
        selAction = ManActDown;
        forceAction = true;
        UnselectLauncher();
        if (IsHandGunSelected() && !IsHandGunInMove())
        {
            if (actPos == ManPosStand)
            {
                selAction = ManActHandGunOn;
            }
            else
            {
                SelectPrimaryWeapon();
            }
        }
    }
    else if (BinocularSelected())
    {
        selAction = ManActBinocOn;
    }
    else if (IsBinocularInMove())
    {
        selAction = ManActBinocOff;
    }
    else if (isWeapon != IsWeaponInMove() && !LauncherSelected())
    {
        if (IsWeaponInMove())
        {
            if (actPos == ManPosLying)
            {
                selAction = ManActCivilLying;
            }
            else
            {
                selAction = ManActCivil;
            }
        }
        else if (IsHandGunSelected())
        {
            selAction = ManActHandGunOn;
        }
        else
        {
            if (actPos == ManPosLyingNoWeapon)
            {
                selAction = ManActLying;
            }
            else
            {
                selAction = ManActCombat;
            }
        }
    }
    else if (LauncherSelected() && GetActUpDegree() != ManPosWeapon)
    {
        selAction = ManActWeaponOn;
        forceAction = true;
    }
    else if (GetActUpDegree() == ManPosWeapon && !LauncherSelected())
    {
        selAction = IsHandGunSelected() ? ManActHandGunOn : ManActWeaponOff;
        forceAction = true;
    }
    else if (!IsAbleToStand() && !IsDown() && !LauncherSelected())
    {
        if (IsHandGunSelected())
        {
            selAction = ManActDown;
        }
        else
        {
            selAction = ManActLying;
        }
        forceAction = true;
        Scream(nullptr);
    }
    else if (!forceStand && !CanStand(slope) && !IsDown() && !LauncherSelected())
    {
        if (IsHandGunSelected())
        {
            selAction = ManActDown;
        }
        else
        {
            selAction = ManActLying;
        }
        forceAction = true;
    }
    else if (IsHandGunSelected() && IsPrimaryWeaponInMove())
    {
        selAction = ManActHandGunOn;
    }
    else if (!IsHandGunSelected() && IsHandGunInMove())
    {
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
    else if (!forceAction)
    {
        int fastFlag = turbo;
        int rlFlag = 0, fbFlag = 0;
        if (fabs(rlWanted) > +1.5f)
        {
            fastFlag = 1;
        }
        if (fabs(fbWanted) > +1.5f)
        {
            fastFlag = 1;
        }

        if (rlWanted > +0.5f)
        {
            rlFlag = +1;
        }
        else if (rlWanted < -0.5f)
        {
            rlFlag = -1;
        }
        if (fbWanted > +0.5f)
        {
            fbFlag = +1;
        }
        else if (fbWanted < -0.5f)
        {
            fbFlag = -1;
        }
        if (IsDown())
        {
            if (!_canMoveFast)
            {
                fastFlag = 0;
            }
            fastFlag += 1;
        }
        else
        {
            if (!_canMoveFast || !CanSprint(slope))
            {
                fastFlag = 0;
            }
            fastFlag += 1;
            if (wantSlow && fastFlag == 1 || !CanRun(slope))
            {
                fastFlag = 0;
            }
        }

        // [fast][fb+1][rl+1]: walk(0)/slow(1)/fast(2) × back/stop/front × left/stop/right
#ifdef __GNUC__
        static const int selectAction[3][3][3] =
#else
        static const ManAction selectAction[3][3][3] =
#endif
            {
                {
                    {ManActWalkLB, ManActWalkB, ManActWalkRB},
                    {ManActWalkL, ManActStop, ManActWalkR},
                    {ManActWalkLF, ManActWalkF, ManActWalkRF},
                },
                {
                    {ManActSlowLB, ManActSlowB, ManActSlowRB},
                    {ManActSlowL, ManActStop, ManActSlowR},
                    {ManActSlowLF, ManActSlowF, ManActSlowRF},
                },
                {
                    {ManActFastLB, ManActFastB, ManActFastRB},
                    {ManActFastL, ManActStop, ManActFastR},
                    {ManActFastLF, ManActFastF, ManActFastRF},
                },
            };
        selAction = (ManAction)selectAction[fastFlag][fbFlag + 1][rlFlag + 1];
    }

    if (selAction == ManActStop)
    {
        if (fabs(_turnWanted) > 0.5f)
        {
            selAction = turnWanted < 0 ? ManActTurnL : ManActTurnR;
        }
    }

    MoveId moveWanted = map ? map->GetAction(selAction) : GetDefaultMove();

    if (_forceMove.id != MoveIdNone)
    {
        SetMoveQueue(_forceMove, prec <= SimulateVisibleFar);
    }
    else
    {
        MotionPathItem item = MotionPathItem(moveWanted);
        if (SetMoveQueue(item, prec <= SimulateVisibleFar))
        {
            if (forceAction)
            {
                _forceMove = item;
            }
        }
    }

    AutoGuide(deltaT);

    AIGroup* g = GetGroup();
    if (g && g->Leader() == unit)
    {
        int actDegree = GetActUpDegree();
        saturate(actDegree, ManPosLying, ManPosStand);
        if (actDegree <= _upDegreeStable)
        {
            _upDegreeStable = actDegree;
            _upDegreeChangeTime = Glob.time;
        }
        else if (_upDegreeChangeTime < Glob.time - 10)
        {
            // stable upDegree higher for >10s — slowly recover toward safer posture
            _upDegreeStable++;
            _upDegreeChangeTime = Glob.time;
        }

        CombatMode autoCM = g->GetCombatModeMinor();
        if (_upDegreeStable < ManPosNormalMin)
        {
            autoCM = CMCombat;
        }
        else if (_upDegreeStable <= ManPosLying)
        {
            autoCM = CMCombat;
        }
        else if (_upDegreeStable <= ManPosCombat)
        {
            autoCM = CMAware;
        }
        else if (_upDegreeStable < ManPosNormalMax)
        {
            autoCM = CMSafe;
        }
        g->SetCombatModeMinor(autoCM);
    }

    CheckAway();
}

int Man::GetAutoUpDegree() const
{
    AIUnit* unit = Brain();
    if (!unit)
    {
        return ManPosStand;
    }
    return GetUpDegree(unit->GetCombatMode(), IsHandGunSelected());
}

MoveId Man::GetDefaultMove() const
{
    int deg = GetAutoUpDegree();
    return Type()->GetDefaultMove(deg);
}

MoveId Man::GetDefaultMove(ManAction action) const
{
    if (_isDead)
    {
        action = ManActDie;
    }
    int deg = GetAutoUpDegree();
    return Type()->GetMove(deg, action);
}

MoveId Man::GetMove(ManAction action) const
{
    const ActionMap* map = Type()->GetActionMap(_primaryMove.id);
    if (map)
    {
        return map->GetAction(action);
    }

    int deg = GetActUpDegree();
    return Type()->GetMove(deg, action);
}

MoveId Man::GetVehMove(ManVehAction action) const
{
    const ActionVehMap& map = Type()->GetActionVehMap();
    if (action < 0)
    {
        Fail("Vehicle action ManVehActNone used");
        return MoveIdNone;
    }
    if (action >= map.GetMaxAction())
    {
        LOG_ERROR(Physics, "Invalid vehicle action {} (should be 0 to {})", (int)action, map.GetMaxAction());
        return MoveIdNone;
    }
    return Type()->GetActionVehMap().GetAction(action);
}

Texture* Man::GetCursorTexture(Person* person)
{
    if (WeaponsDisabled())
    {
        return nullptr;
    }
    return base::GetCursorTexture(person);
}

Texture* Man::GetCursorAimTexture(Person* person)
{
    if (_laserTargetOn)
    {
        return GPreloadedTextures.New(CursorLocked);
    }
    if (WeaponsDisabled())
    {
        return nullptr;
    }
    return base::GetCursorAimTexture(person);
}

Texture* Man::GetFlagTexture()
{
    if (_flagCarrier)
    {
        return _flagCarrier->GetFlagTextureInternal();
    }
    return nullptr;
}

void Man::SetFlagOwner(Person* veh)
{
    if (_flagCarrier)
    {
        _flagCarrier->SetFlagOwner(veh);
    }
}

static RString FormatWeaponSystem(const char* format, EntityAI* veh, int weaponIndex)
{
    if (veh)
    {
        if (weaponIndex >= 0 && weaponIndex < veh->NWeaponSystems())
        {
            const WeaponType* weapon = veh->GetWeaponSystem(weaponIndex);
            RStringB displayName = weapon->GetDisplayName();
            char buffer[512];
            snprintf(buffer, sizeof(buffer), format, (const char*)displayName);
            return buffer;
        }
        else
        {
            RptF("Weapon action - bad weapon %d", weaponIndex);
            return "Weapon Error";
        }
    }
    else
    {
        RptF("Weapon action - bad target");
        return "Weapon Error";
    }
}

RString Man::GetActionName(const UIAction& action)
{
    switch (action.type)
    {
        case ATTouchOff:
        {
            int n = 0;
            for (int i = 0; i < _pipeBombs.Size();)
            {
                if (_pipeBombs[i])
                {
                    if (_pipeBombs[i]->Position().Distance2(Position()) <= Square(300))
                    {
                        n++;
                    }
                    i++;
                }
                else
                {
                    _pipeBombs.Delete(i);
                }
            }
            char buffer[256];
            snprintf(buffer, sizeof(buffer), LocalizeString(IDS_ACTION_TOUCH_OFF), n);
            return buffer;
        }
        case ATSetTimer:
        {
            float minDist2 = Square(3);
            PipeBomb* nearest = nullptr;
            for (int i = 0; i < _pipeBombs.Size();)
            {
                Vehicle* veh = _pipeBombs[i];
                PipeBomb* bomb = dyn_cast<PipeBomb>(veh);
                if (!bomb)
                {
                    _pipeBombs.Delete(i);
                    continue;
                }
                PoseidonAssert(bomb->GetOwner() == this);
                float dist2 = Position().Distance2(bomb->Position());
                if (dist2 < minDist2)
                {
                    nearest = bomb;
                    minDist2 = dist2;
                }
                i++;
            }
            if (nearest)
            {
                float time = nearest->GetTimer();
                char buffer[256];
                if (time > 1e6)
                {
                    snprintf(buffer, sizeof(buffer), LocalizeString(IDS_ACTION_START_TIMER), 30.0f);
                }
                else
                {
                    snprintf(buffer, sizeof(buffer), LocalizeString(IDS_ACTION_SET_TIMER), 30.0f, time);
                }
                return buffer;
            }
            else
            {
                return "Error - bomb not found";
            }
        }
        case ATDeactivate:
            return LocalizeString(IDS_ACTION_DEACTIVATE);
        case ATWeaponOnBack:
            return FormatWeaponSystem(LocalizeString(IDS_ACTION_WEAPONONBACK), this, action.param);
        case ATWeaponInHand:
            return FormatWeaponSystem(LocalizeString(IDS_ACTION_WEAPONINHAND), this, action.param);

        case ATStrokeFist:
            return LocalizeString(IDS_ACTION_STROKEFIST);
        case ATStrokeGun:
            return LocalizeString(IDS_ACTION_STROKEGUN);
        case ATHandGunOn:
        {
            int index = FindWeaponType(MaskSlotHandGun);
            if (GetActUpDegree() == ManPosStand)
            {
                return FormatWeaponSystem(LocalizeString(IDS_ACTION_WEAPONINHAND), this, index);
            }
            else
            {
                return FormatWeaponSystem(LocalizeString(IDS_ACTION_WEAPON), this, index);
            }
        }
            // return LocalizeString(IDS_ACTION_HANDGUNON);
        case ATHandGunOff:
        {
            int index = FindWeaponType(MaskSlotPrimary);
            if (GetActUpDegree() == ManPosStand)
            {
                return FormatWeaponSystem(LocalizeString(IDS_ACTION_WEAPONINHAND), this, index);
            }
            else
            {
                return FormatWeaponSystem(LocalizeString(IDS_ACTION_WEAPON), this, index);
            }
        }
        case ATDeactivateMine:
            return LocalizeString(IDS_ACTION_DEACTIVATE_MINE);
        case ATTakeMine:
            return LocalizeString(IDS_ACTION_TAKE_MINE);
        default:
            return base::GetActionName(action);
    }
}

void Man::ProcessUIAction(const UIAction& action)
{
    AIUnit* unit = Brain();
    action.Process(unit);
}

void Man::PerformAction(const UIAction& action, AIUnit* unit)
{
    if (!unit || unit->GetLifeState() != AIUnit::LSAlive)
    {
        return;
    }
    switch (action.type)
    {
        case ATHideBody:
        {
            EntityAI* tgt = action.target;
            if (tgt)
            {
                Man* man = dyn_cast<Man>(tgt);
                if (man)
                {
                    if (man->IsLocal())
                    {
                        man->_hideBodyWanted = 1;
                    }
                    else
                    {
                        GetNetworkManager().AskForHideBody(man);
                    }
                }
            }
            break;
        }
        case ATTouchOff:
        {
            _lastShotAtAssignedTarget = Glob.time;
            for (int i = 0; i < _pipeBombs.Size();)
            {
                Vehicle* veh = _pipeBombs[i];
                PipeBomb* bomb = dyn_cast<PipeBomb>(veh);
                if (!bomb)
                {
                    _pipeBombs.Delete(i);
                    continue;
                }
                PoseidonAssert(bomb->GetOwner() == this);
                if (Position().Distance2(bomb->Position()) > Square(300.0f))
                {
                    i++;
                    continue;
                }
                bomb->Explode();
                _pipeBombs.Delete(i);
            }
            break;
        }
        case ATSetTimer:
        {
            _lastShotAtAssignedTarget = Glob.time;
            float minDist2 = Square(3);
            PipeBomb* nearest = nullptr;
            for (int i = 0; i < _pipeBombs.Size();)
            {
                Vehicle* veh = _pipeBombs[i];
                PipeBomb* bomb = dyn_cast<PipeBomb>(veh);
                if (!bomb)
                {
                    _pipeBombs.Delete(i);
                    continue;
                }
                PoseidonAssert(bomb->GetOwner() == this);
                float dist2 = Position().Distance2(bomb->Position());
                if (dist2 < minDist2)
                {
                    nearest = bomb;
                    minDist2 = dist2;
                }
                i++;
            }
            if (nearest)
            {
                float time = nearest->GetTimer();
                if (time > 1e6)
                {
                    time = 0;
                }
                nearest->SetTimer(time + 30);
            }
            break;
        }
        case ATDeactivate:
        {
            float minDist2 = Square(3);
            PipeBomb* nearest = nullptr;
            int index = -1;
            for (int i = 0; i < _pipeBombs.Size();)
            {
                Vehicle* veh = _pipeBombs[i];
                PipeBomb* bomb = dyn_cast<PipeBomb>(veh);
                if (!bomb)
                {
                    _pipeBombs.Delete(i);
                    continue;
                }
                PoseidonAssert(bomb->GetOwner() == this);
                float dist2 = Position().Distance2(bomb->Position());
                if (dist2 < minDist2)
                {
                    nearest = bomb;
                    index = i;
                    minDist2 = dist2;
                }
                i++;
            }
            if (nearest)
            {
                nearest->SetDelete();
                _pipeBombs.Delete(index);
                AddMagazine("PipeBomb", false);
            }
        }
        break;
        case ATDeactivateMine:
        {
            float minDist2 = Square(3);
            Mine* nearest = nullptr;

            int xMin, xMax, zMin, zMax;
            ObjRadiusRectangle(xMin, xMax, zMin, zMax, Position(), Position(), 3);
            for (int x = xMin; x <= xMax; x++)
            {
                for (int z = zMin; z <= zMax; z++)
                {
                    const ObjectList& list = GLandscape->GetObjects(z, x);
                    int n = list.Size();
                    for (int i = 0; i < n; i++)
                    {
                        Object* obj = list[i];
                        if (!obj)
                        {
                            continue;
                        }

                        float dist2 = obj->Position().Distance2(Position());
                        if (dist2 > minDist2)
                        {
                            continue;
                        }

                        Mine* mine = dyn_cast<Mine>(obj);
                        if (!mine)
                        {
                            continue;
                        }
                        if (!mine->IsActive())
                        {
                            continue;
                        }

                        nearest = mine;
                        minDist2 = dist2;
                    }
                }
            }
            if (nearest)
            {
                if (nearest->IsLocal())
                {
                    nearest->SetActive(false);
                }
                else
                {
                    GetNetworkManager().AskForActivateMine(nearest, false);
                }
            }
        }
        break;
        case ATTakeMine:
        {
            float minDist2 = Square(3);
            Mine* nearest = nullptr;

            int xMin, xMax, zMin, zMax;
            ObjRadiusRectangle(xMin, xMax, zMin, zMax, Position(), Position(), 3);
            for (int x = xMin; x <= xMax; x++)
            {
                for (int z = zMin; z <= zMax; z++)
                {
                    const ObjectList& list = GLandscape->GetObjects(z, x);
                    int n = list.Size();
                    for (int i = 0; i < n; i++)
                    {
                        Object* obj = list[i];
                        if (!obj)
                        {
                            continue;
                        }

                        float dist2 = obj->Position().Distance2(Position());
                        if (dist2 > minDist2)
                        {
                            continue;
                        }

                        Mine* mine = dyn_cast<Mine>(obj);
                        if (!mine)
                        {
                            continue;
                        }

                        nearest = mine;
                        minDist2 = dist2;
                    }
                }
            }
            if (nearest)
            {
                const AmmoType* ammo = nearest->Type();
                if (ammo)
                {
                    RString name = ammo->_defaultMagazine;
                    if (name.GetLength() > 0)
                    {
                        Ref<MagazineType> magazineType = MagazineTypes.New(name);
                        Ref<Magazine> magazine = new Magazine(magazineType);
                        magazine->_ammo = magazineType->_maxAmmo;

                        AUTO_STATIC_ARRAY(Ref<const Magazine>, conflict, 16);
                        if (CheckMagazine(magazine, conflict) && conflict.Size() == 0)
                        {
                            if (nearest->IsLocal())
                            {
                                nearest->SetDelete();
                            }
                            else
                            {
                                GetNetworkManager().AskForDeleteVehicle(nearest);
                            }
                            AddMagazine(magazine, false, true);
                        }
                    }
                }
            }
        }
        break;
        case ATWeaponInHand:
            PoseidonAssert(action.target == this);
            if (IsHandGunSelected())
            {
                PlayAction(ManActHandGunOn);
            }
            else
            {
                PlayAction(ManActCombat);
            }
            break;
        case ATWeaponOnBack:
            PoseidonAssert(action.target == this);
            if (LauncherSelected())
            {
                UnselectLauncher();
            }
            PlayAction(ManActStand);
            break;
        case ATSitDown:
            PoseidonAssert(action.target == this);
            PlayAction(ManActSitDown);
            break;
        case ATSalute:
            PoseidonAssert(action.target == this);
            PlayAction(ManActSalute);
            break;
        case ATStrokeFist:
            PoseidonAssert(action.target == this);
            PlayAction(ManActStrokeFist);
            break;
        case ATStrokeGun:
            PoseidonAssert(action.target == this);
            PlayAction(ManActStrokeGun);
            break;
        case ATHandGunOn:
            if (!IsHandGunSelected())
            {
                SelectHandGunWeapon();
                if (GetActUpDegree() == ManPosStand)
                {
                    PlayAction(ManActHandGunOn);
                }
            }
            break;
        case ATHandGunOff:
            if (IsHandGunSelected())
            {
                SelectPrimaryWeapon();
                if (GetActUpDegree() == ManPosStand)
                {
                    PlayAction(ManActCombat);
                }
            }
            break;
        default:
            base::PerformAction(action, unit);
            break;
    }
}

static bool CheckEnemySoldier(AIUnit* unit, Vector3Par pos, Vector3Par dir)
{
    float hitDistance = 0.7f;
    float hitRadius = 0.8f;

    Vector3 center = pos + dir * hitDistance;
    int xMin, xMax, zMin, zMax;
    ObjRadiusRectangle(xMin, xMax, zMin, zMax, center, center, hitRadius);
    AIGroup* myGroup = unit->GetGroup();
    if (!myGroup)
    {
        return false;
    }
    AICenter* myCenter = myGroup->GetCenter();
    if (!myCenter)
    {
        return false;
    }

    for (int x = xMin; x <= xMax; x++)
    {
        for (int z = zMin; z <= zMax; z++)
        {
            const ObjectList& list = GLandscape->GetObjects(z, x);
            for (int i = 0; i < list.Size(); i++)
            {
                Object* obj = list[i];
                if (obj->Position().DistanceXZ2(center) > hitRadius)
                {
                    continue;
                }
                Man* man = dyn_cast<Man>(obj);
                if (!man)
                {
                    continue;
                }
                if (man->IsDown())
                {
                    continue;
                }
                if (!myCenter->IsEnemy(man->GetTargetSide()))
                {
                    continue;
                }
                return true;
            }
        }
    }
    return false;
}

void Man::GetActions(UIActions& actions, AIUnit* unit, bool now)
{
    if (_hideBody > 0)
    {
        return;
    }

    base::GetActions(actions, unit, now);

    if (!unit)
    {
        return;
    }
    bool free = unit->IsFreeSoldier();

    Person* veh = unit->GetPerson();

    if (GetAllocSupply() && GetAllocSupply() != veh)
    {
        return; // wait
    }

    if (_isDead && CheckSupply(veh, this, nullptr, 0, now))
    {
        if (free)
        {
            bool reloading = unit->GetPerson()->IsActionInProgress(MFReload);
            if (!reloading)
            {
                // corpse
                for (int i = 0; i < NWeaponSystems(); i++)
                {
                    const WeaponType* weapon = GetWeaponSystem(i);
                    if (weapon->_scope < 2)
                    {
                        continue;
                    }
                    AUTO_STATIC_ARRAY(Ref<const WeaponType>, conflict, 16);
                    if (!veh->CheckWeapon(weapon, conflict))
                    {
                        continue;
                    }
                    actions.Add(ATTakeWeapon, this, 0.52f, 0, true, false, 0, weapon->GetName());
                }

                int n = NMagazines();
                AUTO_STATIC_ARRAY(bool, checked, 16);
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
                    const Magazine* magazine = GetMagazine(i);
                    if (magazine->_type->_scope < 2)
                    {
                        continue;
                    }
                    if (magazine->_ammo == 0)
                    {
                        continue;
                    }
                    if (IsMagazineUsed(magazine))
                    {
                        continue;
                    }
                    if (!veh->IsMagazineUsable(magazine->_type))
                    {
                        continue;
                    }
                    for (int j = i + 1; j < n; j++)
                    {
                        if (GetMagazine(j)->_type == magazine->_type)
                        {
                            checked[j] = true;
                            if (GetMagazine(j)->_ammo > magazine->_ammo && !IsMagazineUsed(GetMagazine(j)))
                            {
                                magazine = GetMagazine(j);
                            }
                        }
                    }
                    AUTO_STATIC_ARRAY(Ref<const Magazine>, conflict, 16);
                    if (!veh->CheckMagazine(magazine, conflict))
                    {
                        continue;
                    }
                    actions.Add(ATTakeMagazine, this, 0.53f, 0, true, false, 0, magazine->_type->GetName());
                }

                if (_hideBodyWanted == 0)
                {
                    Man* vehMan = dyn_cast<Man>(veh);
                    if (vehMan && vehMan->Type()->_canHideBodies)
                    {
                        actions.Add(ATHideBody, this, 0.51f);
                    }
                }
            }
        }

        if (_flagCarrier && GetFlagTexture())
        {
            TargetSide side = _flagCarrier->GetFlagSide();
            AIGroup* grp = unit->GetGroup();
            AICenter* center = grp ? grp->GetCenter() : nullptr;
            if (center)
            {
                if (center->IsEnemy(side))
                {
                    actions.Add(ATTakeFlag, this, 0.99f, 0, true, true); //***
                }
                else if (center->IsFriendly(side))
                {
                    actions.Add(ATReturnFlag, this, 0.99f, 0, true, true);
                }
            }
        }
    }
    else
    {
        if (unit->GetPerson() == this)
        {
            if (free)
            {
                const ActionMap* map = Type()->GetActionMap(_primaryMove.id);
                if (QIsManual())
                {
                    int index = IsHandGunSelected() ? FindWeaponType(MaskSlotHandGun) : FindWeaponType(MaskSlotPrimary);
                    if (index < 0)
                    {
                        index = FindWeaponType(MaskSlotSecondary);
                    }
                    if (index >= 0)
                    {
                        if (GetActUpDegree() < ManPosStand)
                        {
                            if (EnableWeaponManipulation())
                            {
                                actions.Add(ATWeaponOnBack, this, 0.3f, index);
                            }
                        }
                        else if (GetActUpDegree() == ManPosStand)
                        {
                            actions.Add(ATWeaponInHand, this, 2.0f, index);
                        }
                    }

                    if (IsHandGunSelected())
                    {
                        int index = FindWeaponType(MaskSlotPrimary);
                        if (index >= 0)
                        {
                            actions.Add(ATHandGunOff, this, 0.4f, index);
                        }
                    }
                    else
                    {
                        int index = FindWeaponType(MaskSlotHandGun);
                        if (index >= 0)
                        {
                            actions.Add(ATHandGunOn, this, 0.4f, index);
                        }
                    }
                }

                if (map && map->GetAction(ManActSitDown) != MoveIdNone)
                {
                    actions.Add(ATSitDown, this, 0.1f, 0);
                }
                if (map && map->GetAction(ManActSalute) != MoveIdNone)
                {
                    actions.Add(ATSalute, this, 0.11f, 0);
                }
            }

            bool isNear = false;
            bool isVeryNear = false;
            for (int i = 0; i < _pipeBombs.Size();)
            {
                if (_pipeBombs[i])
                {
                    float dist2 = _pipeBombs[i]->Position().Distance2(Position());
                    if (!isNear)
                    {
                        isNear = dist2 <= Square(300);
                    }
                    if (!isVeryNear)
                    {
                        isVeryNear = dist2 < Square(3);
                    }
                    i++;
                }
                else
                {
                    _pipeBombs.Delete(i);
                }
            }

            if (isNear)
            {
                actions.Add(ATTouchOff, this, 0.515f, 0);
            }
            if (free && isVeryNear)
            {
                actions.Add(ATSetTimer, this, 0.516f, 0, true, false);
                actions.Add(ATDeactivate, this, 0.514f, 0, true, false);
            }

            // mines
            if (Type()->_canDeactivateMines)
            {
                int xMin, xMax, zMin, zMax;
                ObjRadiusRectangle(xMin, xMax, zMin, zMax, Position(), Position(), 3);
                for (int x = xMin; x <= xMax; x++)
                {
                    for (int z = zMin; z <= zMax; z++)
                    {
                        const ObjectList& list = GLandscape->GetObjects(z, x);
                        int n = list.Size();
                        for (int i = 0; i < n; i++)
                        {
                            Object* obj = list[i];
                            if (!obj)
                            {
                                continue;
                            }

                            float dist2 = obj->Position().Distance2(Position());
                            if (dist2 > Square(3))
                            {
                                continue;
                            }

                            Mine* mine = dyn_cast<Mine>(obj);
                            if (!mine)
                            {
                                continue;
                            }
                            if (mine->IsActive())
                            {
                                actions.Add(ATDeactivateMine, this, 0.513f, 0, true);
                            }

                            const AmmoType* ammo = mine->Type();
                            if (!ammo)
                            {
                                continue;
                            }
                            RString name = ammo->_defaultMagazine;
                            if (name.GetLength() == 0)
                            {
                                continue;
                            }

                            Ref<MagazineType> magazineType = MagazineTypes.New(name);
                            Ref<Magazine> magazine = new Magazine(magazineType);
                            magazine->_ammo = magazineType->_maxAmmo;

                            AUTO_STATIC_ARRAY(Ref<const Magazine>, conflict, 16);
                            if (CheckMagazine(magazine, conflict) && conflict.Size() == 0)
                            {
                                actions.Add(ATTakeMine, this, 0.512f, 0, true);
                            }
                        }
                    }
                }
            }
        }
    }
}

bool Man::IsNVEnabled() const
{
    if (QIsManual())
    {
        if (IsDammageDestroyed())
        {
            return false;
        }
        if (GWorld->GetCameraType() == CamGroup)
        {
            return false;
        }
        if (GWorld->HasMap())
        {
            return false;
        }
    }
    return _hasNVG;
}

bool Man::IsNVWanted() const
{
    return _nvg;
}

void Man::SetNVWanted(bool set)
{
    _nvg = set;
}

void CutScene(const char* name);

struct MagazineInWeapon
{
    Ref<MuzzleType> muzzle;
    Ref<Magazine> magazine;
};

bool Man::Supply(EntityAI* vehicle, UIActionType action, int param, int param2, RString param3)
{
    if (action == ATTakeWeapon)
    {
        Man* man = dyn_cast<Man>(vehicle);
        if (!man)
        {
            return false;
        }
        // find weapon
        Ref<WeaponType> weapon = WeaponTypes.New(param3);
        AUTO_STATIC_ARRAY(Ref<const WeaponType>, conflict, 16);
        if (FindWeapon(weapon) && man->CheckWeapon(weapon, conflict))
        {
            // 1. find magazines in weapons
            AUTO_STATIC_ARRAY(MagazineInWeapon, miwConflict, 16);
            for (int i = 0; i < conflict.Size(); i++)
            {
                const WeaponType* weapon = conflict[i];
                for (int j = 0; j < weapon->_muzzles.Size(); j++)
                {
                    MuzzleType* muzzle = weapon->_muzzles[j];
                    for (int k = 0; k < man->NMagazineSlots(); k++)
                    {
                        const MagazineSlot& slot = man->GetMagazineSlot(k);
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
            AUTO_STATIC_ARRAY(MagazineInWeapon, miwWeapon, 16);
            for (int j = 0; j < weapon->_muzzles.Size(); j++)
            {
                MuzzleType* muzzle = weapon->_muzzles[j];
                for (int k = 0; k < NMagazineSlots(); k++)
                {
                    const MagazineSlot& slot = GetMagazineSlot(k);
                    if (slot._muzzle == muzzle)
                    {
                        if (slot._magazine && slot._magazine->_ammo > 0)
                        {
                            int index = miwWeapon.Add();
                            miwWeapon[index].muzzle = slot._muzzle;
                            miwWeapon[index].magazine = slot._magazine;
                        }
                        break;
                    }
                }
            }
            for (int i = 0; i < conflict.Size(); i++)
            {
                const WeaponType* wt = conflict[i];
                man->RemoveWeapon(wt, false);
                AddWeapon(const_cast<WeaponType*>(wt), true, false, false);
            }
            RemoveWeapon(weapon, false);
            Verify(man->AddWeapon(weapon, false, false, false) >= 0);
            if (GWorld->FocusOn() && GWorld->FocusOn()->GetVehicle() == man)
            {
                GWorld->UI()->ResetVehicle(man);
            }
            for (int i = 0; i < miwConflict.Size(); i++)
            {
                Ref<Magazine> magazine = miwConflict[i].magazine;
                if (!magazine)
                {
                    continue;
                }
                man->RemoveMagazine(magazine);
                AddMagazine(magazine, true);
                AttachMagazine(miwConflict[i].muzzle, magazine);
            }
            for (int i = 0; i < man->NMagazines();)
            {
                Ref<Magazine> magazine = man->GetMagazine(i);
                if (!magazine || man->IsMagazineUsable(magazine->_type))
                {
                    i++;
                    continue;
                }
                man->RemoveMagazine(magazine);
                if (magazine->_ammo >= 0)
                {
                    AddMagazine(magazine, true);
                }
            }
            for (int i = 0; i < miwWeapon.Size(); i++)
            {
                Ref<Magazine> magazine = miwWeapon[i].magazine;
                if (!magazine)
                {
                    continue;
                }
                AUTO_STATIC_ARRAY(Ref<const Magazine>, conflict, 16);
                if (man->CheckMagazine(magazine, conflict))
                {
                    for (int j = 0; j < conflict.Size(); j++)
                    {
                        const Magazine* m = conflict[j];
                        man->RemoveMagazine(m);
                        if (m->_ammo >= 0)
                        {
                            AddMagazine(const_cast<Magazine*>(m), true);
                        }
                    }
                    RemoveMagazine(magazine);
                    if (magazine->_ammo >= 0)
                    {
                        Verify(man->AddMagazine(magazine) >= 0);
                        man->AttachMagazine(miwWeapon[i].muzzle, magazine);
                    }
                }
            }
            for (int i = 0; i < weapon->_muzzles.Size(); i++)
            {
                MuzzleType* muzzle = weapon->_muzzles[i];
                for (int j = 0; j < muzzle->_magazines.Size(); j++)
                {
                    MagazineType* type = muzzle->_magazines[j];
                    for (int k = 0; k < NMagazines();)
                    {
                        Ref<Magazine> magazine = GetMagazine(k);
                        if (!magazine || magazine->_type != type)
                        {
                            k++;
                            continue;
                        }
                        if (IsMagazineUsed(magazine))
                        {
                            k++;
                            continue;
                        }
                        AUTO_STATIC_ARRAY(Ref<const Magazine>, conflict, 16);
                        if (!man->CheckMagazine(magazine, conflict) || conflict.Size() > 0)
                        {
                            goto EnoughMagazines;
                        }
                        RemoveMagazine(magazine);
                        Verify(man->AddMagazine(magazine) >= 0);
                    }
                }
            }
        EnoughMagazines:;
        }

        man->OnWeaponChanged();
        OnWeaponChanged();
        if (man == GWorld->CameraOn())
        {
            CutScene("TakeWeapon");
        }
        if (!IsLocal())
        {
            GetNetworkManager().UpdateWeapons(this);
        }

        UpdateWeaponsInBriefing();

        return true;
    }
    else if (action == ATTakeMagazine)
    {
        Man* man = dyn_cast<Man>(vehicle);
        if (!man)
        {
            return false;
        }
        Ref<const Magazine> magazine = FindMagazine(param3);
        if (!magazine)
        {
            return false;
        }
        AUTO_STATIC_ARRAY(Ref<const Magazine>, conflict, 16);
        if (man->CheckMagazine(magazine, conflict))
        {
            for (int i = 0; i < conflict.Size(); i++)
            {
                const Magazine* m = conflict[i];
                man->RemoveMagazine(m);
                if (m->_ammo >= 0)
                {
                    AddMagazine(const_cast<Magazine*>(m), true);
                }
            }
            RemoveMagazine(magazine);
            Verify(man->AddMagazine(const_cast<Magazine*>(magazine.GetRef()), false, true) >= 0);
        }
        if (man == GWorld->CameraOn())
        {
            CutScene("TakeMagazine");
        }
        if (!IsLocal())
        {
            GetNetworkManager().UpdateWeapons(this);
        }

        UpdateWeaponsInBriefing();

        return true;
    }
    else if (action == ATRearm)
    {
        Man* man = dyn_cast<Man>(vehicle);
        if (!man)
        {
            return false;
        }
        AIUnit* unit = man->Brain();
        if (!unit)
        {
            return false;
        }
        const MuzzleType* muzzle1 = nullptr;
        const MuzzleType* muzzle2 = nullptr;
        int slots1 = 0, slots2 = 0, slots3 = 0;
        unit->CheckAmmo(muzzle1, muzzle2, slots1, slots2, slots3);
        for (int i = 0; i < NMagazines();)
        {
            Ref<const Magazine> magazine = GetMagazine(i);
            if (!magazine || magazine->_ammo == 0)
            {
                i++;
                continue;
            }
            if (IsMagazineUsed(magazine))
            {
                i++;
                continue;
            }
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
                        AddMagazine(const_cast<Magazine*>(m), true);
                    }
                    RemoveMagazine(magazine);
                    Verify(man->AddMagazine(const_cast<Magazine*>(magazine.GetRef()), false, true) >= 0);
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
        if (!IsLocal())
        {
            GetNetworkManager().UpdateWeapons(this);
        }

        UpdateWeaponsInBriefing();

        return false;
    }
    return base::Supply(vehicle, action, param, param2, param3);
}

float Man::Rigid() const
{
    return 0.6f;
}

bool Man::HasGeometry() const
{
    return !_isDead;
}

void LogFVector(Vector3Val val)
{
    LOG_DEBUG(Physics, "  {:.5f},{:.5f},{:.5f}", val.X(), val.Y(), val.Z());
}

void LogFMatrix(Matrix4Val val)
{
    LogFVector(val.DirectionAside());
    LogFVector(val.DirectionUp());
    LogFVector(val.Direction());
    LogFVector(val.Position());
}

static Matrix3 CreateZRotation(float sinr, float cosr)
{
    Matrix3 ret = M3Identity;

    ret(0, 0) = +cosr, ret(0, 1) = -sinr;
    ret(1, 0) = +sinr, ret(1, 1) = +cosr;
    return ret;
}

void Man::RecalcGunTransform()
{
    Vector3 gunAxis = GetWeaponCenter(0);
    if (QIsManual())
    {
        Matrix4 correctBank;
        correctBank.SetOrientation(CreateZRotation(_correctBankSin, _correctBankCos));
        correctBank.SetPosition(VZero);
        _gunTrans = (Matrix4(MTranslation, gunAxis) * Matrix4(MRotationY, _gunYRot) * Matrix4(MRotationX, -_gunXRot) *
                     correctBank * Matrix4(MTranslation, -gunAxis));
        _gunTransIdent = false;
    }
    else
    {
        if (fabs(_gunXRot) + fabs(_gunYRot) > 1e-6)
        {
            _gunTrans = (Matrix4(MTranslation, gunAxis) * Matrix4(MRotationY, _gunYRot) *
                         Matrix4(MRotationX, -_gunXRot) * Matrix4(MTranslation, -gunAxis));
            _gunTransIdent = false;
        }
        else
        {
            _gunTrans = MIdentity; // no aim - avoid matrix multiply
            _gunTransIdent = true;
        }
    }

    // head rotation axis is the world-up direction, not the gun pivot
    Matrix4 headOrient(MIdentity);
    Vector3 headAxis = GetHeadCenter();

    headOrient.SetPosition(headAxis);

    Matrix4 headOrientInv(MInverseScaled, headOrient);

    if (fabs(_headYRot) + fabs(_headXRot) > 1e-6)
    {
        _headTrans = (headOrient * Matrix4(MRotationY, _headYRot) * Matrix4(MRotationX, -_headXRot) * headOrientInv);
        _headTransIdent = false;
    }
    else
    {
        _headTrans = MIdentity;
        _headTransIdent = true;
    }

    if (_recoil)
    {
        _recoil->ApplyRecoil(_recoilTime, _gunTrans, _recoilFactor);
        _gunTransIdent = false;
    }
}

bool Man::VerifyStructure() const
{
    if (DirectionAside().SquareSize() < 0.01f)
    {
        Fail("DirectionAside zero");
        return false;
    }
    if (DirectionUp().SquareSize() < 0.01f)
    {
        Fail("DirectionUp zero");
        return false;
    }
    if (Direction().SquareSize() < 0.01f)
    {
        Fail("Direction zero");
        return false;
    }

    if (!DirectionAside().IsFinite())
    {
        Fail("DirectionAside inf");
        return false;
    }
    if (!DirectionUp().IsFinite())
    {
        Fail("DirectionUp inf");
        return false;
    }
    if (!Direction().IsFinite())
    {
        Fail("Direction inf");
        return false;
    }
    if (!Position().IsFinite())
    {
        Fail("Position inf");
        return false;
    }

    Matrix4Val it = GetInvTransform();

    if (!it.DirectionAside().IsFinite())
    {
        Fail("inv DirectionAside inf");
        return false;
    }
    if (!it.DirectionUp().IsFinite())
    {
        Fail("inv DirectionUp inf");
        return false;
    }
    if (!it.Direction().IsFinite())
    {
        Fail("inv Direction inf");
        return false;
    }
    if (!it.Position().IsFinite())
    {
        Fail("inv Position inf");
        return false;
    }

    return true;
}

bool Man::MoveHead(float deltaT)
{
    bool forceRecalcMatrix = false;
    float headXRotWanted = _headXRotWanted;
    float headYRotWanted = _headYRotWanted;

    if (_isDead)
    {
        headXRotWanted = headYRotWanted = 0;
    }

    float ability = floatMax(GetAbility(), 0.5f);
    float maxV = 8 * ability;

    AIUnit* unit = Brain();
    if (unit && !QIsManual())
    {
        if (unit->GetCombatMode() <= CMSafe)
        {
            saturateMin(maxV, 2);
        }
    }

    float headXRotChange = headXRotWanted - _headXRot;
    float headYRotChange = headYRotWanted - _headYRot;
    Limit(headXRotChange, -maxV * deltaT, +maxV * deltaT);
    Limit(headYRotChange, -maxV * deltaT, +maxV * deltaT);

    if (fabs(headXRotChange) > 1e-6)
    {
        forceRecalcMatrix = true;
    }
    else
    {
        headXRotChange = 0;
    }
    if (fabs(headYRotChange) > 1e-6)
    {
        forceRecalcMatrix = true;
    }
    else
    {
        headYRotChange = 0;
    }

    _headXRot += headXRotChange;
    _headYRot += headYRotChange;
    return forceRecalcMatrix;
}

void Man::MoveWeapons(float deltaT, bool forceRecalcMatrix)
{
    float f = GetLimitGunMovement();
    float ability = floatMax(GetAbility(), 0.5f);
    if (f < 0.01f)
    {
        _gunXSpeed = 0;
        _gunYSpeed = 0;

        if (fabs(_gunXRot) > 1e-5)
        {
            forceRecalcMatrix = true;
        }
        if (fabs(_gunYRot) > 1e-5)
        {
            forceRecalcMatrix = true;
        }

        _gunXRot = 0;
        _gunYRot = 0;
    }
    else
    {
        float gunXRotWanted = _gunXRotWanted;
        float gunYRotWanted = _gunYRotWanted;

        Limit(gunXRotWanted, Type()->_minGunElev * f, Type()->_maxGunElev * f);
        Limit(gunYRotWanted, Type()->_minGunTurnAI * f, Type()->_maxGunTurnAI * f);

        float weaponDexterity = 1;

        int weapon = SelectedWeapon();
        if (weapon >= 0 && weapon < NMagazineSlots())
        {
            const WeaponType* info = GetMagazineSlot(weapon)._weapon;
            if (info)
            {
                weaponDexterity = info->_dexterity;
            }
        }

        // limited acceleration model
        float delta;
        float speed;
        float deltaPos;

        const float maxV = 5 * ability * weaponDexterity;
        const float maxA = 20 * weaponDexterity;

        deltaPos = gunXRotWanted - _gunXRot;
        speed = deltaPos * 8 * weaponDexterity;
        if (fabs(speed) * deltaT > fabs(deltaPos))
        {
            speed = deltaPos / deltaT;
        }
        delta = speed - _gunXSpeed;
        Limit(delta, -maxA * deltaT, +maxA * deltaT);
        _gunXSpeed += delta;

        deltaPos = gunYRotWanted - _gunYRot;
        speed = deltaPos * 6 * weaponDexterity;
        if (fabs(speed) * deltaT > fabs(deltaPos))
        {
            speed = deltaPos / deltaT;
        }
        delta = speed - _gunYSpeed;
        Limit(delta, -maxA * deltaT, +maxA * deltaT);
        _gunYSpeed += delta;

        Limit(_gunXSpeed, -maxV, +maxV);
        Limit(_gunYSpeed, -maxV, +maxV);

        float gunXRotChange = _gunXSpeed * deltaT;
        float gunYRotChange = _gunYSpeed * deltaT;

        if (fabs(gunXRotChange) >= 1e-6)
        {
            forceRecalcMatrix = true;
        }
        else
        {
            gunXRotChange = 0;
        }
        if (fabs(gunYRotChange) >= 1e-6)
        {
            forceRecalcMatrix = true;
        }
        else
        {
            gunYRotChange = 0;
        }

        _gunXRot += gunXRotChange;
        _gunYRot += gunYRotChange;

        Limit(_gunXRot, Type()->_minGunElev * f, Type()->_maxGunElev * f);
        Limit(_gunYRot, Type()->_minGunTurnAI * f, Type()->_maxGunTurnAI * f);
    }

    bool headMoved = MoveHead(deltaT);
    if (headMoved)
    {
        forceRecalcMatrix = true;
    }

    if (forceRecalcMatrix)
    {
        RecalcGunTransform();
    }
}

inline bool IsDown(int pos)
{
    return pos == ManPosLying || pos == ManPosBinocLying || pos == ManPosLyingNoWeapon || pos == ManPosHandGunLying;
}
inline bool IsLaunchDown(int pos)
{
    return pos == ManPosWeapon;
}

bool Man::IsDown() const
{
    return ::IsDown(GetActUpDegree());
}

bool Man::IsLaunchDown() const
{
    return ::IsLaunchDown(GetActUpDegree());
}

bool Man::EnableTest(TestEnable func) const
{
    const MoveInfo* info = Type()->GetMoveInfo(_primaryMove.id);
    if (info && !(info->*func)())
    {
        return false;
    }
    if (_primaryFactor < 0.99f)
    {
        const MoveInfo* sec = Type()->GetMoveInfo(_secondaryMove.id);
        if (sec && !(sec->*func)())
        {
            return false;
        }
    }
    return true;
}

float Man::ValueTest(TestValue func, float defValue) const
{
    const MoveInfo* info = Type()->GetMoveInfo(_primaryMove.id);
    float primValue = info ? (info->*func)() : 0;
    if (_primaryFactor < 0.99f)
    {
        const MoveInfo* sec = Type()->GetMoveInfo(_secondaryMove.id);
        float secValue = sec ? (sec->*func)() : defValue;
        primValue = _primaryFactor * primValue + (1 - _primaryFactor) * secValue;
    }
    return primValue;
}

float Man::ValueTest(TestValueTimed func, float defValue) const
{
    const MoveInfo* info = Type()->GetMoveInfo(_primaryMove.id);
    float primValue = info ? (info->*func)(_primaryTime) : 0;
    if (_primaryFactor < 0.99f)
    {
        const MoveInfo* sec = Type()->GetMoveInfo(_secondaryMove.id);
        float secValue = sec ? (sec->*func)(_secondaryTime) : defValue;
        primValue = _primaryFactor * primValue + (1 - _primaryFactor) * secValue;
    }
    return primValue;
}

bool Man::DisableWeapons() const
{
    return WeaponsDisabled();
}

bool Man::EnableMissile() const
{
    const MoveInfo* info = Type()->GetMoveInfo(_primaryMove.id);
    if (info && !info->MissileEnabled())
    {
        return false;
    }
    if (_primaryFactor < 0.99f)
    {
        const MoveInfo* sec = Type()->GetMoveInfo(_secondaryMove.id);
        if (sec && !sec->MissileEnabled())
        {
            return false;
        }
    }
    return true;
}

const BlendAnimSelections& Man::GetBlendAnim(BlendAnimSelections& tgt, BlendAnimFunc func) const
{
    const ManType* type = Type();
    if (_primaryFactor > 0.99f)
    {
        const MoveInfo* move = type->GetMoveInfo(_primaryMove.id);
        if (move)
        {
            return (move->*func)();
        }
        return tgt;
    }

    const BlendAnimSelections* anim1 = nullptr;
    const BlendAnimSelections* anim2 = nullptr;

    const MoveInfo* move1 = type->GetMoveInfo(_primaryMove.id);
    const MoveInfo* move2 = type->GetMoveInfo(_secondaryMove.id);
    if (move1)
    {
        anim1 = &(move1->*func)();
    }
    if (move2)
    {
        anim2 = &(move2->*func)();
    }
    if (anim1 == anim2)
    {
        return *anim1;
    }
    tgt.AddOther(*anim1, _primaryFactor);
    tgt.AddOther(*anim1, 1 - _primaryFactor);
    return tgt;
}

const BlendAnimSelections& Man::GetAiming(BlendAnimSelections& tgt) const
{
    return GetBlendAnim(tgt, &MoveInfo::GetAiming);
}
const BlendAnimSelections& Man::GetLegs(BlendAnimSelections& tgt) const
{
    return GetBlendAnim(tgt, &MoveInfo::GetLegs);
}
const BlendAnimSelections& Man::GetHead(BlendAnimSelections& tgt) const
{
    return GetBlendAnim(tgt, &MoveInfo::GetHead);
}

float Man::VisibleMovement() const
{
    if (_shootVisible > 1)
    {
        return _shootVisible;
    }
    float vis = base::VisibleMovement();

    const MoveInfo* pri = Type()->GetMoveInfo(_primaryMove.id);
    if (pri)
    {
        vis *= pri->GetVisibleSize();
    }
    if (_hideBody > 0)
    {
        float bodyNotHidden = floatMax(1 - _hideBody, 0);
        vis *= bodyNotHidden;
    }

    return vis;
}

float Man::Audible() const
{
    float vis = 0.2f;
    if (_whenScreamed < Glob.time && _whenScreamed > Glob.time - 30)
    {
        saturateMax(vis, 1.2f);
    }

    float rSpeed = Speed().SizeXZ() * 0.5f;
    saturateMin(rSpeed, 4);
    saturateMax(vis, rSpeed);
    vis *= GetType()->GetAudible();
    saturateMax(vis, _shootAudible);
    return vis;
}

float Man::GetHidden() const
{
    return 1 - _surround.Track(this, Position(), 50, 0.5f);
}

float Man::VisibleSize() const
{
    float bodyNotHidden = floatMax(1 - _hideBody, 0);
    float vis = GetShape()->GeometrySphere() * bodyNotHidden;

    const MoveInfo* pri = Type()->GetMoveInfo(_primaryMove.id);
    if (pri)
    {
        vis *= pri->GetVisibleSize();
    }

    return vis;
}

float Man::CollisionSize() const
{
    if (IsDown())
    {
        return GetShape()->GeometrySphere();
    }
    return base::CollisionSize();
}

Vector3 Man::VisiblePosition() const
{
    return AimingPosition();
}

Vector3 Man::CalculateAimingPosition(Matrix4Par pos) const
{
    if (Type()->_aimPoint >= 0)
    {
        int level = _shape->FindMemoryLevel();
        return pos.FastTransform(AnimatePoint(level, Type()->_aimPoint));
    }
    else
    {
        LOG_DEBUG(Physics, "Obsolete aiming position");
        Vector3 basePos = base::AimingPosition();
        if (IsDown())
        {
            return basePos - Vector3(0, 1.25f, 0);
        }
        return basePos;
    }
}

Vector3 Man::CalculateCameraPosition(Matrix4Par pos) const
{
    // falls back to aiming position when no explicit pilot point is defined
    int level = _shape->FindMemoryLevel();
    int selIndex = Type()->_pilotPoint;
    if (selIndex < 0)
    {
        return CalculateAimingPosition(pos);
    }
    const Selection& sel = _shape->LevelOpaque(level)->NamedSel(selIndex);
    return pos.FastTransform(AnimatePoint(level, sel[0]));
}

Vector3 Man::AimingPosition() const
{
    return _aimingPositionWorld;
}

Vector3 Man::CameraPosition() const
{
    return _cameraPositionWorld;
}

const float DownArmorCoef = 2;

float Man::GetArmor() const
{
    float armor = base::GetArmor();
    if (IsDown())
    {
        return armor * DownArmorCoef;
    }
    return armor;
}
float Man::GetInvArmor() const
{
    float iArmor = base::GetInvArmor();
    if (IsDown())
    {
        return iArmor * (1 / DownArmorCoef);
    }
    return iArmor;
}

float Man::FireAngleInRange(int weapon, Vector3Par rel) const
{
    const ManType* type = Type();
    float dist2 = rel.SquareSizeXZ();
    float y2 = Square(rel.Y());
    // x>0: atan(x)<x
    // atan(y/sqrt(dist2)) < type->_maxGunElev
    // y/sqrt(dist2) < type->_maxGunElev
    // y < type->_maxGunElev * sqrt(dist2)
    // y2 < type->_maxGunElev^2 * dist2
    // we need to have correct signs
    PoseidonAssert(type->_maxGunElev >= 0);
    PoseidonAssert(type->_minGunElev <= 0);
    float ret = 0;
    if (rel.Y() >= 0)
    {
        // fire up
        if (y2 > dist2 * Square(type->_maxGunElev))
        {
            return 0;
        }
        ret = rel.Y() * InvSqrt(dist2) * (1 / type->_maxGunElev);
    }
    else
    {
        // fire down
        if (y2 > dist2 * Square(type->_minGunElev))
        {
            return 0;
        }
        ret = rel.Y() * InvSqrt(dist2) * (1 / type->_minGunElev);
    }
    return floatMin(1, 2 - ret * 2);
}

float Man::RifleInaccuracy() const
{
    float woundFactor = GetHitCont(Type()->_handsHit) * 10 + 1;
    woundFactor += floatMin(_tired * 2, 1) * 6;
    float speedXZ = _speed.SizeXZ();
    float ret = floatMin(speedXZ * 0.5f, 1) * 0.05f + 0.005f;
    const MoveInfo* prim = Type()->GetMoveInfo(_primaryMove.id);
    if (prim)
    {
        ret *= prim->GetAimPrecision();
    }
    const WeaponModeType* mode = GetCurrentWeaponMode();
    // Note: make coefficient part of WeaponModeType
    if (mode && mode->_ammo && mode->_ammo->_simulation == AmmoShotLaser)
    {
        ret *= 0.2f;
    }
    if (QIsManual() && TouchInput_IsAimFocusActive())
    {
        ret *= 0.35f;
    }
    return ret * woundFactor;
}

} // namespace Poseidon
