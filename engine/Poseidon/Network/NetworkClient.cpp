#include <Poseidon/Foundation/Platform/AppConfig.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/PlayerMuteIgnore.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Core/ModSystem.hpp>
#include <Poseidon/Core/Version.hpp>
#include <Poseidon/Core/Config/Config.hpp>
#include <Poseidon/Network/NetworkClientCommon.hpp>
#include <Poseidon/IO/Filesystem/DirTree.hpp>
#include <Poseidon/Network/NetworkConfig.hpp>
#include <Poseidon/Network/NetworkCustomAssets.hpp>
#include <Poseidon/Network/NetworkMissionTransfer.hpp>
#include <Poseidon/Network/WireBounds.hpp>
#include <Poseidon/Core/Global.hpp>
// #include "strIncl.hpp"
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/Foundation/Platform/GamePaths.hpp>

#include <Poseidon/AI/ArcadeTemplate.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/Network/XML/Xml.hpp>
#include <Poseidon/Foundation/Strings/StrFormat.hpp>

#include <Poseidon/Foundation/Algorithms/Qsort.hpp>
#include <filesystem>

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

#include <Poseidon/Input/InputSubsystem.hpp>
#include <SDL3/SDL_scancode.h>
#include <Poseidon/Game/UiActions.hpp>

#include <Poseidon/Foundation/Algorithms/Crc.hpp>

// #include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <system_error>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <Poseidon/Foundation/Common/Win.h>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/BankArray.hpp>
#include <Poseidon/Foundation/Containers/RStringArray.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>
#include <Poseidon/Foundation/Types/Memtype.h>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/Types/RemoveLinks.hpp>
#include <Poseidon/Foundation/platform.hpp>
#ifdef _WIN32
#include <io.h>
#endif

using namespace Poseidon;
namespace Poseidon
{
void RunMissionScript(char const*, class ::GameValue);
RString GetServerConfig();
RString GetUserDirectory();
RString GetUserParams();
} // namespace Poseidon

using Poseidon::Foundation::QSort;

// Time after that body can disappear (in seconds)
#define OLD_BODY 10

// Number of bodies on bot client
#define BODIES_ON_BOT_CLIENT 10

// Number of bodies on all other clients
#define BODIES_ON_CLIENTS 20

// enable diagnostic logs of message errors on client
#define LOG_CLIENT_ERRORS 0
// limit for diagnostic logs of message errors on client
const float LogClientErrorLimit = 0.01f;

#define LOG_SEND_PROCESS 0

const char* GameStateNames[] = {"None",          "Creating",         "Create",       "Login",      "Edit",
                                "Mission Voted", "Prepare Side",     "Prepare Role", "Prepare OK", "Debriefing",
                                "Debriefing OK", "Transfer Mission", "Load Island",  "Briefing",   "Play"};

static bool ShouldInitializeClientVoice(bool botClient)
{
    if (IsDedicatedServer() && botClient)
    {
        return false;
    }

    if (!IsDedicatedServer())
    {
        return true;
    }

    ParamFile cfg;
    cfg.Parse(Poseidon::GetServerConfig());

    const ParamEntry* entry = cfg.FindEntry("voiceOverNet");
    if (!entry)
    {
        return true;
    }

    return entry->GetInt() != 0;
}

// Network client itself

NetworkClient::NetworkClient(NetworkManager* parent, RString address, RString password, bool botClient)
    : NetworkComponent(parent)
{
    Verify(Init(address, password, botClient));
    _state = NGSCreating;
    _serverState = NGSCreating;

    _connectionQuality = CQGood;

    _localPlayerName = Glob.header.playerName;

    _missionFileValid = false;

    _gameMaster = false;
    _admin = false;
    _selectMission = false;
    _voteMission = false;

    _controlsPaused = false;
    _jip = false;
    _pendingSelectPlayer = false;
    _missionRawLastRequestTime = 0;
    _missionRawLastSegmentTime = 0;
    _missionRawExpectedSize = 0;
    _missionRawHighestReceivedSegment = -1;
    _missionRawRequestedDuplicateUniqueSegments = 0;
    _missionTransferHeaderStatsLogged = false;

    _soundId = 0; // incremented for each PlaySound

    GetValue(_chatSound, Pars >> "CfgInGameUI" >> "Chat" >> "sound");

    // ensure no content remain
    Poseidon::DeleteDirectoryStructure("tmp", false);

    _clientInfo = new ClientInfoObject();

    _hideBodies = 0;
}

bool NetworkClient::Init(RString address, RString password, bool botClient)
{
    // load some settings from Flashpoint.cfg
    ParamFile cfg;
    cfg.Parse(FlashpointCfg);

    _client = CreateNetClient(cfg);
    if (!_client)
    {
        _connectResult = CRError;
        return false;
    }

    _client->SetNetworkParams(cfg);

    int connectPort = GetNetworkConnectPort();
    if (connectPort <= 0)
    {
        connectPort = GetNetworkPort();
    }
    MPVersionInfo versionInfo;
    versionInfo.versionActual = MP_VERSION_ACTUAL;
    versionInfo.versionRequired = MP_VERSION_REQUIRED;
    strncpy(versionInfo.mod, ModSystem::GetModNames(), MOD_LENGTH); // folder names, not local paths
    versionInfo.mod[MOD_LENGTH - 1] = 0;
    strncpy(versionInfo.versionTag, GetVersionTag(), VERSION_TAG_LENGTH);
    versionInfo.versionTag[VERSION_TAG_LENGTH - 1] = 0;
    _connectResult =
        _client->Init(address, password, botClient, connectPort, Glob.header.playerName, versionInfo, MAGIC_APP);
    if (_connectResult != CROK)
    {
        _client = nullptr;
        return false;
    }

    if (ShouldInitializeClientVoice(botClient))
    {
        _client->InitVoice();
    }

    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%sTmp%d", GamePaths::Instance().TempDir().c_str(), GetNetworkPort());
    ServerTmpDir = buffer;

    return true;
}

NetworkClient::~NetworkClient()
{
    RemoveSystemMessages();

    Done();
    // Mute/ignore are per-session, client-local: drop them when the MP client
    // tears down so they don't leak into the next session.
    ClearMuteIgnore();
    // Drop the server difficulty override so it doesn't leak into a later session.
    USER_CONFIG.ClearServerDifficulty();
    Poseidon::DeleteDirectoryStructure("tmp", true);
}

void NetworkClient::Done()
{
    RemoveUserMessages();

    _soundBuffers.Clear();

    _client = nullptr;
}

int CmpBodies(const BodyInfo* info1, const BodyInfo* info2)
{
    float diff = info1->value - info2->value;
    return sign(diff);
}

void NetworkClient::DisposeBody(Person* body)
{
    NET_ERROR(body->IsLocal());

    // Add body to queue
    int index = _bodies.Add();
    BodyInfo& bodyInfo = _bodies[index];
    bodyInfo.body = body;
    bodyInfo.hideTime = Glob.time + OLD_BODY;
    bodyInfo.value = 0;

    // Recalculate body values
    for (int i = 0; i < _bodies.Size(); i++)
    {
        BodyInfo& bodyInfo = _bodies[i];
        if (!bodyInfo.body)
        {
            _bodies.Delete(i);
            i--;
            continue;
        }
        bodyInfo.value = 0;
        for (int w = 0; w < bodyInfo.body->NWeaponSystems(); w++)
        {
            const WeaponType* weapon = bodyInfo.body->GetWeaponSystem(w);
            if (weapon)
            {
                bodyInfo.value += weapon->_value;
            }
        }
        for (int m = 0; m < bodyInfo.body->NMagazines(); m++)
        {
            const Magazine* magazine = bodyInfo.body->GetMagazine(m);
            const MagazineType* type = magazine ? magazine->_type : nullptr;
            if (type)
            {
                bodyInfo.value += type->_value;
            }
        }
        if (bodyInfo.body->GetFlagCarrier())
        {
            bodyInfo.value += 1000;
        }
    }

    // Sort values
    QSort(_bodies.Data(), _bodies.Size(), CmpBodies);

    // Remove old and unvaluable bodies
    for (int i = 0; i < _bodies.Size(); i++)
    {
        if (_hideBodies <= 0)
        {
            break;
        }

        BodyInfo& bodyInfo = _bodies[i];
        if (bodyInfo.value >= 1000)
        {
            break;
        }
        if (bodyInfo.hideTime > Glob.time)
        {
            continue;
        }

        NET_ERROR(bodyInfo.body->IsLocal());
        bodyInfo.body->HideBody();
        _bodies.Delete(i);
        _hideBodies--;
        i--;
    }
}

void NetworkClient::DoRespawn(RespawnQueueItem& item)
{
    if (_state < NGSPlay)
    {
        return; // send event messages only when playing
    }

    Person* body = item.person;
    if (!body)
    {
        Fail("Respawn failed - body disappeared");
        return;
    }
    AIUnit* unit = body->Brain();
    if (!unit)
    {
        Fail("Respawn failed - unit disappeared");
        return;
    }
    TargetSide side = body->Vehicle::GetTargetSide();
    int id = -1;
    for (int i = 0; i < vehiclesMap.Size(); i++)
    {
        if (vehiclesMap[i] == body)
        {
            id = i;
            break;
        }
    }

    // new body
    Soldier* soldier = new Soldier(const_cast<VehicleType*>(body->GetType()));

    // move brain
    soldier->SetBrain(unit);
    body->SetBrain(nullptr);
    GLOB_WORLD->RemoveSensor(body);
    unit->SetPerson(soldier);
    unit->SetVehicleIn(nullptr);

    // other parameters
    soldier->SetTargetSide(side);
    unit->SetLifeState(AIUnit::LSAlive);

    AIUnitInfo& info = body->GetInfo();
    saturateMax(info._experience, info._initExperience);
    soldier->SetInfo(info);
    soldier->SetRemotePlayer(body->GetRemotePlayer());
    soldier->SetFace(info._face, info._name);
    soldier->SetGlasses(info._glasses);

    RString var = body->GetVarName();
    if (var.GetLength() > 0)
    {
        soldier->SetVarName(var);
        GWorld->GetGameState()->VarSet(var, GameValueExt(soldier), true);
    }

    // weapons
    soldier->RemoveAllWeapons();
    soldier->RemoveAllMagazines();
    soldier->AddDefaultWeapons();
    switch (side)
    {
        case TWest:
            soldier->AddMagazine("M16");
            soldier->AddWeapon("M16");
            break;
        case TEast:
            soldier->AddMagazine("AK74");
            soldier->AddWeapon("AK74");
            break;
        case TGuerrila:
            soldier->AddMagazine("AK47");
            soldier->AddWeapon("AK47");
            break;
    }

    // add to world
    Matrix4 pos;
    pos.SetOrientation(Matrix3(MRotationY, -H_PI * 2 * GRandGen.RandomValue()));
    pos.SetPosition(item.position);
    soldier->SetTransform(pos);
    soldier->Init(pos);
    GLOB_WORLD->AddVehicle(soldier);
    if (id >= 0)
    {
        vehiclesMap[id] = soldier;
    }

    CreateVehicle(soldier, VLTVehicle, var, id);
    AttachPerson(soldier);

    // FIX: try to asure player is valid whenever unit is respawned
    if (item.player)
    {
        SelectPlayer(_player, soldier, true);
    }

    UpdateObject(soldier, NMFGuaranteed);
    UpdateObject(unit, NMFGuaranteed);

    unit->SendAnswer(AI::ReportPosition);

    // single body can disappear
    _hideBodies++;

    // Add body to queue
    DisposeBody(body);
}

NetMsgFlags NetworkClient::GetChatPriority() const
{
    if (GetServerState() == NGSPlay)
    {
        return NMFNone;
    }
    return NMFHighPriority;
}

void NetworkClient::SetParams(float param1, float param2)
{
    _param1 = param1;
    _param2 = param2;

    MissionParamsMessage msg;
    msg._param1 = param1;
    msg._param2 = param2;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::OnSimulate()
{
    // raw statistics
#if _ENABLE_CHEATS
    // remove statistics info
    static AutoArray<int> texts;
    for (int i = 0; i < texts.Size(); i++)
    {
        if (texts[i] >= 0)
            GEngine->RemoveText(texts[i]);
    }
    texts.Resize(0);

    if (outputDiags == 4)
    {
        NET_ERROR(_rawStatistics.Size() <= 1);
        if (_rawStatistics.Size() > 0)
        {
            char output[1024];
            snprintf(output, sizeof(output), "Server: sent %d (%d), received %d (%d)", _rawStatistics[0].sizeSent,
                     _rawStatistics[0].msgSent, _rawStatistics[0].sizeReceived, _rawStatistics[0].msgReceived);
            texts.Add(GEngine->ShowTextF(1000, 10, 15, output));
        }
    }
#endif

    _connectionQuality = CQGood;

#if _ENABLE_CHEATS
    auto& input = InputSubsystem::Instance();
    if (input.GetCheat2ToDo(SDL_SCANCODE_COMMA))
    {
        outputLogs = !outputLogs;
        if (outputLogs)
            GEngine->ShowMessage(500, "message logs on");
        else
            GEngine->ShowMessage(500, "message logs off");
    }
    if (input.GetCheat2ToDo(SDL_SCANCODE_SEMICOLON))
    {
        outputDiags++;
        if (outputDiags > nOutputDiags)
            outputDiags = 0;
    }
    if (input.GetCheat2ToDo(SDL_SCANCODE_APOSTROPHE))
    {
        _statistics.Clear();
        if (_parent->GetServer())
            _parent->GetServer()->GetStatistics().Clear();
    }
#endif

    if (_remoteObjects.Size() > 0 && !_parent->IsServer())
    {
        float age = _client->GetLastMsgAge();

        if (age > 10)
        {
            if (_connectionQuality < CQBad)
            {
                _connectionQuality = CQBad;
            }

            float ageReported = _client->GetLastMsgAgeReported();
            if (ageReported >= 5)
            {
                char message[256];
                snprintf(message, sizeof(message), LocalizeString(IDS_MP_NO_MESSAGE), age);
                GChatList.Add(CCGlobal, nullptr, message, false, true);
                _client->LastMsgAgeReported();
            }
        }
        else if (age > 5)
        {
            if (_connectionQuality < CQPoor)
            {
                _connectionQuality = CQPoor;
            }
        }
    }

    _controlsPaused = _client->GetLastMsgAgeReliable() > 10.0f;

    // receive all system and user messages
    ReceiveSystemMessages();
    ReceiveLocalMessages();
    ReceiveUserMessages();
    RequestMissingMissionRawSegments();

    // statistics
    if (DiagLevel >= 3)
    {
        int nMsg, nBytes, nMsgG, nBytesG;
        _client->GetSendQueueInfo(nMsg, nBytes, nMsgG, nBytesG);
        if (nMsg > 0 || nMsgG > 0)
        {
            if (nMsgG < 0)
            { // old style
                DiagLogF("Client: pending in SendQueue: %d messages, %d bytes", nMsg, nBytes);
            }
            else
            { // new style
                DiagLogF(
                    "Client: pending in SendQueue: common - %d messages, %d bytes, guaranteed - %d messages, %d bytes",
                    nMsg, nBytes, nMsgG, nBytesG);
            }
        }
    }

    // cancel update messages
    /*
      int canceled = 0;
      for (int i=0; i<_localObjects.Size(); i++)
      {
        NetworkLocalObjectInfo &info = _localObjects[i];
        if (info.lastCreatedMsgId != 0xFFFFFFFF && info.canCancel)
        {
          HRESULT hr = _dp->CancelAsyncOperation(info.lastCreatedMsgId, 0);
          if (DiagLevel > 0 && FAILED(hr))
            LOG_DEBUG(Network, "Client: cannot cancel message {:x}, error {:x}", info.lastCreatedMsgId, hr);
          canceled++;
        }
      }

      // statistics
      if (DiagLevel >= 2)
      {
        if (canceled > 0) DiagLogF("Client: canceled %d messages", canceled);
      }
    */

    // respawning
    for (int i = 0; i < _respawnQueue.Size();)
    {
        RespawnQueueItem& item = _respawnQueue[i];
        if (item.time > Glob.time)
        {
            i++;
            continue;
        }

        DoRespawn(item);
        /*
            if (item.group)
            {
              if (item.group->IsLocal())
                DoRespawn(item);
              else
                SendMsg(&item, 0, 0, NMFGuaranteed);
            }
            else
            {
              Fail("No group in respawn");
            }
        */

        _respawnQueue.Delete(i);
    }

    // send update messages
    SendMessages();

    // update speaker buffers
    for (int i = 0; i < _soundBuffers.Size(); i++)
    {
        if (_soundBuffers[i].buffer && _soundBuffers[i].object)
        {
            Vector3 pos = _soundBuffers[i].object->GetSpeakerPosition();
            _soundBuffers[i].buffer->SetPosition(pos[0], pos[1], pos[2]);
            bool playing = _client->IsVoicePlaying(_soundBuffers[i].player);
            _soundBuffers[i].object->SetRandomLip(playing);
        }
    }
    if (GWorld->GetRealPlayer())
    {
        GWorld->GetRealPlayer()->SetRandomLip(_client->IsVoiceRecording() && ActualChatChannel() == CCDirect);
    }

    /*
      _bodies.Compact();
      while (_bodies.Size() > MAX_LOCAL_BODIES)
      {
        NET_ERROR(_bodies[0].body);
        NET_ERROR(_bodies[0].body->IsLocal());
        _bodies[0]->HideBody();
        _bodies.Delete(0);
      }
    */

    // check if sent confirmed for all outgoing messages
    for (int o = 0; o < _localObjects.Size(); o++)
    {
        NetworkLocalObjectInfo& oInfo = _localObjects[o];
        for (int j = NMCUpdateFirst; j < NMCUpdateN; j++)
        {
            NetworkUpdateInfo& info = oInfo.updates[j];
            if (info.lastCreatedMsg && ::GlobalTickCount() > info.lastCreatedMsgTime + 5000)
            {
                RptF("Client: Network message %x (update info %x) is pending", info.lastCreatedMsgId, &info);
                info.lastCreatedMsg = nullptr;
                info.lastCreatedMsgId = 0xFFFFFFFF;
                info.lastCreatedMsgTime = 0;
            }
        }
    }

    // log all diagnostics
    WriteDiagOutput(false);
}

void OnClientUserMessage(char* buffer, int bufferSize, void* context)
{
    NetworkClient* client = (NetworkClient*)context;

    bufferSize -= sizeof(int);
    int crc = *(int*)(buffer + bufferSize);
    static Poseidon::Foundation::CRCCalculator calculator;
    if (calculator.CRC(buffer, bufferSize) == crc)
    {
        NetworkMessageRaw rawMsg(buffer, bufferSize);
        client->DecodeMessage(TO_SERVER, rawMsg);
    }
    else
    {
        Fail("Bad CRC for incoming message");
    }
}

void OnClientRawMagicMessage(int magic, const char* buffer, int bufferSize, void* context)
{
    NetworkClient* client = (NetworkClient*)context;
    client->OnRawMagicMessage(magic, buffer, bufferSize);
}

void OnClientSendComplete(DWORD msgID, bool ok, void* context)
{
    NetworkClient* client = (NetworkClient*)context;
    client->OnSendComplete(msgID, ok);
}

void NetworkClient::ReceiveSystemMessages()
{
    if (!_client)
    {
        LOG_DEBUG(Network, "[disconnect] ReceiveSystemMessages: _client is null, serverState={}", (int)_serverState);
        return;
    }
    if (_client->IsSessionTerminated())
    {
        LOG_INFO(Network, "[disconnect] Session terminated, reason={}", (int)_client->GetWhySessionTerminated());
        _state = NGSNone;
        _serverState = NGSNone;

        // In mp-assign mode, exit directly — the display chain may not unwind
        // (DestroyAllObjects can null _cameraOn, preventing SimulateHUD calls)
        if (!AppConfig::Instance().GetMPAssign().empty())
        {
            GApp->m_exitCode =
                ResolveMultiplayerAutomationExitCode(GApp->m_exitCode, GApp->m_closeRequest, _serverReachedPlay);
            GApp->m_closeRequest = true;
            LOG_INFO(Network, "[mp-assign] Session lost, exitCode={}", GApp->m_exitCode);
            return;
        }

        //    GChatList.Add(CCGlobal, nullptr, "Session lost", false, true);
        NetTerminationReason reason = _client->GetWhySessionTerminated();
        RString format = LocalizeString(IDS_MP_SESSION_LOST);
        RString message = format;
        RString name = GetLocalPlayerName();
        const PlayerIdentity* id = FindIdentity(_player);
        if (id)
        {
            name = id->name;
        }
        switch (reason)
        {
            case NTRTimeout:
                format = LocalizeString(IDS_MP_TIMEOUT);
                break;
            case NTRKicked:
                format = LocalizeString(IDS_MP_KICKED);
                break;
            case NTRBanned:
                format = LocalizeString(IDS_MP_BANNED);
                break;
            case NTRDisconnected:
                format = LocalizeString(IDS_MP_DISCONNECT);
                break;
            case NTRVersion:
                message = LocalizeString(IDS_MSG_MP_VERSION);
                GChatList.Add(CCGlobal, nullptr, message, false, true);
                return;
        }
        message = Format(format, (const char*)name);
        GChatList.Add(CCGlobal, nullptr, message, false, true);
        return;
    }
    _client->ProcessSendComplete(OnClientSendComplete, this);
}

void NetworkClient::ReceiveUserMessages()
{
    if (_client)
    {
        _client->ProcessUserMessages(OnClientUserMessage, this);
        _client->ProcessRawMagicMessages(OnClientRawMagicMessage, this);
    }
}

void NetworkClient::OnRawMagicMessage(int magic, const char* buffer, int bufferSize)
{
    if (magic != Poseidon::NetworkMissionBulkRawMagic || _missionFileValid)
    {
        return;
    }

    Poseidon::NetworkMissionBulkRawPacket packet;
    if (!Poseidon::DecodeNetworkMissionBulkRawPayload(buffer, bufferSize, packet) ||
        packet.kind != Poseidon::NetworkMissionBulkRawKind::Data)
    {
        LOG_WARN(Network, "[NMTTransferMission] rejected malformed raw mission packet size={}", bufferSize);
        return;
    }

    TransferFileMessage transfer;
    transfer.path = Poseidon::BuildNetworkMissionTransferCachePboPath(_missionHeader.fileName);
    if (transfer.path.GetLength() == 0)
    {
        transfer.path = Poseidon::BuildNetworkMissionTransferCachePboPathFromTransferPath(packet.transferPath);
    }
    if (transfer.path.GetLength() == 0)
    {
        LOG_WARN(
            Network,
            "[NMTTransferMission] rejected raw mission packet for unsafe mission file name '{}' transfer path '{}'",
            (const char*)_missionHeader.fileName, (const char*)packet.transferPath);
        return;
    }
    transfer.totSize = packet.totalSize;
    _missionRawExpectedSize = packet.totalSize;
    transfer.offset = packet.offset;
    transfer.totSegments = packet.totalSegments;
    transfer.curSegment = packet.curSegment;
    transfer.data.Resize(packet.dataSize);
    if (packet.dataSize > 0)
    {
        memcpy(transfer.data.Data(), packet.data, packet.dataSize);
    }

    if (_missionRawFirstSegmentTime == 0)
    {
        const DWORD now = ::GlobalTickCount();
        _missionRawFirstSegmentTime = now;
        _missionRawLastSegmentTime = now;
        _missionRawLastRequestTime = 0;
        _missionRawLastRequestFirstSegment = -1;
        _missionRawLastRequestSegmentCount = 0;
        _missionRawHighestReceivedSegment = -1;
        _missionRawReceivedSegments = 0;
        _missionRawDuplicateSegments = 0;
        _missionRawRequestedDuplicateSegments = 0;
        _missionRawRequestedDuplicateUniqueSegments = 0;
        _missionRawRequestedSegments = 0;
        _missionRawRequestCount = 0;
        _missionRawRequestedSegmentMap.Clear();
        _missionRawRequestedDuplicateSegmentMap.Clear();
    }
    else
    {
        _missionRawLastSegmentTime = ::GlobalTickCount();
    }
    _missionRawHighestReceivedSegment = std::max(_missionRawHighestReceivedSegment, transfer.curSegment);
    RString receivePath = transfer.path;
    if (Poseidon::IsSafeNetworkTransferredAssetPath(receivePath))
    {
        receivePath = Poseidon::GetUserDirectory() + receivePath;
    }
    const bool duplicateSegment = HasReceivedFileSegment(receivePath, transfer.curSegment);
    if (duplicateSegment)
    {
        ++_missionRawDuplicateSegments;
        const bool requestedDuplicate =
            Poseidon::WasNetworkMissionBulkSegmentRequested(_missionRawRequestedSegmentMap, transfer.curSegment);
        if (requestedDuplicate)
        {
            ++_missionRawRequestedDuplicateSegments;
            if (Poseidon::MarkNetworkMissionBulkSegmentSeen(_missionRawRequestedDuplicateSegmentMap,
                                                            transfer.totSegments, transfer.curSegment))
            {
                ++_missionRawRequestedDuplicateUniqueSegments;
            }
        }
        LOG_DEBUG(Network, "[NMTTransferMission] ignored duplicate raw segment {} requested={} path='{}'",
                  transfer.curSegment, requestedDuplicate ? 1 : 0, (const char*)transfer.path);
        return;
    }

    if (Poseidon::ShouldResetNetworkMissionTransferBank(transfer.offset))
    {
        const std::string prefix = GameDirs::MPCurrentPrefix();
        RemoveBank(prefix.c_str());
    }

    int ret = ReceiveFileSegment(transfer);
    LOG_DEBUG(Network, "[NMTTransferMission] raw path='{}' ret={} state={} missionFileValid={}",
              (const char*)transfer.path, ret, (int)_state, _missionFileValid);
    if (ret >= 0)
    {
        ++_missionRawReceivedSegments;
    }
    if (ret > 0)
    {
        const char* ptr = transfer.path;
        const char* ext = strrchr(ptr, '.');
        NET_ERROR(ext);
        NET_ERROR(stricmp(ext, ".pbo") == 0);
        RString path = transfer.path.Substring(0, ext - ptr);
        CreateMPMissionBank(path, _missionHeader.island);

        _missionFileValid = true;
        _missionRawExpectedSize = 0;
        AskMissionFileMessage ask(true);
        const DWORD now = ::GlobalTickCount();
        const DWORD elapsed = _missionRawFirstSegmentTime != 0 ? now - _missionRawFirstSegmentTime : 0;
        LOG_INFO(Network,
                 "[NMTTransferMission] completed {} bytes via raw UDP in {} ms, segments={}/{}, duplicates={}, "
                 "requestedDuplicates={}, requestedDuplicateSegments={}, requestedSegments={}, resendRequests={}, "
                 "sending AskMissionFile valid=true",
                 transfer.totSize, elapsed, _missionRawReceivedSegments, transfer.totSegments,
                 _missionRawDuplicateSegments, _missionRawRequestedDuplicateSegments,
                 _missionRawRequestedDuplicateUniqueSegments, _missionRawRequestedSegments, _missionRawRequestCount);
        _missionRawFirstSegmentTime = 0;
        _missionRawLastSegmentTime = 0;
        _missionRawHighestReceivedSegment = -1;
        _missionRawReceivedSegments = 0;
        _missionRawDuplicateSegments = 0;
        _missionRawRequestedDuplicateSegments = 0;
        _missionRawRequestedDuplicateUniqueSegments = 0;
        _missionRawRequestedSegments = 0;
        _missionRawRequestCount = 0;
        _missionRawRequestedSegmentMap.Clear();
        _missionRawRequestedDuplicateSegmentMap.Clear();
        SendMsg(&ask, NMFGuaranteed);
        NET_ERROR(!_parent->IsServer());
        if (_state == NGSTransferMission && PrepareGame())
        {
            _state = NGSLoadIsland;
            if (_jip && _missionHeader.joinInProgress)
            {
                _state = NGSBriefing;
                ClientReady(NGSBriefing);
            }
        }
    }
    else if (ret < 0)
    {
        RString format = LocalizeString(IDS_MP_VALIDERROR_2);
        char message[512];
        snprintf(message, sizeof(message), "%s", (const char*)"");
        const PlayerIdentity* id = FindIdentity(_player);
        if (id)
        {
            snprintf(message, sizeof(message), format, (const char*)id->name);
            sprintf(message + strlen(message), " - %s", (const char*)transfer.path);
        }
        Disconnect(message);
    }
}

void NetworkClient::RequestMissingMissionRawSegments()
{
    if (!_client || _missionFileValid || _missionRawExpectedSize <= 0)
    {
        return;
    }
    const DWORD now = ::GlobalTickCount();

    const RString transferPath = Poseidon::BuildNetworkMissionTransferCachePboPath(_missionHeader.fileName);
    if (transferPath.GetLength() == 0)
    {
        return;
    }

    for (int fileIndex = 0; fileIndex < _files.Size(); ++fileIndex)
    {
        ReceivingFile& file = _files[fileIndex];
        if (file.fileName != transferPath)
        {
            continue;
        }

        int firstMissing = -1;
        int missingCount = 0;
        const int scanLimit = Poseidon::GetNetworkMissionBulkMissingScanLimit(
            file.fileSegments.Size(), _missionRawHighestReceivedSegment, now, _missionRawLastSegmentTime,
            Poseidon::NetworkMissionBulkRetransmitDelayMs, Poseidon::NetworkMissionBulkRepeatedRetransmitDelayMs);
        if (scanLimit < 0)
        {
            return;
        }
        const int requestSegmentLimit = Poseidon::GetNetworkMissionBulkRetransmitSegmentLimit(
            now, _missionRawLastSegmentTime, Poseidon::NetworkMissionBulkRetransmitDelayMs);

        for (int segment = 0; segment <= scanLimit; ++segment)
        {
            if (file.fileSegments[segment])
            {
                if (firstMissing >= 0)
                {
                    break;
                }
                continue;
            }
            if (firstMissing < 0)
            {
                firstMissing = segment;
            }
            ++missingCount;
            if (missingCount >= requestSegmentLimit)
            {
                break;
            }
        }

        if (firstMissing < 0)
        {
            return;
        }

        const DWORD delay = Poseidon::GetNetworkMissionBulkRetransmitDelayMs(
            firstMissing, missingCount, _missionRawLastRequestFirstSegment, _missionRawLastRequestSegmentCount);
        if (Poseidon::ShouldWaitForNetworkMissionBulkRetransmit(
                now, _missionRawFirstSegmentTime, _missionRawLastRequestTime, _missionRawLastSegmentTime, delay))
        {
            return;
        }

        AutoArray<char> payload;
        if (Poseidon::EncodeNetworkMissionBulkRawRequest(firstMissing, missingCount, _missionRawExpectedSize, payload))
        {
            _missionRawLastRequestTime = now;
            _missionRawLastRequestFirstSegment = firstMissing;
            _missionRawLastRequestSegmentCount = missingCount;
            ++_missionRawRequestCount;
            const int newlyRequested = Poseidon::MarkNetworkMissionBulkRequestedRange(
                _missionRawRequestedSegmentMap, file.fileSegments.Size(), firstMissing, missingCount);
            _missionRawRequestedSegments += newlyRequested;
            LOG_DEBUG(Network,
                      "[NMTTransferMission] requesting raw resend firstSegment={} count={} size={} delay={} "
                      "newSegments={}",
                      firstMissing, missingCount, _missionRawExpectedSize, delay, newlyRequested);
            _client->SendRawMagic(Poseidon::NetworkMissionBulkRawMagic, reinterpret_cast<BYTE*>(payload.Data()),
                                  payload.Size());
        }
        return;
    }
}

void NetworkClient::RemoveSystemMessages()
{
    if (_client)
    {
        _client->RemoveSendComplete();
    }
}

void NetworkClient::RemoveUserMessages()
{
    if (_client)
    {
        _client->RemoveUserMessages();
    }
}

void NetworkClient::OnSendComplete(DWORD msgID, bool ok)
{
#if CHECK_MSG
    CheckLocalObjects();
#endif

#if LOG_SEND_PROCESS
    LOG_DEBUG(Network, "Client: Send {:x} completed: {}", msgID, ok ? "ok" : "failed");
#endif
    for (int i = 0; i < _localObjects.Size(); i++)
    {
        NetworkLocalObjectInfo& localObject = _localObjects[i];
        for (int j = NMCUpdateFirst; j < NMCUpdateN; j++)
        {
            // NetworkLocalObjectInfo &info = _localObjects[i];
            NetworkUpdateInfo& info = localObject.updates[j];
            if (info.lastCreatedMsgId != msgID)
            {
                continue;
            }
            if (!info.lastCreatedMsg)
            {
                Fail("Message ID without message");
                info.lastCreatedMsgId = 0xFFFFFFFF;
                info.lastCreatedMsgTime = 0;
                continue;
            }
            if (ok)
            {
                // message sent
                info.lastSentMsg = info.lastCreatedMsg;

#if CHECK_MSG
                NetworkObject* object = localObject.object;
                if (object)
                {
                    NetworkDataTyped<int>* creator =
                        static_cast<NetworkDataTyped<int>*>(info.lastSentMsg->values[0].GetRef());
                    NetworkDataTyped<int>* id =
                        static_cast<NetworkDataTyped<int>*>(info.lastSentMsg->values[1].GetRef());
                    LOG_DEBUG(Network, "Message {:x} ({}:{}) assigned to object {:x} ({}:{})", info.lastSentMsg.GetRef,
                              creator->value, id->value, object, object->GetNetworkId().creator,
                              object->GetNetworkId().id);
                }
#endif
            }
            else
            {
                if (DiagLevel >= 4)
                {
                    DiagLogF("Client: sent of message %x failed", msgID);
                }
            }
            info.lastCreatedMsg = nullptr;
            info.lastCreatedMsgId = 0xFFFFFFFF;
            info.lastCreatedMsgTime = 0;
#if LOG_SEND_PROCESS
            LOG_DEBUG(Network, "  - update info {:x} updated", (uintptr_t)&info);
#endif
        }
    }

#if CHECK_MSG
    CheckLocalObjects();
#endif
}

unsigned NetworkClient::CleanUpMemory()
{
    if (_client)
    {
        return _client->FreeMemory();
    }
    return 0;
}

bool NetworkClient::CheckLocalObjects() const
{
    for (int i = 0; i < _localObjects.Size(); i++)
    {
        const NetworkLocalObjectInfo& localObject = _localObjects[i];
        NetworkObject* object = localObject.object;
        if (object)
        {
            for (int j = NMCUpdateFirst; j < NMCUpdateN; j++)
            {
                const NetworkUpdateInfo& info = localObject.updates[j];
                if (info.lastCreatedMsg)
                {
                    CHECK_ASSIGN(creator, info.lastCreatedMsg->values[0], const RefNetworkDataTyped<int>);
                    CHECK_ASSIGN(id, info.lastCreatedMsg->values[1], const RefNetworkDataTyped<int>);
                    if (creator.GetVal() != object->GetNetworkId().creator || id.GetVal() != object->GetNetworkId().id)
                    {
                        Fail("lastCreatedMsg is bad");
                        return false;
                    }
                }
                else if (info.lastSentMsg)
                {
                    CHECK_ASSIGN(creator, info.lastCreatedMsg->values[0], const RefNetworkDataTyped<int>);
                    CHECK_ASSIGN(id, info.lastCreatedMsg->values[1], const RefNetworkDataTyped<int>);
                    if (creator.GetVal() != object->GetNetworkId().creator || id.GetVal() != object->GetNetworkId().id)
                    {
                        Fail("lastSentMsg is bad");
                        return false;
                    }
                }
            }
        }
    }
    return true;
}
