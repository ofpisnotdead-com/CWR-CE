#include <Poseidon/Core/Application.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/Entities/Infantry/Person.hpp>
#include <Poseidon/AI/AIRadio.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>

#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/IO/Serialization/ParamArchive.hpp>
#include <Random/randomGen.hpp>

#include <Poseidon/World/Scene/Camera/CamEffects.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>

#include <Poseidon/Foundation/Algorithms/Qsort.hpp>

#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/Network/Network.hpp>

#include <Poseidon/Foundation/Enums/EnumNames.hpp>
#include <Poseidon/Game/UiActions.hpp>

#include <Poseidon/World/Entities/Infantry/MoveActions.hpp>
#include <float.h>
#include <limits.h>
#include <stdio.h>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/BoolArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

#include <Poseidon/AI/Path/AIDefs.hpp>
#include <Poseidon/Game/Scripting/Scripts.hpp>
#pragma warning(disable : 4355)

namespace Poseidon
{
using namespace Foundation;

// Construction and destruction
AIGroup::AIGroup() : _radio(CCGroup, this, RNRadio)
{
    _id = 0;

    _lastUpdateTime = Glob.time - 1.0F;
    _semaphore = SemaphoreYellow;
    _combatModeMinor = CMSafe; // default auto behaviour

    _nextCmdId = 0;
    _locksWP = 0;

    _enemiesDetected = 0;
    _unknownsDetected = 0;
    _lastEnemyDetected = Glob.time;
    // avoid assigning all groups at once

    _expensiveThinkFrac = toIntFloor(GRandGen.RandomValue() * 1000);
    // perform first expensive think as soon as possible
    _expensiveThinkTime = Glob.time;
    _checkCenterDBase = Glob.time - 60;

    _lastSendTargetTime = Glob.time - 60;

    _disclosed = TIME_MIN;

    _maxStrength = 0;
    _forceCourage = -1;
    _courage = 1.0;
    _flee = false;

    _threshold = 0;
    _thresholdValid = Glob.time - 1.0;

    _nearestEnemyDist2 = 100; // unless we know better, assume 100 m

    _guardPosition = VZero;
}

inline void AIGroup::ExpensiveThinkDone()
{
    // calculate time of next expensive think
    // find start of this second
    // advance to start of next second, advance to our frac
    _expensiveThinkTime = Glob.time.Floor().AddMs(1000 + _expensiveThinkFrac);
}

AIGroup::~AIGroup()
{
    _targetList.Clear();
    if (IsLocal())
    {
        NetworkId ni = GetNetworkId();
        GetNetworkManager().DeleteObject(ni);
    }
}

AICenter* AIGroup::GetCenterS() const
{
    return _center;
}

AIGroup* AIGroup::LoadRef(ParamArchive& ar)
{
    TargetSide side = TSideUnknown;
    int id;
    if (ar.SerializeEnum("side", side, 1) != LSOK)
    {
        return nullptr;
    }
    if (ar.Serialize("id", id, 1) != LSOK)
    {
        return nullptr;
    }
    AICenter* center = nullptr;
    switch (side)
    {
        case TWest:
            center = GWorld->GetWestCenter();
            break;
        case TEast:
            center = GWorld->GetEastCenter();
            break;
        case TGuerrila:
            center = GWorld->GetGuerrilaCenter();
            break;
        case TCivilian:
            center = GWorld->GetCivilianCenter();
            break;
        case TLogic:
            center = GWorld->GetLogicCenter();
            break;
    }
    if (!center)
    {
        return nullptr;
    }
    for (int i = 0; i < center->NGroups(); i++)
    {
        AIGroup* grp = center->GetGroup(i);
        if (grp && grp->ID() == id)
        {
            return grp;
        }
    }
    return nullptr;
}

LSError AIGroup::SaveRef(ParamArchive& ar) const
{
    TargetSide side = GetCenter() ? GetCenter()->GetSide() : TSideUnknown;
    int id = ID();
    PARAM_CHECK(ar.SerializeEnum("side", side, 1))
    PARAM_CHECK(ar.Serialize("id", id, 1))
    return LSOK;
}

LSError AIGroup::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(base::Serialize(ar))

    // structure
    PARAM_CHECK(ar.Serialize("Subgroups", _subgroups, 1))
    PARAM_CHECK(ar.SerializeRef("MainSubgroup", _mainSubgroup, 1))
    PARAM_CHECK(ar.SerializeRef("Leader", _leader, 1))
    PARAM_CHECK(ar.SerializeRef("Center", _center, 1))
    PARAM_CHECK(ar.Serialize("Radio", _radio, 1))

    if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassFirst)
    {
        // build this array in first pass - will be used for refs
        for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
        {
            _units[i] = nullptr;
        }
        for (int i = 0; i < _subgroups.Size(); i++)
        {
            AISubgroup* subgrp = _subgroups[i];
            AI_ERROR(subgrp);
            if (!subgrp)
            {
                continue;
            }
            for (int j = 0; j < subgrp->NUnits(); j++)
            {
                AIUnit* unit = subgrp->GetUnit(j);
                AI_ERROR(unit);
                if (!unit)
                {
                    continue;
                }
                int id = unit->ID();
                AI_ERROR(id > 0 && id <= MAX_UNITS_PER_GROUP);
                _units[id - 1] = unit;
            }
        }
    }

    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        if (_units[i])
        {
            char buf[256];
            snprintf(buf, sizeof(buf), "AssignTarget%d", i);
            PARAM_CHECK(ar.SerializeRef(buf, _assignTarget[i], 1))

            snprintf(buf, sizeof(buf), "AssignTargetState%d", i);
            PARAM_CHECK(ar.SerializeEnum(buf, _assignTargetState[i], 1, TargetEnemyCombat))

            snprintf(buf, sizeof(buf), "assignValidUntil%d", i);
            PARAM_CHECK(ar.Serialize(buf, _assignValidUntil[i], 1, Time(0)))
            snprintf(buf, sizeof(buf), "ammo%d", i);
            PARAM_CHECK(ar.SerializeEnum(buf, _ammoState[i], 1, AIUnit::RSNormal));
            snprintf(buf, sizeof(buf), "health%d", i);
            PARAM_CHECK(ar.SerializeEnum(buf, _healthState[i], 1, AIUnit::RSNormal));
            snprintf(buf, sizeof(buf), "dammage%d", i);
            PARAM_CHECK(ar.SerializeEnum(buf, _dammageState[i], 1, AIUnit::RSNormal));
            snprintf(buf, sizeof(buf), "fuel%d", i);
            PARAM_CHECK(ar.SerializeEnum(buf, _fuelState[i], 1, AIUnit::RSNormal));
            snprintf(buf, sizeof(buf), "down%d", i);
            PARAM_CHECK(ar.Serialize(buf, _reportedDown[i], 1, false));
            snprintf(buf, sizeof(buf), "reporttime%d", i);
            PARAM_CHECK(ar.Serialize(buf, _reportBeforeTime[i], 1, TIME_MAX));
        }
    }

    PARAM_CHECK(ar.Serialize("expensiveThinkFrac", _expensiveThinkFrac, 1, 0));
    PARAM_CHECK(ar.Serialize("expensiveThinkTime", _expensiveThinkTime, 1, Time(0)));
    PARAM_CHECK(ar.Serialize("checkCenterDBase", _checkCenterDBase, 1, Time(0)));
    // state
    PARAM_CHECK(ar.Serialize("lastUpdateTime", _lastUpdateTime, 1))
    PARAM_CHECK(ar.Serialize("checkTime", _checkTime, 1)) // used in seek and destroy waypoint
    PARAM_CHECK(ar.SerializeEnum("semaphore", _semaphore, 1, SemaphoreYellow))
    PARAM_CHECK(ar.SerializeEnum("combatModeMinor", _combatModeMinor, 1, CMSafe))
    PARAM_CHECK(ar.Serialize("lastEnemyDetected", _lastEnemyDetected, 1))
    PARAM_CHECK(ar.Serialize("nextCmdId", _nextCmdId, 2, 0))
    PARAM_CHECK(ar.Serialize("locksWP", _locksWP, 2, 0))

    PARAM_CHECK(ar.Serialize("nearestEnemyDist2", _nearestEnemyDist2, 1, 0));
    PARAM_CHECK(ar.Serialize("enemiesDetected", _enemiesDetected, 1, 0))
    PARAM_CHECK(ar.Serialize("unknownsDetected", _unknownsDetected, 1, 0))

    PARAM_CHECK(ar.Serialize("disclosed", _disclosed, 1))

    // info
    PARAM_CHECK(ar.Serialize("id", _id, 1))

    PARAM_CHECK(ar.Serialize("name", _letterName, 1, ""))
    PARAM_CHECK(ar.Serialize("color", _colorName, 1, ""))
    PARAM_CHECK(ar.Serialize("picture", _pictureName, 1, ""))

    if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassFirst)
    {
        if (_letterName.GetLength() == 0 && _colorName.GetLength() == 0)
        {
            Init();
        }
        else
        {
            SetIdentity(_letterName, _colorName, _pictureName);
        }
    }

    // targets
    PARAM_CHECK(ar.Serialize("targetList", _targetList, 1))

    // assigned vehicles
    PARAM_CHECK(ar.SerializeRefs("Vehicles", _vehicles, 1))

    PARAM_CHECK(ar.SerializeRef("OverlookTarget", _overlookTarget, 1))
    PARAM_CHECK(ar.Serialize("guardPosition", _guardPosition, 1))

    // waypoints
    PARAM_CHECK(ar.Serialize("Waypoints", _wp, 1))

    // flee mode
    PARAM_CHECK(ar.Serialize("maxStrength", _maxStrength, 1))
    PARAM_CHECK(ar.Serialize("forceCourage", _forceCourage, 1))
    PARAM_CHECK(ar.Serialize("courage", _courage, 1))
    PARAM_CHECK(ar.Serialize("flee", _flee, 1, false))

    // threshold for randomization
    PARAM_CHECK(ar.Serialize("threshold", _threshold, 1))
    PARAM_CHECK(ar.Serialize("thresholdValid", _thresholdValid, 1))

    return LSOK;
}

NetworkMessageType AIGroup::GetNMType(NetworkMessageClass cls) const
{
    switch (cls)
    {
        case NMCCreate:
            return NMTCreateAIGroup;
        case NMCUpdateGeneric:
            return NMTUpdateAIGroup;
        default:
            return NMTNone;
    }
}

class IndicesCreateAIGroup : public IndicesNetworkObject
{
    typedef IndicesNetworkObject base;

  public:
    //@{
    int center;
    int id;
    int waypoints;
    //@}

    IndicesCreateAIGroup();
    NetworkMessageIndices* Clone() const override { return new IndicesCreateAIGroup; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesCreateAIGroup::IndicesCreateAIGroup()
{
    center = -1;
    id = -1;
    waypoints = -1;
}

void IndicesCreateAIGroup::Scan(NetworkMessageFormatBase* format)
{
    base::Scan(format);

    SCAN(center)
    SCAN(id)
    SCAN(waypoints)
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesCreateAIGroup()
{
    using namespace Poseidon;
    return new IndicesCreateAIGroup();
}
namespace Poseidon
{

IndicesUpdateAIGroup::IndicesUpdateAIGroup()
{
    mainSubgroup = -1;
    leader = -1;
    semaphore = -1;
    combatModeMinor = -1;
    // ??	_lastEnemyDetected
    // ?? _nextCmdId
    // ?? _locksWP
    enemiesDetected = -1;
    unknownsDetected = -1;
    // ?? disclosed = -1;
    // ?? _vehicles
    // ?? _overlookTarget
    // ?? _guardPosition
    // ?? _maxStrength
    forceCourage = -1;
    courage = -1;
    flee = -1;
    // ?? _threshold
    // ?? _thresholdValid

    waypointIndex = -1;
}

void IndicesUpdateAIGroup::Scan(NetworkMessageFormatBase* format)
{
    base::Scan(format);

    SCAN(mainSubgroup)
    SCAN(leader)
    SCAN(semaphore)
    SCAN(combatModeMinor)
    // ??	_lastEnemyDetected
    // ?? _nextCmdId
    // ?? _locksWP
    SCAN(enemiesDetected)
    SCAN(unknownsDetected)
    // ?? SCAN(disclosed)
    // ?? _vehicles
    // ?? _overlookTarget
    // ?? _guardPosition
    // ?? _maxStrength
    SCAN(forceCourage)
    SCAN(courage)
    SCAN(flee)
    // ?? _threshold
    // ?? _thresholdValid

    SCAN(waypointIndex)
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesUpdateAIGroup()
{
    using namespace Poseidon;
    return new IndicesUpdateAIGroup();
}
namespace Poseidon
{

NetworkMessageFormat& AIGroup::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    switch (cls)
    {
        case NMCCreate:
            NetworkObject::CreateFormat(cls, format);
            format.Add("center", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Superior center"));
            format.Add("id", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Unique identifier in center"));
            format.Add("waypoints", NDTObjectArray, NCTNone, DEFVALUE_MSG(NMTWaypoint), DOC_MSG("List of waypoints"));
            break;
        case NMCUpdateGeneric:
            NetworkObject::CreateFormat(cls, format);
            format.Add("mainSubgroup", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Main subgroup"), ET_NOT_EQUAL,
                       ERR_COEF_STRUCTURE);
            format.Add("leader", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Leader unit"), ET_NOT_EQUAL,
                       ERR_COEF_STRUCTURE);
            format.Add("semaphore", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, SemaphoreYellow),
                       DOC_MSG("Default combat mode"), ET_NOT_EQUAL, ERR_COEF_MODE);
            format.Add("combatModeMinor", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, CMSafe),
                       DOC_MSG("Default behaviour"), ET_NOT_EQUAL, ERR_COEF_MODE);
            // ??	_lastEnemyDetected
            // ?? _nextCmdId
            // ?? _locksWP
            format.Add("enemiesDetected", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0),
                       DOC_MSG("Number of detected enemies"), ET_ABS_DIF, ERR_COEF_VALUE_MINOR);
            format.Add("unknownsDetected", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0),
                       DOC_MSG("Number of detected possible enemies"), ET_ABS_DIF, ERR_COEF_VALUE_MINOR);
            // ?? format.Add("disclosed", NDTTime, NCTNone, DEFVALUE(Time, Time(0)));
            // ?? _vehicles
            // ?? _overlookTarget
            // ?? _guardPosition
            // ?? _maxStrength
            format.Add("forceCourage", NDTFloat, NCTFloatM1ToP1, DEFVALUE(float, -1),
                       DOC_MSG("Enforced (by designer) courage"), ET_ABS_DIF, ERR_COEF_MODE);
            format.Add("courage", NDTFloat, NCTFloat0To1, DEFVALUE(float, 1), DOC_MSG("Calculated courage"), ET_ABS_DIF,
                       ERR_COEF_MODE);
            format.Add("flee", NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Units are fleeing"), ET_NOT_EQUAL,
                       ERR_COEF_MODE);
            // ?? _threshold
            // ?? _thresholdValid

            format.Add("waypointIndex", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0),
                       DOC_MSG("Index of currently processing waypoint"), ET_NOT_EQUAL, ERR_COEF_MODE);
            break;
        default:
            NetworkObject::CreateFormat(cls, format);
            break;
    }
    return format;
}

AIGroup* AIGroup::CreateObject(NetworkMessageContext& ctx)
{
    AI_ERROR(dynamic_cast<const IndicesCreateAIGroup*>(ctx.GetIndices()))
    const IndicesCreateAIGroup* indices = static_cast<const IndicesCreateAIGroup*>(ctx.GetIndices());

    AICenter* center;
    if (ctx.IdxTransferRef(indices->center, center) != TMOK)
    {
        return nullptr;
    }
    if (!center)
    {
        return nullptr;
    }
    int id;
    if (ctx.IdxTransfer(indices->id, id) != TMOK)
    {
        return nullptr;
    }
    AIGroup* grp = new AIGroup();
    center->AddGroup(grp, id);

    NetworkId objectId;
    if (ctx.IdxTransfer(indices->objectCreator, objectId.creator) != TMOK)
    {
        return nullptr;
    }
    if (ctx.IdxTransfer(indices->objectId, objectId.id) != TMOK)
    {
        return nullptr;
    }
    grp->SetNetworkId(objectId);
    grp->SetLocal(false);

    // create waypoints etc.
    grp->TransferMsg(ctx);
    // create mission
    Mission mis;
    mis._action = Mission::Arcade;
    grp->ReceiveMission(mis);
    return grp;
}

void AIGroup::DestroyObject()
{
    if (NUnits() > 0)
    {
        return;
    }

    AICenter* center = _center;
    if (center)
    {
        _center = nullptr;
        center->GroupRemoved(this);
    }
}

void ApplyEffects(AIGroup* group, int index);

TMError AIGroup::TransferMsg(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCCreate:
            TMCHECK(NetworkObject::TransferMsg(ctx))
            {
                AI_ERROR(dynamic_cast<const IndicesCreateAIGroup*>(ctx.GetIndices()))
                const IndicesCreateAIGroup* indices = static_cast<const IndicesCreateAIGroup*>(ctx.GetIndices());

                if (ctx.IsSending())
                {
                    ITRANSF_REF(center)
                    ITRANSF(id)
                }
                TMCHECK(ctx.IdxTransferArray(indices->waypoints, _wp))
                if (!ctx.IsSending())
                {
                    for (int i = 0; i < _wp.Size(); i++)
                    {
                        ArcadeWaypointInfo& wInfo = _wp[i];
                        for (int j = 0; j < wInfo.synchronizations.Size(); j++)
                        {
                            int sync = wInfo.synchronizations[j];
                            AI_ERROR(sync >= 0);
                            if (sync >= synchronized.Size())
                            {
                                synchronized.Resize(sync + 1);
                            }
                            synchronized[sync].Add(this);
                        }
                    }
                }
            }
            break;
        case NMCUpdateGeneric:
            TMCHECK(NetworkObject::TransferMsg(ctx))
            {
                AI_ERROR(dynamic_cast<const IndicesUpdateAIGroup*>(ctx.GetIndices()))
                const IndicesUpdateAIGroup* indices = static_cast<const IndicesUpdateAIGroup*>(ctx.GetIndices());

                if (ctx.IsSending())
                {
                    ITRANSF_REF(mainSubgroup)
                    ITRANSF_REF(leader)
                }
                else
                {
                    AISubgroup* subgrp;
                    TMCHECK(ctx.IdxTransferRef(indices->mainSubgroup, subgrp))
                    if (subgrp != _mainSubgroup)
                    {
                        if (_mainSubgroup)
                        {
                            LOG_DEBUG(AI, "Warning: main subgroup changed");
                        }
                        if (!subgrp)
                        {
                            LOG_DEBUG(AI, "Warning: no main subgroup");
                        }
                        else if (subgrp->GetGroup() != this)
                        {
                            LOG_DEBUG(AI, "Warning: main subgroup not from this group");
                        }
                        else
                        {
                            _mainSubgroup = subgrp;
                        }
                    }
                    AIUnit* leader;
                    TMCHECK(ctx.IdxTransferRef(indices->leader, leader))
                    if (leader != _leader)
                    {
                        if (!leader)
                        {
                            LOG_DEBUG(AI, "Warning: no group leader");
                        }
                        else if (leader->GetGroup() != this)
                        {
                            LOG_DEBUG(AI, "Warning: leader not from this group");
                        }
                        else
                        {
                            _leader = leader;
                        }
                    }
                }
                ITRANSF_ENUM(semaphore)
                ITRANSF_ENUM(combatModeMinor)
                // ??	_lastEnemyDetected
                // ?? _nextCmdId
                // ?? _locksWP
                ITRANSF(enemiesDetected)
                ITRANSF(unknownsDetected)
                // ?? format.Add(indices->disclosed, NDTTime, NCTNone, DEFVALUE(Time, Time(0)));
                // ?? _vehicles
                // ?? _overlookTarget
                // ?? _guardPosition
                // ?? _maxStrength
                ITRANSF(forceCourage)
                ITRANSF(courage)
                ITRANSF(flee)
                // ?? _threshold
                // ?? _thresholdValid
                AI_ERROR(GetCurrent());

                if (ctx.IsSending())
                    TMCHECK(ctx.IdxTransfer(indices->waypointIndex, GetCurrent()->_fsm->Var(0)))
                else
                {
                    int& oldIndex = GetCurrent()->_fsm->Var(0);
                    int newIndex;
                    TMCHECK(ctx.IdxTransfer(indices->waypointIndex, newIndex))
                    if (newIndex != oldIndex)
                    {
                        ApplyEffects(this, oldIndex);
                        oldIndex = newIndex;
                    }
                }
            }
            break;
        default:
            TMCHECK(NetworkObject::TransferMsg(ctx))
            break;
    }
    return TMOK;
}

float AIGroup::CalculateError(NetworkMessageContext& ctx)
{
    float error = NetworkObject::CalculateError(ctx);
    {
        AI_ERROR(dynamic_cast<const IndicesUpdateAIGroup*>(ctx.GetIndices()))
        const IndicesUpdateAIGroup* indices = static_cast<const IndicesUpdateAIGroup*>(ctx.GetIndices());

        ICALCERR_NEQREF(AISubgroup, mainSubgroup, ERR_COEF_STRUCTURE)
        ICALCERR_NEQREF(AIUnit, leader, ERR_COEF_STRUCTURE)
        ICALCERR_NEQ(int, semaphore, ERR_COEF_MODE)
        ICALCERR_NEQ(int, combatModeMinor, ERR_COEF_MODE)
        ICALCERR_ABSDIF(int, enemiesDetected, ERR_COEF_VALUE_MINOR)
        ICALCERR_ABSDIF(int, unknownsDetected, ERR_COEF_VALUE_MINOR)
        ICALCERR_ABSDIF(float, forceCourage, ERR_COEF_MODE)
        ICALCERR_ABSDIF(float, courage, ERR_COEF_MODE)
        ICALCERR_NEQ(bool, flee, ERR_COEF_MODE)
        ICALCERRE_NEQ(int, waypointIndex, GetCurrent()->_fsm->Var(0), ERR_COEF_MODE)
    }
    return error;
}

int AIGroup::NUnits() const
{
    int n = 0;
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* unit = _units[i];
        if (unit)
        {
            n++;
        }
    }
    return n;
}

void AIGroup::SetUnit(int index, AIUnit* unit)
{
    // low level function used for network transfer
    // !!! avoid another using
    _units[index] = unit;
    InitUnitSlot(index);
}

int AIGroup::NSoldiers() const
{
    int n = 0;
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* unit = _units[i];
        if (unit && unit->IsFreeSoldier() && !unit->_vehicleAssigned)
        {
            n++;
        }
    }
    return n;
}

int AIGroup::NFreeVehicles() const
{
    int n = 0;
    for (int i = 0; i < _vehicles.Size(); i++)
    {
        Transport* veh = _vehicles[i];
        if (veh && !veh->GetDriverAssigned())
        {
            n++;
        }
    }
    return n;
}

int AIGroup::NFreeManCargo() const
{
    int n = 0;
    for (int i = 0; i < _vehicles.Size(); i++)
    {
        Transport* veh = _vehicles[i];
        if (veh)
        {
            n += veh->GetMaxManCargo() - veh->NCargoAssigned();
        }
    }
    return n;
}

void AIGroup::SetCombatModeMajor(CombatMode mode)
{
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* unit = _units[i];
        if (unit)
        {
            unit->SetCombatModeMajor(mode);
        }
    }
}

void AIGroup::LockWP(bool lock)
{
    if (lock)
    {
        _locksWP++;
    }
    else
    {
        _locksWP--;
    }
}

void AIGroup::Disclose(AIUnit* sender)
{
    if (IsPlayerGroup())
    {
        return;
    }
    if (!sender)
    {
        sender = Leader();
    }

    if (!sender->IsLocal())
    {
        return;
    }
    if (sender->GetLifeState() != AIUnit::LSAlive)
    {
        return;
    }

    _disclosed = Glob.time;

    if (!_flee)
    {
        // note: combatModeMinor is never CMCareless (it is autodetected)
        // combatModeMajor may be - but this is member of leader
        if (_combatModeMinor < CMCombat)
        {
            _combatModeMinor = CMCombat;
            AIUnit* unit = Leader();
            if (unit && unit->GetLifeState() == AIUnit::LSAlive && unit->GetCombatModeMajor() != CMCareless)
            {
                SendUnderFire(sender);

                // unload all units in car cargo position
                for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
                {
                    AIUnit* unit = _units[i];
                    if (!unit)
                    {
                        continue;
                    }
                    // going to combat - get out of some vehicles
                    if (unit->IsInCargo())
                    {
                        Transport* trans = unit->GetVehicleIn();
                        if (trans && trans->Type()->GetUnloadInCombat())
                        {
                            unit->OrderGetIn(false);
                        }
                    }
                }
            }
        }
        if (_semaphore != SemaphoreBlue)
        {
            _semaphore = ApplyOpenFire(_semaphore, OFSOpenFire);
            PackedBoolArray list;
            for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
            {
                AIUnit* unit = _units[i];
                if (unit)
                {
                    list.Set(i, true);
                }
            }
            SendOpenFire(OFSOpenFire, list);
        }
    }
}

// Changes of structure
void AIGroup::SubgroupRemoved(AISubgroup* subgrp)
{
    AI_ERROR(subgrp->NUnits() == 0);
    AI_ERROR(subgrp != MainSubgroup());

    int index = _subgroups.Find(subgrp);
    AI_ERROR(index >= 0);
    if (index < 0)
    {
        return;
    }

    _subgroups.Delete(index);
}

void AIGroup::UnitRemoved(AIUnit* unit)
{
    AI_ERROR(unit->GetGroup() == this);
    bool bLeader = (unit == Leader());

    int id = unit->ID() - 1;
    AI_ERROR(_units[id] == unit);
    _units[id] = nullptr;
    unit->GetSubgroup()->UnitRemoved(unit);

    if (NUnits() == 0)
    {
        _leader = nullptr;
        return;
    }
    if (bLeader)
    {
        GetCenter()->SelectLeader(this);
    }
}

void AIGroup::AddSubgroup(AISubgroup* subgrp)
{
    AI_ERROR(!subgrp->GetGroup());

    // add to group
    int index = _subgroups.Find(nullptr);
    if (index >= 0)
    {
        _subgroups[index] = subgrp;
    }
    else
    {
        _subgroups.Add(subgrp);
    }
    subgrp->_group = this;
}

void AIGroup::CopyUnitSlot(int from, int to)
{
    _assignTarget[to] = _assignTarget[from];
    _healthState[to] = _healthState[from];
    _ammoState[to] = _ammoState[from];
    _fuelState[to] = _fuelState[from];
    _dammageState[to] = _dammageState[from];
    _reportedDown[to] = _reportedDown[from];
    _reportBeforeTime[to] = _reportBeforeTime[from];
    _assignTargetState[to] = _assignTargetState[from];
}

void AIGroup::InitUnitSlot(int i)
{
    _assignTarget[i] = nullptr;
    _healthState[i] = AIUnit::RSNormal;
    _ammoState[i] = AIUnit::RSNormal;
    _fuelState[i] = AIUnit::RSNormal;
    _dammageState[i] = AIUnit::RSNormal;
    _reportedDown[i] = false;
    _reportBeforeTime[i] = TIME_MAX;
    _assignTargetState[i] = TargetEnemyCombat;
}

void AIGroup::RessurectUnit(AIUnit* unit)
{
    int i = unit->ID() - 1;
    _healthState[i] = AIUnit::RSNormal;
    _dammageState[i] = AIUnit::RSNormal;
    _reportedDown[i] = false;
    _reportBeforeTime[i] = TIME_MAX;
}

void AIGroup::AddUnit(AIUnit* unit, int id)
{
    if (!_mainSubgroup)
    {
        _mainSubgroup = new AISubgroup();
        AddSubgroup(_mainSubgroup);
    }
    if (unit->ID() != 0)
    {
        int i = unit->ID() - 1;
        if (_units[i] && _units[i] != unit)
        {
            Fail("Unit ID mismatch");
        }
        else
        {
            _units[i] = unit;
            InitUnitSlot(i);
            _mainSubgroup->AddUnit(unit);
        }
        return;
    }

    if (id > 0)
    {
        Ref<AIUnit> removed = (AIUnit*)_units[id - 1];
        _units[id - 1] = unit;

        unit->_id = id;
        _mainSubgroup->AddUnit(unit);
        if (removed)
        {
            for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
            {
                if (!_units[i])
                {
                    _units[i] = removed.GetRef();
                    removed->_id = i + 1;
                    // move old information together with the unit
                    CopyUnitSlot(id - 1, i);
                    // init information for the new unit
                    InitUnitSlot(id - 1);

                    return;
                }
            }
            Fail("Cannot find id for unit");
        }
        else
        {
            InitUnitSlot(id - 1);
        }
    }
    else
    {
        for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
        {
            if (!_units[i])
            {
                _units[i] = unit;
                InitUnitSlot(i);
                unit->_id = i + 1;
                _mainSubgroup->AddUnit(unit);
                return;
            }
        }
        Fail("Cannot find id for unit");
        _mainSubgroup->AddUnit(unit);
    }
}

void AIGroup::AddVehicle(Transport* veh)
{
    // delete free positions
    _vehicles.Compact();
    // try to find
    if (_vehicles.Find(veh) >= 0)
    {
        return;
    }

    // assign
    veh->AssignGroup(this);

    // add to array sorted by cost
    int i;
    for (i = 0; i < _vehicles.Size(); i++)
    {
        Transport* trans = _vehicles[i];
        if (veh->GetType()->GetCost() >= trans->GetType()->GetCost())
        {
            _vehicles.Insert(i, veh);
            return;
        }
    }
    _vehicles.Add(veh);
}

AIUnit* AIGroup::LeaderCandidate(AIUnit* dead) const
{
    // implementation of leader selection for group
    AIUnit* unit = nullptr;
    Rank bestRank = RankPrivate;
    float bestExp = -FLT_MAX;
    // find unit with the highest rank
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* u = UnitWithID(i + 1);
        if (u && u != dead)
        {
            Rank rank = u->GetPerson()->GetRank();
            if (rank > bestRank)
            {
                bestRank = rank;
                bestExp = u->GetPerson()->GetExperience();
                unit = u;
            }
            else if (rank == bestRank)
            {
                float exp = u->GetPerson()->GetExperience();
                if (exp > bestExp)
                {
                    bestExp = exp;
                    unit = u;
                }
            }
        }
    }

    return unit;
}

} // namespace Poseidon
void AIGroup::SelectLeader(AIUnit* unit)
{
    using namespace Poseidon;
    AI_ERROR(unit);
    AI_ERROR(unit->GetSubgroup()) AI_ERROR(unit->GetSubgroup()->GetGroup() == this);

    if (_leader != unit)
    {
        _leader = unit;
        CalculateCourage();

        AISubgroup* subgrp = unit->GetSubgroup();
        subgrp->SelectLeader();

        if (unit == GWorld->FocusOn())
        {
            subgrp->ClearAllCommands();
            if (subgrp != MainSubgroup() && MainSubgroup())
            {
                MainSubgroup()->ClearAllCommands();
            }
        }
    }
}
namespace Poseidon
{

static int CompUnits(const OLink<AIUnit>* unit1, const OLink<AIUnit>* unit2)
{
    // check existence
    AIUnit* u1 = unit1->GetLink();
    if (!u1)
    {
        return 1;
    }
    AIUnit* u2 = unit2->GetLink();
    if (!u2)
    {
        return -1;
    }

    // check rank
    int rank1 = u1->GetPerson()->GetRank();
    int rank2 = u2->GetPerson()->GetRank();
    int diff = rank2 - rank1;
    if (diff != 0)
    {
        return diff;
    }

    // check function
    int pos1 = 1;
    int pos2 = 1;
    Transport* v1 = u1->GetVehicleIn();
    Transport* v2 = u2->GetVehicleIn();
    if (v1)
    {
        if (v1->CommanderBrain() == u1)
        {
            pos1 = 2;
        }
        else if (v1->GunnerBrain() == u1)
        {
            pos1 = 0;
        }
    }
    if (v2)
    {
        if (v2->CommanderBrain() == u2)
        {
            pos2 = 2;
        }
        else if (v2->GunnerBrain() == u2)
        {
            pos2 = 0;
        }
    }
    diff = pos2 - pos1;
    if (diff != 0)
    {
        return diff;
    }

    diff = u2->IsGroupLeader() - u1->IsGroupLeader();
    if (diff)
    {
        return diff;
    }
    // other cases
    return u1->ID() - u2->ID();
}

void AIGroup::SortUnits()
{
    QSort(_units, MAX_UNITS_PER_GROUP, CompUnits);

    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* unit = _units[i];
        if (unit)
        {
            unit->_id = i + 1;
        }
    }
}

bool AIGroup::IsPlayerGroup() const
{
    return Leader() && Leader()->IsPlayer();
}

bool AIGroup::IsAnyPlayerGroup() const
{
    // note: currently group is always local where leader is local
    // therefore IsAnyPlayerGroup called on local group
    // should always be identical to IsPlayerGroup
    return Leader() && Leader()->IsAnyPlayer();
}

bool AIGroup::IsCameraGroup() const
{
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        if (!_units[i])
        {
            continue;
        }
        if (_units[i]->GetVehicle() == GWorld->CameraOn())
        {
            return true;
        }
    }
    return false;
}

// Mind
bool AIGroup::Think()
{
#if LOG_THINK
    Log("Group %s think.", (const char*)GetDebugName());
#endif

    if (!Leader())
    {
        PoseidonAssert(NUnits() == 0);
        return false;
    }

    if (GetCenter()->GetSide() == TLogic)
    {
        if (IsLocal())
        {
            DoUpdate();
        }
        return false;
    }

    if (!IsLocal())
    {
        // only update target list
        if (Glob.time > _expensiveThinkTime)
        {
            ExpensiveThinkDone();
            CreateTargetList();
        }
        return false;
    }

    bool leaderAlive = Leader()->GetLifeState() == AIUnit::LSAlive;

    if (IsAnyPlayerGroup())
    {
        if (_flee)
        {
            Unflee();
        }
    }
    else
    {
        // flee only when leader is alive
        if (leaderAlive)
        {
            float strength = ActualStrength();
            if (strength < (1.0 - _courage) * _maxStrength)
            {
                if (!_flee)
                {
                    Flee();
                }
            }
            else
            {
                if (_flee)
                {
                    Unflee();
                }
            }
        }
    }

    // check if state in state graph not changed
    {
        Ref<AIGroup> safePtr = this;
        DoUpdate();
        AI_ERROR(safePtr->RefCounter() > 0);
        if (safePtr->RefCounter() == 1)
        {
            return false; // group destroyed
        }

        if (!Leader())
        {
            PoseidonAssert(NUnits() == 0);
            return false;
        }
    }

    AISubgroup* leaderSubgroup = Leader()->GetSubgroup();
    if (leaderSubgroup != MainSubgroup() && !leaderSubgroup->HasCommand())
    {
        leaderSubgroup->JoinToSubgroup(MainSubgroup());
    }

    if (!IsAnyPlayerGroup())
    {
        // do not join while scripted waypoint
        if (!GetScript())
        {
            // if there are subgroups in wait state, join them
            for (int i = 0; i < _subgroups.Size(); i++)
            {
                AISubgroup* sub = _subgroups[i];
                if (!sub)
                {
                    continue;
                }
                if (sub == MainSubgroup())
                {
                    continue;
                }
                if (sub->HasCommand())
                {
                    continue;
                }
                sub->JoinToSubgroup(MainSubgroup());
            }
        }
    }

    // if some enemy is near, we have to track near vincity continuously
    if (_nearestEnemyDist2 < Square(100))
    {
        // force all units to track if necessary
        for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
        {
            AIUnit* unit = _units[i];
            if (!unit)
            {
                continue;
            }
            if (!unit->IsLocal())
            {
                continue;
            }
            if (!unit->IsUnit())
            {
                continue;
            }
            if (unit->GetLifeState() != AIUnit::LSAlive)
            {
                continue;
            }
            if (unit->GetNearestEnemyDist2() < Square(100))
            {
                unit->GetVehicle()->TrackNearTargets(_targetList);
            }
        }
    }

    // visible enemy targets
    // if we have fresh assignement, do not assign anything
    if (Glob.time > _expensiveThinkTime)
    {
        // many functions need not be performed in every thing
        // once per second is enough for them

        ExpensiveThinkDone();

        int oldED = _enemiesDetected;
        CreateTargetList();

        if (oldED == 0 && _enemiesDetected > 0 && !IsAnyPlayerGroup())
        {
            ReactToEnemyDetected();
        }

        if (leaderAlive)
        {
            //  min delay betweeen targets
            float minDelay = Leader()->GetInvAbility() * 5;
            if (Glob.time > _lastSendTargetTime + minDelay)
            {
                AssignTargets();
            }
        }

        if (_enemiesDetected > 0)
        {
            _lastEnemyDetected = Glob.time + GRandGen.PlusMinus(30.0f, 5.0f);
        }

        // try to assign unassigned vehicles
        if (!IsAnyPlayerGroup() && leaderAlive)
        {
            AssignVehicles();
            GetInVehicles();

            // check resources
            bool ok = true;
            if (!CheckFuel())
            {
                ok = false;
            }
            if (!CheckArmor())
            {
                ok = false;
            }
            if (!CheckHealth())
            {
                ok = false;
            }
            if (!CheckAmmo())
            {
                ok = false;
            }

            if (ok)
            {
                AI_ERROR(_center);
                if (_center->GetRadio().Done() && _center->IsSupported(this, ATNone))
                {
                    _center->GetRadio().Transmit(new RadioMessageSupportDone(this), _center->GetLanguage());
                }
            }
        }

        // check if support needed
        CheckSupport();

        CheckAlive(); // check if unseen units are alive
    }

    if (!IsAnyPlayerGroup() && leaderAlive)
    {
        // autodetection only for non-player groups
        if (_combatModeMinor >= CMAware)
        {
            float timeFromCombat = Glob.time - _lastEnemyDetected;
            if (_disclosed > Glob.time - 10 * 60)
            {
                saturateMin(timeFromCombat, Glob.time - (_disclosed + 15.0f));
            }
            if (timeFromCombat > 0)
            {
                if (timeFromCombat > 5 * 60)
                {
                    _combatModeMinor = CMSafe;
                }
                else
                {
                    if (_combatModeMinor >= CMCombat)
                    {
                        _combatModeMinor = CMAware;
                        SendClear(Leader());
                    }
                }
            }
        }
    }

    bool someOperPath = false;
    for (int i = 0; i < NSubgroups(); i++)
    {
        // do not destroy subgroup during Think
        Ref<AISubgroup> subgrp = GetSubgroup(i);
        ThinkImportance prec = subgrp->CalculateImportance();
        if (subgrp->Think(prec))
        {
            someOperPath = true;
        }
    }

#if LOG_THINK
    Log("End of group %s think (%d).", (const char*)GetDebugName(), someOperPath);
#endif
    return someOperPath;
}

// Communication with subgroups
void CreateUnitsList(PackedBoolArray list, char* buffer);

void AIGroup::SendCommand(Command& cmd, bool channelCenter)
{
    cmd._id = _nextCmdId++;

    AI_ERROR(Leader());
#if LOG_COMM
    Log("Send command: Main subgroup of %s: Command %d (context %d)", (const char*)GetDebugName(), cmd._message,
        cmd._context);
#endif

    AIUnit* leader = Leader();
    if (!leader)
    {
        return;
    }

    // if leader is not alive, he should not be able to send commands
    if (leader && leader->GetLifeState() != AIUnit::LSAlive)
    {
        LOG_DEBUG(AI, "No command, leader {} is not alive", (const char*)leader->GetDebugName());
        return;
    }
    bool transmit = true;
    if (leader->GetSubgroup() == MainSubgroup())
    {
        MainSubgroup()->ReceiveCommand(cmd);
        transmit = false;
    }

    // do not say if its near
    if (!transmit)
    {
        AIUnit* subLeader = MainSubgroup()->Leader();
        if (subLeader && subLeader->Position().Distance2(cmd._destination) < Square(6))
        {
            return;
        }
    }

    AI_ERROR(GetCenter());
    RadioChannel& radio = channelCenter ? GetCenter()->GetRadio() : GetRadio();
    PackedBoolArray list;
    radio.Transmit(new RadioMessageCommand(this, list, cmd, true, transmit), GetCenter()->GetLanguage());
}

void AIGroup::SendCommand(Command& cmd, PackedBoolArray list, bool channelCenter)
{
    cmd._id = _nextCmdId++;

    AI_ERROR(Leader());

#if LOG_COMM
    char buffer[256];
    CreateUnitsList(list, buffer);
    Log("Send command: units %s of %s: Command %d (context %d)", buffer, (const char*)GetDebugName(), cmd._message,
        cmd._context);
#endif

    AIUnit* leader = Leader();
    // if leader is not alive, he should not be able to send commands
    if (leader && leader->GetLifeState() != AIUnit::LSAlive)
    {
        LOG_DEBUG(AI, "SendCommand: No command, leader {} is not alive", (const char*)leader->GetDebugName());
        return;
    }

    AI_ERROR(GetCenter());
    RadioChannel& radio = channelCenter ? GetCenter()->GetRadio() : GetRadio();

    int i;
    for (i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        if (!list.Get(i))
        {
            continue;
        }
        AIUnit* unit = UnitWithID(i + 1);
        if (!unit)
        {
            list.Set(i, false);
            continue;
        }

        if (cmd._message == Command::GetIn || cmd._message == Command::GetOut)
        {
            // check radio channel
            int index = INT_MAX;
            while (true)
            {
                RadioMessage* msg = radio.GetPrevMessage(index);
                if (!msg)
                {
                    break;
                }
                if (msg->GetType() == RMTCommand)
                {
                    AI_ERROR(dynamic_cast<RadioMessageCommand*>(msg));
                    RadioMessageCommand* msgCmd = static_cast<RadioMessageCommand*>(msg);
                    AI_ERROR(msgCmd->GetFrom() == this);
                    if (msgCmd->GetCmdMessage() == Command::GetIn && msgCmd->IsTo(unit))
                    {
                        msgCmd->DeleteTo(unit);
                        goto SendCommandForContinue;
                    }
                    if (msgCmd->GetCmdMessage() == Command::GetOut && msgCmd->IsTo(unit))
                    {
                        msgCmd->DeleteTo(unit);
                        goto SendCommandForContinue;
                    }
                }
            }
        }

    SendCommandForContinue:
        continue;
    }

    if (list[leader->ID() - 1])
    {
        // try to process command directly
        if (cmd._message == Command::GetIn || cmd._message == Command::GetOut)
        {
            list.Set(leader->ID() - 1, false);
            PackedBoolArray leaderList;
            leaderList.Set(leader->ID() - 1, true);
            IssueCommand(cmd, leaderList);
        }
        else
        {
            IssueCommand(cmd, list);
            return;
        }
    }

    if (!list.IsEmpty())
    {
        radio.Transmit(new RadioMessageCommand(this, list, cmd), GetCenter()->GetLanguage());
    }
}

Script* AIGroup::GetScript() const
{
    return _script;
}
void AIGroup::SetScript(Script* script)
{
    _script = script;
}
} // namespace Poseidon
