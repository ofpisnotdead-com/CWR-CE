#include <Poseidon/Foundation/Platform/VersionNo.h>
using namespace Poseidon;
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Core/ModSystem.hpp>
#include <Poseidon/Core/Version.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <Poseidon/Core/Config/Config.hpp>
#include <Poseidon/Network/NetworkImpl.hpp>
#include <Poseidon/IO/Filesystem/DirTree.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Core/Application.hpp>
// #include "strIncl.hpp"
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/Foundation/Platform/GamePaths.hpp>

#include <Poseidon/Network/NetworkConfig.hpp>
#include <Poseidon/Network/NetworkServerConfig.hpp>
#include <Poseidon/Network/NetworkMissionTransfer.hpp>
#include <Poseidon/Network/NetworkServerCommon.hpp>
#include <Poseidon/Network/MasterServerPublisher.hpp>
#include <Poseidon/Network/MasterServerServiceClient.hpp>
#include <algorithm>
#include <ctype.h>
#include <filesystem>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <Poseidon/Foundation/Common/Global.hpp>
#include <Poseidon/Foundation/Common/Win.h>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Memory/FastAlloc.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>
namespace Poseidon
{
RString GetServerConfig();
}

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
#endif
#ifdef _WIN32
#include <winsock2.h>
#endif
#ifdef _WIN32
#include <process.h>

namespace Poseidon
{
void DeleteDirectoryStructure(char const*, bool);
}
#endif

namespace Poseidon
{
RString GetUserDirectory();
}

namespace
{
const char* MasterServerPlatform()
{
#ifdef _WIN32
    return "win";
#else
    return "linux";
#endif
}
} // namespace

namespace Poseidon::Foundation
{
template class Ref<NetworkObject>;
} // namespace Poseidon::Foundation

// DISABLED: random integrity check
#define _ENABLE_RANDOM_INTEGRITY_CHECK 0

#define LOG_SEND_PROCESS 0

#define LOG_PLAYERS 1

extern const char* GameStateNames[];
extern int MaxCustomFileSize;

// enable diagnostic logs of message errors
#define LOG_ERRORS 0
// limit for diagnostic logs of message errors
const float LogErrorLimit = 1.0f;

DEFINE_FAST_ALLOCATOR(NetworkPlayerObjectInfo)

NetworkPlayerObjectInfo* NetworkObjectInfo::GetPlayerObjectInfo(int player)
{
    for (int i = 0; i < playerObjects.Size(); i++)
    {
        NetworkPlayerObjectInfo* info = playerObjects[i];
        if (info->player == player)
        {
            return info;
        }
    }
    return nullptr;
}

NetworkPlayerObjectInfo* NetworkObjectInfo::CreatePlayerObjectInfo(int player)
{
    NetworkPlayerObjectInfo* info = GetPlayerObjectInfo(player);
    if (info)
    {
        return info;
    }

    int index = playerObjects.Add();
    playerObjects[index] = new NetworkPlayerObjectInfo;
    NetworkPlayerObjectInfo& poInfo = *playerObjects[index];
    poInfo.player = player;
    for (int j = NMCUpdateFirst; j < NMCUpdateN; j++)
    {
        NetworkUpdateInfo& info = poInfo.updates[j];
        info.lastCreatedMsgId = 0xFFFFFFFF;
        info.lastCreatedMsgTime = 0;
    }
    return &poInfo;
}

void NetworkObjectInfo::DeletePlayerObjectInfo(int player)
{
    for (int i = 0; i < playerObjects.Size(); i++)
    {
        NetworkPlayerObjectInfo& info = *playerObjects[i];
        if (info.player == player)
        {
            playerObjects.Delete(i);
            return;
        }
    }
}
// Load ban list from file
void LoadBanList(RString filename, FindArray<__int64>& list)
{
    list.Clear();
    QIFStream f;
    f.open(filename);
    while (!f.eof() && !f.fail())
    {
        int c = f.get();
        if (f.eof() || f.fail())
        {
            return;
        }
        while (!isdigit(c))
        {
            c = f.get();
            if (f.eof() || f.fail())
            {
                return;
            }
        }
        __int64 value = 0;
        while (isdigit(c))
        {
            value *= 10;
            value += c - '0';
            c = f.get();
        }
        list.AddUnique(value);
    }
}

void SaveBanList(RString filename, const FindArray<__int64>& list)
{
    QOFStream f(filename);
    for (int i = 0; i < list.Size(); i++)
    {
        char buffer[32];
        _i64toa(list[i], buffer, 10);
        f.write(buffer, strlen(buffer));
        f.put('\r');
        f.put('\n');
    }
    f.close();
}

int GetServerBanCount()
{
    NetworkServer* srv = GNetworkManager.GetServer();
    return srv ? srv->GetBanCount() : -1;
}

RString GetServerFirstBanId()
{
    NetworkServer* srv = GNetworkManager.GetServer();
    return srv ? srv->GetFirstBanId() : RString();
}

bool GetServerLocked()
{
    NetworkServer* srv = GNetworkManager.GetServer();
    return srv && srv->IsSessionLocked();
}

// Return if given message type is kind of transport update
// When driver of transport changes, ownership of whole transport is changed.
bool IsUpdateTransport(NetworkMessageType type)
{
    switch (type)
    {
        case NMTUpdateTransport:
        case NMTUpdateTankOrCar:
        case NMTUpdateTank:
        case NMTUpdateCar:
        case NMTUpdateMotorcycle:
        case NMTUpdateAirplane:
        case NMTUpdateHelicopter:
        case NMTUpdateParachute:
        case NMTUpdateShip:
            return true;
        default:
            return false;
    }
}

// Find file bank with given prefix
QFBank* FindBank(const char* prefix)
{
    std::string normalized = platformPath(std::string(prefix));
    int prefixLen = normalized.size();
    for (int i = 0; i < GFileBanks.Size(); i++)
    {
        QFBank& bank = GFileBanks[i];
        if (strnicmp(bank.GetPrefix(), normalized.c_str(), prefixLen) == 0)
        {
            return &bank;
        }
    }
    return nullptr;
}

NetworkServer::NetworkServer(NetworkManager* parent, int port, RString password) : NetworkComponent(parent)
{
    _botClient = 0xFFFFFFFF;
    _state = NGSCreate;
    _stateEnteredTime = GlobalTickCount();
    _password = password.GetLength() > 0;

    _nextPlayerId = 1;

    Verify(Init(port, password));

    _param1 = FLT_MAX;
    _param2 = FLT_MAX;

    _kickDuplicate = false;
    _dedicated = false;
    _cadetMode = false;
    _missionIndex = -1;

    _gameMaster = AI_PLAYER;
    _admin = false;
    _restart = false;
    _reassign = false;

    Monitor(0);
    _debugNext = INT_MAX;
    _debugInterval = 0;

    _pingUpdateNext = GlobalTickCount() + 5000;

    _sessionLocked = false;

    _equalModRequired = false;

    // ensure no content remain
    if (_server)
    {
        Poseidon::DeleteDirectoryStructure(GetServerTmpDir(), false);
    }

    if (!IsDedicatedServer())
    {
        LoadBanList(Poseidon::GetUserDirectory() + RString("ban.txt"), _banListLocal);
        Poseidon::LoadIpBanList(Poseidon::GetUserDirectory() + RString("ipban.txt"), _banListIPLocal);
    }
}

NetTranspServer* __cdecl InitServer(int port, RString password, const ParamEntry& cfg);

// SafeDisk protected version of NetworkServer::Init

NetTranspServer* __cdecl InitServer(int port, RString password, const ParamEntry& cfg)
{
    {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "%sTmp%d", GamePaths::Instance().TempDir().c_str(), port);
        ServerTmpDir = buffer;
    }
    LOG_INFO(Network, "Creating server with Sockets transport on port {}", port);
    NetTranspServer* server = CreateNetServer(cfg);
    if (!server)
    {
        LOG_ERROR(Network, "Failed to create network transport server");
        return nullptr;
    }
    LOG_INFO(Network, "Network transport server created successfully");
    // load some settings from Flashpoint.cfg
    server->SetNetworkParams(cfg);

    RString sessionNameInit = LocalizeString(IDS_SESSION_NAME_INIT);
    RString sessionNameFormat = LocalizeString(IDS_SESSION_NAME_FORMAT);
    RString playerName;

    MPVersionInfo versionInfo;
    versionInfo.versionActual = MP_VERSION_ACTUAL;
    versionInfo.versionRequired = MP_VERSION_REQUIRED;
    versionInfo.mission[0] = 0;
    versionInfo.gameState = NGSCreate;
    // Advertise the mod FOLDER NAMES (e.g. "@CSLA;@mymod"), not the server's local mount
    // paths — that's the cross-machine identity clients match on + the browser shows, and
    // it keeps the field well under MOD_LENGTH (absolute paths truncate mid-name).
    strncpy(versionInfo.mod, ModSystem::GetModNames(), MOD_LENGTH);
    versionInfo.mod[MOD_LENGTH - 1] = 0;
    strncpy(versionInfo.versionTag, GetVersionTag(), VERSION_TAG_LENGTH);
    versionInfo.versionTag[VERSION_TAG_LENGTH - 1] = 0;

    bool equalModRequired = false;

    char hostname[512];
    hostname[0] = 0;
    int maxPlayers = 0;
    bool voiceOverNet = true;

    if (IsDedicatedServer())
    {
        ParamFile cfg;
        cfg.Parse(Poseidon::GetServerConfig());

        const ParamEntry* entry = cfg.FindEntry("hostname");
        if (entry)
        {
            RString hn = *entry;
            strncpy(hostname, hn, 512);
            hostname[511] = 0;
        }
        entry = cfg.FindEntry("maxPlayers");
        if (entry)
        {
            maxPlayers = *entry;
            maxPlayers += 2;
        }

        entry = cfg.FindEntry("voiceOverNet");
        if (entry)
        {
            voiceOverNet = entry->GetInt() != 0;
        }

        entry = cfg.FindEntry("equalModRequired");
        if (entry)
        {
            equalModRequired = *entry;
        }

        // The dedicated server config (-config) takes precedence over the main cfg
        // read in Init() for the custom-file cap, so admins can set it in server.cfg.
        MaxCustomFileSize = Poseidon::NetworkMaxCustomFileSizeFromCfg(cfg, MaxCustomFileSize);

        // on dedicated server, use only server name as session name
    }
    else
    {
        playerName = Glob.header.playerName;
    }

    LOG_INFO(Network, "Initializing server: port={}, maxPlayers={}, hostname='{}'", port, maxPlayers, hostname);
    bool result = server->Init(port, password, hostname, maxPlayers, sessionNameInit, sessionNameFormat, playerName,
                               versionInfo, equalModRequired, MAGIC_APP);
    if (!result)
    {
        LOG_ERROR(Network, "Server Init() failed - possibly port {} already in use or network unavailable", port);
        delete server;
        return nullptr;
    }
    LOG_INFO(Network, "Server initialized successfully on port {}", server->GetSessionPort());

    if (voiceOverNet)
    {
        server->InitVoice();
    }

    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%sTmp%d", GamePaths::Instance().TempDir().c_str(), server->GetSessionPort());
    ServerTmpDir = buffer;

    return server;
}

bool NetworkServer::Init(int port, RString password)
{
    // load some settings from Flashpoint.cfg
    ParamFile cfg;
    cfg.Parse(FlashpointCfg);

    const ParamEntry* entry = cfg.FindEntry("proxy");
    if (entry)
    {
        _proxy = *entry;
    }

    // Server-configurable cap on client custom files (radio sounds, faces); a larger
    // transfer kicks the client. Applies to dedicated and self-hosted servers alike,
    // mirroring the original OFP Flashpoint.cfg "MaxCustomFileSize" key. 0 blocks all.
    MaxCustomFileSize = Poseidon::NetworkMaxCustomFileSizeFromCfg(cfg, MaxCustomFileSize);

    _server = InitServer(port, password, cfg);
    return _server != nullptr;
}

NetworkServer::~NetworkServer()
{
    RemoveSystemMessages();

    if (IsDedicatedServer())
    {
        StopMasterServerReporting();
    }

    Done();
}

void NetworkServer::Done()
{
    RemoveUserMessages();
    if (_server)
    {
        Poseidon::DeleteDirectoryStructure(GetServerTmpDir(), true);
    }
    _server = nullptr;
}

int NetworkServer::GetMaxPlayers()
{
    int maxPlayers = 64;

    if (IsDedicatedServer())
    {
        ParamFile cfg;
        cfg.Parse(Poseidon::GetServerConfig());

        const ParamEntry* entry = cfg.FindEntry("maxPlayers");
        if (entry)
        {
            maxPlayers = *entry;
        }
    }

    if (_state >= NGSPrepareSide)
    {
        saturateMin(maxPlayers, _playerRoles.Size());
    }
    maxPlayers += 2;
    return maxPlayers;
}

bool NetworkServer::GetURL(char* address, DWORD addressLen)
{
    if (!_server)
    {
        return false;
    }
    return _server->GetURL(address, addressLen);
}

void NetworkServer::UpdateSessionDescription()
{
    RString mission;
    if (_state >= NGSPrepareSide)
    {
        mission = _missionHeader.name;
    }
    _server->UpdateSessionDescription(_state, mission);
}

static bool service_registration_callback(MasterServerServiceRegistration& out, void* userdata)
{
    return ((NetworkServer*)userdata)->FillMasterServerServiceRegistration(out);
}

// Null-safe RString -> std::string (an empty RString may cast to nullptr).
static std::string ToStdString(const RString& value)
{
    return value.GetLength() > 0 ? std::string((const char*)value) : std::string();
}

// "Title: <selected value's label>" for a mission parameter, or "" when the mission
// has no such parameter. Falls back to the numeric value when no label matches.
static std::string FormatMissionParam(const RString& title, const AutoArray<float>& values,
                                      const AutoArray<RString>& texts, float selected)
{
    if (title.GetLength() <= 0)
    {
        return std::string();
    }
    RString label;
    for (int i = 0; i < values.Size() && i < texts.Size(); i++)
    {
        if (values[i] == selected)
        {
            label = texts[i];
            break;
        }
    }
    if (label.GetLength() <= 0)
    {
        label = Format("%.0f", selected);
    }
    return ToStdString(title) + ": " + ToStdString(label);
}

bool NetworkServer::FillMasterServerServiceRegistration(MasterServerServiceRegistration& registration)
{
    if (GetNetworkMasterServer().GetLength() <= 0 || _server == nullptr)
    {
        return false;
    }

    char addressBuffer[256] = {};
    if (!GetURL(addressBuffer, sizeof(addressBuffer)))
    {
        return false;
    }

    std::string advertisedUrl = addressBuffer;
    RString advertiseAddress = GetNetworkAdvertiseAddress();
    if (advertiseAddress.GetLength() > 0)
    {
        advertisedUrl = std::string((const char*)advertiseAddress) + ":" + std::to_string(::GetNetworkPort());
    }
    LOG_INFO(Network, "Master-server publish address: {}", advertisedUrl);

    if (!BuildMasterServerServiceRegistrationFromServerUrl(
            advertisedUrl.c_str(), _server->GetSessionName(), _state, _state >= NGSPrepareSide,
            _state >= NGSPrepareSide ? (const char*)_missionHeader.name : nullptr, MP_VERSION_ACTUAL,
            MP_VERSION_REQUIRED, (const char*)GetVersionTag(), _password, _identities.Size(), GetMaxPlayers(),
            ModSystem::GetModNames(), _equalModRequired, MasterServerPlatform(), registration))
    {
        return false;
    }

    registration.stateElapsedSeconds = (int)((GlobalTickCount() - _stateEnteredTime) / 1000);

    // Estimated minutes left, only meaningful once playing.
    int timeLeft = 0;
    if (_state == NGSPlay)
    {
        timeLeft = 15;
        float et = _missionHeader.estimatedEndTime.toFloat();
        if (et > 0)
        {
            et -= Glob.time.toFloat();
            timeLeft = toInt(et / 60.0);
            saturateMax(timeLeft, 1);
        }
    }
    registration.timeLeft = timeLeft;

    if (_state >= NGSPrepareSide)
    {
        registration.island = ToStdString(_missionHeader.island);
        registration.description = ToStdString(_missionHeader.description);
        registration.param1 = FormatMissionParam(_missionHeader.titleParam1, _missionHeader.valuesParam1,
                                                 _missionHeader.textsParam1, _param1);
        registration.param2 = FormatMissionParam(_missionHeader.titleParam2, _missionHeader.valuesParam2,
                                                 _missionHeader.textsParam2, _param2);
        std::string addons;
        for (int i = 0; i < _missionHeader.addOns.Size(); i++)
        {
            if (i > 0)
            {
                addons += ";";
            }
            addons += ToStdString(_missionHeader.addOns[i]);
        }
        registration.requiredAddons = addons;
    }
    registration.cadetMode = _missionHeader.cadetMode;
    int difficulty = 0;
    for (int i = 0; i < _missionHeader.difficulty.Size() && i < 32; i++)
    {
        if (_missionHeader.difficulty[i])
        {
            difficulty |= (1 << i);
        }
    }
    registration.difficulty = difficulty;
    registration.joinInProgress = _missionHeader.joinInProgress;
    registration.disabledAI = _missionHeader.disabledAI;
    registration.respawn = _missionHeader.respawn;
    registration.respawnDelay = toInt(_missionHeader.respawnDelay);
    registration.sessionLocked = _sessionLocked;
    registration.dedicated = _dedicated;
    return true;
}

// Publishing is opt-in for dedicated servers. The master-server URL may still be
// configured for client browsing; it is not enough to publish a server.
void NetworkServer::StartMasterServerReporting()
{
    RString masterServerHost = GetNetworkMasterServer();
    if (!IsDedicatedServer() || !GetNetworkPublicServer() || masterServerHost.GetLength() <= 0)
    {
        return;
    }

    MasterServerPublisherCallbacks callbacks{};
    callbacks.serviceRegistrationQuery = service_registration_callback;
    callbacks.userdata = this;
    StartMasterServerPublisher(masterServerHost, _proxy, ::GetNetworkPort() + 1, callbacks);
}

void NetworkServer::StopMasterServerReporting()
{
    if (!IsDedicatedServer() || !GetNetworkPublicServer() || GetNetworkMasterServer().GetLength() <= 0)
    {
        return;
    }
    StopMasterServerPublisher();
}

void NetworkServer::UpdateMasterServerReporting()
{
    if (!IsDedicatedServer() || !GetNetworkPublicServer() || GetNetworkMasterServer().GetLength() <= 0)
    {
        return;
    }
    TickMasterServerPublisher();
}

// Add list of missions to Network Command Message
void AddMissionList(NetworkCommandMessage& answer)
{
    std::vector<std::string> added;
    auto addMission = [&](const char* name, const std::string& sourcePath, const char* type)
    {
        if (std::find(added.begin(), added.end(), std::string(name)) != added.end())
        {
            LOG_DEBUG(Network, "MP mission duplicate skipped: name={} type={} path={}", name, type, sourcePath);
            return;
        }
        added.emplace_back(name);
        answer.content.WriteString(name);
        LOG_DEBUG(Network, "MP mission discovered: name={} type={} path={}", name, type, sourcePath);
    };

    for (const std::string& dir : Poseidon::GetMPMissionLookupDirectories())
    {
        LOG_DEBUG(Network, "Scanning MP missions in {}", dir);

        // add mission banks
        _finddata_t info;
        const std::string pboMask = (std::filesystem::path(dir) / "*.pbo").string();
        intptr_t h = _findfirst(pboMask.c_str(), &info);
        if (h != -1)
        {
            do
            {
                if ((info.attrib & _A_SUBDIR) == 0)
                {
                    char name[256];
                    snprintf(name, sizeof(name), "%s", (const char*)info.name);
                    char* ext = strrchr(name, '.'); // remove extension .pbo
                    *ext = 0;
                    addMission(name, (std::filesystem::path(dir) / info.name).string(), "pbo");
                }
            } while (_findnext(h, &info) == 0);
            _findclose(h);
        }

        // add mission directories
        const std::string dirMask = (std::filesystem::path(dir) / "*.*").string();
        h = _findfirst(dirMask.c_str(), &info);
        if (h != -1)
        {
            do
            {
                if ((info.attrib & _A_SUBDIR) != 0 && info.name[0] != '.')
                {
                    addMission(info.name, (std::filesystem::path(dir) / info.name).string(), "directory");
                }
            } while (_findnext(h, &info) == 0);
            _findclose(h);
        }
    }
    LOG_DEBUG(Network, "MP mission scan complete: {} templates", added.size());
    answer.content.WriteString("");
}

void NetworkServer::ApplyVoting(const AutoArray<char>& id, const AutoArray<char>* value)
{
    int* ptr = (int*)id.Data();
    int type = *(ptr++);
    switch (type)
    {
        case NCMTMission:
            if (value)
            {
                const char* p = value->Data();
                _mission = p;
                p += strlen(p) + 1;
                _cadetMode = *(bool*)p;
                if (_mission.GetLength() > 0)
                {
                    _restart = true;
                }
                if (_state != NGSPlay)
                {
                    GNetworkManager.DestroyAllObjects();
                    SetGameState(NGSCreate);
                }
            }
            break;
        case NCMTMissions:
        {
            _mission = "?";
            _restart = true;
            if (_state != NGSPlay)
            {
                GNetworkManager.DestroyAllObjects();
                SetGameState(NGSCreate);
            }
            NetworkCommandMessage answer;
            answer.type = NCMTVoteMission;
            AddMissionList(answer);
            for (int i = 0; i < _identities.Size(); i++)
            {
                SendMsg(_identities[i].dpnid, &answer, NMFGuaranteed);
            }
        }
        break;
        case NCMTRestart:
            if (_state == NGSPlay)
            {
                _restart = true;
                _reassign = false;
            }
            break;
        case NCMTReassign:
            _restart = true;
            _reassign = true;
            if (_state != NGSPlay)
            {
                GNetworkManager.DestroyAllObjects();

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
            break;
        case NCMTKick:
        {
            int player = *(ptr++);
            if (player != _botClient)
            {
                KickOff(player, KORKick);
            }
        }
        break;
        case NCMTAdmin:
            if (value)
            {
                const char* p = value->Data();
                int player = *((int*)p);
                if (player != AI_PLAYER && _dedicated && _gameMaster == AI_PLAYER)
                {
                    _gameMaster = player;
                    _admin = true;
                    RString name;
                    for (int i = 0; i < _players.Size(); i++)
                    {
                        if (_players[i].dpid == player)
                        {
                            name = _players[i].name;
                            break;
                        }
                    }
                    LOG_INFO(Network, "Admin {} logged in", (const char*)name);

                    _votings.Clear();

                    NetworkCommandMessage answer;
                    answer.type = NCMTLogged;
                    answer.content.Write(&_admin, sizeof(_admin));
                    SendMsg(player, &answer, NMFGuaranteed);

                    if (_mission[0] == '?' && _mission[1] == 0)
                    {
                        NetworkCommandMessage answer;
                        answer.type = NCMTVoteMission;
                        AddMissionList(answer);
                        SendMsg(player, &answer, NMFGuaranteed);
                    }

                    UpdateAdminState();
                }
            }
            break;
    }
}

void OnServerUserMessage(int from, char* buffer, int bufferSize, void* context)
{
    NetworkServer* server = (NetworkServer*)context;

    // A datagram must be long enough to contain its trailing CRC int; otherwise the
    // size below would go negative. Drop it.
    if (bufferSize < (int)sizeof(int))
    {
        return;
    }
    bufferSize -= sizeof(int);
    int crc = *(int*)(buffer + bufferSize);
    static Poseidon::Foundation::CRCCalculator calculator;
    if (calculator.CRC(buffer, bufferSize) == crc)
    {
        NetworkMessageRaw rawMsg(buffer, bufferSize);
        server->DecodeMessage(from, rawMsg);
    }
    else
    {
        Fail("Bad CRC for incoming message");
    }
}

void OnServerRawMagicMessage(int from, int magic, const char* buffer, int bufferSize, void* context)
{
    NetworkServer* server = (NetworkServer*)context;
    server->OnRawMagicMessage(from, magic, buffer, bufferSize);
}

void OnServerSendComplete(DWORD msgID, bool ok, void* context)
{
    NetworkServer* server = (NetworkServer*)context;
    server->OnSendComplete(msgID, ok);
}

void OnServerPlayerCreate(int player, bool botClient, const char* name, const char* mod, void* context)
{
    if (!botClient && stricmp(mod, ModSystem::GetModNames()) != 0)
    {
        char message[512];
        RString format = LocalizeString(IDS_MP_VALIDERROR_2);

        snprintf(message, sizeof(message), format, name);
        if (mod && *mod)
        {
            sprintf(message + strlen(message), " - %s", mod);
        }

        RefArray<NetworkObject> dummy;
        GNetworkManager.Chat(CCGlobal, "", dummy, message);
        GChatList.Add(CCGlobal, nullptr, message, false, true);
        LOG_INFO(Network, "{}", message);
    }

    NetworkServer* server = (NetworkServer*)context;
    server->OnCreatePlayer(player, botClient, name);
}

void OnServerPlayerDelete(int player, void* context)
{
    NetworkServer* server = (NetworkServer*)context;
    server->OnPlayerDestroy(player);
}

bool OnServerVoicePlayerCreate(int player, void* context)
{
    NetworkServer* server = (NetworkServer*)context;
    return server->OnCreateVoicePlayer(player);
}

unsigned NetworkServer::CleanUpMemory()
{
    if (_server)
    {
        return _server->FreeMemory();
    }
    return 0;
}

void NetworkServer::ReceiveSystemMessages()
{
    if (_server)
    {
        _server->ProcessSendComplete(OnServerSendComplete, this);
        _server->ProcessPlayers(OnServerPlayerCreate, OnServerPlayerDelete, this);
        _server->ProcessVoicePlayers(OnServerVoicePlayerCreate, this);
    }
}

void NetworkServer::ReceiveUserMessages()
{
    if (_server)
    {
        _server->ProcessUserMessages(OnServerUserMessage, this);
        _server->ProcessRawMagicMessages(OnServerRawMagicMessage, this);
    }
}

void NetworkServer::OnRawMagicMessage(int from, int magic, const char* buffer, int bufferSize)
{
    if (magic != Poseidon::NetworkMissionBulkRawMagic)
    {
        return;
    }

    Poseidon::NetworkMissionBulkRawPacket packet;
    if (!Poseidon::DecodeNetworkMissionBulkRawPayload(buffer, bufferSize, packet) ||
        packet.kind != Poseidon::NetworkMissionBulkRawKind::Request)
    {
        LOG_WARN(Network, "[NMTTransferMission] rejected malformed raw mission request from {} size={}", from,
                 bufferSize);
        return;
    }

    QIFStreamB f;
    RString src = _missionBank;
    f.AutoOpen(src);
    const auto source = Poseidon::BuildNetworkMissionFileSendSource(src, _missionHeader.fileName, f.GetBuffer());
    if (!source.data || source.totalSize != packet.totalSize)
    {
        LOG_WARN(Network, "[NMTTransferMission] rejected raw resend request from {} size={} actual={}", from,
                 packet.totalSize, source.totalSize);
        return;
    }

    const int sent = Poseidon::SendNetworkMissionRawFileSegmentRange<TransferMissionFileMessage>(
        source.destinationPath, source.data, source.totalSize, packet.curSegment, packet.segmentCount,
        [this, from](AutoArray<char>& payload)
        {
            _server->SendRawMagic(from, Poseidon::NetworkMissionBulkRawMagic, reinterpret_cast<BYTE*>(payload.Data()),
                                  payload.Size());
        });
    LOG_DEBUG(Network, "[NMTTransferMission] raw resend from {} firstSegment={} count={} sent={}", from,
              packet.curSegment, packet.segmentCount, sent);
}

void NetworkServer::RemoveSystemMessages()
{
    if (_server)
    {
        _server->RemoveSendComplete();
        _server->RemovePlayers();
        _server->RemoveVoicePlayers();
    }
}

void NetworkServer::RemoveUserMessages()
{
    if (_server)
    {
        _server->RemoveUserMessages();
    }
}
