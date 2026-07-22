#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Network/NetworkServerCommon.hpp>
#include <Poseidon/Network/MasterServerPublisher.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Core/ModSystem.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <Poseidon/Core/Config/Config.hpp>
#include <Poseidon/Network/NetworkImpl.hpp>
#include <Poseidon/Network/NetworkServerAuth.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>

#include <Poseidon/Network/NetworkConfig.hpp>

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
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Common/Global.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

#ifdef _WIN32
#include <io.h>
#include <winsock2.h>
#include <process.h>
#endif

using Poseidon::Foundation::Time;

#define LOG_SEND_PROCESS 0
#define LOG_PLAYERS 1

using namespace Poseidon;
bool NetworkServer::IntegrityCheck(int dpnid, IntegrityQuestionType type, const IntegrityQuestion& q)
{
    NetworkPlayerInfo* info = GetPlayerInfo(dpnid);
    if (!info)
    {
        return false;
    }

    const int timeout = 120000; // in ms
    int id = info->nextQuestionId++;

    IntegrityQuestionInfo& qi = info->integrityQuestions.Append();
    qi.id = id;
    qi.type = type;
    qi.timeout = GlobalTickCount() + timeout;
    qi.q = q;

    if (type == IQTData)
    {
        LOG_DEBUG(Network, "Asking {} about file {}", (const char*)info->name, (const char*)q.name);
    }

    IntegrityQuestionMessage msg;
    msg.id = id;
    msg.type = type;
    msg.q = q;
    SendMsg(dpnid, &msg, NMFGuaranteed | NMFHighPriority);
    return true;
}

const ParamEntry* FindConfigParamEntry(const char* path);

IntegrityInvestigationConfig::IntegrityInvestigationConfig(const char* path)
{
    // find entry by query
    _root = nullptr;
    const ParamEntry* entry = FindConfigParamEntry(path);
    if (!entry)
    {
        entry = &Pars;
    }
    if (!entry->IsClass())
    {
        _class = nullptr;
        return;
    }
    NET_ERROR(dynamic_cast<const ParamClass*>(entry));

    _class = static_cast<const ParamClass*>(entry);
    _root = _class;
    _questionTimeout = UINT_MAX;
}

RString IntegrityInvestigationConfig::GetResult() const
{
    return _result;
}

static RString PathFirstFolder(const char* path)
{
    const char* next = strchr(path, '/');
    if (!next)
    {
        return "";
    }
    return RString(path, next - path);
}

static RString ConvertContextToCfgPath(RString path)
{
    RString base = PathFirstFolder(path);
    if (!strcmpi(base, Pars.GetName()))
    {
        RString stripBase = path.Substring(base.GetLength(), INT_MAX);
        return RString("cfg") + stripBase;
    }
    else if (!strcmpi(base, Res.GetName()))
    {
        RString stripBase = path.Substring(base.GetLength(), INT_MAX);
        return RString("rsc") + stripBase;
    }
    return path;
}

bool IntegrityInvestigationConfig::Proceed(NetworkServer* server, NetworkPlayerInfo* pi)
{
    if (!_class)
    {
        return false;
    }
    // if some question is in progress, wait until it is answered
    if (_questionTimeout < UINT_MAX)
    {
        // if investigation timed out, give result
        if (GlobalTickCount() > _questionTimeout)
        {
            Log("Investigation timed out");
            _result = "Time out (probably cheat is used)";
            return false;
        }
        return true;
    }

    // we may ask client only about verified class or about parent of verified class
    // otherwise we have no guarantee that he is able to reply, as class
    // may be non-existing in his config
    if (_class == _root)
    {
        // _root is already known to be invalid, there is no need to ask about it
        QuestionAnswered(false);
    }
    else if (_class->HasChecksum() && _class != _root)
    {
        // perform test with current class
        RString cfgPath = ConvertContextToCfgPath(_class->GetContext());
        _question = IntegrityQuestion(cfgPath, 0, 0);
        // we expect to get answer within two minutes
        // if not, we consider this test failed
        _questionTimeout = GlobalTickCount() + 120000;
// ask given player this question
#if 1
        LOG_DEBUG(Network, "Asking player {} about {}", (const char*)pi->name, (const char*)_class->GetContext());
#endif
        server->IntegrityCheck(pi->dpid, IQTConfig, _question);
    }
    else
    {
        // class not protected - skip it
        // pretend we received an answer class is OK
        do
        {
            QuestionAnswered(true);
        } while (_class && !_class->HasChecksum());
    }

    return true;
}

bool IntegrityInvestigationConfig::QuestionMatching(const IntegrityQuestion& q) const
{
    if (q.name != _question.name)
    {
        return false;
    }
    return true;
}

void IntegrityInvestigationConfig::QuestionAnswered(bool answerOK)
{
    _questionTimeout = UINT_MAX;
    // update investigation, if neccessary, set error code
    if (!answerOK)
    {
        _result = _question.name;
    }
    // if question was answered with no problems detected, we may proceed to next class
    // we should:
    // 1) pass control to our first child
    // 2) if there is none, find next class in our parent
    // if class reply is OK, there is no need to check children
    if (!answerOK)
    {
        for (int i = 0; i < _class->GetEntryCount(); i++)
        {
            const ParamEntry* entry = &_class->GetEntry(i);
            if (!entry->IsClass())
            {
                continue;
            }
            NET_ERROR(dynamic_cast<const ParamClass*>(entry));
            _class = static_cast<const ParamClass*>(entry);
            return;
        }
    }
    // no child, return back to our parent and find its next child
    // check if there are any more entries in current class
    for (;;)
    {
        if (_class == _root)
        {
            // we reached root - terminate
            if (_class && _result.GetLength() == 0)
            {
                _result = ConvertContextToCfgPath(_class->GetContext());
            }
            _class = nullptr;
            return;
        }
        const ParamClass* parent = _class->GetParent();
        if (!parent)
        {
            if (_class == &Pars)
            {
                // after completed with Pars, Res should be processed
                // unless _root is Pars, but this is already handled by _class=_root
                _class = &Res;
                return;
            }
            _class = nullptr;
            return;
        }
        // find _class in its parent
        int index = -1;
        for (int i = 0; i < parent->GetEntryCount(); i++)
        {
            if (&parent->GetEntry(i) == _class)
            {
                index = i + 1;
                break;
            }
        }
        NET_ERROR(index >= 0);
        // find next child of our parent
        while (index < parent->GetEntryCount() && !parent->GetEntry(index).IsClass())
        {
            index++;
        }
        if (index >= parent->GetEntryCount())
        {
            // parent has no more children, proceed with its parent
            _class = parent;
        }
        else
        {
            const ParamEntry* entry = &parent->GetEntry(index);
            NET_ERROR(entry->IsClass());
            NET_ERROR(dynamic_cast<const ParamClass*>(entry));
            _class = static_cast<const ParamClass*>(entry);
            return;
        }
    }
}

// Check if given question is part of any investigation and let investigation
// process it. Returns true if the question was part of an investigation.

bool NetworkServer::IntegrityAnswerReceived(NetworkPlayerInfo* pi, IntegrityQuestionType qType,
                                            const IntegrityQuestion& q, bool answerOK)
{
    for (int qt = 0; qt < IntegrityQuestionTypeCount; qt++)
    {
        IntegrityInvestigation* ii = pi->integrityInvestigation[qt];
        if (!ii)
        {
            continue;
        }
        // check if given answer is answer to any given question
        if (ii->QuestionMatching(q))
        {
            // update result
            ii->QuestionAnswered(answerOK);
            return true;
        }
    }
    return false;
}

void NetworkServer::PerformInitialIntegrityCheck(NetworkPlayerInfo& pi)
{
    IntegrityCheck(pi.dpid, IQTConfig, IntegrityQuestion());
    const ParamEntry* datafiles = _serverCfg.FindEntry("CheckFiles");
    if (datafiles)
    {
        for (int i = 0; i < datafiles->GetSize(); i++)
        {
            RStringB file = (*datafiles)[i];
            IntegrityCheck(pi.dpid, IQTData, IntegrityQuestion(file, 0, INT_MAX));
        }
    }
    const ExeCRCBlock* GetCodeCheck();
    const ExeCRCBlock* crc = GetCodeCheck();
    if (crc->size > 0)
    {
        // perform only one test here  - ID changer
        IntegrityCheck(pi.dpid, IQTExe, IntegrityQuestion("", crc->offset, crc->size));
    }
}

void NetworkServer::PerformFileIntegrityCheck(const char* file, int dpid)
{
    IntegrityQuestion q;
    q.name = file;

    if (dpid >= 0)
    {
        IntegrityCheck(dpid, IQTData, q);
    }
    else
    {
        for (int i = 0; i < _players.Size(); i++)
        {
            const NetworkPlayerInfo& pi = _players[i];
            if (pi.dpid == _botClient)
            {
                continue;
            }
            IntegrityCheck(pi.dpid, IQTData, q);
        }
    }
}

void NetworkServer::PerformExeIntegrityCheck(const char* location, int dpid)
{
    int offset = 0, size = 0;
    sscanf(location, "%x:%x", &offset, &size);
    if (size <= 0)
    {
        return;
    }
    const int granularity = 1024;
    int align = offset & (granularity - 1);
    offset -= align;
    size += align;
    int alignSize = (granularity - size) & (granularity - 1);
    size += alignSize;
    while (size > 0)
    {
        int checkSize = size < granularity ? size : granularity;
        // perform a granular check
        IntegrityQuestion q;
        q.name = nullptr;
        q.offset = offset;
        q.size = checkSize;

        if (dpid >= 0)
        {
            IntegrityCheck(dpid, IQTExe, q);
        }
        else
        {
            for (int i = 0; i < _players.Size(); i++)
            {
                const NetworkPlayerInfo& pi = _players[i];
                if (pi.dpid == _botClient)
                {
                    continue;
                }
                IntegrityCheck(pi.dpid, IQTExe, q);
            }
        }
        offset += checkSize;
        size -= checkSize;
    }
}

void NetworkServer::PerformRandomIntegrityCheck(NetworkPlayerInfo& pi)
{
    const ExeCRCBlock* GetCodeCheck();
    for (const ExeCRCBlock* crc = GetCodeCheck(); crc->size > 0; crc++)
    {
        IntegrityCheck(pi.dpid, IQTExe, IntegrityQuestion("", crc->offset, crc->size));
    }
}

void NetworkServer::PerformIntegrityInvestigations()
{
    // scan all players
    for (int i = 0; i < _players.Size(); i++)
    {
        NetworkPlayerInfo& pi = _players[i];
        bool someInvestigation = false;
        for (int q = 0; q < IntegrityQuestionTypeCount; q++)
        {
            IntegrityInvestigation* ii = pi.integrityInvestigation[q];
            if (!ii)
            {
                continue;
            }
            someInvestigation = true;
            bool toBeContinued = ii->Proceed(this, &pi);
            if (!toBeContinued)
            {
                RString result = ii->GetResult();
                if (result.GetLength() > 0)
                {
                    // inform about result

                    RString format = LocalizeString(IDS_MP_VALIDERROR + q);
                    RString message = BuildNetworkServerValidationMessage((const char*)format, (const char*)pi.name,
                                                                          (const char*)result);
                    ReportNetworkServerGlobalMessage("", message);
                    LOG_INFO(Network, "{}", (const char*)message);
                    Log("{}", (const char*)message);
                }
                // cancel test
                pi.integrityInvestigation[q].Free();
            }
        }
        if (!someInvestigation)
        {
            // if there is no investigation running, we may perform random integrity check
            DWORD time = GlobalTickCount();
            if (time > pi.integrityCheckNext)
            {
                int qIndex = pi.FindIntegrityQuestion(IQTExe);
                if (qIndex >= 0)
                {
                }
                else
                {
                    PerformRandomIntegrityCheck(pi);
                    // avoid regular checks to avoid regular countermeasures
                    pi.integrityCheckNext = time + 1000 * toInt(20 + GRandGen.RandomValue() * 30);
                }
            }
        }
    }
}

void NetworkServer::OnIntegrityCheckFailed(int dpnid, IntegrityQuestionType type, const char* text, bool final)
{
    NetworkPlayerInfo* info = GetPlayerInfo(dpnid);
    if (!info)
    {
        return;
    }
    RString format = LocalizeString(IDS_MP_VALIDERROR + type);
    RString message = BuildNetworkServerValidationMessage((const char*)format, (const char*)info->name, text);
    if (final)
    {
        RefArray<NetworkObject> dummy;
        GNetworkManager.Chat(CCGlobal, "", dummy, message);
    }
    GChatList.Add(CCGlobal, nullptr, message, false, true);
    LOG_INFO(Network, "{}", (const char*)message);
}

NetworkPlayerInfo* NetworkServer::GetPlayerInfo(int dpid)
{
    for (int i = 0; i < _players.Size(); i++)
    {
        NetworkPlayerInfo& info = _players[i];
        if (info.dpid == dpid)
        {
            return &info;
        }
    }
    return nullptr;
}

NetworkPlayerInfo* NetworkServer::OnPlayerCreate(int dpid, const char* name)
{
    char playerName[256];
    snprintf(playerName, sizeof(playerName), "%s", (const char*)name);

    bool ok = false;
    int suffix = 1;
    while (!ok)
    {
        ok = true;
        for (int i = 0; i < _players.Size(); i++)
        {
            NetworkPlayerInfo& info = _players[i];
            if (info.dpid == dpid)
            {
                continue;
            }
            if (stricmp(info.name, playerName) == 0)
            {
                snprintf(playerName, sizeof(playerName), "%s (%d)", name, ++suffix);
                ok = false;
                break;
            }
        }
    }

    {
        NetworkPlayerInfo* info = GetPlayerInfo(dpid);
        if (info)
        {
            NET_ERROR(strcmp(info->name, playerName) == 0);
            return info;
        }
    }

    int index = _players.Add();
    NetworkPlayerInfo& info = _players[index];
    info.dpid = dpid;
    info.dvid = 0;
    info.channel = CCNone;
    info.name = playerName;
    info.cameraPosition = InvalidCamPos;
    info.cameraPositionTime = TIME_MIN;
    info.integrityCheckNext = UINT_MAX;
    info.connectionProblemsReported = false;
    info.jip = false;
    info.kickedOff = false;
    info.nextQuestionId = 1;

#if LOG_PLAYERS
    RptF("Server: Player info added - name %s, id %d (total %d identities, %d players)", (const char*)playerName, dpid,
         _identities.Size(), _players.Size());
#endif

    if (_dedicated)
    {
        info.motdIndex = -1;
        if (dpid != _botClient && _motd.Size() > 0)
        {
            info.motdIndex = 0;
        }
    }

    // register formats
    for (int mt = NMTFirstVariant; mt < NMTN; mt++)
    {
        NetworkMessageFormatBase* item = GetFormat(mt);

        // create message
        NetworkMessageType type = item->GetNMType(NMCCreate);
        NetworkMessageFormatBase* format = GetFormat(type);
        Ref<NetworkMessage> msg = new NetworkMessage();
        msg->time = Glob.time;
        NetworkMessageContext ctx(msg, format, this, dpid, MSG_SEND);

        int IndicesMsgFormatGetIndex(const NetworkMessageIndices* ind);
        int index = IndicesMsgFormatGetIndex(ctx.GetIndices());

        TMError err;
        err = ctx.IdxTransfer(index, mt);
        if (err != TMOK)
        {
            continue;
        }
        err = item->TransferMsg(ctx);
        if (err != TMOK)
        {
            continue;
        }

        // send message
        NetworkComponent::SendMsg(dpid, msg, type, NMFGuaranteed);
    }
    info.state = NGSCreate;
    ChangeGameState gs(NGSCreate);
    SendMsg(dpid, &gs, NMFGuaranteed);

    // Send chat message about player is connecting
    {
        RString message =
            BuildNetworkServerPlayerMessage((const char*)LocalizeString(IDS_MP_CONNECTING), (const char*)info.name);
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

    // Fire onPlayerConnected event
    if (GWorld && GWorld->GetGameState())
    {
        GameState* gs = GWorld->GetGameState();
        GameValue handler = gs->VarGet("onplayerconnectedcode");
        if (handler.GetType() == GameString)
        {
            gs->VarSet("_id", GameValue((GameScalarType)info.dpid), false, true);
            gs->VarSet("_name", GameValue((GameStringType)info.name), false, true);
            gs->Execute((const char*)(GameStringType)handler);
        }
    }

    return &info;
}

void NetworkServer::UpdateAdminState()
{
    for (int i = 0; i < _identities.Size(); i++)
    {
        PlayerIdentity& pi = _identities[i];
        pi._rights = Poseidon::ComputeAdminRights(pi._rights, pi.dpnid == _gameMaster, _admin, PRAdmin, PRVotedAdmin);
    }
    DWORD updatePingTime = GlobalTickCount() + 1000;
    if (_pingUpdateNext > updatePingTime)
    {
        _pingUpdateNext = updatePingTime;
    }
}

void NetworkServer::OnPlayerDestroy(int dpid)
{
    if (dpid == _botClient)
    {
        Fail("Not implemented.");
        // new bot client must be selected
        return;
    }

    RString name;
    if (dpid == _gameMaster)
    {
        for (int i = 0; i < _players.Size(); i++)
        {
            if (_players[i].dpid == _gameMaster)
            {
                name = _players[i].name;
                break;
            }
        }
    }

    NetworkPlayerInfo* info = GetPlayerInfo(dpid);
    if (info)
    {
        // Fire onPlayerDisconnected event before removing
        if (GWorld && GWorld->GetGameState())
        {
            GameState* gs = GWorld->GetGameState();
            GameValue handler = gs->VarGet("onplayerdisconnectedcode");
            if (handler.GetType() == GameString)
            {
                gs->VarSet("_id", GameValue((GameScalarType)info->dpid), false, true);
                gs->VarSet("_name", GameValue((GameStringType)info->name), false, true);
                gs->Execute((const char*)(GameStringType)handler);
            }
        }

        RString message =
            BuildNetworkServerPlayerMessage((const char*)LocalizeString(IDS_MP_DISCONNECT), (const char*)info->name);
        ReportNetworkServerGlobalMessage("", message);

        if (_dedicated)
        {
            LOG_INFO(Network, "Player {} disconnected", (const char*)info->name);
        }

        for (int i = 0; i < _players.Size(); i++)
        {
            NetworkPlayerInfo& info = _players[i];
            if (info.dpid == dpid)
            {
#if LOG_PLAYERS
                RptF("Server: Player info removed - name %s, id %d (total %d identities, %d players)",
                     (const char*)info.name, dpid, _identities.Size(), _players.Size() - 1);
#endif
                _players.Delete(i);
                break;
            }
        }
        if (_players.Size() == 0)
        {
            _nextPlayerId = 1;
            _votings.Clear();
        }
        else if (_dedicated && _players.Size() == 1)
        {
            _nextPlayerId = 1;
            _votings.Clear();
        }
    }

    for (int i = 0; i < _pendingMessages.Size();)
    {
        NetPendingMessage& pending = _pendingMessages[i];
        if (pending.player->player == dpid)
        {
            _pendingMessages.Delete(i);
        }
        else
        {
            i++;
        }
    }

    for (int i = 0; i < _objects.Size(); i++)
    {
        NetworkObjectInfo& oInfo = *_objects[i];
        oInfo.DeletePlayerObjectInfo(dpid);

        for (int j = NMCUpdateFirst; j < NMCUpdateN; j++)
        {
            NetworkCurrentInfo& info = oInfo.current[j];
            if (info.from == dpid)
            {
                info.from = _botClient;
                info.message = nullptr;
            }
        }
        if (oInfo.owner == dpid)
        {
            oInfo.owner = _botClient;
            if (!oInfo.id.IsNull())
            {
                ChangeOwnerMessage msg(oInfo.id, _botClient);
                SendMsg(_botClient, &msg, NMFGuaranteed);
            }
        }
    }
    for (int i = 0; i < _playerRoles.Size(); i++)
    {
        PlayerRole& role = _playerRoles[i];
        if (role.player == dpid)
        {
            role.player = _missionHeader.disabledAI ? NO_PLAYER : AI_PLAYER;
            role.roleLocked = false;

            // create message
            NetworkMessageType type = role.GetNMType(NMCCreate);
            NetworkMessageFormatBase* format = GetFormat(type);
            Ref<NetworkMessage> msg = new NetworkMessage();
            msg->time = Glob.time;
            NetworkMessageContext ctx(msg, format, this, TO_SERVER, MSG_SEND);

            NET_ERROR(dynamic_cast<const IndicesPlayerRole*>(ctx.GetIndices()))
            const IndicesPlayerRole* indices = static_cast<const IndicesPlayerRole*>(ctx.GetIndices());

            TMError err = ctx.IdxTransfer(indices->index, i);
            if (err != TMOK)
            {
                break;
            }
            err = role.TransferMsg(ctx);
            if (err != TMOK)
            {
                break;
            }

            // send message
            for (int i = 0; i < _players.Size(); i++)
            {
                NetworkPlayerInfo& player = _players[i];
                if (player.state < NGSCreate)
                {
                    continue;
                }
                NetworkComponent::SendMsg(player.dpid, msg, type, NMFGuaranteed);
            }

            break;
        }
    }
    LogoutMessage logout;
    logout.dpnid = dpid;
    for (int i = 0; i < _identities.Size(); i++)
    {
        if (_identities[i].dpnid == dpid)
        {
            SquadIdentity* squad = _identities[i].squad;
#if LOG_PLAYERS
            RptF("Server: Identity removed - name %s, id %d (total %d identities, %d players)",
                 (const char*)_identities[i].name, dpid, _identities.Size() - 1, _players.Size());
#endif
            _identities.Delete(i);
            // check if player info was deleted properly
            NetworkPlayerInfo* check = GetPlayerInfo(dpid);
            if (check)
            {
                RptF("Server error: identity deleted, player info still exist (%s, id %d)", (const char*)check->name,
                     dpid);
            }

            if (squad)
            {
                // check if squad is used
                bool found = false;
                for (int j = 0; j < _identities.Size(); j++)
                {
                    if (_identities[j].squad == squad)
                    {
                        found = true;
                        break;
                    }
                }
                // if not, delete it
                if (!found)
                {
                    for (int j = 0; j < _squads.Size(); j++)
                    {
                        if (_squads[j] == squad)
                        {
                            _squads.Delete(j);
                            break;
                        }
                    }
                }
            }
            _votings.Check(this);
            break;
        }
    }
    for (int i = 0; i < _identities.Size(); i++)
    {
        PlayerIdentity& player = _identities[i];
        SendMsg(player.dpnid, &logout, NMFGuaranteed);
    }

    if (IsDedicatedServer())
    {
        NotifyMasterServerStateChanged();
        NetworkClient* client = _parent->GetClient();
        NET_ERROR(client && client->GetPlayer() != logout.dpnid);
        // send new identity to bot client
        SendMsg(client->GetPlayer(), &logout, NMFGuaranteed);
    }

    if (_dedicated)
    {
        if (dpid == _gameMaster)
        {
            LOG_INFO(Network, "Admin {} logged out.", (const char*)name);

            _gameMaster = AI_PLAYER;
            _sessionLocked = false;
            _debugOn.Clear();

            if (_state == NGSCreate && _mission.GetLength() == 1 && _mission[0] == '?')
            {
                // enable other players vote mission or continue with mission defined in config
                _mission = "";
                _restart = false;
                _reassign = false;
            }
            UpdateAdminState();
        }
    }
}

void NetworkServer::SetEstimatedEndTime(Time time)
{
    _missionHeader.estimatedEndTime = time;
    _missionHeader.updateOnly = true;

    // send mission description and roles
    for (int i = 0; i < _players.Size(); i++)
    {
        NetworkPlayerInfo& info = _players[i];
        if (info.state < NGSCreate)
        {
            continue;
        }
        SendMsg(info.dpid, &_missionHeader, NMFGuaranteed);
    }
}

NetworkObjectInfo* NetworkServer::GetObjectInfo(NetworkId& id)
{
    for (int i = 0; i < _objects.Size(); i++)
    {
        NetworkObjectInfo& info = *_objects[i];
        if (info.id == id)
        {
            return &info;
        }
    }
    return nullptr;
}

NetworkObjectInfo* NetworkServer::OnObjectCreate(NetworkId& id, int owner, NetworkMessage* msg, NetworkMessageType type)
{
    {
        NetworkObjectInfo* info = GetObjectInfo(id);
        if (info)
        {
            info->owner = owner;
            if (msg && !info->create.message)
            {
                info->create.type = type;
                info->create.from = owner;
                info->create.message = msg;
            }
            return info;
        }
    }

    int index = _objects.Add();
    _objects[index] = new NetworkObjectInfo;
    NetworkObjectInfo& info = *_objects[index];
    info.id = id;
    info.owner = owner;
    if (msg)
    {
        info.create.type = type;
        info.create.from = owner;
        info.create.message = msg;
    }
    return &info;
}

NetworkObjectInfo* NetworkServer::OnObjectUpdate(NetworkId& id, int from, NetworkMessage* msg, NetworkMessageType type,
                                                 NetworkMessageClass cls)
{
    NetworkObjectInfo* oInfo = GetObjectInfo(id);
    if (!oInfo)
    {
        RptF("Server: Object %d:%d not found (message %s)", id.creator, id.id, NetworkMessageTypeNames[type]);
        return nullptr;
    }

    if (oInfo->owner != from)
    {
        LOG_DEBUG(Network, "Update of object {}:{} arrived from nonowner", id.creator, id.id);
        return nullptr;
    }

    NetworkCurrentInfo& info = oInfo->current[cls];

    NetworkMessageFormatBase* format = GetFormat(/*from, */ type);
    if (!format)
    {
        RptF("Server: Bad message %d(%s) format", (int)type, (const char*)NetworkMessageTypeNames[type]);
        return nullptr;
    }

    if (info.message && info.message->time > msg->time)
    {
        // do not update
        if (DiagLevel >= 1)
        {
            DiagLogF("Server: update message is old (%.3f s)", info.message->time - msg->time);
        }
        return nullptr;
    }

    info.from = from;
    info.type = type;
    info.message = msg;

    if (DiagLevel >= 4)
    {
        DiagLogF("Server: object %d:%d updated", id.creator, id.id);
    }

    return oInfo;
}

int NetworkServer::PerformObjectDestroy(const NetworkId& id)
{
    int owner = 0;
    for (int i = 0; i < _objects.Size(); i++)
    {
        NetworkObjectInfo* info = _objects[i];
        if (info->id == id)
        {
            // check all pending messages
            for (int j = 0; j < _pendingMessages.Size(); j++)
            {
                NetPendingMessage& pend = _pendingMessages[j];
                if (pend.info == info)
                {
                    LOG_DEBUG(Network, "Deleted pending message {:x} for object {:x}", pend.msgID,
                              (uintptr_t)pend.info);
                    _pendingMessages.Delete(j);
                    j--;
                }
            }
            owner = info->owner;
            _objects.Delete(i);
            break;
        }
    }
    return owner;
}

void NetworkServer::OnObjectDestroy(const NetworkId& id)
{
    int owner = PerformObjectDestroy(id);
    DeleteObjectMessage msg;
    msg.object = id;

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
        SendMsg(pInfo.dpid, &msg, NMFGuaranteed);
    }
}

int NetworkServer::UpdateObject(NetworkPlayerInfo* pInfo, NetworkObjectInfo* oInfo, NetworkMessageClass cls,
                                NetMsgFlags dwFlags)
{
    if (!oInfo)
    {
        DiagLogF("Server: Object not found");
        return -1;
    }

    NetworkCurrentInfo& info = oInfo->current[cls];
    if (!info.message)
    {
        return -1;
    }

    NetworkMessageType type = info.type;
    int to = pInfo->dpid;

    DWORD dwMsgId = NetworkComponent::SendMsg(to, info.message, type, dwFlags);
    if (dwMsgId == 0xFFFFFFFF)
    {
        return -1;
    }

    // update object info
    NetworkPlayerObjectInfo* poInfo = oInfo->CreatePlayerObjectInfo(to);
    if (dwMsgId == 0)
    {
        // sent as sync message
        if (poInfo->updates[cls].lastCreatedMsg)
        {
            // some update is pending, but we want to send guaranteed update
            // remove the pending one
            NET_ERROR(dwFlags & NMFGuaranteed);
            int index = FindPendingMessage(&poInfo->updates[cls]);
            NET_ERROR(index >= 0);
            _pendingMessages.Delete(index);

            // no more messages to the update may exist now
            NET_ERROR(FindPendingMessage(&poInfo->updates[cls]) < 0);

            poInfo->updates[cls].lastCreatedMsg = nullptr;
            poInfo->updates[cls].lastCreatedMsgId = 0xffffffff;
            poInfo->updates[cls].lastCreatedMsgTime = 0;
        }

        NET_ERROR(poInfo->updates[cls].lastCreatedMsg == nullptr);
        NET_ERROR(poInfo->updates[cls].lastCreatedMsgId == 0xffffffff);

        poInfo->updates[cls].lastCreatedMsgId = 0xFFFFFFFF;
        poInfo->updates[cls].lastCreatedMsgTime = 0;
        poInfo->updates[cls].lastSentMsg = info.message;
    }
    else
    {
        NET_ERROR(dwMsgId == 1);
        // !!! Pointer to static structure - must be process before structure changed
        info.message->objectUpdateInfo = &poInfo->updates[cls];
        info.message->objectServerInfo = oInfo;
        info.message->objectPlayerInfo = poInfo;

        // we want to set lastCreatedMsg
        // if it was already set, we have a conflict
        NET_ERROR(poInfo->updates[cls].lastCreatedMsg == nullptr);
        NET_ERROR(poInfo->updates[cls].lastCreatedMsgId == 0xffffffff);

        poInfo->updates[cls].lastCreatedMsg = info.message;
        poInfo->updates[cls].lastCreatedMsgId = MSGID_REPLACE;
        poInfo->updates[cls].lastCreatedMsgTime = ::GlobalTickCount();
        poInfo->updates[cls].canCancel = (dwFlags & NMFGuaranteed) == 0;

#if LOG_SEND_PROCESS
        LOG_DEBUG(Network, "Server: Update info {:x} marked for send", (uintptr_t)&poInfo->updates[cls]);
#endif
    }
    return info.message->size;
}

// Simplified enumeration of array items
#define FOR_EACH(type, var, array) for (type* var = array.Data(), *end = array.Data() + array.Size(); var < end; var++)

void NetworkServer::DestroyAllObjects()
{
    _objects.Clear();
    _pendingMessages.Clear();
    FOR_EACH(NetworkPlayerInfo, player, _players)
    {
        player->person = NetworkId::Null();
        player->unit = NetworkId::Null();
        player->group = NetworkId::Null();
    }
    _mapPersonUnit.Clear();

    // avoid id duplicity at next missions
}

bool NetworkServer::CheckIntegrity() const
{
    bool ret = true;
    if (!CheckIntegrityOfPendingMessages())
    {
        ret = false;
    }
    return ret;
}

bool NetworkServer::CheckIntegrityOfPendingMessages() const
{
    // check if each pending message is using correct message ID
    bool ret = true;
    for (int p = 0; p < _pendingMessages.Size(); p++)
    {
        const NetPendingMessage& pend = _pendingMessages[p];
        const NetworkObjectInfo* oInfo = pend.info;
        const NetworkPlayerObjectInfo* poInfo = pend.player;
        const NetworkUpdateInfo* uInfo = pend.update;
        // verify update is in player
        if (uInfo < poInfo->updates && uInfo >= poInfo->updates + NMCUpdateN)
        {
            RptF("Pending message %x - wrong update pointer", pend.msgID);
            ret = false;
            continue;
        }
        // verify player exists in given object
        bool found = false;
        for (int i = 0; i < oInfo->playerObjects.Size(); i++)
        {
            if (oInfo->playerObjects[i] == poInfo)
            {
                found = true;
            }
        }
        if (!found)
        {
            RptF("Pending message %x not found in playerObjects", pend.msgID);
            ret = false;
            continue;
        }
        if (uInfo->lastCreatedMsgId != pend.msgID)
        {
            RptF("Pending message invalid ID %x!=%x", uInfo->lastCreatedMsgId, pend.msgID);
            ret = false;
            continue;
        }
    }
    // check if each message that is created but not sent
    // is recorded in pending messages
    for (int o = 0; o < _objects.Size(); o++)
    {
        const NetworkObjectInfo* oInfo = _objects[o];
        for (int i = 0; i < oInfo->playerObjects.Size(); i++)
        {
            NetworkPlayerObjectInfo& poInfo = *oInfo->playerObjects[i];
            for (int j = NMCUpdateFirst; j < NMCUpdateN; j++)
            {
                const NetworkUpdateInfo& info = poInfo.updates[j];
                if (info.lastCreatedMsg && !info.lastSentMsg)
                {
                    // message is created but not sent,
                    // it is pending
                    // check if we can find it in the pending list
                    if (info.lastCreatedMsgId == 0)
                    {
                        // message not created, creation in progress
                    }
                    else if (FindPendingMessage(info.lastCreatedMsgId) < 0)
                    {
                        RptF("Pending message %x not found", info.lastCreatedMsgId);
                        RptF("  info.lastCreatedMsg: %x", info.lastCreatedMsg.GetRef());
                        ret = false;
                    }
                }
            }
        }
    }

    return ret;
}

NetworkMessageFormatBase* NetworkServer::GetFormat(/*int client, */ int type)
{
    // A wire type is untrusted; reject the whole out-of-range span so a negative
    // type (NCTSmallUnsigned decodes a >INT_MAX varint into a negative int) can
    // never index GMsgFormats[] out of bounds.
    if (type < 0 || type >= NMTN)
    {
        return nullptr; // unknown message
    }
    return GMsgFormats[type];
}
