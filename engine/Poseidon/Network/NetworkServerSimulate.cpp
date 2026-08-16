#include <Poseidon/Foundation/Platform/VersionNo.h>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Foundation/Platform/AppConfig.hpp>
#include <Poseidon/Foundation/Platform/GamePaths.hpp>
#include <Poseidon/Network/NetworkServerCommon.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Core/ModSystem.hpp>
#include <Poseidon/Core/Version.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <Poseidon/Core/Config/Config.hpp>
#include <Poseidon/Network/NetworkImpl.hpp>
#include <Poseidon/Network/NetworkMissionTransfer.hpp>
#include <Poseidon/Core/Global.hpp>
// #include "strIncl.hpp"
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <filesystem>
#include <random>

#include <Poseidon/Network/NetworkConfig.hpp>
#include <float.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <system_error>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Enums/EnumNames.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Memory/CheckMem.hpp>
#include <Poseidon/Foundation/Memory/FastAlloc.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/Memtype.h>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

using namespace Poseidon;
namespace Poseidon
{
RString& GetMPMissionsDir();
bool ParseMission(bool);
void RunMissionScript(char const*, class ::GameValue);
void SetMission(RString, RString, RString);
} // namespace Poseidon

// Path to cleanup on simulate exit (random-prefixed mission copy)
static std::filesystem::path s_simulateMissionCopy;
static bool s_simulateSmokeSuccessReported = false;

namespace
{
void FailSimulateSmokeCheck(const char* reason)
{
    if (!AppConfig::Instance().IsSimulateSmokeCheck())
        return;

    LOG_ERROR(Mission, "[simulate-smoke] {}", reason);
    GApp->m_exitCode = 1;
    GApp->m_closeRequest = true;
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

using Poseidon::Foundation::EnumName;
using Poseidon::Foundation::GetEnumValue;
using Poseidon::Foundation::MemoryUsed;
using Poseidon::Foundation::Time;
using Poseidon::Foundation::UITime;

#ifdef _WIN32
#include <io.h>
#include <winsock2.h>
#include <process.h>
#endif

namespace Poseidon
{
void RunInitScript();
}

void CleanupSimulateMission()
{
    if (!s_simulateMissionCopy.empty())
    {
        std::error_code ec;
        std::filesystem::remove_all(s_simulateMissionCopy, ec);
        s_simulateMissionCopy.clear();
    }
    s_simulateSmokeSuccessReported = false;
}

void CalculateMinMaxAvg::Sample(float value, float weight)
{
    saturateMin(_min, value);
    saturateMax(_max, value);
    _sum += value * weight;
    _weight += weight;
}

void CalculateMinMaxAvg::Reset()
{
    _min = FLT_MAX;
    _max = FLT_MIN;
    _sum = 0;
    _weight = 0;
}

CalculateMinMaxAvg::CalculateMinMaxAvg()
{
    Reset();
}

inline int toLargeIntSat(float x)
{
    if (x >= INT_MAX)
    {
        return INT_MAX;
    }
    if (x < INT_MIN)
    {
        return INT_MIN;
    }
    return toLargeInt(x);
}

void NetworkServer::ServerMessage(const char* text)
{
    LOG_INFO(Network, "{}", text);
    GChatList.Add(CCGlobal, nullptr, text, false, true);
    RefArray<NetworkObject> dummy;
    GNetworkManager.Chat(CCGlobal, "", dummy, text);
}

#define DEBUG_TYPE_ENUM(type, prefix, XX) \
    XX(type, prefix, TotalSent)           \
    XX(type, prefix, UserSent)            \
    XX(type, prefix, Console)             \
    XX(type, prefix, UserInfo)            \
    XX(type, prefix, UserQueue)

DECLARE_ENUM(DebugType, DT, DEBUG_TYPE_ENUM)

template <>
const EnumName* Poseidon::Foundation::GetEnumNames(DebugType dummy)
{
    static const EnumName DebugTypeNames[] = {DEBUG_TYPE_ENUM(DebugType, DT, ENUM_NAME) EnumName()};
    return DebugTypeNames;
}

static RString ExtractWord(RString& line)
{
    const char* endWord = strchr(line, ' ');
    if (!endWord)
    {
        RString word = line;
        line = RString();
        return word;
    }
    RString word = line.Substring(0, endWord - line.Data());
    line = endWord + 1;
    return word;
}

void NetworkServer::DebugAsk(RString str, int from, bool fullAccess)
{
    // check for some special debug commands
    RString line = str;
    RString word = ExtractWord(line);
    if (!strcmpi(word, "version"))
    {
        RString versionText = GetVersionString();

        DebugAnswer(RString("Debug: version ") + versionText);

        ChatMessage chat;
        chat.channel = CCGlobal;
        chat.text = RString("Server Version: ") + versionText;
        chat.sender = nullptr;
        chat.name = "";
        SendMsg(from, &chat, NMFGuaranteed);
        return;
    }
    if (!fullAccess)
    {
        return;
    }
    if (!strcmpi(word, "checkfile"))
    {
        // get file name
        RString filename = ExtractWord(line);
        if (filename.GetLength() > 0)
        {
            // check file CRC
            if (!IsRelativePath(filename))
            {
                RString message = RString("Only relative path check allowed: ") + filename;
                GChatList.Add(CCGlobal, nullptr, message, false, true);
            }
            else
            {
                // if there is no ID, check all players
                PerformFileIntegrityCheck(filename);
            }
        }
    }
    else if (!strcmpi(word, "checkexe"))
    {
        // format: offset:size offset:size, offset:size
        for (;;)
        {
            RString location = ExtractWord(line);
            if (location.GetLength() <= 0)
            {
                break;
            }
            PerformExeIntegrityCheck(location);
        }
    }

    // check if string is only numbers
    char* endNum;
    float interval = strtod(str, &endNum);
    if (*endNum == 0)
    {
        _debugInterval = interval;
        saturate(_debugInterval, 0.1f, 3600);
        BString<256> msg;
        sprintf(msg, "Debug: Interval %g", _debugInterval);
        DebugAnswer(RString(msg));
        return;
    }
    if (!stricmp(str, "off"))
    {
        // clear all debug info
        _debugOn.Clear();
        DebugAnswer(RString("Debug: All off"));
        _debugNext = INT_MAX;
        return;
    }

    const char* beg = str;
    const char* end = strchr(beg, ' ');
    int len = end ? end - beg : strlen(beg);

    RString strCommand(beg, len);
    DebugType command = GetEnumValue<DebugType>((const char*)strCommand);
    if (command < 0)
    {
        return;
    }

    beg += len;
    while (*beg == ' ')
    {
        beg++;
    }

    if (stricmp(beg, "off") == 0)
    {
        _debugOn.DeleteKey(command);
        DebugAnswer(RString("Debug: ") + strCommand + RString(" off"));
    }
    else
    {
        _debugOn.AddUnique(command);
        DebugAnswer(RString("Debug: ") + strCommand);

        if (_debugInterval <= 0)
        {
            _debugInterval = 10;
            _debugNext = _debugNext + toInt(_debugInterval * 1000);
        }
        int next = GlobalTickCount() + toInt(_debugInterval * 1000);
        if (_debugNext > static_cast<DWORD>(next))
        {
            _debugNext = next; // _debugNext is DWORD, next is int (always positive here)
        }
    }
}

void NetworkServer::DebugAnswer(RString str)
{
    if (_gameMaster != AI_PLAYER)
    {
        NetworkCommandMessage msg;
        msg.type = NCMTDebugAnswer;
        msg.content.WriteString(str);
        SendMsg(_gameMaster, &msg, NMFGuaranteed);
    }
}

int NetworkServer::ConsoleF(const char* format, ...)
{
    va_list arglist;
    va_start(arglist, format);

    BString<512> buf;
    vsprintf(buf, format, arglist);

    if (_debugOn.FindKey(DTConsole) >= 0)
    {
        DebugAnswer((const char*)buf);
    }
    LOG_INFO(Network, "{}", (const char*)buf);

    va_end(arglist);
    return 0;
}

void NetworkServer::SimulateDS()
{
    if (!_dedicated)
    {
        return;
    }

    DWORD tickCount = GlobalTickCount();
    bool simulateMode = AppConfig::Instance().IsSimulateMode();

    // Periodic stats logging in simulate mode
    if (simulateMode && _state == NGSPlay)
    {
        static unsigned long long frameCount = 0;
        frameCount++;

        int statsInterval = AppConfig::Instance().GetStatsInterval();
        if (statsInterval > 0)
        {
            static DWORD lastStatsTick = 0;
            DWORD statsIntervalMs = statsInterval * 1000;
            if (lastStatsTick == 0 || (tickCount - lastStatsTick) >= statsIntervalMs)
            {
                lastStatsTick = tickCount;
                int vehicles = GWorld->NVehicles();
                int buildings = GWorld->NBuildings();
                int animals = GWorld->NAnimals();
                int shots = GWorld->NFastVehicles();

                auto countSide = [](AICenter* c, int& groups, int& units)
                {
                    groups = 0;
                    units = 0;
                    if (!c)
                        return;
                    groups = c->NGroups();
                    for (int i = 0; i < c->NGroups(); i++)
                        if (c->GetGroup(i))
                            units += c->GetGroup(i)->NUnits();
                };
                int wG, wU, eG, eU, gG, gU, cG, cU;
                countSide(GWorld->GetWestCenter(), wG, wU);
                countSide(GWorld->GetEastCenter(), eG, eU);
                countSide(GWorld->GetGuerrilaCenter(), gG, gU);
                countSide(GWorld->GetCivilianCenter(), cG, cU);

                int totalGroups = wG + eG + gG + cG;
                int totalUnits = wU + eU + gU + cU;
                int players = _players.Size();
                int roles = _playerRoles.Size();

                // Game time in seconds since mission start
                float gameTime = Glob.time - Time(0);
                int gtMin = (int)(gameTime / 60.0f);
                int gtSec = (int)gameTime % 60;

                LOG_INFO(Mission,
                         "[stats] frame={} time={}m{}s vehicles={} buildings={} animals={} shots={} "
                         "groups={} units={} (W:{}/{} E:{}/{} G:{}/{} C:{}/{}) "
                         "players={} roles={}",
                         frameCount, gtMin, gtSec, vehicles, buildings, animals, shots, totalGroups, totalUnits, wG, wU,
                         eG, eU, gG, gU, cG, cU, players, roles);
            }
        }
    }

    // In simulate mode, inject test-mission path into the state machine
    // Template format is "missionName.worldName" (e.g. "script_test.eden")
    // The folder must exist in mpmissions/ directory
    if (simulateMode && _state == NGSCreate && _mission.GetLength() == 0)
    {
        static bool missionInjected = false;
        if (!missionInjected)
        {
            const std::string& testMission = AppConfig::Instance().GetTestMissionPath();
            if (!testMission.empty())
            {
                namespace fs = std::filesystem;
                fs::path srcPath(testMission);
                // Remove trailing separator so filename() works correctly
                if (srcPath.has_filename() == false)
                    srcPath = srcPath.parent_path();
                std::string mpDir(Poseidon::GetMPMissionsDir());
                fs::path destDir = fs::path(mpDir);
                fs::create_directories(destDir);

                // Generate random hex prefix for parallel run isolation
                std::random_device rd;
                char prefix[12];
                snprintf(prefix, sizeof(prefix), "%08x_", rd());

                std::string templateName;
                if (fs::is_directory(srcPath))
                {
                    std::string prefixedName = prefix + srcPath.filename().string();
                    templateName = prefixedName;
                    fs::path dest = destDir / prefixedName;
                    fs::copy(srcPath, dest, fs::copy_options::recursive);
                    s_simulateMissionCopy = dest;
                }
                else
                {
                    std::string prefixedName = prefix + srcPath.stem().string();
                    std::string prefixedFilename = prefixedName + srcPath.extension().string();
                    templateName = prefixedName;
                    fs::path dest = destDir / prefixedFilename;
                    fs::copy_file(srcPath, dest, fs::copy_options::overwrite_existing);
                    s_simulateMissionCopy = dest;
                }
                _mission = templateName.c_str();
                LOG_INFO(Mission, "[simulate] Mission template: {}", templateName);
                missionInjected = true;
            }
        }
    }

    if (!simulateMode && _players.Size() <= 1)
    {
        if (_state != NGSCreate)
        {
            LOG_INFO(Network, "All users disconnected, waiting for users");
            SetGameState(NGSCreate);
            CleanUpMemory();
        }
        _missionIndex = -1;
        _mission = "";
        _restart = false;
        _reassign = false;
    }
    else
    {
        static bool loadIsland = false;
        //			static UITime debriefing;
        static RString lastSideMessage;
        static UITime lastSideMessageTime;
        switch (_state)
        {
            case NGSCreate:
            {
                RString t;
                if (_mission.GetLength() == 1 && _mission[0] == '?')
                {
                    break;
                }
                else if (_mission.GetLength() > 0)
                {
                    t = _mission;
                    USER_CONFIG.easyMode = _cadetMode;
                    _param1 = FLT_MAX;
                    _param2 = FLT_MAX;

                    _mission = "";
                    _restart = false;
                    _reassign = false;
                }
                else
                {
                    const ParamEntry* entry = _serverCfg.FindEntry("Missions");
                    int nEntries = entry ? entry->GetEntryCount() : 0;
                    if (nEntries == 0)
                    {
                        int voteMissionPlayers = 1;
                        entry = _serverCfg.FindEntry("voteMissionPlayers");
                        if (entry)
                        {
                            voteMissionPlayers = *entry;
                        }
                        if (_identities.Size() >= voteMissionPlayers)
                        {
                            _mission = "?";
                            NetworkCommandMessage answer;
                            answer.type = NCMTVoteMission;
                            AddMissionList(answer);
                            for (int i = 0; i < _identities.Size(); i++)
                            {
                                SendMsg(_identities[i].dpnid, &answer, NMFGuaranteed);
                            }
                        }
                        break;
                    }

                    _missionIndex++;
                    if (_missionIndex >= nEntries)
                    {
                        _missionIndex = 0;
                    }
                    const ParamEntry& item = entry->GetEntry(_missionIndex);

                    t = item >> "template";
                    USER_CONFIG.easyMode = item >> "cadetMode";

                    _param1 = FLT_MAX;
                    _param2 = FLT_MAX;
                    if (item.FindEntry("param1"))
                    {
                        _param1 = item >> "param1";
                    }
                    if (item.FindEntry("param2"))
                    {
                        _param2 = item >> "param2";
                    }
                }

                CurrentCampaign = "";
                CurrentBattle = "";
                CurrentMission = "";

                SetBaseDirectory("");

                char mission[256];
                snprintf(mission, sizeof(mission), "%s", (const char*)t);
                char* world = strrchr(mission, '.');
                if (!world)
                {
                    LOG_ERROR(Network, "Mission template '{}' has no world suffix", (const char*)t);
                    return;
                }
                *world = 0;
                world++;

                RString resolvedTemplate = Poseidon::ResolveMPMissionTemplateBase(mission, world);
                LOG_DEBUG(Network, "Mission selection: template={} mission={} world={} resolved={}", (const char*)t,
                          mission, world, (const char*)resolvedTemplate);
                if (QIFStream::FileExists(resolvedTemplate + RString(".pbo")))
                {
                    // mission in public bank
                    CreateMission(mission, world);
                    // CreateMPMissionBank mounts the staged .pbo under the relative virtual
                    // prefix "MPMissions/__cur_mp.<world>/", so the parse must read it via that
                    // same relative path. In simulate mode GetMPMissionsDir() is an absolute
                    // temp path (it stages the copy there) — passing it would make
                    // GetMissionDirectory() miss the bank and groups.Size()==0 -> ParseMission
                    // fails for every .pbo. The loose-dir branch below can use the absolute
                    // path because its files exist on disk, not in a prefixed bank.
                    Poseidon::SetMission(world, "__cur_mp", RString(GameDirs::MPMissionsPath().c_str()));
                    Glob.header.filenameReal = mission;

                    LOG_INFO(Network, "Mission {} read from bank {}", (const char*)t, (const char*)resolvedTemplate);
                }
                else if (QIFStream::FileExists(Poseidon::GetMPMissionsDir() + t + RString("/mission.sqm")))
                {
                    // mission in public directory
                    CreateMission("", "");
                    Poseidon::SetMission(world, mission, Poseidon::GetMPMissionsDir());

                    LOG_INFO(Network, "Mission {} read from directory", (const char*)t);
                }
                else
                {
                    RString triedPbo = resolvedTemplate + RString(".pbo");
                    RString triedDir = Poseidon::GetMPMissionsDir() + t + RString("/mission.sqm");
                    LOG_ERROR(Network, "Mission template '{}' not found; tried pbo='{}' and dir='{}'", (const char*)t,
                              (const char*)triedPbo, (const char*)triedDir);
                    FailSimulateSmokeCheck("mission file was not found after staging");
                    break;
                }

                // load template
                CurrentTemplate.Clear();
                if (!Poseidon::ParseMission(true))
                {
                    LOG_ERROR(Network, "Mission template '{}' failed to parse", (const char*)t);
                    FailSimulateSmokeCheck("Poseidon::ParseMission(true) failed");
                    break;
                }

                if (_param1 == FLT_MAX)
                {
                    _param1 = 0;
                    const ParamEntry* cfg = ExtParsMission.FindEntry("defValueParam1");
                    if (cfg)
                    {
                        _param1 = *cfg;
                    }
                }
                if (_param2 == FLT_MAX)
                {
                    _param2 = 0;
                    const ParamEntry* cfg = ExtParsMission.FindEntry("defValueParam2");
                    if (cfg)
                    {
                        _param2 = *cfg;
                    }
                }

                InitMission(USER_CONFIG.easyMode);
                SetGameState(NGSPrepareSide);
            }
            break;
            case NGSPrepareSide:
            case NGSPrepareRole:
            {
                int mpAutoStart = AppConfig::Instance().GetMPAutoStart();
                // check if all commanders are ready
                if (simulateMode && mpAutoStart > 0)
                {
                    // Auto-start mode: wait for N human players to be ready
                    int ready = 0;
                    for (int i = 0; i < _players.Size(); i++)
                    {
                        if (_players[i].dpid == _botClient)
                            continue;
                        if (!FindIdentity(_players[i].dpid))
                            continue;
                        if (_players[i].state >= NGSPrepareOK)
                            ready++;
                    }
                    if (ready < mpAutoStart)
                        break;
                    LOG_INFO(Network, "[mp-auto-start] {} of {} players ready, starting", ready, mpAutoStart);
                }
                else if (!simulateMode && !_restart)
                {
                    if (_gameMaster != AI_PLAYER)
                    {
                        if (GetPlayerState(_gameMaster) < NGSPrepareOK)
                        {
                            break;
                        }
                    }
                    else
                    {
                        int total = 0, ready = 0;
                        for (int i = 0; i < _players.Size(); i++)
                        {
                            if (_players[i].dpid == _botClient)
                            {
                                continue;
                            }

                            // CHECK FOR PLAYER WITHOUT IDENTITY
                            if (!FindIdentity(_players[i].dpid))
                            {
                                RptF("Server error: Player without identity %s (id %d)", (const char*)_players[i].name,
                                     _players[i].dpid);
                                continue;
                            }

                            total++;
                            if (_players[i].state >= NGSPrepareOK)
                            {
                                ready++;
                            }
                        }
                        if (ready < total && ready < NPlayerRoles())
                        {
                            break;
                        }
                        if (ready <= 0)
                        {
                            break;
                        }
                    }
                }
                _restart = false;

                // create game
                SetGameState(NGSTransferMission);
                SendMissionFile();

                LOG_INFO(Network, "Roles assigned");
            }
            break;
            case NGSTransferMission:
            {
                int tracked = 0;
                if (!Poseidon::AreTrackedNetworkMissionFilesValid(
                        _players.Size(), [this](int index) -> const NetworkPlayerInfo& { return _players[index]; },
                        _botClient, NGSTransferMission, [this](int dpid) { return FindPlayerRole(dpid) != nullptr; },
                        tracked))
                {
                    break;
                }
                LOG_INFO(Network, "Mission file transfer complete for {} player(s)", tracked);
                SetGameState(NGSLoadIsland);
                loadIsland = true;
            }
            break;
            case NGSLoadIsland:
                if (loadIsland && (simulateMode || GNetworkManager.GetClient()->GetGameState() == NGSLoadIsland))
                {
                    loadIsland = false;

                    LOG_INFO(Network, "Reading mission ...");

                    GWorld->SwitchLandscape(GetWorldName(Glob.header.worldname));

                    // set parameters - after SwitchLandscape
                    float param1 = 0, param2 = 0;
                    GetNetworkManager().GetParams(param1, param2);
                    GameState* gstate = GWorld->GetGameState();
                    gstate->VarSet("param1", GameValue(param1), false);
                    gstate->VarSet("param2", GameValue(param2), false);
                    GNetworkManager.PublicVariable("param1");
                    GNetworkManager.PublicVariable("param2");

                    GStats.ClearMission();
                    GWorld->ActivateAddons(CurrentTemplate.addOns);
                    GWorld->InitGeneral(CurrentTemplate.intel);
                    bool error = false;
                    if (!GWorld->InitVehicles(GModeNetware, CurrentTemplate))
                    {
                        error = true;
                    }
                    RString message = GetMaxErrorMessage();
                    if (message.GetLength() > 0)
                    {
                        ServerMessage(message);
                        FailSimulateSmokeCheck(message);
                        SetGameState(NGSCreate);
                        break;
                    }
                    if (error)
                    {
                        ServerMessage("Error loading mission");
                        FailSimulateSmokeCheck("InitVehicles(GModeNetware, CurrentTemplate) failed");
                        SetGameState(NGSCreate);
                        break;
                    }

                    GNetworkManager.CreateAllObjects();
                    Poseidon::RunInitScript();

                    void RunMissionScript(const char* filename, GameValue argument);
                    Poseidon::RunMissionScript("initServer.sqs", GameValue());

                    message = GetMaxErrorMessage();
                    if (message.GetLength() > 0)
                    {
                        ServerMessage(message);
                        FailSimulateSmokeCheck(message);
                        SetGameState(NGSCreate);
                        break;
                    }
                    else
                    {
                        LOG_INFO(Network, "Mission read");
                        if (simulateMode)
                        {
                            LOG_INFO(Mission, "[loaded]");
                            SetGameState(NGSBriefing);
                        }
                    }
                }
                break;
            case NGSBriefing:
            {
                bool all = true;
                int mpAutoStart = AppConfig::Instance().GetMPAutoStart();
                if (!simulateMode || mpAutoStart > 0)
                {
                    for (int i = 0; i < _playerRoles.Size(); i++)
                    {
                        int player = _playerRoles[i].player;
                        if (player == AI_PLAYER || player == NO_PLAYER)
                        {
                            continue;
                        }

                        if (GetPlayerState(player) != NGSPlay)
                        {
                            all = false;
                            break;
                        }
                    }
                }
                if (all)
                {
                    // start game
                    SetGameState(NGSPlay);
                    {
                        NetworkPlayerInfo* info = GetPlayerInfo(_botClient);
                        NET_ERROR(info);
                        info->state = NGSPlay;
                        SetPlayerState(_botClient, NGSPlay);
                    }

// source hack to enable 1.96 compiled with the actual version of this cpp file
#if APP_VERSION_NUM > 196
                    GStats.OnMissionStart();
#endif
                    LOG_INFO(Network, "Game started");
                    if (simulateMode && AppConfig::Instance().IsSimulateSmokeCheck() && !s_simulateSmokeSuccessReported)
                    {
                        LOG_INFO(Mission, "[simulate-smoke] reached play state");
                        GApp->m_exitCode = 0;
                        GApp->m_closeRequest = true;
                        s_simulateSmokeSuccessReported = true;
                    }
                }
            }
            break;
            case NGSPlay:
                if (_restart)
                {
                    GNetworkManager.DestroyAllObjects();
                    LOG_INFO(Network, "Game restarted");
                    SetGameState(NGSDebriefing);
                }
                else if (GWorld->GetEndMode() != EMContinue &&
                         ((!GWorld->GetCameraEffect() && !GWorld->GetTitleEffect()) || GWorld->IsEndForced()))
                {
                    GNetworkManager.DestroyAllObjects();
                    LOG_INFO(Network, "Game finished");
                    SetGameState(NGSDebriefing);
                }
                else if (!simulateMode)
                {
                    bool all = true;
                    for (int i = 0; i < _playerRoles.Size(); i++)
                    {
                        int player = _playerRoles[i].player;
                        if (player == AI_PLAYER || player == NO_PLAYER)
                        {
                            continue;
                        }

                        if (GetPlayerState(player) == NGSPlay)
                        {
                            all = false;
                            break;
                        }
                    }
                    if (all)
                    {
                        GNetworkManager.DestroyAllObjects();
                        LOG_INFO(Network, "Game finished");
                        SetGameState(NGSDebriefing);
                    }
                }
                break;
            case NGSDebriefing:
            {
                if (simulateMode)
                {
                    LOG_INFO(Mission, "[complete]");
                    GApp->m_closeRequest = true;
                    break;
                }
                float all = true;
                if (_gameMaster != AI_PLAYER)
                {
                    NetworkGameState state = GetPlayerState(_gameMaster);
                    all = state != NGSDebriefing && state <= NGSDebriefingOK;
                }
                else
                {
                    for (int i = 0; i < _playerRoles.Size(); i++)
                    {
                        int player = _playerRoles[i].player;
                        if (player == AI_PLAYER || player == NO_PLAYER)
                        {
                            continue;
                        }

                        NetworkGameState state = GetPlayerState(player);
                        if (state == NGSDebriefing || state > NGSDebriefingOK)
                        {
                            all = false;
                            break;
                        }
                    }
                }
                //				if (Glob.uiTime - debriefing >= 5.0)
                if (all)
                {
                    if (_restart && _reassign)
                    {
                        // unassign all players
                        int defaultPlayer = _missionHeader.disabledAI ? NO_PLAYER : AI_PLAYER;
                        for (int i = 0; i < _playerRoles.Size(); i++)
                        {
                            _playerRoles[i].player = defaultPlayer;
                            _playerRoles[i].roleLocked = false;
                        }
                        // send to clients
                        for (int i = 0; i < _players.Size(); i++)
                        {
                            NetworkPlayerInfo& info = _players[i];
                            if (info.state < NGSCreate)
                            {
                                continue;
                            }
                            SendMissionInfo(info.dpid, true);
                            info.state = NGSPrepareSide;
                            SetPlayerState(info.dpid, NGSPrepareSide);
                        }

                        SetGameState(NGSPrepareSide);
                        _restart = false;
                        _reassign = false;
                    }
                    else if (_restart && _mission.GetLength() == 0)
                    {
                        SetGameState(NGSPrepareRole);
                        loadIsland = true;
                    }
                    else
                    {
                        SetGameState(NGSCreate);
                        LOG_INFO(Network, "Waiting for next game");
                    }
                }
            }
            break;
        }
    }

    _monitorFrames++;
    if (tickCount >= _monitorNext)
    {
        if (_gameMaster == AI_PLAYER)
        {
            _monitorInterval = 0;
        }
        else // if (_state == NGSPlay)
        {
            NetworkCommandMessage msg;
            msg.type = NCMTMonitorAnswer;
            float invInterval = 1.0 / (_monitorInterval + 0.001f * (tickCount - _monitorNext));
            float fps = _monitorFrames * invInterval;
            int memory = MemoryUsed();
            float in = _monitorIn * invInterval;
            float out = _monitorOut * invInterval;
            msg.content.Write(&fps, sizeof(fps));
            msg.content.Write(&memory, sizeof(memory));
            msg.content.Write(&in, sizeof(in));
            msg.content.Write(&out, sizeof(out));

            int sizeG = 0, sizeNG = 0;
            for (int i = 0; i < _players.Size(); i++)
            {
                NetworkPlayerInfo& player = _players[i];
                for (int j = 0; j < player._messageQueue.Size(); j++)
                {
                    sizeG += player._messageQueue[j].msg->size;
                }
                for (int j = 0; j < player._messageQueueNonGuaranteed.Size(); j++)
                {
                    sizeNG += player._messageQueueNonGuaranteed[j].msg->size;
                }
            }
            msg.content.Write(&sizeG, sizeof(sizeG));
            msg.content.Write(&sizeNG, sizeof(sizeNG));

            SendMsg(_gameMaster, &msg, NMFGuaranteed);
        }
        Monitor(_monitorInterval);
    }

    for (int i = 0; i < _players.Size(); i++)
    {
        NetworkPlayerInfo& info = _players[i];
        if (info.state < NGSCreate)
        {
            continue;
        }
        if (info.motdIndex >= 0 && Glob.uiTime >= info.motdTime)
        {
            ChatMessage chat;
            chat.channel = CCGlobal;
            chat.text = _motd[info.motdIndex];
            chat.sender = nullptr;
            chat.name = "";
            SendMsg(info.dpid, &chat, NMFGuaranteed);

            info.motdIndex++;
            if (info.motdIndex >= _motd.Size())
            {
                info.motdIndex = -1;
            }
            else
            {
                info.motdTime = Glob.uiTime + _motdInterval;
            }
        }
    }
}

void NetworkServer::OnSimulate()
{
    // raw statistics

    if (_dedicated)
    {
        // static DWORD lastDebugMessageTime = GlobalTickCount();
        DWORD now = GlobalTickCount();
        if (now >= _debugNext)
        {
            if (_gameMaster == AI_PLAYER || _debugInterval < 0.001)
            {
                _debugNext = INT_MAX;
            }
            else
            {
                _debugNext = _debugNext + toInt(_debugInterval * 1000);
            }

            float invAge = 1 / _debugInterval;

            if (_debugOn.FindKey(DTUserSent) >= 0)
            {
                for (int i = 0; i < _rawStatistics.Size(); i++)
                {
                    BString<512> output;
                    NetworkPlayerInfo* info = GetPlayerInfo(_rawStatistics[i].player);
                    if (!info)
                    {
                        continue;
                    }
                    sprintf(output, "%s: sent %.0f bps (%.2f Msgps), received %.0f bps (%.2f Msgps)",
                            (const char*)info->name, invAge * _rawStatistics[i].sizeSent * 8,
                            invAge * _rawStatistics[i].msgSent, invAge * _rawStatistics[i].sizeReceived * 8,
                            invAge * _rawStatistics[i].msgReceived);
                    DebugAnswer((const char*)output);
                }
            }

            if (_debugOn.FindKey(DTTotalSent) >= 0)
            {
                int totalMsgSent = 0;
                int totalMsgReceived = 0;
                int totalSizeSent = 0;
                int totalSizeReceived = 0;
                for (int i = 0; i < _rawStatistics.Size(); i++)
                {
                    totalMsgSent += _rawStatistics[i].msgSent;
                    totalMsgReceived += _rawStatistics[i].msgReceived;
                    totalSizeSent += _rawStatistics[i].sizeSent;
                    totalSizeReceived += _rawStatistics[i].sizeReceived;
                }
                BString<512> output;
                sprintf(output, "** Total: sent %.0f bps (%.2f Msgps), received %.0f bps (%.2f Msgps)",
                        invAge * totalSizeSent * 8, invAge * totalMsgSent, invAge * totalSizeReceived * 8,
                        invAge * totalMsgReceived);
                DebugAnswer((const char*)output);
            }

            _rawStatistics.Clear();

            if (_debugOn.FindKey(DTUserQueue) >= 0)
            {
                // send info about all players
                for (int i = 0; i < _players.Size(); i++)
                {
                    const NetworkPlayerInfo& player = _players[i];
                    int sizeG = 0, sizeNG = 0;
                    for (int j = 0; j < player._messageQueue.Size(); j++)
                    {
                        sizeG += player._messageQueue[j].msg->size;
                    }
                    for (int j = 0; j < player._messageQueueNonGuaranteed.Size(); j++)
                    {
                        sizeNG += player._messageQueueNonGuaranteed[j].msg->size;
                    }
                    // print actual desync as well

                    BString<512> output;
                    sprintf(output, "%s: Queue %d B (%d) %d B Guaranteed (%d), Desync %.0f", (const char*)player.name,
                            sizeNG, player._messageQueueNonGuaranteed.Size(), sizeG, player._messageQueue.Size(),
                            player._desync.GetAvg() * 100);
                    DebugAnswer((const char*)output);
                }
            }
            if (_debugOn.FindKey(DTUserInfo) >= 0)
            {
                // send info about all players
                for (int i = 0; i < _players.Size(); i++)
                {
                    const NetworkPlayerInfo& player = _players[i];

                    RString stats = _server->GetStatistics(player.dpid);

                    BString<512> output;
                    sprintf(output, "%s: Info %s", (const char*)player.name, (const char*)stats);
                    DebugAnswer((const char*)output);
                }
            }
        } // if (time for #debug)
    }

    // receive all system and user messages
    ReceiveSystemMessages();
    ReceiveLocalMessages();
    ReceiveUserMessages();
    PerformIntegrityInvestigations();

    // cancel all update messages
    for (int i = 0; i < _players.Size(); i++)
    {
        NetworkPlayerInfo& pInfo = _players[i];
        // statistics
        if (DiagLevel >= 3)
        {
            int nMsg, nBytes, nMsgG, nBytesG;
            _server->GetSendQueueInfo(pInfo.dpid, nMsg, nBytes, nMsgG, nBytesG);
            if (nMsg > 0 || nMsgG > 0)
            {
                if (nMsgG < 0)
                { // old style
                    DiagLogF("Server to %s: pending in SendQueue: %d messages, %d bytes", (const char*)pInfo.name, nMsg,
                             nBytes);
                }
                else
                { // new style
                    DiagLogF("Server to %s: pending in SendQueue: common - %d messages, %d bytes, guaranteed - %d "
                             "messages, %d bytes",
                             (const char*)pInfo.name, nMsg, nBytes, nMsgG, nBytesG);
                }
            }
        }
        // cancel messages
    }

    SimulateDS();

    // kick off players faded out
    CheckFadeOut();

    // destroy players kicked out
    for (int i = 0; i < _identities.Size(); i++)
    {
        PlayerIdentity& identity = _identities[i];
        if (identity.destroy && Glob.uiTime >= identity.destroyTime)
        {
            OnPlayerDestroy(identity.dpnid);
            i--;
        }
    }

    // send update messages
    SendMessages();

    DWORD tickCount = GlobalTickCount();
    if (tickCount >= _pingUpdateNext)
    {
        _pingUpdateNext = tickCount + 30000;

        // update all players information
        for (int i = 0; i < _players.Size(); i++)
        {
            NetworkPlayerInfo& pnet = _players[i];
            PlayerIdentity* id = FindIdentity(pnet.dpid);
            if (!id)
            {
                continue;
            }
            id->_minBandwidth = toLargeIntSat(pnet._bandwidth.GetMin() / (1024 / 8));
            id->_maxBandwidth = toLargeIntSat(pnet._bandwidth.GetMax() / (1024 / 8));
            id->_avgBandwidth = toLargeIntSat(pnet._bandwidth.GetAvg() / (1024 / 8));
            id->_minPing = toInt(pnet._ping.GetMin());
            id->_maxPing = toInt(pnet._ping.GetMax());
            id->_avgPing = toInt(pnet._ping.GetAvg());
            id->_desync = toInt(pnet._desync.GetAvg() * 100);
            saturate(id->_desync, 0, 100000);
            pnet._ping.Reset();
            pnet._bandwidth.Reset();
            pnet._desync.Reset();
        }

        // broadcast messages about all players to all players
        for (int i = 0; i < _players.Size(); i++)
        {
            NetworkPlayerInfo& info = _players[i];
            if (info.state < NGSCreate)
            {
                continue;
            }
            for (int i = 0; i < _identities.Size(); i++)
            {
                PlayerIdentity& pi = _identities[i];

                NetworkMessageType type = pi.GetNMType(NMCUpdatePosition);
                NetworkMessageFormatBase* format = GetFormat(type);

                Ref<NetworkMessage> msg = new NetworkMessage();
                msg->time = Glob.time;
                NetworkMessageContext ctx(msg, format, this, info.dpid, MSG_SEND);
                ctx.SetClass(NMCUpdatePosition);

                NET_ERROR(dynamic_cast<const IndicesPlayerUpdate*>(ctx.GetIndices()))
                const IndicesPlayerUpdate* indices = static_cast<const IndicesPlayerUpdate*>(ctx.GetIndices());

                TMError err;
                int dpnid = pi.dpnid;
                err = ctx.IdxTransfer(indices->dpnid, dpnid);
                if (err != TMOK)
                {
                    continue;
                }
                err = pi.TransferMsg(ctx);
                if (err != TMOK)
                {
                    continue;
                }

                // send message
                // NetworkComponent::
                NetworkComponent::SendMsg(info.dpid, msg, type, NMFNone);
            }
        }
    }

    // integrity checks
    DWORD time = GlobalTickCount();
    for (int i = 0; i < _players.Size(); i++)
    {
        NetworkPlayerInfo& info = _players[i];
        for (int t = 0; t < info.integrityQuestions.Size(); t++)
        {
            if (time > info.integrityQuestions[t].timeout)
            {
                OnIntegrityCheckFailed(info.dpid, (IntegrityQuestionType)t, nullptr, true);
                info.integrityQuestions.Delete(t);
                t--;
            }
        }
    }

    // squad checks
    for (int i = 0; i < _squadChecks.Size();)
    {
        CheckSquadObject* obj = _squadChecks[i];
        if (obj->IsDone())
        {
            PlayerIdentity& identity = obj->_identity;
            Ref<SquadIdentity> squad = obj->_squad;
            if (squad)
            {
                // only new squad is passed into CreateIdentity and added into _squads
                for (int i = 0; i < _squads.Size(); i++)
                {
                    if (_squads[i]->id == identity.squadId)
                    {
                        squad = nullptr;
                        break;
                    }
                }
            }
            CreateIdentity(identity, squad);

            _squadChecks.Delete(i);
        }
        else
        {
            i++;
        }
    }

    if (IsDedicatedServer())
    {
        UpdateMasterServerReporting();
    }

    NET_ERROR(CheckIntegrityOfPendingMessages());

    // DWORD tickCount = ::GlobalTickCount();
    //  check pending messages for timeout
    for (int p = 0; p < _pendingMessages.Size(); p++)
    {
        NetPendingMessage& pend = _pendingMessages[p];
        NetworkObjectInfo* oInfo = pend.info;
        NetworkPlayerObjectInfo* poInfo = pend.player;
        NetworkUpdateInfo* uInfo = pend.update;
        // NetworkUpdateInfo &info = poInfo->updates[j];
        // verify update is in player
        NET_ERROR(uInfo >= poInfo->updates && uInfo < poInfo->updates + NMCUpdateN);
        // verify player exists in given object
        bool found = false;
        for (int i = 0; i < oInfo->playerObjects.Size(); i++)
        {
            if (oInfo->playerObjects[i] == poInfo)
            {
                found = true;
            }
        }
        NET_ERROR(found);
        NET_ERROR(uInfo->lastCreatedMsgId == pend.msgID);
        if (uInfo->lastCreatedMsgId != pend.msgID)
        {
            continue;
        }
        if (uInfo->lastCreatedMsg && tickCount > uInfo->lastCreatedMsgTime + 5000)
        {
            RptF("Server: Network message %x is pending", uInfo->lastCreatedMsgId);
            // try to find it in pending message list

            uInfo->lastCreatedMsg = nullptr;
            uInfo->lastCreatedMsgId = 0xFFFFFFFF;
            uInfo->lastCreatedMsgTime = 0;

            _pendingMessages.Delete(p);
            p--;
        }
    }
    // check if sent confirmed for all outgoing messages
    for (int o = 0; o < _objects.Size(); o++)
    {
        NetworkObjectInfo* oInfo = _objects[o];
        for (int i = 0; i < oInfo->playerObjects.Size(); i++)
        {
            NetworkPlayerObjectInfo& poInfo = *oInfo->playerObjects[i];
            for (int j = NMCUpdateFirst; j < NMCUpdateN; j++)
            {
                NetworkUpdateInfo& info = poInfo.updates[j];
                if (info.lastCreatedMsg && tickCount > info.lastCreatedMsgTime + 5000)
                {
                    RptF("Server: Late pending discovery - message %x", info.lastCreatedMsgId);
                    // try to find it in pending message list
                    int pend = FindPendingMessage(info.lastCreatedMsgId);
                    if (pend >= 0)
                    {
                        _pendingMessages.Delete(pend);
                        RptF("  Deleted from pending message list");
                    }
                    else
                    {
                        Fail("  Message missing in pending message list");
                    }
                    Fail("Late pending discovery");

                    info.lastCreatedMsg = nullptr;
                    info.lastCreatedMsgId = 0xFFFFFFFF;
                    info.lastCreatedMsgTime = 0;
                }
            }
        }
    }

    // log all diagnostics
    WriteDiagOutput(true);

    // check for disconnected players
    for (int i = 0; i < _players.Size(); i++)
    {
        NetworkPlayerInfo& info = _players[i];
        if (info.state < NGSPlay)
        {
            continue;
        }
        if (info.dpid == _botClient)
        {
            continue;
        }
        float age = _server->GetLastMsgAgeReliable(info.dpid);
        if (info.connectionProblemsReported)
        {
            info.connectionProblemsReported = age > 5.0f;
        }
        else if (age >= 10.0f)
        {
            BString<256> message;
            sprintf(message, LocalizeString(IDS_MP_CONNECTION_LOOSING), (const char*)info.name);
            ServerMessage(message);
            info.connectionProblemsReported = true;
        }
    }
}

DEFINE_FAST_ALLOCATOR(NetworkObjectInfo)
