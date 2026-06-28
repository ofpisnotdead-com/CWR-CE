#include <Poseidon/Core/Application.hpp>

using namespace Poseidon;
#include <Poseidon/Network/NetworkServerCommon.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Core/ModSystem.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <Poseidon/Core/Config/Config.hpp>
#include <Poseidon/Network/NetworkImpl.hpp>
#include <Poseidon/Core/Global.hpp>
// #include "strIncl.hpp"
#include <Poseidon/UI/Locale/StringtableExt.hpp>

#include <Poseidon/Network/NetworkConfig.hpp>
#include <Poseidon/Network/NetworkMissionTransfer.hpp>
#include <Poseidon/Network/NetworkServerAuth.hpp>
#include <Poseidon/Network/NetworkServerDispatch.hpp>
#include <Poseidon/Network/NetworkScriptValueCodec.hpp>
#include <Poseidon/Network/WireBounds.hpp>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/Memtype.h>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>
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
#endif

namespace Poseidon
{
void RunMissionScript(char const*, class ::GameValue);
}

#define LOG_SEND_PROCESS 0
#define LOG_PLAYERS 1
extern int MaxCustomFileSize;

static bool CheckValidUpload(RString path, RString name)
{
    RString prefixShouldBe = GetServerTmpDir() + RString("/players/") + name + RString("/");
    if (strnicmp(path, prefixShouldBe, prefixShouldBe.GetLength()))
    {
        return false;
    }
    // A prefix match alone does not confine the path; reject any parent-directory escape.
    return !Poseidon::PathHasParentEscape(path);
}

static RString GetRelUploadPath(RString path, RString name)
{
    RString prefixShouldBe = GetServerTmpDir() + RString("/players/") + name + RString("/");
    if (strnicmp(path, prefixShouldBe, prefixShouldBe.GetLength()))
    {
        return path;
    }
    return path.Substring(prefixShouldBe.GetLength(), INT_MAX);
}

static NetworkObject* ResolveServerNetworkObject(void* context, const NetworkId& id)
{
    NetworkServer* server = static_cast<NetworkServer*>(context);
    NetworkId mutableId = id;
    return server ? server->GetObject(mutableId) : nullptr;
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

static bool RemoteExecCanRunServer(int from, int gameMaster)
{
    return Poseidon::RemoteExecServerAuthorized(from, gameMaster, AI_PLAYER);
}

static bool PublicExecCanRunClients(int from, int gameMaster, int botClient)
{
    return from == botClient || (gameMaster != AI_PLAYER && from == gameMaster);
}

void NetworkServer::OnMessage(int from, NetworkMessage* msg, NetworkMessageType type)
{
    NetworkMessageFormatBase* format = GetFormat(/*from, */ type);
    if (!format)
    {
        RptF("Server: Bad message %d(%s) format", (int)type, (const char*)NetworkMessageTypeNames[type]);
        return;
    }
    NetworkMessageContext ctx(msg, format, this, from, MSG_RECEIVE);

#if _ENABLE_CHEATS
    int level = GetDiagLevel(type, from != _parent->GetClient()->GetPlayer());
    if (level >= 2)
    {
        DiagLogF("Server: received message %s from %d", NetworkMessageTypeNames[type], from);
        ctx.LogMessage(level, "\t");
    }
#endif

    // Table-driven dispatch (strangler-fig migration). Every message is first
    // authorized centrally against its MsgDescriptor; types with a registered
    // handler are then dispatched through the seam, everything else falls through
    // to the switch below unchanged.
    if (const MsgDescriptor* d = LookupMsgDescriptor(type))
    {
        if (!Authorize(*d, from))
        {
            LOG_WARN(Network, "Server: rejected {} from {} (server-origin message sent by a client)",
                     NetworkMessageTypeNames[type], from);
            return;
        }
        if (d->handle)
        {
            ServerMsgCtx sctx{from, msg, type, *this, ctx};
            d->handle(sctx);
            return;
        }
    }

    int index1 = -1, index2 = -1;

    switch (type)
    {
        case NMTIntegrityAnswer:
        {
            IntegrityAnswerMessage answer;
            answer.TransferMsg(ctx);
            NetworkPlayerInfo* info = GetPlayerInfo(from);
            if (info)
            {
                int qId = answer.id;
                IntegrityQuestionType qType = answer.type;
                //  it should be an answer to the first question of given type

                int qIndex = -1;
                if (qId > 0)
                {
                    qIndex = info->FindIntegrityQuestion(qId);
                }
                else
                {
                    qIndex = info->FindIntegrityQuestion(qType);
                }

                if (qIndex >= 0)
                {
                    const IntegrityQuestion& q = info->integrityQuestions[qIndex].q;
                    // LogF
                    //(
                    //	"%s - %s: Received answer %d",
                    //	(const char *)info->name,(const char *)q.name,answer.answer
                    //);
                    bool dedicated = IsDedicatedServer();
                    unsigned int myAnswer = IntegrityCheckAnswer(qType, q, dedicated);
                    // integrity question may be a part of active investigation
                    if (qType != IQTExe && myAnswer != answer.answer)
                    {
                        LOG_DEBUG(Network, "{} - {}: Received answer {:x}, expected {:x}", (const char*)info->name,
                                  (const char*)q.name, answer.answer, myAnswer);
                    }
                    bool wasIA = IntegrityAnswerReceived(info, qType, q, myAnswer == answer.answer);
                    if (!wasIA && myAnswer != answer.answer)
                    {
                        // calculate correct answer
                        // try to provide more info if possible
                        if (qType == IQTConfig)
                        {
                            OnIntegrityCheckFailed(from, (IntegrityQuestionType)qType, q.name, false);
                            // find where integrity failed - ask more detailed questions
                            info->integrityInvestigation[qType] = new IntegrityInvestigationConfig(q.name);
                        }
                        else if (qType == IQTExe)
                        {
                            // Original exe integrity kickoff disabled — not applicable to CWR.
                        }
                        else
                        {
                            OnIntegrityCheckFailed(from, (IntegrityQuestionType)qType, q.name, true);
                        }
                        // we discovered one modification
                        // report it and stop checking
                        // otherwise log would be full of "modified xxxxxx" messages
                    }
                    info->integrityQuestions.Delete(qIndex);
                }
                else
                {
                    LOG_DEBUG(Network, "Received unknown answer {:08x}", answer.id);
                }
            }
        }
        break;
        case NMTSelectPlayer:
        {
            SelectPlayerMessage pl;
            pl.TransferMsg(ctx);
            NET_ERROR(pl.player != AI_PLAYER);
            NetworkPlayerInfo* info = GetPlayerInfo(pl.player);
            NET_ERROR(info);
            if (!info)
            {
                break;
            }
            // A client may only select its own slot; only an admin/bot may assign
            // another player and change object ownership on its behalf, so the
            // ChangeOwner below is gated to the acting slot.
            if (!Poseidon::SelectPlayerAuthorized(from, pl.player, _gameMaster, _botClient))
            {
                LOG_WARN(Network, "SelectPlayer: rejected from={} acting for player={}", from, pl.player);
                break;
            }
            info->person = pl.person;
            info->cameraPosition = pl.position;
            info->cameraPositionTime = msg->time;
            // fulfill info->unit, info->group
            NetworkId brain = PersonToUnit(pl.person);
            info->unit = brain;

            if (from != pl.player)
            {
                NetworkComponent::SendMsg(pl.player, msg, type, NMFGuaranteed);
            }
            NetworkObjectInfo* oInfo = GetObjectInfo(pl.person);
            NET_ERROR(oInfo);
            if (!oInfo)
            {
                break;
            }
            ChangeOwner(pl.person, oInfo->owner, pl.player);
            if (!brain.IsNull())
            {
                NetworkObjectInfo* oInfo = GetObjectInfo(brain);
                NET_ERROR(oInfo);
                if (!oInfo)
                {
                    break;
                }
                ChangeOwner(brain, oInfo->owner, pl.player);
            }
        }
        break;
        case NMTAttachPerson:
        {
            NET_ERROR(dynamic_cast<const IndicesAttachPerson*>(ctx.GetIndices()))
            const IndicesAttachPerson* indices = static_cast<const IndicesAttachPerson*>(ctx.GetIndices());

            NetworkId person;
            if (ctx.IdxGetId(indices->person, person) != TMOK)
            {
                break;
            }
            NetworkId unit;
            if (ctx.IdxGetId(indices->unit, unit) != TMOK)
            {
                break;
            }
            if (unit.IsNull())
            {
                break;
            }
            for (int i = 0; i < _mapPersonUnit.Size(); i++)
            {
                PersonUnitPair& pair = _mapPersonUnit[i];
                if (pair.unit == unit)
                {
                    pair.person = person;
                    break;
                }
            }
        }
        break;
        case NMTAskMissionFile:
        {
            AskMissionFileMessage ask;
            ask.TransferMsg(ctx);
            if (ask.valid)
            {
                NetworkPlayerInfo* info = GetPlayerInfo(from);
                if (info)
                {
                    info->missionFileValid = true;
                }
            }
        }
        break;
        case NMTGameState:
            OnGameStateMessage(from, msg, type, ctx);
            break;
        case NMTLogin:
        {
            PlayerIdentity identity;
            identity.TransferMsg(ctx);

            __int64 id64 = _atoi64(identity.id);
            // check if not in ban lists
            if (from != _botClient)
            {
                // reload ban list
                LoadBanList("ban.txt", _banListGlobal);

                bool ban = false;
                for (int i = 0; i < _banListGlobal.Size(); i++)
                {
                    if (id64 == _banListGlobal[i])
                    {
                        ban = true;
                        break;
                    }
                }
                if (!ban)
                {
                    for (int i = 0; i < _banListLocal.Size(); i++)
                    {
                        if (id64 == _banListLocal[i])
                        {
                            ban = true;
                            break;
                        }
                    }
                }
                // IP bans (ipban.txt) — checked alongside the id lists so a player
                // banned by either key is rejected, independent of banMode.
                if (!ban && _server)
                {
                    const RString ipStr = _server->GetPlayerHostIP(from);
                    uint32_t ip = 0;
                    if (ipStr.GetLength() > 0 && Poseidon::ParseIPv4(static_cast<const char*>(ipStr), ip))
                    {
                        Poseidon::LoadIpBanList("ipban.txt", _banListIPGlobal);
                        for (int i = 0; i < _banListIPGlobal.Size() && !ban; i++)
                        {
                            if (ip == _banListIPGlobal[i])
                                ban = true;
                        }
                        for (int i = 0; i < _banListIPLocal.Size() && !ban; i++)
                        {
                            if (ip == _banListIPLocal[i])
                                ban = true;
                        }
                    }
                }
                if (ban)
                {
                    KickOff(from, KORBan);
                    break;
                }
            }

            // check if same squad id is already used
            // only in SUPER_RELEASE,
            // so that in RELEASE we may test server/client on single computer
            if (id64 != 0 && id64 != 999)
            {
                // check only when id!=0 and id!=999
                // for id==0 we have better handling
                // id==999 is used in Multiplayer Demo
                bool validateId = true;
                for (int i = 0; i < _identities.Size(); i++)
                {
                    if (_identities[i].id == identity.id)
                    {
                        validateId = false;
                    }
                }
                if (!validateId)
                {
                    // report problems on global channel
                    RString string = LocalizeString(IDS_FADE_AWAY);
                    RString senderName = LocalizeString(IDS_FADE_REMMEMBER);
                    RefArray<NetworkObject> dummy;
                    GNetworkManager.Chat(CCGlobal, senderName, dummy, string);
                    GChatList.Add(CCGlobal, senderName, string, false, true);

                    if (_kickDuplicate)
                    {
                        KickOff(identity.dpnid, KORKick);
                    }
                }
            }

            bool isSquad = identity.squadId.GetLength() > 0;
            if (isSquad)
            {
                Ref<SquadIdentity> squad;
                for (int i = 0; i < _squads.Size(); i++)
                {
                    if (_squads[i]->id == identity.squadId)
                    {
                        squad = _squads[i];
                        break;
                    }
                }

                bool newSquad = false;
                if (!squad)
                {
                    squad = new SquadIdentity();
                    squad->id = identity.squadId;
                    newSquad = true;
                }

                // create object
                _squadChecks.Add(new CheckSquadObject(identity, squad, newSquad, _proxy));

#if LOG_PLAYERS
                RptF("Server: Identity check started - name %s, id %d (total %d identities, %d players)",
                     (const char*)identity.name, identity.dpnid, _identities.Size(), _players.Size());
#endif
            }
            else
            {
                CreateIdentity(identity, nullptr);
            }
        }
        break;
        case NMTPlayerRole:
            OnMessagePlayerRole(from, type, ctx);
            break;
        case NMTAskForAnimationPhase:
        case NMTAskForDammage:
        case NMTAskForSetDammage:
        case NMTAskForGetIn:
        case NMTAskForGetOut:
        case NMTAskForChangePosition:
        case NMTAskForAimWeapon:
        case NMTAskForAimObserver:
        case NMTAskForSelectWeapon:
        case NMTAskForAmmo:
        case NMTAskForAddImpulse:
        case NMTAskForMoveVector:
        case NMTAskForMoveMatrix:
        case NMTAskForJoinGroup:
        case NMTAskForJoinUnits:
        case NMTAskForHideBody:
        case NMTUpdateWeapons:
        case NMTShowTarget:
        case NMTShowGroupDir:
        case NMTAskForCreateUnit:
        case NMTAskForDeleteVehicle:
        case NMTAskForGroupRespawn:
        case NMTAskForActivateMine:
        case NMTAskForInflameFire:
        case NMTMsgVTarget:
        case NMTMsgVFire:
        case NMTMsgVMove:
        case NMTMsgVFormation:
        case NMTMsgVSimpleCommand:
        case NMTMsgVLoad:
        case NMTMsgVAzimut:
        case NMTMsgVStopTurning:
        case NMTMsgVFireFailed:
        case NMTGroupRespawnDone:
        case NMTAskForReceiveUnitAnswer:
            OnAskForMessage(from, msg, type, ctx);
            break;
        case NMTMarkerDelete:
            // send to all except from (state Briefing expected)
            for (int i = 0; i < _players.Size(); i++)
            {
                NetworkPlayerInfo& info = _players[i];
                if (!Poseidon::RelayEligible(info.dpid, info.state, from, NGSBriefing))
                {
                    continue;
                }
                NetworkComponent::SendMsg(info.dpid, msg, type, NMFGuaranteed);
            }
            break;
        case NMTPlaySound:
        case NMTFireWeapon:
        case NMTAddWeaponCargo:
        case NMTRemoveWeaponCargo:
        case NMTAddMagazineCargo:
        case NMTRemoveMagazineCargo:
        case NMTExplosionDammageEffects:
        case NMTSoundState:
        case NMTVehicleDestroyed:
        case NMTGroupSynchronization:
        case NMTDetectorActivation:
        case NMTCopyUnitInfo:
        case NMTVehicleDamaged:
        case NMTIncomingMissile:
            // send to all except from (state Play expected)
            for (int i = 0; i < _players.Size(); i++)
            {
                NetworkPlayerInfo& info = _players[i];
                if (!Poseidon::RelayEligible(info.dpid, info.state, from, NGSPlay))
                {
                    continue;
                }
                NetworkComponent::SendMsg(info.dpid, msg, type, NMFGuaranteed);
            }
            break;
        case NMTTransferFile:
        case NMTTransferMissionFile:
        case NMTVehicleInit:
        case NMTPublicVariable:
            // send to all except from (state Create expected)
            for (int i = 0; i < _players.Size(); i++)
            {
                NetworkPlayerInfo& info = _players[i];
                if (!Poseidon::RelayEligible(info.dpid, info.state, from, NGSLoadIsland))
                {
                    continue;
                }
                NetworkComponent::SendMsg(info.dpid, msg, type, NMFGuaranteed);
            }
            // JIP: store publicVariable for late-joining players (deduplicate by name)
            if (type == NMTPublicVariable && _missionHeader.joinInProgress)
            {
                // Extract variable name to deduplicate
                RString varName;
                {
                    NetworkMessageFormatBase* pvFormat = GetFormat(type);
                    if (pvFormat)
                    {
                        NetworkMessageContext pvCtx(msg, pvFormat, this, from, MSG_RECEIVE);
                        PublicVariableMessage pvMsg;
                        if (pvMsg.TransferMsg(pvCtx) == TMOK)
                            varName = pvMsg._name;
                    }
                }

                // Replace existing entry for same variable, or append
                bool replaced = false;
                if (varName.GetLength() > 0)
                {
                    for (int j = 0; j < _jipMessages.Size(); j++)
                    {
                        if (_jipMessages[j].type != NMTPublicVariable)
                            continue;
                        if (_jipMessages[j].key == varName)
                        {
                            _jipMessages[j].msg = msg;
                            replaced = true;
                            break;
                        }
                    }
                }
                if (!replaced)
                {
                    static const int kMaxJIPMessages = 10000;
                    if (_jipMessages.Size() < kMaxJIPMessages)
                    {
                        JIPInitMessage jipMsg;
                        jipMsg.type = type;
                        jipMsg.msg = msg;
                        jipMsg.key = varName;
                        _jipMessages.Add(jipMsg);
                    }
                }
            }
            break;
        case NMTPublicExec:
            if (!PublicExecCanRunClients(from, _gameMaster, _botClient))
            {
                LOG_WARN(Network, "publicExec blocked from unprivileged sender {}", from);
                break;
            }
            for (int i = 0; i < _players.Size(); i++)
            {
                NetworkPlayerInfo& info = _players[i];
                if (!Poseidon::RelayEligible(info.dpid, info.state, from, NGSLoadIsland))
                {
                    continue;
                }
                NetworkComponent::SendMsg(info.dpid, msg, type, NMFGuaranteed);
            }
            break;
        case NMTRemoteExec:
        {
            RemoteExecMessage params;
            params.TransferMsg(ctx);

            if (params._remove)
            {
                if (!PublicExecCanRunClients(from, _gameMaster, _botClient))
                {
                    LOG_WARN(Network, "remoteExecRemove '{}' blocked from unprivileged sender {}",
                             (const char*)params._jipKey, from);
                    break;
                }
                for (int i = 0; i < _jipMessages.Size();)
                {
                    if (_jipMessages[i].type == NMTRemoteExec && _jipMessages[i].key == params._jipKey)
                    {
                        _jipMessages.Delete(i);
                        continue;
                    }
                    ++i;
                }
                break;
            }

            if (!WireBounds::ValidIdentifier(params._name, 256))
            {
                LOG_WARN(Network, "remoteExec blocked invalid identifier from sender {}", from);
                break;
            }

            RemoteExecTargetSelector targetSelector;
            if (params._targetSpec.Size() > 0)
            {
                if (!DecodeRemoteExecTargetSelector(targetSelector, params._targetSpec))
                {
                    LOG_WARN(Network, "remoteExec '{}' blocked malformed target selector from sender {}",
                             (const char*)params._name, from);
                    break;
                }
            }
            else
            {
                targetSelector.kind = RemoteExecTargetKind::Scalar;
                targetSelector.scalar = params._target;
            }

            int target = params._target;
            if (targetSelector.kind == RemoteExecTargetKind::Scalar)
            {
                target = targetSelector.scalar;
            }
            bool executeOnServer = target == 0 || target == 2;
            bool selectorMayResolveServer = targetSelector.kind != RemoteExecTargetKind::Scalar;
            bool sendToClients =
                targetSelector.kind != RemoteExecTargetKind::Scalar || target == 0 || target == -2 || target != 2;
            bool canRunByPolicy = RemoteExecClientAuthorized(
                from, _gameMaster, _botClient, AI_PLAYER, _remoteExecPolicyMode, _remoteExecAllowedNames, params._name);
            bool canRunServer = !(executeOnServer || selectorMayResolveServer) ||
                                (RemoteExecCanRunServer(from, _gameMaster) || canRunByPolicy);
            bool canRunClients = !sendToClients || canRunByPolicy;
            bool acceptedForJip = false;

            if (executeOnServer)
            {
                if (canRunServer)
                {
                    GameValue payload = Poseidon::DecodeScriptValue(params._params, ResolveServerNetworkObject, this);
                    if (!payload.GetNil() && GWorld && GWorld->GetGameState())
                    {
                        ExecuteNamedRemoteExec(GWorld->GetGameState(), params._name, payload);
                    }
                    acceptedForJip = true;
                }
                else
                {
                    LOG_WARN(Network,
                             "remoteExec '{}' server target {} blocked from sender {} botClient {} originator {} "
                             "policyMode {} allowedNames {}",
                             (const char*)params._name, target, from, _botClient, params._originator,
                             (int)_remoteExecPolicyMode, _remoteExecAllowedNames.Size());
                }
            }

            if (sendToClients)
            {
                if (!canRunClients)
                {
                    LOG_WARN(Network,
                             "remoteExec '{}' client target {} blocked from sender {} botClient {} originator {} "
                             "policyMode {} allowedNames {}",
                             (const char*)params._name, target, from, _botClient, params._originator,
                             (int)_remoteExecPolicyMode, _remoteExecAllowedNames.Size());
                }
                else
                {
                    NetworkRemoteExecDispatchResult dispatch;
                    if (targetSelector.kind == RemoteExecTargetKind::Scalar)
                    {
                        dispatch = DispatchNetworkRemoteExecTarget(
                            target, _players.Size(), [this](int index) { return _players[index].dpid; },
                            [this](int index) { return _players[index].state; }, NGSLoadIsland,
                            [this, msg, type](int dpid) { NetworkComponent::SendMsg(dpid, msg, type, NMFGuaranteed); },
                            []() {});
                        acceptedForJip = acceptedForJip || dispatch.acceptedClientTarget;
                    }
                    else
                    {
                        dispatch = DispatchNetworkRemoteExecTargetSelector(
                            targetSelector, _players.Size(), [this](int index) { return _players[index].dpid; },
                            [this](int index) { return _players[index].state; }, NGSLoadIsland,
                            [this, msg, type](int dpid) { NetworkComponent::SendMsg(dpid, msg, type, NMFGuaranteed); },
                            [this, &params, canRunServer, from]()
                            {
                                if (!canRunServer)
                                {
                                    LOG_WARN(Network, "remoteExec '{}' server-owned target blocked from sender {}",
                                             (const char*)params._name, from);
                                    return;
                                }
                                GameValue payload =
                                    Poseidon::DecodeScriptValue(params._params, ResolveServerNetworkObject, this);
                                if (!payload.GetNil() && GWorld && GWorld->GetGameState())
                                {
                                    ExecuteNamedRemoteExec(GWorld->GetGameState(), params._name, payload);
                                }
                            },
                            [this](const NetworkId& id)
                            {
                                if (id.IsNull())
                                {
                                    return 0;
                                }
                                NetworkId mutableId = id;
                                NetworkObjectInfo* info = GetObjectInfo(mutableId);
                                return info ? info->owner : 0;
                            });
                        acceptedForJip = acceptedForJip || dispatch.acceptedClientTarget ||
                                         (dispatch.executedOnServer && canRunServer);
                    }
                }
            }

            // JIP: store for late-joining players if requested
            if (acceptedForJip && params._jip && _missionHeader.joinInProgress)
            {
                static const int kMaxJIPMessages = 10000;
                bool replaced = false;
                if (params._jipKey.GetLength() > 0)
                {
                    for (int i = 0; i < _jipMessages.Size(); ++i)
                    {
                        if (_jipMessages[i].type == NMTRemoteExec && _jipMessages[i].key == params._jipKey)
                        {
                            _jipMessages[i].msg = msg;
                            replaced = true;
                            break;
                        }
                    }
                }
                if (!replaced && _jipMessages.Size() < kMaxJIPMessages)
                {
                    JIPInitMessage jipMsg;
                    jipMsg.type = type;
                    jipMsg.msg = msg;
                    jipMsg.key = params._jipKey;
                    _jipMessages.Add(jipMsg);
                }
            }
        }
        break;
        case NMTTransferFileToServer:
        {
            TransferFileMessage transfer;
            transfer.TransferMsg(ctx);
            // check if path is valid upload path
            // if not, kick sender
            PlayerIdentity* id = FindIdentity(from);
            bool alreadyKicked = false;
            if (id && id->destroy)
            {
                alreadyKicked = true;
            }

            RString name;
            for (int i = 0; i < _players.Size(); i++)
            {
                if (_players[i].dpid == from)
                {
                    name = _players[i].name;
                    if (_players[i].kickedOff)
                    {
                        alreadyKicked = true;
                    }
                    break;
                }
            }

            if (alreadyKicked)
            {
                LOG_DEBUG(Network, "Transfer file {} request ignored, sender is being kicked off",
                          (const char*)transfer.path);
                break;
            }
            // check if file transferred is within allowed range
            // if not, kick off sender
            if (transfer.totSize > MaxCustomFileSize)
            {
                // check name of player (from file name)
                ServerMessage(Format("Player %s kicked off - too big custom file '%s' (%d B > %d B)", (const char*)name,
                                     (const char*)GetRelUploadPath(transfer.path, name), transfer.totSize,
                                     MaxCustomFileSize));
                KickOff(from, KORKick);
                break;
            }

            if (!CheckValidUpload(transfer.path, name))
            {
                ServerMessage(Format("Player %s kicked off - invalid custom file '%s'", (const char*)name,
                                     (const char*)transfer.path));
                KickOff(from, KORKick);
                break;
            }

            ReceiveFileSegment(transfer);
        }
        break;
        case NMTSetFlagOwner:
        {
            NET_ERROR(dynamic_cast<const IndicesSetFlagOwner*>(ctx.GetIndices()))
            const IndicesSetFlagOwner* indices = static_cast<const IndicesSetFlagOwner*>(ctx.GetIndices());

            NetworkId id;
            if (ctx.IdxGetId(indices->carrier, id) == TMOK)
            {
                NetworkObjectInfo* info = GetObjectInfo(id);
                if (info)
                {
                    NetworkComponent::SendMsg(info->owner, msg, type, NMFGuaranteed);
                }
                else
                {
                    RptF("Server: Object %d:%d not found (message %s)", id.creator, id.id,
                         NetworkMessageTypeNames[type]);
                }
            }
        }
        break;
        case NMTSetFlagCarrier:
        {
            NET_ERROR(dynamic_cast<const IndicesSetFlagOwner*>(ctx.GetIndices()))
            const IndicesSetFlagOwner* indices = static_cast<const IndicesSetFlagOwner*>(ctx.GetIndices());

            NetworkId id;
            if (ctx.IdxGetId(indices->owner, id) == TMOK)
            {
                NetworkObjectInfo* info = GetObjectInfo(id);
                if (info)
                {
                    NetworkComponent::SendMsg(info->owner, msg, type, NMFGuaranteed);
                }
                else
                {
                    RptF("Server: Object %d:%d not found (message %s)", id.creator, id.id,
                         NetworkMessageTypeNames[type]);
                }
            }
        }
        break;
        case NMTDeleteObject:
            if (_state < NGSLoadIsland)
            {
                break; // updates from the last session
            }
            {
                NET_ERROR(dynamic_cast<const IndicesDeleteObject*>(ctx.GetIndices()))
                const IndicesDeleteObject* indices = static_cast<const IndicesDeleteObject*>(ctx.GetIndices());

                NetworkId id;
                if (ctx.IdxTransfer(indices->creator, id.creator) != TMOK)
                {
                    break;
                }
                if (ctx.IdxTransfer(indices->id, id.id) != TMOK)
                {
                    break;
                }
                OnObjectDestroy(id);
            }
            break;
        case NMTDeleteCommand:
            if (_state < NGSLoadIsland)
            {
                break; // updates from the last session
            }
            {
                NET_ERROR(dynamic_cast<const IndicesDeleteCommand*>(ctx.GetIndices()))
                const IndicesDeleteCommand* indices = static_cast<const IndicesDeleteCommand*>(ctx.GetIndices());

                NetworkId id;
                if (ctx.IdxTransfer(indices->creator, id.creator) != TMOK)
                {
                    break;
                }
                if (ctx.IdxTransfer(indices->id, id.id) != TMOK)
                {
                    break;
                }
                // destroy command object
                int owner = PerformObjectDestroy(id);
                // notify all players command was deleted
                for (int i = 0; i < _players.Size(); i++)
                {
                    NetworkPlayerInfo& pInfo = _players[i];
                    if (pInfo.state < NGSLoadIsland)
                    {
                        continue;
                    }
                    if (pInfo.dpid == owner)
                    {
                        continue;
                    }
                    NetworkComponent::SendMsg(pInfo.dpid, msg, type, NMFGuaranteed);
                }
            }
            break;
        case NMTCreateObject:
        case NMTCreateVehicle:
        case NMTCreateDetector:
        case NMTCreateShot:
        case NMTCreateExplosion:
        case NMTCreateCrater:
        case NMTCreateCraterOnVehicle:
        case NMTCreateObjectDestructed:
        case NMTCreateAICenter:
        case NMTCreateAIGroup:
        case NMTCreateAISubgroup:
        case NMTCreateAIUnit:
        case NMTCreateCommand:
        case NMTCreateHelicopter:
        case NMTUpdateDammageVehicleAI:
        case NMTUpdateDammageObject:
        case NMTUpdatePositionVehicle:
        case NMTUpdatePositionMan:
        case NMTUpdatePositionTank:
        case NMTUpdatePositionCar:
        case NMTUpdatePositionAirplane:
        case NMTUpdatePositionHelicopter:
        case NMTUpdatePositionShip:
        case NMTUpdatePositionSeagull:
        case NMTUpdatePositionMotorcycle:
        case NMTUpdateTransport:
        case NMTUpdateTankOrCar:
        case NMTUpdateTank:
        case NMTUpdateCar:
        case NMTUpdateAirplane:
        case NMTUpdateHelicopter:
        case NMTUpdateParachute:
        case NMTUpdateShip:
        case NMTUpdateSeagull:
        case NMTUpdateObject:
        case NMTUpdateVehicle:
        case NMTUpdateDetector:
        case NMTUpdateFlag:
        case NMTUpdateShot:
        case NMTUpdateMine:
        case NMTUpdateVehicleAI:
        case NMTUpdateVehicleBrain:
        case NMTUpdateVehicleSupply:
        case NMTUpdateMan:
        case NMTUpdateAICenter:
        case NMTUpdateAIGroup:
        case NMTUpdateAISubgroup:
        case NMTUpdateAIUnit:
        case NMTUpdateCommand:
        case NMTUpdateMotorcycle:
        case NMTUpdateFireplace:
            OnObjectMessage(from, msg, type, ctx);
            break;
        case NMTUpdateClientInfo:
        {
            ClientInfoObject clientInfo;
            clientInfo.TransferMsg(ctx);
            NetworkPlayerInfo* pInfo = GetPlayerInfo(from);
            if (pInfo && msg->time > pInfo->cameraPositionTime)
            {
                pInfo->cameraPosition = clientInfo._cameraPosition;
                pInfo->cameraPositionTime = msg->time;
            }
        }
        break;
        case NMTChat:
        {
            NET_ERROR(dynamic_cast<const IndicesChat*>(ctx.GetIndices()))
            const IndicesChat* indices = static_cast<const IndicesChat*>(ctx.GetIndices());

            AutoArray<NetworkId> units;
            if (ctx.IdxGetIds(indices->units, units) != TMOK)
            {
                break;
            }
            AUTO_STATIC_ARRAY(int, players, 32);
            int channel;
            if (ctx.IdxTransfer(indices->channel, channel) != TMOK)
            {
                break;
            }
            if (channel != CCGlobal && units.Size() > 0)
            {
                GetPlayersOnChannel(players, units, from, false);
            }
            else
            {
                GetPlayersOnChannel(players, channel, from, false);
            }

            if (indices->text >= 0)
            {
                // read message text
                RString text;
                ctx.IdxTransfer(indices->text, text);
                const int maxLength = 200;
                if (text.GetLength() > maxLength)
                {
                    // reduce text length
                    text = text.Substring(0, maxLength);
                    msg->values[indices->text] = RefNetworkDataTyped<RString>(text);
                }
            }

            for (int i = 0; i < players.Size(); i++)
            {
                NetMsgFlags priority = (_state == NGSPlay ? NMFNone : NMFHighPriority);
                NetworkComponent::SendMsg(players[i], msg, type, NMFGuaranteed | priority);
            }
        }
        break;
        case NMTRadioChat:
        {
            NET_ERROR(dynamic_cast<const IndicesRadioChat*>(ctx.GetIndices()))
            const IndicesRadioChat* indices = static_cast<const IndicesRadioChat*>(ctx.GetIndices());
            index1 = indices->units;
            index2 = indices->channel;
        }
            goto LabelChat;
        case NMTRadioChatWave:
        {
            NET_ERROR(dynamic_cast<const IndicesRadioChatWave*>(ctx.GetIndices()))
            const IndicesRadioChatWave* indices = static_cast<const IndicesRadioChatWave*>(ctx.GetIndices());
            index1 = indices->units;
            index2 = indices->channel;
        }
            goto LabelChat;
        case NMTMarkerCreate:
        {
            NET_ERROR(dynamic_cast<const IndicesMarkerCreate*>(ctx.GetIndices()))
            const IndicesMarkerCreate* indices = static_cast<const IndicesMarkerCreate*>(ctx.GetIndices());
            index1 = indices->units;
            index2 = indices->channel;
        }
        LabelChat:
        {
            AutoArray<NetworkId> units;
            if (ctx.IdxGetIds(index1, units) != TMOK)
            {
                break;
            }
            AUTO_STATIC_ARRAY(int, players, 32);
            int channel;
            if (ctx.IdxTransfer(index2, channel) != TMOK)
            {
                break;
            }
            if (channel != CCGlobal)
            {
                GetPlayersOnChannel(players, units, from, false);
            }
            else
            {
                GetPlayersOnChannel(players, channel, from, false);
            }
            for (int i = 0; i < players.Size(); i++)
            {
                NetworkComponent::SendMsg(players[i], msg, type, NMFGuaranteed);
            }
        }
        break;
        case NMTSetVoiceChannel:
        {
            NET_ERROR(dynamic_cast<const IndicesSetVoiceChannel*>(ctx.GetIndices()))
            const IndicesSetVoiceChannel* indices = static_cast<const IndicesSetVoiceChannel*>(ctx.GetIndices());

            AutoArray<NetworkId> units;
            if (ctx.IdxGetIds(indices->units, units) != TMOK)
            {
                break;
            }
            AUTO_STATIC_ARRAY(int, players, 32);
            int channel;
            if (ctx.IdxTransfer(indices->channel, channel) != TMOK)
            {
                break;
            }
            if (channel != CCGlobal && units.Size() > 0)
            {
                GetPlayersOnChannel(players, units, from, true);
            }
            else
            {
                GetPlayersOnChannel(players, channel, from, true);
            }
            NetworkPlayerInfo* info = GetPlayerInfo(from);
            if (info)
            {
                if (info->channel == CCDirect)
                {
                    AUTO_STATIC_ARRAY(int, oldPlayers, 32);
                    _server->GetTransmitTargets(from, oldPlayers);
                    SetSpeakerMessage message(from, false, info->person);
                    for (int i = 0; i < oldPlayers.Size(); i++)
                    {
                        SendMsg(oldPlayers[i], &message, NMFGuaranteed);
                    }
                }
                if (channel == CCDirect)
                {
                    SetSpeakerMessage message(from, true, info->person);
                    for (int i = 0; i < players.Size(); i++)
                    {
                        SendMsg(players[i], &message, NMFGuaranteed);
                    }
                }
                info->channel = channel;
            }
            _server->SetTransmitTargets(from, players, channel);

            // Refresh routes for all OTHER active voice players so they
            // include the newly-joined (or channel-changed) player.
            for (int i = 0; i < _players.Size(); i++)
            {
                NetworkPlayerInfo& pi = _players[i];
                if (!Poseidon::ShouldRefreshOtherNetworkVoiceRoute(pi.dpid, from, pi.state, pi.channel, NGSCreate,
                                                                   CCNone))
                    continue;
                AUTO_STATIC_ARRAY(int, refreshed, 32);
                GetPlayersOnChannel(refreshed, pi.channel, pi.dpid, true);
                _server->SetTransmitTargets(pi.dpid, refreshed, pi.channel);
                if (Poseidon::ShouldNotifyJoinedPlayerAboutActiveDirectSpeaker(pi.dpid, from, pi.state, pi.channel,
                                                                               NGSCreate, CCDirect))
                {
                    SetSpeakerMessage message(pi.dpid, true, pi.person);
                    SendMsg(from, &message, NMFGuaranteed);
                }
            }
        }
        break;
        case NMTNetworkCommand:
        {
            NetworkCommandMessage cmd;
            cmd.TransferMsg(ctx);
            OnNetworkCommand(from, cmd);
        }
        break;
        case NMTMissionParams:
        {
            MissionParamsMessage params;
            params.TransferMsg(ctx);

            _param1 = params._param1;
            _param2 = params._param2;

            for (int i = 0; i < _players.Size(); i++)
            {
                NetworkPlayerInfo& info = _players[i];
                if (!Poseidon::RelayEligible(info.dpid, info.state, from, NGSCreate))
                {
                    continue;
                }
                NetworkComponent::SendMsg(info.dpid, msg, type, NMFGuaranteed);
            }
        }
        break;
        default:
            RptF("Server: Unhandled user message %d", (int)type);
            break;
    }
}

void NetworkServer::OnNetworkCommand(int from, NetworkCommandMessage& cmd)
{
    switch (cmd.type)
    {
        case NCMTLogin:
        {
            if (Poseidon::AdminLoginAllowed(_dedicated, _gameMaster, _admin, AI_PLAYER))
            {
                RString name;
                int ident;
                for (ident = 0; ident < _identities.Size(); ident++)
                {
                    if (_identities[ident].dpnid == from)
                    {
                        name = name + RString(" (id=") + _identities[ident].id + RString(")");
                        break;
                    }
                }
                if (ident < _identities.Size())
                {
                    RString password = cmd.content.ReadString();
                    if (!Poseidon::AdminLoginPasswordAccepted(_serverCfg, password))
                    {
                        if (++_identities[ident].failedLogin >= 10)
                        {
                            LOG_INFO(Network, "Admin login rejected for {}", (const char*)name);
                            LOG_INFO(Network, "  Reason: Too many attempts with bad passwords");
                            KickOff(from, KOROther);
                        }
                        break;
                    }

                    // if password is ok, reset the counter
                    _identities[ident].failedLogin = 0;

                    if (_gameMaster != AI_PLAYER)
                    {
                        NetworkCommandMessage answer;
                        answer.type = NCMTLoggedOut;
                        SendMsg(_gameMaster, &answer, NMFGuaranteed);
                    }

                    _gameMaster = from;
                    _admin = false;
                    _adminLoginGranted = true;
                    LOG_INFO(Network, "Admin {} logged in", (const char*)name);

                    _votings.Clear();

                    NetworkCommandMessage answer;
                    answer.type = NCMTLogged;
                    answer.content.Write(&_admin, sizeof(_admin));
                    SendMsg(from, &answer, NMFGuaranteed);

                    if (_mission[0] == '?' && _mission[1] == 0)
                    {
                        NetworkCommandMessage answer;
                        answer.type = NCMTVoteMission;
                        AddMissionList(answer);
                        SendMsg(from, &answer, NMFGuaranteed);
                    }

                    UpdateAdminState();
                }
            }
        }
        break;
        case NCMTLogout:
            if (Poseidon::CommandFromGameMaster(_dedicated, from, _gameMaster))
            {
                NetworkCommandMessage answer;
                answer.type = NCMTLoggedOut;
                SendMsg(_gameMaster, &answer, NMFGuaranteed);

                RString name;
                for (int i = 0; i < _players.Size(); i++)
                {
                    if (_players[i].dpid == _gameMaster)
                    {
                        name = _players[i].name;
                        break;
                    }
                }
                LOG_INFO(Network, "Admin {} logged out", (const char*)name);

                _gameMaster = AI_PLAYER;
                _sessionLocked = false;
                _debugOn.Clear();

                UpdateAdminState();

                if (_mission[0] == '?' && _mission[1] == 0)
                {
                    // when admin logged out, perform mission voting instead
                    NetworkCommandMessage answer;
                    answer.type = NCMTVoteMission;
                    AddMissionList(answer);
                    for (int i = 0; i < _identities.Size(); i++)
                    {
                        SendMsg(_identities[i].dpnid, &answer, NMFGuaranteed);
                    }
                }
            }
            break;
        case NCMTShutdown:
            if (Poseidon::CommandFromPasswordAdmin(_dedicated, from, _gameMaster, _admin))
            {
                GApp->m_validateQuit = true;
            }
            break;
        case NCMTMonitorAsk:
            if (Poseidon::CommandFromGameMaster(_dedicated, from, _gameMaster))
            {
                float interval;
                cmd.content.Read(&interval, sizeof(interval));
                Monitor(interval);
            }
            break;
        case NCMTDebugAsk:
            if (_dedicated && from == _gameMaster || !_dedicated && from == _botClient)
            {
                DebugAsk(cmd.content.ReadString(), from, !_admin);
            }
            break;
        case NCMTMission:
            if (Poseidon::CommandFromGameMaster(_dedicated, from, _gameMaster))
            {
                _mission = cmd.content.ReadString();
                cmd.content.Read(&_cadetMode, sizeof(bool));
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
            if (Poseidon::CommandFromGameMaster(_dedicated, from, _gameMaster))
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
                SendMsg(_gameMaster, &answer, NMFGuaranteed);
            }
            break;
        case NCMTRestart:
            if (Poseidon::CommandFromGameMaster(_dedicated, from, _gameMaster) && _state == NGSPlay)
            {
                _restart = true;
            }
            break;
        case NCMTReassign:
            if (Poseidon::CommandFromGameMaster(_dedicated, from, _gameMaster))
            {
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
            }
            break;
        case NCMTInit:
            if (Poseidon::CommandFromGameMaster(_dedicated, from, _gameMaster))
            {
                _serverCfg.Parse(Poseidon::GetServerConfig());
            }
            break;
        case NCMTLockSession:
            if (Poseidon::CommandFromPasswordAdmin(_dedicated, from, _gameMaster, _admin))
            {
                bool lock;
                if (cmd.content.Read(&lock, sizeof(bool)))
                {
                    _sessionLocked = lock;
                    const PlayerIdentity* adminId = FindIdentity(from);
                    RString fmt = LocalizeString(lock ? IDS_MP_LOCKED : IDS_MP_UNLOCKED);
                    ChatToAllPlayers(Format(fmt, adminId ? (const char*)adminId->name : "admin"));
                }
            }
            break;
        case NCMTKick:
            if (Poseidon::CommandFromAdminOrBot(from, _gameMaster, _botClient))
            {
                int player;
                if (cmd.content.Read(&player, sizeof(int)) && player != _botClient)
                {
                    KickOff(player, KORKick);
                }
            }
            break;
        case NCMTBan:
            if (Poseidon::CommandFromAdminOrBot(from, _gameMaster, _botClient))
            {
                int player;
                if (cmd.content.Read(&player, sizeof(int)) && player != _botClient)
                {
                    Ban(player);
                }
            }
            break;
        case NCMTUnban:
            if (Poseidon::CommandFromAdminOrBot(from, _gameMaster, _botClient))
            {
                RString arg = cmd.content.ReadString();
                if (arg.GetLength() > 0)
                {
                    Unban(static_cast<const char*>(arg));
                }
            }
            break;
        case NCMTVote:
            if (Poseidon::VotingOpen(_dedicated, _gameMaster, AI_PLAYER))
            {
                const NetworkPlayerInfo* info = GetPlayerInfo(from);
                if (!info)
                {
                    break;
                }
                bool doEcho = false;
                char echo[512];
                snprintf(echo, sizeof(echo), "%s votes: ", (const char*)info->name);
                int subtype;
                cmd.content.Read(&subtype, sizeof(int));
                switch (subtype)
                {
                    case NCMTMission:
                    {
                        char* ptr = cmd.content.Data() + cmd.content.GetPos();
                        int size = cmd.content.Size() - cmd.content.GetPos();
                        _votings.Add(this, (char*)&subtype, sizeof(int), 0.9999, from, ptr, size, true);
                        // ptr is raw wire bytes, not guaranteed NUL-terminated; bound both the
                        // read (%.*s precision) and the write (snprintf) so a long or
                        // unterminated vote payload stays within the packet and echo[].
                        if (size > 0)
                        {
                            size_t used = strlen(echo);
                            snprintf(echo + used, sizeof(echo) - used, "Mission %.*s", size, ptr);
                        }
                        doEcho = true;
                    }
                    break;
                    case NCMTMissions:
                        _votings.Add(this, (char*)&subtype, sizeof(int), _voteThreshold, from);
                        strncat(echo, "Missions", sizeof(echo) - strlen(echo) - 1);
                        doEcho = true;
                        break;
                    case NCMTRestart:
                        _votings.Add(this, (char*)&subtype, sizeof(int), _voteThreshold, from);
                        strncat(echo, "Restart", sizeof(echo) - strlen(echo) - 1);
                        doEcho = true;
                        break;
                    case NCMTReassign:
                        _votings.Add(this, (char*)&subtype, sizeof(int), _voteThreshold, from);
                        strncat(echo, "Reassign", sizeof(echo) - strlen(echo) - 1);
                        doEcho = true;
                        break;
                    case NCMTKick:
                    {
                        int id[2];
                        id[0] = subtype;
                        cmd.content.Read(&id[1], sizeof(int));
                        _votings.Add(this, (char*)id, 2 * sizeof(int), _voteThreshold, from);
                        const NetworkPlayerInfo* info = GetPlayerInfo(id[1]);
                        if (info)
                        {
                            size_t used = strlen(echo);
                            snprintf(echo + used, sizeof(echo) - used, "Kick %s", (const char*)info->name);
                            doEcho = true;
                        }
                    }
                    break;
                    case NCMTAdmin:
                    {
                        char* ptr = cmd.content.Data() + cmd.content.GetPos();
                        int size = cmd.content.Size() - cmd.content.GetPos();
                        _votings.Add(this, (char*)&subtype, sizeof(int), _voteThreshold, from, ptr, size);
                        // ptr is wire data; the target player id is the first int — require it
                        // to actually be present before dereferencing.
                        if (!WireBounds::RangeInBounds(0, sizeof(int), size))
                        {
                            break;
                        }
                        int player = *(int*)ptr;
                        RString name;
                        for (int i = 0; i < _players.Size(); i++)
                        {
                            if (_players[i].dpid == player)
                            {
                                name = _players[i].name;
                                break;
                            }
                        }
                        size_t used = strlen(echo);
                        snprintf(echo + used, sizeof(echo) - used, "Admin %s", (const char*)name);
                        doEcho = true;
                    }
                    break;
                }
                // Send voting echo to all other players
                if (doEcho)
                {
                    RefArray<NetworkObject> dummy;
                    ChatMessage msg(CCGlobal, nullptr, dummy, "", echo);
                    for (int i = 0; i < _players.Size(); i++)
                    {
                        if (_players[i].state >= NGSCreate)
                        {
                            SendMsg(_players[i].dpid, &msg, NMFGuaranteed);
                        }
                    }
                }
            }
            break;
        default:
            Fail("Network command type");
            break;
    }
}

void NetworkServer::OnGameStateMessage(int from, NetworkMessage* msg, NetworkMessageType type,
                                       NetworkMessageContext& ctx)
{
    ChangeGameState gs;
    gs.TransferMsg(ctx);
    switch (gs.gameState)
    {
        case NGSLogin:
        case NGSMissionVoted:
        case NGSPrepareSide:
        case NGSPrepareRole:
        case NGSPrepareOK:
        case NGSPlay:
        case NGSDebriefing:
        case NGSDebriefingOK:
        {
            NetworkPlayerInfo* info = GetPlayerInfo(from);
            NET_ERROR(info);
            if (info && info->state >= NGSCreate)
            {
                // JIP: player was in NGSBriefing, now clicking "I'm Ready"
                // — send ChangeGameState(NGSPlay) so client transitions
                if (gs.gameState == NGSPlay && info->jip && info->state == NGSBriefing && _state >= NGSPlay)
                {
                    LOG_INFO(Network, "JIP: Player {} confirmed ready, advancing to NGSPlay", (const char*)info->name);
                    ChangeGameState gsPlay(NGSPlay);
                    SendMsg(from, &gsPlay, NMFGuaranteed);
                }
                info->state = gs.gameState;
                SetPlayerState(from, gs.gameState);
            }

            // JIP: when a JIP player sends PrepareOK (role assigned), advance
            // them through mission transfer → island load → briefing automatically.
            // The MissionHeader was already sent in OnCreatePlayer, so the client
            // has already checked mission file validity and sent AskMissionFile.
            if (gs.gameState == NGSPrepareOK && info && info->jip && _state >= NGSPlay)
            {
                LOG_INFO(Network, "JIP: Player {} assigned role (missionFileValid={}), advancing",
                         (const char*)info->name, info->missionFileValid);

                // Send mission file if client doesn't have it
                if (!info->missionFileValid)
                {
                    SendMissionFile();
                }

                // Advance client through the remaining states.
                // Mirrors normal flow: TransferMission → LoadIsland → Briefing
                ChangeGameState gsTransfer(NGSTransferMission);
                SendMsg(from, &gsTransfer, NMFGuaranteed);
                ChangeGameState gsLoad(NGSLoadIsland);
                SendMsg(from, &gsLoad, NMFGuaranteed);
                ChangeGameState gsBriefing(NGSBriefing);
                SendMsg(from, &gsBriefing, NMFGuaranteed);
                // Client processes these in order:
                // - NGSTransferMission: _state = NGSTransferMission (if has role)
                // - NGSLoadIsland: if _missionFileValid, calls PrepareGame()
                // - NGSBriefing: if _state >= NGSLoadIsland, _state = NGSBriefing
                // Then DisplayMultiplayerSetup handles NGSBriefing → creates
                // DisplayClientGetReady → sends ClientReady(NGSBriefing) → server
                // fast-track triggers.
            }
        }
        break;
        case NGSBriefing:
        {
            if (from == _botClient)
            {
                // select players
                for (int i = 0; i < _players.Size(); i++)
                {
                    NetworkPlayerInfo& info = _players[i];
                    NetworkId& id = info.person;
                    if (id.IsNull())
                    {
                        continue;
                    }
                    NetworkObjectInfo* oInfo = GetObjectInfo(id);
                    if (!oInfo)
                    {
                        continue;
                    }
                    SelectPlayerMessage pl(info.dpid, id, info.cameraPosition, false);
                    NetworkComponent::SendMsg(pl.player, &pl, NMFGuaranteed);
                    ChangeOwner(id, oInfo->owner, pl.player);
                }
                // change state
                SetGameState(NGSBriefing);
            }
            NetworkPlayerInfo* info = GetPlayerInfo(from);
            NET_ERROR(info);
            if (info && info->state >= NGSCreate)
            {
                info->state = NGSBriefing;
                SetPlayerState(from, NGSBriefing);
                // ask player about world file CRC
                RString worldFile = GetWorldName(Glob.header.worldname);
                PerformFileIntegrityCheck(worldFile, info->dpid);

                // JIP: fast-track to NGSPlay if server is already playing
                if (info->jip && _state >= NGSPlay)
                {
                    LOG_INFO(Network, "JIP: Fast-tracking player {} to NGSPlay", (const char*)info->name);

                    // Resolve person from world if not yet set (normal case for JIP)
                    if (info->person.IsNull())
                    {
                        // Find the JIP player's role to get side/group/unit indices
                        bool found = false;
                        for (int r = 0; r < _playerRoles.Size(); r++)
                        {
                            if (_playerRoles[r].player != info->dpid)
                                continue;

                            found = true;
                            AICenter* center = GWorld->GetCenter(_playerRoles[r].side);
                            if (!center)
                            {
                                LOG_WARN(Network, "JIP: No center for side {}", (int)_playerRoles[r].side);
                                break;
                            }
                            AIGroup* grp = center->GetGroup(_playerRoles[r].group);
                            if (!grp)
                            {
                                LOG_WARN(Network, "JIP: No group {} in center", _playerRoles[r].group);
                                break;
                            }
                            // UnitWithID is 1-indexed; role.unit is 0-indexed
                            AIUnit* unit = grp->UnitWithID(_playerRoles[r].unit + 1);
                            if (!unit || !unit->GetPerson())
                            {
                                LOG_WARN(Network, "JIP: No unit/person at index {} (unit={}, person={})",
                                         _playerRoles[r].unit + 1, (void*)unit,
                                         unit ? (void*)unit->GetPerson() : nullptr);
                                break;
                            }

                            Person* person = unit->GetPerson();
                            info->person = person->GetNetworkId();
                            info->cameraPosition = person->Position();
                            // Also store the brain (unit) network ID
                            NetworkId brain = PersonToUnit(info->person);
                            info->unit = brain;

                            LOG_INFO(Network, "JIP: Resolved person {}:{} for player {} role {}", info->person.creator,
                                     info->person.id, (const char*)info->name, r);
                            break;
                        }
                        if (!found)
                            LOG_WARN(Network, "JIP: No role found for player {} (dpid={})", (const char*)info->name,
                                     info->dpid);
                    }
                    else
                    {
                        LOG_DEBUG(Network, "JIP: Person already set {}:{} for player {}", info->person.creator,
                                  info->person.id, (const char*)info->name);
                    }

                    // Send world state first — client needs objects before
                    // SelectPlayer can resolve GetObject(person)
                    SendWorldState(info->dpid);

                    // Send time sync so client sets _jip flag
                    NetworkCommandMessage timeMsg;
                    timeMsg.type = NCMTMissionTimeElapsed;
                    int timeElapsed = GlobalTickCount() - _missionHeader.start;
                    timeMsg.content.Write(&timeElapsed, sizeof(timeElapsed));
                    SendMsg(info->dpid, &timeMsg, NMFGuaranteed);

                    // DON'T send NGSPlay yet — player stays in NGSBriefing
                    // (yellow in player list) until they click "I'm Ready".
                    // The client sends ClientReady(NGSPlay) when ready, and
                    // the server echoes ChangeGameState(NGSPlay) back then.

                    // Assign player AFTER world state so client
                    // has the Person object when it processes SelectPlayer
                    if (!info->person.IsNull())
                    {
                        LOG_INFO(Network, "JIP: Sending SelectPlayer for person {}:{}", info->person.creator,
                                 info->person.id);
                        NetworkObjectInfo* oInfo = GetObjectInfo(info->person);
                        if (oInfo)
                        {
                            SelectPlayerMessage pl(info->dpid, info->person, info->cameraPosition, false);
                            NetworkComponent::SendMsg(pl.player, &pl, NMFGuaranteed);
                            ChangeOwner(info->person, oInfo->owner, pl.player);
                        }
                        // Also transfer ownership of the brain/unit
                        NetworkId brain = PersonToUnit(info->person);
                        if (!brain.IsNull())
                        {
                            NetworkObjectInfo* brainInfo = GetObjectInfo(brain);
                            if (brainInfo)
                            {
                                ChangeOwner(brain, brainInfo->owner, info->dpid);
                            }
                        }
                    }
                    else
                    {
                        LOG_WARN(Network, "JIP: Person is null — NOT sending SelectPlayer for player {}",
                                 (const char*)info->name);
                    }

                    // Run initPlayerServer.sqs on server for JIP player
                    using Poseidon::RunMissionScript;
                    RunMissionScript("initPlayerServer.sqs", GameValue());
                }
            }
        }
        break;
        default:
            RptF("Server: Unexpected game state %d", gs.gameState);
            break;
    }
}

void NetworkServer::OnAskForMessage(int from, NetworkMessage* msg, NetworkMessageType type, NetworkMessageContext& ctx)
{
    int index1 = -1;
    switch (type)
    {
        case NMTAskForAnimationPhase:
        {
            NET_ERROR(dynamic_cast<const IndicesAskForAnimationPhase*>(ctx.GetIndices()))
            const IndicesAskForAnimationPhase* indices =
                static_cast<const IndicesAskForAnimationPhase*>(ctx.GetIndices());
            index1 = indices->vehicle;
        }
            goto LabelAskForDammage;
        case NMTAskForDammage:
        {
            NET_ERROR(dynamic_cast<const IndicesAskForDammage*>(ctx.GetIndices()))
            const IndicesAskForDammage* indices = static_cast<const IndicesAskForDammage*>(ctx.GetIndices());
            index1 = indices->who;
        }
            goto LabelAskForDammage;
        case NMTAskForSetDammage:
        {
            NET_ERROR(dynamic_cast<const IndicesAskForSetDammage*>(ctx.GetIndices()))
            const IndicesAskForSetDammage* indices = static_cast<const IndicesAskForSetDammage*>(ctx.GetIndices());
            index1 = indices->who;
        }
        LabelAskForDammage:
        {
            NetworkId id;
            if (ctx.IdxGetId(index1, id) == TMOK)
            {
                if (id.creator == STATIC_OBJECT)
                {
                    for (int i = 0; i < _players.Size(); i++)
                    {
                        NetworkPlayerInfo& info = _players[i];
                        if (!Poseidon::RelayEligible(info.dpid, info.state, from, NGSPlay))
                        {
                            continue;
                        }
                        NetworkComponent::SendMsg(info.dpid, msg, type, NMFNone);
                    }
                }
                else
                {
                    NetworkObjectInfo* info = GetObjectInfo(id);
                    if (info)
                    {
                        NetworkComponent::SendMsg(info->owner, msg, type, NMFGuaranteed);
                    }
                    else
                    {
                        RptF("Server: Object %d:%d not found (message %s)", id.creator, id.id,
                             NetworkMessageTypeNames[type]);
                    }
                }
            }
        }
        break;
        case NMTAskForGetIn:
        {
            NET_ERROR(dynamic_cast<const IndicesAskForGetIn*>(ctx.GetIndices()))
            const IndicesAskForGetIn* indices = static_cast<const IndicesAskForGetIn*>(ctx.GetIndices());
            index1 = indices->vehicle;
        }
            goto LabelAskWithVehicle;
        case NMTAskForGetOut:
        {
            NET_ERROR(dynamic_cast<const IndicesAskForGetOut*>(ctx.GetIndices()))
            const IndicesAskForGetOut* indices = static_cast<const IndicesAskForGetOut*>(ctx.GetIndices());
            index1 = indices->vehicle;
        }
            goto LabelAskWithVehicle;
        case NMTAskForChangePosition:
        {
            NET_ERROR(dynamic_cast<const IndicesAskForChangePosition*>(ctx.GetIndices()))
            const IndicesAskForChangePosition* indices =
                static_cast<const IndicesAskForChangePosition*>(ctx.GetIndices());
            index1 = indices->vehicle;
        }
            goto LabelAskWithVehicle;
        case NMTAskForAimWeapon:
        {
            NET_ERROR(dynamic_cast<const IndicesAskForAimWeapon*>(ctx.GetIndices()))
            const IndicesAskForAimWeapon* indices = static_cast<const IndicesAskForAimWeapon*>(ctx.GetIndices());
            index1 = indices->vehicle;
        }
            goto LabelAskWithVehicle;
        case NMTAskForAimObserver:
        {
            NET_ERROR(dynamic_cast<const IndicesAskForAimObserver*>(ctx.GetIndices()))
            const IndicesAskForAimObserver* indices = static_cast<const IndicesAskForAimObserver*>(ctx.GetIndices());
            index1 = indices->vehicle;
        }
            goto LabelAskWithVehicle;
        case NMTAskForSelectWeapon:
        {
            NET_ERROR(dynamic_cast<const IndicesAskForSelectWeapon*>(ctx.GetIndices()))
            const IndicesAskForSelectWeapon* indices = static_cast<const IndicesAskForSelectWeapon*>(ctx.GetIndices());
            index1 = indices->vehicle;
        }
            goto LabelAskWithVehicle;
        case NMTAskForAmmo:
        {
            NET_ERROR(dynamic_cast<const IndicesAskForAmmo*>(ctx.GetIndices()))
            const IndicesAskForAmmo* indices = static_cast<const IndicesAskForAmmo*>(ctx.GetIndices());
            index1 = indices->vehicle;
        }
            goto LabelAskWithVehicle;
        case NMTAskForAddImpulse:
        {
            NET_ERROR(dynamic_cast<const IndicesAskForAddImpulse*>(ctx.GetIndices()))
            const IndicesAskForAddImpulse* indices = static_cast<const IndicesAskForAddImpulse*>(ctx.GetIndices());
            index1 = indices->vehicle;
        }
            goto LabelAskWithVehicle;
        case NMTAskForMoveVector:
        {
            NET_ERROR(dynamic_cast<const IndicesAskForMoveVector*>(ctx.GetIndices()))
            const IndicesAskForMoveVector* indices = static_cast<const IndicesAskForMoveVector*>(ctx.GetIndices());
            index1 = indices->vehicle;
        }
            goto LabelAskWithVehicle;
        case NMTAskForMoveMatrix:
        {
            NET_ERROR(dynamic_cast<const IndicesAskForMoveMatrix*>(ctx.GetIndices()))
            const IndicesAskForMoveMatrix* indices = static_cast<const IndicesAskForMoveMatrix*>(ctx.GetIndices());
            index1 = indices->vehicle;
        }
            goto LabelAskWithVehicle;
        case NMTAskForJoinGroup:
        {
            NET_ERROR(dynamic_cast<const IndicesAskForJoinGroup*>(ctx.GetIndices()))
            const IndicesAskForJoinGroup* indices = static_cast<const IndicesAskForJoinGroup*>(ctx.GetIndices());
            index1 = indices->join;
        }
            goto LabelAskWithVehicle;
        case NMTAskForJoinUnits:
        {
            NET_ERROR(dynamic_cast<const IndicesAskForJoinUnits*>(ctx.GetIndices()))
            const IndicesAskForJoinUnits* indices = static_cast<const IndicesAskForJoinUnits*>(ctx.GetIndices());
            index1 = indices->join;
        }
            goto LabelAskWithVehicle;
        case NMTAskForHideBody:
        {
            NET_ERROR(dynamic_cast<const IndicesAskForHideBody*>(ctx.GetIndices()))
            const IndicesAskForHideBody* indices = static_cast<const IndicesAskForHideBody*>(ctx.GetIndices());
            index1 = indices->vehicle;
        }
            goto LabelAskWithVehicle;
        case NMTUpdateWeapons:
        {
            NET_ERROR(dynamic_cast<const IndicesUpdateWeapons*>(ctx.GetIndices()))
            const IndicesUpdateWeapons* indices = static_cast<const IndicesUpdateWeapons*>(ctx.GetIndices());
            index1 = indices->vehicle;
        }
            goto LabelAskWithVehicle;
        case NMTShowTarget:
        {
            NET_ERROR(dynamic_cast<const IndicesShowTarget*>(ctx.GetIndices()))
            const IndicesShowTarget* indices = static_cast<const IndicesShowTarget*>(ctx.GetIndices());
            index1 = indices->vehicle;
        }
            goto LabelAskWithVehicle;
        case NMTShowGroupDir:
        {
            NET_ERROR(dynamic_cast<const IndicesShowGroupDir*>(ctx.GetIndices()))
            const IndicesShowGroupDir* indices = static_cast<const IndicesShowGroupDir*>(ctx.GetIndices());
            index1 = indices->vehicle;
        }
            goto LabelAskWithVehicle;
        case NMTAskForCreateUnit:
        {
            NET_ERROR(dynamic_cast<const IndicesAskForCreateUnit*>(ctx.GetIndices()))
            const IndicesAskForCreateUnit* indices = static_cast<const IndicesAskForCreateUnit*>(ctx.GetIndices());
            index1 = indices->group;
        }
            goto LabelAskWithVehicle;
        case NMTAskForDeleteVehicle:
        {
            NET_ERROR(dynamic_cast<const IndicesAskForDeleteVehicle*>(ctx.GetIndices()))
            const IndicesAskForDeleteVehicle* indices =
                static_cast<const IndicesAskForDeleteVehicle*>(ctx.GetIndices());
            index1 = indices->vehicle;
        }
            goto LabelAskWithVehicle;
        case NMTAskForGroupRespawn:
        {
            NET_ERROR(dynamic_cast<const IndicesAskForGroupRespawn*>(ctx.GetIndices()))
            const IndicesAskForGroupRespawn* indices = static_cast<const IndicesAskForGroupRespawn*>(ctx.GetIndices());
            index1 = indices->group;
        }
            goto LabelAskWithVehicle;
        case NMTAskForActivateMine:
        {
            NET_ERROR(dynamic_cast<const IndicesAskForActivateMine*>(ctx.GetIndices()))
            const IndicesAskForActivateMine* indices = static_cast<const IndicesAskForActivateMine*>(ctx.GetIndices());
            index1 = indices->mine;
        }
            goto LabelAskWithVehicle;
        case NMTAskForInflameFire:
        {
            NET_ERROR(dynamic_cast<const IndicesAskForInflameFire*>(ctx.GetIndices()))
            const IndicesAskForInflameFire* indices = static_cast<const IndicesAskForInflameFire*>(ctx.GetIndices());
            index1 = indices->fireplace;
        }
            goto LabelAskWithVehicle;
        case NMTMsgVTarget:
        case NMTMsgVFire:
        case NMTMsgVMove:
        case NMTMsgVFormation:
        case NMTMsgVSimpleCommand:
        case NMTMsgVLoad:
        case NMTMsgVAzimut:
        case NMTMsgVStopTurning:
        case NMTMsgVFireFailed:
        {
            NET_ERROR(dynamic_cast<const IndicesVMessage*>(ctx.GetIndices()))
            const IndicesVMessage* indices = static_cast<const IndicesVMessage*>(ctx.GetIndices());
            index1 = indices->vehicle;
        }
        LabelAskWithVehicle:
        {
            NetworkId id;
            if (ctx.IdxGetId(index1, id) == TMOK)
            {
                NetworkObjectInfo* info = GetObjectInfo(id);
                if (info)
                {
                    NetworkComponent::SendMsg(info->owner, msg, type, NMFGuaranteed);
                }
                else
                {
                    RptF("Server: Object %d:%d not found (message %s)", id.creator, id.id,
                         NetworkMessageTypeNames[type]);
                }
            }
        }
        break;
        case NMTGroupRespawnDone:
        {
            NET_ERROR(dynamic_cast<const IndicesGroupRespawnDone*>(ctx.GetIndices()))
            const IndicesGroupRespawnDone* indices = static_cast<const IndicesGroupRespawnDone*>(ctx.GetIndices());
            int to;
            if (ctx.IdxTransfer(indices->to, to) == TMOK)
            {
                NetworkComponent::SendMsg(to, msg, type, NMFGuaranteed);
            }
        }
            goto LabelAskWithVehicle;
        case NMTAskForReceiveUnitAnswer:
        {
            NET_ERROR(dynamic_cast<const IndicesAskForReceiveUnitAnswer*>(ctx.GetIndices()))
            const IndicesAskForReceiveUnitAnswer* indices =
                static_cast<const IndicesAskForReceiveUnitAnswer*>(ctx.GetIndices());
            index1 = indices->to;
            // it is not vehicle index, but it does not matter
            // all we need is to be able to get owner of given network object
        }
            goto LabelAskWithVehicle;
    }
}

void NetworkServer::OnObjectMessage(int from, NetworkMessage* msg, NetworkMessageType type, NetworkMessageContext& ctx)
{
    switch (type)
    {
        case NMTCreateObject:
        case NMTCreateVehicle:
        case NMTCreateDetector:
        case NMTCreateShot:
        case NMTCreateExplosion:
        case NMTCreateCrater:
        case NMTCreateCraterOnVehicle:
        case NMTCreateObjectDestructed:
        case NMTCreateAICenter:
        case NMTCreateAIGroup:
        case NMTCreateAISubgroup:
        case NMTCreateAIUnit:
        case NMTCreateCommand:
        case NMTCreateHelicopter:
            if (_state < NGSLoadIsland)
            {
                break; // updates from the last session
            }
            {
                NET_ERROR(dynamic_cast<const IndicesNetworkObject*>(ctx.GetIndices()))
                const IndicesNetworkObject* indices = static_cast<const IndicesNetworkObject*>(ctx.GetIndices());

                NetworkId id;
                if (ctx.IdxTransfer(indices->objectCreator, id.creator) != TMOK)
                {
                    break;
                }
                if (ctx.IdxTransfer(indices->objectId, id.id) != TMOK)
                {
                    break;
                }
                OnObjectCreate(id, from, msg, type);
                for (int i = 0; i < _players.Size(); i++)
                {
                    NetworkPlayerInfo& info = _players[i];
                    if (!Poseidon::RelayEligible(info.dpid, info.state, from, NGSLoadIsland))
                    {
                        continue;
                    }
                    NetworkComponent::SendMsg(info.dpid, msg, type, NMFGuaranteed);
                }
                if (type == NMTCreateAIUnit)
                {
                    NET_ERROR(dynamic_cast<const IndicesCreateAIUnit*>(ctx.GetIndices()))
                    const IndicesCreateAIUnit* indices = static_cast<const IndicesCreateAIUnit*>(ctx.GetIndices());

                    NetworkId person;
                    if (ctx.IdxGetId(indices->person, person) == TMOK)
                    {
                        for (int i = 0; i < _players.Size(); i++)
                        {
                            AddPersonUnitPair(person, id);
                            NetworkPlayerInfo& info = _players[i];
                            if (info.person == person)
                            {
                                // change owner
                                info.unit = id;
                                ChangeOwner(id, from, info.dpid);
                                break;
                            }
                        }
                    }
                }
            }
            break;
        case NMTUpdateDammageVehicleAI:
        case NMTUpdateDammageObject:
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
        case NMTUpdateTransport:
        case NMTUpdateTankOrCar:
        case NMTUpdateTank:
        case NMTUpdateCar:
        case NMTUpdateAirplane:
        case NMTUpdateHelicopter:
        case NMTUpdateParachute:
        case NMTUpdateShip:
        case NMTUpdateSeagull:
        case NMTUpdateObject:
        case NMTUpdateVehicle:
        case NMTUpdateDetector:
        case NMTUpdateFlag:
        case NMTUpdateShot:
        case NMTUpdateMine:
        case NMTUpdateVehicleAI:
        case NMTUpdateVehicleBrain:
        case NMTUpdateVehicleSupply:
        case NMTUpdateMan:
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
                NET_ERROR(dynamic_cast<const IndicesNetworkObject*>(ctx.GetIndices()))
                const IndicesNetworkObject* indices = static_cast<const IndicesNetworkObject*>(ctx.GetIndices());

                NetworkId id;
                if (ctx.IdxTransfer(indices->objectCreator, id.creator) != TMOK)
                {
                    break;
                }
                if (ctx.IdxTransfer(indices->objectId, id.id) != TMOK)
                {
                    break;
                }
                NetworkObjectInfo* oInfo = OnObjectUpdate(id, from, msg, type, ctx.GetClass());
                if (!oInfo)
                {
                    break;
                }
                bool guaranteed;
                if (ctx.IdxTransfer(indices->guaranteed, guaranteed) != TMOK)
                {
                    guaranteed = false;
                }
                if (guaranteed)
                {
                    // init state transfer, send immediately
                    for (int i = 0; i < _players.Size(); i++)
                    {
                        NetworkPlayerInfo& pInfo = _players[i];
                        if (!Poseidon::RelayEligible(pInfo.dpid, pInfo.state, from, NGSLoadIsland))
                        {
                            continue;
                        }

                        UpdateObject(&pInfo, oInfo, ctx.GetClass(), NMFGuaranteed);
                    }
                }

                if (ctx.GetClass() != NMCUpdateGeneric)
                {
                    break;
                }
                if (IsUpdateTransport(type))
                {
                    NET_ERROR(dynamic_cast<const IndicesUpdateTransport*>(ctx.GetIndices()))
                    const IndicesUpdateTransport* indices =
                        static_cast<const IndicesUpdateTransport*>(ctx.GetIndices());

                    NetworkId driver;
                    // unit is always simulated where driver is local
                    // if there is no driver, it can be simulated on any computer
                    bool ownerKnown = false;

                    if (ctx.IdxGetId(indices->driver, driver) == TMOK)
                    {
                        // check for nullptr id
                        if (!driver.IsNull())
                        {
                            // check who is owner of the driver
                            for (int i = 0; i < _objects.Size(); i++)
                            {
                                NetworkObjectInfo& dInfo = *_objects[i];
                                if (!dInfo.current)
                                {
                                    continue;
                                }
                                if (dInfo.id == driver)
                                {
                                    ChangeOwner(id, oInfo->owner, dInfo.owner);
                                    ownerKnown = true;
                                    break;
                                }
                            }
                            if (!ownerKnown)
                            {
                                LOG_DEBUG(Network, "Owner not known - searching player");
                                for (int i = 0; i < _players.Size(); i++)
                                {
                                    NetworkPlayerInfo& pInfo = _players[i];
                                    if (pInfo.person == driver)
                                    {
                                        // change owner
                                        LOG_DEBUG(Network, "Owner not known - player found");
                                        ChangeOwner(id, oInfo->owner, pInfo.dpid);
                                    }
                                }
                            }
                        }
                    }
                }
                else if (type == NMTUpdateAIGroup)
                {
                    NET_ERROR(dynamic_cast<const IndicesUpdateAIGroup*>(ctx.GetIndices()))
                    const IndicesUpdateAIGroup* indices = static_cast<const IndicesUpdateAIGroup*>(ctx.GetIndices());

                    NetworkId leader;
                    if (ctx.IdxGetId(indices->leader, leader) == TMOK)
                    {
                        UpdateGroupLeader(id, leader);
                    }
                }
            }
            break;
    }
}

bool NetworkServer::Authorize(const MsgDescriptor& d, int from)
{
    switch (d.auth)
    {
        case MsgAuth::ServerOrigin:
            // The server originates these; every message reaching the server's
            // OnMessage came from a client (or the local loopback client), so a
            // ServerOrigin message here is always an illegitimate client-sent copy.
            (void)from;
            return false;
        case MsgAuth::Public:
        case MsgAuth::GameMaster:
        case MsgAuth::ObjectOwner:
        case MsgAuth::Unconnected:
        default:
            // GameMaster / ObjectOwner / Public-connected are enforced at their
            // handlers today; the central gate passes them through.
            return true;
    }
}

static bool IsAllDisabled(const AutoArray<PlayerRole>& roles)
{
    bool allDisabled = true;
    bool allPlayers = true;
    for (int i = 0; i < roles.Size(); i++)
    {
        const PlayerRole& role = roles[i];
        if (role.player == AI_PLAYER)
        {
            allDisabled = false;
            allPlayers = false;
            break;
        }
        else if (role.player == NO_PLAYER)
        {
            allPlayers = false;
        }
    }
    return allDisabled && !allPlayers;
}

void NetworkServer::OnMessagePlayerRole(int from, NetworkMessageType type, NetworkMessageContext& ctx)
{
    // PATCHED
    if (_state != NGSPrepareSide)
    {
        // Allow JIP players to assign roles when game is already in progress
        NetworkPlayerInfo* jipInfo = GetPlayerInfo(from);
        if (!jipInfo || !jipInfo->jip || _state < NGSPlay)
        {
            LOG_WARN(Network, "JIP: OnMessagePlayerRole rejected from={} (jipInfo={}, jip={}, state={})", from,
                     (void*)jipInfo, jipInfo ? jipInfo->jip : false, (int)_state);
            return;
        }
        LOG_INFO(Network, "JIP: OnMessagePlayerRole ACCEPTED from={} (JIP player)", from);
    }

    NET_ERROR(dynamic_cast<const IndicesPlayerRole*>(ctx.GetIndices()))
    const IndicesPlayerRole* indices = static_cast<const IndicesPlayerRole*>(ctx.GetIndices());

    int index = -1;
    ctx.IdxTransfer(indices->index, index);
    // The role index is wire-controlled and indexes _playerRoles all through this
    // handler, so it must be range-checked at release. AtOrNull is release-checked,
    // so an out-of-range index is dropped here.
    if (!_playerRoles.AtOrNull(index))
    {
        LOG_WARN(Network, "OnMessagePlayerRole: wire index {} out of range [0,{}) from {}", index, _playerRoles.Size(),
                 from);
        return;
    }
    PlayerRole info;
    info.TransferMsg(ctx);

    // disable AI
    int noPlayer = _missionHeader.disabledAI ? NO_PLAYER : AI_PLAYER;
    if (info.player == AI_PLAYER)
    {
        info.player = noPlayer;
    }

    if (noPlayer == AI_PLAYER)
    {
        // if all AI is disabled, replace removed player by NO_PLAYER
        if (IsAllDisabled(_playerRoles))
        {
            noPlayer = NO_PLAYER;
        }
    }

    int iFound = -1;
    if (info.player != NO_PLAYER && info.player != AI_PLAYER)
    {
        for (int i = 0; i < _playerRoles.Size(); i++)
        {
            if (_playerRoles[i].player == info.player)
            {
                iFound = i;
                break;
            }
        }
    }

    bool lock = Poseidon::CommandFromAdminOrBot(from, _gameMaster, _botClient);

    LOG_DEBUG(Network, "JIP-ROLE: index={}, info.player={}, from={}, lock={}, iFound={}, roles[index].player={}", index,
              info.player, from, lock, iFound, _playerRoles[index].player);

    if (lock)
    {
        if (iFound >= 0)
        {
            _playerRoles[iFound].player = noPlayer;
            _playerRoles[iFound].roleLocked = false;
        }
        info.roleLocked = Poseidon::ShouldLockRole(info.player, from, AI_PLAYER, NO_PLAYER);
        _playerRoles[index] = info;
        LOG_DEBUG(Network, "JIP-ROLE: locked assign OK");
    }
    else
    {
        if (_playerRoles[index].roleLocked)
        {
            LOG_WARN(Network, "JIP-ROLE: REJECTED — target role locked");
            return;
        }
        if (iFound >= 0)
        {
            if (_playerRoles[iFound].roleLocked)
            {
                LOG_WARN(Network, "JIP-ROLE: REJECTED — found role locked");
                return;
            }
        }
        if (Poseidon::RoleSwapAllowed(_playerRoles[index].player, info.player, from, AI_PLAYER, NO_PLAYER))
        {
            _playerRoles[index] = info;
            LOG_DEBUG(Network, "JIP-ROLE: unlocked assign OK, roles[{}].player={}", index, info.player);
        }
        else
        {
            LOG_WARN(Network, "JIP-ROLE: REJECTED — condition not met (cur={}, new={}, from={})",
                     _playerRoles[index].player, info.player, from);
            return;
        }
        if (iFound >= 0)
        {
            _playerRoles[iFound].player = noPlayer;
            _playerRoles[iFound].roleLocked = false;
        }
    }

    {
        // create message
        Ref<NetworkMessage> msgSend = new NetworkMessage();
        msgSend->time = Glob.time;
        NetworkMessageContext ctxSend(msgSend, ctx.GetFormat(), this, TO_SERVER, MSG_SEND);

        NET_ERROR(dynamic_cast<const IndicesPlayerRole*>(ctxSend.GetIndices()))
        const IndicesPlayerRole* indices = static_cast<const IndicesPlayerRole*>(ctxSend.GetIndices());

        TMError err = ctxSend.IdxTransfer(indices->index, index);
        if (err != TMOK)
        {
            return;
        }
        err = info.TransferMsg(ctxSend);
        if (err != TMOK)
        {
            return;
        }

        // send message
        for (int i = 0; i < _players.Size(); i++)
        {
            NetworkPlayerInfo& player = _players[i];
            if (player.state < NGSCreate)
            {
                continue;
            }
            //					SendMsg(player.dpid, &info, 0, index, NMFGuaranteed);
            NetworkComponent::SendMsg(player.dpid, msgSend, type, NMFGuaranteed);
        }
    }
    if (iFound >= 0)
    {
        // create message
        Ref<NetworkMessage> msgSend = new NetworkMessage();
        msgSend->time = Glob.time;
        NetworkMessageContext ctxSend(msgSend, ctx.GetFormat(), this, TO_SERVER, MSG_SEND);

        NET_ERROR(dynamic_cast<const IndicesPlayerRole*>(ctxSend.GetIndices()))
        const IndicesPlayerRole* indices = static_cast<const IndicesPlayerRole*>(ctxSend.GetIndices());

        TMError err = ctxSend.IdxTransfer(indices->index, iFound);
        if (err != TMOK)
        {
            return;
        }
        err = _playerRoles[iFound].TransferMsg(ctxSend);
        if (err != TMOK)
        {
            return;
        }

        // send message
        for (int i = 0; i < _players.Size(); i++)
        {
            NetworkPlayerInfo& player = _players[i];
            if (player.state < NGSCreate)
            {
                continue;
            }
            //					SendMsg(player.dpid, &info, 0, index, NMFGuaranteed);
            NetworkComponent::SendMsg(player.dpid, msgSend, type, NMFGuaranteed);
        }
    }
}

DWORD NetworkServer::SendMsg(int to, NetworkSimpleObject* object, NetMsgFlags dwFlags)
{
    // check if message can be sent
    NetworkPlayerInfo* info = GetPlayerInfo(to);
    if (!info)
    {
        RptF("Server: cannot send message - player %d is not known.", to);
        return 0xFFFFFFFF;
    }
    if (info->state < NGSCreate && object->GetNMType() >= NMTFirstVariant)
    {
        RptF("Server: cannot send message - player's %s messages are not registred yet.", (const char*)info->name);
        return 0xFFFFFFFF;
    }
    return NetworkComponent::SendMsg(to, object, dwFlags);
}

void NetworkServer::EnqueueMsg(int to, NetworkMessage* msg, NetworkMessageType type)
{
    NetworkPlayerInfo* info = GetPlayerInfo(to);
    if (!info)
    {
        Fail("PlayerInfo");
        return;
    }
    // if
    int index = info->_messageQueue.Add();
    info->_messageQueue[index].type = type;
    info->_messageQueue[index].msg = msg;
}

void NetworkServer::EnqueueMsgNonGuaranteed(int to, NetworkMessage* msg, NetworkMessageType type)
{
    NetworkPlayerInfo* info = GetPlayerInfo(to);
    if (!info)
    {
        Fail("PlayerInfo");
        return;
    }
    // if
    int index = info->_messageQueueNonGuaranteed.Add();
    info->_messageQueueNonGuaranteed[index].type = type;
    info->_messageQueueNonGuaranteed[index].msg = msg;
}
