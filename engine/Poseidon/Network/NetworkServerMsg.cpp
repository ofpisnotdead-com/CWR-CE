#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Network/NetworkServerCommon.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Core/ModSystem.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <Poseidon/Core/Config/Config.hpp>
#include <Poseidon/Network/NetworkImpl.hpp>
#include <Poseidon/Network/NetworkCustomAssets.hpp>
#include <Poseidon/Core/Global.hpp>
// #include "strIncl.hpp"
#include <Poseidon/UI/Locale/StringtableExt.hpp>

#include <Poseidon/Network/NetworkConfig.hpp>
#include <Poseidon/Network/NetworkCustomAssets.hpp>
#include <limits.h>
#include <stdio.h>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Memory/MemAlloc.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/Memtype.h>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

using namespace Poseidon;
namespace Poseidon
{
RString GetServerConfig();
}

#include <Poseidon/AI/ArcadeTemplate.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/Entities/Infantry/Person.hpp>
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

using Poseidon::Foundation::MemAllocSA;

#ifdef _WIN32
#include <io.h>
#include <winsock2.h>
#include <process.h>

namespace Poseidon
{
void RunMissionScript(char const*, class ::GameValue);
}
#endif

#define LOG_SEND_PROCESS 0
#define LOG_PLAYERS 1
extern int MaxCustomFileSize;

int NetworkServer::FindPendingMessage(NetworkUpdateInfo* update) const
{
    // try to find this message in pending message list
    for (int i = 0; i < _pendingMessages.Size(); i++)
    {
        if (_pendingMessages[i].update == update)
        {
            return i;
        }
    }
    return -1;
}

int NetworkServer::FindPendingMessage(DWORD msgID) const
{
    // try to find this message in pending message list
    for (int i = 0; i < _pendingMessages.Size(); i++)
    {
        if (_pendingMessages[i].msgID == msgID)
        {
            return i;
        }
    }
    return -1;
}

void NetworkServer::AddPendingMessage(DWORD msgID, NetworkObjectInfo* info, NetworkUpdateInfo* update,
                                      NetworkPlayerObjectInfo* player)
{
    NET_ERROR(info);
    NET_ERROR(msgID != MSGID_REPLACE);
    NetPendingMessage& pend = _pendingMessages.Append();
    pend.msgID = msgID;
    pend.info = info;
    pend.player = player;
    pend.update = update;
    // note: similiar message (same msgId and info) may be already pending
    // two updates for one object may travel in one message
}

bool NetworkUpdateInfo::OnSendComplete(bool ok)
{
    if (!lastCreatedMsg)
    {
        Fail("Message ID without message");
        lastCreatedMsgId = 0xFFFFFFFF;
        lastCreatedMsgTime = 0;
        return false;
    }
    if (ok)
    {
        // message sent
        lastSentMsg = lastCreatedMsg;
    }
    else
    {
        if (DiagLevel >= 4)
        {
            DiagLogF("Server: sent of message %x failed", lastCreatedMsgId);
        }
    }
    lastCreatedMsg = nullptr;
    lastCreatedMsgId = 0xFFFFFFFF;
    lastCreatedMsgTime = 0;
#if LOG_SEND_PROCESS
    LOG_DEBUG(Network, "  - update info {:x} updated", (uintptr_t)&info);
#endif
    return true;
}

void NetworkServer::OnSendComplete(DWORD msgID, bool ok)
{
#if LOG_SEND_PROCESS
    LOG_DEBUG(Network, "Server: Send {:x} completed: {}", msgID, ok ? "ok" : "failed");
#endif
    // try to find this message in pending message list
    int foundCount = 0;
    for (int i = 0; i < _pendingMessages.Size(); i++)
    {
        if (_pendingMessages[i].msgID == msgID)
        {
            //  we have object candidate - search in it
            NetworkObjectInfo* oInfo = _pendingMessages[i].info;
            NetworkPlayerObjectInfo* poInfo = _pendingMessages[i].player;
            NetworkUpdateInfo* uInfo = _pendingMessages[i].update;
            // verify update is in player
            NET_ERROR(uInfo >= poInfo->updates && uInfo < poInfo->updates + NMCUpdateN);
            // verify player exists in given object
            bool found = false;
            for (int j = 0; j < oInfo->playerObjects.Size(); j++)
            {
                if (oInfo->playerObjects[j] == poInfo)
                {
                    found = true;
                }
            }
            NET_ERROR(found);
            NET_ERROR(uInfo->lastCreatedMsgId == msgID);
            if (uInfo->lastCreatedMsgId == msgID)
            {
#if LOG_SEND_PROCESS
                LOG_DEBUG(Network, "Processed pending message {:x} ({}:{})", msgID, oInfo->id.creator, oInfo->id.id);
#endif
                uInfo->OnSendComplete(ok);
                foundCount++;
            }
            // pending message processed - delete it
            _pendingMessages.Delete(i);
            i--;
        }
    }
    if (foundCount > 0)
    {
        return;
    }
    //  if it failed, it is probably some bug
    //  to be robust, we will perform complete dumb search

    for (int o = 0; o < _objects.Size(); o++)
    {
        NetworkObjectInfo& oInfo = *_objects[o];
        for (int i = 0; i < oInfo.playerObjects.Size(); i++)
        {
            NetworkPlayerObjectInfo& poInfo = *oInfo.playerObjects[i];
            for (int j = NMCUpdateFirst; j < NMCUpdateN; j++)
            {
                NetworkUpdateInfo& info = poInfo.updates[j];
                if (info.lastCreatedMsgId == msgID)
                {
                    LOG_ERROR(Network, "dumb search found pending message {:x}", msgID);
                    info.OnSendComplete(ok);
                    // note: multiple logical messages may be connected with one message id
                }
            }
        }
    }
}

void NetworkServer::OnCreatePlayer(int player, bool botClient, const char* name)
{
    if (_state >= NGSTransferMission && !_missionHeader.joinInProgress)
    {
        // Game is loading or in progress and JIP is not enabled — reject late joiners
        LOG_INFO(Network, "Rejecting player {} — game in progress without JIP (state={})", name, (int)_state);
        _server->KickOff(player, NTRKicked);
        return;
    }
    if (_sessionLocked && !_missionHeader.joinInProgress)
    {
        // session is locked and JIP is not enabled
        _server->KickOff(player, NTRKicked);
        return;
    }

    // Check if player is joining mid-game (JIP)
    bool isJip = _state >= NGSPlay && _missionHeader.joinInProgress;

    if (botClient)
    {
        _botClient = player;
    }

    bool server = _dedicated && player == _botClient;
    if (server)
    {
        name = "__SERVER__";
    }

    NetworkPlayerInfo* pInfo = OnPlayerCreate(player, name);

    if (isJip)
    {
        pInfo->jip = true;
        // Set state to NGSCreate so player participates in broadcasts and state checks
        pInfo->state = NGSCreate;
        LOG_INFO(Network, "Player {} joining in progress (JIP)", (const char*)pInfo->name);
    }

    // send player int and name to himself
    if (_dedicated)
    {
        if (server)
        {
            LOG_INFO(Network, "Server identity created");
        }
        else
        {
            LOG_INFO(Network, "Player {} connecting", (const char*)pInfo->name);
        }
    }

    PlayerMessage msg(player, pInfo->name, server);
    SendMsg(player, &msg, NMFGuaranteed);

    // JIP: send current server state so client knows to show role selection
    // and can enter the fast-track flow. Without this, client's _serverState
    // stays at NGSCreating and it never reaches the NGSPlay JIP handling.
    if (isJip)
    {
        // Send mission info (header + params + roles) so client has
        // joinInProgress flag and can show role selection UI
        SendMissionInfo(player, false);

        // Send NGSPrepareRole so client shows the role selection UI
        ChangeGameState gsPrepare(NGSPrepareRole);
        SendMsg(player, &gsPrepare, NMFGuaranteed);
    }
}

bool NetworkServer::OnCreateVoicePlayer(int player)
{
    NetworkPlayerInfo* info = GetPlayerInfo(player);
    if (!info)
    {
        return false;
    }
    info->dvid = player;
    return true;
}

void NetworkServer::GetPlayers(AutoArray<NetPlayerInfo, MemAllocSA>& players)
{
    players.Resize(0);
    for (int i = 0; i < _players.Size(); i++)
    {
        int index = players.Add();
        players[index].dpid = _players[i].dpid;
        players[index].name = _players[i].name;
    }
}

void NetworkServer::GetConnectionSnapshots(AutoArray<NetworkConnectionSnapshot, MemAllocSA>& connections)
{
    connections.Resize(0);
    for (int i = 0; i < _players.Size(); i++)
    {
        const NetworkPlayerInfo& player = _players[i];
        NetworkConnectionSnapshot& snapshot = connections[connections.Add()];
        snapshot.dpid = player.dpid;
        snapshot.name = player.name;
        snapshot.state = player.state;
        snapshot.jip = player.jip;
        snapshot.botClient = IsDedicatedBotClient(player.dpid);
        // Per-connection latency/bandwidth bookkeeping is not yet wired; report
        // zeroes so the harness `connections` query still exposes the live
        // dpid/name/state/jip set the multiplayer tests assert on.
        snapshot.hasConnectionInfo = false;
        snapshot.latencyMs = 0;
        snapshot.throughputBps = 0;
        snapshot.avgPingMs = 0;
        snapshot.avgBandwidthBps = 0;
    }
}

void NetworkServer::GetPlayersOnChannel(AutoArray<int, MemAllocSA>& players, AutoArray<NetworkId> units, DWORD from,
                                        bool voice)
{
    players.Resize(0);
    for (int i = 0; i < _players.Size(); i++)
    {
        NetworkPlayerInfo& info = _players[i];
        if (info.dpid == from)
        {
            continue;
        }
        if (info.state < NGSCreate)
        {
            continue;
        }
        for (int j = 0; j < units.Size(); j++)
        {
            if (units[j] == info.person)
            {
                players.Add(info.dpid);
                break;
            }
        }
    }
}

void NetworkServer::GetPlayersOnChannel(AutoArray<int, MemAllocSA>& players, int channel, DWORD from, bool voice)
{
    players.Resize(0);
    switch (channel)
    {
        case CCNone:
            return;
        case CCDirect:
            // Direct voice: broadcast to all connected players (proximity
            // filtering happens client-side via 3D attenuation)
        case CCGlobal:
            for (int i = 0; i < _players.Size(); i++)
            {
                NetworkPlayerInfo& info = _players[i];
                if (info.dpid == from)
                {
                    continue;
                }
                if (info.state < NGSCreate)
                {
                    continue;
                }
                players.Add(info.dpid);
            }
            break;
        case CCSide:
        {
            TargetSide side = TSideUnknown;
            // find side
            for (int i = 0; i < _playerRoles.Size(); i++)
            {
                PlayerRole& role = _playerRoles[i];
                if (role.player == from)
                {
                    side = role.side;
                    break;
                }
            }
            if (side != TSideUnknown)
            {
                for (int i = 0; i < _playerRoles.Size(); i++)
                {
                    PlayerRole& role = _playerRoles[i];
                    if (role.player == from)
                    {
                        continue;
                    }
                    if (role.side != side)
                    {
                        continue;
                    }
                    NetworkPlayerInfo* info = GetPlayerInfo(role.player);
                    if (!info)
                    {
                        continue;
                    }
                    if (info->state < NGSCreate)
                    {
                        continue;
                    }
                    if (voice)
                    {
                        players.Add(Poseidon::SelectNetworkVoiceTargetPlayerId(info->dpid, info->dvid));
                    }
                    else
                    {
                        players.Add(info->dpid);
                    }
                }
            }
        }
        break;
        case CCGroup:
        {
            TargetSide side = TSideUnknown;
            int group = -1;
            // find group
            for (int i = 0; i < _playerRoles.Size(); i++)
            {
                PlayerRole& role = _playerRoles[i];
                if (role.player == from)
                {
                    side = role.side;
                    group = role.group;
                    break;
                }
            }
            // send message to all players with this group
            if (group >= 0)
            {
                for (int i = 0; i < _playerRoles.Size(); i++)
                {
                    PlayerRole& role = _playerRoles[i];
                    if (role.player == from)
                    {
                        continue;
                    }
                    if (role.side != side || role.group != group)
                    {
                        continue;
                    }
                    NetworkPlayerInfo* info = GetPlayerInfo(role.player);
                    if (!info)
                    {
                        continue;
                    }
                    if (info->state < NGSCreate)
                    {
                        continue;
                    }
                    if (voice)
                    {
                        players.Add(Poseidon::SelectNetworkVoiceTargetPlayerId(info->dpid, info->dvid));
                    }
                    else
                    {
                        players.Add(info->dpid);
                    }
                }
            }
        }
        break;
        case CCVehicle:
        {
            TargetSide side = TSideUnknown;
            int group = -1;
            int unit = -1;
            // find unit
            for (int i = 0; i < _playerRoles.Size(); i++)
            {
                PlayerRole& role = _playerRoles[i];
                if (role.player == from)
                {
                    side = role.side;
                    group = role.group;
                    unit = role.unit;
                    break;
                }
            }
            // send message to all players with this group
            if (group >= 0)
            {
                for (int i = 0; i < _playerRoles.Size(); i++)
                {
                    PlayerRole& role = _playerRoles[i];
                    if (role.player == from)
                    {
                        continue;
                    }
                    if (role.side != side || role.group != group || role.unit != unit)
                    {
                        continue;
                    }
                    NetworkPlayerInfo* info = GetPlayerInfo(role.player);
                    if (!info)
                    {
                        continue;
                    }
                    if (info->state < NGSCreate)
                    {
                        continue;
                    }
                    if (voice)
                    {
                        players.Add(Poseidon::SelectNetworkVoiceTargetPlayerId(info->dpid, info->dvid));
                    }
                    else
                    {
                        players.Add(info->dpid);
                    }
                }
            }
        }
        break;
    }
}

void NetworkServer::CreateIdentity(PlayerIdentity& ident, Ref<SquadIdentity> squad)
{
    // check if player is still connected
    if (!GetPlayerInfo(ident.dpnid))
    {
        // player already disconnected
        return;
    }

    int index = _identities.Add(ident);
    PlayerIdentity& identity = _identities[index];
    identity.playerid = _nextPlayerId++;
    if (identity.dpnid == _botClient)
    {
        identity._rights |= PRServer;
    }

#if LOG_PLAYERS
    RptF("Server: Identity added - name %s, id %d (total %d identities, %d players)", (const char*)ident.name,
         ident.dpnid, _identities.Size(), _players.Size());
#endif

    if (squad)
    {
        // add squad and send it to all players except new (do not send twice)
        _squads.Add(squad);
        RString src, dst;
        if (squad->picture.GetLength() > 0)
        {
            src = Poseidon::BuildNetworkServerSquadPictureUploadPath(GetServerTmpDir(), squad->nick, squad->picture);
            if (QIFStream::FileExists(src))
            {
                dst = Poseidon::BuildNetworkSquadPictureTmpPath(squad->nick, squad->picture);
            }
            else
            {
                src = "";
            }
        }
        for (int i = 0; i < _identities.Size(); i++)
        {
            if (i != index)
            {
                if (src.GetLength() > 0)
                {
                    TransferFile(_identities[i].dpnid, dst, src);
                }
                SendMsg(_identities[i].dpnid, squad, NMFGuaranteed);
            }
        }
        if (_dedicated)
        {
            NetworkClient* client = _parent->GetClient();
            NET_ERROR(client && client->GetPlayer() != identity.dpnid);
            // send new identity to bot client
            if (src.GetLength() > 0)
            {
                TransferFile(client->GetPlayer(), dst, src);
            }
            SendMsg(client->GetPlayer(), squad, NMFGuaranteed);
        }
    }
    NetworkPlayerInfo* info = GetPlayerInfo(identity.dpnid);
    if (!info)
    {
        info = OnPlayerCreate(identity.dpnid, identity.name);
    }
    identity.name = info->name;
    for (int i = 0; i < _squads.Size(); i++)
    {
        SquadIdentity* squad = _squads[i];
        if (squad->picture.GetLength() > 0)
        {
            RString src =
                Poseidon::BuildNetworkServerSquadPictureUploadPath(GetServerTmpDir(), squad->nick, squad->picture);
            if (QIFStream::FileExists(src))
            {
                RString dst = Poseidon::BuildNetworkSquadPictureTmpPath(squad->nick, squad->picture);
                TransferFile(identity.dpnid, dst, src);
            }
        }
        SendMsg(identity.dpnid, squad, NMFGuaranteed);
    }

    bool hasCustomFace = stricmp(identity.face, "custom") == 0;

    for (int i = 0; i < _identities.Size(); i++)
    {
        PlayerIdentity& player = _identities[i];
        if (i != index)
        {
            // send all faces and sounds to new player
            //						if (notBotClient)
            {
                if (stricmp(player.face, "custom") == 0)
                {
                    TransferFace(identity.dpnid, player.dpnid);
                }
                TransferCustomRadio(identity.dpnid, player.dpnid);
            }
            // send new face and sound to all players
            //						if (!client || client->GetPlayer() != player.dpnid)
            {
                if (hasCustomFace)
                {
                    TransferFace(player.dpnid, identity.dpnid);
                }
                TransferCustomRadio(player.dpnid, identity.dpnid);
            }
            // send all identities to new player
            NetworkPlayerInfo* pInfo = GetPlayerInfo(player.dpnid);
            if (pInfo)
            {
                player.state = pInfo->state;
            }
            SendMsg(identity.dpnid, &player, NMFGuaranteed);
        }
        // send new identity to all players
        SendMsg(player.dpnid, &identity, NMFGuaranteed);
    }
    if (_dedicated)
    {
        NetworkClient* client = _parent->GetClient();
        NET_ERROR(client && client->GetPlayer() != identity.dpnid);
        // send new identity to bot client
        SendMsg(client->GetPlayer(), &identity, NMFGuaranteed);
    }
    info->state = NGSLogin;

    // all message formats registered, we may send messages
    // send state dependent info
    if (info->jip && _state >= NGSPlay)
    {
        // JIP player: don't send the current NGSPlay state — it would cause
        // the client UI to show "Game in progress" dead-end. Instead, send
        // NGSPrepareRole so the client shows role selection and enters the
        // JIP fast-track flow. Mission info was already sent in OnCreatePlayer.
        ChangeGameState gs(NGSPrepareRole);
        SendMsg(info->dpid, &gs, NMFGuaranteed);
        info->state = NGSPrepareRole;
        info->missionFileValid = false;
    }
    else
    {
        if (_state >= NGSPrepareSide)
        {
            // send mission info
            SendMissionInfo(info->dpid);
            info->state = NGSPrepareSide;
            info->missionFileValid = false;
        }
        // Send state to client. Cap at NGSPrepareRole for clients joining
        // during loading/play — sending NGSPlay would skip role selection UI
        // and the client would never detect occupied slots.
        NetworkGameState stateToSend = _state;
        if (stateToSend > NGSPrepareRole)
        {
            stateToSend = NGSPrepareRole;
        }
        ChangeGameState gs(stateToSend);
        SendMsg(info->dpid, &gs, NMFGuaranteed);

        // if game is already in progress, send how long is played already
        if (_state == NGSPlay)
        {
            NetworkCommandMessage msg;
            msg.type = NCMTMissionTimeElapsed;
            int timeElapsed = GlobalTickCount() - _missionHeader.start;
            msg.content.Write(&timeElapsed, sizeof(timeElapsed));
            SendMsg(info->dpid, &msg, NMFGuaranteed);
        }
    }

    identity.state = info->state;

    // send player state to all clients
    SetPlayerState(info->dpid, info->state);

    identity._minPing = 10;
    identity._avgPing = 100;
    identity._maxPing = 1000;

    identity._minBandwidth = 14;
    identity._avgBandwidth = 28;
    identity._maxBandwidth = 33;

    // Send chat message about player is connected
    {
        char message[256];
        snprintf(message, sizeof(message), LocalizeString(IDS_MP_CONNECT), (const char*)identity.name);
        RefArray<NetworkObject> dummy;
        GNetworkManager.Chat(CCGlobal, "", dummy, message);
        GChatList.Add(CCGlobal, nullptr, message, false, true);
        DWORD updatePingTime = GlobalTickCount() + 5000;
        if (_pingUpdateNext > updatePingTime)
        {
            _pingUpdateNext = updatePingTime;
        }
        if (_dedicated)
        {
            LOG_INFO(Network, "Player {} connected (id={})", (const char*)identity.name, (const char*)identity.id);
        }
    }

    // send basic integrity questions
    // do not send integrity question to myself or to botclient
    if (info->dpid != _player && info->dpid != _botClient)
    {
        // if player is using different version, it is impossible to check integrity
        int ver = identity.version;
        // temporary: enable 1.47 server checking config of 146 client
        if (ver != MP_VERSION_ACTUAL && (ver != 146 || MP_VERSION_ACTUAL < 147 || MP_VERSION_ACTUAL > 149))
        {
            // never check
            info->integrityCheckNext = UINT_MAX;
            char buf[256];
            if (ver)
            {
                snprintf(buf, sizeof(buf), "%d.%02d", ver / 100, ver % 100);
            }
            else
            {
                snprintf(buf, sizeof(buf), "%s", (const char*)"1.40..1.42");
            }
            OnIntegrityCheckFailed(info->dpid, IQTConfig, buf, true);
        }
        else
        {
            PerformInitialIntegrityCheck(*info);
            info->integrityCheckNext = GlobalTickCount() + 1000 * toInt(20 + GRandGen.RandomValue() * 30);
        }
    }

    if (_state == NGSCreate && _mission.GetLength() == 1 && _mission[0] == '?')
    {
        NetworkCommandMessage answer;
        answer.type = NCMTVoteMission;
        AddMissionList(answer);
        SendMsg(identity.dpnid, &answer, NMFGuaranteed);
    }
}

void NetworkServer::SetPlayerState(int dpid, NetworkGameState state)
{
    PlayerStateMessage ps;
    ps.player = dpid;
    ps.state = state;
    for (int i = 0; i < _players.Size(); i++)
    {
        SendMsg(_players[i].dpid, &ps, NMFGuaranteed);
    }
}

static bool CheckValidUpload(RString path, int player)
{
    return Poseidon::IsSafeNetworkServerPlayerUploadPath(path, GetServerTmpDir(), player);
}

static RString GetRelUploadPath(RString path, int player)
{
    RString prefixShouldBe = Poseidon::BuildNetworkServerPlayerUploadDir(GetServerTmpDir(), player);
    if (prefixShouldBe.GetLength() == 0 || strnicmp(path, prefixShouldBe, prefixShouldBe.GetLength()))
    {
        return path;
    }
    return path.Substring(prefixShouldBe.GetLength(), INT_MAX);
}

int NetworkPlayerInfo::FindIntegrityQuestion(IntegrityQuestionType type, const IntegrityQuestion& q) const
{
    for (int i = 0; i < integrityQuestions.Size(); i++)
    {
        const IntegrityQuestionInfo& qi = integrityQuestions[i];
        if (qi.type == type && qi.q.name == q.name && qi.q.offset == q.offset && qi.q.size == q.size)
        {
            return i;
        }
    }
    return -1;
}

int NetworkPlayerInfo::FindIntegrityQuestion(IntegrityQuestionType type) const
{
    for (int i = 0; i < integrityQuestions.Size(); i++)
    {
        const IntegrityQuestionInfo& qi = integrityQuestions[i];
        if (qi.type == type)
        {
            return i;
        }
    }
    return -1;
}

int NetworkPlayerInfo::FindIntegrityQuestion(int id) const
{
    for (int i = 0; i < integrityQuestions.Size(); i++)
    {
        const IntegrityQuestionInfo& qi = integrityQuestions[i];
        if (qi.id == id)
        {
            return i;
        }
    }
    return -1;
}
