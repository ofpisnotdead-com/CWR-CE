#include <Poseidon/Foundation/Platform/VersionNo.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Foundation/Platform/AppConfig.hpp>
#include <Poseidon/Network/NetworkServerCommon.hpp>
#include <Poseidon/Foundation/Enums/EnumNames.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Core/ModSystem.hpp>
#include <Poseidon/Core/Version.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <Poseidon/Core/Config/Config.hpp>
#include <Poseidon/Network/NetworkImpl.hpp>
#include <Poseidon/Network/NetworkMissionTransfer.hpp>
#include <Poseidon/Network/NetworkServerAuth.hpp>
#include <Poseidon/Network/NetworkScriptValueCodec.hpp>
#include <Poseidon/Core/Global.hpp>
// #include "strIncl.hpp"
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/Foundation/Platform/GamePaths.hpp>
#include <float.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <string>
#include <system_error>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Memory/MemAlloc.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>

#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/Memtype.h>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>
#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

typedef int SOCKET;
#define closesocket close
#endif

// Must apply on every platform: the server/mission helpers below (GetMPMissionsDir,
// GetCurrentTemplate, …) live in namespace Poseidon. Trapped in the !_WIN32 block
// above it was Linux-only, so the Windows build couldn't see them.
using namespace Poseidon;

#include <Poseidon/Network/NetworkConfig.hpp>

using Poseidon::Foundation::GetEnumValue;

namespace Poseidon
{
RString GetRankingLog();
RString GetPidFileName();
extern bool MyPidFile;
} // namespace Poseidon

namespace
{
void AddRemoteExecAllowedNames(AutoArray<RString>& names, const ParamEntry* parent)
{
    if (!parent)
    {
        return;
    }
    for (int i = 0; i < parent->GetEntryCount(); ++i)
    {
        const ParamEntry& entry = parent->GetEntry(i);
        if (!entry.IsClass())
        {
            continue;
        }
        RString name = entry.GetName();
        if (!Poseidon::RemoteExecNameAllowed(names, name))
        {
            names.Add(name);
        }
    }
}

Poseidon::RemoteExecPolicyMode ParseRemoteExecPolicyMode(const ParamEntry* cfg)
{
    const ParamEntry* mode = cfg ? cfg->FindEntry("mode") : nullptr;
    if (!mode)
    {
        return Poseidon::RemoteExecPolicyMode::DenyClient;
    }
    int value = *mode;
    if (value <= 0)
    {
        return Poseidon::RemoteExecPolicyMode::DenyClient;
    }
    if (value >= 2)
    {
        return Poseidon::RemoteExecPolicyMode::AllowAll;
    }
    return Poseidon::RemoteExecPolicyMode::AllowListed;
}
} // namespace

#include <Poseidon/AI/ArcadeTemplate.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/Network/XML/Xml.hpp>
#include <Poseidon/Foundation/Strings/StrFormat.hpp>

#include <Poseidon/Foundation/Algorithms/Qsort.hpp>
#include <Poseidon/Foundation/Common/Filenames.hpp>

#include <Poseidon/World/Entities/Vehicles/AllAIVehicles.hpp>
#include <Poseidon/World/Entities/Vehicles/SeaGull.hpp>
#include <Poseidon/World/Detection/Detector.hpp>
#include <Poseidon/World/Entities/Weapons/Shots.hpp>

#include <Poseidon/Dev/Debug/DebugTrap.hpp>

#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/IO/PackFiles.hpp>
#include <Poseidon/IO/FileServer.hpp>

#include <Random/randomGen.hpp>
#include <Poseidon/Game/Commands/GameStateExt.hpp>

#include <Poseidon/Core/Progress.hpp>

#include <Poseidon/Game/UiActions.hpp>

#include <Poseidon/Foundation/Algorithms/Crc.hpp>
#include <Poseidon/Foundation/Strings/Bstring.hpp>

#ifdef _WIN32
#include <io.h>
#include <winsock2.h>
#include <process.h>
#endif

namespace Poseidon
{
RString GetUserDirectory();
RString GetMissionDirectory();
} // namespace Poseidon

using Poseidon::Foundation::EnumName;
using Poseidon::Foundation::MemAllocSA;
using Poseidon::Foundation::QSort;
using Poseidon::Foundation::Time;

#define LOG_SEND_PROCESS 0
#define LOG_ERRORS 0
const float LogErrorLimit = 1.0f;
extern const char* GameStateNames[];

std::vector<std::string> Poseidon::GetMPMissionLookupDirectories()
{
    std::vector<std::string> dirs;

    struct ActiveModMissionDirs
    {
        std::vector<std::string>* dirs;
    } ctx{&dirs};
    ModSystem::EnumDirectories(
        [](RStringB dir, void* context) -> bool
        {
            if (dir.GetLength() == 0)
            {
                return false;
            }
            auto* active = static_cast<ActiveModMissionDirs*>(context);
            active->dirs->push_back((std::filesystem::path((const char*)dir) / GameDirs::MPMissions).string());
            active->dirs->push_back((std::filesystem::path((const char*)dir) / "mpmissions").string());
            return false;
        },
        &ctx);

    dirs.emplace_back((const char*)GetMPMissionsDir());

    if (!AppConfig::Instance().IsSimulateMode())
    {
        const std::string userDir = GamePaths::Instance().MPMissionsDir();
        if (!userDir.empty() && std::find(dirs.begin(), dirs.end(), userDir) == dirs.end())
        {
            dirs.emplace_back(userDir);
        }
    }
    return dirs;
}

static std::string LowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

RString Poseidon::ResolveMPMissionTemplateBase(RString mission, RString world)
{
    const std::string missionName = (const char*)mission;
    const std::string worldName = (const char*)world;
    std::vector<std::string> templateNames;
    templateNames.push_back(missionName + "." + worldName);

    const std::string lowerWorld = LowerCopy(worldName);
    if (lowerWorld != worldName)
    {
        templateNames.push_back(missionName + "." + lowerWorld);
    }

    for (const std::string& dir : GetMPMissionLookupDirectories())
    {
        for (const std::string& templateName : templateNames)
        {
            std::filesystem::path base = std::filesystem::path(dir) / templateName;
            std::error_code ec;
            if (std::filesystem::exists(base.string() + ".pbo", ec))
            {
                LOG_DEBUG(Network, "MP mission template resolved: {} -> {}", templateName, base.string());
                return base.string().c_str();
            }
        }
    }

    RString fallback = GetMPMissionsDir() + mission + RString(".") + world;
    LOG_DEBUG(Network, "MP mission template using fallback: {} -> {}", templateNames.front(), (const char*)fallback);
    return fallback;
}

void NetworkServer::CreateMission(RString mission, RString world)
{
    _originalName = mission;
    if (mission.GetLength() > 0)
    {
        RString filename = ResolveMPMissionTemplateBase(mission, world);
        _missionBank = filename + RString(".pbo");
        CreateMPMissionBank(filename, world);
    }
    else
    {
        _missionBank = RString();
        RemoveBank(GameDirs::MPCurrentPrefix().c_str());
    }
}

// Map RespawnMode enum values to its text representation
template <>
const EnumName* Poseidon::Foundation::GetEnumNames(RespawnMode dummy)
{
    static const EnumName RespawnNames[] = {EnumName(RespawnNone, "NONE"),
                                            EnumName(RespawnSeaGull, "BIRD"),
                                            EnumName(RespawnAtPlace, "INSTANT"),
                                            EnumName(RespawnInBase, "BASE"),
                                            EnumName(RespawnToGroup, "GROUP"),
                                            EnumName(RespawnToFriendly, "SIDE"),
                                            EnumName()};
    return RespawnNames;
}

void NetworkServer::InitMission(bool cadetMode)
{
    // unlock session
    _sessionLocked = false;

    // clear JIP message queue from previous mission
    _jipMessages.Resize(0);

    // extern RString GetBaseDirectory();
    _missionHeader.estimatedEndTime = TIME_MIN;

    // mission description
    _missionHeader.island = Glob.header.worldname;
    _missionHeader.name = Localize(CurrentTemplate.intel.briefingName);
    if (_missionHeader.name.GetLength() == 0)
    {
        _missionHeader.name = _originalName.GetLength() > 0 ? _originalName : Glob.header.filename;
    }
    _missionHeader.description = CurrentTemplate.intel.briefingDescription;

    // checks of addons
    _missionHeader.addOns = CurrentTemplate.addOns;

    // disable AI
    _missionHeader.disabledAI = false;
    const ParamEntry* disabledAI = ExtParsMission.FindEntry("disabledAI");
    if (disabledAI)
    {
        _missionHeader.disabledAI = *disabledAI;
    }

    // difficulty
    _missionHeader.cadetMode = cadetMode;
    if (cadetMode)
    {
        for (int i = 0; i < DTN; i++)
        {
            _missionHeader.difficulty[i] = USER_CONFIG.cadetDifficulty[i];
        }
    }
    else
    {
        for (int i = 0; i < DTN; i++)
        {
            _missionHeader.difficulty[i] = USER_CONFIG.veteranDifficulty[i];
        }
    }

    // mission respawn info
    _missionHeader.respawn = RespawnSeaGull;
    _missionHeader.respawnDelay = 0;
    const ParamEntry* respawn = ExtParsMission.FindEntry("respawn");
    if (respawn)
    {
        // check if it is numeric name
        RStringB respawnMode = *respawn;
        int mode = GetEnumValue<RespawnMode>(respawnMode);
        if (mode < 0)
        {
            mode = *respawn;
        }
        if (mode < 0 || mode > RespawnToFriendly)
        {
            mode = RespawnNone;
        }
        _missionHeader.respawn = (RespawnMode)mode;
    }
    respawn = ExtParsMission.FindEntry("respawnDelay");
    if (respawn)
    {
        _missionHeader.respawnDelay = *respawn;
    }

    _missionHeader.aiKills = false;
    const ParamEntry* aiKills = ExtParsMission.FindEntry("aiKills");
    if (aiKills)
    {
        _missionHeader.aiKills = *aiKills;
    }

    _missionHeader.joinInProgress = false;
    const ParamEntry* jip = ExtParsMission.FindEntry("joinInProgress");
    if (jip)
    {
        _missionHeader.joinInProgress = *jip;
    }
    if (AppConfig::Instance().GetForceJIP())
    {
        _missionHeader.joinInProgress = true;
        LOG_INFO(Network, "[force-jip] JIP enabled by --force-jip");
    }

    _remoteExecAllowedNames.Resize(0);
    _remoteExecPolicyMode = RemoteExecPolicyMode::DenyClient;
    const ParamEntry* remoteExecCfg = ExtParsMission.FindEntry("CfgRemoteExec");
    if (remoteExecCfg)
    {
        _remoteExecPolicyMode = ParseRemoteExecPolicyMode(remoteExecCfg);
        AddRemoteExecAllowedNames(_remoteExecAllowedNames, remoteExecCfg->FindEntry("Functions"));
        AddRemoteExecAllowedNames(_remoteExecAllowedNames, remoteExecCfg->FindEntry("Commands"));
    }
    LOG_INFO(Network, "remoteExec policy: mode={} allowedNames={}", (int)_remoteExecPolicyMode,
             _remoteExecAllowedNames.Size());

    // mission parameters
    const ParamEntry* entry = ExtParsMission.FindEntry("titleParam1");
    _missionHeader.titleParam1 = entry ? (RString)*entry : "";
    _missionHeader.valuesParam1.Resize(0);
    _missionHeader.textsParam1.Resize(0);
    entry = ExtParsMission.FindEntry("valuesParam1");
    if (entry)
    {
        int n = entry->GetSize();
        _missionHeader.valuesParam1.Resize(n);
        for (int i = 0; i < n; i++)
        {
            _missionHeader.valuesParam1[i] = (*entry)[i];
        }

        const ParamEntry* entryTexts = ExtParsMission.FindEntry("textsParam1");
        if (!entryTexts || entryTexts->GetSize() != n)
        {
            entryTexts = entry;
        }

        if (entryTexts)
        {
            _missionHeader.textsParam1.Resize(n);
            for (int i = 0; i < n; i++)
            {
                _missionHeader.textsParam1[i] = (*entryTexts)[i];
            }
        }
    }
    entry = ExtParsMission.FindEntry("defValueParam1");
    _missionHeader.defValueParam1 = entry ? *entry : 0;

    entry = ExtParsMission.FindEntry("titleParam2");
    _missionHeader.titleParam2 = entry ? (RString)*entry : "";
    _missionHeader.valuesParam2.Resize(0);
    _missionHeader.textsParam2.Resize(0);
    entry = ExtParsMission.FindEntry("valuesParam2");
    if (entry)
    {
        int n = entry->GetSize();
        _missionHeader.valuesParam2.Resize(n);
        for (int i = 0; i < n; i++)
        {
            _missionHeader.valuesParam2[i] = (*entry)[i];
        }

        const ParamEntry* entryTexts = ExtParsMission.FindEntry("textsParam2");
        if (!entryTexts || entryTexts->GetSize() != n)
        {
            entryTexts = entry;
        }

        if (entryTexts)
        {
            _missionHeader.textsParam2.Resize(n);
            for (int i = 0; i < n; i++)
            {
                _missionHeader.textsParam2[i] = (*entryTexts)[i];
            }
        }
    }
    entry = ExtParsMission.FindEntry("defValueParam2");
    _missionHeader.defValueParam2 = entry ? *entry : 0;

    const NetworkMissionParams resolvedParams =
        ResolveNetworkMissionParams(_param1, _param2, _missionHeader.defValueParam1, _missionHeader.defValueParam2);
    _param1 = resolvedParams.param1;
    _param2 = resolvedParams.param2;

    // mission file
    QFBank* bank = ::FindBank(GameDirs::MPCurrentPrefix().c_str());
    LOG_DEBUG(Network, "[InitMission] FindBank('{}') -> {} (banks={})", GameDirs::MPCurrentPrefix(), bank != nullptr,
              GFileBanks.Size());
    if (bank)
    {
        // mission in bank
        _missionHeader.fileDir = GetMPMissionsDir();
        _missionHeader.fileName = _originalName + RString(".") + _missionHeader.island;
        if (_missionBank.GetLength() == 0)
        {
            _missionBank = ResolveMPMissionTemplateBase(_originalName, _missionHeader.island) + RString(".pbo");
        }
    }
    else
    {
        _missionHeader.fileDir = RString("tmp/");
        _missionHeader.fileName = RString("__cur_mp");

        RString srcDir = GetServerTmpDir() + RString("/");
        _missionBank = srcDir + _missionHeader.fileName + RString(".pbo");

        ::CreateDirectory(srcDir, nullptr);
        ::DeleteFile(_missionBank);
        FileBankManager mgr;
        RString missionDir = Poseidon::GetMissionDirectory();
        LOG_INFO(Network, "[InitMission] Creating PBO: src='{}' dst='{}'", (const char*)missionDir,
                 (const char*)_missionBank);
        mgr.Create(_missionBank, missionDir, true);
    }
    {
        std::error_code ec;
        auto fileSize = std::filesystem::file_size(std::string(_missionBank), ec);
        if (!ec)
        {
            _missionHeader.fileSizeL = static_cast<DWORD>(fileSize & 0xffffffff);
            _missionHeader.fileSizeH = static_cast<DWORD>(fileSize >> 32);
        }
        else
        {
            _missionHeader.fileSizeL = 0;
            _missionHeader.fileSizeH = 0;
        }
    }

    {
        QIFStream f;
        f.open(_missionBank);
        Poseidon::Foundation::CRCCalculator crc;
        _missionHeader.fileCRC = crc.CRC(f.act(), f.rest());
    }

    // init roles for players
    _playerRoles.Resize(0);
    // disable AI
    int defaultPlayer = _missionHeader.disabledAI ? NO_PLAYER : AI_PLAYER;
    int east = 0, west = 0, guer = 0, civl = 0;
    for (int i = 0; i < CurrentTemplate.groups.Size(); i++)
    {
        ArcadeGroupInfo& gInfo = CurrentTemplate.groups[i];
        TargetSide side = gInfo.side;
        int grp;
        switch (side)
        {
            case TEast:
                grp = east++;
                break;
            case TWest:
                grp = west++;
                break;
            case TGuerrila:
                grp = guer++;
                break;
            case TCivilian:
                grp = civl++;
                break;
            default:
                continue;
        }

        PlayerRole role;
        role.side = side;
        role.group = grp;
        role.roleLocked = false;
        // disable AI
        role.player = defaultPlayer;
        for (int j = 0; j < gInfo.units.Size(); j++)
        {
            ArcadeUnitInfo& uInfo = gInfo.units[j];
            switch (uInfo.player)
            {
                case APPlayerCommander:
                {
                    role.unit = j;
                    role.vehicle = uInfo.vehicle;
                    VehicleType* type = dynamic_cast<VehicleType*>(VehicleTypes.New(uInfo.vehicle));
                    if (type && type->HasCommander())
                    {
                        role.position = PRPCommander;
                    }
                    else
                    {
                        role.position = PRPNone;
                    }
                    role.leader = uInfo.leader;
                    _playerRoles.Add(role);
                }
                break;
                case APPlayerDriver:
                    role.unit = j;
                    role.vehicle = uInfo.vehicle;
                    role.position = PRPDriver;
                    role.leader = false;
                    _playerRoles.Add(role);
                    break;
                case APPlayerGunner:
                    role.unit = j;
                    role.vehicle = uInfo.vehicle;
                    role.position = PRPGunner;
                    role.leader = false;
                    _playerRoles.Add(role);
                    break;
                case APPlayableC:
                    role.unit = j;
                    role.vehicle = uInfo.vehicle;
                    role.position = PRPCommander;
                    role.leader = uInfo.leader;
                    _playerRoles.Add(role);
                    break;
                case APPlayableD:
                    role.unit = j;
                    role.vehicle = uInfo.vehicle;
                    role.position = PRPDriver;
                    role.leader = false;
                    _playerRoles.Add(role);
                    break;
                case APPlayableG:
                    role.unit = j;
                    role.vehicle = uInfo.vehicle;
                    role.position = PRPGunner;
                    role.leader = false;
                    _playerRoles.Add(role);
                    break;
                case APPlayableCD:
                    role.unit = j;
                    role.vehicle = uInfo.vehicle;
                    role.position = PRPCommander;
                    role.leader = uInfo.leader;
                    _playerRoles.Add(role);
                    role.position = PRPDriver;
                    role.leader = false;
                    _playerRoles.Add(role);
                    break;
                case APPlayableCG:
                    role.unit = j;
                    role.vehicle = uInfo.vehicle;
                    role.position = PRPCommander;
                    role.leader = uInfo.leader;
                    _playerRoles.Add(role);
                    role.position = PRPGunner;
                    role.leader = false;
                    _playerRoles.Add(role);
                    break;
                case APPlayableDG:
                    role.unit = j;
                    role.vehicle = uInfo.vehicle;
                    role.position = PRPDriver;
                    role.leader = false;
                    _playerRoles.Add(role);
                    role.position = PRPGunner;
                    _playerRoles.Add(role);
                    break;
                case APPlayableCDG:
                    // all available
                    {
                        role.unit = j;
                        role.vehicle = uInfo.vehicle;
                        VehicleType* type = dynamic_cast<VehicleType*>(VehicleTypes.New(uInfo.vehicle));
                        NET_ERROR(type);
                        if (type->HasCommander())
                        {
                            role.position = PRPCommander;
                            role.leader = uInfo.leader;
                            _playerRoles.Add(role);
                            role.position = PRPDriver;
                            role.leader = false;
                            _playerRoles.Add(role);
                            if (type->HasGunner())
                            {
                                role.position = PRPGunner;
                                _playerRoles.Add(role);
                            }
                        }
                        else
                        {
                            if (type->HasGunner())
                            {
                                role.position = PRPGunner;
                                role.leader = false;
                                _playerRoles.Add(role);
                                role.position = PRPDriver;
                            }
                            else
                            {
                                role.position = PRPNone;
                            }
                            role.leader = uInfo.leader;
                            _playerRoles.Add(role);
                        }
                    }
                    break;
            }
        }
    }

    // send mission description and roles
    for (int i = 0; i < _players.Size(); i++)
    {
        NetworkPlayerInfo& info = _players[i];
        if (info.state < NGSCreate)
        {
            continue;
        }
        SendMissionInfo(info.dpid);
        info.state = NGSPrepareSide;
        SetPlayerState(info.dpid, NGSPrepareSide);
        info.missionFileValid = false;
    }
}

void NetworkServer::ChangeOwner(NetworkId& id, int from, int to)
{
    if (from == to)
    {
        return;
    }
    if (id.IsNull())
    {
        return;
    }

    NetworkObjectInfo* oInfo = GetObjectInfo(id);
    if (!oInfo)
    {
        RptF("Server: Object info %d:%d not found.", id.creator, id.id);
        return;
    }
    oInfo->owner = to;
    LOG_DEBUG(Network, "ChangeOwner: object {}:{} from={} to={}", id.creator, id.id, from, to);

    NetworkPlayerInfo* pInfo = GetPlayerInfo(to);
    if (pInfo)
    {
        for (int cls = NMCUpdateFirst; cls < NMCUpdateN; cls++)
        {
            UpdateObject(pInfo, oInfo, (NetworkMessageClass)cls, NMFGuaranteed);
        }
        //		pInfo->DeleteObjectInfo(id);
    }
    oInfo->DeletePlayerObjectInfo(from);

    // PlayerObjectInfo for object creates automatically

    ChangeOwnerMessage msg(id, to);
    SendMsg(from, &msg, NMFGuaranteed);
    SendMsg(to, &msg, NMFGuaranteed);
}

void NetworkServer::UpdateGroupLeader(NetworkId& group, NetworkId& leader)
{
    NetworkPlayerInfo* oldOwnerInfo = nullptr;
    NetworkPlayerInfo* newOwnerInfo = nullptr;
    for (int i = 0; i < _players.Size(); i++)
    {
        NetworkPlayerInfo& pInfo = _players[i];
        if (pInfo.group == group)
        {
            oldOwnerInfo = &pInfo;
        }
        if (pInfo.unit == leader)
        {
            newOwnerInfo = &pInfo;
        }
    }

    int oldOwner = oldOwnerInfo ? oldOwnerInfo->dpid : _botClient;
    int newOwner = newOwnerInfo ? newOwnerInfo->dpid : _botClient;

    // group leader owner changed

    // change group owner
    if (oldOwnerInfo)
    {
        oldOwnerInfo->group = NetworkId::Null();
    }
    if (newOwnerInfo)
    {
        newOwnerInfo->group = group;
    }
    ChangeOwner(group, oldOwner, newOwner);

    // change subgroups owner
    AutoArray<NetworkId> persons;
    for (int i = 0; i < _objects.Size(); i++)
    {
        NetworkObjectInfo& oInfo = *_objects[i];
        NetworkCurrentInfo& info = oInfo.current[NMCUpdateGeneric];
        if (!info.message)
        {
            continue;
        }
        if (info.type != NMTUpdateAISubgroup)
        {
            continue;
        }
        NetworkMessageFormatBase* format = GetFormat(info.type);
        NET_ERROR(format);
        NetworkMessageContext ctx(info.message, format, this, info.from, MSG_RECEIVE);

        NET_ERROR(dynamic_cast<const IndicesUpdateAISubgroup*>(ctx.GetIndices()))
        const IndicesUpdateAISubgroup* indices = static_cast<const IndicesUpdateAISubgroup*>(ctx.GetIndices());

        NetworkId grp;
        if (ctx.IdxGetId(indices->group, grp) != TMOK)
        {
            continue;
        }
        if (grp != group)
        {
            continue;
        }
        ChangeOwner(oInfo.id, oInfo.owner, newOwner);

        AutoArray<NetworkId> units;
        if (ctx.IdxGetIds(indices->units, units) != TMOK)
        {
            continue;
        }
        for (int j = 0; j < units.Size(); j++)
        {
            NetworkId unit = units[j];
            if (unit.IsNull())
            {
                continue;
            }
            // avoid players' units change
            bool found = false;
            for (int j = 0; j < _players.Size(); j++)
            {
                NetworkPlayerInfo& pInfo = _players[j];
                if (pInfo.unit == unit)
                {
                    found = true;
                    break;
                }
            }
            if (found)
            {
                continue;
            }
            NetworkObjectInfo* oInfo = GetObjectInfo(unit);
            if (!oInfo)
            {
                continue;
            }
            int oldOwner = oInfo->owner;
            ChangeOwner(unit, oldOwner, newOwner);
            // change person owner
            NetworkId person = UnitToPerson(unit);
            persons.Add(person);
            ChangeOwner(person, oldOwner, newOwner);
        }
    }

    // change transports owner
    for (int i = 0; i < _objects.Size(); i++)
    {
        NetworkObjectInfo& oInfo = *_objects[i];
        NetworkCurrentInfo& info = oInfo.current[NMCUpdateGeneric];
        if (!info.message)
        {
            continue;
        }
        if (!IsUpdateTransport(info.type))
        {
            continue;
        }
        NetworkMessageFormatBase* format = GetFormat(info.type);
        NET_ERROR(format);
        NetworkMessageContext ctx(info.message, format, this, info.from, MSG_RECEIVE);

        NET_ERROR(dynamic_cast<const IndicesUpdateTransport*>(ctx.GetIndices()))
        const IndicesUpdateTransport* indices = static_cast<const IndicesUpdateTransport*>(ctx.GetIndices());

        NetworkId driver;
        if (ctx.IdxGetId(indices->driver, driver) != TMOK)
        {
            continue;
        }
        bool found = false;
        for (int j = 0; j < persons.Size(); j++)
        {
            if (persons[j] == driver)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            continue;
        }
        ChangeOwner(oInfo.id, oInfo.owner, newOwner);
    }
}

void NetworkServer::SendMissionInfo(int to, bool onlyPlayers)
{
    _missionHeader.updateOnly = false;

    if (!onlyPlayers)
    {
        SendMsg(to, &_missionHeader, NMFGuaranteed);
    }

    for (int i = 0; i < _playerRoles.Size(); i++)
    {
        // create message
        NetworkMessageType type = _playerRoles[i].GetNMType(NMCCreate);
        NetworkMessageFormatBase* format = GetFormat(type);
        Ref<NetworkMessage> msg = new NetworkMessage();
        msg->time = Glob.time;
        NetworkMessageContext ctx(msg, format, this, to, MSG_SEND);

        NET_ERROR(dynamic_cast<const IndicesPlayerRole*>(ctx.GetIndices()))
        const IndicesPlayerRole* indices = static_cast<const IndicesPlayerRole*>(ctx.GetIndices());

        TMError err;
        err = ctx.IdxTransfer(indices->index, i);
        if (err != TMOK)
        {
            continue;
        }
        err = _playerRoles[i].TransferMsg(ctx);
        if (err != TMOK)
        {
            continue;
        }

        // send message
        NetworkComponent::SendMsg(to, msg, type, NMFGuaranteed);
    }

    if (!onlyPlayers)
    {
        MissionParamsMessage msg;
        msg._param1 = _param1;
        msg._param2 = _param2;
        SendMsg(to, &msg, NMFGuaranteed);
    }
}

namespace Poseidon
{
void Encrypt(QOStream& out, const unsigned char* publicKey, int keySize);
}

bool NetworkServer::IsDedicatedBotClient(int dpnid) const
{
    return _dedicated && dpnid == _botClient;
}

static const char* NGSName(NetworkGameState s)
{
    switch (s)
    {
        case NGSNone:
            return "None";
        case NGSCreating:
            return "Creating";
        case NGSCreate:
            return "Create";
        case NGSLogin:
            return "Login";
        case NGSEdit:
            return "Edit";
        case NGSMissionVoted:
            return "MissionVoted";
        case NGSPrepareSide:
            return "PrepareSide";
        case NGSPrepareRole:
            return "PrepareRole";
        case NGSPrepareOK:
            return "PrepareOK";
        case NGSDebriefing:
            return "Debriefing";
        case NGSDebriefingOK:
            return "DebriefingOK";
        case NGSTransferMission:
            return "TransferMission";
        case NGSLoadIsland:
            return "LoadIsland";
        case NGSBriefing:
            return "Briefing";
        case NGSPlay:
            return "Play";
        default:
            return "?";
    }
}

void NetworkServer::SendWorldState(int dpnid)
{
    if (dpnid <= 0)
    {
        LOG_ERROR(Network, "JIP: SendWorldState called with invalid dpnid {}", dpnid);
        return;
    }
    LOG_INFO(Network, "JIP: Sending world state to player {} ({} objects)", dpnid, _objects.Size());

    // 5-pass ordered object sync (matching OFP Xbox approach)
    // Order ensures parent objects exist before children reference them

    // Pass 1: Non-AI objects (terrain, vehicles, static props)
    for (int i = 0; i < _objects.Size(); i++)
    {
        const NetworkObjectInfo& info = *_objects[i];
        if (!info.create.message)
            continue;
        NetworkMessageType type = info.create.type;
        if (type == NMTCreateAICenter || type == NMTCreateAIGroup || type == NMTCreateAISubgroup ||
            type == NMTCreateAIUnit || type == NMTCreateCommand)
            continue;
        NetworkComponent::SendMsg(dpnid, info.create.message, type, NMFGuaranteed);
    }

    // Pass 2: AI Centers (one per side)
    for (int i = 0; i < _objects.Size(); i++)
    {
        const NetworkObjectInfo& info = *_objects[i];
        if (!info.create.message)
            continue;
        if (info.create.type != NMTCreateAICenter)
            continue;
        NetworkComponent::SendMsg(dpnid, info.create.message, info.create.type, NMFGuaranteed);
    }

    // Pass 3: AI Groups (squads)
    for (int i = 0; i < _objects.Size(); i++)
    {
        const NetworkObjectInfo& info = *_objects[i];
        if (!info.create.message)
            continue;
        if (info.create.type != NMTCreateAIGroup)
            continue;
        NetworkComponent::SendMsg(dpnid, info.create.message, info.create.type, NMFGuaranteed);
    }

    // Pass 4: AI Subgroups (fireteams)
    for (int i = 0; i < _objects.Size(); i++)
    {
        const NetworkObjectInfo& info = *_objects[i];
        if (!info.create.message)
            continue;
        if (info.create.type != NMTCreateAISubgroup)
            continue;
        NetworkComponent::SendMsg(dpnid, info.create.message, info.create.type, NMFGuaranteed);
    }

    // Pass 5: AI Units (individual soldiers)
    for (int i = 0; i < _objects.Size(); i++)
    {
        const NetworkObjectInfo& info = *_objects[i];
        if (!info.create.message)
            continue;
        if (info.create.type != NMTCreateAIUnit)
            continue;
        NetworkComponent::SendMsg(dpnid, info.create.message, info.create.type, NMFGuaranteed);
    }

    // Pass 6: Commands (waypoints)
    for (int i = 0; i < _objects.Size(); i++)
    {
        const NetworkObjectInfo& info = *_objects[i];
        if (!info.create.message)
            continue;
        if (info.create.type != NMTCreateCommand)
            continue;
        NetworkComponent::SendMsg(dpnid, info.create.message, info.create.type, NMFGuaranteed);
    }

    // Send current state updates (position, damage, generic)
    for (int i = 0; i < _objects.Size(); i++)
    {
        const NetworkObjectInfo& info = *_objects[i];
        for (int cls = NMCUpdateFirst; cls < NMCUpdateN; cls++)
        {
            if (info.current[cls].message)
            {
                NetworkComponent::SendMsg(dpnid, info.current[cls].message, info.current[cls].type, NMFGuaranteed);
            }
        }
    }

    // Send JIP init messages (publicVariable history, filtered remoteExec, etc.)
    for (int i = 0; i < _jipMessages.Size(); i++)
    {
        if (!_jipMessages[i].msg)
        {
            continue;
        }
        if (_jipMessages[i].type == NMTRemoteExec)
        {
            RemoteExecMessage params;
            NetworkMessageFormatBase* remoteFormat = GetFormat(NMTRemoteExec);
            if (remoteFormat)
            {
                NetworkMessageContext remoteCtx(_jipMessages[i].msg, remoteFormat, this, TO_SERVER, MSG_RECEIVE);
                if (params.TransferMsg(remoteCtx) == TMOK)
                {
                    RemoteExecTargetSelector selector;
                    bool selectorDecoded = false;
                    if (params._targetSpec.Size() > 0)
                    {
                        selectorDecoded = DecodeRemoteExecTargetSelector(selector, params._targetSpec);
                    }
                    else
                    {
                        selector.kind = RemoteExecTargetKind::Scalar;
                        selector.scalar = params._target;
                        selectorDecoded = true;
                    }
                    if (!selectorDecoded || !RemoteExecTargetSelectorReplaysTo(selector, dpnid,
                                                                               [this](const NetworkId& id)
                                                                               {
                                                                                   if (id.IsNull())
                                                                                   {
                                                                                       return 0;
                                                                                   }
                                                                                   NetworkId mutableId = id;
                                                                                   NetworkObjectInfo* info =
                                                                                       GetObjectInfo(mutableId);
                                                                                   return info ? info->owner : 0;
                                                                               }))
                    {
                        continue;
                    }
                }
            }
        }
        NetworkComponent::SendMsg(dpnid, _jipMessages[i].msg, _jipMessages[i].type, NMFGuaranteed);
    }

    LOG_INFO(Network, "JIP: World state sent to player {} ({} JIP messages)", dpnid, _jipMessages.Size());
}

bool NetworkServer::SaveWorldState(const char* filename)
{
    if (!filename || !filename[0])
    {
        LOG_ERROR(Network, "SaveWorldState: empty filename");
        return false;
    }

    FILE* f = fopen(filename, "wb");
    if (!f)
    {
        LOG_ERROR(Network, "SaveWorldState: cannot open file '{}'", filename);
        return false;
    }

    // Header: magic + version
    const int32_t magic = 0x4A495053; // 'JIPS'
    const int32_t version = 1;

    // Mission time elapsed
    int32_t timeElapsed = GlobalTickCount() - _missionHeader.start;

    // Object count (informational)
    int32_t objectCount = _objects.Size();

    // JIP message count
    int32_t jipCount = _jipMessages.Size();

    bool ok = fwrite(&magic, sizeof(magic), 1, f) == 1 && fwrite(&version, sizeof(version), 1, f) == 1 &&
              fwrite(&timeElapsed, sizeof(timeElapsed), 1, f) == 1 &&
              fwrite(&objectCount, sizeof(objectCount), 1, f) == 1 && fwrite(&jipCount, sizeof(jipCount), 1, f) == 1;

    fclose(f);

    if (!ok)
    {
        LOG_ERROR(Network, "SaveWorldState: write error for '{}'", filename);
        return false;
    }
    LOG_INFO(Network, "SaveWorldState: saved to '{}' ({} objects, {} JIP msgs, {}ms elapsed)", filename, objectCount,
             jipCount, timeElapsed);
    return true;
}

bool NetworkServer::LoadWorldState(const char* filename)
{
    if (!filename || !filename[0])
    {
        LOG_ERROR(Network, "LoadWorldState: empty filename");
        return false;
    }

    FILE* f = fopen(filename, "rb");
    if (!f)
    {
        LOG_ERROR(Network, "LoadWorldState: cannot open file '{}'", filename);
        return false;
    }

    int32_t magic, version;
    if (fread(&magic, sizeof(magic), 1, f) != 1 || fread(&version, sizeof(version), 1, f) != 1)
    {
        LOG_ERROR(Network, "LoadWorldState: truncated file header");
        fclose(f);
        return false;
    }

    if (magic != 0x4A495053 || version != 1)
    {
        LOG_ERROR(Network, "LoadWorldState: invalid file format");
        fclose(f);
        return false;
    }

    int32_t timeElapsed, objectCount, jipCount;
    if (fread(&timeElapsed, sizeof(timeElapsed), 1, f) != 1 || fread(&objectCount, sizeof(objectCount), 1, f) != 1 ||
        fread(&jipCount, sizeof(jipCount), 1, f) != 1)
    {
        LOG_ERROR(Network, "LoadWorldState: truncated file data");
        fclose(f);
        return false;
    }

    fclose(f);

    // Adjust mission start time
    _missionHeader.start = GlobalTickCount() - timeElapsed;

    LOG_INFO(Network, "LoadWorldState: loaded from '{}' ({} objects, {} JIP msgs, {}ms elapsed)", filename, objectCount,
             jipCount, timeElapsed);
    return true;
}

void NetworkServer::SetGameState(NetworkGameState state)
{
    LOG_INFO(Network, "State: {} -> {}", NGSName(_state), NGSName(state));
    if (state != _state)
    {
        _stateEnteredTime = GlobalTickCount();
    }
    if (state == NGSPrepareRole && _state > NGSPrepareRole)
    {
        // cancel all messages - updates from last sessions, file transfers etc.
        _server->CancelAllMessages();
        // reset all camera information from previous session
        for (int i = 0; i < _players.Size(); i++)
        {
            NetworkPlayerInfo& p = _players[i];
            p.cameraPosition = InvalidCamPos;
            p.cameraPositionTime = TIME_MIN;
        }
    }

    if (state == NGSPlay)
    {
        _missionHeader.start = GlobalTickCount();

        // A player whose connect handshake spanned the lobby/transfer/briefing phases
        // was created with jip=false (the flag is only computed at creation, against
        // the state at that moment). Once the mission is running, any roleless human
        // is effectively a join-in-progress client: without the flag every role
        // request is rejected and the client is stuck roleless until mission end.
        if (_missionHeader.joinInProgress)
        {
            for (int i = 0; i < _players.Size(); i++)
            {
                NetworkPlayerInfo& p = _players[i];
                if (p.jip || p.dpid == _botClient || FindPlayerRole(p.dpid) != nullptr)
                {
                    continue;
                }
                p.jip = true;
                p.state = NGSCreate;
                LOG_INFO(Network, "Player {} reclassified as JIP at mission start", (const char*)p.name);
            }
        }
    }

    if (_state == NGSPlay && state == NGSDebriefing)
    {
        // end of mission - create log file
        RString ranking = Poseidon::GetRankingLog();
        if (ranking.GetLength() > 0)
        {
            const char* ptr = strrchr(ranking, ':');
            if (ptr)
            {
                // Historical, probably Korean-release, match-results upload path.
                // No built-in server address is kept here; --ranking supplies the active host.
                RString ip = ranking.Substring(0, ptr - ranking);
                int port = atoi(ptr + 1);

                // create paramfile
                ParamFile f;
                // Historical authentic password left just for fun as an easter egg.
                f.Add("password", "Naskove3Praha5");
                AutoArray<AIStatsMPRow>& table = GStats._mission._tableMP;
                for (int i = 0; i < table.Size(); i++)
                {
                    char buffer[32];
                    snprintf(buffer, sizeof(buffer), "Player%d", i + 1);
                    ParamEntry* cls = f.AddClass(buffer);
                    if (cls)
                    {
                        AIStatsMPRow& row = table[i];
                        cls->Add("name", row.player);
                        cls->Add("killsInfantry", row.killsInfantry);
                        cls->Add("killsSoft", row.killsSoft);
                        cls->Add("killsArmor", row.killsArmor);
                        cls->Add("killsAir", row.killsAir);
                        cls->Add("killsPlayers", row.killsPlayers);
                        cls->Add("customScore", row.customScore);
                        cls->Add("killsTotal", row.killsTotal);
                        cls->Add("killed", row.killed);
                    }
                }
                // encrypt and save it
                QOStream s;
                f.Save(s, 0);

                static const unsigned char publicKey[] = {
                    0x11, 0x00, 0x00, 0x00, 0x59, 0x40, 0x61, 0xd6, 0x4f, 0x0a, 0xc4, 0x58, 0xea, 0x72,
                    0x28, 0x0b, 0xf4, 0x9c, 0x87, 0xbe, 0x31, 0x7d, 0x46, 0x1c, 0x70, 0xd6, 0xff, 0x8f,
                    0x2a, 0x2d, 0xe8, 0x98, 0xb8, 0xd4, 0x5a, 0xf1, 0xba, 0x32, 0xff, 0x2e, 0x70, 0xec,
                    0x3f, 0x7f, 0xb0, 0xa1, 0xb3, 0x91, 0xec, 0x86, 0xa7, 0x37, 0xe3, 0xe2, 0x6d, 0xba,
                    0x27, 0x0f, 0x57, 0xe0, 0x2d, 0x76, 0xb8, 0x43, 0xda, 0xaf, 0x17, 0x8a};
                Poseidon::Encrypt(s, publicKey, sizeof(publicKey) - 4);

                {
                    // send it throught sockets
#ifdef _WIN32
                    WSADATA data;
                    WSAStartup(0x0101, &data);
#endif

                    sockaddr_in addr;
                    addr.sin_family = AF_INET;
                    addr.sin_addr.s_addr = inet_addr(ip);
                    addr.sin_port = htons(port);

                    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                    connect(sock, (sockaddr*)&addr, sizeof(addr));
                    send(sock, s.str(), s.pcount(), 0);
                    closesocket(sock);

#ifdef _WIN32
                    WSACleanup();
#endif
                }
            }
        }
    }

    _state = state;
    ChangeGameState gs(state);

    // send to all players
    for (int i = 0; i < _players.Size(); i++)
    {
        NetworkPlayerInfo& info = _players[i];
        if (info.state < NGSCreate)
        {
            continue;
        }
        // Roleless peers stay in role selection until delayed-role catch-up starts
        // their transfer. JIP peers then cross load and briefing through that same
        // per-player choreography before global progression is safe again.
        const bool missionParticipant = FindPlayerRole(info.dpid) != nullptr || IsDedicatedBotClient(info.dpid);
        if (!Poseidon::ShouldBroadcastNetworkMissionState(state, info.jip, info.state, missionParticipant,
                                                          NGSTransferMission, NGSLoadIsland, NGSBriefing, NGSPlay))
        {
            continue;
        }
        SendMsg(info.dpid, &gs, NMFGuaranteed);

        // Duplicity with NetworkClient::OnMessage - case NMTGameState
        switch (state)
        {
            case NGSPrepareRole:
                if (info.state > NGSPrepareRole)
                {
                    if (info.state == NGSTransferMission)
                    {
                        info.missionFileValid = false; // transfer canceled
                    }
                    info.state = NGSPrepareRole;
                }
                else if (IsDedicatedBotClient(info.dpid))
                {
                    info.state = NGSPrepareRole;
                }
                break;
            case NGSPrepareOK:
                if (FindPlayerRole(info.dpid) || IsDedicatedBotClient(info.dpid))
                {
                    info.state = NGSPrepareOK;
                }
                break;
            case NGSDebriefing:
            case NGSDebriefingOK:
                if (info.state >= NGSDebriefing)
                {
                    info.state = state;
                }
                break;
            case NGSTransferMission:
                if (FindPlayerRole(info.dpid) || IsDedicatedBotClient(info.dpid))
                {
                    info.state = NGSTransferMission;
                }
                break;
            case NGSLoadIsland:
                if (FindPlayerRole(info.dpid) || IsDedicatedBotClient(info.dpid))
                {
                    if (info.dpid == _botClient)
                    {
                        info.state = NGSLoadIsland;
                    }
                    else
                    {
                        if (info.missionFileValid)
                        {
                            info.state = NGSLoadIsland;
                        }
                        // else if (info.state < NGSPrepareSide) info.state = NGSPrepareSide;
                    }
                }
                break;
            case NGSBriefing:
                if (info.state >= NGSLoadIsland)
                {
                    info.state = NGSBriefing;
                }
                break;
            case NGSPlay:
                if (info.state == NGSBriefing)
                {
                    info.state = NGSPlay;
                }
                break;
            default:
                info.state = state;
                break;
        }
    }

    UpdateSessionDescription();
}

NetworkGameState NetworkServer::GetPlayerState(int dpid)
{
    NetworkPlayerInfo* info = GetPlayerInfo(dpid);
    if (info)
    {
        return info->state;
    }
    else
    {
        return NGSNone;
    }
}

RString NetworkServer::GetPlayerName(int dpid)
{
    NetworkPlayerInfo* info = GetPlayerInfo(dpid);
    NET_ERROR(info);
    if (info)
    {
        return info->name;
    }
    else
    {
        return "Unknown player";
    }
}

Vector3 NetworkServer::GetCameraPosition(int dpid)
{
    NetworkPlayerInfo* info = GetPlayerInfo(dpid);
    NET_ERROR(info);
    if (info)
    {
        return info->cameraPosition;
    }
    else
    {
        return VZero;
    }
}

NetworkObject* NetworkServer::GetObject(NetworkId& id)
{
    Fail("Server: GetObject is not implemented");
    return nullptr;
}

// Subsidiary structure for sorting messages by error
struct UpdateObjectInfo
{
    // Object info
    NetworkObjectInfo* oInfo;
    // Player object info
    NetworkPlayerObjectInfo* poInfo;
    // Message class type
    NetworkMessageClass cls;
    // Error (difference), distance adjusted
    float error;
    // Error (difference)
    float errorNoCoef;
    // Critical update
    bool criticalUpdate;
};

// Compare player object infos by error
int CmpPlayerObjects(const UpdateObjectInfo* info1, const UpdateObjectInfo* info2)
{
    float diff = info2->error - info1->error;
    return sign(diff);
}

void NetworkServer::KickOff(int dpnid, KickOffReason reason)
{
    RString format;
    switch (reason)
    {
        case KORKick:
            format = LocalizeString(IDS_MP_KICKED);
            break;
        case KORBan:
            format = LocalizeString(IDS_MP_BANNED);
            break;
    }
    PlayerIdentity* id = FindIdentity(dpnid);
    if (id)
    {
        if (id->destroy)
        {
            LOG_DEBUG(Network, "{}: KickOff in progress, new request ignored", (const char*)id->name);
            return;
        }
        if (format.GetLength() > 0)
        {
            RString message = Format(format, (const char*)id->name);
            RefArray<NetworkObject> dummy;
            // GNetworkManager.Chat() relays via the local client (listen server);
            // a dedicated server has none, so it also pushes the notice directly.
            GNetworkManager.Chat(CCGlobal, nullptr, dummy, message);
            if (_dedicated)
            {
                ChatToAllPlayers(message);
            }
            GChatList.Add(CCGlobal, nullptr, message, false, true);
        }
        id->destroy = true;
        id->destroyTime = Glob.uiTime + 15; // destroy player after 15 seconds if no system message arrive
    }
    // mark player kickoff is initiated
    for (int i = 0; i < _players.Size(); i++)
    {
        if (_players[i].dpid == dpnid)
        {
            _players[i].kickedOff = true;
        }
    }
    NetTerminationReason netReason = NTROther;
    switch (reason)
    {
        case KORKick:
            netReason = NTRKicked;
            break;
        case KORBan:
            netReason = NTRBanned;
            break;
        case KORFade:
            netReason = NTROther;
            break;
        case KORAddon:
            netReason = NTRMissingAddon;
            break;
    }
    _server->KickOff(dpnid, netReason);
}

void NetworkServer::Ban(int dpnid)
{
    const PlayerIdentity* identity = FindIdentity(dpnid);
    if (!identity)
    {
        return;
    }

    // banMode controls what is recorded (default both); login enforces both lists.
    if (_banMode == Poseidon::BanMode::Id || _banMode == Poseidon::BanMode::Both)
    {
        const __int64 id64 = _atoi64(identity->id);
        _banListLocal.AddUnique(id64);
        SaveBanList(Poseidon::GetUserDirectory() + RString("ban.txt"), _banListLocal);
    }

    // Capture the IP before KickOff tears the channel down.
    if ((_banMode == Poseidon::BanMode::Ip || _banMode == Poseidon::BanMode::Both) && _server)
    {
        char ipStr[24] = {};
        uint32_t ip = 0;
        if (_server->GetPlayerHostIP(dpnid, ipStr, sizeof(ipStr)) && Poseidon::ParseIPv4(ipStr, ip))
        {
            _banListIPLocal.AddUnique(ip);
            Poseidon::SaveIpBanList(Poseidon::GetUserDirectory() + RString("ipban.txt"), _banListIPLocal);
        }
    }

    _server->KickOff(dpnid, NTRBanned);
}

// Remove a player id (decimal) or IPv4 from the dynamic ban lists and rewrite
// the corresponding file.  An argument that parses as a dotted quad is treated
// as an IP, otherwise as a decimal player id.
void NetworkServer::Unban(const char* idOrIp)
{
    uint32_t ip = 0;
    if (Poseidon::ParseIPv4(idOrIp, ip))
    {
        if (_banListIPLocal.Delete(ip))
        {
            Poseidon::SaveIpBanList(Poseidon::GetUserDirectory() + RString("ipban.txt"), _banListIPLocal);
        }
        return;
    }

    const __int64 id64 = _atoi64(idOrIp);
    if (_banListLocal.Delete(id64))
    {
        SaveBanList(Poseidon::GetUserDirectory() + RString("ban.txt"), _banListLocal);
    }
}

void NetworkServer::ChatToAllPlayers(RString message)
{
    RefArray<NetworkObject> dummy;
    ChatMessage msg(CCGlobal, nullptr, dummy, "", message);
    for (int i = 0; i < _players.Size(); i++)
    {
        if (_players[i].state >= NGSCreate)
        {
            SendMsg(_players[i].dpid, &msg, NMFGuaranteed);
        }
    }
}

#if LOG_ERRORS
// diagnostic logs of error (difference) between messages
static void LogError(NetworkMessageType type, int dpid1, NetworkMessage* msg1, int dpid2, NetworkMessage* msg2)
{
    if (type < 0 || type >= NMTN)
        return;
    NetworkMessageFormat* format = GMsgFormats[type];

    float dt = msg1->time - msg2->time;

    // new version of message comparing
    for (int i = 0; i < format->NItems(); i++)
    {
        const NetworkMessageErrorInfo& info = format->GetErrorInfo(i);
        if (info.type == ET_NONE)
            continue;

        const RefNetworkData& value1 = msg1->values[i];
        const RefNetworkData& value2 = msg2->values[i];
        float value = value1.CalculateError(info.type, value2, format->GetItem(i), dt);
        float coef = info.coef;
        float error = coef * value;

        if (error > 0)
            DiagLogF("    - %s: %g * %g = %g", (const char*)format->GetItem(i).name, coef, value, error);
    }
}
#endif

#define LOG_QUEUE_PROGRESS 0

void NetworkServer::CheckFadeOut()
{
    // Copy protection kickoff disabled — this was the original game's
    // CD key validation that kicked players with invalid/pirated keys.
    // Not applicable to the open-source reimplementation.
}

void NetworkServer::EstimateBandwidth(NetworkPlayerInfo& pInfo, int nPlayers, int& nMsgMax, int& nBytesMax)
{
    if (pInfo.dpid == _botClient)
    {
        nBytesMax = INT_MAX;
        nMsgMax = INT_MAX;

        pInfo._ping.Sample(0);
        pInfo._bandwidth.Sample(INT_MAX);
    }
    else
    {
        int nMsg = 0, nBytes = 0, nMsgG = 0, nBytesG = 0;

        // calculate quotas
        _server->GetSendQueueInfo(pInfo.dpid, nMsg, nBytes, nMsgG, nBytesG);
        if (nMsgG > 0)
        {
            nMsg += nMsgG;
            nBytes += nBytesG;
        }

        int minBandwidth;
        if (IsDedicatedServer())
        {
            nMsgMax = DSMaxMsgSend;
            minBandwidth = DSMinBandwidth;
        }
        else
        {
            nMsgMax = MaxMsgSend;
            minBandwidth = MinBandwidth;
        }
        int bandwidth = 1024;
        int latencyMS = 0, throughputBPS = 0;
        if (_server->GetConnectionInfo(pInfo.dpid, latencyMS, throughputBPS))
        {
            // nBytesMax += 1.25 * throughputBPS;
            bandwidth += throughputBPS;
            bandwidth += throughputBPS >> 2; // throughputBPS / 4
        }
        // assume server upload bandwidth is at least MinBandwidth
        if (nPlayers > 0)
        {
            saturate(bandwidth, minBandwidth / (8 * nPlayers), MaxBandwidth / 8);
        }

        nBytesMax = toInt(0.001 * GEngine->GetAvgFrameDuration() * bandwidth);

        nBytesMax -= nBytes;
        nMsgMax -= nMsg;

        {
            // statistics
            int latencyMSRaw = 0, throughputBPSRaw = 0;
            _server->GetConnectionInfoRaw(pInfo.dpid, latencyMSRaw, throughputBPSRaw);
            pInfo._ping.Sample(latencyMSRaw);
            pInfo._bandwidth.Sample(throughputBPSRaw);
        }

        // write statistics
#if _ENABLE_CHEATS
        if (outputLogs)
            LOG_DEBUG(Network, "Server to {}: Limit ({}, {})", (const char*)pInfo.name, nBytesMax, nMsgMax);
#endif
    }
}

void NetworkServer::CreateObjectsList(AutoArray<UpdateObjectInfo, MemAllocSA>& objects, NetworkPlayerInfo& pInfo)
{
    // calculate errors and place object in list sorted by error
    for (int j = 0; j < _objects.Size(); j++)
    {
        NetworkObjectInfo& oInfo = *_objects[j];
        if (oInfo.owner == pInfo.dpid)
        {
            continue; // no updates for local objects
        }

        // found best object position and calculate error coef
        Time time = TIME_MIN;
        NetworkCurrentInfo* best = nullptr;
        for (int j = NMCUpdateFirst; j < NMCUpdateN; j++)
        {
            NetworkCurrentInfo& info = oInfo.current[j];
            NetworkMessage* msg = info.message;
            if (msg)
            {
                if (msg->time > time)
                {
                    best = &info;
                    time = msg->time;
                }
            }
        }
        if (!best)
        {
            continue;
        }
        Vector3 bestPosition(-FLT_MAX, -FLT_MAX, -FLT_MAX);
        {
            NetworkMessageFormat* format = GMsgFormats[best->type];
            NetworkMessageContext ctx(best->message, format, this, best->from, MSG_RECEIVE);

            NET_ERROR(dynamic_cast<const IndicesNetworkObject*>(ctx.GetIndices()))
            const IndicesNetworkObject* indices = static_cast<const IndicesNetworkObject*>(ctx.GetIndices());

            if (ctx.IdxTransfer(indices->objectPosition, bestPosition) != TMOK)
            {
                continue;
            }
        }

        float errCoef = CalculateErrorCoef(best->type, pInfo.cameraPosition, bestPosition);

        NetworkPlayerObjectInfo* poInfo = oInfo.CreatePlayerObjectInfo(pInfo.dpid);

        for (int j = NMCUpdateFirst; j < NMCUpdateN; j++)
        {
            NetworkCurrentInfo& info = oInfo.current[j];
            if (!info.message)
            {
                continue; // no current state
            }

            NetworkUpdateInfo& update = poInfo->updates[j];

            float error;
            // error without distance coef
            float errorNoCoef;
            if (update.lastCreatedMsg)
            {
                // message on the way
                continue;
            }
            else if (!update.lastSentMsg)
            {
                errorNoCoef = error = FLT_MAX;
#if LOG_ERRORS
                DiagLogF("Object %s %d:%d - first update to %d", NetworkMessageTypeNames[info.type], oInfo.id.creator,
                         oInfo.id.id, pInfo.dpid);
#endif
            }
            else
            {
                errorNoCoef = error =
                    CalculateError(info.type, info.from, info.message, pInfo.dpid, update.lastSentMsg);
#if LOG_ERRORS
                if (error * errCoef >= LogErrorLimit)
                {
                    DiagLogF("Object %s %d:%d - update to %d", NetworkMessageTypeNames[info.type], oInfo.id.creator,
                             oInfo.id.id, pInfo.dpid);
                    DiagLogF("  - error %.3f * %.0f = %.0f", errCoef, error, errCoef * error);
                    if (pInfo.cameraPosition[1] != -FLT_MAX)
                    {
                        DiagLogF("  - camera [%.0f, %.0f, %.0f]", pInfo.cameraPosition[0], pInfo.cameraPosition[1],
                                 pInfo.cameraPosition[2]);
                        DiagLogF("  - position [%.0f, %.0f, %.0f] (distance %.0f m)", bestPosition[0], bestPosition[1],
                                 bestPosition[2], bestPosition.Distance(pInfo.cameraPosition));
                    }
                    else
                    {
                        DiagLogF("  - position [%.0f, %.0f, %.0f], no camera position", bestPosition[0],
                                 bestPosition[1], bestPosition[2]);
                    }
                    LogError(info.type, info.from, info.message, pInfo.dpid, update.lastSentMsg);
                }
#endif
                error *= errCoef;
            }

            // note: following condition seems to be identical to error>MinErrorToSend
            // but it is different when error is NaN
            if (!(error <= MinErrorToSend))
            {
                int index = objects.Add();
                // objects[index].pInfo = &pInfo;
                objects[index].oInfo = &oInfo;
                objects[index].poInfo = poInfo;
                objects[index].cls = (NetworkMessageClass)j;
                objects[index].error = error;
                objects[index].errorNoCoef = errorNoCoef;
                objects[index].criticalUpdate = false;

#if _DEBUG
#ifdef _WIN32
#define NOP __nop()
#else
#define NOP __asm__ __volatile__("nop")
#endif
                switch (info.type)
                {
                    case NMTUpdateAIUnit:
                        NOP;
                        break;
                    case NMTUpdateMan:
                        NOP;
                        break;
                    case NMTUpdatePositionMan:
                        NOP;
                        break;
                    case NMTUpdateVehicleAI:
                        NOP;
                        break;
                    case NMTUpdateDammageVehicleAI:
                        NOP;
                        break;
                    case NMTUpdateObject:
                        NOP;
                        break;
                    case NMTUpdateDammageObject:
                        NOP;
                        break;
                }
                if (update.lastSentMsg)
                {
                    // tracing opportunity
                    // place breakpoint depending on message type
                    switch (info.type)
                    {
                        case NMTUpdateAIUnit:
                            NOP;
                            break;
                        case NMTUpdateMan:
                            NOP;
                            break;
                        case NMTUpdatePositionMan:
                            NOP;
                            break;
                        case NMTUpdateVehicleAI:
                            NOP;
                            break;
                        case NMTUpdateDammageVehicleAI:
                            NOP;
                            break;
                        case NMTUpdateObject:
                            NOP;
                            break;
                        case NMTUpdateDammageObject:
                            NOP;
                            break;
                    }
                    float verError = CalculateError(info.type, info.from, info.message, pInfo.dpid, update.lastSentMsg);
                    (void)verError; // Calculated for debugging purposes
                    NOP;
                }

#endif
            }
        }
    }

    // sort objects by errors
    QSort(objects.Data(), objects.Size(), CmpPlayerObjects);
}

void NetworkServer::OnSendStarted(DWORD msgID, const NetworkMessageQueueItem& item)
{
    NET_ERROR(msgID != MSGID_REPLACE);
    if (item.msg->objectUpdateInfo)
    {
#if LOG_SEND_PROCESS
        LOG_DEBUG(Network, "  - update info {:x} updated", (uintptr_t)item.msg->objectUpdateInfo);
#endif
        if (item.msg->objectUpdateInfo->lastCreatedMsg != item.msg)
        {
            RptF("Last created message is bad:");
            RptF("  sending message: %x (type %s), id = %x", item.msg.GetRef(), NetworkMessageTypeNames[item.type],
                 msgID);
            RptF("	attached object info: %x", item.msg->objectUpdateInfo);
            RptF("	last created message: %x, id = %x", item.msg->objectUpdateInfo->lastCreatedMsg.GetRef(),
                 item.msg->objectUpdateInfo->lastCreatedMsgId);
            RptF("	last sent message: %x", item.msg->objectUpdateInfo->lastSentMsg.GetRef());

            item.msg->objectUpdateInfo->lastCreatedMsg = item.msg;
        }
        NET_ERROR(item.msg->objectUpdateInfo->lastCreatedMsgId == MSGID_REPLACE);
        item.msg->objectUpdateInfo->lastCreatedMsgId = msgID;
        if (msgID == 0xffffffff)
        {
            item.msg->objectUpdateInfo->lastCreatedMsg = nullptr;
            item.msg->objectUpdateInfo->lastCreatedMsgTime = 0;
            // no pending message may exist to this - it had id=MSGID_REPLACE
        }
        else
        {
            AddPendingMessage(msgID, item.msg->objectServerInfo, item.msg->objectUpdateInfo,
                              item.msg->objectPlayerInfo);
        }
    }
}

void NetworkServer::SendMessages()
{
#if _ENABLE_CHEATS
    // remove statistics info
    static AutoArray<int> texts;
    for (int i = 0; i < texts.Size(); i++)
    {
        if (texts[i] >= 0)
            GEngine->RemoveText(texts[i]);
    }
    texts.Resize(0);
    int y = 40;
#endif

#if _ENABLE_CHEATS
#define LOG_DESYNC 1
#endif

#if LOG_DESYNC
    static DWORD time = 0;
    DWORD now = GlobalTickCount();
    bool logDesync = false;
    if (now >= time + 1000)
    {
        logDesync = true;
        time = now;
    }
#endif

    int nPlayers = _identities.Size() - 1;
    if (IsDedicatedServer())
    {
        nPlayers++;
    }

    for (int i = 0; i < _players.Size(); i++)
    {
        NetworkPlayerInfo& pInfo = _players[i];

        int nMsg = 0;
        int nBytes = 0;
        int nMsgMax, nBytesMax;

        EstimateBandwidth(pInfo, nPlayers, nMsgMax, nBytesMax);

#if LOG_QUEUE_PROGRESS
        if (pInfo._messageQueue.Size() > 0)
        {
            LOG_DEBUG(Network, "{}: nBytesMax {}, nBytes {}, queue size {}", (const char*)pInfo.name, nBytesMax, nBytes,
                      pInfo._messageQueue.Size());
        }
#endif

        // send enqueued guaranteed messages
        int maxSize = MaxSizeGuaranteed;

        const int nMsgMaxGuaranteed = nMsgMax;
        const int nBytesMaxGuaranteed = nBytesMax;

        while (pInfo._messageQueue.Size() > 0)
        {
            if (nMsg >= nMsgMaxGuaranteed || nBytes >= nBytesMaxGuaranteed)
            {
#if LOG_QUEUE_PROGRESS
                int qBytes = 0;
                for (int i = 0; i < pInfo._messageQueue.Size(); i++)
                {
                    qBytes += pInfo._messageQueue[i].msg->size;
                }
                LOG_DEBUG(Network, "  -- queue left {} ({} B)", pInfo._messageQueue.Size(), qBytes);
#endif
                break;
            }

            NetworkMessage* msg = pInfo._messageQueue[0].msg;
            int size = msg->size;
            if (size >= maxSize)
            {
                // DWORD dwMsgId =
                SendMsgRemote(pInfo.dpid, msg, pInfo._messageQueue[0].type, NMFGuaranteed | NMFStatsAlreadyDone);
                pInfo._messageQueue.Delete(0);
                nMsg++;
                nBytes += size;
            }
            else
            {
                int totalSize = 0;
                int i;
                for (i = 0; i < pInfo._messageQueue.Size(); i++)
                {
                    NetworkMessage* msg = pInfo._messageQueue[i].msg;
                    int size = msg->size;
                    if (totalSize + size > maxSize)
                    {
                        break;
                    }
                    totalSize += size;
                }
                // DWORD dwMsgId =
                SendMsgQueue(pInfo.dpid, pInfo._messageQueue, 0, i, NMFGuaranteed);
                pInfo._messageQueue.Delete(0, i);
                nMsg++;
                nBytes += totalSize;
            }
        }

#if LOG_QUEUE_PROGRESS
        if (nBytes > 0)
        {
            LOG_DEBUG(Network, "  -- nMsg {} (nBytes {})", nMsg, nBytes);
            if (pInfo._messageQueue.Size() == 0)
            {
                LOG_DEBUG(Network, "  ** Queue empty");
            }
        }
#endif

        AUTO_STATIC_ARRAY(UpdateObjectInfo, objects, 256);

#define ENABLE_CRITICAL_UPDATES 1

#if !ENABLE_CRITICAL_UPDATES
        if (nMsg >= nMsgMax || nBytes >= nBytesMax)
        {
#if _ENABLE_CHEATS
            if (outputLogs)
                LOG_DEBUG(Network, "  Server to {}: limit reached (guaranteed)", (const char*)pInfo.name);
#endif
            goto DoNotSendUpdates;
        }
#endif

        // update objects
        if (_state >= NGSPlay && pInfo.state >= NGSPlay)
        {
            // update errors of objects
            CreateObjectsList(objects, pInfo);

            // create and send critical updates
#if ENABLE_CRITICAL_UPDATES
            for (int i = 0; i < objects.Size(); i++)
            {
                NetworkObjectInfo* oInfo = objects[i].oInfo;
                NetworkMessageClass cls = objects[i].cls;

                float error = objects[i].error;
                float errorNoCoef = objects[i].errorNoCoef;

                // any message about dammage that transmit change
                // that could mean dead should be considered high priority
                // errorNoCoef - error with no regard to distance
                // error - distance adjusted error
                if (cls != NMCUpdateDammage || error < ERR_COEF_STRUCTURE * 0.01 || errorNoCoef < ERR_COEF_STRUCTURE)
                {
                    continue;
                }

                objects[i].criticalUpdate = true;
                int bytes = UpdateObject(&pInfo, oInfo, cls, NMFGuaranteed | NMFHighPriority);

#if _ENABLE_CHEATS
                if (outputLogs)
                {
                    LOG_DEBUG(Network, "  Server to {}: Object {}:{} updated - size {} bytes, critical",
                              (const char*)pInfo.name, objects[i].oInfo->id.creator, objects[i].oInfo->id.id, bytes);
                }
#endif

                if (bytes >= 0)
                {
                    // no error
                    nMsg++;
                    nBytes += bytes;
                }
            }

            if (nMsg >= nMsgMax || nBytes >= nBytesMax)
            {
#if _ENABLE_CHEATS
                if (outputLogs)
                    LOG_DEBUG(Network, "  Server to {}: limit reached (guaranteed)", (const char*)pInfo.name);
#endif
                goto DoNotSendUpdates;
            }
#endif // ENABLE_CRITICAL_UPDATES
        }

    DoNotSendUpdates:
        // send nonguaranteed enqueued messages
        maxSize = MaxSizeNonguaranteed;
        int next = 0; // index of next object to update
        bool empty = false;
        while (true)
        {
            // enforce send last message
            if (!empty || pInfo._messageQueueNonGuaranteed.Size() <= 0)
            {
                if (nMsg >= nMsgMax || nBytes >= nBytesMax)
                {
                    break;
                }
            }

            if (pInfo._messageQueueNonGuaranteed.Size() <= 0)
            {
                empty = true;
                // starving, try to create further message
                if (!PrepareNextUpdate(pInfo, objects, next))
                {
                    break;
                }
                // local communication - message was already sent
                if (pInfo.dpid == _botClient)
                {
                    NET_ERROR(pInfo._messageQueueNonGuaranteed.Size() == 0);
                    nMsg++;
                    continue;
                }
                NET_ERROR(pInfo._messageQueueNonGuaranteed.Size() > 0);
            }

            NetworkMessageQueueItem& item = pInfo._messageQueueNonGuaranteed[0];
            int size = item.msg->size;
            // enforce send last message
            if (size >= maxSize || nMsg >= nMsgMax || nBytes >= nBytesMax)
            {
                DWORD dwMsgId = SendMsgRemote(pInfo.dpid, item.msg, item.type, NMFStatsAlreadyDone);
#if LOG_SEND_PROCESS
                LOG_DEBUG(Network, "Server: Message {:x} sent", dwMsgId);
#endif
                OnSendStarted(dwMsgId, item);
                pInfo._messageQueueNonGuaranteed.Delete(0);
                nMsg++;
                nBytes += size;
            }
            else
            {
                int totalSize = 0;
                int i;
                for (i = 0; true; i++)
                {
                    if (i >= pInfo._messageQueueNonGuaranteed.Size())
                    {
                        empty = true;
                        // starving, try to create further message
                        if (!PrepareNextUpdate(pInfo, objects, next))
                        {
                            break;
                        }
                        NET_ERROR(pInfo.dpid != _botClient);
                        NET_ERROR(i < pInfo._messageQueueNonGuaranteed.Size());
                    }

                    NetworkMessageQueueItem& item = pInfo._messageQueueNonGuaranteed[i];
                    int size = item.msg->size;
                    if (totalSize + size > maxSize)
                    {
                        break;
                    }
                    totalSize += size;
                }
                DWORD dwMsgId = SendMsgQueue(pInfo.dpid, pInfo._messageQueueNonGuaranteed, 0, i, NMFNone);
#if LOG_SEND_PROCESS
                LOG_DEBUG(Network, "Server: Message {:x} sent", dwMsgId);
#endif
                for (int j = 0; j < i; j++)
                {
                    NetworkMessageQueueItem& item = pInfo._messageQueueNonGuaranteed[j];
                    OnSendStarted(dwMsgId, item);
                }
                pInfo._messageQueueNonGuaranteed.Delete(0, i);
                nMsg++;
                nBytes += totalSize;
            }
        }
        NET_ERROR(!empty || pInfo._messageQueueNonGuaranteed.Size() == 0);

        float sumError = 0; // messages not sent total error
        if (next < pInfo._messageQueueNonGuaranteed.Size())
        {
#if _ENABLE_CHEATS
            if (outputLogs)
                LOG_DEBUG(Network, "  Server to {}: limit reached", (const char*)pInfo.name);
#endif
            for (int i = next; i < objects.Size(); i++)
            {
                if (objects[i].error < FLT_MAX)
                {
                    sumError += objects[i].error;
                }
            }
        }

        pInfo._desync.Sample(sumError, 1e-3);

        // show server diagnostics
#if _ENABLE_CHEATS

        if (outputDiags == 1)
        {
            int count = pInfo._messageQueue.Size(), size = 0;
            for (int i = 0; i < count; i++)
                size += pInfo._messageQueue[i].msg->size;

            RString output = Format("%s: HLWait%3d(%5dB) ", (const char*)pInfo.name, count, size) +
                             _server->GetStatistics(pInfo.dpid);

            texts.Add(GEngine->ShowTextF(1000, 10, y, output));
            y += 25;
        }
#endif
    }
}

bool NetworkServer::PrepareNextUpdate(NetworkPlayerInfo& pInfo, AutoArray<UpdateObjectInfo, MemAllocSA>& objects,
                                      int& next)
{
    while (next < objects.Size())
    {
        UpdateObjectInfo& info = objects[next++];
        if (info.criticalUpdate)
        {
            continue;
        }

        NetworkObjectInfo* oInfo = info.oInfo;
        NetworkMessageClass cls = info.cls;
        int bytes = UpdateObject(&pInfo, oInfo, cls, NMFNone);
        if (bytes < 0)
        {
            continue;
        }

#if _ENABLE_CHEATS
        if (outputLogs)
            LOG_DEBUG(Network, "  Server to {}: Object updated - size {} bytes", (const char*)pInfo.name, bytes);
#endif
        return true;
    }
    return false;
}

float NetworkServer::CalculateErrorCoef(NetworkMessageType type, Vector3Par cameraPosition, Vector3Val position)
{
    switch (type)
    {
        case NMTUpdateDammageObject:
        case NMTUpdateObject:
            return Object::CalculateErrorCoef(position, cameraPosition);
        case NMTUpdatePositionVehicle:
        case NMTUpdateVehicle:
            return Vehicle::CalculateErrorCoef(position, cameraPosition);
        case NMTUpdateDetector:
            return Detector::CalculateErrorCoef(position, cameraPosition);
        case NMTUpdateFlag:
            return FlagCarrier::CalculateErrorCoef(position, cameraPosition);
        case NMTUpdateShot:
            return Shot::CalculateErrorCoef(position, cameraPosition);
        case NMTUpdateMine:
            return Mine::CalculateErrorCoef(position, cameraPosition);
        case NMTUpdateDammageVehicleAI:
        case NMTUpdateVehicleAI:
            return EntityAI::CalculateErrorCoef(position, cameraPosition);
        case NMTUpdateVehicleBrain:
            return Person::CalculateErrorCoef(position, cameraPosition);
        case NMTUpdateVehicleSupply:
            return VehicleSupply::CalculateErrorCoef(position, cameraPosition);
        case NMTUpdateTransport:
            return Transport::CalculateErrorCoef(position, cameraPosition);
        case NMTUpdatePositionMan:
        case NMTUpdateMan:
            return Man::CalculateErrorCoef(position, cameraPosition);
        case NMTUpdateTankOrCar:
            return TankOrCar::CalculateErrorCoef(position, cameraPosition);
        case NMTUpdatePositionTank:
        case NMTUpdateTank:
            return TankWithAI::CalculateErrorCoef(position, cameraPosition);
        case NMTUpdatePositionMotorcycle:
        case NMTUpdateMotorcycle:
            return Motorcycle::CalculateErrorCoef(position, cameraPosition);
        case NMTUpdatePositionCar:
        case NMTUpdateCar:
            return Car::CalculateErrorCoef(position, cameraPosition);
        case NMTUpdatePositionAirplane:
        case NMTUpdateAirplane:
            return AirplaneAuto::CalculateErrorCoef(position, cameraPosition);
        case NMTUpdatePositionHelicopter:
        case NMTUpdateHelicopter:
            return HelicopterAuto::CalculateErrorCoef(position, cameraPosition);
        case NMTUpdateParachute:
            return ParachuteAuto::CalculateErrorCoef(position, cameraPosition);
        case NMTUpdatePositionShip:
        case NMTUpdateShip:
            return ShipWithAI::CalculateErrorCoef(position, cameraPosition);
        case NMTUpdatePositionSeagull:
        case NMTUpdateSeagull:
            return SeaGullAuto::CalculateErrorCoef(position, cameraPosition);
        case NMTUpdateAICenter:
            return AICenter::CalculateErrorCoef(position, cameraPosition);
        case NMTUpdateAIGroup:
            return AIGroup::CalculateErrorCoef(position, cameraPosition);
        case NMTUpdateAISubgroup:
            return AISubgroup::CalculateErrorCoef(position, cameraPosition);
        case NMTUpdateAIUnit:
            return AIUnit::CalculateErrorCoef(position, cameraPosition);
        case NMTUpdateCommand:
            return Command::CalculateErrorCoef(position, cameraPosition);
        case NMTUpdateClientInfo:
            return ClientInfoObject::CalculateErrorCoef(position, cameraPosition);
        case NMTUpdateFireplace:
            return Fireplace::CalculateErrorCoef(position, cameraPosition);
        default:
            RptF("Server: Unknown update message %d(%s)", (int)type, (const char*)NetworkMessageTypeNames[type]);
            return 0;
    }
}

float NetworkServer::CalculateError(NetworkMessageType type, int dpid1, NetworkMessage* msg1, int dpid2,
                                    NetworkMessage* msg2)
{
    if (type < 0 || type >= NMTN)
    {
        RptF("Server: Bad message %d format", (int)type);
        return 0.0;
    }
    NetworkMessageFormat* format = GMsgFormats[type];

    float errCoefTime;
    switch (type)
    {
        case NMTUpdatePositionVehicle:
        case NMTUpdatePositionMan:
        case NMTUpdatePositionTank:
        case NMTUpdatePositionCar:
        case NMTUpdatePositionAirplane:
        case NMTUpdatePositionHelicopter:
        case NMTUpdatePositionShip:
        case NMTUpdatePositionSeagull:
        case NMTUpdatePositionMotorcycle:
            errCoefTime = ERR_COEF_TIME_POSITION;
            break;
        default:
            errCoefTime = ERR_COEF_TIME_GENERIC;
            break;
    }

    float dt = msg1->time - msg2->time;
    float errValue = errCoefTime * dt;

    // new version of message comparing
    for (int i = 0; i < format->NItems(); i++)
    {
        const NetworkMessageErrorInfo& info = format->GetErrorInfo(i);
        if (info.type == ET_NONE)
        {
            continue;
        }

        const RefNetworkData& value1 = msg1->values[i];
        const RefNetworkData& value2 = msg2->values[i];
        errValue += info.coef * value1.CalculateError(info.type, value2, format->GetItem(i), dt);
    }

    return errValue;
}

NetworkId NetworkServer::PersonToUnit(NetworkId& person)
{
    for (int i = 0; i < _mapPersonUnit.Size(); i++)
    {
        PersonUnitPair& pair = _mapPersonUnit[i];
        if (pair.person == person)
        {
            return pair.unit;
        }
    }
    return NetworkId::Null();
}

NetworkId NetworkServer::UnitToPerson(NetworkId& unit)
{
    for (int i = 0; i < _mapPersonUnit.Size(); i++)
    {
        PersonUnitPair& pair = _mapPersonUnit[i];
        if (pair.unit == unit)
        {
            return pair.person;
        }
    }
    return NetworkId::Null();
}

int NetworkServer::AddPersonUnitPair(NetworkId& person, NetworkId& unit)
{
    int index = _mapPersonUnit.Add();
    PersonUnitPair& pair = _mapPersonUnit[index];
    pair.person = person;
    pair.unit = unit;
    return index;
}

void NetworkServer::SendMissionFile()
{
    SendMissionFileTo(NO_PLAYER);
}

static int GetTestMissionTransferSegmentDelayMs()
{
    const char* value = getenv("POSEIDON_TEST_MISSION_TRANSFER_SEGMENT_DELAY_MS");
    if (value == nullptr || value[0] == 0)
    {
        return 0;
    }
    int delayMs = atoi(value);
    saturate(delayMs, 0, 5000);
    return delayMs;
}

void NetworkServer::SendMissionFileTo(int player)
{
    const NetworkGameState minimumTransferState =
        Poseidon::GetNetworkMissionFileSendMinimumState(player, NO_PLAYER, NGSCreate, NGSTransferMission);
    bool found = false;
    for (int i = 0; i < _players.Size(); i++)
    {
        NetworkPlayerInfo& info = _players[i];
        if (player != NO_PLAYER && info.dpid != player)
        {
            continue;
        }
        if (info.dpid == _botClient)
        {
            continue;
        }
        if (info.state < minimumTransferState)
        {
            continue;
        }
        if (!info.missionFileValid)
        {
            found = true;
            break;
        }
    }
    LOG_INFO(Network, "[SendMissionFile] found={} players={} botClient={}", found, _players.Size(), (int)_botClient);
    if (!found)
    {
        return;
    }

    QIFStreamB f;
    RString src = _missionBank;
    f.AutoOpen(src);
    const auto source = Poseidon::BuildNetworkMissionFileSendSource(src, _missionHeader.fileName, f.GetBuffer());
    LOG_INFO(Network, "[SendMissionFile] src='{}' dst='{}' size={} segmentSize={}", (const char*)source.sourcePath,
             (const char*)source.destinationPath, source.totalSize, Poseidon::NetworkMissionTransferSegmentSize);
    if (!source.data || source.totalSize < 0)
    {
        LOG_ERROR(Network, "[SendMissionFile] mission bank is not readable: '{}'", (const char*)src);
        return;
    }

    AutoArray<int, MemAllocSA> recipients;
    if (player != NO_PLAYER)
    {
        recipients.Add(player);
    }
    else
    {
        Poseidon::CollectNetworkMissionFileRecipients(
            _players.Size(), [this](int index) -> const NetworkPlayerInfo& { return _players[index]; }, _botClient,
            minimumTransferState, [&recipients](int dpid) { recipients.Add(dpid); });
    }
    if (recipients.Size() <= 0)
    {
        return;
    }

    const int testSegmentDelayMs = GetTestMissionTransferSegmentDelayMs();
    if (testSegmentDelayMs > 0)
    {
        SendMessages();
    }

    const auto result = Poseidon::SendCurrentNetworkMissionFile<TransferMissionFileMessage>(
        _missionHeader.fileName, source.data, source.totalSize, recipients.Size(),
        [&recipients](int index) { return recipients[index]; }, _botClient,
        [this, testSegmentDelayMs](int dpid, TransferMissionFileMessage& msg)
        {
            if (testSegmentDelayMs > 0)
            {
                Sleep(testSegmentDelayMs);
            }
            SendMsg(dpid, &msg, Poseidon::NetworkMissionTransferSendFlags);
        },
        _players.Size(), [this](int index) -> NetworkPlayerInfo& { return _players[index]; }, minimumTransferState);
    LOG_INFO(Network, "[SendMissionFile] queued {} guaranteed segments for {} player(s)", result.segmentCount,
             recipients.Size());
}

namespace Poseidon
{
RString GetPidFileName();
RString GetRankingLog();
RString GetServerConfig();
void SetServerConfig(const RString& config);
extern bool MyPidFile;
} // namespace Poseidon

static std::string NormalizeMissionLookupPath(const std::string& path)
{
    std::filesystem::path fsPath(path);
    std::error_code ec;
    if (fsPath.is_relative())
    {
        fsPath = std::filesystem::absolute(fsPath, ec);
        if (ec)
        {
            return path;
        }
    }
    return fsPath.lexically_normal().string();
}

static bool LoadDedicatedServerConfig(RString config, ParamFile& out)
{
    if (config.GetLength() <= 0)
    {
        LOG_INFO(Network, "No dedicated server config specified");
        return true;
    }

    std::filesystem::path configPath((const char*)config);
    std::error_code ec;
    std::filesystem::path resolved = std::filesystem::absolute(configPath, ec);
    if (ec)
    {
        LOG_ERROR(Network, "Server config path could not be resolved: {} ({})", (const char*)config, ec.message());
        return false;
    }
    resolved = resolved.lexically_normal();

    if (!std::filesystem::exists(resolved, ec) || std::filesystem::is_directory(resolved, ec))
    {
        LOG_ERROR(Network, "Server config missing: {}", resolved.string());
        return false;
    }

    const std::string resolvedString = resolved.string();
    if (out.Parse(resolvedString.c_str()) != LSOK)
    {
        LOG_ERROR(Network, "Server config parse failed: {}", resolvedString);
        return false;
    }

    Poseidon::SetServerConfig(RString(resolvedString.c_str()));
    LOG_INFO(Network, "Server config loaded: {}", resolvedString);
    return true;
}

bool CreateDedicatedServer(RString config)
{
    // Only destroy HUD if options UI exists (not created for dedicated servers)
    if (GWorld->Options())
    {
        GWorld->Options()->DestroyHUD(-1);
    }

    RString password("");
    ParamFile serverCfg;
    if (!LoadDedicatedServerConfig(config, serverCfg))
    {
        return false;
    }
    if (serverCfg.FindEntry("password"))
    {
        password = serverCfg >> "password";
    }
    else
    {
        password = GetNetworkPassword();
    }

    int port = GetNetworkPort();
    LOG_DEBUG(Network, "GetNetworkPort() returned: {}", port);

    // If port is 0, use default 1985
    if (port == 0)
    {
        port = 1985;
        LOG_INFO(Network, "Port was 0, using default 1985");
        SetNetworkPort(port); // Set globally so bit mask setup uses correct port
    }

    // Try to create session, retry with port+1 if occupied (up to 10 attempts)
    constexpr int MAX_PORT_ATTEMPTS = 10;
    int originalPort = port;
    bool sessionCreated = false;

    // Avoid sessions enumeration:	GNetworkManager.Init("", port);
    InitMsgFormats();

    for (int attempt = 0; attempt < MAX_PORT_ATTEMPTS; ++attempt)
    {
        if (attempt > 0)
        {
            LOG_INFO(Network, "Port {} occupied, trying port {}...", port - 1, port);
            // Clean up failed attempt before retrying
            GNetworkManager.Done();
            SetNetworkPort(port); // Update global port for next attempt
        }

        GNetworkManager.CreateSession(port, password);

        if (GNetworkManager.GetServer())
        {
            sessionCreated = true;
            if (port != originalPort)
            {
                LOG_INFO(Network, "Successfully bound to alternate port {}", port);
            }
            break;
        }

        // Port occupied or Init failed, try next port
        port++;
    }

    if (sessionCreated && GNetworkManager.GetServer())
    {
        GNetworkManager.GetServer()->SetDedicated(Poseidon::GetServerConfig());
        const std::string missionTemplateDir = (const char*)Poseidon::GetMPMissionsDir();
        LOG_INFO(Network, "Server mission lookup: templateDir={} absolute={} userContentMPMissions={}",
                 missionTemplateDir, NormalizeMissionLookupPath(missionTemplateDir),
                 NormalizeMissionLookupPath(GamePaths::Instance().MPMissionsDir()));
        LOG_INFO(Network, "Server: {} - Port: {} - Transport: Sockets", (const char*)GetVersionString(), port);
        // writting the pid of the server instance:
        if (Poseidon::GetPidFileName().GetLength())
        {
            FILE* pidFp;
            if ((pidFp = fopen((const char*)Poseidon::GetPidFileName(), "wt")) != nullptr)
            {
                fprintf(pidFp, "%d\n", getpid());
#ifdef _WIN32
#else
                fchmod(fileno(pidFp), (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH));
#endif
                fclose(pidFp);
                Poseidon::MyPidFile = true;
            }
        }
        return true;
    }

    // Failed to create server on any port - cleanup network resources
    LOG_ERROR(Network, "Failed to bind server to any port from {} to {}", originalPort,
              originalPort + MAX_PORT_ATTEMPTS - 1);
    GNetworkManager.Done();

    return false;
}

void NetworkServer::SetDedicated(RString config)
{
    _dedicated = true;

    _serverCfg.Parse(config);
    _missionIndex = -1;

    _motdInterval = 5;
    const ParamEntry* entry = _serverCfg.FindEntry("motdInterval");
    if (entry)
    {
        _motdInterval = *entry;
    }

    entry = _serverCfg.FindEntry("motd");
    if (entry)
    {
        for (int i = 0; i < (*entry).GetSize(); i++)
        {
            _motd.Add((*entry)[i]);
        }
    }

    _voteThreshold = 0.5;
    entry = _serverCfg.FindEntry("voteThreshold");
    if (entry)
    {
        _voteThreshold = *entry;
    }

    entry = _serverCfg.FindEntry("kickDuplicate");
    if (entry)
    {
        _kickDuplicate = *entry;
    }

    // banMode = id | ip | both (default both): what a runtime Ban() records.
    entry = _serverCfg.FindEntry("banMode");
    if (entry)
    {
        const RString banModeStr = *entry;
        _banMode = Poseidon::ParseBanMode(static_cast<const char*>(banModeStr));
    }

    entry = _serverCfg.FindEntry("equalModRequired");
    if (entry)
    {
        _equalModRequired = *entry;
    }

    StartMasterServerReporting();
}

void NetworkServer::Monitor(float interval)
{
    _monitorInterval = interval;
    _monitorFrames = 0;
    _monitorIn = 0;
    _monitorOut = 0;
    if (_monitorInterval > 0)
    {
        _monitorNext = GlobalTickCount() + toInt(1000.0 * _monitorInterval);
    }
    else
    {
        _monitorInterval = UINT_MAX, _monitorNext = UINT_MAX;
    }
}

void NetworkServer::OnMonitorIn(int size)
{
    if (_dedicated)
    {
        _monitorIn += size;
    }
}
