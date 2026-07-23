#include <Poseidon/Foundation/Platform/AppConfig.hpp>

using namespace Poseidon;
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Core/ModSystem.hpp>
#include <Poseidon/Core/Config/Config.hpp>
#include <Poseidon/Network/NetworkClientCommon.hpp>
#include <Poseidon/Network/NetworkCustomAssets.hpp>
#include <Poseidon/Network/NetworkScriptValueCodec.hpp>
#include <Poseidon/Network/NetworkSoundReplication.hpp>
#include <Poseidon/Network/WireBounds.hpp>
#include <Poseidon/Core/Global.hpp>
// #include "strIncl.hpp"
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/Foundation/Platform/GamePaths.hpp>
#include <Poseidon/IO/Filesystem/FileOps.hpp>
#include <Poseidon/IO/Filesystem/Utf8Paths.hpp>
#include <Poseidon/Network/NetworkMissionTransfer.hpp>
#include <Poseidon/Network/NetworkServerAuth.hpp>

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

namespace Poseidon
{
void RunMissionScript(char const*, class ::GameValue);
RString GetUserDirectory();
RString GetUserParams();
// Cumulative count of NMTPlaySound messages this client has received. Test
// observable for MP sound replication (e.g. fizzy #154 car horn).
unsigned GTriNetSoundsReceived = 0;
} // namespace Poseidon

// NetworkClient::OnMessage and its helpers, split out of NetworkClient.cpp
// to keep that translation unit under the size threshold.
extern const char* GameStateNames[];
#define BODIES_ON_BOT_CLIENT 10
#define BODIES_ON_CLIENTS 20

static int NetworkPlayerFaceTransferOwner(RString path)
{
    const char prefix[] = "tmp/players/";
    const int prefixLen = static_cast<int>(sizeof(prefix) - 1);
    const char* data = path;
    if (strncmp(data, prefix, prefixLen) != 0)
    {
        return -1;
    }

    const char* owner = data + prefixLen;
    const char* slash = strchr(owner, '/');
    if (!slash)
    {
        return -1;
    }

    const char* name = slash + 1;
    if (stricmp(name, "face.paa") != 0 && stricmp(name, "face.jpg") != 0)
    {
        return -1;
    }

    for (const char* p = owner; p < slash; ++p)
    {
        if (*p < '0' || *p > '9')
        {
            return -1;
        }
    }
    return atoi(owner);
}

static AutoArray<int> PendingTransferredNetworkFaceRefreshes;

static int RefreshTransferredNetworkFace(int player)
{
    if (!GWorld || player < 0)
    {
        return 0;
    }

    int refreshed = 0;
    for (int i = 0; i < GWorld->NVehicles(); ++i)
    {
        Person* person = dyn_cast<Person>(GWorld->GetVehicle(i));
        if (!person || person->GetRemotePlayer() != player || stricmp(person->GetInfo()._face, "custom") != 0)
        {
            continue;
        }

        AIUnitInfo& info = person->GetInfo();
        person->SetFace(info._face, info._name);
        refreshed++;
    }
    if (refreshed > 0)
    {
        LOG_INFO(Network, "Refreshed {} custom face assignment(s) for transferred player face {}", refreshed, player);
    }
    return refreshed;
}

static void QueueTransferredNetworkFaceRefresh(int player)
{
    if (player < 0)
    {
        return;
    }
    for (int i = 0; i < PendingTransferredNetworkFaceRefreshes.Size(); ++i)
    {
        if (PendingTransferredNetworkFaceRefreshes[i] == player)
        {
            return;
        }
    }
    PendingTransferredNetworkFaceRefreshes.Add(player);
    LOG_INFO(Network, "Queued custom face refresh for transferred player face {}", player);
}

static void FlushTransferredNetworkFaceRefreshes()
{
    for (int i = 0; i < PendingTransferredNetworkFaceRefreshes.Size();)
    {
        if (RefreshTransferredNetworkFace(PendingTransferredNetworkFaceRefreshes[i]) > 0)
        {
            PendingTransferredNetworkFaceRefreshes.Delete(i);
        }
        else
        {
            ++i;
        }
    }
}

// Check if given unit is in list of units
static bool FindUnit(NetworkObject* soldier, RefArray<NetworkObject>& units)
{
    if (!soldier)
    {
        return false;
    }
    for (int i = 0; i < units.Size(); i++)
    {
        if (units[i] == soldier)
        {
            return true;
        }
    }
    return false;
}

// Find output radio channel for given unit and chat channel
RadioChannel* FindChannel(AIUnit* unit, int channel)
{
    switch (channel)
    {
        case CCVehicle:
        {
            Transport* veh = unit->GetVehicleIn();
            return veh ? &veh->GetRadio() : nullptr;
        }
        case CCGroup:
        {
            AIGroup* grp = unit->GetGroup();
            return grp ? &grp->GetRadio() : nullptr;
        }
        case CCSide:
        {
            AIGroup* grp = unit->GetGroup();
            if (!grp)
            {
                return nullptr;
            }
            AICenter* center = grp->GetCenter();
            return center ? &center->GetRadio() : nullptr;
        }
        case CCGlobal:
        case CCDirect:
            return &GWorld->GetRadio();
        default:
            RptF("Client: Bad radio channel %d", channel);
            return nullptr;
    }
}

// Check whole AI structute for consistency
static bool AssertAIValid()
{
    for (int side = 0; side < TSideUnknown; side++)
    {
        AICenter* center = GWorld->GetCenter((TargetSide)side);
        if (!center)
        {
            continue;
        }
        if (!center->AssertValid())
        {
            return false;
        }
    }
    return true;
}

bool CheckMissionFile(RString fileName, MissionHeader& header)
{
    RString fileNameExt = fileName + RString(".pbo");

    std::error_code ec;
    auto fileSize = std::filesystem::file_size(std::string(fileNameExt), ec);
    if (ec)
        return false;

    if (header.fileSizeL != static_cast<DWORD>(fileSize & 0xffffffff) ||
        header.fileSizeH != static_cast<DWORD>(fileSize >> 32))
    {
        return false;
    }

    QIFStream f;
    f.open(fileNameExt);
    Poseidon::Foundation::CRCCalculator crc;
    if (crc.CRC(f.act(), f.rest()) != header.fileCRC)
    {
        return false;
    }

    CreateMPMissionBank(fileName, header.island);
    return true;
}

// client-side settings:
// custom face file bigger than given limit is ignored
int MaxCustomFaceSize = 100 * 1024;
// custom sound file bigger than given limit is ignored
int MaxCustomSoundSize = 50 * 1024;
// server-side setting:
// client attemping to transfer more than given limit will be kicked

int MaxCustomFileSize = 128 * 1024;

static int FileSize(const char* name)
{
    HANDLE file = OpenFileForRead(name);
    if (file == INVALID_HANDLE_VALUE)
        return INT_MAX;

    const int size = GetOpenFileSize(file);
    CloseHandle(file);
    return size < 0 ? INT_MAX : size;
}

static NetworkObject* ResolveClientNetworkObject(void* context, const NetworkId& id)
{
    NetworkClient* client = static_cast<NetworkClient*>(context);
    return client ? client->GetObject(const_cast<NetworkId&>(id)) : nullptr;
}

static void ExecuteNamedRemoteExec(GameState* gstate, RString name, GameValuePar params)
{
    if (!gstate || !WireBounds::ValidIdentifier(name, 256))
    {
        return;
    }

    GameVarSpace local(gstate->GetContext());
    gstate->BeginContext(&local);
    gstate->VarSetLocal("_this", params, true);
    GameValue function = gstate->VarGet(name);
    if (!function.GetNil() && function.GetType() == GameString)
    {
        gstate->Execute((RString)(GameStringType)function);
        gstate->EndContext();
        return;
    }
    gstate->Execute(name + RString(" _this"));
    gstate->EndContext();
}

bool NetworkClient::TryApplySelectPlayer(const SelectPlayerMessage& pl, bool allowPending)
{
    NET_ERROR(pl.player == _player);
    NetworkId person = pl.person;
    NetworkObject* object = GetObject(person);
    Person* veh = dynamic_cast<Person*>(object);
    if (!veh)
    {
        if (allowPending)
        {
            _pendingSelectPlayer = true;
            _pendingSelectPlayerMessage = pl;
            LOG_DEBUG(Network, "SelectPlayer: pending player={} person={}:{}", pl.player, pl.person.creator,
                      pl.person.id);
        }
        else
        {
            RptF("Client: Player (%d) is not vehicle with brain (%x)", (int)pl.player, pl.person.id);
        }
        return false;
    }
    AIUnit* unit = veh->Brain();
    if (!unit || unit->GetLifeState() == AIUnit::LSDead)
    {
        if (allowPending)
        {
            _pendingSelectPlayer = true;
            _pendingSelectPlayerMessage = pl;
            LOG_DEBUG(Network, "SelectPlayer: pending player={} person={}:{} brain={}", pl.player, pl.person.creator,
                      pl.person.id, unit != nullptr);
            return false;
        }
        LOG_WARN(Network, "SelectPlayer: person {}:{} has no living brain", pl.person.creator, pl.person.id);
        return false;
    }
    LLink<Person> selectedPerson(veh);
    EntityAI* selectedVehicle = unit->GetVehicle();
    LOG_DEBUG(Network, "SelectPlayer: player={} person={}:{} respawn={} vehicle='{}'", pl.player, pl.person.creator,
              pl.person.id, pl.respawn, selectedVehicle ? (const char*)selectedVehicle->GetDebugName() : "<none>");
    GWorld->SwitchCameraTo(selectedVehicle, CamInternal);
    GWorld->SetPlayerManual(true);
    GWorld->SwitchPlayerTo(veh);
    GWorld->SetRealPlayer(veh);
    if (GWorld->UI())
    {
        GWorld->UI()->ResetVehicle(selectedVehicle);
    }
    if (pl.respawn)
    {
        RString name = "onPlayerResurrect.sqs";
        if (QIFStreamB::FileExist(RString("scripts\\") + name))
        {
            Person* scriptPerson = selectedPerson.GetLink();
            if (!scriptPerson)
            {
                LOG_WARN(Network, "SelectPlayer: selected person disappeared before resurrect script");
                return false;
            }
            GameArrayType arguments;
            arguments.Add(GameValueExt(scriptPerson));
            Script* script = new Script(name, GameValue(arguments));
            GWorld->StartCameraScript(script);
        }
    }
    _pendingSelectPlayer = false;
    return true;
}

void NetworkClient::TryApplyPendingSelectPlayer(NetworkId id)
{
    if (!_pendingSelectPlayer)
    {
        return;
    }
    if (_pendingSelectPlayerMessage.person == id || GetObject(_pendingSelectPlayerMessage.person))
    {
        TryApplySelectPlayer(_pendingSelectPlayerMessage, true);
    }
}

bool NetworkClient::TryApplyChangeOwner(const ChangeOwnerMessage& co, bool allowPending)
{
    NetworkLocalObjectInfo* knownLocal = GetLocalObjectInfo(const_cast<NetworkId&>(co.object));
    NetworkRemoteObjectInfo* knownRemote = GetRemoteObjectInfo(const_cast<NetworkId&>(co.object));
    NetworkObject* knownObject = knownLocal ? knownLocal->object : (knownRemote ? knownRemote->object : nullptr);
    LOG_INFO(Network,
             "ChangeOwner: received object {}:{} owner={} localPlayer={} knownLocal={} knownRemote={} objectLocal={}",
             co.object.creator, co.object.id, co.owner, _player, knownLocal != nullptr, knownRemote != nullptr,
             knownObject ? knownObject->IsLocal() : false);

    if (!knownObject)
    {
        if (!allowPending)
        {
            return false;
        }
        for (int i = 0; i < _pendingChangeOwners.Size(); i++)
        {
            if (_pendingChangeOwners[i].object == co.object)
            {
                _pendingChangeOwners[i] = co;
                LOG_INFO(Network, "ChangeOwner: updated pending object {}:{} owner={}", co.object.creator, co.object.id,
                         co.owner);
                return false;
            }
        }
        _pendingChangeOwners.Add(co);
        LOG_INFO(Network, "ChangeOwner: pending object {}:{} owner={}", co.object.creator, co.object.id, co.owner);
        return false;
    }

    if (co.owner == _player)
    {
        NetworkObject* object = nullptr;
        for (int i = 0; i < _remoteObjects.Size(); i++)
        {
            NetworkRemoteObjectInfo& info = _remoteObjects[i];
            if (info.id == co.object)
            {
                object = info.object;
                _remoteObjects.Delete(i);
                break;
            }
        }
        if (!object)
        {
            LOG_INFO(Network, "ChangeOwner: local target object {}:{} missing at apply time", co.object.creator,
                     co.object.id);
            RptF("Client: Remote object %d:%d not found", co.object.creator, co.object.id);
            return false;
        }

        object->SetLocal(true);
#if CHECK_MSG
        CheckLocalObjects();
#endif
        int index = _localObjects.Add();
        NetworkLocalObjectInfo& localObject = _localObjects[index];
        localObject.id = co.object;
        localObject.object = object;
        for (int j = NMCUpdateFirst; j < NMCUpdateN; j++)
        {
            NetworkUpdateInfo& info = localObject.updates[j];
            info.lastCreatedMsg = nullptr;
            info.lastCreatedMsgId = 0xFFFFFFFF;
            info.lastCreatedMsgTime = 0;
        }
        if (DiagLevel >= 1)
        {
            DiagLogF("Client: object %d:%d is now local", localObject.id.creator, localObject.id.id);
        }
        LOG_INFO(Network, "ChangeOwner: applied local object {}:{} localObjects={} remoteObjects={}",
                 localObject.id.creator, localObject.id.id, _localObjects.Size(), _remoteObjects.Size());
#if CHECK_MSG
        CheckLocalObjects();
#endif
        return true;
    }

#if CHECK_MSG
    CheckLocalObjects();
#endif
    NetworkObject* object = nullptr;
    for (int i = 0; i < _localObjects.Size(); i++)
    {
        NetworkLocalObjectInfo& info = _localObjects[i];
        if (info.id == co.object)
        {
            object = info.object;
            _localObjects.Delete(i);
            break;
        }
    }
#if CHECK_MSG
    CheckLocalObjects();
#endif
    if (!object)
    {
        LOG_INFO(Network, "ChangeOwner: remote target object {}:{} missing at apply time", co.object.creator,
                 co.object.id);
        RptF("Client: Local object %d:%d not found", co.object.creator, co.object.id);
        return false;
    }

    object->SetLocal(false);
    int index = _remoteObjects.Add();
    NetworkRemoteObjectInfo& info = _remoteObjects[index];
    info.id = co.object;
    info.object = object;
    if (DiagLevel >= 1)
    {
        DiagLogF("Client: object %d:%d is now remote", info.id.creator, info.id.id);
    }
    LOG_INFO(Network, "ChangeOwner: applied remote object {}:{} localObjects={} remoteObjects={}", info.id.creator,
             info.id.id, _localObjects.Size(), _remoteObjects.Size());
    return true;
}

void NetworkClient::TryApplyPendingChangeOwner(NetworkId id)
{
    for (int i = 0; i < _pendingChangeOwners.Size();)
    {
        if (_pendingChangeOwners[i].object != id)
        {
            i++;
            continue;
        }
        ChangeOwnerMessage co = _pendingChangeOwners[i];
        _pendingChangeOwners.Delete(i);
        TryApplyChangeOwner(co, true);
    }
}

void NetworkClient::TryApplyPendingChangeOwners()
{
    for (int i = 0; i < _pendingChangeOwners.Size();)
    {
        NetworkId id = _pendingChangeOwners[i].object;
        if (!GetObject(id))
        {
            i++;
            continue;
        }
        ChangeOwnerMessage co = _pendingChangeOwners[i];
        _pendingChangeOwners.Delete(i);
        TryApplyChangeOwner(co, true);
    }
}

void NetworkClient::OnMessage(int from, NetworkMessage* msg, NetworkMessageType type)
{
    if (_serverState == NGSNone)
    {
        return;
    }
    NetworkMessageFormatBase* format = GetFormat(/*from, */ type);
    if (!format)
    {
        RptF("Client: Bad message %d(%s) format", (int)type, (const char*)NetworkMessageTypeNames[type]);
        return;
    }
    NetworkMessageContext ctx(msg, format, this, from, MSG_RECEIVE);

#if _ENABLE_CHEATS
    int level = GetDiagLevel(type, !_parent->GetServer());
    if (level >= 2)
    {
        DiagLogF("Client: received message %s from %d", NetworkMessageTypeNames[type], from);
        ctx.LogMessage(level, "\t");
    }
#endif

#ifndef NDEBUG
    bool validBefore = true;
    if (_state == NGSPlay)
    {
        validBefore = AssertAIValid();
    }
#endif

    switch (type)
    {
        case NMTMsgFormat:
        {
            int IndicesMsgFormatGetIndex(const NetworkMessageIndices* ind);

            int index;
            ctx.IdxTransfer(IndicesMsgFormatGetIndex(ctx.GetIndices()), index);
            if (index < NMTFirstVariant || index >= NMTN)
            {
                LOG_WARN(Network, "Ignoring invalid remote message format index {} (valid dynamic range {}..{})", index,
                         static_cast<int>(NMTFirstVariant), static_cast<int>(NMTN) - 1);
                break;
            }
            int pos = index - NMTFirstVariant;
            _formats.Access(pos);
            _formats[pos].Clear();
            _formats[pos].TransferMsg(ctx);
            _formats[pos].Init(GMsgFormats[index]->GetIndices()->Clone());
        }
        break;
        case NMTIntegrityQuestion:
        {
            IntegrityQuestionMessage question;
            question.TransferMsg(ctx);

            IntegrityAnswerMessage answer;
            answer.id = question.id;
            answer.type = question.type;
            answer.answer = IntegrityCheckAnswer(question.type, question.q);
            SendMsg(&answer, NMFGuaranteed | NMFHighPriority);
        }
        break;
        case NMTPlayerState:
        {
            PlayerStateMessage state;
            state.TransferMsg(ctx);

            for (int i = 0; i < _identities.Size(); i++)
            {
                PlayerIdentity& identity = _identities[i];
                if (identity.dpnid == state.player)
                {
                    identity.state = state.state;
                    break;
                }
            }
        }
        break;
        case NMTNetworkCommand:
        {
            NetworkCommandMessage cmd;
            cmd.TransferMsg(ctx);
            switch (cmd.type)
            {
                case NCMTLogged:
                {
                    _gameMaster = true;
                    cmd.content.Read(&_admin, sizeof(_admin));
                    /*
                                _serverMissions.Clear();
                                RString mission = cmd.content.ReadString();
                                while (mission.GetLength() > 0)
                                {
                                  _serverMissions.Add(mission);
                                  mission = cmd.content.ReadString();
                                }
                    */
                    GChatList.Add(CCGlobal, nullptr, LocalizeString(IDS_MP_LOGGED), false, true);
                }
                break;
                case NCMTLoggedOut:
                {
                    _gameMaster = false;
                    _selectMission = false;
                    // _serverMissions.Clear();
                    GChatList.Add(CCGlobal, nullptr, LocalizeString(IDS_MP_LOGGED_OUT), false, true);
                }
                break;
                case NCMTVoteMission:
                {
                    _serverMissions.Clear();
                    RString mission = cmd.content.ReadString();
                    while (mission.GetLength() > 0)
                    {
                        _serverMissions.Add(mission);
                        mission = cmd.content.ReadString();
                    }
                    if (_gameMaster)
                    {
                        _selectMission = true;
                    }
                    _voteMission = true;
                }
                break;
                case NCMTMonitorAnswer:
                {
                    float fps = 0;
                    int memory = 0;
                    float in = 0;
                    float out = 0;
                    cmd.content.Read(&fps, sizeof(fps));
                    cmd.content.Read(&memory, sizeof(memory));
                    cmd.content.Read(&in, sizeof(in));
                    cmd.content.Read(&out, sizeof(out));

                    int sizeNG = 0, sizeG = 0;
                    cmd.content.Read(&sizeG, sizeof(sizeG));
                    cmd.content.Read(&sizeNG, sizeof(sizeNG));

                    char buffer[256];
                    snprintf(buffer, sizeof(buffer), LocalizeString(IDS_SERVER_MONITOR), fps, 1e-6 * memory, 8e-3 * out,
                             8e-3 * in, sizeNG, sizeG);
                    GChatList.Add(CCGlobal, nullptr, buffer, false, true);
                }
                break;
                case NCMTDebugAnswer:
                    LOG_DEBUG(Network, "{}", (const char*)cmd.content.ReadString());
                    break;
                case NCMTMissionTimeElapsed:
                {
                    int timeElapsed = 0;
                    cmd.content.Read(&timeElapsed, sizeof(timeElapsed));
                    _missionHeader.start = GlobalTickCount() - timeElapsed;
                    _jip = true;
                    LOG_INFO(Network, "JIP: Joined in progress (mission elapsed: {}ms)", timeElapsed);
                }
                break;
            }
        }
        break;
        case NMTPlayer:
        {
            PlayerMessage playerMsg;
            playerMsg.TransferMsg(ctx);
            _player = playerMsg.player;
            //{ DEDICATED SERVER SUPPORT
            if (playerMsg.server)
            {
                break; // do not create identity for server
            }
            //}

            // send identity
            PlayerIdentity identity;
            identity.dpnid = _player;
            identity.version = MP_VERSION_ACTUAL;
            RString GetPublicKey();
            identity.id = GetPublicKey();
            identity.name = playerMsg.name; // name is changed by server
            identity.face = Glob.header.playerFace;
            identity.glasses = Glob.header.playerGlasses;
            identity.speaker = Glob.header.playerSpeaker;
            identity.pitch = Glob.header.playerPitch;
            identity._minPing = 10;
            identity._avgPing = 100;
            identity._maxPing = 1000;
            identity._minBandwidth = 14;
            identity._avgBandwidth = 28;
            identity._maxBandwidth = 33;
            RString filename = Poseidon::GetUserParams();
            if (QIFStream::FileExists(filename))
            {
                ParamFile cfg;
                cfg.Parse(filename);
                const ParamEntry* entry = cfg.FindEntry("Identity");
                if (entry)
                {
                    if (entry->FindEntry("squad"))
                    {
                        identity.squadId = (*entry) >> "squad";
                    }
                }
            }

            // send face
            bool transfer = !_parent->IsServer();
            if (stricmp(identity.face, "custom") == 0)
            {
                RString srcDir = GetUserDirectory();
                RString relativeDstDir = Poseidon::BuildNetworkPlayerAssetTmpDir(identity.dpnid);
                RString dstDir = Poseidon::GetUserDirectory() + relativeDstDir;
                RString serverDir = Poseidon::BuildNetworkServerPlayerUploadDir(GetServerTmpDir(), identity.dpnid);
                if (relativeDstDir.GetLength() > 0 && serverDir.GetLength() > 0)
                {
                    CreatePath(dstDir);
                    if (!transfer)
                    {
                        CreatePath(serverDir);
                    }
                    RString src = srcDir + RString("face.paa");
                    if (QIFStream::FileExists(src) && FileSize(src) <= MaxCustomFaceSize)
                    {
                        RString dst = Poseidon::GetUserDirectory() +
                                      Poseidon::BuildNetworkPlayerAssetTmpPath(identity.dpnid, RString("face.paa"));
                        Poseidon::CopyFileUtf8(src, dst, false);
                        QueueTransferredNetworkFaceRefresh(identity.dpnid);
                        RString server = Poseidon::BuildNetworkServerPlayerAssetUploadPath(
                            GetServerTmpDir(), identity.dpnid, RString("face.paa"));
                        if (transfer)
                        {
                            TransferFileToServer(server, src);
                        }
                        else
                        {
                            Poseidon::CopyFileUtf8(src, server, false);
                        }
                    }
                    else
                    {
                        src = srcDir + RString("face.jpg");
                        if (QIFStream::FileExists(src) && FileSize(src) <= MaxCustomFaceSize)
                        {
                            RString dst = Poseidon::GetUserDirectory() +
                                          Poseidon::BuildNetworkPlayerAssetTmpPath(identity.dpnid, RString("face.jpg"));
                            Poseidon::CopyFileUtf8(src, dst, false);
                            QueueTransferredNetworkFaceRefresh(identity.dpnid);
                            RString server = Poseidon::BuildNetworkServerPlayerAssetUploadPath(
                                GetServerTmpDir(), identity.dpnid, RString("face.jpg"));
                            if (transfer)
                            {
                                TransferFileToServer(server, src);
                            }
                            else
                            {
                                Poseidon::CopyFileUtf8(src, server, false);
                            }
                        }
                    }
                }
            }

            // send sounds (UI is null on dedicated server)
            if (GWorld->UI())
            {
                const AutoArray<RString>& sounds = GWorld->UI()->GetCustomRadio();
                LOG_DEBUG(Network, "[CustomSoundUpload] {} custom radio sound(s)", sounds.Size());
                if (sounds.Size() > 0)
                {
                    RString srcDir = Poseidon::GetUserDirectory() + RString("Sound/");
                    RString relativeDstDir = Poseidon::BuildNetworkPlayerSoundTmpDir(identity.dpnid);
                    RString dstDir = Poseidon::GetUserDirectory() + relativeDstDir;
                    RString serverDir =
                        Poseidon::BuildNetworkServerPlayerSoundUploadDir(GetServerTmpDir(), identity.dpnid);
                    if (relativeDstDir.GetLength() > 0 && serverDir.GetLength() > 0)
                    {
                        CreatePath(dstDir);
                        if (!transfer)
                        {
                            CreatePath(serverDir);
                        }
                        for (int i = 0; i < sounds.Size(); i++)
                        {
                            RString relativeDst = Poseidon::BuildNetworkPlayerSoundTmpPath(identity.dpnid, sounds[i]);
                            RString dst = Poseidon::GetUserDirectory() + relativeDst;
                            RString relative = Poseidon::BuildNetworkPlayerSoundRelativePath(identity.dpnid, sounds[i]);
                            if (relativeDst.GetLength() == 0 || relative.GetLength() == 0)
                            {
                                LOG_DEBUG(Network, "[CustomSoundUpload] rejected unsafe sound '{}'",
                                          (const char*)sounds[i]);
                                continue;
                            }
                            RString src = srcDir + sounds[i];
                            if (!QIFStream::FileExists(src))
                            {
                                src = Poseidon::GetUserDirectory() + RString("sound/") + sounds[i];
                            }
                            const int soundSize = FileSize(src);
                            if (soundSize < MaxCustomSoundSize)
                            {
                                LOG_DEBUG(Network, "[CustomSoundUpload] uploading '{}' ({} B)", (const char*)sounds[i],
                                          soundSize);
                                const bool copiedLocal = Poseidon::CopyFileUtf8(src, dst, false);
                                LOG_DEBUG(Network, "[CustomSoundUpload] local copy '{}' -> '{}' result={}",
                                          (const char*)src, (const char*)dst, copiedLocal);
                                RString server = Poseidon::BuildNetworkServerPlayerSoundUploadPath(
                                    GetServerTmpDir(), identity.dpnid, sounds[i]);
                                if (transfer)
                                {
                                    TransferFileToServer(server, src);
                                }
                                else
                                {
                                    Poseidon::CopyFileUtf8(src, server, false);
                                }
                            }
                            else
                            {
                                LOG_DEBUG(Network, "[CustomSoundUpload] skipping '{}' size={} limit={}",
                                          (const char*)sounds[i], soundSize, MaxCustomSoundSize);
                            }
                        }
                    }
                }
            }

            SendMsg(&identity, NMFGuaranteed);
        }
        break;
        case NMTLogin:
        {
            int index = _identities.Add();
            PlayerIdentity& identity = _identities[index];
            identity.TransferMsg(ctx);
            // identity.state = NGSLogin;
            // find squad info
            if (identity.squadId.GetLength() > 0)
            {
                for (int i = 0; i < _squads.Size(); i++)
                {
                    if (_squads[i]->id == identity.squadId)
                    {
                        identity.squad = _squads[i];
                        break;
                    }
                }
            }
        }
        break;
        case NMTLogout:
        {
            LogoutMessage logout;
            logout.TransferMsg(ctx);
            for (int i = 0; i < _identities.Size(); i++)
            {
                if (_identities[i].dpnid == logout.dpnid)
                {
                    _identities.Delete(i);
                    break;
                }
            }
        }
        break;
        case NMTSquad:
        {
            int index = _squads.Add(new SquadIdentity());
            SquadIdentity* squad = _squads[index];
            squad->TransferMsg(ctx);
        }
        break;
        case NMTMissionHeader:
        {
            _missionTransferHeaderStatsLogged = false;
            _missionHeader.TransferMsg(ctx);
            if (_missionHeader.updateOnly)
            {
                break;
            }
            {
                // AddOns check
                void CheckPatch(FindArrayRStringCI & addOns, FindArrayRStringCI & missing);
                FindArrayRStringCI missing;
                CheckPatch(_missionHeader.addOns, missing);
                if (missing.Size() > 0)
                {
                    RString message = LocalizeString(IDS_MSG_ADDON_MISSING);
                    bool first = true;
                    for (int i = 0; i < missing.Size(); i++)
                    {
                        if (first)
                        {
                            first = false;
                        }
                        else
                        {
                            message = message + RString(", ");
                        }
                        message = message + missing[i];
                    }
                    Disconnect(message);
                    return;
                }
                GWorld->ActivateAddons(_missionHeader.addOns);
            }

            USER_CONFIG.easyMode = _missionHeader.cadetMode;
            // Difficulty is server-authoritative in multiplayer: adopt the host's settings so
            // name tags, HUD and map markers match the server, not this client's local profile.
            USER_CONFIG.SetServerDifficulty(_missionHeader.difficulty.Data());
            _playerRoles.Resize(0);

            // check if mission file is valid
            const RString missionCacheBasePath =
                Poseidon::BuildNetworkMissionTransferCacheBasePath(_missionHeader.fileName);
            RString fullname =
                missionCacheBasePath.GetLength() > 0 ? _missionHeader.fileDir + _missionHeader.fileName : RString();
            _missionFileValid = fullname.GetLength() > 0 && CheckMissionFile(fullname, _missionHeader);
            LOG_DEBUG(Network, "[MissionHeader] file='{}' valid={}", (const char*)fullname, _missionFileValid);

            if (!_missionFileValid)
            {
                // check cache
                RString fullname = missionCacheBasePath;
                _missionFileValid = fullname.GetLength() > 0 && CheckMissionFile(fullname, _missionHeader);
                LOG_DEBUG(Network, "[MissionHeader] cache='{}' valid={}", (const char*)fullname, _missionFileValid);
            }

            AskMissionFileMessage ask(_missionFileValid);
            LOG_DEBUG(Network, "[MissionHeader] sending AskMissionFile valid={}", _missionFileValid);
            SendMsg(&ask, NMFGuaranteed);
        }
        break;
        case NMTPlayerRole:
        {
            NET_ERROR(dynamic_cast<const IndicesPlayerRole*>(ctx.GetIndices()))
            const IndicesPlayerRole* indices = static_cast<const IndicesPlayerRole*>(ctx.GetIndices());

            int index;
            ctx.IdxTransfer(indices->index, index);
            if (index >= _playerRoles.Size())
            {
                _playerRoles.Resize(index + 1);
            }
            _playerRoles[index].TransferMsg(ctx);
        }
        break;
        case NMTSelectPlayer:
        {
            SelectPlayerMessage pl;
            pl.TransferMsg(ctx);
            TryApplySelectPlayer(pl, true);
        }
        break;
        case NMTTransferFile:
        {
            TransferFileMessage transfer;
            transfer.TransferMsg(ctx);
            const RString wirePath = transfer.path;
            const size_t maxTransferSize =
                Poseidon::NetworkTransferredAssetMaxSize(transfer.path, static_cast<size_t>(MaxCustomFileSize));
            if (!Poseidon::ShouldAcceptNetworkTransferredAsset(transfer.path, static_cast<size_t>(transfer.totSize),
                                                               static_cast<size_t>(MaxCustomFileSize)))
            {
                if (!Poseidon::IsSafeNetworkTransferredAssetPath(transfer.path))
                {
                    LOG_WARN(Network, "[NMTTransferFile] rejected unsafe path '{}'", (const char*)transfer.path);
                }
                else
                {
                    LOG_WARN(Network, "[NMTTransferFile] rejected oversized custom asset '{}' ({} B > {} B)",
                             (const char*)transfer.path, transfer.totSize, maxTransferSize);
                }
                break;
            }
            transfer.path = Poseidon::GetUserDirectory() + transfer.path;
            const int ret = ReceiveFileSegment(transfer);
            if (ret > 0)
            {
                LOG_INFO(Network, "[NMTTransferFile] completed receive path='{}' bytes={} segments={}",
                         (const char*)wirePath, transfer.totSize, transfer.totSegments);
                const int player = NetworkPlayerFaceTransferOwner(wirePath);
                if (RefreshTransferredNetworkFace(player) <= 0)
                {
                    QueueTransferredNetworkFaceRefresh(player);
                }
            }
        }
        break;
        case NMTTransferMissionFile:
        {
            TransferMissionFileMessage transfer;
            transfer.TransferMsg(ctx);
            const RString wirePath = transfer.path;

            // Rewrite transfer path to use client's own cache dir
            // (server embeds its absolute CacheDir which differs per-instance)
            transfer.path = Poseidon::BuildNetworkMissionTransferCachePboPath(_missionHeader.fileName);
            if (transfer.path.GetLength() == 0)
            {
                transfer.path = Poseidon::BuildNetworkMissionTransferCachePboPathFromTransferPath(wirePath);
            }
            if (transfer.path.GetLength() == 0)
            {
                LOG_WARN(Network, "[NMTTransferMissionFile] rejected unsafe mission file name '{}' transfer path '{}'",
                         (const char*)_missionHeader.fileName, (const char*)wirePath);
                break;
            }

            const std::string prefix = GameDirs::MPCurrentPrefix();
            RemoveBank(prefix.c_str());

            int ret = ReceiveFileSegment(transfer);
            LOG_DEBUG(Network, "[NMTTransferMissionFile] path='{}' ret={} state={} missionFileValid={}",
                      (const char*)transfer.path, ret, (int)_state, _missionFileValid);
            if (ret > 0)
            {
                // transfer mission file always into tmp directory (do not rewrite original file)
                const char* ptr = transfer.path;
                const char* ext = strrchr(ptr, '.');
                NET_ERROR(ext);
                NET_ERROR(stricmp(ext, ".pbo") == 0);
                RString path = transfer.path.Substring(0, ext - ptr);
                CreateMPMissionBank(path, _missionHeader.island);

                _missionFileValid = true;
                AskMissionFileMessage ask(true);
                LOG_DEBUG(Network, "[NMTTransferMissionFile] completed, sending AskMissionFile valid=true");
                SendMsg(&ask, NMFGuaranteed);
                NET_ERROR(!_parent->IsServer());
                /*
                          if (PrepareGame())
                          {
                            NET_ERROR(_state == NGSTransferMission);
                            _state = NGSLoadIsland;
                          }
                          else
                          {
                            _state = NGSPrepareSide;
                          }
                */
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
        break;
        case NMTAskForDammage:
        {
            AskForDammageMessage ask;
            ask.TransferMsg(ctx);
            if (ask.who)
            {
                ask.who->DoDammage(ask.owner, ask.modelPos, ask.val, ask.valRange, ask.ammo);
            }
        }
        break;
        case NMTAskForSetDammage:
        {
            AskForSetDammageMessage ask;
            ask.TransferMsg(ctx);
            if (ask.who)
            {
                ask.who->SetDammage(ask.dammage);
            }
        }
        break;
        case NMTAskForAddImpulse:
        {
            AskForAddImpulseMessage ask;
            ask.TransferMsg(ctx);
            if (ask.vehicle)
            {
                ask.vehicle->AddImpulse(ask.force, ask.torque);
            }
        }
        break;
        case NMTAskForMoveVector:
        {
            AskForMoveVectorMessage ask;
            ask.TransferMsg(ctx);
            if (ask.vehicle)
            {
                ask.vehicle->Move(ask.pos);
            }
        }
        break;
        case NMTAskForMoveMatrix:
        {
            AskForMoveMatrixMessage ask;
            ask.TransferMsg(ctx);
            if (ask.vehicle)
            {
                Matrix4 trans;
                trans.SetPosition(ask.pos);
                trans.SetOrientation(ask.orient);
                ask.vehicle->Move(trans);
            }
        }
        break;
        case NMTAskForJoinGroup:
        {
            AskForJoinGroupMessage ask;
            ask.TransferMsg(ctx);
            void ProcessJoinGroups(AIGroup * from, AIGroup * to);
            ProcessJoinGroups(ask.group, ask.join);
        }
        break;
        case NMTAskForJoinUnits:
        {
            AskForJoinUnitsMessage ask;
            ask.TransferMsg(ctx);
            void ProcessJoinGroups(OLinkArray<AIUnit> & units, AIGroup * grp);
            ProcessJoinGroups(ask.units, ask.join);
        }
        break;
        case NMTAskForHideBody:
        {
            AskForHideBodyMessage ask;
            ask.TransferMsg(ctx);
            if (ask.vehicle)
            {
                ask.vehicle->HideBody();
            }
        }
        break;
        case NMTExplosionDammageEffects:
        {
            ExplosionDammageEffectsMessage ask;
            ask.TransferMsg(ctx);
            Ref<AmmoType> ammo;
            if (ask.type.GetLength() > 0)
            {
                VehicleNonAIType* type = VehicleTypes.New(ask.type);
                ammo = dynamic_cast<AmmoType*>(type);
            }
            GLandscape->ExplosionDammageEffects(ask.owner, ask.shot, ask.directHit, ask.pos, ask.dir, ammo,
                                                ask.enemyDammage);
        }
        break;
        case NMTAskForGetIn:
        {
            AskForGetInMessage ask;
            ask.TransferMsg(ctx);
            if (ask.vehicle)
            {
                bool ok = false;
                switch (ask.position)
                {
                    case GIPCommander:
                        if (ask.vehicle->Commander() && !ask.vehicle->Commander()->IsDammageDestroyed())
                        {
                            break;
                        }
                        ask.vehicle->GetInCommander(ask.soldier);
                        ok = true;
                        break;
                    case GIPDriver:
                        if (ask.vehicle->Driver() && !ask.vehicle->Driver()->IsDammageDestroyed())
                        {
                            break;
                        }
                        ask.vehicle->GetInDriver(ask.soldier);
                        ok = true;
                        break;
                    case GIPGunner:
                        if (ask.vehicle->Gunner() && !ask.vehicle->Gunner()->IsDammageDestroyed())
                        {
                            break;
                        }
                        ask.vehicle->GetInGunner(ask.soldier);
                        ok = true;
                        break;
                    case GIPCargo:
                    {
                        for (int i = 0; i < ask.vehicle->GetManCargo().Size(); i++)
                        {
                            if (ask.vehicle->GetManCargo()[i] == nullptr ||
                                ask.vehicle->GetManCargo()[i]->IsDammageDestroyed())
                            {
                                ok = true;
                                break;
                            }
                        }
                    }
                        if (ok)
                        {
                            ask.vehicle->GetInCargo(ask.soldier);
                        }
                        break;
                    default:
                        RptF("Client: Unknown get in position %d", ask.position);
                        break;
                }
                if (ok)
                {
                    ask.vehicle->GetInFinished(ask.soldier->Brain());
                    if (GLOB_WORLD->FocusOn() == ask.soldier->Brain())
                    {
                        GLOB_WORLD->SwitchCameraTo(ask.vehicle, GLOB_WORLD->GetCameraType());
                    }
                }
            }
        }
        break;
        case NMTAskForGetOut:
        {
            AskForGetOutMessage ask;
            ask.TransferMsg(ctx);
            if (ask.vehicle && ask.soldier && ask.soldier->Brain())
            {
                /*
                LOG_DEBUG(Network,
                  "%s: get out from %s to %s",
                  (const char *)ask.soldier->GetDebugName(),
                  (const char *)ask.vehicle->GetDebugName(),
                  ask.parachute ? "Parachute" : "nothing"
                );
                */
                ask.soldier->Brain()->DoGetOut(ask.vehicle, ask.parachute);
            }
        }
        break;
        case NMTAskForChangePosition:
        {
            AskForChangePositionMessage ask;
            ask.TransferMsg(ctx);
            if (ask.vehicle && ask.soldier)
            {
                ask.vehicle->ChangePosition(ask.type, ask.soldier);
            }
        }
        break;
        case NMTAskForAimWeapon:
        {
            AskForAimWeaponMessage ask;
            ask.TransferMsg(ctx);
            if (ask.vehicle)
            {
                int weapon = ask.vehicle->SelectedWeapon();
                if (weapon < 0)
                {
                    weapon = ask.weapon;
                }
                ask.vehicle->AimWeapon(weapon, ask.dir);
            }
        }
        break;
        case NMTAskForAimObserver:
        {
            AskForAimObserverMessage ask;
            ask.TransferMsg(ctx);
            if (ask.vehicle)
            {
                ask.vehicle->AimObserver(ask.dir);
            }
        }
        break;
        case NMTAskForSelectWeapon:
        {
            AskForSelectWeaponMessage ask;
            ask.TransferMsg(ctx);
            if (ask.vehicle)
            {
                ask.vehicle->SelectWeapon(ask.weapon);
            }
        }
        break;
        case NMTAskForAmmo:
        {
            AskForAmmoMessage ask;
            ask.TransferMsg(ctx);
            if (!ask.vehicle)
            {
                break;
            }
            Magazine* state = ask.vehicle->GetMagazineSlot(ask.weapon)._magazine;
            if (!state)
            {
                break;
            }
            saturateMin(ask.burst, state->_ammo);
            state->_ammo -= ask.burst;
            if (state->_ammo < 0)
            {
                state->_ammo = 0;
            }
        }
        break;
        case NMTFireWeapon:
        {
            FireWeaponMessage fire;
            fire.TransferMsg(ctx);
            EntityAI* veh = fire.vehicle;
            if (veh)
            {
                const Magazine* magazine = veh->FindMagazine(fire.magazineCreator, fire.magazineId);
                veh->FireWeaponEffects(fire.weapon, magazine, fire.target);
            }
        }
        break;
        case NMTUpdateWeapons:
        {
            UpdateWeaponsMessage update;
            update.TransferMsg(ctx);
        }
        break;
        case NMTAddWeaponCargo:
        {
            AddWeaponCargoMessage update;
            update.TransferMsg(ctx);
            if (update.vehicle)
            {
                Ref<WeaponType> weapon = WeaponTypes.New(update.weapon);
                update.vehicle->AddWeaponCargo(weapon);
            }
        }
        break;
        case NMTRemoveWeaponCargo:
        {
            RemoveWeaponCargoMessage update;
            update.TransferMsg(ctx);
            if (update.vehicle)
            {
                Ref<WeaponType> weapon = WeaponTypes.New(update.weapon);
                update.vehicle->RemoveWeaponCargo(weapon);
            }
        }
        break;
        case NMTAddMagazineCargo:
        {
            AddMagazineCargoMessage update;
            update.TransferMsg(ctx);
            if (update.vehicle && update.magazine)
            {
                update.vehicle->AddMagazineCargo(update.magazine);
            }
        }
        break;
        case NMTRemoveMagazineCargo:
        {
            RemoveMagazineCargoMessage update;
            update.TransferMsg(ctx);
            if (update.vehicle)
            {
                const Magazine* magazine = update.vehicle->FindMagazine(update.creator, update.id);
                if (magazine)
                {
                    update.vehicle->RemoveMagazineCargo(const_cast<Magazine*>(magazine));
                }
            }
        }
        break;
        case NMTVehicleInit:
        {
            VehicleInitCmd init;
            init.TransferMsg(ctx);
            GameState* gstate = GWorld->GetGameState();
            gstate->VarSet("this", GameValueExt(init.vehicle), true);
            gstate->Execute(init.init);
        }
        break;
        case NMTVehicleDestroyed:
        {
            VehicleDestroyedMessage info;
            info.TransferMsg(ctx);
            if (info.killed)
            {
                Person* person = dyn_cast<Person>(info.killed);
                if (person && person->GetInfo()._name.GetLength() == 0 && person->IsNetworkPlayer())
                {
                    const PlayerIdentity* identity = FindIdentity(person->GetRemotePlayer());
                    if (identity)
                    {
                        person->GetInfo()._name = identity->name;
                    }
                }
            }
            if (info.killer)
            {
                Person* person = dyn_cast<Person>(info.killer);
                if (!person)
                {
                    Transport* vehicle = dyn_cast<Transport>(info.killer);
                    if (vehicle)
                    {
                        person = vehicle->Commander();
                        if (!person || !person->IsNetworkPlayer())
                        {
                            person = vehicle->Gunner();
                        }
                        if (!person || !person->IsNetworkPlayer())
                        {
                            person = vehicle->Driver();
                        }
                    }
                }
                if (person && person->GetInfo()._name.GetLength() == 0 && person->IsNetworkPlayer())
                {
                    const PlayerIdentity* identity = FindIdentity(person->GetRemotePlayer());
                    if (identity)
                    {
                        person->GetInfo()._name = identity->name;
                    }
                }
            }

            GStats.OnVehicleDestroyed(info.killed, info.killer);
            // add experience
            if (info.killer && info.killed)
            {
                // use Entity member to get original target side
                // all dead bodies are considered civilian
                TargetSide origSide = info.killed->Entity::GetTargetSide();
                const VehicleType& type = *info.killed->GetType();

                // increase killer's experience
                AIUnit* kBrain = info.killer->CommanderUnit();
                if (kBrain && kBrain->IsLocal())
                {
                    kBrain->IncreaseExperience(type, origSide);
                    // send radio message
                    AIGroup* killerGroup = kBrain->GetGroup();
                    if (killerGroup && killerGroup->GetCenter()->IsEnemy(origSide))
                    {
                        // find corresponding target
                        Target* tar = killerGroup->FindTargetAll(info.killed);
                        if (tar)
                        {
                            // mark killer
                            // when destroyed will be set, it will be marked for reporting
                            tar->idKiller = info.killer;
                        }
                    }
                }
                if (info.killer->GunnerUnit() && info.killer->GunnerUnit()->IsLocal() &&
                    info.killer->GunnerUnit() != kBrain)
                {
                    info.killer->GunnerUnit()->IncreaseExperience(type, origSide);
                }
                if (info.killer->PilotUnit() && info.killer->PilotUnit()->IsLocal() &&
                    info.killer->PilotUnit() != kBrain)
                {
                    info.killer->PilotUnit()->IncreaseExperience(type, origSide);
                }
            }
        }
        break;
        case NMTVehicleDamaged:
        {
            VehicleDamagedMessage info;
            info.TransferMsg(ctx);
            if (info._damaged)
                GStats.OnVehicleDamaged(info._damaged, info._killer, info._damage, info._ammo);
        }
        break;
        case NMTIncomingMissile:
        {
            IncomingMissileMessage info;
            info.TransferMsg(ctx);
            if (info._target)
            {
                info._target->OnEvent(EEIncomingMissile, info._ammo, info._owner);
            }
        }
        break;
        case NMTMarkerCreate:
        {
            Person* soldier = GWorld->GetRealPlayer();
            if (!soldier)
            {
                break;
            }
            MarkerCreateMessage marker;
            marker.TransferMsg(ctx);
            if (marker.channel != CCGlobal && !FindUnit(GWorld->GetRealPlayer(), marker.units))
            {
                break;
            }
            RString name = marker.marker.name;
            int index = -1;
            for (int i = 0; i < markersMap.Size(); i++)
            {
                if (stricmp(markersMap[i].name, name) == 0)
                {
                    index = i;
                    break;
                }
            }
            if (index < 0)
            {
                index = markersMap.Add();
            }
            markersMap[index] = marker.marker;
        }
        break;
        case NMTMarkerDelete:
        {
            MarkerDeleteMessage info;
            info.TransferMsg(ctx);
            for (int i = 0; i < markersMap.Size(); i++)
            {
                ArcadeMarkerInfo& marker = markersMap[i];
                if (marker.name == info.name)
                {
                    markersMap.Delete(i);
                    break;
                }
            }
        }
        break;
            /*
                case NMTRespawn:
                  {
                    RespawnQueueItem item;
                    item.TransferMsg(ctx);
                    DoRespawn(item);
                  }
                  break;
            */
        case NMTSetFlagOwner:
        {
            SetFlagOwnerMessage ask;
            ask.TransferMsg(ctx);
            if (!ask.carrier)
            {
                break;
            }
            ask.carrier->SetFlagOwner(ask.owner);
        }
        break;
        case NMTSetFlagCarrier:
        {
            SetFlagCarrierMessage ask;
            ask.TransferMsg(ctx);
            if (!ask.owner)
            {
                break;
            }
            ask.owner->SetFlagCarrier(ask.carrier);
        }
        break;
        case NMTMsgVTarget:
        {
            RadioMessageVTarget radioMsg;
            radioMsg.TransferMsg(ctx);
            radioMsg.Send();
        }
        break;
        case NMTMsgVFire:
        {
            RadioMessageVFire radioMsg;
            radioMsg.TransferMsg(ctx);
            radioMsg.Send();
        }
        break;
        case NMTMsgVMove:
        {
            RadioMessageVMove radioMsg;
            radioMsg.TransferMsg(ctx);
            radioMsg.Send();
        }
        break;
        case NMTMsgVFormation:
        {
            RadioMessageVFormation radioMsg;
            radioMsg.TransferMsg(ctx);
            radioMsg.Send();
        }
        break;
        case NMTMsgVSimpleCommand:
        {
            RadioMessageVSimpleCommand radioMsg;
            radioMsg.TransferMsg(ctx);
            radioMsg.Send();
        }
        break;
        case NMTMsgVLoad:
        {
            RadioMessageVLoad radioMsg;
            radioMsg.TransferMsg(ctx);
            radioMsg.Send();
        }
        break;
        case NMTMsgVAzimut:
        {
            RadioMessageVAzimut radioMsg;
            radioMsg.TransferMsg(ctx);
            radioMsg.Send();
        }
        break;
        case NMTMsgVStopTurning:
        {
            RadioMessageVStopTurning radioMsg;
            radioMsg.TransferMsg(ctx);
            radioMsg.Send();
        }
        break;
        case NMTMsgVFireFailed:
        {
            RadioMessageVFireFailed radioMsg;
            radioMsg.TransferMsg(ctx);
            radioMsg.Send();
        }
        break;
        case NMTChangeOwner:
        {
            ChangeOwnerMessage co;
            co.TransferMsg(ctx);
            TryApplyChangeOwner(co, true);
        }
        break;
        case NMTPlaySound:
        {
            PlaySoundMessage sound;
            sound.TransferMsg(ctx);

            ++GTriNetSoundsReceived; // tri test observable (replicated-sound count)

            IWave* wave =
                GSoundScene->OpenAndPlayOnce(sound.name, sound.position, sound.speed, sound.volume, sound.freq);
            if (wave)
            {
                GSoundScene->AddSound(wave);

                for (int i = 0; i < _receivedSounds.Size();)
                {
                    PlaySoundInfo& info = _receivedSounds[i];
                    if (info.wave)
                    {
                        i++;
                    }
                    else
                    {
                        _receivedSounds.Delete(i);
                    }
                }

                int index = _receivedSounds.Add();
                _receivedSounds[index].creator = sound.creator;
                _receivedSounds[index].id = sound.soundId;
                _receivedSounds[index].wave = wave;
            }
        }
        break;
        case NMTSoundState:
        {
            SoundStateMessage sound;
            sound.TransferMsg(ctx);

            for (int i = 0; i < _receivedSounds.Size();)
            {
                PlaySoundInfo& info = _receivedSounds[i];
                if (!info.wave)
                {
                    _receivedSounds.Delete(i);
                    continue;
                }
                if (info.creator == sound.creator && info.id == sound.soundId)
                {
                    const bool applied =
                        Poseidon::ApplyReplicatedNetworkSoundState(info.wave, static_cast<SoundStateType>(sound.state),
                                                                   [](IWave* wave) { GSoundScene->DeleteSound(wave); });
                    if (!applied)
                    {
                        Fail("Sound state");
                    }
                }
                i++;
            }
        }
        break;
        case NMTPublicVariable:
        {
            PublicVariableMessage varMsg;
            varMsg.TransferMsg(ctx);

            RString name = varMsg._name;
            QIStream in;
            in.init(varMsg._value.Data(), varMsg._value.Size());

            GameState* gstate = GWorld->GetGameState();
            GameValue val = Poseidon::DeserializeScriptValue(in, ResolveClientNetworkObject, this);
            // The variable name is wire-controlled; require a well-formed, bounded
            // identifier before touching the script var table.
            if (!val.GetNil() && WireBounds::ValidIdentifier(name, 256))
            {
                gstate->VarSet(name, val);
            }
        }
        break;
        case NMTGroupSynchronization:
        {
            GroupSynchronizationMessage sync;
            sync.TransferMsg(ctx);
            if (sync._group)
            {
                synchronized[sync._synchronization].SetActive(sync._group, sync._active);
            }
        }
        break;
        case NMTDetectorActivation:
        {
            DetectorActivationMessage act;
            act.TransferMsg(ctx);
            if (act._detector)
            {
                act._detector->DoActivate();
            }
        }
        break;
        case NMTAskForCreateUnit:
        {
            AskForCreateUnitMessage ask;
            ask.TransferMsg(ctx);
            void CreateUnit(AIGroup * group, RString type, Vector3Par position, RString init, float skill, Rank rank);
            if (ask._group)
            {
                CreateUnit(ask._group, ask._type, ask._position, ask._init, ask._skill, (Rank)ask._rank);
            }
        }
        break;
        case NMTAskForDeleteVehicle:
        {
            AskForDeleteVehicleMessage ask;
            ask.TransferMsg(ctx);
            void DeleteVehicle(Entity * veh);
            if (ask._vehicle)
            {
                DeleteVehicle(ask._vehicle);
            }
        }
        break;
        case NMTAskForReceiveUnitAnswer:
        {
            AskForReceiveUnitAnswerMessage ask;
            ask.TransferMsg(ctx);
            if (ask._to)
            {
                ask._to->ReceiveAnswer(ask._from, (AI::Answer)ask._answer);
            }
        }
        break;
        case NMTAskForGroupRespawn:
        {
            AskForGroupRespawnMessage ask;
            ask.TransferMsg(ctx);
            /*
            RptF
            (
              "Group respawn: ask received - person %s, group %s, from %d",
              ask._person ? (const char *)ask._person->GetDebugName() : nullptr,
              ask._group ? (const char *)ask._group->GetDebugName() : nullptr,
              ask._from
            );
            */
            if (ask._person && ask._group && ask._person->Brain() && ask._person->Brain()->GetGroup() == ask._group)
            {
                Person* ProcessGroupRespawn(Person * person, int player);
                Person* respawn = ProcessGroupRespawn(ask._person, ask._from);

                GroupRespawnDoneMessage answer;
                answer._person = ask._person;
                answer._killer = ask._killer;
                answer._respawn = respawn;
                answer._to = ask._from;
                /*
                RptF
                (
                  "Group respawn: sending answer - person %s, respawn %s, to %d",
                  answer._person ? (const char *)answer._person->GetDebugName() : nullptr,
                  answer._respawn ? (const char *)answer._respawn->GetDebugName() : nullptr,
                  answer._to
                );
                */
                SendMsg(&answer, NMFGuaranteed);
            }
        }
        break;
        case NMTAskForActivateMine:
        {
            AskForActivateMineMessage ask;
            ask.TransferMsg(ctx);
            if (ask._mine)
            {
                ask._mine->SetActive(ask._activate);
            }
        }
        break;
        case NMTAskForInflameFire:
        {
            AskForInflameFireMessage ask;
            ask.TransferMsg(ctx);
            if (ask._fireplace)
            {
                ask._fireplace->Inflame(ask._fire);
            }
        }
        break;
        case NMTAskForAnimationPhase:
        {
            AskForAnimationPhaseMessage ask;
            ask.TransferMsg(ctx);
            if (ask._vehicle)
            {
                ask._vehicle->SetAnimationPhase(ask._animation, ask._phase);
            }
        }
        break;
        case NMTCopyUnitInfo:
        {
            CopyUnitInfoMessage copy;
            copy.TransferMsg(ctx);
            if (copy._from && copy._to)
            {
                AIUnitInfo& info = copy._from->GetInfo();
                saturateMax(info._experience, info._initExperience);
                copy._to->SetRemotePlayer(copy._from->GetRemotePlayer());
                copy._to->SetInfo(info);
                copy._to->SetFace(info._face, info._name);
                copy._to->SetGlasses(info._glasses);
                FlushTransferredNetworkFaceRefreshes();
            }
        }
        break;
        case NMTGroupRespawnDone:
        {
            GroupRespawnDoneMessage answer;
            answer.TransferMsg(ctx);
            /*
            RptF
            (
              "Group respawn: answer received - person %s, respawn %s",
              answer._person ? (const char *)answer._person->GetDebugName() : nullptr,
              answer._respawn ? (const char *)answer._respawn->GetDebugName() : nullptr
            );
            */
            if (answer._person)
            {
                if (answer._respawn)
                {
                    void GroupRespawnDone(Person * person, EntityAI * killer, Person * respawn);
                    GroupRespawnDone(answer._person, answer._killer, answer._respawn);
                }
                else
                {
                    void ProcessSeagullRespawn(Person * person, EntityAI * killer);
                    ProcessSeagullRespawn(answer._person, answer._killer);
                }
            }
        }
        break;
        case NMTChat:
        {
            ChatMessage chat;
            chat.TransferMsg(ctx);
            if (chat.sender)
            {
                GChatList.Add((ChatChannel)chat.channel, chat.sender, chat.text, false, true);
            }
            else
            {
                GChatList.Add((ChatChannel)chat.channel, chat.name, chat.text, false, true);
            }
            if (_chatSound.name.GetLength() > 0)
            {
                IWave* wave = GSoundScene->OpenAndPlayOnce2D(_chatSound.name, _chatSound.vol, _chatSound.freq, false);
                if (wave)
                {
                    wave->SetKind(WaveMusic); // UI sounds considered music???
                    wave->SetSticky(true);    // enable chat sounds in the lobby
                    GSoundScene->AddSound(wave);
                }
            }
        }
        break;
        case NMTRadioChat:
        {
            RadioChatMessage chat;
            chat.TransferMsg(ctx);
            Person* soldier = GWorld->GetRealPlayer();
            if (!soldier)
            {
                break;
            }
            if (chat.channel != CCGlobal && !FindUnit(GWorld->GetRealPlayer(), chat.units))
            {
                break;
            }
            GChatList.Add((ChatChannel)chat.channel, chat.sender, chat.text, false, false);
            if (chat.sender && chat.sentence.Size() > 0)
            {
                AIUnit* unit = soldier->Brain();
                if (!unit)
                {
                    break;
                }
                RadioChannel* channel = FindChannel(unit, chat.channel);
                if (channel)
                {
                    chat.sentence.Say(channel, chat.sender->GetSpeaker());
                }
            }
        }
        break;
        case NMTRadioChatWave:
        {
            RadioChatWaveMessage chat;
            chat.TransferMsg(ctx);
            Person* soldier = GWorld->GetRealPlayer();
            if (!soldier)
            {
                break;
            }
            if (chat.channel != CCGlobal && !FindUnit(GWorld->GetRealPlayer(), chat.units))
            {
                break;
            }
            if (chat.wave.GetLength() > 0)
            {
                AIUnit* unit = soldier->Brain();
                if (!unit)
                {
                    break;
                }
                RadioChannel* channel = FindChannel(unit, chat.channel);
                if (channel)
                {
                    RString player;
                    if (chat.wave[0] == '#' && chat.sender)
                    {
                        player = Poseidon::BuildNetworkPlayerStorageKey(chat.sender->GetPerson()->GetRemotePlayer());
                    }
                    channel->Say(chat.wave, chat.sender, chat.senderName, player, 2.0);
                }
            }
        }
        break;
        case NMTSetSpeaker:
        {
            SetSpeakerMessage message;
            message.TransferMsg(ctx);
            if (message.on)
            {
                int index = -1;
                for (int i = 0; i < _soundBuffers.Size(); i++)
                {
                    if (_soundBuffers[i].player == message.player)
                    {
                        _soundBuffers[i].buffer = nullptr;
                        index = i;
                        break;
                    }
                }
                if (index < 0)
                {
                    index = _soundBuffers.Add();
                    _soundBuffers[index].player = message.player;
                }
                _soundBuffers[index].buffer = _client->Create3DSoundBuffer(message.player);
                NetworkObject* networkObject = GetObject(message.object);
                if (networkObject)
                {
                    Vector3 pos = networkObject->GetSpeakerPosition();
                    if (_soundBuffers[index].buffer)
                    {
                        _soundBuffers[index].buffer->SetPosition(pos[0], pos[1], pos[2]);
                    }
                }
                _soundBuffers[index].object = networkObject;
            }
            else
            {
                for (int i = 0; i < _soundBuffers.Size(); i++)
                {
                    if (_soundBuffers[i].player == message.player)
                    {
                        _soundBuffers[i].buffer = nullptr;
                        if (_soundBuffers[i].object)
                        {
                            _soundBuffers[i].object->SetRandomLip(false);
                        }
                        _soundBuffers.Delete(i);
                        break;
                    }
                }
            }
        }
        break;
        case NMTGameState:
        {
            ChangeGameState gs;
            gs.TransferMsg(ctx);
            // Stale join-time leftovers are pre-load states that can arrive reordered
            // while this JIP client is still between mission transfer and play; they must not
            // roll the join back. Once the client is in play, lower states are real
            // transitions (mission end, #restart cycling to debriefing/lobby) and must apply.
            if (!_parent->IsServer() && _missionHeader.joinInProgress && _state >= NGSTransferMission &&
                _state < NGSPlay && gs.gameState <= NGSDebriefingOK)
            {
                LOG_DEBUG(Network, "[NMTGameState] ignoring stale JIP state {} while clientState={}", (int)gs.gameState,
                          (int)_state);
                break;
            }
            _serverState = gs.gameState;
            if (gs.gameState >= NGSPlay)
                _serverReachedPlay = true;

            LOG_DEBUG(Network, "[NMTGameState] serverState={} clientState={} hasRole={} serverPlaying={}",
                      (int)gs.gameState, (int)_state, GetMyPlayerRole() != nullptr, _serverReachedPlay);

            // Duplicity with NetworkServer::SetGameState [1/31/2002]
            switch (gs.gameState)
            {
                case NGSPrepareRole:
                    if (IsDedicatedServer())
                    {
                        _state = NGSPrepareRole;
                    }
                    break;
                case NGSPrepareOK:
                    if (GetMyPlayerRole() || IsDedicatedServer())
                    {
                        _state = NGSPrepareOK;
                    }
                    break;
                case NGSDebriefing:
                case NGSDebriefingOK:
                    if (_state >= NGSDebriefing)
                    {
                        _state = gs.gameState;
                    }
                    break;
                case NGSTransferMission:
                    LOG_DEBUG(Network, "[NMTGameState] NGSTransferMission: _state={} role={} missionValid={}",
                              (int)_state, GetMyPlayerRole() != nullptr, _missionFileValid);
                    if (Poseidon::ClientShouldAcceptTransferState(_state, NGSLoadIsland) &&
                        (GetMyPlayerRole() || IsDedicatedServer()))
                    {
                        _state = NGSTransferMission;
                    }
                    break;
                case NGSLoadIsland:
                    LOG_DEBUG(Network, "[NMTGameState] NGSLoadIsland: _state={} role={} missionValid={} isServer={}",
                              (int)_state, GetMyPlayerRole() != nullptr, _missionFileValid, _parent->IsServer());
                    if (GetMyPlayerRole() || IsDedicatedServer())
                    {
                        if (_parent->IsServer())
                        {
                            _state = NGSLoadIsland;
                        }
                        else
                        {
                            if (!Poseidon::ClientShouldPrepareLoadIsland(_state, NGSLoadIsland))
                            {
                                _state = NGSLoadIsland;
                            }
                            else if (_missionFileValid && PrepareGame())
                            {
                                _state = NGSLoadIsland;
                            }
                            else if (_state < NGSPrepareSide)
                            {
                                _state = NGSPrepareSide;
                            }
                            else
                            {
                                LOG_WARN(Network,
                                         "[NMTGameState] NGSLoadIsland: PrepareGame skipped/failed! "
                                         "_missionFileValid={} _state={}",
                                         _missionFileValid, (int)_state);
                            }
                        }
                    }
                    else
                    {
                        LOG_WARN(Network, "[NMTGameState] NGSLoadIsland: no role and not dedicated, skipping!");
                    }
                    break;
                case NGSBriefing:
                    LOG_DEBUG(Network, "[NMTGameState] NGSBriefing: _state={} worldMode={}", (int)_state,
                              GWorld ? (int)GWorld->GetMode() : -1);
                    if (GetMaxError() >= EMError)
                    {
                        // some error when loading mission
                        RString message = GetMaxErrorMessage();
                        if (message.GetLength() <= 0)
                        {
                            message = LocalizeString(IDS_MSG_ADDON_MISSING);
                        }

                        const PlayerIdentity* id = FindIdentity(_player);
                        if (id)
                        {
                            RString senderName = id->name;
                            GChatList.Add(CCGlobal, senderName, message, false, true);
                            RefArray<NetworkObject> dummy;
                            GNetworkManager.Chat(CCGlobal, senderName, dummy, message);
                        }
                        // we might want to disconnect now
                    }

                    _respawnQueue.Clear();
                    if (_state >= NGSLoadIsland)
                    {
                        TryApplyPendingChangeOwners();
                        _state = NGSBriefing;
                        TryApplyPendingSelectPlayer(NetworkId::Null());
                    }
                    break;
                case NGSPlay:
                    LOG_DEBUG(Network, "[NMTGameState] NGSPlay: _state={} worldMode={}", (int)_state,
                              GWorld ? (int)GWorld->GetMode() : -1);
                    if (_state == NGSBriefing || (_jip && _missionHeader.joinInProgress && _state >= NGSLoadIsland))
                    {
                        if (_state != NGSBriefing)
                        {
                            LOG_INFO(Network, "JIP: accepting NGSPlay from client state {}", (int)_state);
                            _respawnQueue.Clear();
                        }
                        TryApplyPendingChangeOwners();
                        TryApplyPendingSelectPlayer(NetworkId::Null());
                        // Register client info as network object
                        CreateLocalObject(_clientInfo);

                        _state = NGSPlay;

                        // Run client-side init scripts for JIP players
                        if (_jip)
                        {
                            void RunMissionScript(const char* filename, GameValue argument);
                            Poseidon::RunMissionScript("initPlayerLocal.sqs", GameValue());
                            Poseidon::RunMissionScript("initJIP.sqs", GameValue());
                        }

                        // number of bodies we want to hide in mission
                        if (IsBotClient())
                        {
                            _hideBodies = -BODIES_ON_BOT_CLIENT;
                        }
                        else
                        {
                            int nClients = 0;
                            for (int i = 0; i < NPlayerRoles(); i++)
                            {
                                const PlayerRole* role = GetPlayerRole(i);
                                if (role->player != NO_PLAYER && role->player != AI_PLAYER)
                                {
                                    nClients++;
                                }
                            }
                            NET_ERROR(nClients > 0);
                            _hideBodies = -(BODIES_ON_CLIENTS / nClients);
                            saturateMin(_hideBodies, -1);
                        }
                        _bodies.Clear();
                    }
                    // else if (_state < NGSPrepareSide) _state = NGSBriefing;
                    _missionHeader.start = GlobalTickCount();
                    break;
                default:
                    _state = gs.gameState;
                    break;
            }
        }
        break;
        case NMTDeleteObject:
            if (_state < NGSLoadIsland)
            {
                break; // updates from the last session
            }
            {
                DeleteObjectMessage dom;
                dom.TransferMsg(ctx);
                DestroyRemoteObject(dom.object);
            }
            break;
        case NMTDeleteCommand:
            if (_state < NGSLoadIsland)
            {
                break; // updates from the last session
            }
            {
                DeleteCommandMessage dc;
                dc.TransferMsg(ctx);
                for (int i = 0; i < _remoteObjects.Size(); i++)
                {
                    NetworkRemoteObjectInfo& info = _remoteObjects[i];
                    if (info.id == dc.object)
                    {
                        if (dc.subgrp && info.object)
                        {
                            dc.subgrp->DeleteCommand(dc.index, dynamic_cast<Command*>(info.object.GetLink()));
                        }
                        if (DiagLevel >= 1)
                        {
                            DiagLogF("Client: remote command destroyed %d:%d", dc.object.creator, dc.object.id);
                        }
                        _remoteObjects.Delete(i);
                        break;
                    }
                }
            }
            break;
        case NMTCreateObject:
            CreateRemoteObject(ctx, (Object*)nullptr);
            break;
        case NMTCreateVehicle:
            CreateRemoteObject(ctx, (Vehicle*)nullptr);
            break;
        case NMTCreateDetector:
            CreateRemoteObject(ctx, (Detector*)nullptr);
            break;
        case NMTCreateShot:
            CreateRemoteObject(ctx, (Shot*)nullptr);
            break;
        case NMTCreateExplosion:
            CreateRemoteObject(ctx, (Explosion*)nullptr);
            break;
        case NMTCreateCrater:
            CreateRemoteObject(ctx, (Crater*)nullptr);
            break;
        case NMTCreateCraterOnVehicle:
            CreateRemoteObject(ctx, (CraterOnVehicle*)nullptr);
            break;
        case NMTCreateObjectDestructed:
            CreateRemoteObject(ctx, (ObjectDestructed*)nullptr);
            break;
        case NMTCreateAICenter:
            CreateRemoteObject(ctx, (AICenter*)nullptr);
            break;
        case NMTCreateAIGroup:
            CreateRemoteObject(ctx, (AIGroup*)nullptr);
            break;
        case NMTCreateAISubgroup:
            CreateRemoteObject(ctx, (AISubgroup*)nullptr);
            break;
        case NMTCreateAIUnit:
            CreateRemoteObject(ctx, (AIUnit*)nullptr);
            break;
        case NMTCreateCommand:
            CreateRemoteObject(ctx, (Command*)nullptr);
            break;
        case NMTCreateHelicopter:
            CreateRemoteObject(ctx, (HelicopterAuto*)nullptr);
            break;
        case NMTUpdateDammageObject:
        case NMTUpdateDammageVehicleAI:
            ctx.SetClass(NMCUpdateDammage);
            goto Update;
        case NMTUpdatePositionVehicle:
        case NMTUpdatePositionMan:
        case NMTUpdatePositionTank:
        case NMTUpdatePositionCar:
        case NMTUpdatePositionAirplane:
        case NMTUpdatePositionHelicopter:
        case NMTUpdatePositionShip:
        case NMTUpdatePositionSeagull:
        case NMTUpdatePositionMotorcycle:
            ctx.SetClass(NMCUpdatePosition);
            // continue with update
        case NMTUpdateObject:
        case NMTUpdateVehicle:
        case NMTUpdateDetector:
        case NMTUpdateFlag:
        case NMTUpdateShot:
        case NMTUpdateMine:
        case NMTUpdateVehicleAI:
        case NMTUpdateVehicleBrain:
        case NMTUpdateVehicleSupply:
        case NMTUpdateTransport:
        case NMTUpdateMan:
        case NMTUpdateTankOrCar:
        case NMTUpdateTank:
        case NMTUpdateCar:
        case NMTUpdateAirplane:
        case NMTUpdateHelicopter:
        case NMTUpdateParachute:
        case NMTUpdateShip:
        case NMTUpdateSeagull:
        case NMTUpdateAICenter:
        case NMTUpdateAIGroup:
        case NMTUpdateAISubgroup:
        case NMTUpdateAIUnit:
        case NMTUpdateCommand:
        case NMTUpdateMotorcycle:
        case NMTUpdateFireplace:
        Update:
            if (_state < NGSLoadIsland)
            {
                break; // updates from the last session
            }
            {
                // find IndicesNetworkObject level
                NET_ERROR(dynamic_cast<const IndicesNetworkObject*>(ctx.GetIndices()))
                const IndicesNetworkObject* indices = static_cast<const IndicesNetworkObject*>(ctx.GetIndices());

                NetworkId id;
                ctx.IdxTransfer(indices->objectCreator, id.creator);
                ctx.IdxTransfer(indices->objectId, id.id);
                NetworkObject* object = GetObject(id);
                if (!object)
                {
                    RptF("Client: Object %d:%d (type %s) not found.", id.creator, id.id, NetworkMessageTypeNames[type]);
                    break;
                }
                if (object->IsLocal())
                {
                    RptF("Client: Object (id %d:%d, type %s) is local - update is ignored.", id.creator, id.id,
                         NetworkMessageTypeNames[type]);
                    break;
                }
                object->TransferMsg(ctx);
                if (DiagLevel >= 4)
                {
                    DiagLogF("Client: object %d:%d updated", id.creator, id.id);
                }
            }
            break;
        case NMTUpdateClientInfo:
            Fail("Unexpected on client");
            break;
        case NMTPlayerUpdate:
        {
            ctx.SetClass(NMCUpdatePosition);
            NET_ERROR(dynamic_cast<const IndicesPlayerUpdate*>(ctx.GetIndices()))
            const IndicesPlayerUpdate* indices = static_cast<const IndicesPlayerUpdate*>(ctx.GetIndices());
            // check which identity is this message affecting
            int dpnid;
            if (ctx.IdxTransfer(indices->dpnid, dpnid) == TMOK)
            {
                // find corresponding identity
                PlayerIdentity* pi = FindIdentity(dpnid);
                if (pi)
                {
                    // update identity
                    pi->TransferMsg(ctx);
                }
            }
            break;
        }
        case NMTShowTarget:
        {
            ShowTargetMessage show;
            show.TransferMsg(ctx);
            Person* person = show.vehicle;
            AIUnit* unit = person ? person->Brain() : nullptr;
            AIGroup* grp = unit ? unit->GetGroup() : nullptr;
            if (grp && show.target)
            {
                Target* target = grp->FindTarget(show.target);
                unit->AssignTarget(target);
                if (GWorld->UI())
                    GWorld->UI()->ShowTarget();
            }
        }
        break;
        case NMTShowGroupDir:
        {
            ShowGroupDirMessage show;
            show.TransferMsg(ctx);
            Person* person = show.vehicle;
            AIUnit* unit = person ? person->Brain() : nullptr;
            AIGroup* grp = unit ? unit->GetGroup() : nullptr;
            AISubgroup* subgrp = grp ? grp->MainSubgroup() : nullptr;
            if (subgrp)
            {
                subgrp->SetDirection(show.dir);
                if (GWorld->UI())
                    GWorld->UI()->ShowGroupDir();
            }
        }
        break;
        case NMTMissionParams:
        {
            MissionParamsMessage params;
            params.TransferMsg(ctx);

            _param1 = params._param1;
            _param2 = params._param2;
        }
        break;
        case NMTPublicExec:
        {
            PublicExecMessage params;
            params.TransferMsg(ctx);

            GameState* gstate = GWorld->GetGameState();
            gstate->Execute(params._command);
        }
        break;
        case NMTRemoteExec:
        {
            RemoteExecMessage params;
            params.TransferMsg(ctx);

            GameValue payload = Poseidon::DecodeScriptValue(params._params, ResolveClientNetworkObject, this);
            if (!payload.GetNil() && GWorld && GWorld->GetGameState())
                ExecuteNamedRemoteExec(GWorld->GetGameState(), params._name, payload);
        }
        break;
        default:
            RptF("Client: Unhandled user message %d", (int)type);
            break;
    }

    FlushTransferredNetworkFaceRefreshes();

#ifndef NDEBUG
    if (_state == NGSPlay && validBefore)
    {
        bool validAfter = AssertAIValid();
        if (!validAfter)
        {
            DiagLogF("Client: error in structure after message %s", NetworkMessageTypeNames[type]);
            ctx.LogMessage(4, "\t");
        }
    }
#endif
}
