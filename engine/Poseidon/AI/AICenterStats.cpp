#include <Poseidon/AI/AI.hpp>
#include <Poseidon/AI/Path/AIDefs.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/World/Entities/Infantry/Person.hpp>
#include <Poseidon/Core/Version.hpp>

#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/Audio/DynSound.hpp>
#include <Poseidon/World/Detection/Detector.hpp>
#include <Poseidon/World/Entities/Weapons/Shots.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Random/randomGen.hpp>
#include <Poseidon/World/Scene/Camera/CamEffects.hpp>

#include <Poseidon/UI/Locale/Languages.hpp>

#include <Poseidon/Foundation/Algorithms/Qsort.hpp>

#include <Poseidon/AI/ArcadeTemplate.hpp>
#include <Poseidon/IO/Serialization/ParamArchive.hpp>

#include <Poseidon/Game/Commands/GameStateExt.hpp>

#include <Poseidon/Network/Network.hpp>
#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/Foundation/Platform/AppConfig.hpp>
#include <Poseidon/Foundation/Platform/GamePaths.hpp>

#include <Poseidon/Foundation/Enums/EnumNames.hpp>

#include <Poseidon/Game/UiActions.hpp>

#include <Poseidon/Foundation/Strings/Mbcs.hpp>
#include <Poseidon/Foundation/Strings/Bstring.hpp>
#include <Poseidon/Foundation/Strings/StrFormat.hpp>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <float.h>
#include <string>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Memtype.h>
#include <Poseidon/Foundation/Types/Pointers.hpp>

Person* GetPlayerPerson(EntityAI* vehicle);

namespace Poseidon
{
using namespace Foundation;
void PositionToAA11(Vector3Val pos, char* buffer);
} // namespace Poseidon

namespace Poseidon
{

const float AccuracyLast = 7200; // how long we can remember the information

void AIStatsCampaign::AddVariable(GameVariable& var)
{
    for (int i = 0; i < _variables.Size(); i++)
    {
        if (_variables[i]._name == var._name)
        {
            _variables[i] = var;
            return;
        }
    }
    _variables.Add(var);
}

void AIStatsCampaign::Update(AIStatsMission& mission)
{
    _lastScore = _score;

    if (_playerInfo.unit)
    {
        float experience = _playerInfo.unit->GetPerson()->GetExperience();
        float score = experience - _playerInfo.experience;
        _playerInfo.experience = experience;
        _playerInfo.rank = _playerInfo.unit->GetPerson()->GetRank();

        // score
        if (ExtParsMission.FindEntry("minScore") && ExtParsMission.FindEntry("avgScore") &&
            ExtParsMission.FindEntry("maxScore"))
        {
            float minScore = ExtParsMission >> "minScore";
            float avgScore = ExtParsMission >> "avgScore";
            float maxScore = ExtParsMission >> "maxScore";

            int points;
            if (score >= avgScore)
            {
                points = toInt(4 * (score - avgScore) / (maxScore - avgScore));
                saturateMin(points, 4);
            }
            else
            {
                points = toInt(3 * (score - avgScore) / (avgScore - minScore));
                saturateMax(points, -3);
            }
            _score += points;
        }
    }

    // FIX - _playerInfo.unit is not set in MP
    else if (GWorld->GetRealPlayer())
    {
        _playerInfo.experience = GWorld->GetRealPlayer()->GetExperience();
        _playerInfo.rank = GWorld->GetRealPlayer()->GetRank();
    }

    _inCombat += Glob.time - mission._start;

    time(&_date);

    for (int i = 0; i < mission._events.Size(); i++)
    {
        switch (mission._events[i].type)
        {
            case SETUnitLost:
                _casualties++;
                break;
            case SETKillsEnemyInfantry:
                _kills[SKEnemyInfantry]++;
                break;
            case SETKillsEnemySoft:
                _kills[SKEnemySoft]++;
                break;
            case SETKillsEnemyArmor:
                _kills[SKEnemyArmor]++;
                break;
            case SETKillsEnemyAir:
                _kills[SKEnemyAir]++;
                break;
            case SETKillsFriendlyInfantry:
                _kills[SKFriendlyInfantry]++;
                break;
            case SETKillsFriendlySoft:
                _kills[SKFriendlySoft]++;
                break;
            case SETKillsFriendlyArmor:
                _kills[SKFriendlyArmor]++;
                break;
            case SETKillsFriendlyAir:
                _kills[SKFriendlyAir]++;
                break;
            case SETKillsCivilInfantry:
                _kills[SKCivilianInfantry]++;
                break;
            case SETKillsCivilSoft:
                _kills[SKCivilianSoft]++;
                break;
            case SETKillsCivilArmor:
                _kills[SKCivilianArmor]++;
                break;
            case SETKillsCivilAir:
                _kills[SKCivilianAir]++;
                break;
            case SETUser:
                break;
        }
    }
}

struct KillsItem
{
    AIStatsKills index;
    int value;

    LSError Serialize(ParamArchive& ar);
};

LSError KillsItem::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.SerializeEnum("index", index, 1))
    PARAM_CHECK(ar.Serialize("value", value, 1, 0))
    return LSOK;
}

LSError AIStatsCampaign::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.Serialize("PlayerInfo", _playerInfo, 1))
    ar.SetParams(GWorld->GetGameState());
    PARAM_CHECK(ar.Serialize("Variables", _variables, 1))

    PARAM_CHECK(ar.Serialize("casualities", _casualties, 1, 0))
    PARAM_CHECK(ar.Serialize("inCombat", _inCombat, 1, 0))

    PARAM_CHECK(ar.Serialize("score", _score, 1, 0))
    PARAM_CHECK(ar.Serialize("lastScore", _lastScore, 1, 0))

    PARAM_CHECK(ar.SerializeArray("awards", _awards, 1))
    PARAM_CHECK(ar.SerializeArray("penalties", _penalties, 1))

    AutoArray<KillsItem> kills;
    if (ar.IsSaving())
    {
        kills.Resize(SKN);
        for (int i = 0; i < SKN; i++)
        {
            kills[i].index = (AIStatsKills)i;
            kills[i].value = _kills[i];
        }
        PARAM_CHECK(ar.Serialize("Kills", kills, 1))
    }
    else if (ar.GetPass() == ParamArchive::PassFirst)
    {
        PARAM_CHECK(ar.Serialize("Kills", kills, 1))
        for (int i = 0; i < kills.Size(); i++)
        {
            int index = kills[i].index;
            if (index >= 0 && index < SKN)
            {
                _kills[index] = kills[i].value;
            }
        }
    }

    PARAM_CHECK(ar.Serialize("date", (int&)_date, 1, 0))

    return LSOK;
}

LSError AIStatsCampaign::CampaignSerialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.Serialize("PlayerInfo", _playerInfo, 1))
    ar.SetParams(GWorld->GetGameState());
    PARAM_CHECK(ar.Serialize("Variables", _variables, 1))

    PARAM_CHECK(ar.Serialize("casualities", _casualties, 1))
    PARAM_CHECK(ar.Serialize("inCombat", _inCombat, 1))

    PARAM_CHECK(ar.Serialize("score", _score, 1, 0))
    PARAM_CHECK(ar.Serialize("lastScore", _lastScore, 1, 0))

    PARAM_CHECK(ar.SerializeArray("awards", _awards, 1))
    PARAM_CHECK(ar.SerializeArray("penalties", _penalties, 1))

    if (ar.GetArVersion() >= 3)
    {
        AutoArray<KillsItem> kills;
        if (ar.IsSaving())
        {
            kills.Resize(SKN);
            for (int i = 0; i < SKN; i++)
            {
                kills[i].index = (AIStatsKills)i;
                kills[i].value = _kills[i];
            }
            PARAM_CHECK(ar.Serialize("Kills", kills, 3))
        }
        else if (ar.GetPass() == ParamArchive::PassFirst)
        {
            PARAM_CHECK(ar.Serialize("Kills", kills, 3))
            for (int i = 0; i < kills.Size(); i++)
            {
                int index = kills[i].index;
                if (index >= 0 && index < SKN)
                {
                    _kills[index] = kills[i].value;
                }
            }
        }
    }

    PARAM_CHECK(ar.Serialize("date", (int&)_date, 1, 0))

    return LSOK;
}
void AIStats::OnMPMissionEnd()
{
    const bool automationMissionExit = AppConfig::Instance().IsSimulateMode() ||
                                       !AppConfig::Instance().GetTestMissionPath().empty() ||
                                       !AppConfig::Instance().GetMPAssign().empty();
    if (AppConfig::Instance().WriteMPReport() && (GetNetworkManager().IsServer() || automationMissionExit))
    {
        _mission.WriteMPReport();
    }
}

void AIStats::OnMissionStart()
{
    _mission.OnMissionStart();
}

void AIStats::OnVehicleDestroyed(EntityAI* killed, EntityAI* killer)
{
    if (!killed)
    {
        return;
    }

    Person* killedSoldier = dyn_cast<Person>(killed);
    Person* killerPerson = GetPlayerPerson(killer);

    if (killed && killer)
    {
        _mission.RecordReportKill(killed, killer);
    }

    // MP statistics
    bool isEnemy;
    if (killer == killed)
    {
        isEnemy = false;
    }
    else if (killed->CommanderUnit() && killed->CommanderUnit()->GetPerson()->GetExperience() < ExperienceRenegadeLimit)
    {
        isEnemy = true;
    }
    else
    {
        if (killer)
        {
            AICenter* center = GWorld->GetCenter(killer->Vehicle::GetTargetSide());
            isEnemy = center && center->IsEnemy(killed->Vehicle::GetTargetSide());
        }
        else
        {
            isEnemy = true;
        }
    }

    const MissionHeader* header = GetNetworkManager().GetMissionHeader();
    if (header && header->aiKills)
    {
        if (killer)
        {
            if (killerPerson && killerPerson->Brain() && killerPerson->Brain()->IsPlayable())
            {
                RString playerName = killerPerson->GetInfo()._name;
                TargetSide playerSide = TSideUnknown;
                AIGroup* grp = killerPerson->Brain()->GetGroup();
                if (grp)
                {
                    AICenter* center = grp->GetCenter();
                    if (center)
                    {
                        playerSide = center->GetSide();
                    }
                }
                _mission.AddMPKill(playerName, playerSide, killed->GetType(),
                                   killedSoldier && killedSoldier->IsNetworkPlayer(), isEnemy);
            }
        }
        if (killedSoldier && killedSoldier->Brain() && killedSoldier->Brain()->IsPlayable())
        {
            TargetSide playerSide = TSideUnknown;
            AIGroup* grp = killedSoldier->Brain()->GetGroup();
            if (grp)
            {
                AICenter* center = grp->GetCenter();
                if (center)
                {
                    playerSide = center->GetSide();
                }
            }
            _mission.AddMPKill(killedSoldier->GetInfo()._name, playerSide, nullptr, true, isEnemy);
        }
    }
    else
    {
        if (killer)
        {
            if (killerPerson && killerPerson->IsNetworkPlayer())
            {
                RString playerName = killerPerson->GetInfo()._name;
                TargetSide playerSide = TSideUnknown;
                AIUnit* brain = killerPerson->Brain();
                if (brain)
                {
                    AIGroup* grp = brain->GetGroup();
                    if (grp)
                    {
                        AICenter* center = grp->GetCenter();
                        if (center)
                        {
                            playerSide = center->GetSide();
                        }
                    }
                }
                _mission.AddMPKill(playerName, playerSide, killed->GetType(),
                                   killedSoldier && killedSoldier->IsNetworkPlayer(), isEnemy);
            }
        }
        if (killedSoldier && killedSoldier->Brain() && killedSoldier->IsNetworkPlayer())
        {
            TargetSide playerSide = TSideUnknown;
            AIGroup* grp = killedSoldier->Brain()->GetGroup();
            if (grp)
            {
                AICenter* center = grp->GetCenter();
                if (center)
                {
                    playerSide = center->GetSide();
                }
            }
            _mission.AddMPKill(killedSoldier->GetInfo()._name, playerSide, nullptr, true, isEnemy);
        }
    }

    Person* player = GWorld->GetRealPlayer();
    if (!player)
    {
        return;
    }
    AIUnit* me = player->Brain();
    AIGroup* myGroup = me ? me->GetGroup() : nullptr;
    AICenter* myCenter = myGroup ? myGroup->GetCenter() : nullptr;
    if (!myCenter)
    {
        switch (player->Vehicle::GetTargetSide())
        {
            case TEast:
                myCenter = GWorld->GetEastCenter();
                break;
            case TWest:
                myCenter = GWorld->GetWestCenter();
                break;
            case TGuerrila:
                myCenter = GWorld->GetGuerrilaCenter();
                break;
            case TCivilian:
                myCenter = GWorld->GetCivilianCenter();
                break;
        }
    }

    AIUnit* killedUnit = killedSoldier ? killedSoldier->Brain() : nullptr;

    if (killedSoldier == player || myGroup && killedUnit && killedUnit->GetGroup() == myGroup)
    {
        char time[32], position[32];
        float tod = 24.0 * Glob.clock.GetTimeOfDay();
        int hour = toIntFloor(tod);
        int minute = toIntFloor(60.0 * (tod - hour));
        snprintf(time, sizeof(time), "% 2d:%02d", hour, minute);
        PositionToAA11(killed->Position(), position);

        RString message = LocalizeString(IDS_STAT_UNIT_LOST);
        char buffer[256];
        ::sprintf(buffer, message, time,
                  (const char*)LocalizeString(IDS_PRIVATE + ClampRankIndex(killedSoldier->GetRank())),
                  (const char*)killedSoldier->GetInfo()._name, position);
        if (killerPerson)
        {
            _mission.AddEvent(SETUnitLost, killed->Position(), buffer, killed->GetType(),
                              killedSoldier->GetInfo()._name, killedSoldier == player, killer->GetType(),
                              killerPerson->GetInfo()._name, killerPerson->IsNetworkPlayer());
        }
        else
        {
            _mission.AddEvent(SETUnitLost, killed->Position(), buffer, killed->GetType(),
                              killedSoldier->GetInfo()._name, killed == player, GWorld->Preloaded(VTypeAllVehicles), "",
                              false);
        }
    }

    if (killerPerson == player && killed->GetType()->GetCost() > 2000)
    {
        char time[32], position[32];
        float tod = 24.0 * Glob.clock.GetTimeOfDay();
        int hour = toIntFloor(tod);
        int minute = toIntFloor(60.0 * (tod - hour));
        snprintf(time, sizeof(time), "% 2d:%02d", hour, minute);
        PositionToAA11(killed->Position(), position);

        // get original target side
        TargetSide side = killed->GetType()->GetTypicalSide();
        bool isEnemy =
            killed->CommanderUnit() && killed->CommanderUnit()->GetPerson()->GetExperience() < ExperienceRenegadeLimit;
        if (!isEnemy && myCenter)
        {
            isEnemy = myCenter->IsEnemy(side);
        }

        if (killedSoldier)
        {
            RString message;
            AIStatsEventType type;
            if (isEnemy)
            {
                message = LocalizeString(IDS_STAT_KILL_ENEMY_SOLDIER);
                type = SETKillsEnemyInfantry;
            }
            else if (side == TCivilian)
            {
                message = LocalizeString(IDS_STAT_KILL_CIVIL);
                type = SETKillsCivilInfantry;
            }
            else
            {
                message = LocalizeString(IDS_STAT_KILL_FRIENDLY_SOLDIER);
                type = SETKillsFriendlyInfantry;
            }
            char buffer[256];
            ::sprintf(buffer, message, time, position);
            if (killedSoldier)
            {
                _mission.AddEvent(type, killed->Position(), buffer, killed->GetType(), killedSoldier->GetInfo()._name,
                                  killedSoldier->IsRemotePlayer(), killer->GetType(), killerPerson->GetInfo()._name,
                                  true);
            }
            else
            {
                _mission.AddEvent(type, killed->Position(), buffer, killed->GetType(), "", false, killer->GetType(),
                                  killerPerson->GetInfo()._name, true);
            }
        }
        else
        {
            RString message;
            if (isEnemy)
            {
                message = LocalizeString(IDS_STAT_KILL_ENEMY_UNIT);
            }
            else if (side == TCivilian)
            {
                message = LocalizeString(IDS_STAT_KILL_CIVILIAN_UNIT);
            }
            else
            {
                message = LocalizeString(IDS_STAT_KILL_FRIENDLY_UNIT);
            }
            char buffer[256];
            ::sprintf(buffer, message, time, (const char*)killed->GetType()->GetDisplayName(), position);
            AIStatsEventType type;
            switch (killed->GetType()->GetKind())
            {
                default:
                case VSoft:
                    if (isEnemy)
                    {
                        type = SETKillsEnemySoft;
                    }
                    else if (side == TCivilian)
                    {
                        type = SETKillsCivilSoft;
                    }
                    else
                    {
                        type = SETKillsFriendlySoft;
                    }
                    break;
                case VArmor:
                    if (isEnemy)
                    {
                        type = SETKillsEnemyArmor;
                    }
                    else if (side == TCivilian)
                    {
                        type = SETKillsCivilArmor;
                    }
                    else
                    {
                        type = SETKillsFriendlyArmor;
                    }
                    break;
                case VAir:
                    if (isEnemy)
                    {
                        type = SETKillsEnemyAir;
                    }
                    else if (side == TCivilian)
                    {
                        type = SETKillsCivilAir;
                    }
                    else
                    {
                        type = SETKillsFriendlyAir;
                    }
                    break;
            }
            _mission.AddEvent(type, killed->Position(), buffer, killed->GetType(), "", false, killer->GetType(),
                              killerPerson->GetInfo()._name, true);
        }
    }
}

void AIStats::OnVehicleDamaged(EntityAI* injured, EntityAI* killer, float damage, RString ammo)
{
    if (injured)
        _mission.RecordReportInjury(injured, killer, damage, ammo);
}

void AIStats::AddReportHeader(RString text)
{
    _mission.AddReportHeader(text);
}

void AIStats::AddReportEvent(RString text)
{
    _mission.AddReportEvent(text);
}

void AIStats::AddReportFooter(RString text)
{
    _mission.AddReportFooter(text);
}

void AIStats::AddUserEvent(RString event, Vector3Par pos)
{
    _mission.AddEvent(SETUser, pos, event, GWorld->Preloaded(VTypeAllVehicles), "", false,
                      GWorld->Preloaded(VTypeAllVehicles), "", false);
}

LSError AIStats::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.Serialize("Mission", _mission, 1))
    PARAM_CHECK(ar.Serialize("Campaign", _campaign, 1))
    return LSOK;
}

AIStats& GetGStats()
{
    static AIStats instance;
    return instance;
}

AITargetInfo::AITargetInfo()
{
    _idExact = nullptr;
    _side = TSideUnknown;
    _type = nullptr;
    _x = 0;
    _z = 0;

    _accuracySide = 0;
    _timeSide = TIME_MIN;
    _accuracyType = 0;
    _timeType = TIME_MIN;

    _time = TIME_MIN;
    _precisionPos = 0;

    _exposure = false;
    _vanished = false;
    _destroyed = false;
    _dir = VZero; // unknown
}

LSError AITargetInfo::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.SerializeRef("IdExact", _idExact, 1))
    PARAM_CHECK(ar.SerializeEnum("side", _side, 1))
    PARAM_CHECK(Poseidon::Serialize(ar, "type", _type, 1))

    PARAM_CHECK(ar.Serialize("realPos", _realPos, 1))
    PARAM_CHECK(ar.Serialize("pos", _pos, 1))
    PARAM_CHECK(ar.Serialize("x", _x, 1))
    PARAM_CHECK(ar.Serialize("z", _z, 1))
    PARAM_CHECK(ar.Serialize("dir", _dir, 1, VZero))
    PARAM_CHECK(ar.Serialize("accuracySide", _accuracySide, 1, 4.0))
    PARAM_CHECK(ar.Serialize("timeSide", _timeSide, 1))
    PARAM_CHECK(ar.Serialize("accuracyType", _accuracyType, 1, 4.0))
    PARAM_CHECK(ar.Serialize("timeType", _timeType, 1))
    PARAM_CHECK(ar.Serialize("time", _time, 1))
    PARAM_CHECK(ar.Serialize("precisionPos", _precisionPos, 1, 0))

    if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassFirst)
    {
        _exposure = false; // AIMap is not serialized
    }
    return LSOK;
}

float AITargetInfo::FadingSideAccuracy() const
{
    if (_timeSide < Glob.time - AccuracyLast - 2)
    {
        return 0;
    }
    const float invT = 1.0 / AccuracyLast;
    float fade = 1 - (Glob.time - _timeSide - 2) * invT;
    saturate(fade, 0, 1);
    return _accuracySide * fade;
}

float AITargetInfo::FadingTypeAccuracy() const
{
    if (_timeType < Glob.time - AccuracyLast - 2)
    {
        return 0;
    }
    const float invT = 1.0 / AccuracyLast;
    float fade = 1 - (Glob.time - _timeType - 2) * invT;
    saturate(fade, 0, 1);
    return _accuracyType * fade;
}

float AITargetInfo::FadingPositionAccuracy() const
{
    if (_time < Glob.time - AccuracyLast - 2)
    {
        return 0;
    }
    const float invT = 1.0 / AccuracyLast;
    float fade = 1 - (Glob.time - _time - 2) * invT;
    saturate(fade, 0, 1);
    return fade;
}

LSError AIGuardingGroup::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.SerializeRef("Group", group, 1))
    PARAM_CHECK(ar.Serialize("guarding", guarding, 1, false))
    return LSOK;
}

LSError AIGurdedVehicle::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.SerializeRef("Vehicle", vehicle, 1))
    PARAM_CHECK(ar.SerializeRef("By", by, 1))
    return LSOK;
}

LSError AIGurdedPoint::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.Serialize("position", position, 1))
    PARAM_CHECK(ar.SerializeRef("By", by, 1))
    return LSOK;
}

LSError AISupportTarget::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.SerializeRef("Group", group, 1))
    PARAM_CHECK(ar.Serialize("pos", pos, 1))
    PARAM_CHECK(ar.Serialize("heal", heal, 1))
    PARAM_CHECK(ar.Serialize("repair", repair, 1))
    PARAM_CHECK(ar.Serialize("rearm", rearm, 1))
    PARAM_CHECK(ar.Serialize("refuel", refuel, 1))
    return LSOK;
}

LSError AISupportGroup::Serialize(ParamArchive& ar)
{
    PARAM_CHECK(ar.SerializeRef("Group", group, 1))
    PARAM_CHECK(ar.SerializeRef("Assigned", assigned, 1))
    PARAM_CHECK(ar.Serialize("heal", heal, 1))
    PARAM_CHECK(ar.Serialize("repair", repair, 1))
    PARAM_CHECK(ar.Serialize("rearm", rearm, 1))
    PARAM_CHECK(ar.Serialize("refuel", refuel, 1))
    return LSOK;
}

AICheckPointInfo::AICheckPointInfo()
{
    _missions = 0;
    _time = Time(0);
}

AICheckPointInfo::AICheckPointInfo(Vector3Val pos, Time time)
{
    _pos = pos;
    _x = toIntFloor(pos.X() * InvLandGrid);
    _z = toIntFloor(pos.Z() * InvLandGrid);
    _missions = 0;
    _time = time;
}

#define DIRTY_ENEMY 0x01
#define DIRTY_UNKNOWN 0x10

struct AITargetChange
{
    bool erase;
    bool enemy;
    TargetId idExact;
    const VehicleType* type;
    int x;
    int z;
    Time time;
};
} // namespace Poseidon

#include <Poseidon/Foundation/Containers/Array2D.hpp>

namespace Poseidon
{
class AIMap
{
  public:
    Array2D<BYTE> _dirty;
    AutoArray<BYTE> _dirtyRow;
    AutoArray<AITargetChange> _changesQueue;
    Array2D<AIThreat> _exposureEnemy;
    Array2D<AIThreat> _exposureUnknown;

    AIMap() { Init(); }
    void Init();
    void AddChange(bool erase, bool enemy, const AITargetInfo& info);
    bool ProcessChange();

  protected:
    void SetDirty(int x, int z, BYTE dirty);
};

void AIMap::Init()
{
    // realloc all arrays
    _dirty.Dim(LandRange, LandRange);
    _dirtyRow.Realloc(LandRange);
    _dirtyRow.Resize(LandRange);
    _exposureEnemy.Dim(LandRange, LandRange);
    _exposureUnknown.Dim(LandRange, LandRange);

    for (int i = 0; i < LandRange; i++)
    {
        for (int j = 0; j < LandRange; j++)
        {
            _exposureEnemy(j, i).packed = 0;
            _exposureUnknown(j, i).packed = 0;
            _dirty(j, i) = 0;
        }
        _dirtyRow[i] = 0;
    }
}

void AIMap::SetDirty(int x, int z, BYTE dirty)
{
    _dirty(x, z) |= dirty;
    _dirtyRow[z] |= dirty;
}

void AIMap::AddChange(bool erase, bool enemy, const AITargetInfo& info)
{
    if (erase && info._idExact != nullptr)
    {
        // try to eliminate pairs insert - delete
        int i;
        for (i = _changesQueue.Size() - 1; i >= 0; i--)
        {
            AITargetChange& change = _changesQueue[i];
            if (change.idExact == info._idExact)
            {
                if (!change.erase && change.enemy == enemy && change.type == info._type)
                {
                    // pair insert - delete found
                    // delete after insert - unit destroyed sooner insert applied
                    _changesQueue.Delete(i);
                    return;
                }
                else
                {
                    break;
                }
            }
        }
    }

    int index = _changesQueue.Add();
    AITargetChange& change = _changesQueue[index];
    change.erase = erase;
    change.enemy = enemy;
    change.idExact = info._idExact;
    change.type = info._type;
    change.x = info._x;
    change.z = info._z;
    change.time = Glob.time;
}

bool AIMap::ProcessChange()
{
    AI_ERROR(_dirty.GetXRange() == LandRange);
    AI_ERROR(_dirty.GetYRange() == LandRange);

    if (_changesQueue.Size() < 1)
    {
        return false; // done
    }
    AITargetChange& info = _changesQueue[0];
    if (Glob.time < info.time)
    {
        return false; // cannot continue
    }
    int x = info.x;
    int z = info.z;
    const VehicleType* type = info.type;
    BYTE dirty;
    if (info.enemy)
    {
        dirty = DIRTY_ENEMY;
    }
    else
    {
        dirty = DIRTY_UNKNOWN;
    }
    int i, j, r;
    int xMin, xMax, zMin, zMax;

    float maxRange = -FLT_MAX;
    for (int w = 0; w < type->NWeaponSystems(); w++)
    {
        const WeaponType* weapon = type->GetWeaponSystem(w);
        for (int i = 0; i < weapon->_muzzles.Size(); i++)
        {
            const MuzzleType* muzzle = weapon->_muzzles[i];
            const MagazineType* magazine = muzzle->_typicalMagazine;
            if (!magazine)
            {
                continue;
            }
            for (int j = 0; j < magazine->_modes.Size(); j++)
            {
                const AmmoType* ammo = magazine->_modes[j]->_ammo;
                if (ammo)
                {
                    saturateMax(maxRange, ammo->maxRange);
                }
            }
        }
    }
    if (maxRange < 0)
    {
        goto ExitProcessChange;
    }
    r = toIntCeil(maxRange * InvLandGrid);

    xMin = x - r;
    xMax = x + r;
    zMin = z - r;
    zMax = z + r;
    if (xMin >= LandRange)
    {
        goto ExitProcessChange;
    }
    if (xMax < 0)
    {
        goto ExitProcessChange;
    }
    if (zMin >= LandRange)
    {
        goto ExitProcessChange;
    }
    if (zMax < 0)
    {
        goto ExitProcessChange;
    }
    saturateMax(xMin, 0);
    saturateMin(xMax, LandRangeMask);
    saturateMax(zMin, 0);
    saturateMin(zMax, LandRangeMask);

    for (i = zMin; i <= zMax; i++)
    {
        for (j = xMin; j <= xMax; j++)
        {
            float dist2 = Square(j - x) + Square(i - z);
            if (dist2 > r * r)
            {
                continue;
            }
            dist2 *= Square(LandGrid);

            AIThreat* expInMap;
            if (info.enemy)
            {
                expInMap = &_exposureEnemy(j, i);
            }
            else
            {
                expInMap = &_exposureUnknown(j, i);
            }
            SetDirty(j, i, dirty);
        }
    }
ExitProcessChange:
    _changesQueue.Delete(0);
    return true;
}

} // namespace Poseidon

#pragma warning(disable : 4355)

namespace Poseidon
{
AICenter::AICenter(TargetSide side, Mode mode) : _radio(CCSide, this, RNRadio)
{
    _nSoldier = 0;
    _nWoman = 0;

    _side = side;

    _mode = mode;

    _row = 0;
    _column = 0;
    _resources = 0;

    if (side < TSideUnknown)
    {
        _map = new AIMap();
    }
    else
    {
        _map = nullptr;
    }

    _guardingValid = Glob.time - 1.0f;
    _supportValid = Glob.time - 1.0f;
}

template <>
const EnumName* Foundation::GetEnumNames(EndMode dummy)
{
    static const EnumName EndModeNames[] = {EnumName(EMContinue, "CONTINUE"), EnumName(EMKilled, "KILLED"),
                                            EnumName(EMLoser, "LOSER"),       EnumName(EMEnd1, "END1"),
                                            EnumName(EMEnd2, "END2"),         EnumName(EMEnd3, "END3"),
                                            EnumName(EMEnd4, "END4"),         EnumName(EMEnd5, "END5"),
                                            EnumName(EMEnd6, "END6"),         EnumName()};
    return EndModeNames;
}

template <>
const EnumName* Foundation::GetEnumNames(AICenter::Mode dummy)
{
    static const EnumName CenterModeNames[] = {EnumName(AICMDisabled, "DISABLED"),
                                               EnumName(AICMArcade, "ARCADE"),
                                               EnumName(AICMIntro, "INTRO"),
                                               EnumName(AICMNetwork, "NETWORK"),
                                               EnumName(AICMArcade, "NORMAL"),
                                               EnumName(AICMArcade, "TRAINING"),
                                               EnumName()};
    return CenterModeNames;
}

LSError AICenter::Serialize(ParamArchive& ar)
{
    if (ar.GetArVersion() >= 6)
    {
        PARAM_CHECK(ar.Serialize("Groups", _groups, 6))
    }
    else if (ar.IsLoading())
    {
        _groups.Resize(28);
        for (int i = 0; i < 28; i++)
        {
            char buffer[256];
            ::sprintf(buffer, "Group%d", i);
            PARAM_CHECK(ar.Serialize(buffer, _groups[i], 1))
        }
    }
    else
    {
        return LSStructure;
    }
    PARAM_CHECK(ar.Serialize("Radio", _radio, 1))

    PARAM_CHECK(ar.SerializeEnum("mode", _mode, 1, (AICenterMode)AICMArcade))
    PARAM_CHECK(ar.Serialize("lastUpdateTime", _lastUpdateTime, 1))
    PARAM_CHECK(ar.SerializeEnum("side", _side, 1))
    PARAM_CHECK(ar.Serialize("resources", _resources, 1, 0))

    for (int i = 0; i < TSideUnknown; i++)
    {
        char buffer[256];
        ::sprintf(buffer, "friends%d", i);
        PARAM_CHECK(ar.Serialize(buffer, _friends[i], 1, 1.0))
    }

    PARAM_CHECK(ar.Serialize("Targets", _targets, 1))

    PARAM_CHECK(ar.Serialize("soldier", _nSoldier, 1))
    PARAM_CHECK(ar.Serialize("woman", _nWoman, 1, 0))

    PARAM_CHECK(ar.Serialize("GuardingGroups", _guardingGroups, 1))
    PARAM_CHECK(ar.Serialize("GuardingVehicles", _guardedVehicles, 1))
    PARAM_CHECK(ar.Serialize("GuardingPoints", _guardedPoints, 1))
    PARAM_CHECK(ar.Serialize("guardingValid", _guardingValid, 1))

    PARAM_CHECK(ar.Serialize("SupportGroups", _supportGroups, 1))
    PARAM_CHECK(ar.Serialize("SupportTargets", _supportTargets, 1))
    PARAM_CHECK(ar.Serialize("supportValid", _supportValid, 1, Glob.time))

    // Recreate strategic map
    if (ar.IsLoading() && ar.GetPass() == ParamArchive::PassSecond)
    {
        _row = 0;
        _column = 0;
        if (_side < TSideUnknown)
        {
            _map = new AIMap();
            _map->Init();
            AddNewExposures();
            while (_map->ProcessChange())
            {
                ;
            }
        }
        else
        {
            _map = nullptr;
        }
    }

    return LSOK;
}

AICenter* AICenter::LoadRef(ParamArchive& ar)
{
    TargetSide side = TSideUnknown;
    if (ar.SerializeEnum("side", side, 1) != LSOK)
    {
        return nullptr;
    }
    switch (side)
    {
        case TWest:
            return GWorld->GetWestCenter();
        case TEast:
            return GWorld->GetEastCenter();
        case TGuerrila:
            return GWorld->GetGuerrilaCenter();
        case TCivilian:
            return GWorld->GetCivilianCenter();
        case TLogic:
            return GWorld->GetLogicCenter();
    }
    return nullptr;
}

LSError AICenter::SaveRef(ParamArchive& ar) const
{
    TargetSide side = GetSide();
    PARAM_CHECK(ar.SerializeEnum("side", side, 1))
    return LSOK;
}

NetworkMessageType AICenter::GetNMType(NetworkMessageClass cls) const
{
    switch (cls)
    {
        case NMCCreate:
            return NMTCreateAICenter;
        case NMCUpdateGeneric:
            return NMTUpdateAICenter;
        default:
            return NMTNone;
    }
}

#define CREATE_AI_CENTER_MSG(XX) \
	XX(int, side, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, TEast), DOC_MSG("Side of center"), IdxTransfer)

DECLARE_NET_INDICES_EX(CreateAICenter, NetworkObject, CREATE_AI_CENTER_MSG)
DEFINE_NET_INDICES_EX(CreateAICenter, NetworkObject, CREATE_AI_CENTER_MSG)

} // namespace Poseidon

DEFINE_GET_INDICES(CreateAICenter)

namespace Poseidon
{

#define UPDATE_AI_CENTER_MSG(XX) \
	XX(float, friends0, NDTFloat, NCTNone, DEFVALUE(float, 1.0), DOC_MSG("Friendship to other side"), IdxTransfer) \
	XX(float, friends1, NDTFloat, NCTNone, DEFVALUE(float, 1.0), DOC_MSG("Friendship to other side"), IdxTransfer) \
	XX(float, friends2, NDTFloat, NCTNone, DEFVALUE(float, 1.0), DOC_MSG("Friendship to other side"), IdxTransfer) \
	XX(float, friends3, NDTFloat, NCTNone, DEFVALUE(float, 1.0), DOC_MSG("Friendship to other side"), IdxTransfer)

DECLARE_NET_INDICES_EX(UpdateAICenter, NetworkObject, UPDATE_AI_CENTER_MSG)
DEFINE_NET_INDICES_EX(UpdateAICenter, NetworkObject, UPDATE_AI_CENTER_MSG)

} // namespace Poseidon

DEFINE_GET_INDICES(UpdateAICenter)

namespace Poseidon
{

NetworkMessageFormat& AICenter::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    switch (cls)
    {
        case NMCCreate:
            NetworkObject::CreateFormat(cls, format);
            CREATE_AI_CENTER_MSG(MSG_FORMAT)
            break;
        case NMCUpdateGeneric:
            NetworkObject::CreateFormat(cls, format);
            UPDATE_AI_CENTER_MSG(MSG_FORMAT)
            break;
        default:
            NetworkObject::CreateFormat(cls, format);
            break;
    }
    return format;
}

AICenter* AICenter::CreateObject(NetworkMessageContext& ctx)
{
    AI_ERROR(dynamic_cast<const IndicesCreateAICenter*>(ctx.GetIndices()))
    const IndicesCreateAICenter* indices = static_cast<const IndicesCreateAICenter*>(ctx.GetIndices());

    TargetSide side;
    if (ctx.IdxTransfer(indices->side, (int&)side) != TMOK)
    {
        return nullptr;
    }
    AICenter* center = new AICenter(side, AICMNetwork);
    GWorld->AddCenter(center);

    NetworkId objectId;
    if (ctx.IdxTransfer(indices->objectCreator, objectId.creator) != TMOK)
    {
        return nullptr;
    }
    if (ctx.IdxTransfer(indices->objectId, objectId.id) != TMOK)
    {
        return nullptr;
    }
    center->SetNetworkId(objectId);
    center->SetLocal(false);

    center->TransferMsg(ctx);

    return center;
}

void AICenter::DestroyObject()
{
    AI_ERROR(NGroups() == 0);
    GWorld->RemoveCenter(this);
}

TMError AICenter::TransferMsg(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCCreate:
            TMCHECK(NetworkObject::TransferMsg(ctx))
            if (ctx.IsSending())
            {
                AI_ERROR(dynamic_cast<const IndicesCreateAICenter*>(ctx.GetIndices()))
                const IndicesCreateAICenter* indices = static_cast<const IndicesCreateAICenter*>(ctx.GetIndices());

                ITRANSF_ENUM(side)
            }
            break;
        case NMCUpdateGeneric:
            TMCHECK(NetworkObject::TransferMsg(ctx))
            {
                AI_ERROR(dynamic_cast<const IndicesUpdateAICenter*>(ctx.GetIndices()))
                const IndicesUpdateAICenter* indices = static_cast<const IndicesUpdateAICenter*>(ctx.GetIndices());

                TMCHECK(ctx.IdxTransfer(indices->friends0, _friends[0]))
                TMCHECK(ctx.IdxTransfer(indices->friends1, _friends[1]))
                TMCHECK(ctx.IdxTransfer(indices->friends2, _friends[2]))
                TMCHECK(ctx.IdxTransfer(indices->friends3, _friends[3]))
            }
            break;
        default:
            TMCHECK(NetworkObject::TransferMsg(ctx))
            break;
    }
    return TMOK;
}

float AICenter::CalculateError(NetworkMessageContext& ctx)
{
    return 0.0;
}

AICenter* CreateCenter(ArcadeTemplate& t, TargetSide side, AICenter::Mode mode)
{
    switch (mode)
    {
        case AICMNetwork:
            return new AICenter(side, mode);
        case AICMArcade:
        case AICMIntro:
        {
            int n = t.groups.Size();
            for (int i = 0; i < n; i++)
            {
                if (t.groups[i].side == side)
                {
                    return new AICenter(side, mode);
                }
            }
            return nullptr;
        }
    }
    return nullptr;
}

int AICenter::GetLanguage() const
{
    switch (_side)
    {
        case TWest:
            return English;
        case TEast:
            return Czech;
        case TGuerrila:
            return English;
        case TCivilian:
            return English;
        case TLogic:
            return English;
        default:
            Fail("Side");
            return English;
    }
}

bool AICenter::IsFriendly(TargetSide side) const
{
    if (side == TFriendly)
    {
        return true;
    }
    if (side == TEnemy)
    {
        return false;
    }
    if (side < 0 || side >= TSideUnknown)
    {
        return false;
    }

    return _friends[side] >= 0.6;
}

bool AICenter::IsNeutral(TargetSide side) const
{
    return false;
}

bool AICenter::IsEnemy(TargetSide side) const
{
    if (side == TFriendly)
    {
        return false;
    }
    if (side == TEnemy)
    {
        return true;
    }
    if (side < 0 || side >= TSideUnknown)
    {
        return false;
    }

    return _friends[side] < 0.6;
}

bool AICenter::IsUnknown(TargetSide side) const
{
    return side == TSideUnknown;
}

bool AICenter::IsCivilian(TargetSide side) const
{
    return side == TCivilian;
}

bool AICenter::IsWest(TargetSide side) const
{
    return side == TWest;
}

bool AICenter::IsEast(TargetSide side) const
{
    return side == TEast;
}

bool AICenter::IsResistance(TargetSide side) const
{
    return side == TGuerrila;
}

void AICenter::UpdateSupport()
{
    _supportValid = Glob.time + 5.0f + 2.0f * GRandGen.RandomValue();

    // 1. remove destroyed groups
    for (int i = 0; i < _supportTargets.Size();)
    {
        AISupportTarget& target = _supportTargets[i];
        if (!target.group)
        {
            _supportTargets.Delete(i);
        }
        else if (target.group->NUnits() == 0)
        {
            SupportDone(target.group);
        }
        else
        {
            i++;
        }
    }

    // 2. assign groups to targets
    for (int i = 0; i < _supportGroups.Size(); i++)
    {
        AISupportGroup& group = _supportGroups[i];
        if (!group.group || !group.group->Leader())
        {
            continue;
        }
        if (group.assigned)
        {
            continue;
        }
        Vector3Val posG = group.group->Leader()->Position();

        // find target to assign
        AISupportTarget* best = nullptr;
        float minDist2 = FLT_MAX;
        for (int j = 0; j < _supportTargets.Size(); j++)
        {
            AISupportTarget& target = _supportTargets[j];
            if (!target.group->Leader())
            {
                continue;
            }
            if (target.heal && group.heal || target.repair && group.repair || target.refuel && group.refuel ||
                target.rearm && group.rearm)
            {
                float dist2 = target.group->Leader()->Position().Distance2(posG);
                if (dist2 < minDist2)
                {
                    best = &target;
                    minDist2 = dist2;
                }
            }
        }

        if (best)
        {
            // assign target
            group.assigned = best->group;
            if (group.heal)
            {
                best->heal = false;
            }
            if (group.repair)
            {
                best->repair = false;
            }
            if (group.refuel)
            {
                best->refuel = false;
            }
            if (group.rearm)
            {
                best->rearm = false;
            }
            group.group->Support(group.assigned, group.assigned->Leader()->Position());
        }
    }
}

void AICenter::ReadyForSupport(AIGroup* grp)
{
    if (!grp->NUnits())
    {
        return;
    }

    int index = -1;
    for (int i = 0; i < _supportGroups.Size(); i++)
    {
        if (_supportGroups[i].group == grp)
        {
            index = i;
            break;
        }
    }

    if (index < 0)
    {
        index = _supportGroups.Add();
        _supportGroups[index].group = grp;
    }

    _supportGroups[index].heal = false;
    _supportGroups[index].rearm = false;
    _supportGroups[index].refuel = false;
    _supportGroups[index].repair = false;

    for (int i = 0; i < MAX_UNITS_PER_GROUP; i++)
    {
        AIUnit* unit = grp->UnitWithID(i + 1);
        if (!unit || unit->GetLifeState() != AIUnit::LSAlive)
        {
            continue;
        }

        Transport* transport = unit->GetVehicleIn();
        if (transport && !transport->IsDammageDestroyed())
        {
            if (transport->IsAttendant())
            {
                _supportGroups[index].heal = true;
            }
            if (transport->GetRepairCargo() > 10)
            {
                _supportGroups[index].repair = true;
            }
            if (transport->GetFuelCargo() > 10)
            {
                _supportGroups[index].refuel = true;
            }
            if (transport->GetAmmoCargo() > 10)
            {
                _supportGroups[index].rearm = true;
            }
        }
        if (unit->GetPerson()->GetType()->IsAttendant())
        {
            _supportGroups[index].heal = true;
        }
    }
}

void AICenter::AskSupport(AIGroup* grp, UIActionType type)
{
    if (!grp->Leader())
    {
        return;
    }

    int index = -1;
    for (int i = 0; i < _supportTargets.Size(); i++)
    {
        if (_supportTargets[i].group == grp)
        {
            index = i;
            break;
        }
    }

    if (index < 0)
    {
        index = _supportTargets.Add();
        _supportTargets[index].group = grp;
        _supportTargets[index].heal = false;
        _supportTargets[index].repair = false;
        _supportTargets[index].refuel = false;
        _supportTargets[index].rearm = false;
    }

    _supportTargets[index].pos = grp->Leader()->Position();
    switch (type)
    {
        case ATHeal:
            _supportTargets[index].heal = true;
            break;
        case ATRepair:
            _supportTargets[index].repair = true;
            break;
        case ATRefuel:
            _supportTargets[index].refuel = true;
            break;
        case ATRearm:
            _supportTargets[index].rearm = true;
            break;
        default:
            Fail("Action");
            break;
    }
}
} // namespace Poseidon
