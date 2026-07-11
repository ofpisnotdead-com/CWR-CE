#include <Poseidon/World/Entities/Infantry/SoldierOldCommon.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Input/InputSubsystem.hpp>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/BoolArray.hpp>
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
#include <Poseidon/Foundation/platform.hpp>
#include <Random/randomGen.hpp>

void WhatUnits(RefArray<NetworkObject>& units, ChatChannel channel, NetworkObject* object);

namespace Poseidon
{
void Man::ProcessMoveFunction(ActionContextBase* context)
{
    switch (context->function)
    {
        case MFGetIn:
        {
            AIUnit* unit = Brain();
            ActionContextGetIn* ctx = static_cast<ActionContextGetIn*>(context);
            if (unit)
            {
                unit->ProcessGetIn2(ctx->vehicle, ctx->position);
            }
        }
        break;
        case MFReload:
        {
            ActionContextDefault* cd = static_cast<ActionContextDefault*>(context);
            int iMagazine = -1;
            for (int i = 0; i < NMagazines(); i++)
            {
                const Magazine* mag = GetMagazine(i);
                if (!mag)
                {
                    continue;
                }
                if (mag->_creator == cd->param && mag->_id == cd->param2)
                {
                    iMagazine = i;
                    break;
                }
            }
            if (iMagazine < 0)
            {
                break;
            }

            const char* separator = strchr(cd->param3, '|');
            if (!separator)
            {
                break;
            };
            int separatorPos = separator - cd->param3;
            RString weapon = cd->param3.Substring(0, separatorPos);
            RString muzzle = cd->param3.Substring(separatorPos + 1, INT_MAX);
            int iSlot = -1;
            for (int i = 0; i < NMagazineSlots(); i++)
            {
                const MagazineSlot& slot = GetMagazineSlot(i);
                if (slot._weapon->GetName() == weapon && slot._muzzle->GetName() == muzzle)
                {
                    iSlot = i;
                    break;
                }
            }
            if (iSlot < 0)
            {
                break;
            }

            ReloadMagazineTimed(iSlot, iMagazine, true);
        }
        break;
        case MFThrowGrenade:
        {
            ActionContextDefault* cd = static_cast<ActionContextDefault*>(context);
            ThrowGrenadeAction(cd->param);
        }
        break;
        case MFUIAction:
        {
            ActionContextUIAction* cd = static_cast<ActionContextUIAction*>(context);
            ProcessUIAction(cd->action);
        }
        break;
        case MFDead:
            if (ENGINE_CONFIG.blood && GRandGen.RandomValue() <= 0.3f)
            {
                LODShapeWithShadow* shape = GLOB_SCENE->Preloaded(SlopBlood);
                if (!shape)
                {
                    break;
                }
                float azimut = GRandGen.RandomValue() * H_PI * 2;

                Matrix4 transform(MIdentity);
                transform.SetOrientation(Matrix3(MRotationY, azimut));

                Vector3Val pos = Position();
                float surfY = GLandscape->RoadSurfaceY(pos + VUp * 0.5f);
                Vector3 offset = transform.Rotate(shape->BoundingCenter());
                transform.SetPosition(Vector3(pos[0], surfY, pos[2]) + offset);

                float ttl = 180.0f + 420.0f * GRandGen.RandomValue();
                Slop* slop = new Slop(shape, VehicleTypes.New("slop"), transform, ttl);
                slop->SetTransform(transform);
                slop->SetAlpha(0.7f);
                GWorld->AddAnimal(slop);
            }
            break;
    }
}

bool Man::AdvanceMoveQueue(float deltaT, float adjustSpeed, float& moveX, float& moveZ, SimulationImportance prec)
{
    bool change = false;
    bool finished = false;

    const MoveInfo* prim = Type()->GetMoveInfo(_primaryMove.id);
    if (prim)
    {
        const AnimationRT* primA = *prim;
        float priSpeed = prim->GetSpeed();
        if (priSpeed > 1e5)
        {
            // static looped animation: no movement contribution
            _primaryTime = 0;
            finished = true;
        }
        else
        {
            change = true;

            float priPhaseChng = deltaT * priSpeed * adjustSpeed;
            float oldPrimTime = _primaryTime;
            _primaryTime += priPhaseChng;

            if (_primaryTime >= 1)
            {
                finished = true;
                if (primA->GetLooped())
                {
                    _primaryTime -= 1;
                    if (_primaryTime >= 1)
                    {
                        _primaryTime = 0.5f;
                    }
                }
                else
                {
                    priPhaseChng -= _primaryTime - 1;
                    _primaryTime = 1;
                }
            }

            float moveFactor = priPhaseChng * _primaryFactor;

            moveZ -= moveFactor * primA->GetStepLength();
            moveX -= moveFactor * primA->GetStepLengthX();

            if (prim->GetSoundEnabled())
            {
                float edge1Time = prim->GetSoundEdge1();
                float edge2Time = prim->GetSoundEdge2();
                bool step = false;
                bool left = false;
                if (oldPrimTime < edge1Time && oldPrimTime + priPhaseChng >= edge1Time)
                {
                    _doSoundStep = true;
                    step = true;
                    left = true;
                    _soundStepOverride = prim->GetSoundOverride();
                }
                else if (oldPrimTime < edge2Time && oldPrimTime + priPhaseChng >= edge2Time)
                {
                    _doSoundStep = true;
                    step = true;
                    _soundStepOverride = prim->GetSoundOverride();
                }
                if (step && EnableVisualEffects(prec) && ValueTest(&MoveInfo::OnLand) < 0.5f)
                {
                    int index = (left ? Type()->_stepLIndex : Type()->_stepRIndex);
                    if (index >= 0)
                    {
                        Vector3 mPos = AnimatePoint(_shape->FindMemoryLevel(), index);
                        Vector3 pos = PositionModelToWorld(mPos);
                        float timeToLive = 10;
                        float ys = GLandscape->SurfaceY(pos.X(), pos.Z());
                        float yr = GLandscape->RoadSurfaceYAboveWater(pos);
                        if (yr <= ys + 0.2f)
                        {
                            LODShapeWithShadow* shape = GScene->Preloaded(left ? FootStepL : FootStepR);
                            if (shape)
                            {
                                Ref<Mark> mark = new Mark(shape, 0.25f, timeToLive);
                                mark->SetDirectionAndUp(Direction(), VUp);
                                mark->SetPosition(pos);
                                GWorld->AddAnimal(mark);
                            }
                        }
                    }
                }
            }
        }
    }

    if (_primaryFactor < 0.99f)
    {
        const MoveInfo* sec = Type()->GetMoveInfo(_secondaryMove.id);
        if (sec)
        {
            const AnimationRT* secA = *sec;
            float secSpeed = sec->GetSpeed();

            if (secSpeed > 1e5)
            {
                _secondaryTime = 0;
            }
            else
            {
                change = true;

                float secPhaseChng = deltaT * secSpeed * adjustSpeed;

                _secondaryTime += secPhaseChng;

                if (_secondaryTime >= 1)
                {
                    if (secA->GetLooped())
                    {
                        _secondaryTime -= 1;
                        if (_secondaryTime >= 1)
                        {
                            _secondaryTime = 0.5f;
                        }
                    }
                    else
                    {
                        secPhaseChng -= _secondaryTime - 1;
                        _secondaryTime = 1;
                    }
                }

                float moveFactor = secPhaseChng * (1 - _primaryFactor);

                moveZ -= moveFactor * secA->GetStepLength();
                moveX -= moveFactor * secA->GetStepLengthX();
            }
        }
    }

    if (_primaryFactor < 0.99f)
    {
        float interpolSpeed = GetInterpolSpeed();
        _primaryFactor += deltaT * interpolSpeed;
        saturateMin(_primaryFactor, 1);
        return true;
    }
    _primaryFactor = 1;

    if (_primaryMove.id == _forceMove.id)
    {
        _forceMove = MotionPathItem((MoveId)MoveIdNone); // forced move running as primary (no secondary)
    }

    if (finished && _primaryMove.id == _externalMove.id)
    {
        _externalMoveFinished = true;
    }

    if (finished && _primaryMove.context)
    {
        ProcessMoveFunction(_primaryMove.context);
        _primaryMove.context = nullptr;
    }

    if (_externalMoveFinished)
    {
        _externalMove = MotionPathItem((MoveId)MoveIdNone);
    }

    // get next move from move queue
    if (_queueMove.Size() <= 0)
    {
        return change;
    }

    if (prim->GetTerminal())
    {
        return change;
    }

    MotionPathItem nextMove = _queueMove[0];
    if (_primaryMove.id == nextMove.id)
    {
        // same move: transfer context without restarting
        _primaryMove = nextMove;
    }
    else
    {
        const MotionEdge& edge = Type()->Edge(_primaryMove.id, nextMove.id);
        switch ((MotionEdgeType)edge.type)
        {
            case MEdgeNone:
                LOG_DEBUG(Physics, "No edge from {} to {}", NAME_T(Type(), _primaryMove.id),
                          NAME_T(Type(), nextMove.id));
                SetPrimaryMove(nextMove);
                _primaryTime = 0;
                _primaryFactor = 1;
                change = true;

                break;
            case MEdgeInterpol:
            {
                _secondaryMove = _primaryMove;
                _secondaryTime = _primaryTime;
                SetPrimaryMove(nextMove);

                const MoveInfo* pri = Type()->GetMoveInfo(_primaryMove.id);
                if (pri->GetInterpolRestart())
                {
                    _primaryTime = 0;
                }
                _primaryFactor = 0;
            }
            break;
            case MEdgeSimple:
                if (!finished)
                {
                    return change;
                }
                SetPrimaryMove(nextMove);
                _primaryFactor = 1;
                _primaryTime = 0;
                break;
        }
    }
    _queueMove.Delete(0);
    if (_queueMove.Size() <= 0)
    {
        _stillMoveQueueEnd = MoveIdNone;
    }
    return change;
}

void Man::PlayMove(RStringB move, ActionContextBase* context)
{
    MoveId id = Type()->GetMoveId(move);
    _externalQueue.Add(MotionPathItem(id, context));
}

void Man::SwitchMove(MoveId id, ActionContextBase* context)
{
    if (_externalMove.id != MoveIdNone)
    {
        // return _externMove (if any) to queue
        _externalQueue.Insert(0, _externalMove);
    }
    _externalMove = MotionPathItem(id, context);
    _externalMoveFinished = false;
    if (id == MoveIdNone)
    {
        id = GetDefaultMove();
    }
    _primaryMove = _externalMove;
    _secondaryMove = _externalMove;
    _queueMove.Clear();
    _stillMoveQueueEnd = id; // be ready to select any variant
    _variantTime = Glob.time;
    _primaryFactor = 1;
    _primaryTime = 0;
    _secondaryTime = 0;
    _gunXRotWanted = _gunXRot = 0;
    _gunYRotWanted = _gunYRot = 0;
    _gunXSpeed = 0;
    _gunYSpeed = 0;
    _headXRotWanted = _headXRot = 0;
    _headYRotWanted = _headYRot = 0;
    RecalcGunTransform();
}

RString Man::GetCurrentMove() const
{
    int n = _queueMove.Size();
    MoveId id = (n == 0 ? _primaryMove.id : _queueMove[n - 1].id);
    return id < 0 ? RString() : Type()->GetMoveName(id);
}

void Man::SwitchMove(RStringB move, ActionContextBase* context)
{
    MoveId id = Type()->GetMoveId(move);
    SwitchMove(id, context);
}

bool Man::PlayAction(ManAction action, ActionContextBase* context)
{
    MoveId id = GetMove((ManAction)action);
    if (id == MoveIdNone)
    {
        if (context)
        {
            ProcessMoveFunction(context);
        }
        return false;
    }
    _externalQueue.Add(MotionPathItem(id, context));
    return true;
}

bool Man::SwitchAction(ManAction action, ActionContextBase* context)
{
    MoveId id = GetMove((ManAction)action);
    if (id == MoveIdNone)
    {
        if (context)
        {
            ProcessMoveFunction(context);
        }
        return false;
    }
    SwitchMove(id, context);
    return true;
}

void Man::SwitchVehicleAction(ManVehAction action)
{
    MoveId id = GetVehMove(action);
    if (id == MoveIdNone)
    {
        return;
    }
    SwitchMove(id, nullptr);
    _legTrans = MIdentity;
}

bool Man::CheckActionProcessing(UIActionType action, AIUnit* unit) const
{
    return IsActionInProgress(MFUIAction);
}

void __cdecl Heal(Person* me, const UIAction& action);

void __cdecl Heal(Person* me, const UIAction& action)
{
    Person* person = dyn_cast<Person, EntityAI>(action.target);
    if (person && person != me)
    {
        Vector3 dir = (me->Position() - person->Position()).Normalized();

        Matrix4 trans;
        trans.SetUpAndDirection(VUp, -dir);
        trans.SetPosition(me->Position());
        me->Move(trans);

        Vector3 pos = me->Position() - dir;
        pos[1] = GLandscape->RoadSurfaceYAboveWater(pos + VUp * 0.5f);
        trans.SetUpAndDirection(VUp, dir);
        trans.SetPosition(pos);
        person->Move(trans);

        person->PlayAction(ManActMedic);
        person->PlayAction(ManActUp);
    }

    Ref<ActionContextUIAction> context = new ActionContextUIAction(action);
    me->PlayAction(ManActTreated, context);
    me->PlayAction(ManActUp);
}

void Man::StartActionProcessing(const UIAction& action, AIUnit* unit)
{
    switch (action.type)
    {
        case ATHeal:
        {
            Heal(this, action);
            break;
        }
        case ATRepair:
        case ATRefuel:
        case ATRearm:
        case ATTakeWeapon:
        case ATTakeMagazine:
        case ATDropWeapon:
        case ATDropMagazine:
        case ATSetTimer:
        case ATDeactivate:
        case ATUseWeapon:
        case ATUseMagazine:
        case ATHideBody:
        case ATFireInflame:
        case ATFirePutDown:
        case ATDeactivateMine:
        case ATTakeMine:
        {
            Ref<ActionContextUIAction> context = new ActionContextUIAction(action);
            PlayAction(ManActPutDown, context);
            break;
        }
        case ATTakeFlag:
        case ATReturnFlag:
        {
            // pole flag requires a different pickup animation
            Ref<ActionContextUIAction> context = new ActionContextUIAction(action);
            if (!dyn_cast<FlagCarrier, EntityAI>(context->action.target))
            {
                PlayAction(ManActPutDown, context);
            }
            else
            {
                PlayAction(ManActTakeFlag, context);
            }
            break;
        }
        case ATLadderOnDown:
        case ATLadderDown:
        {
            Ref<ActionContextUIAction> context = new ActionContextUIAction(action);
            PlayAction(ManActLadderOnDown, context);
            break;
        }
        case ATLadderOff:
        {
            action.Process(unit);
            PlayAction(ManActLadderOff);
            break;
        }
        case ATLadderOnUp:
        case ATLadderUp:
        {
            Ref<ActionContextUIAction> context = new ActionContextUIAction(action);
            PlayAction(ManActLadderOnUp, context);
            break;
        }
        default:
            action.Process(unit);
            break;
    }
}

bool Man::IsActionInProgress(MoveFinishF action) const
{
    if (_primaryMove.context && _primaryMove.context->function == action)
    {
        return true;
    }
    if (_externalMove.context && _externalMove.context->function == action)
    {
        return true;
    }
    for (int i = 0; i < _externalQueue.Size(); i++)
    {
        ActionContextBase* context = _externalQueue[i].context;
        if (context && context->function == action)
        {
            return true;
        }
    }
    return false;
}

bool Man::ReloadMagazine(int slotIndex, int iMagazine)
{
    const MagazineSlot& slot = GetMagazineSlot(slotIndex);
    const MuzzleType* muzzle = slot._muzzle;

    bool ret = false;
    if (!IsActionInProgress(MFReload))
    {
        bool reloadOk = false;
        AIUnit* unit = CommanderUnit();
        if (unit && !muzzle->_autoReload)
        {
            const Magazine* magazine = GetMagazine(iMagazine);
            RString muzzleID = slot._weapon->GetName() + RString("|") + slot._muzzle->GetName();
            Ref<ActionContextDefault> context = new ActionContextDefault;

            context->function = MFReload;
            context->param = magazine->_creator;
            context->param2 = magazine->_id;
            context->param3 = muzzleID;

            reloadOk = unit->GetPerson()->PlayAction(magazine->_type->_reloadAction, context);
            if (reloadOk)
            {
                PlayReloadMagazineSound(slotIndex, muzzle);
                ret = true;
            }
        }
        if (!reloadOk)
        {
            ret = ReloadMagazineTimed(slotIndex, iMagazine, false);
        }
    }
    return ret;
}

bool Man::EnableWeaponManipulation() const
{
    if (IsActionInProgress(MFReload))
    {
        return false;
    }
    if (DisableWeaponsLong())
    {
        return false;
    }
    return base::EnableWeaponManipulation();
}

bool Man::EnableViewThroughOptics() const
{
    if (BinocularSelected() && !EnableBinocular())
    {
        return false;
    }
    return EnableOptics();
}

void Man::ResetMovement(float speed, int action)
{
    if (action < 0)
    {
        action = ManActDefault;
    }
    if (_isDead)
    {
        action = ManActDie;
    }
    ManAction act = (ManAction)action;
    MoveId move = GetDefaultMove(act);

    int primaryIndex = FindWeaponType(MaskSlotPrimary);
    int handGunIndex = FindWeaponType(MaskSlotHandGun);
    if (handGunIndex >= 0 && primaryIndex < 0)
    {
        SelectHandGun(true);
    }
    if (primaryIndex >= 0 && handGunIndex < 0)
    {
        SelectHandGun(false);
    }

    AIUnit* unit = Brain();
    int upDegreeWanted;
    if (unit)
    {
        upDegreeWanted = GetUpDegree(unit->GetCombatMode(), IsHandGunSelected());
    }
    else
    {
        upDegreeWanted = IsHandGunSelected() ? ManPosHandGunStand : ManPosCombat;
    }

    if (_primaryMove.id != MoveIdNone)
    {
        const ActionMap* map = Type()->GetActionMap(_primaryMove.id);
        if (map->GetUpDegree() == upDegreeWanted)
        {
            MoveId moveWanted = map->GetAction(act);
            if (moveWanted != MoveIdNone)
            {
                move = moveWanted;
            }
        }
    }
    _queueMove.Clear();
    SetPrimaryMove(MotionPathItem(move));

    _turnWanted = 0;
    _landContact = false;
    _objectContact = false;
    _tired = 0;
    _secondaryMove = _primaryMove;
    _primaryTime = _secondaryTime = 0;
    _primaryFactor = 1;
    _speed = VZero;
    _angMomentum = VZero;
    _impulseForce = VZero;
    _impulseTorque = VZero;

    if (unit)
    {
        Path& path = unit->GetPath();
        path.SetSearchTime(Glob.time - 60);
    }
    _modelSpeed = VZero;
    _externalQueue.Clear();
    _externalMove = MotionPathItem((MoveId)MoveIdNone);

    _gunXRotWanted = _gunXRot = 0;
    _gunYRotWanted = _gunYRot = 0;
    _gunXSpeed = 0;
    _gunYSpeed = 0;
    _headXRotWanted = _headXRot = 0;
    _headYRotWanted = _headYRot = 0;

    RecalcGunTransform();
    if (IsInLandscape())
    {
        RecalcPositions(*this);
    }
}

static ArcadeMarkerInfo* FindMarker(RString name)
{
    AUTO_STATIC_ARRAY(int, indices, 16);
    int len = name.GetLength();
    for (int i = 0; i < markersMap.Size(); i++)
    {
        ArcadeMarkerInfo& mInfo = markersMap[i];
        if (strnicmp(mInfo.name, name, len) == 0)
        {
            indices.Add(i);
        }
    }
    if (indices.Size() == 0)
    {
        return nullptr;
    }
    int index = toIntFloor(indices.Size() * GRandGen.RandomValue());
    return &markersMap[indices[index]];
}

static ArcadeMarkerInfo* FindRespawnMarker(Person* veh)
{
    if (veh && veh->GetVarName().GetLength() > 0)
    {
        RString name = RString("respawn_") + veh->GetVarName();
        ArcadeMarkerInfo* info = FindMarker(name);
        if (info)
        {
            return info;
        }
    }

    AIUnit* unit = veh ? veh->Brain() : nullptr;
    AIGroup* grp = unit ? unit->GetGroup() : nullptr;
    if (grp)
    {
        unit = grp->Leader();
        veh = unit ? unit->GetPerson() : nullptr;
        if (veh && veh->GetVarName().GetLength() > 0)
        {
            RString name = RString("respawn_") + veh->GetVarName();
            ArcadeMarkerInfo* info = FindMarker(name);
            if (info)
            {
                return info;
            }
        }

        AICenter* center = grp->GetCenter();
        if (center)
        {
            RString name;
            switch (center->GetSide())
            {
                case TWest:
                    name = "respawn_west";
                    goto SideFound;
                case TEast:
                    name = "respawn_east";
                    goto SideFound;
                case TGuerrila:
                    name = "respawn_guerrila";
                    goto SideFound;
                case TCivilian:
                    name = "respawn_civilian";
                    goto SideFound;
                SideFound:
                {
                    ArcadeMarkerInfo* info = FindMarker(name);
                    if (info)
                    {
                        return info;
                    }
                }
                break;
            }
        }
    }

    return FindMarker("respawn");
}

static Person* FindGroupRespawn(Person* who, bool& leader)
{
    AIUnit* unit = who->Brain();
    if (!unit)
    {
        Fail("Respawning unit with no brain");
        return nullptr;
    }
    AIGroup* grp = unit->GetGroup();
    if (!grp)
    {
        Fail("Respawning unit with no group");
        return nullptr;
    }
    // Note: two units may want to respawn to the same unit simultaneously
    if (unit->IsGroupLeader())
    {
        AIUnit* respawn = grp->LeaderCandidate(unit);
        if (respawn && respawn->IsLocal() && !respawn->GetPerson()->IsNetworkPlayer() &&
            respawn->GetLifeState() == AIUnit::LSAlive && !respawn->GetPerson()->IsDammageDestroyed())
        {
            leader = true;
            return respawn->GetPerson();
        }
    }
    float minExpDif = 1e10;
    AIUnit* respawn = grp->Leader();
    if (respawn->GetPerson()->IsNetworkPlayer() || respawn == unit || respawn->GetLifeState() != AIUnit::LSAlive)
    {
        respawn = nullptr;
    }
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* u = grp->UnitWithID(i + 1);
        if (!u || u == unit || u->GetLifeState() != AIUnit::LSAlive)
        {
            continue;
        }
        if (u->IsGroupLeader())
        {
            continue;
        }
        Person* person = u->GetPerson();
        if (!person->IsLocal())
        {
            continue;
        }
        if (person->IsNetworkPlayer())
        {
            continue;
        }
        if (person->IsDammageDestroyed())
        {
            continue;
        }
        float expDif = person->GetExperience() - who->GetExperience();
        if (fabs(expDif) < minExpDif)
        {
            minExpDif = expDif;
            respawn = u;
        }
    }
    if (respawn)
    {
        return respawn->GetPerson();
    }
    return nullptr;
}

static bool RunRespawnScript(const char* script, Person* man, EntityAI* killer, GameValue par3)
{
    // terminate any in-progress camera script before starting a new one
    GWorld->TerminateCameraScript();

    RString name = script;
    if (QIFStreamB::FileExist(RString("scripts\\") + name))
    {
        GameArrayType arguments;
        arguments.Add(GameValueExt(man));
        arguments.Add(GameValueExt(killer));
        arguments.Add(par3);
        Script* script = new Script(name, GameValue(arguments));
        GWorld->StartCameraScript(script);
        return true;
    }
    return false;
}

} // namespace Poseidon
Person* ProcessGroupRespawn(Person* person, int player)
{
    using namespace Poseidon;
    AISubgroup* subgrp = person->Brain()->GetSubgroup();
    PoseidonAssert(subgrp);
    AIGroup* grp = subgrp->GetGroup();
    PoseidonAssert(grp);
    PoseidonAssert(grp->IsLocal());

    bool leader = false;
    Person* respawn = FindGroupRespawn(person, leader);
    if (!respawn)
    {
        return nullptr;
    }
    respawn->SetRemotePlayer(player);
    if (leader)
    {
        grp->SelectLeader(respawn->Brain());
    }
    AIUnitInfo& info = person->GetInfo();
    saturateMax(info._experience, info._initExperience);
    respawn->SetInfo(info);
    respawn->SetFace(info._face, info._name);
    respawn->SetGlasses(info._glasses);
    GetNetworkManager().CopyUnitInfo(person, respawn);
    // make this vehicle player
    GetNetworkManager().SelectPlayer(player, respawn, true);
    // grp->SetReportedDown(person->Brain(), true);
    grp->SendUnitDown(respawn->Brain(), person->Brain());

    if (leader)
    {
        GetNetworkManager().UpdateObject(subgrp);
        GetNetworkManager().UpdateObject(grp);
    }
    return respawn;
}
namespace Poseidon
{

} // namespace Poseidon
void GroupRespawnDone(Person* person, EntityAI* killer, Person* respawn)
{
    using namespace Poseidon;
    bool script =
        RunRespawnScript("onPlayerRespawnOtherUnit.sqs", person, killer, GameValueExt(respawn->Brain()->GetVehicle()));
    if (!script)
    {
        GWorld->SwitchCameraTo(respawn->Brain()->GetVehicle(), CamExternal);
    }
}
namespace Poseidon
{

} // namespace Poseidon
void ProcessSeagullRespawn(Person* person, EntityAI* killer)
{
    using namespace Poseidon;
    Vector3 pos = person->WorldPosition() + 20.0f * VUp;
    SeaGullAuto* seagull = dyn_cast<SeaGullAuto>(NewNonAIVehicle("seagull", nullptr));
    if (seagull)
    {
        DoAssert(pos.IsFinite());
        seagull->SetPosition(pos);
        seagull->SetManual(true);
        seagull->MakeAirborne(20);
        GWorld->AddAnimal(seagull); // insert vehicle to both landscape and world
        GetNetworkManager().CreateVehicle(seagull, VLTAnimal, "", -1);
    }
    bool script = RunRespawnScript("onPlayerRespawnAsSeagull.sqs", person, killer, GameValueExt(seagull));
    if (!script && seagull)
    {
        GWorld->SwitchCameraTo(seagull, CamExternal);
    }

    AIUnit* brain = person->Brain();
    if (brain)
    {
        AIGroup* grp = brain->GetGroup();
        if (grp && grp->Leader() == brain)
        {
            int total = 0, players = 0;
            for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
            {
                AIUnit* unit = grp->UnitWithID(i + 1);
                if (!unit)
                {
                    continue;
                }
                if (unit == brain)
                {
                    continue;
                }
                total++;
                if (unit->IsAnyPlayer())
                {
                    players++;
                }
            }
            if (players > 0 && total == players)
            {
                grp->SetReportedDown(brain, true);
                AISubgroup* subgrp = brain->GetSubgroup();
                subgrp->ReceiveAnswer(brain, AI::UnitDestroyed);
            }
        }
        brain->SetLifeState(AIUnit::LSDead);
    }

    if (person->IsLocal())
    {
        GetNetworkManager().DisposeBody(person);
    }
}
namespace Poseidon
{

void Man::KilledBy(EntityAI* owner)
{
    PoseidonAssert(_brain);
    if (!_brain)
    {
        return;
    }
    AIUnit::LifeState state = _brain->GetLifeState();
    if (state == AIUnit::LSDead || state == AIUnit::LSDeadInRespawn)
    {
        return;
    }
    bool player = this == GWorld->GetRealPlayer();
    bool playable = _brain->IsPlayable();
    RespawnMode mode = RespawnNone;
    if (GWorld->GetMode() == GModeNetware)
    {
        mode = GetNetworkManager().GetRespawnMode();
        if (player)
        {
            char message[256];
            AIUnit* killerUnit = owner ? owner->CommanderUnit() : nullptr;

            AIGroup* myGroup = _brain->GetGroup();
            AICenter* myCenter = myGroup ? myGroup->GetCenter() : nullptr;
            AIGroup* killerGroup = killerUnit ? killerUnit->GetGroup() : nullptr;
            AICenter* killerCenter = killerGroup ? killerGroup->GetCenter() : nullptr;
            bool friendly = myCenter && killerCenter && killerCenter->IsFriendly(myCenter->GetSide()) &&
                            owner->GetTargetSide() != TEnemy && GetTargetSide() != TEnemy && owner != this;

            if (killerUnit && killerUnit->GetPerson()->IsRemotePlayer())
            {
                snprintf(message, sizeof(message),
                         friendly ? LocalizeString(IDS_KILLED_BY_FRIENDLY) : LocalizeString(IDS_KILLED_BY),
                         (const char*)_brain->GetPerson()->GetInfo()._name,
                         myGroup ? (const char*)myGroup->GetName() : "", _brain->ID(),
                         (const char*)killerUnit->GetPerson()->GetInfo()._name);
            }
            else
            {
                snprintf(message, sizeof(message),
                         friendly ? LocalizeString(IDS_KILLED_FRIENDLY) : LocalizeString(IDS_KILLED),
                         (const char*)_brain->GetPerson()->GetInfo()._name,
                         myGroup ? (const char*)myGroup->GetName() : "", _brain->ID());
            }
            GChatList.Add(CCGlobal, nullptr, message, false, true);
            RefArray<NetworkObject> units;
            WhatUnits(units, CCGlobal, nullptr);
            RadioSentence rs = RadioSentence();
            GetNetworkManager().RadioChat(CCGlobal, nullptr, units, message, rs);
        }
    }
    switch (mode)
    {
        default:
            Fail("Respawn mode");
        case RespawnNone:
            if (player)
            {
                if (GWorld->GetMode() != GModeNetware)
                {
                    GChatList.Clear();
                }
                RString name = "onPlayerKilled.sqs";
                if (QIFStreamB::FileExist(RString("scripts\\") + name))
                {
                    GWorld->EnableEndDialog(false);
                    GameArrayType arguments;
                    arguments.Add(GameValueExt(this));
                    arguments.Add(GameValueExt(owner));
                    Script* script = new Script(name, GameValue(arguments));
                    GWorld->StartCameraScript(script);
                }
            }
            break;
        case RespawnToGroup:
        GroupRespawn:
            if (player)
            {
                Brain()->SetLifeState(AIUnit::LSDead);
                GetNetworkManager().UpdateObject(this);
                GetNetworkManager().UpdateObject(Brain());
                AIGroup* grp = Brain()->GetGroup();
                if (grp->IsLocal())
                {
                    Person* respawn = ProcessGroupRespawn(this, GetNetworkManager().GetPlayer());
                    if (respawn)
                    {
                        GroupRespawnDone(this, owner, respawn);
                    }
                    else
                    {
                        ProcessSeagullRespawn(this, owner);
                    }
                }
                else
                {
                    GetNetworkManager().AskForGroupRespawn(this, owner);
                }
                if (IsLocal())
                {
                    GetNetworkManager().DisposeBody(this);
                }
                return;
            }
            break;
        case RespawnToFriendly:
            Fail("RespawnToFriendly (respawn=side) not implemented");
            goto GroupRespawn;
        case RespawnSeaGull:
            if (player)
            {
                ProcessSeagullRespawn(this, owner);
                return;
            }
            break;
        case RespawnInBase:
            if (playable)
            {
                Transport* veh = _brain->GetVehicleIn();
                if (veh)
                {
                    _brain->ProcessGetOut(false);
                }
                Vector3 pos = Position();
                ArcadeMarkerInfo* info = FindRespawnMarker(this);
                if (info)
                {
                    if (info->markerType == MTIcon || info->a == 0 || info->b == 0)
                    {
                        pos = info->position;
                    }
                    else if (info->markerType == MTRectangle)
                    {
                        float a = (2.0f * GRandGen.RandomValue() - 1.0f) * info->a;
                        float b = (2.0f * GRandGen.RandomValue() - 1.0f) * info->b;
                        float angleRad = -HDegree(info->angle);
                        float s = sin(angleRad);
                        float c = cos(angleRad);
                        pos = info->position;
                        pos[0] += c * a + s * b;
                        pos[2] += s * a - c * b;
                    }
                    else
                    {
                        PoseidonAssert(info->markerType == MTEllipse) float e = sqrt(Square(info->a) - Square(info->b));
                        float a, b;
                        do
                        {
                            a = (2.0f * GRandGen.RandomValue() - 1.0f) * info->a;
                            b = (2.0f * GRandGen.RandomValue() - 1.0f) * info->b;
                        } while (sqrt(Square(a - e) + Square(b)) + sqrt(Square(a + e) + Square(b)) > 2.0f * info->a);

                        float angleRad = -HDegree(info->angle);
                        float s = sin(angleRad);
                        float c = cos(angleRad);
                        pos = info->position;
                        pos[0] += c * a + s * b;
                        pos[2] += s * a - c * b;
                    }
                }
                _brain->FindNearestEmpty(pos);
                pos[1] = GLandscape->RoadSurfaceYAboveWater(pos + VUp * 0.5f);
                if (player)
                {
                    RunRespawnScript("onPlayerRespawn.sqs", this, owner, GetNetworkManager().GetRespawnDelay());
                }
                GetNetworkManager().Respawn(this, pos);
                _brain->SetLifeState(AIUnit::LSDeadInRespawn);
                return;
            }
            break;
        case RespawnAtPlace:
            if (playable)
            {
                Transport* veh = _brain->GetVehicleIn();
                if (veh)
                {
                    _brain->ProcessGetOut(false);
                }
                if (player)
                {
                    RunRespawnScript("onPlayerRespawn.sqs", this, owner, GetNetworkManager().GetRespawnDelay());
                }
                GetNetworkManager().Respawn(this, Position());
                _brain->SetLifeState(AIUnit::LSDeadInRespawn);
                return;
            }
            break;
    }

    Transport* veh = _brain->VehicleAssigned();
    if (veh)
    {
        veh->UpdateStop();
    }
    GLOB_WORLD->RemoveSensor(this);
    AIUnit* unit = _brain;
    unit->SendAnswer(AI::UnitDestroyed);

    if (IsLocal())
    {
        GetNetworkManager().DisposeBody(this);
    }
}

float Man::NeedsAmbulance() const
{
    if (_isDead)
    {
        return 0;
    }

    if (GetTotalDammage() <= 0.05f)
    {
        return 0;
    }
    if (GetHitCont(Type()->_handsHit) >= 0.3f)
    {
        return GetHitCont(Type()->_handsHit);
    }
    if (GetHitCont(Type()->_legsHit) >= 0.3f)
    {
        return GetHitCont(Type()->_legsHit);
    }
    if (GetHitCont(Type()->_headHit) >= 0.3f)
    {
        return GetHitCont(Type()->_headHit);
    }
    if (GetHitCont(Type()->_bodyHit) >= 0.3f)
    {
        return GetHitCont(Type()->_bodyHit);
    }
    return GetTotalDammage();
}

float Man::NeedsRepair() const
{
    return 0;
}
float Man::NeedsRefuel() const
{
    return 0;
}
float Man::NeedsInfantryRearm() const
{
    if (IsDammageDestroyed())
    {
        return 0;
    }
    AIUnit* unit = Brain();
    if (!unit)
    {
        return 0;
    }

    const MuzzleType* muzzle1 = nullptr;
    const MuzzleType* muzzle2 = nullptr;
    int slots1 = 0, slots2 = 0, slots3 = 0;
    unit->CheckAmmo(muzzle1, muzzle2, slots1, slots2, slots3);

    int slotsMax = GetItemSlotsCount(GetType()->_weaponSlots);
    if (slotsMax == 0)
    {
        return 0;
    }

    int slotsMaxPri = 0;
    if (muzzle1)
    {
        slotsMaxPri = slotsMax < 4 ? slotsMax : 4;
    }
    int slotsMaxSec = 0;
    if (muzzle2)
    {
        slotsMaxSec = slotsMax - slotsMaxPri;
    }
    int slotsMaxOth = slotsMax - slotsMaxPri - slotsMaxSec;

    return (4.0f * slots1 + 2.0f * slots2 + 1.0f * slots1) /
           (4.0f * slotsMaxPri + 2.0f * slotsMaxSec + 1.0f * slotsMaxOth);
}

void Man::SetDammage(float dammage)
{
    bool wasDead = _isDead;
    base::SetDammage(dammage);
    if (wasDead && !_isDead)
    {
        AIUnit* unit = Brain();
        if (unit)
        {
            unit->SetLifeState(AIUnit::LSAlive);
            ResetMovement(0);
            AIGroup* grp = unit->GetGroup();
            if (grp)
            {
                grp->RessurectUnit(unit);
            }
        }
        else
        {
            _isDead = true;
        }
    }
}

void Man::ReactToDammage()
{
    if (!_isDead)
    {
        if (IsDammageDestroyed())
        {
            _isDead = true;
        }
        if (GetHit(Type()->_headHit) > 0.9f)
        {
            _isDead = true;
        }
        if (GetHit(Type()->_bodyHit) > 0.9f)
        {
            _isDead = true;
        }
    }
    if (_isDead)
    {
        _turnWanted = 0;
    }
}

bool Man::BinocularSelected() const
{
    if (_currentWeapon < 0 || _currentWeapon >= NMagazineSlots())
    {
        return false;
    }
    const MagazineSlot& slot = GetMagazineSlot(_currentWeapon);
    const WeaponType* type = slot._weapon;
    return stricmp(type->GetName(), "binocular") == 0;
}

bool Man::IsHandGunInMove() const
{
    int actPos = GetActUpDegree();
    return (actPos == ManPosHandGunLying || actPos == ManPosHandGunCrouch || actPos == ManPosHandGunStand);
}

bool Man::IsPrimaryWeaponInMove() const
{
    int actPos = GetActUpDegree();
    return (actPos == ManPosLying || actPos == ManPosCrouch || actPos == ManPosCombat);
}

const WeaponModeType* Man::GetCurrentWeaponMode() const
{
    if (_currentWeapon < 0)
    {
        return nullptr;
    }
    const WeaponModeType* mode = GetWeaponMode(_currentWeapon);
    if (mode)
    {
        return mode;
    }
    // fall back to first mode of the first typical magazine (LAW-style weapons have no direct mode)
    const MagazineSlot& slot = GetMagazineSlot(_currentWeapon);
    const MuzzleType* muzzle = slot._muzzle;
    if (!muzzle)
    {
        return nullptr;
    }
    if (muzzle->_magazines.Size() <= 0)
    {
        return nullptr;
    }
    MagazineType* magazine = muzzle->_magazines[0];
    if (!magazine)
    {
        magazine = muzzle->_typicalMagazine;
    }
    if (!magazine)
    {
        return nullptr;
    }
    if (magazine->_modes.Size() <= 0)
    {
        return nullptr;
    }
    return magazine->_modes[0];
}

bool Man::LauncherSelected() const
{
    const WeaponModeType* mode = GetCurrentWeaponMode();
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
    if (mode->_ammo->_simulation == AmmoShotLaser)
    {
        return true;
    }
    return false;
}

bool Man::LauncherWanted() const
{
    AIUnit* unit = _brain;
    if (!unit)
    {
        return false;
    }
    if (unit->GetSemaphore() <= AI::SemaphoreGreen)
    {
        Target* tgt = unit->GetTargetAssigned();
        if (tgt)
        {
            if (tgt->type->_armor > 10)
            {
                return true;
            }
        }
        return false;
    }
    Target* tgt = unit->GetTargetAssigned();
    if (tgt)
    {
        if (tgt->type->_armor > 10)
        {
            return true;
        }
        if (tgt->type->IsKindOf(GWorld->Preloaded(VTypeMan)))
        {
            return false;
        }
    }
    AIGroup* grp = GetGroup();
    if (!grp)
    {
        return false;
    }
    AICenter* center = grp->GetCenter();
    if (!center)
    {
        return false;
    }
    const TargetList& list = grp->GetTargetList();
    for (int i = 0; i < list.Size(); i++)
    {
        Target* tgt = list[i];
        if (tgt->type->_armor < 50)
        {
            continue;
        }
        if (!tgt->IsKnownBy(unit))
        {
            continue;
        }
        if (!center->IsEnemy(tgt->side) && !center->IsUnknown(tgt->side))
        {
            continue;
        }
        return true;
    }
    return false;
}

bool Man::LaserSelected() const
{
    const WeaponModeType* mode = GetCurrentWeaponMode();
    if (!mode)
    {
        return false;
    }
    if (!mode->_ammo)
    {
        return false;
    }
    if (mode->_ammo->_simulation == AmmoShotLaser)
    {
        return true;
    }
    return false;
}

bool Man::LauncherFire() const
{
    PoseidonAssert(!QIsManual());
    if (!LauncherSelected())
    {
        return false;
    }
    if (!_fire._fireTarget || _fire._firePrepareOnly)
    {
        return false;
    }
    return (_fireState == FireAim || _fireState == FireAimed);
}

bool Man::LauncherReady() const
{
    if (!LauncherSelected())
    {
        return false;
    }
    const Magazine* magazine = GetMagazineSlot(_currentWeapon)._magazine;
    if (!magazine || magazine->_ammo == 0)
    {
        return false;
    }
    if (_fire._fireTarget)
    {
        return true;
    }
    return true;
}

void Man::ResetLauncher() {}

void Man::GetRelSpeedRange(float& speedZ, float& minSpd, float& maxSpd)
{
    speedZ = 0;
    minSpd = 0, maxSpd = 1000;
    const MoveInfo* pri = Type()->GetMoveInfo(_primaryMove.id);
    if (pri)
    {
        speedZ = pri->GetSpeed() * -(*pri)->GetStepLength();
        saturateMax(minSpd, pri->GetRelSpeedMin());
        saturateMin(maxSpd, pri->GetRelSpeedMax());
        if (_primaryFactor < 0.99f)
        {
            const MoveInfo* sec = Type()->GetMoveInfo(_secondaryMove.id);
            if (sec)
            {
                float secSpeedZ = sec->GetSpeed() * -(*sec)->GetStepLength();
                speedZ = (1 - _primaryFactor) * secSpeedZ + _primaryFactor * speedZ;
                saturateMax(minSpd, sec->GetRelSpeedMin());
                saturateMin(maxSpd, sec->GetRelSpeedMax());
            }
        }
    }
}

} // namespace Poseidon
