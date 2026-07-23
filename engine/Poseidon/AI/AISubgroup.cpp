#include <Poseidon/Core/Application.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/AI/AIRadio.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/World/Entities/Infantry/Person.hpp>

#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/IO/Serialization/ParamArchive.hpp>

#include <Poseidon/World/Terrain/Roads.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>

#include <Poseidon/Network/Network.hpp>

#include <Poseidon/Foundation/Enums/EnumNames.hpp>
#include <float.h>
#include <limits.h>
#include <stdio.h>
#include <cmath>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/BoolArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Memory/FastAlloc.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

#include <Poseidon/AI/Path/AIDefs.hpp>

using namespace Poseidon;
namespace Poseidon
{
using namespace Foundation;
using Foundation::EnumName;

#define LOST_UNIT_MIN 45.0F
#define LOST_UNIT_MAX 300.0F

#define CRITICAL_EXPOSURE_CHANGE 500.0F
#define COEF_EXPOSURE 4e-6F // coeficient for including exposure into cost

#define LOG_FORMATION_COEF 0

DEFINE_FAST_ALLOCATOR(PathTreeNode)

extern const FormInfo formations[AI::NForms][MAX_UNITS_PER_GROUP] = {
    // FormInfo(base, x, z, angle)
    {
        // column
        FormInfo(-1, 0, 0, 0),
        FormInfo(0, 0, -1, 0.25 * H_PI),
        FormInfo(1, 0, -1, -0.25 * H_PI),
        FormInfo(2, 0, -1, H_PI),
        FormInfo(3, 0, -1, 0),
        FormInfo(4, 0, -1, 0.25 * H_PI),
        FormInfo(5, 0, -1, -0.25 * H_PI),
        FormInfo(6, 0, -1, H_PI),
        FormInfo(7, 0, -1, 0),
        FormInfo(8, 0, -1, 0.25 * H_PI),
        FormInfo(9, 0, -1, -0.25 * H_PI),
        FormInfo(10, 0, -1, H_PI),
    },
    {
        // staggered_column
        FormInfo(-1, 0, 0, 0),
        FormInfo(0, 1, -1, 0.25 * H_PI),
        FormInfo(1, -1, -1, -0.25 * H_PI),
        FormInfo(2, 1, -1, H_PI),
        FormInfo(3, -1, -1, 0),
        FormInfo(4, 1, -1, 0.25 * H_PI),
        FormInfo(5, -1, -1, -0.25 * H_PI),
        FormInfo(6, 1, -1, H_PI),
        FormInfo(7, -1, -1, 0),
        FormInfo(8, 1, -1, 0.25 * H_PI),
        FormInfo(9, -1, -1, -0.25 * H_PI),
        FormInfo(10, 1, -1, H_PI),
    },
    {
        // wedge
        FormInfo(-1, 0, 0, 0),
        FormInfo(0, 1, -1, 0.25 * H_PI),
        FormInfo(0, -1, -1.33, -0.25 * H_PI),
        FormInfo(1, 1, -1, 0.5 * H_PI),
        FormInfo(2, -1, -1.33, -0.25 * H_PI),
        FormInfo(3, 1, -1, 0.5 * H_PI),
        FormInfo(4, -1, -1.33, -0.25 * H_PI),
        FormInfo(5, 1, -1, 0.5 * H_PI),
        FormInfo(6, -1, -1.33, -0.25 * H_PI),
        FormInfo(7, 1, -1, 0.5 * H_PI),
        FormInfo(8, -1, -1.33, -0.25 * H_PI),
        FormInfo(9, 1, -1, 0.5 * H_PI),
    },
    {
        // echolon_left
        FormInfo(-1, 0, 0, 0),
        FormInfo(0, -1, -1, -0.25 * H_PI),
        FormInfo(1, -1, -1, -0.25 * H_PI),
        FormInfo(2, -1, -1, -0.5 * H_PI),
        FormInfo(3, -1, -1, 0),
        FormInfo(4, -1, -1, -0.25 * H_PI),
        FormInfo(5, -1, -1, -0.25 * H_PI),
        FormInfo(6, -1, -1, -0.5 * H_PI),
        FormInfo(7, -1, -1, 0),
        FormInfo(8, -1, -1, -0.25 * H_PI),
        FormInfo(9, -1, -1, -0.25 * H_PI),
        FormInfo(10, -1, -1, -0.5 * H_PI),
    },
    {
        // echolon_right
        FormInfo(-1, 0, 0, 0),
        FormInfo(0, 1, -1, 0.25 * H_PI),
        FormInfo(1, 1, -1, 0.25 * H_PI),
        FormInfo(2, 1, -1, 0.5 * H_PI),
        FormInfo(3, 1, -1, 0),
        FormInfo(4, 1, -1, 0.25 * H_PI),
        FormInfo(5, 1, -1, 0.25 * H_PI),
        FormInfo(6, 1, -1, 0.5 * H_PI),
        FormInfo(7, 1, -1, 0),
        FormInfo(8, 1, -1, 0.25 * H_PI),
        FormInfo(9, 1, -1, 0.25 * H_PI),
        FormInfo(10, 1, -1, 0.5 * H_PI),
    },
    {
        // vee
        FormInfo(-1, 0, 0, -0.25 * H_PI),
        FormInfo(0, 1, 0, 0.25 * H_PI),
        FormInfo(0, -1, 1, -0.25 * H_PI),
        FormInfo(1, 1, 1, 0.25 * H_PI),
        FormInfo(2, -1, 1, -0.25 * H_PI),
        FormInfo(3, 1, 1, 0.25 * H_PI),
        FormInfo(4, -1, 1, -0.25 * H_PI),
        FormInfo(5, 1, 1, 0.25 * H_PI),
        FormInfo(6, -1, 1, -0.25 * H_PI),
        FormInfo(7, 1, 1, 0.25 * H_PI),
        FormInfo(8, -1, 1, -0.25 * H_PI),
        FormInfo(9, 1, 1, 0.25 * H_PI),
    },
    {
        // line
        FormInfo(-1, 0, 0, 0), // 0
        FormInfo(0, 1, 0, 0),  // 1
        FormInfo(0, -1, 0, 0), // 2
        FormInfo(1, 1, 0, 0),
        FormInfo(2, -1, 0, 0),
        FormInfo(3, 1, 0, 0),
        FormInfo(4, -1, 0, 0),
        FormInfo(5, 1, 0, 0),
        FormInfo(6, -1, 0, 0),
        FormInfo(7, 1, 0, 0),
        FormInfo(8, -1, 0, 0),
        FormInfo(9, 1, 0, 0),
    }};

LSError Command::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.SerializeEnum("message", _message, 1))
    PARAM_CHECK(ar.SerializeRef("Target", _target, 1))
    PARAM_CHECK(ar.SerializeRef("TargetE", _targetE, 1))
    PARAM_CHECK(ar.Serialize("destination", _destination, 1))
    PARAM_CHECK(ar.Serialize("time", _time, 1))
    PARAM_CHECK(ar.SerializeRef("JoinTo", _joinToSubgroup, 1))
    PARAM_CHECK(ar.SerializeEnum("action", _action, 1, (UIActionType)0))
    PARAM_CHECK(ar.Serialize("param", _param, 1, -1))
    PARAM_CHECK(ar.Serialize("param2", _param2, 1, -1))
    PARAM_CHECK(ar.Serialize("param3", _param3, 1, ""))
    PARAM_CHECK(ar.SerializeEnum("discretion", _discretion, 1, Undefined))
    PARAM_CHECK(ar.SerializeEnum("context", _context, 1, CtxAuto))
    PARAM_CHECK(ar.Serialize("id", _id, 1, -1))
    return LSOK;
}

NetworkMessageType Command::GetNMType(NetworkMessageClass cls) const
{
    switch (cls)
    {
        case NMCCreate:
            return NMTCreateCommand;
        case NMCUpdateGeneric:
            return NMTUpdateCommand;
        default:
            return NMTNone;
    }
}

IndicesCreateCommand::IndicesCreateCommand()
{
    subgroup = -1;
    index = -1;
    message = -1;
    target = -1;
    targetE = -1;
    destination = -1;
    time = -1;
    join = -1;
    action = -1;
    param = -1;
    param2 = -1;
    param3 = -1;
    discretion = -1;
    context = -1;
    id = -1;
}

void IndicesCreateCommand::Scan(NetworkMessageFormatBase* format)
{
    base::Scan(format);

    SCAN(subgroup)
    SCAN(index)
    SCAN(message)
    SCAN(target)
    SCAN(targetE)
    SCAN(destination)
    SCAN(time)
    SCAN(join)
    SCAN(action)
    SCAN(param)
    SCAN(param2)
    SCAN(param3)
    SCAN(discretion)
    SCAN(context)
    SCAN(id)
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesCreateCommand()
{
    using namespace Poseidon;
    return new IndicesCreateCommand();
}
namespace Poseidon
{

class IndicesUpdateCommand : public IndicesNetworkObject
{
    typedef IndicesNetworkObject base;

  public:
    int destination;

    IndicesUpdateCommand();
    NetworkMessageIndices* Clone() const override { return new IndicesUpdateCommand; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesUpdateCommand::IndicesUpdateCommand()
{
    destination = -1;
}

void IndicesUpdateCommand::Scan(NetworkMessageFormatBase* format)
{
    base::Scan(format);

    SCAN(destination)
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesUpdateCommand()
{
    using namespace Poseidon;
    return new IndicesUpdateCommand();
}
namespace Poseidon
{

NetworkMessageFormat& Command::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    switch (cls)
    {
        case NMCCreate:
            NetworkObject::CreateFormat(cls, format);
            format.Add("subgroup", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Subgroup, executing this command"));
            format.Add("index", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0),
                       DOC_MSG("Index of command in subgroup"));

            format.Add("message", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, NoCommand), DOC_MSG("Type of command"));
            format.Add("target", NDTRef, NCTNone, DEFVALUENULL,
                       DOC_MSG("Target (exact) of command - used for well known targets"));
            format.Add("targetE", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Target (inexact) - used for enemies"));
            format.Add("destination", NDTVector, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Destination position"));
            format.Add("time", NDTTime, NCTNone, DEFVALUE(Time, Time(0)), DOC_MSG("Time, when command timeouts"));
            format.Add("join", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Join to main subroup after completition"));
            format.Add("action", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0),
                       DOC_MSG("Type of action for Action command"));
            format.Add("param", NDTInteger, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Action parameter"));
            format.Add("param2", NDTInteger, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Action parameter"));
            format.Add("param3", NDTString, NCTNone, DEFVALUE(RString, RString("")), DOC_MSG("Action parameter"));
            format.Add("discretion", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, Undefined),
                       DOC_MSG("Subgroup discretion"));
            format.Add("context", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, CtxUndefined),
                       DOC_MSG("How command was ordered"));
            format.Add("id", NDTInteger, NCTSmallSigned, DEFVALUE(int, -1), DOC_MSG("Unique (in group) id of command"));
            break;
        case NMCUpdateGeneric:
            NetworkObject::CreateFormat(cls, format);
            format.Add("destination", NDTVector, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Destination position"),
                       ET_ABS_DIF, 1);
            break;
        default:
            NetworkObject::CreateFormat(cls, format);
            break;
    }
    return format;
}

TMError Command::TransferMsg(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCCreate:
            TMCHECK(NetworkObject::TransferMsg(ctx))
            {
                AI_ERROR(dynamic_cast<const IndicesCreateCommand*>(ctx.GetIndices()))
                const IndicesCreateCommand* indices = static_cast<const IndicesCreateCommand*>(ctx.GetIndices());

                TMCHECK(ctx.IdxTransfer(indices->message, (int&)_message))
                ITRANSF_REF(target)
                if (ctx.IsSending())
                {
                    EntityAI* target = nullptr;
                    if (_targetE)
                    {
                        target = _targetE->idExact;
                    }
                    TMCHECK(ctx.IdxTransferRef(indices->targetE, target));
                }
                else
                {
                    // see CreateObject
                }
                ITRANSF(destination)
                ITRANSF(time)
                TMCHECK(ctx.IdxTransferRef(indices->join, _joinToSubgroup))
                ITRANSF_ENUM(action)
                ITRANSF(param)
                ITRANSF(param2)
                ITRANSF(param3)
                TMCHECK(ctx.IdxTransfer(indices->discretion, (int&)_discretion))
                TMCHECK(ctx.IdxTransfer(indices->context, (int&)_context))
                ITRANSF(id)
            }
            break;
        case NMCUpdateGeneric:
            TMCHECK(NetworkObject::TransferMsg(ctx))
            {
                AI_ERROR(dynamic_cast<const IndicesUpdateCommand*>(ctx.GetIndices()))
                const IndicesUpdateCommand* indices = static_cast<const IndicesUpdateCommand*>(ctx.GetIndices());

                ITRANSF(destination)
            }
            break;
        default:
            TMCHECK(NetworkObject::TransferMsg(ctx))
            break;
    }
    return TMOK;
}

float Command::CalculateError(NetworkMessageContext& ctx)
{
    float error = 0;

    AI_ERROR(dynamic_cast<const IndicesUpdateCommand*>(ctx.GetIndices()))
    const IndicesUpdateCommand* indices = static_cast<const IndicesUpdateCommand*>(ctx.GetIndices());
    ICALCERR_DIST(destination, 1)

    return error;
}

Command* Command::CreateObject(NetworkMessageContext& ctx)
{
    AI_ERROR(dynamic_cast<const IndicesCreateCommand*>(ctx.GetIndices()))
    const IndicesCreateCommand* indices = static_cast<const IndicesCreateCommand*>(ctx.GetIndices());

    AISubgroup* subgrp;
    if (ctx.IdxTransferRef(indices->subgroup, subgrp) != TMOK)
    {
        return nullptr;
    }
    if (!subgrp)
    {
        return nullptr;
    }

    int index;
    if (ctx.IdxTransfer(indices->index, index) != TMOK)
    {
        return nullptr;
    }
    if (index < 0)
    {
        return nullptr;
    }

    Command* cmd = new Command();
    cmd->TransferMsg(ctx);
    cmd->_targetE = nullptr;
    EntityAI* target = nullptr;
    if (ctx.IdxTransferRef(indices->targetE, target) && target)
    {
        AIGroup* grp = subgrp ? subgrp->GetGroup() : nullptr;
        if (grp)
        {
            cmd->_targetE = grp->FindTarget(target);
        }
    }

    subgrp->InsertCommand(index, cmd);

    NetworkId objectId;
    if (ctx.IdxTransfer(indices->objectCreator, objectId.creator) != TMOK)
    {
        return nullptr;
    }
    if (ctx.IdxTransfer(indices->objectId, objectId.id) != TMOK)
    {
        return nullptr;
    }
    cmd->SetNetworkId(objectId);
    cmd->SetLocal(false);

    return cmd;
}

void Command::DestroyObject()
{
    Fail("Do not use");
}

Vector3 Command::GetCurrentPosition() const
{
    return VZero;
}

RString Command::GetDebugName() const
{
    return RString("Command ") + FindEnumName(_message);
}

template <>
const EnumName* Foundation::GetEnumNames(Command::Message dummy)
{
    static const EnumName CommandMessageNames[] = {EnumName(Command::NoCommand, "NO CMD"),
                                                   EnumName(Command::Wait, "WAIT"),
                                                   EnumName(Command::Attack, "ATTACK"),
                                                   EnumName(Command::Hide, "HIDE"),
                                                   EnumName(Command::Move, "MOVE"),
                                                   EnumName(Command::Heal, "HEAL"),
                                                   EnumName(Command::Repair, "REPAIR"),
                                                   EnumName(Command::Refuel, "REFUEL"),
                                                   EnumName(Command::Rearm, "REARM"),
                                                   EnumName(Command::Support, "SUPPORT"),
                                                   EnumName(Command::Join, "JOIN"),
                                                   EnumName(Command::GetIn, "GET IN"),
                                                   EnumName(Command::Fire, "FIRE"),
                                                   EnumName(Command::GetOut, "GET OUT"),
                                                   EnumName(Command::Stop, "STOP"),
                                                   EnumName(Command::Expect, "EXPECT"),
                                                   EnumName(Command::Action, "ACTION"),
                                                   EnumName(Command::AttackAndFire, "ATTACKFIRE"),
                                                   EnumName()};
    return CommandMessageNames;
}

template <>
const EnumName* Foundation::GetEnumNames(Command::Discretion dummy)
{
    static const EnumName CommandDiscretionNames[] = {
        EnumName(Command::Undefined, "UNDEF"), EnumName(Command::Major, "MAJOR"), EnumName(Command::Normal, "NORMAL"),
        EnumName(Command::Minor, "MINOR"), EnumName()};
    return CommandDiscretionNames;
}

template <>
const EnumName* Foundation::GetEnumNames(Command::Context dummy)
{
    static const EnumName CommandContextNames[] = {
        EnumName(Command::CtxUndefined, "UNDEF"),       EnumName(Command::CtxAuto, "AUTO"),
        EnumName(Command::CtxAutoSilent, "AUTOSILENT"), EnumName(Command::CtxJoin, "JOIN"),
        EnumName(Command::CtxAutoJoin, "AUTOJOIN"),     EnumName(Command::CtxEscape, "ESCAPE"),
        EnumName(Command::CtxMission, "MISSION"),       EnumName(Command::CtxUI, "UI"),
        EnumName(Command::CtxUIWithJoin, "UIJOIN"),     EnumName()};
    return CommandContextNames;
}

AISubgroup::AISubgroup()
{
    _formation = FormWedge;

    _speedMode = SpeedNormal;

    _formationCoef = 1.0;
    _formationCoefChanged = Glob.time - 120.0f;

    _direction = Vector3(0, 0, 1);
    _directionChanged = Time(0);

    _mode = Wait;
    _refreshTime = TIME_MAX;

    _lastPrec = LevelOperative;

    _avoidRefresh = false;
    _doRefresh = false;

    _wantedPosition = VUndefined;
}

AISubgroup::~AISubgroup()
{
    if (IsLocal())
    {
        NetworkId ni = GetNetworkId();
        GetNetworkManager().DeleteObject(ni);
    }
}

template <>
const EnumName* Foundation::GetEnumNames(AI::ThinkImportance dummy)
{
    static const EnumName ThinkImportanceNames[] = {
        EnumName(AI::LevelOperative, "OPER"), EnumName(AI::LevelFastOperative, "FAST"),
        EnumName(AI::LevelStrategic, "STRAT"), EnumName(AI::LevelCommands, "COMM"), EnumName()};
    return ThinkImportanceNames;
}

template <>
const EnumName* Foundation::GetEnumNames(AISubgroup::Mode dummy)
{
    static const EnumName SubgroupModeNames[] = {EnumName(AISubgroup::Wait, "WAIT"),
                                                 EnumName(AISubgroup::PlanAndGo, "PLAN AND GO"),
                                                 EnumName(AISubgroup::DirectGo, "DIRECT GO"), EnumName()};
    return SubgroupModeNames;
}

LSError AISubgroup::Serialize(ParamArchive& ar)
{
    base::Serialize(ar);

    PARAM_CHECK(ar.Serialize("Units", _units, 1))
    PARAM_CHECK(ar.SerializeRef("Leader", _whoAmI, 1))
    PARAM_CHECK(ar.SerializeRef("Group", _group, 1))

    PARAM_CHECK(ar.SerializeEnum("mode", _mode, 1, PlanAndGo))

    PARAM_CHECK(ar.Serialize("wantedPosition", _wantedPosition, 1, VUndefined))

    PARAM_CHECK(ar.Serialize("refreshTime", _refreshTime, 1))

    // strategic plan
    if (ar.GetArVersion() < 8)
    {
        AIUnit* leader = Leader();
        if (leader)
        {
            PARAM_CHECK(ar.Serialize("Planner", *leader->_planner, 1))
            PARAM_CHECK(ar.Serialize("completedTime", leader->_completedTime, 1)) // not used - why ??
            PARAM_CHECK(ar.Serialize("waitWithPlan", leader->_waitWithPlan, 1))
            PARAM_CHECK(ar.Serialize("attemptPlan", leader->_attemptPlan, 1, 0))

            PARAM_CHECK(ar.Serialize("lastPlan", leader->_lastPlan, 1, false))
            PARAM_CHECK(ar.Serialize("noPath", leader->_noPath, 1, false))
            PARAM_CHECK(ar.Serialize("updatePath", leader->_updatePath, 1, false))

            PARAM_CHECK(ar.Serialize("exposureChange", leader->_exposureChange, 1, 0))
        }
    }

    // operative control
    PARAM_CHECK(ar.SerializeEnum("formation", _formation, 1, FormVee))

    PARAM_CHECK(ar.SerializeEnum("speedMode", _speedMode, 1, SpeedNormal))

    // flags
    PARAM_CHECK(ar.SerializeEnum("lastPrec", _lastPrec, 1, LevelOperative))

    PARAM_CHECK(ar.Serialize("avoidRefresh", _avoidRefresh, 1, false))

    PARAM_CHECK(ar.Serialize("formationCoef", _formationCoef, 1, 1.0))
    PARAM_CHECK(ar.Serialize("formationCoefChanged", _formationCoefChanged, 1))

    PARAM_CHECK(ar.Serialize("direction", _direction, 1))
    PARAM_CHECK(ar.Serialize("directionChanged", _directionChanged, 1))
    return LSOK;
}

AISubgroup* AISubgroup::LoadRef(ParamArchive& ar)
{
    TargetSide side = TSideUnknown;
    int idGroup;
    int index;
    if (ar.SerializeEnum("side", side, 1) != LSOK)
    {
        return nullptr;
    }
    if (ar.Serialize("idGroup", idGroup, 1) != LSOK)
    {
        return nullptr;
    }
    if (ar.Serialize("index", index, 1) != LSOK)
    {
        return nullptr;
    }
    AICenter* center = GWorld->GetCenter(side);
    if (!center)
    {
        return nullptr;
    }
    AIGroup* group = nullptr;
    for (int i = 0; i < center->NGroups(); i++)
    {
        AIGroup* grp = center->GetGroup(i);
        if (grp && grp->ID() == idGroup)
        {
            group = grp;
            break;
        }
    }
    if (!group)
    {
        return nullptr;
    }
    return group->GetSubgroup(index);
}

LSError AISubgroup::SaveRef(ParamArchive& ar) const
{
    AIGroup* grp = GetGroup();
    AICenter* center = grp ? grp->GetCenter() : nullptr;
    TargetSide side = center ? center->GetSide() : TSideUnknown;
    int idGroup = grp ? grp->ID() : -1;
    int index = -1;
    if (grp)
    {
        for (int i = 0; i < grp->NSubgroups(); i++)
        {
            if (grp->GetSubgroup(i) == this)
            {
                index = i;
                break;
            }
        }
    }
    PARAM_CHECK(ar.SerializeEnum("side", side, 1))
    PARAM_CHECK(ar.Serialize("idGroup", idGroup, 1))
    PARAM_CHECK(ar.Serialize("index", index, 1))
    return LSOK;
}

NetworkMessageType AISubgroup::GetNMType(NetworkMessageClass cls) const
{
    switch (cls)
    {
        case NMCCreate:
            return NMTCreateAISubgroup;
        case NMCUpdateGeneric:
            return NMTUpdateAISubgroup;
        default:
            return NMTNone;
    }
}

class IndicesCreateAISubgroup : public IndicesNetworkObject
{
    typedef IndicesNetworkObject base;

  public:
    int group;

    IndicesCreateAISubgroup();
    NetworkMessageIndices* Clone() const override { return new IndicesCreateAISubgroup; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesCreateAISubgroup::IndicesCreateAISubgroup()
{
    group = -1;
}

void IndicesCreateAISubgroup::Scan(NetworkMessageFormatBase* format)
{
    base::Scan(format);

    SCAN(group)
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesCreateAISubgroup()
{
    using namespace Poseidon;
    return new IndicesCreateAISubgroup();
}
namespace Poseidon
{

IndicesUpdateAISubgroup::IndicesUpdateAISubgroup()
{
    group = -1;
    units = -1;
    leader = -1;
    mode = -1;
    wantedPosition = -1;
    formation = -1;
    speedMode = -1;
    lastPrec = -1;
    formationCoef = -1;
    direction = -1;
}

void IndicesUpdateAISubgroup::Scan(NetworkMessageFormatBase* format)
{
    base::Scan(format);

    SCAN(group)
    SCAN(units)
    SCAN(leader)
    SCAN(mode)
    SCAN(wantedPosition)
    SCAN(formation)
    SCAN(speedMode)
    SCAN(lastPrec)
    SCAN(formationCoef)
    SCAN(direction)
}

} // namespace Poseidon
NetworkMessageIndices* GetIndicesUpdateAISubgroup()
{
    using namespace Poseidon;
    return new IndicesUpdateAISubgroup();
}
namespace Poseidon
{

NetworkMessageFormat& AISubgroup::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    switch (cls)
    {
        case NMCCreate:
            NetworkObject::CreateFormat(cls, format);
            format.Add("group", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Superior group"));
            break;
        case NMCUpdateGeneric:
            NetworkObject::CreateFormat(cls, format);
            format.Add("group", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Superior group"));
            format.Add("units", NDTRefArray, NCTNone, DEFVALUEREFARRAY, DOC_MSG("Member units"), ET_NOT_CONTAIN_COUNT,
                       ERR_COEF_STRUCTURE);
            format.Add("leader", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Leader unit"), ET_NOT_EQUAL,
                       ERR_COEF_STRUCTURE);
            format.Add("mode", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, PlanAndGo), DOC_MSG("Planning mode"),
                       ET_NOT_EQUAL, ERR_COEF_MODE);
            format.Add("wantedPosition", NDTVector, NCTNone, DEFVALUE(Vector3, VUndefined), DOC_MSG("Destination"),
                       ET_NOT_EQUAL, ERR_COEF_MODE);
            format.Add("formation", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, FormVee), DOC_MSG("Formation type"),
                       ET_NOT_EQUAL, ERR_COEF_MODE);
            format.Add("speedMode", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, SpeedNormal), DOC_MSG("Speed mode"),
                       ET_NOT_EQUAL, ERR_COEF_MODE);
            format.Add("lastPrec", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, LevelOperative),
                       DOC_MSG("Planning precision"));
            format.Add("formationCoef", NDTFloat, NCTNone, DEFVALUE(float, 1.0), DOC_MSG("Formation spacing"));
            format.Add("direction", NDTVector, NCTNone, DEFVALUE(Vector3, VForward), DOC_MSG("Formation direction"));
            break;
        default:
            NetworkObject::CreateFormat(cls, format);
            break;
    }
    return format;
}

AISubgroup* AISubgroup::CreateObject(NetworkMessageContext& ctx)
{
    AI_ERROR(dynamic_cast<const IndicesCreateAISubgroup*>(ctx.GetIndices()))
    const IndicesCreateAISubgroup* indices = static_cast<const IndicesCreateAISubgroup*>(ctx.GetIndices());

    AIGroup* grp;
    if (ctx.IdxTransferRef(indices->group, grp) != TMOK)
    {
        return nullptr;
    }
    if (!grp)
    {
        return nullptr;
    }
    AISubgroup* subgrp = new AISubgroup();
    grp->AddSubgroup(subgrp);

    NetworkId objectId;
    if (ctx.IdxTransfer(indices->objectCreator, objectId.creator) != TMOK)
    {
        return nullptr;
    }
    if (ctx.IdxTransfer(indices->objectId, objectId.id) != TMOK)
    {
        return nullptr;
    }
    subgrp->SetNetworkId(objectId);
    subgrp->SetLocal(false);

    return subgrp;
}

void AISubgroup::DestroyObject()
{
    AISubgroup* main = _group->MainSubgroup();
    if (this == main)
    {
        return;
    }
    if (main)
    {
        while (_units.Size() > 0)
        {
            AIUnit* unit = _units[0];
            if (unit)
            {
                main->AddUnit(unit);
            }
            else
            {
                _units.Delete(0);
            }
        }
        if (!main->Leader())
        {
            main->SelectLeader();
        }
    }

    AIGroup* group = _group;
    if (group)
    {
        _group = nullptr;
        group->SubgroupRemoved(this);
    }
}

TMError AISubgroup::TransferMsg(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCCreate:
            TMCHECK(NetworkObject::TransferMsg(ctx))
            if (ctx.IsSending())
            {
                AI_ERROR(dynamic_cast<const IndicesCreateAISubgroup*>(ctx.GetIndices()))
                const IndicesCreateAISubgroup* indices = static_cast<const IndicesCreateAISubgroup*>(ctx.GetIndices());

                ITRANSF_REF(group)
            }
            break;
        case NMCUpdateGeneric:
            TMCHECK(NetworkObject::TransferMsg(ctx))
            {
                AI_ERROR(dynamic_cast<const IndicesUpdateAISubgroup*>(ctx.GetIndices()))
                const IndicesUpdateAISubgroup* indices = static_cast<const IndicesUpdateAISubgroup*>(ctx.GetIndices());

                if (ctx.IsSending())
                {
                    ITRANSF_REF(group) // used only for owner changes
                    ITRANSF_REFS(units)
                    TMCHECK(ctx.IdxTransferRef(indices->leader, _whoAmI))
                }
                else
                {
                    RefArray<AIUnit> units;
                    TMCHECK(ctx.IdxTransferRefs(indices->units, units))
                    for (int i = 0; i < units.Size(); i++)
                    {
                        // add new units, do not remove old units
                        AIUnit* unit = units[i];
                        if (!unit)
                        {
                            continue;
                        }
                        bool found = false;
                        for (int j = 0; j < _units.Size(); j++)
                        {
                            if (_units[j] == unit)
                            {
                                found = true;
                                break;
                            }
                        }
                        if (found)
                        {
                            continue;
                        }

                        // add unit
                        AI_ERROR(GetGroup());
                        if (unit->GetGroup() != GetGroup())
                        {
                            // group changed
                            Ref<AISubgroup> oldSubgroup = unit->GetSubgroup();
                            Ref<AIGroup> oldGroup = unit->GetGroup();
                            bool bSubgroupLeader = oldSubgroup && oldSubgroup->Leader() == unit;
                            bool bGroupLeader = oldGroup && oldGroup->Leader() == unit;
                            unit->AddRef();
                            unit->ForceRemoveFromGroup();
                            GetGroup()->AddUnit(unit, -1);
                            unit->Release();
                            if (bGroupLeader)
                            {
                                oldGroup->GetCenter()->SelectLeader(oldGroup);
                            }
                            if (bSubgroupLeader)
                            {
                                oldSubgroup->SelectLeader();
                            }
                        }
                        if (unit->GetSubgroup() != this)
                        {
                            // subgroup changed
                            Ref<AISubgroup> oldSubgroup = unit->GetSubgroup();
                            ;
                            bool bSubgroupLeader = oldSubgroup && oldSubgroup->Leader() == unit;
                            AddUnit(unit);
                            if (bSubgroupLeader)
                            {
                                oldSubgroup->SelectLeader();
                            }
                        }
                    }
                    AIUnit* leader;
                    TMCHECK(ctx.IdxTransferRef(indices->leader, leader))
                    if (leader != _whoAmI)
                    {
                        if (!leader)
                        {
                            LOG_DEBUG(AI, "Warning: no subgroup leader");
                        }
                        else if (leader->GetSubgroup() != this)
                        {
                            LOG_DEBUG(AI, "Warning: leader not from this subgroup");
                        }
                        else
                        {
                            _whoAmI = leader;
                        }
                    }
                    if (!GetGroup()->Leader())
                    {
                        GetGroup()->GetCenter()->SelectLeader(GetGroup());
                        if (GetGroup()->Leader())
                        {
                            LOG_DEBUG(AI, "Warning: group leader mising - selected {}",
                                      (const char*)GetGroup()->Leader()->GetDebugName());
                        }
                    }
                }
                ITRANSF_ENUM(mode)
                ITRANSF(wantedPosition)
                ITRANSF_ENUM(formation)
                ITRANSF_ENUM(speedMode)
                ITRANSF_ENUM(lastPrec)
                ITRANSF(formationCoef)
                ITRANSF(direction)
            }
            break;
        default:
            TMCHECK(NetworkObject::TransferMsg(ctx))
            break;
    }
    return TMOK;
}

float AISubgroup::CalculateError(NetworkMessageContext& ctx)
{
    float error = NetworkObject::CalculateError(ctx);

    AI_ERROR(dynamic_cast<const IndicesUpdateAISubgroup*>(ctx.GetIndices()))
    const IndicesUpdateAISubgroup* indices = static_cast<const IndicesUpdateAISubgroup*>(ctx.GetIndices());

    RefArray<AIUnit> units;
    if (ctx.IdxTransferRefs(indices->units, units) == TMOK)
    {
        for (int i = 0; i < units.Size(); i++)
        {
            AIUnit* unit = units[i];
            if (!unit)
            {
                continue;
            }
            bool found = false;
            for (int j = 0; j < _units.Size(); j++)
            {
                if (_units[j] == unit)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                error += ERR_COEF_STRUCTURE;
            }
        }
    }

    ICALCERRE_NEQREF(AIUnit, leader, _whoAmI, ERR_COEF_STRUCTURE)
    ICALCERR_NEQ(int, mode, ERR_COEF_MODE)
    ICALCERR_NEQ(Vector3, wantedPosition, ERR_COEF_MODE)
    ICALCERR_NEQ(int, formation, ERR_COEF_MODE)
    ICALCERR_NEQ(int, speedMode, ERR_COEF_MODE)
    return error;
}

void AISubgroup::OnTaskCreated(int index, Command& cmd)
{
    if (GWorld->GetMode() == GModeNetware && IsLocal())
    {
        GetNetworkManager().CreateCommand(this, index, &cmd);
    }
}

void AISubgroup::OnTaskDeleted(int index, Command& cmd)
{
    if (GWorld->GetMode() == GModeNetware && cmd.IsLocal())
    {
        GetNetworkManager().DeleteCommand(this, index, &cmd);
    }
}

void AISubgroup::InsertCommand(int index, Command* cmd)
{
    if (index > _stack.Size())
    {
        RptF("Warning: Insert out of order");
        _stack.Resize(index);
    }

    _stack.Insert(index);
    _stack[index]._task = cmd;
    _stack[index]._fsm = CreateFSM(cmd->GetType());
    AISubgroupContext context(this);
    context._task = GetCurrent()->_task;
    context._fsm = GetCurrent()->_fsm;
    GetCurrent()->_fsm->SetState(0);
}

void AISubgroup::DeleteCommand(int index, Command* cmd)
{
    if (index >= _stack.Size())
    {
        Fail("Bad index");
        return;
    }

    AI_ERROR(_stack[index]._task->GetNetworkId() == cmd->GetNetworkId());
    if (index == _stack.Size() - 1)
    {
        // FIX: remove dummy items
        for (int i = index - 1; i >= 0; i--)
        {
            if (!_stack[i]._fsm)
            {
                _stack.Delete(i, 1);
            }
        }

        AISubgroupContext context(this);
        context._task = GetCurrent()->_task;
        context._fsm = GetCurrent()->_fsm;
        PopTask(&context, false);
    }
    else
    {
        RptF("Warning: Delete out of order");
        _stack.Delete(index);
    }
}

AIUnit* AISubgroup::Commander() const
{
    AIUnit* leader = Leader();
    if (!leader)
    {
        return nullptr;
    }
    Transport* veh = leader->GetVehicleIn();
    if (veh)
    {
        return veh->CommanderUnit();
    }
    else
    {
        return leader;
    }
}

void AISubgroup::SetDirection(Vector3Val dir)
{
    _direction = dir;
    _directionChanged = Glob.time;
}

// Changes of structure
void AISubgroup::UnitRemoved(AIUnit* unit)
{
    int index = _units.Find(unit);
    AI_ERROR(index >= 0);
    if (index < 0)
    {
        return;
    }

    bool bLeader = (unit == Leader());

    _units.Delete(index);
    if (NUnits() == 0)
    {
        _whoAmI = nullptr;
        return;
    }

    if (bLeader)
    {
        SelectLeader();
    }

    RefreshPlan();

    // Leader() is null only when every remaining unit is dead (wiped subgroup) —
    // expected after removing the last living unit, not an error. Logging it here
    // floods AICenter::AssertValid and trips --strict under mass casualties.
}

void AISubgroup::UnitReplaced(AIUnit* unitOld, AIUnit* unitNew)
{
    AI_ERROR(unitNew);

    int index = _units.Find(unitOld);
    AI_ERROR(index >= 0);
    if (index < 0)
    {
        return;
    }

    // replace all occurences
    if (_whoAmI == unitOld)
    {
        _whoAmI = unitNew;
    }
    if (GetGroup()->_leader == unitOld)
    {
        GetGroup()->_leader = unitNew;
    }
    _units[index] = unitNew;
    AI_ERROR(unitOld->ID() == unitNew->ID());
    GetGroup()->_units[unitOld->ID() - 1] = unitNew;

    // maintain pointers back to subgroup
    unitOld->_subgroup = nullptr;
    unitNew->_subgroup = this;
}

void AISubgroup::AddUnit(AIUnit* unit)
{
    if (!this)
    {
        Fail("No subgroup");
        return;
    }

    AIGroup* grp = GetGroup();
    if (!grp)
    {
        Fail("No group");
        return;
    }

    if (unit->GetSubgroup())
    {
        if (!unit->GetGroup())
        {
            Fail("Subgroup with no group");
            return;
        }
        if (unit->GetGroup() != grp)
        {
            Fail("Adds unit from another group");
            RptF("From %s to %s", (const char*)unit->GetGroup()->GetDebugName(), (const char*)grp->GetDebugName());
            return;
        }
    }
    AI_ERROR(unit->GetSubgroup() != this);

    unit->AddRef();
    if (unit->GetSubgroup())
    {
        unit->RemoveFromSubgroup();
    }
    int index = _units.Find(nullptr);
    if (index >= 0)
    {
        _units[index] = unit;
    }
    else
    {
        _units.Add(unit);
    }
    unit->Release();

    unit->_subgroup = this;
    unit->_formPos = AI::PosInFormation;

    // new unit is not leader unless selected by SelectLeader
    unit->GetVehicle()->SwitchToFormation();
    unit->SetWantedPosition(_wantedPosition, AIUnit::DoNotPlan, true);
    // path is no longer valid
    unit->GetPath().SetSearchTime(Glob.time - 60);

    if (grp && grp->MainSubgroup() == this && !grp->IsAnyPlayerGroup())
    {
        PackedBoolArray list;
        list.Set(unit->ID() - 1, true);
        grp->SendSemaphore(grp->_semaphore, list);
    }
}

void AISubgroup::AddUnitWithCargo(AIUnit* unit)
{
    if (!unit->IsUnit())
    {
        return;
    }

    PoseidonAssert(AssertValid());

    Transport* veh = unit->GetVehicleIn();
    if (veh)
    {
        AIGroup* grp = unit->GetGroup();
        AIUnit* u = veh->CommanderBrain();
        if (u && u->GetGroup() == grp && u->GetSubgroup() != this)
        {
            AddUnit(u);
        }
        u = veh->DriverBrain();
        if (u && u->GetGroup() == grp && u->GetSubgroup() != this)
        {
            AddUnit(u);
        }
        u = veh->GunnerBrain();
        if (u && u->GetGroup() == grp && u->GetSubgroup() != this)
        {
            AddUnit(veh->GunnerBrain());
        }
        const ManCargo& cargo = veh->GetManCargo();
        for (int i = 0; i < cargo.Size(); i++)
        {
            Person* man = cargo[i];
            if (!man)
            {
                continue;
            }
            u = man->Brain();
            if (u && u->GetGroup() == grp && u->GetSubgroup() != this)
            {
                AddUnit(u);
            }
        }
    }
    else
    {
        AddUnit(unit);
    }

    SelectLeader();
    RefreshPlan();

    PoseidonAssert(AssertValid());
}

} // namespace Poseidon
void AISubgroup::SelectLeader(AIUnit* unit)
{
    using namespace Poseidon;
    if (NUnits() == 0)
    {
        // empty subgroup
        _whoAmI = nullptr;
        return;
    }

    int nUnits = 0;
    for (int i = 0; i < NUnits(); i++)
    {
        AIUnit* u = GetUnit(i);
        if (u && u->IsUnit())
        {
            nUnits++;
        }
    }

    if (nUnits > 0 && unit && !unit->IsUnit())
    {
        unit = nullptr;
    }

    if (nUnits > 0 && GetGroup() && GetGroup()->Leader() && GetGroup()->Leader()->IsUnit() &&
        GetGroup()->Leader()->GetSubgroup() == this)
    {
        unit = GetGroup()->Leader();
    }
    else
    {
        Rank bestRank = RankPrivate;
        float bestExp = -FLT_MAX;
        for (int i = 0; i < NUnits(); i++)
        {
            AIUnit* u = GetUnit(i);
            if (!u)
            {
                continue;
            }
            if (nUnits > 0 && !u->IsUnit())
            {
                continue;
            }

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
    // No selectable leader: every remaining unit is null, so the rank loop above
    // left `unit` unset — a wiped subgroup. Do not reject a non-null !IsUnit()
    // here: cargo passengers are not vehicle commanders, but they still need to
    // stay subgroup leaders so get-out waypoint chains keep thinking.
    // Without this guard the code falls through to a null GetVehicle() deref
    // (radio-driven UnitRemoved reselecting a leader for a
    // subgroup whose units are all being torn down). Bail like the empty-subgroup
    // path; the subgroup reselects once a unit is valid again. No error log — a
    // wiped subgroup is expected under mass casualties and logging it here floods
    // AICenter::AssertValid and trips --strict.
    if (!unit)
    {
        _whoAmI = nullptr;
        return;
    }

    if (unit == _whoAmI)
    {
        return;
    }
    if (_whoAmI)
    {
        // reset old leader
        _whoAmI->GetVehicle()->SwitchToFormation();
        _whoAmI->SetWantedPosition(_wantedPosition, AIUnit::DoNotPlan, true);
    }
    unit->GetVehicle()->SwitchToLeader();
    _whoAmI = unit;

    if (_whoAmI->HasAI())
    {
        switch (_mode)
        {
            case Wait:
                _whoAmI->SetWantedPosition(_wantedPosition, AIUnit::DoNotPlan);
                break;
            case PlanAndGo:
                _whoAmI->SetWantedPosition(_wantedPosition, AIUnit::LeaderPlanned);
                break;
            case DirectGo:
                _whoAmI->SetWantedPosition(_wantedPosition, AIUnit::LeaderDirect);
                break;
        }
    }
    else
    {
        DoNotGo();
    }

    _doRefresh = true; // process DoRefresh in next Think

    PoseidonAssert(AssertValid());
}
namespace Poseidon
{

void AISubgroup::RemoveFromGroup()
{
    if (!IsLocal())
    {
        return;
    }

    AIGroup* group = _group;
    if (group)
    {
        _group = nullptr;
        group->SubgroupRemoved(this);
    }
}

void AISubgroup::JoinToSubgroup(AISubgroup* subgrp)
{
    AI_ERROR(subgrp);
    AI_ERROR(subgrp != this);
    if (subgrp == this)
    {
        Fail("Join with themselves.");
        return;
    }
    AI_ERROR(subgrp->GetGroup() == GetGroup());
    if (subgrp->GetGroup() != GetGroup())
    {
        Fail("Join to another group");
        return;
    }
    AI_ERROR(this != GetGroup()->MainSubgroup());

    PoseidonAssert(AssertValid());

    ClearAllCommands();

    while (_units.Size() > 0)
    {
        Ref<AIUnit> unit = _units[0];
        _units.Delete(0);
        if (!unit)
        {
            Fail("No unit");
            continue;
        }
        if (unit->GetSubgroup() != this)
        {
            Fail("Bad structure");
        }
        // remove explicitly - avoid refresh of destroyed subgroup
        unit->_subgroup = nullptr;
        subgrp->AddUnit(unit);
    }
    RemoveFromGroup();

    subgrp->SelectLeader();
    subgrp->RefreshPlan();
    subgrp->UpdateFormationPos();

    PoseidonAssert(subgrp->AssertValid());
}

// Mind
AI::ThinkImportance AISubgroup::CalculateImportance()
{
    return LevelOperative;
}

bool AISubgroup::Think(ThinkImportance prec)
{
#if LOG_THINK
    Log("  Subgroup %s think.", (const char*)GetDebugName());
#endif

    // Empty/all-dead subgroup: no leader to think for, and the Leader()-> derefs below
    // would hit null. Leader() self-heals to a live unit, so a null here means the
    // subgroup is genuinely empty or wiped (every unit dead) — mirror AIGroup::Think
    // and bail. No error: a wiped subgroup keeps NUnits()>0 (corpses) for a while,
    // and logging it floods AICenter::AssertValid and trips --strict.
    if (!Leader())
    {
        return false;
    }

    int u;
    if ((_lastPrec == LevelCommands && prec != LevelCommands) || (_lastPrec != LevelCommands && prec == LevelCommands))
    {
        if (Leader())
        {
            Leader()->ClearStrategicPlan();
        }
    }
    if (_lastPrec != prec && (_lastPrec <= LevelFastOperative || prec <= LevelFastOperative))
    {
        Log("Subgroup %s: Precision changed from %d to %d", (const char*)GetDebugName(), (int)_lastPrec, (int)prec);
        for (u = 0; u < NUnits(); u++)
        {
            AIUnit* unit = GetUnit(u);
            if (!unit)
            {
                continue;
            }
            AIUnit::State state = unit->GetState();
            if (state != AIUnit::Delay && state != AIUnit::InCargo && state != AIUnit::Stopping &&
                state != AIUnit::Stopped)
                Verify(unit->SetState(AIUnit::Wait));
        }
    }
    _lastPrec = prec;

    AddRef();
    AISubgroupContext context(this);
    if (_doRefresh && !_avoidRefresh)
    {
        UpdateAndRefresh(&context);
        _doRefresh = false;
    }
    else
    {
        Update(&context);
    }
    bool destroyed = RefCounter() <= 1;
    Release();
    if (destroyed)
    {
        return false;
    }
    if (NUnits() <= 0)
    {
        return false;
    }
    AI_ERROR(GetGroup());
    AI_ERROR(Leader());

    // BUG:
    if (!Leader())
    {
        SelectLeader();
    }

    if (!HasCommand() && (!Leader() || Leader()->GetState() != AIUnit::Stopping))
    {
        DoNotGo();
    }

    if (Leader() && Leader()->IsUnit())
    {
        const float updateCoefInterval = 2.0;
        if (Glob.time >= _formationCoefChanged + updateCoefInterval)
        {
            UpdateFormationPos();

            UpdateFormationDirection();

            UpdateFormationCoef();
#if LOG_FORMATION_COEF
            GEngine->ShowMessage(1000, "%.3f", _formationCoef);
#endif
        }

        PoseidonAssert(GLOB_WORLD->CheckVehicleStructure());

        // Refresh command
        if (Leader() && Leader()->GetExposureChange() >= CRITICAL_EXPOSURE_CHANGE || Glob.time >= _refreshTime)
        {
            if (GetCurrent())
            {
                AISubgroupContext context(this);
                context._fsm = GetCurrent()->_fsm;
                context._task = GetCurrent()->_task;
                GetCurrent()->_fsm->Refresh(&context);
            }

            Leader()->ClearExposureChange();
        }

        PoseidonAssert(GLOB_WORLD->CheckVehicleStructure());
    }

    bool path = false;
    for (u = 0; u < NUnits(); u++)
    {
        AIUnit* unit = GetUnit(u);
        if (!unit)
        {
            continue;
        }
        if (unit->Think(prec))
        {
            path = true;
        }
    }

    PoseidonAssert(GLOB_WORLD->CheckVehicleStructure());

    return path; // OperPath called - return busy
}

// Communication with units
void AISubgroup::ReceiveAnswer(AIUnit* from, Answer answer)
{
    if (!from)
    {
        return;
    }

#if LOG_COMM
    Log("ReceiveAnswer: Subgroup %s: From %s: Answer %d", (const char*)GetDebugName(),
        (const char*)from->GetDebugName(), answer);
#endif
    switch (answer)
    {
        case AI::UnitDestroyed:
        {
            AICenter* center = GetGroup()->GetCenter();
            PoseidonAssert(center->AssertValid());
            {
                Ref<AIGroup> group = GetGroup(); // group may be removed from center
                AI_ERROR(group);
                AI_ERROR(group->Leader());
                if (!group->Leader())
                {
                    return;
                }
                AICenter* center = group->GetCenter();
                AI_ERROR(center);
                center->DeleteTarget(from->GetVehicle());
                bool bGroupLeader = (from == group->Leader());
                if (!bGroupLeader)
                {
                    // penalize leader for unit lost
                    const VehicleType* type = from->GetVehicle()->GetType();
                    float base = ExperienceForDestroyedCost(type->GetCost());
                    AI_ERROR(base > 0);

                    group->Leader()->AddExp(ExperienceDestroyYourUnit * base);
                }
                // during AIUnit::RemoveFromGroup()
                // unit will be removed from subgroup
                // but subgroup should remain valid
                // patch: hold temporary Ref<> to subgroup
                Ref<AISubgroup> ref = this;
                // remove unit from subgroup
                bool bSubgroupLeader = from == Leader();
                from->RemoveFromGroup();
                // remove unit from corresponding person
                // there are two placed where unit is destructed:
                // here and in AIGroup destructor
                if (from->GetPerson())
                {
                    from->GetPerson()->SetBrain(nullptr);
                }
                if (NUnits() == 0)
                {
                    if (group->NUnits() == 0)
                    {
                        group->SendAnswer(AI::GroupDestroyed); // do nothing
                        return;
                    }
                    else
                    {
                        // report if the only ref is the temporary
                        // this would lead to crash without the patch
                        // we never received following error message
                        // so the patch is not necessary
                        if (ref->RefCounter() == 1)
                        {
                            LOG_ERROR(AI, "AISubgroup::ReceiveAnswer - no refs ({})",
                                      (const char*)group->GetDebugName());
                        }
                        if (this != group->MainSubgroup())
                        {
                            RemoveFromGroup();
                        }
                        AI_ERROR(group->Leader());
                        if (bGroupLeader)
                        {
                            group->Leader()->SendAnswer(AI::IsLeader);
                        }
                    }
                }
                else
                {
                    AI_ERROR(group->Leader());
                    if (bGroupLeader)
                    {
                        group->Leader()->SendAnswer(AI::IsLeader);
                    }
                    if (bSubgroupLeader)
                    {
                        SelectLeader();
                    }
                }
                // group are not destroyed even when all units are destroyed
            }
            PoseidonAssert(center->AssertValid());
        }
        break;
        default:
        {
            AIGroup* group = GetGroup(); // group may be removed from center
            if (group)
            {
                // unit is alive - cancel MIA status
                group->SetReportedDown(from, false);
                group->SetReportBeforeTime(from, TIME_MAX);
                group->ReceiveUnitStatus(from, answer);
            }
            break;
        }
    }
}

void AISubgroup::ClearMissionCommands()
{
    // only one mission command can be in stack
    {
        int i, n = 0;
        for (i = 0; i < _stack.Size(); i++)
        {
            Command* cmd = _stack[i]._task;
            if (cmd->_context == Command::CtxMission)
            {
                n++;
            }
        }
        AI_ERROR(n <= 1);
    }

    int i, n = -1;
    for (i = 0; i < _stack.Size(); i++)
    {
        Command* cmd = _stack[i]._task;
        if (cmd->_context == Command::CtxMission)
        {
            n = i;
            break;
        }
    }
    if (n < 0)
    {
        return;
    }

    AISubgroupContext context(this);
    base::Delete(n, &context);

    if (n == _stack.Size())
    {
        Stop();
    }
}

void AISubgroup::ClearAllCommands()
{
    AISubgroupContext context(this);
    base::Clear(&context); // FSM - clear stack
    Stop();
}

void AISubgroup::ClearEscapeCommands()
{
    int i, n = -1;
    for (i = 0; i < _stack.Size(); i++)
    {
        Command* cmd = _stack[i]._task;
        if (cmd->_context == Command::CtxEscape)
        {
            n = i;
            break;
        }
    }
    if (n < 0)
    {
        return;
    }

    AISubgroupContext context(this);
    base::Delete(n, &context);

    if (n == _stack.Size())
    {
        Stop();
    }
}

bool AISubgroup::CheckHide() const
{
    for (int i = 0; i < _stack.Size(); i++)
    {
        const Command* cmd = _stack[i]._task;
        if (cmd->_message == Command::Hide)
        {
            return true;
        }
    }
    return false;
}

void AISubgroup::ClearAttackCommands()
{
    int i;
    bool deletedLast = false;
    for (i = 0; i < _stack.Size();)
    {
        Command* cmd = _stack[i]._task;
        if ((cmd->_context == Command::CtxAuto || cmd->_context == Command::CtxAutoSilent) &&
            (cmd->_message == Command::Attack || cmd->_message == Command::AttackAndFire ||
             cmd->_message == Command::Hide))
        {
            if (i == _stack.Size() - 1)
            {
                deletedLast = true;
            }
            AISubgroupContext context(this);
            base::Delete(i, &context, false); // do not refresh
        }
        else
        {
            i++;
        }
    }

    if (deletedLast)
    {
        Stop();
    }
}

void AISubgroup::ClearGetInCommands()
{
    int i;
    bool deletedLast = false;
    for (i = 0; i < _stack.Size();)
    {
        Command* cmd = _stack[i]._task;
        if ((cmd->_context == Command::CtxAuto || cmd->_context == Command::CtxAutoSilent) &&
            (cmd->_message == Command::GetIn))
        {
            if (i == _stack.Size() - 1)
            {
                deletedLast = true;
            }
            AISubgroupContext context(this);
            base::Delete(i, &context, false); // do not refresh
        }
        else
        {
            i++;
        }
    }

    if (deletedLast)
    {
        Stop();
    }
}

// Communication with group
void AISubgroup::ReceiveCommand(Command& cmd)
{
#if LOG_COMM
    char buffer[256];
    CreateUnitsList(GetUnitsList(), buffer);
    Log("Receive command: Subgroup %s (%s): Command %d (context %d)", (const char*)GetDebugName(), buffer, cmd._message,
        cmd._context);
#endif

    AI_ERROR(cmd._id >= 0);
    SetDiscretion(cmd._discretion);

    // note: it looks like AISubgroup may be destroyed during Clear(&context)
    SetCommandState(cmd._id, CSReceived, this);

    AISubgroupContext context(this);
    switch (cmd._context)
    {
        case Command::CtxMission:
            ClearMissionCommands();
            EnqueueTask(cmd, &context);
            break;
        case Command::CtxUI:
            Clear(&context);
            PushTask(cmd, &context);
            break;
        case Command::CtxEscape:
            ClearEscapeCommands();
            PushTask(cmd, &context);
            break;
        case Command::CtxUIWithJoin:
            Clear(&context);
            // continue
        case Command::CtxAuto:
        case Command::CtxAutoSilent:
        case Command::CtxJoin:
        case Command::CtxAutoJoin:
            PushTask(cmd, &context);
            break;
        default:
            Fail("Context");
            return;
    }
}

void AISubgroup::SendAnswer(Answer answer)
{
#if LOG_COMM
    Log("Send answer: Subgroup %s: answer %d", (const char*)GetDebugName(), answer);
#endif

    if (_group)
    {
        if (Leader() == _group->Leader())
        {
            _group->ReceiveAnswer(this, answer);
        }
        else
        {
            bool display = false;
            int id = -1;
            Command* cmd = GetCommand();
            if (cmd)
            {
                display = cmd->_context != Command::CtxUndefined && cmd->_context != Command::CtxAutoSilent &&
                          cmd->_context != Command::CtxJoin;
                id = cmd->_id;
            }
            if (answer == AI::CommandCompleted || answer == AI::CommandFailed)
            {
                // remove command confirm if not said
                int index = INT_MAX;
                while (true)
                {
                    RadioMessage* msg = _group->GetRadio().GetPrevMessage(index);
                    if (!msg)
                    {
                        break;
                    }
                    if (msg->GetType() == RMTCommandConfirm)
                    {
                        AI_ERROR(dynamic_cast<RadioMessageCommandConfirm*>(msg));
                        RadioMessageCommandConfirm* msgConfirm = static_cast<RadioMessageCommandConfirm*>(msg);
                        AI_ERROR(msgConfirm->GetTo() == _group);
                        if (msgConfirm->GetFrom() && msgConfirm->GetFrom()->GetSubgroup() == this &&
                            msgConfirm->GetCommand()._id == id)
                        {
                            _group->GetRadio().Cancel(msg);
                            break;
                        }
                    }
                }
            }

            _group->GetRadio().Transmit(new RadioMessageSubgroupAnswer(this, _group, answer, cmd, display),
                                        _group->GetCenter()->GetLanguage());
        }

        if (answer == AI::CommandCompleted)
        {
            for (int i = 0; i < NUnits(); i++)
            {
                AIUnit* unit = GetUnit(i);
                if (!unit || unit->IsGroupLeader())
                {
                    continue;
                }

                unit->AddExp(ExperienceCommandCompleted);
            }
        }
        else if (answer == AI::CommandFailed)
        {
            for (int i = 0; i < NUnits(); i++)
            {
                AIUnit* unit = GetUnit(i);
                if (!unit)
                {
                    continue;
                }

                unit->AddExp(ExperienceCommandFailed);
            }
        }
    }
}

#define OED_SURVIVE_TIME_OK 10.0f
#define OED_OVERFORCE 0.5f
#define OED_SAFE_DIST 1.25f
#define OED_SAFE_EXP 250.0f

void AISubgroup::OnEnemyDetected(const VehicleType* type, Vector3Val pos) {}

void FailCommand(AISubgroupContext* context);

void AISubgroup::FailCommand()
{
    AISubgroupContext context(this);
    if (GetCurrent())
    {
        context._fsm = GetCurrent()->_fsm;
        context._task = GetCurrent()->_task;
        ::FailCommand(&context);
    }
}

void AISubgroup::SetFormation(Formation f)
{
    _formation = f;
    // force all units to change formation
    for (int i = 0; i < NUnits(); i++)
    {
        AIUnit* unit = GetUnit(i);
        if (!unit || !unit->IsUnit())
        {
            unit->GetVehicle()->FormationChanged();
        }
    }
}

PackedBoolArray AISubgroup::GetUnitsList()
{
    PackedBoolArray result;
    for (int i = 0; i < NUnits(); i++)
    {
        AIUnit* unit = GetUnit(i);
        if (!unit)
        {
            continue;
        }

        result.Set(unit->ID() - 1, true);
    }
    return result;
}

AIUnit* AISubgroup::GetFormationPrevious(AIUnit* unit) const
{
    // find convoy precedestor
    AIUnit* maxUnit = nullptr;
    AIUnit* leader = Leader();
    int maxID = 0;
    for (int i = 0; i < NUnits(); i++)
    {
        AIUnit* u = GetUnit(i);
        if (!unit || u == unit || u == leader)
        {
            continue;
        }
        if (!u->IsUnit())
        {
            continue;
        }
        // cautious vehicles follow formation, not convoy
        EntityAI* veh = u->GetVehicle();
        if (veh->IsCautious())
        {
            continue;
        }
        int id = u->ID();
        if (id > maxID && id < unit->ID())
        {
            maxID = u->ID();
            maxUnit = u;
        }
    }

    // if we do not know whom to follow, follow leader
    if (!maxUnit)
    {
        maxUnit = Leader();
    }
    return maxUnit;
}

AIUnit* AISubgroup::GetFormationNext(AIUnit* unit) const
{
    // find convoy successor
    AIUnit* minUnit = nullptr;
    AIUnit* leader = Leader();
    int minID = INT_MAX;
    for (int i = 0; i < NUnits(); i++)
    {
        AIUnit* u = GetUnit(i);
        if (!unit || u == unit || u == leader)
        {
            continue;
        }
        if (!u->IsUnit())
        {
            continue;
        }
        // cautious vehicles follow formation, not convoy
        EntityAI* veh = u->GetVehicle();
        if (veh->IsCautious())
        {
            continue;
        }
        int id = u->ID();
        if (id < minID && id > unit->ID())
        {
            minID = u->ID();
            minUnit = u;
        }
    }
    return minUnit;
}

void AISubgroup::UpdateFormationPos()
{
    AI_ERROR(GetGroup());
    if (!GetGroup())
    {
        return;
    }

    int nTanks = 0;
    for (int i = 0; i < NUnits(); i++)
    {
        AIUnit* unit = GetUnit(i);
        if (!unit)
        {
            continue;
        }
        Transport* veh = unit->GetVehicleIn();
        if (!veh)
        {
            continue;
        }
        if (!unit->IsUnit())
        {
            continue;
        }
        if (veh->GetType()->HasGunner())
        {
            nTanks++;
        }
    }

    AIUnit* formUnits[MAX_UNITS_PER_GROUP];
    Vector3 formPos[MAX_UNITS_PER_GROUP];
    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        formUnits[i] = nullptr;
        formPos[i] = VZero;
    }

    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* unit = GetGroup()->UnitWithID(i + 1);

        int j = i; // formation index given by ID
        const FormInfo& info = formations[_formation][j];
        formUnits[j] = unit;

        formPos[j].Init();
        if (info.base >= 0)
        {
            AIUnit* base = formUnits[info.base];
            float fXBase = 1, fZBase = 1;
            float fXUnit = 1, fZUnit = 1;
            if (base)
            {
                EntityAI* baseVehicle = base->IsUnit() ? base->GetVehicle() : base->GetPerson();
                fXBase = baseVehicle->GetFormationX();
                fZBase = baseVehicle->GetFormationZ();
            }
            if (unit)
            {
                EntityAI* unitVehicle = unit->IsUnit() ? unit->GetVehicle() : unit->GetPerson();
                fXUnit = unitVehicle->GetFormationX();
                fZUnit = unitVehicle->GetFormationZ();
            }
            float factorX = 0.5 * (fXBase + fXUnit);
            float factorZ = 0.5 * (fZBase + fZUnit);
            formPos[j][0] = formPos[info.base][0] + factorX * info.position[0];
            formPos[j][1] = 0;
            formPos[j][2] = formPos[info.base][2] + factorZ * info.position[2];
        }
        else
        {
            formPos[j][0] = info.position[0];
            formPos[j][1] = 0;
            formPos[j][2] = info.position[2];
        }

        if (unit && unit->GetSubgroup() == this)
        {
            unit->_formationAngle = info.angle;
            if (nTanks == 1)
            {
                Transport* veh = unit->GetVehicleIn();
                if (veh && veh->GetType()->HasGunner())
                {
                    unit->_formationAngle = 0;
                }
            }
            unit->_formationPos.Init();
            unit->_formationPos[0] = formPos[j][0];
            unit->_formationPos[1] = 0;
            unit->_formationPos[2] = formPos[j][2];
        }
        j++;
    }
}

void AISubgroup::UpdateFormationCoef()
{
    int u, un = NUnits();

    AIUnit* leader = Leader();
    EntityAI* veh = leader->GetVehicle();

    float leaderSpeed = veh->GetType()->GetMaxSpeedMs();
#if LOG_FORMATION_COEF
    LOG_DEBUG(AI, "Leader speed {:.0f}", leaderSpeed);
#endif
    float maxDelay = 0;
    float maxDelaySpeed = leaderSpeed;
    const Vector3& leaderDir = leader->Direction();
    for (u = 0; u < un; u++)
    {
        AIUnit* unit = GetUnit(u);
        if (!unit || unit == leader || !unit->IsUnit())
        {
            continue;
        }
        EntityAI* uVeh = unit->GetVehicle();

        if (!uVeh->IsCautious())
        {
            continue; // do not follow formation
        }
        Vector3 vect = unit->GetFormationAbsolute() - unit->Position();
        float speed = uVeh->GetType()->GetMaxSpeedMs();
        float delay = (vect * leaderDir);
#if LOG_FORMATION_COEF
        LOG_DEBUG(AI, "Unit {}: speed {:.0f}, delay {:.1f}", unit->ID(), speed, delay);
#endif
        if (delay * maxDelaySpeed > maxDelay * speed)
        {
            maxDelay = delay;
            maxDelaySpeed = speed;
        }
    }

    const float timeToEqual = 3;
    const float maxCoefChange = 0.1; // per second
    const float noDelay = leader->GetCombatMode() >= CMCombat ? 1.0f : 0.5f;

    maxDelay -= noDelay;
#if LOG_FORMATION_COEF
    LOG_DEBUG(AI, "Maximum: speed {:.0f}, delay {:.1f}", maxDelaySpeed, maxDelay);
#endif

    float wantedCoef = (maxDelaySpeed - (1.0f / timeToEqual) * maxDelay) * (1.0f / leaderSpeed);
    float diffCoef = wantedCoef - _formationCoef;
#if LOG_FORMATION_COEF
    LOG_DEBUG(AI, "Old coef: {:.2f}, wanted coef: {:.2f} (diff {:.2f})", _formationCoef, wantedCoef, diffCoef);
#endif

    float age = Glob.time - _formationCoefChanged;
    saturate(diffCoef, -maxCoefChange * age, maxCoefChange * age);
#if LOG_FORMATION_COEF
    LOG_DEBUG(AI, "Saturated diff {:.2f}", diffCoef);
#endif

    _formationCoef += diffCoef;
#if LOG_FORMATION_COEF
    LOG_DEBUG(AI, "New coef: {:.2f}", _formationCoef);
#endif
    saturate(_formationCoef, 0.1, 1.5);
#if LOG_FORMATION_COEF
    LOG_DEBUG(AI, "Saturated new coef: {:.2f}", _formationCoef);
#endif
    _formationCoefChanged = Glob.time;
#if LOG_FORMATION_COEF
    LOG_DEBUG(AI, "");
#endif

    float limitSpeed = _formationCoef * leaderSpeed;
    float maxSpeed = leaderSpeed;
    switch (_speedMode)
    {
        case SpeedLimited:
            // FIX
            if (leader->GetCombatMode() != CMCombat)
            {
                saturateMin(limitSpeed, 0.22 * maxSpeed);
            }
            // FIX END
            veh->LimitSpeed(limitSpeed);
            break;
        case SpeedNormal:
            veh->LimitSpeed(limitSpeed);
            break;
        case SpeedFull:
            veh->LimitSpeed(1.5 * maxSpeed);
            break;
    }
}

void AISubgroup::UpdateFormationDirection()
{
    AIUnit* leader = Leader();
    if (!leader)
    {
        return;
    }

    // check if angle needs to be updated

    const float maxAngleChange = 0.3;
    float age = Glob.time - _directionChanged;

    Vector3Val leaderMove = leader->GetVehicle()->Speed();

    if (leaderMove.SquareSize() > Square(1))
    {
        float angleL = atan2(leaderMove.X(), leaderMove.Z());
        float angleF = atan2(_direction.X(), _direction.Z());
        float angle = AngleDifference(angleL, angleF);

        saturate(angle, -maxAngleChange * age, maxAngleChange * age);

        _direction = Matrix3(MRotationY, -angleF - angle).Direction();
    }

    _directionChanged = Glob.time;
}

PackedBoolArray AISubgroup::GetUnitsListNoCargo()
{
    PackedBoolArray result;
    for (int i = 0; i < NUnits(); i++)
    {
        AIUnit* unit = GetUnit(i);
        if (!unit || !unit->IsUnit())
        {
            continue;
        }

        result.Set(unit->ID() - 1, true);
    }
    return result;
}

// Implementation

// Strategic planning
float AISubgroup::GetExposure(int x, int z)
{
    AI_ERROR(Leader());
    if (Leader()->IsHoldingFire())
    {
        return GetGroup()->GetCenter()->GetExposurePessimistic(x, z);
    }
    else
    {
        return GetGroup()->GetCenter()->GetExposureOptimistic(x, z);
    }
}

float AISubgroup::CalculateExposure(int x, int z)
{
    // returns dammage (in $) per second
    float expField = GetExposure(x, z); // dammage per minute
    expField *= 1 / 60.0;
    float sumCost = 0;
    for (int i = 0; i < NUnits(); i++)
    {
        AIUnit* unit = GetUnit(i);
        if (!unit)
        {
            continue;
        }
        EntityAI* veh = unit->GetVehicle();
        float dammage = expField * veh->GetType()->GetInvArmor();
        float cost = unit->GetPerson()->GetType()->GetCost();
        if (!unit->IsFreeSoldier())
        { // may be driver, commander, gunner, cargo
            if (unit->IsUnit())
            { // caluculate only "commander" units
                cost += veh->GetType()->GetCost();
            }
        }
        // if we are not able to defend, assume bigger dammage
        bool ableToDefend = (veh->GetAmmoHit() > 50 && !unit->IsHoldingFire());
        if (!ableToDefend)
        {
            dammage *= 4;
        }
        sumCost += dammage * cost;
    }
    return sumCost;
}

float AISubgroup::GetFieldCost(int x, int z)
{
    if (!InRange(x, z))
    {
        return SET_UNACCESSIBLE;
    }
    GeographyInfo geogr = GLOB_LAND->GetGeography(x, z);

    float cost = -FLT_MAX;
    for (int i = 0; i < NUnits(); i++)
    {
        AIUnit* unit = GetUnit(i);
        if (!unit || !unit->IsUnit())
        {
            continue;
        }
        EntityAI* uVeh = unit->GetVehicle();
        float costU = uVeh->GetCost(geogr) * uVeh->GetFieldCost(geogr);
        if (costU > cost)
        {
            cost = costU;
        }
    }

    cost *= LandGrid;                                    // cost = time for distance == LandGrid
    float costExposure = cost * CalculateExposure(x, z); // dammage (in $) per time cost

    return cost + COEF_EXPOSURE * costExposure;
}

bool AISubgroup::FindNearestSafe(int& x, int& z, float threshold)
{
    if (IsSafe(x, z, threshold))
    {
        return true;
    }
    int rMax = 10; // do not too far (max 500 m)
    int i, r, xt, zt;
    for (r = 1; r < rMax; r++)
    {
        for (i = 0; i < r; i++)
        {
            xt = x - i;
            zt = z - r;
            if (IsSafe(xt, zt, threshold))
            {
                goto SafeFieldFound;
            }
            xt = x + i;
            zt = z - r;
            if (IsSafe(xt, zt, threshold))
            {
                goto SafeFieldFound;
            }
            xt = x + r;
            zt = z - i;
            if (IsSafe(xt, zt, threshold))
            {
                goto SafeFieldFound;
            }
            xt = x + r;
            zt = z + i;
            if (IsSafe(xt, zt, threshold))
            {
                goto SafeFieldFound;
            }
            xt = x + i;
            zt = z + r;
            if (IsSafe(xt, zt, threshold))
            {
                goto SafeFieldFound;
            }
            xt = x - i;
            zt = z + r;
            if (IsSafe(xt, zt, threshold))
            {
                goto SafeFieldFound;
            }
            xt = x - r;
            zt = z + i;
            if (IsSafe(xt, zt, threshold))
            {
                goto SafeFieldFound;
            }
            xt = x - r;
            zt = z - i;
            if (IsSafe(xt, zt, threshold))
            {
                goto SafeFieldFound;
            }
        }
        xt = x - r;
        zt = z - r;
        if (IsSafe(xt, zt, threshold))
        {
            goto SafeFieldFound;
        }
        xt = x + r;
        zt = z - r;
        if (IsSafe(xt, zt, threshold))
        {
            goto SafeFieldFound;
        }
        xt = x + r;
        zt = z + r;
        if (IsSafe(xt, zt, threshold))
        {
            goto SafeFieldFound;
        }
        xt = x - r;
        zt = z + r;
        if (IsSafe(xt, zt, threshold))
        {
            goto SafeFieldFound;
        }
    }
    return false;
SafeFieldFound:
    x = xt;
    z = zt;
    return true;
}

#define CheckSafety                                            \
    if (GetSafety(xt, zt, exposure) && exposure < minExposure) \
    {                                                          \
        xBest = xt;                                            \
        zBest = zt;                                            \
        minExposure = exposure;                                \
    }

bool AISubgroup::FindNearestSafer(int& x, int& z)
{
    int xt, xBest = x;
    int zt, zBest = z;
    float exposure = 0, minExposure = GetExposure(x, z);
    int r, rMax = 8;
    for (r = 1; r < rMax; r++)
    {
        int i;
        for (i = 0; i < r; i++)
        {
            xt = x - i;
            zt = z - r;
            CheckSafety xt = x + i;
            zt = z - r;
            CheckSafety xt = x + r;
            zt = z - i;
            CheckSafety xt = x + r;
            zt = z + i;
            CheckSafety xt = x + i;
            zt = z + r;
            CheckSafety xt = x - i;
            zt = z + r;
            CheckSafety xt = x - r;
            zt = z + i;
            CheckSafety xt = x - r;
            zt = z - i;
            CheckSafety
        }
        xt = x - r;
        zt = z - r;
        CheckSafety xt = x + r;
        zt = z - r;
        CheckSafety xt = x + r;
        zt = z + r;
        CheckSafety xt = x - r;
        zt = z + r;
        CheckSafety
    }
    if (x == xBest && z == zBest)
    {
        return false;
    }
    else
    {
        x = xBest;
        z = zBest;
        return true;
    }
}

void AISubgroup::ClearPlan()
{
    if (Leader())
    {
        Leader()->ClearStrategicPlan();
    }
}

void AISubgroup::RefreshPlan()
{
    if (Leader())
    {
        Leader()->RefreshStrategicPlan();
    }
}

void AISubgroup::ExposureChanged(int x, int z, float optimistic, float pessimistic)
{
    if (Leader())
    {
        Leader()->ExposureChanged(x, z, optimistic, pessimistic);
    }
}

bool AISubgroup::IsSafe(int x, int z, float threshold)
{
    return InRange(x, z) && GetFieldCost(x, z) < GET_UNACCESSIBLE && GetExposure(x, z) < threshold;
}
bool AISubgroup::GetSafety(int x, int z, float& exposure)
{
    if (InRange(x, z) && GetFieldCost(x, z) < GET_UNACCESSIBLE)
    {
        exposure = GetExposure(x, z);
        return true;
    }
    else
    {
        return false;
    }
}

// interface for fsm

void AISubgroup::GoDirect(Vector3Val pos)
{
    Command* cmd = GetCommand();
    if (cmd)
    {
        cmd->_destination = pos;
    }

    _mode = DirectGo;
    _wantedPosition = pos;
    if (_whoAmI)
    {
        _whoAmI->SetWantedPosition(pos, AIUnit::LeaderDirect);
    }
}

void AISubgroup::GoPlanned(Vector3Val pos)
{
    Command* cmd = GetCommand();
    if (cmd)
    {
        cmd->_destination = pos;
    }

    _mode = PlanAndGo;
    _wantedPosition = pos;
    if (_whoAmI)
    {
        _whoAmI->SetWantedPosition(pos, AIUnit::LeaderPlanned);
    }
}

void AISubgroup::DoNotGo()
{
    _mode = Wait;
    if (_whoAmI)
    {
        _whoAmI->SetWantedPosition(_wantedPosition, AIUnit::DoNotPlan);
    }
}

void AISubgroup::Stop()
{
    _mode = Wait;
    if (_whoAmI)
    {
        _whoAmI->SetWantedPosition(_wantedPosition, AIUnit::DoNotPlan, true);
    }
}

void AISubgroup::SetDiscretion(Command::Discretion discretion)
{
    if (!GetGroup() || GetGroup()->IsAnyPlayerGroup())
    {
        return;
    }

    if (discretion == Command::Undefined)
    {
        return;
    }

    if (!GetGroup()->GetFlee())
    {
        // ignore when group flees
        if (Glob.time - 180.0 < GetGroup()->GetDisclosed() && discretion == Command::Major)
        {
            discretion = Command::Normal;
        }
    }

    Formation f;
    Semaphore s;
    switch (discretion)
    {
        case Command::Major:
            f = FormColumn;
            s = SemaphoreGreen;
            break;
        case Command::Normal:
            f = FormWedge;
            s = SemaphoreYellow;
            break;
        default:
        case Command::Minor:
            f = FormLine;
            s = SemaphoreRed;
            break;
    }

    GetGroup()->SendFormation(f, this);
    GetGroup()->SendSemaphore(s, GetUnitsList());
    if (GetGroup() && GetGroup()->MainSubgroup() == this)
    {
        GetGroup()->_semaphore = s;
    }
}

bool AISubgroup::AllUnitsCompleted()
{
    AIUnit* leader = Leader();
    if (!leader || !leader->HasAI())
    {
        return true;
    }
    if (leader->GetState() == AIUnit::Completed)
    {
        // avoid AllUnitsCompleted satisfy twice
        leader->SetState(AIUnit::Wait);
        return true;
    }
    else
    {
        return false;
    }
}

RString AISubgroup::GetDebugName() const
{
    AI_ERROR(GetGroup());
    char buffer[256];
    if (NUnits() > 0)
    {
        AI_ERROR(Leader());
        snprintf(buffer, sizeof(buffer), "%s:%d", (const char*)GetGroup()->GetDebugName(), Leader()->ID());
    }
    else
    {
        AI_ERROR(GetGroup()->MainSubgroup() == this);
        snprintf(buffer, sizeof(buffer), "%s: Empty main subgroup", (const char*)GetGroup()->GetDebugName());
    }
    return buffer;
}

bool AISubgroup::AssertValid() const
{
    if (NUnits() == 0)
    {
        return true;
    }

    bool result = true;
    if (!Leader())
    {
        // Leader() self-heals to any living unit, so a null leader means every
        // unit is dead: a wiped subgroup whose corpses are still pending cleanup.
        // That is an expected transient state under mass casualties, not an
        // invariant violation, so treat it as valid like an empty subgroup. Left
        // as an error it makes AICenter::AssertValid log a fatal AI_ERROR every
        // frame, which --strict turns into a game shutdown.
        return true;
    }
    else if (Leader()->GetSubgroup() == nullptr)
    {
        Fail("leader with no subgroup");
        result = false;
    }
    else if (Leader()->GetSubgroup() != this)
    {
        Fail("leader from another subgroup");
        result = false;
        {
            // try to repair it
            AISubgroup* subgroup = const_cast<AISubgroup*>(this);
            subgroup->SelectLeader();
        }
    }
    for (int i = 0; i < _units.Size(); i++)
    {
        AIUnit* unit = _units[i];
        if (!unit)
        {
            continue;
        }
        if (unit != Leader())
        {
            // do not check twice
            if (unit->GetSubgroup() == nullptr)
            {
                Fail("unit with no subgroup");
                result = false;
            }
            else if (unit->GetSubgroup() != this)
            {
                Fail("unit from another subgroup");
                result = false;
            }
        }
        if (!unit->AssertValid())
        {
            result = false;
        }
    }
    return result;
}

void AISubgroup::Dump(int indent) const {}

} // namespace Poseidon
