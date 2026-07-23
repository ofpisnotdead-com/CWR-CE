#include <algorithm>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Core/ModSystem.hpp>
#include <Poseidon/Core/Config/Config.hpp>
#include <Poseidon/Network/NetworkClientCommon.hpp>
#include <Poseidon/Network/NetworkFileTransfer.hpp>
#include <Poseidon/Network/NetworkMissionTransfer.hpp>
#include <Poseidon/Network/NetworkPlayerRoleAssignment.hpp>
#include <Poseidon/Network/NetworkScriptValueCodec.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/IO/Filesystem/Utf8Paths.hpp>
// #include "strIncl.hpp"
#include <Poseidon/UI/Locale/StringtableExt.hpp>

#include <Poseidon/AI/ArcadeTemplate.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/Network/XML/Xml.hpp>
#include <Poseidon/Foundation/Strings/StrFormat.hpp>

#include <Poseidon/Foundation/Algorithms/Qsort.hpp>

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

// #include <sys/types.h>
#include <sys/stat.h>
#include <float.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
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
void SetMission(RString, RString, RString);
RString& GetMPMissionsDir();
void SetBaseDirectory(RString);
} // namespace Poseidon

using Poseidon::Foundation::MemAllocSA;
using Poseidon::Foundation::QSort;

namespace
{
int NextRemoteExecSequence()
{
    static int sequence = 0;
    return ++sequence;
}
} // namespace
using Poseidon::Foundation::Time;

// Maximal number of dead bodies simulated by single client

bool NetworkClient::PrepareGame()
{
    LOG_DEBUG(Network, "[PrepareGame] role={} island='{}'", GetMyPlayerRole() != nullptr,
              (const char*)_missionHeader.island);

    if (GetMyPlayerRole() && _missionHeader.island.GetLength() > 0)
    {
        RString fullname = GetWorldName(_missionHeader.island);
        if (!QIFStreamB::FileExist(fullname))
        {
            WarningMessage(LocalizeString(IDS_MSG_NO_WORLD), (const char*)fullname);
            return false;
        }

        SetBaseDirectory("");
        Poseidon::SetMission(_missionHeader.island, "__CUR_MP", Poseidon::GetMPMissionsDir());
        const char* ext = strrchr(_missionHeader.fileName, '.');
        if (ext)
        {
            Glob.header.filenameReal = _missionHeader.fileName.Substring(0, ext - (const char*)_missionHeader.fileName);
        }
        else
        {
            Glob.header.filenameReal = _missionHeader.fileName;
        }
        GWorld->SwitchLandscape(fullname);

        CurrentTemplate.Clear();
        if (GWorld->UI())
        {
            GWorld->UI()->Init();
        }
        Poseidon::ParseMission(true);

        GStats.ClearMission();
        GWorld->ActivateAddons(CurrentTemplate.addOns);
        GWorld->InitGeneral(CurrentTemplate.intel);
        GWorld->AdjustSubdivision(GModeNetware);
        GWorld->InitClient();
        return true;
    }
    LOG_WARN(Network, "[PrepareGame] skipped: role={} island='{}'", GetMyPlayerRole() != nullptr,
             (const char*)_missionHeader.island);
    return false;
}

NetworkId NetworkClient::CreateLocalObject(NetworkObject* object)
{
    NET_ERROR(object);

    // set network id
    NetworkId id(_player, _nextId++);
    object->SetNetworkId(id);

    // add to local objects
#if CHECK_MSG
    CheckLocalObjects();
#endif

    int index = _localObjects.Add();
    NetworkLocalObjectInfo& localObject = _localObjects[index];
    localObject.id = id;
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
        NetworkMessageType type = object->GetNMType();
        DiagLogF("Client: local object created %d:%d (type %s)", id.creator, id.id,
                 (const char*)NetworkMessageTypeNames[type]);
    }

#if CHECK_MSG
    CheckLocalObjects();
#endif

    return id;
}

bool NetworkClient::CreateObject(NetworkObject* object)
{
    if (!object)
    {
        return false;
    }
    if (_state < NGSLoadIsland)
    {
        return false;
    }

    NetworkId id = CreateLocalObject(object);

    // send message to center
    NetworkMessageType msgType = object->GetNMType(NMCCreate);
    NetworkMessageFormatBase* format = GetFormat(/*LOCAL_PLAYER, */ msgType);

    // create message
    Ref<NetworkMessage> msg = new NetworkMessage();
    msg->time = Glob.time;
    NetworkMessageContext ctx(msg, format, this, TO_SERVER, MSG_SEND);
    ctx.SetClass(NMCCreate);
    if (object->TransferMsg(ctx) != TMOK)
    {
        return false;
    }

    // send message
    DWORD dwMsgId = NetworkComponent::SendMsg(TO_SERVER, msg, msgType, NMFGuaranteed);
    return dwMsgId != 0xFFFFFFFF;
}

bool NetworkClient::PublicExec(RString command)
{
    PublicExecMessage msg;
    msg._command = command;
    SendMsg(&msg, NMFGuaranteed);

    return true;
}

bool NetworkClient::RemoteExec(RString name, const AutoArray<char>& params, int target,
                               const AutoArray<char>& targetSpec, bool jip, RString jipKey, bool callMode)
{
    RemoteExecMessage msg;
    msg._name = name;
    msg._params = params;
    msg._target = target;
    msg._targetSpec = targetSpec;
    msg._jip = jip;
    msg._jipKey = jipKey;
    msg._callMode = callMode;
    msg._originator = GetPlayer();
    msg._sequence = NextRemoteExecSequence();
    msg._remove = false;
    SendMsg(&msg, NMFGuaranteed);

    return true;
}

bool NetworkClient::RemoteExecRemove(RString jipKey)
{
    RemoteExecMessage msg;
    msg._jipKey = jipKey;
    msg._originator = GetPlayer();
    msg._sequence = NextRemoteExecSequence();
    msg._remove = true;
    SendMsg(&msg, NMFGuaranteed);

    return true;
}

int NetworkClient::UpdateObject(NetworkObject* object, NetworkMessageClass cls, NetMsgFlags dwFlags)
{
    if (!object)
    {
        return -1;
    }
    if (_state < NGSLoadIsland)
    {
        return -1;
    }

    // find local objects
    NetworkId id = object->GetNetworkId();
    if (id.IsNull())
    {
        RptF("Client: Nonnetwork object %x.", object);
        return -1;
    }
    if (!object->IsLocal())
    {
        RptF("Update of nonlocal object %d:%d called", id.creator, id.id);
        return -1;
    }
#if CHECK_MSG
    CheckLocalObjects();
#endif
    NetworkLocalObjectInfo* localObject = GetLocalObjectInfo(id);
    if (!localObject)
    {
        return -1;
    }

    NetworkUpdateInfo& info = localObject->updates[cls];

    // send message to center
    NetworkMessageType msgType = object->GetNMType(cls);
    if (msgType == NMTNone)
    {
        return -1;
    }

    // FIX
    bool IsDedicatedServer();
    if (msgType == NMTUpdateClientInfo && _parent->GetServer() && IsDedicatedServer())
    {
        return -1;
    }

    NetworkMessageFormatBase* format = GetFormat(/*LOCAL_PLAYER, */ msgType);

    // create message
    Ref<NetworkMessage> msg = new NetworkMessage();
    // must be Ref - msg is stored
    msg->time = Glob.time;
    NetworkMessageContext ctx(msg, format, this, TO_SERVER, MSG_SEND);
    ctx.SetClass(cls);
    if ((dwFlags & NMFGuaranteed) != 0)
    {
        ctx.SetInitialUpdate();
    }
    if (object->TransferMsg(ctx) != TMOK)
    {
        return -1;
    }

    NET_ERROR(dynamic_cast<const IndicesNetworkObject*>(ctx.GetIndices()))
    const IndicesNetworkObject* indices = static_cast<const IndicesNetworkObject*>(ctx.GetIndices());
    bool guaranteed = (dwFlags & NMFGuaranteed) != 0;
    if (ctx.IdxTransfer(indices->guaranteed, guaranteed) != TMOK)
    {
        Fail("Cannot transfer");
        return -1;
    }

#if CHECK_MSG
    NET_ERROR(localObject->object == object);
    NET_ERROR(localObject->id == id);
    if (object)
    {
        NetworkDataTyped<int>* creator = static_cast<NetworkDataTyped<int>*>(msg->values[0].GetRef());
        NetworkDataTyped<int>* id = static_cast<NetworkDataTyped<int>*>(msg->values[1].GetRef());
        LOG_DEBUG(Network, "Message {:x} ({}:{}) assigned to object {:x} ({}:{})", (uintptr_t)msg.GetRef(),
                  creator->value, id->value, (uintptr_t)object, object->GetNetworkId().creator,
                  object->GetNetworkId().id);
    }
#endif

    // send message
    DWORD dwMsgId = NetworkComponent::SendMsg(TO_SERVER, msg, msgType, dwFlags);
    msg->objectUpdateInfo = nullptr;

    if (dwMsgId == 0xFFFFFFFF)
    {
        return -1;
        // update local objects info
    }
    else if (dwMsgId == 0)
    {
        // sent as sync message
        info.lastCreatedMsg = nullptr;
        info.lastCreatedMsgId = 0xFFFFFFFF;
        info.lastCreatedMsgTime = 0;
        info.lastSentMsg = msg;
    }
    else
    {
        NET_ERROR(dwMsgId == 1);
        // !!! Pointer to static structure - must be process before structure changed
        msg->objectUpdateInfo = &info;
        msg->objectServerInfo = nullptr;

        info.lastCreatedMsg = msg;
        info.lastCreatedMsgId = MSGID_REPLACE;
        info.lastCreatedMsgTime = ::GlobalTickCount();
        info.canCancel = (dwFlags & NMFGuaranteed) == 0;
#if LOG_SEND_PROCESS
        LOG_DEBUG(Network, "Client: Update info {:x} marked for send", (uintptr_t)&info);
#endif
    }
#if CHECK_MSG
    CheckLocalObjects();
#endif

    return msg->size;
}

void NetworkClient::UpdateObject(NetworkObject* object, NetMsgFlags dwFlags)
{
    if (!object)
    {
        return;
    }

    // find local objects
    NetworkId id = object->GetNetworkId();
    if (id.IsNull())
    {
        RptF("Client: Nonnetwork object %x.", object);
        return;
    }
#if CHECK_MSG
    CheckLocalObjects();

    if (id.id == 0x24)
        LOG_DEBUG(Network, "Here");
#endif

    NetworkLocalObjectInfo* localObject = GetLocalObjectInfo(id);
    if (!localObject)
    {
        return;
    }

    for (int cls = NMCUpdateFirst; cls < NMCUpdateN; cls++)
    {
        NetworkUpdateInfo& info = localObject->updates[cls];

        // send message to center
        NetworkMessageType msgType = object->GetNMType((NetworkMessageClass)cls);
        if (msgType == NMTNone)
        {
            continue;
        }

        NetworkMessageFormatBase* format = GetFormat(/*LOCAL_PLAYER, */ msgType);

        // create message
        Ref<NetworkMessage> msg = new NetworkMessage();
        // must be Ref - msg is stored
        msg->time = Glob.time;
        NetworkMessageContext ctx(msg, format, this, TO_SERVER, MSG_SEND);
        ctx.SetClass((NetworkMessageClass)cls);
        if ((dwFlags & NMFGuaranteed) != 0)
        {
            ctx.SetInitialUpdate();
        }
        if (object->TransferMsg(ctx) != TMOK)
        {
            return;
        }

        NET_ERROR(dynamic_cast<const IndicesNetworkObject*>(ctx.GetIndices()))
        const IndicesNetworkObject* indices = static_cast<const IndicesNetworkObject*>(ctx.GetIndices());
        bool guaranteed = (dwFlags & NMFGuaranteed) != 0;
        if (ctx.IdxTransfer(indices->guaranteed, guaranteed) != TMOK)
        {
            Fail("Cannot transfer");
            return;
        }

#if CHECK_MSG
        NET_ERROR(localObject->object == object);
        NET_ERROR(localObject->id == id);
        if (object)
        {
            NetworkDataTyped<int>* creator = static_cast<NetworkDataTyped<int>*>(msg->values[0].GetRef());
            NetworkDataTyped<int>* id = static_cast<NetworkDataTyped<int>*>(msg->values[1].GetRef());
            LOG_DEBUG(Network, "Message {:x} ({}:{}) assigned to object {:x} ({}:{})", (uintptr_t)msg.GetRef(),
                      creator->value, id->value, (uintptr_t)object, object->GetNetworkId().creator,
                      object->GetNetworkId().id);
        }
#endif

        // send message
        DWORD dwMsgId = NetworkComponent::SendMsg(TO_SERVER, msg, msgType, dwFlags);
        if (dwMsgId == 0xFFFFFFFF)
        {
            return;
            // update local objects info
        }
        else if (dwMsgId == 0)
        {
            // sent as sync message
            info.lastCreatedMsg = nullptr;
            info.lastCreatedMsgId = 0xFFFFFFFF;
            info.lastCreatedMsgTime = 0;
            info.lastSentMsg = msg;
        }
        else
        {
            NET_ERROR(dwMsgId == 1);
            // !!! Pointer to static structure - must be process before structure changed
            msg->objectUpdateInfo = &info;
            msg->objectServerInfo = nullptr;

            info.lastCreatedMsg = msg;
            info.lastCreatedMsgId = MSGID_REPLACE;
            info.lastCreatedMsgTime = ::GlobalTickCount();
            info.canCancel = (dwFlags & NMFGuaranteed) == 0;
#if LOG_SEND_PROCESS
            LOG_DEBUG(Network, "Client: Update info {:x} marked for send", (uintptr_t)&info);
#endif
        }
    }
#if CHECK_MSG
    CheckLocalObjects();
#endif
}

Time NetworkClient::GetEstimatedEndTime() const
{
    return _missionHeader.estimatedEndTime;
}

void NetworkClient::DestroyRemoteObject(NetworkId id)
{
    for (int i = 0; i < _remoteObjects.Size(); i++)
    {
        NetworkRemoteObjectInfo& info = _remoteObjects[i];
        if (info.id == id)
        {
            if (info.object)
            {
                info.object->DestroyObject();
            }
            if (DiagLevel >= 1)
            {
                DiagLogF("Client: remote object destroyed %d:%d", id.creator, id.id);
            }
            _remoteObjects.Delete(i);
            break;
        }
    }
}

/*
void NetworkClient::RegisterFormats()
{
  for (int mt=NMTFirstVariant; mt<NMTN; mt++)
  {
    NetworkMessageFormatBase *item = GetFormat(LOCAL_PLAYER, mt);
    SendMsg(item, 0, mt, NMFGuaranteed);
  }
  ChangeGameState gs(NGSCreate);
  SendMsg(&gs, 0, 0, NMFGuaranteed);
}
*/

void NetworkClient::SelectPlayer(int player, Person* person, bool respawn)
{
    SelectPlayerMessage msg(player, person->GetNetworkId(), person->Position(), respawn);
    SendMsg(&msg, NMFGuaranteed);

    if (person->IsLocal())
    {
        AIUnit* unit = person->Brain();
        if (unit)
        {
            Transport* veh = unit->GetVehicleIn();
            if (veh)
            {
                if (veh->IsLocal())
                {
                    UpdateObject(veh, NMFGuaranteed);
                }
                else
                {
                    RptF("SelectPlayer - local player in remote vehicle");
                }
            }
            AIGroup* grp = unit->GetGroup();
            if (grp)
            {
                if (grp->IsLocal())
                {
                    UpdateObject(grp, NMFGuaranteed);
                }
                else
                {
                    RptF("SelectPlayer - local player in remote group");
                }
            }
        }
    }
    else
    {
        RptF("SelectPlayer - on remote person");
    }

    if (player == _player)
    {
        GWorld->SwitchCameraTo(person->Brain()->GetVehicle(), CamInternal);
        GWorld->SetPlayerManual(true);
        // old player is now in _playerOn
        GWorld->SwitchPlayerTo(person);
        GWorld->SetRealPlayer(person);
        GWorld->UI()->ResetVehicle(person->Brain()->GetVehicle());

        if (respawn)
        {
            RString name = "onPlayerResurrect.sqs";
            if (QIFStreamB::FileExist(RString("scripts\\") + name))
            {
                GameArrayType arguments;
                arguments.Add(GameValueExt(person));
                Script* script = new Script(name, GameValue(arguments));
                GWorld->StartCameraScript(script);
            }
        }
    }
}

void NetworkClient::AttachPerson(Person* person)
{
    AttachPersonMessage msg(person);
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::PlaySound(RString name, Vector3Par position, Vector3Par speed, float volume, float freq,
                              IWave* wave)
{
    if (_state < NGSPlay)
    {
        return; // send event messages only when playing
    }

    PlaySoundMessage msg;
    msg.name = name;
    msg.position = position;
    msg.speed = speed;
    msg.volume = volume;
    msg.freq = freq;
    msg.soundId = _soundId++;
    msg.creator = _player;
    //  int id = _soundId++;
    SendMsg(&msg, NMFNone);

    // compact _sentSounds
    for (int i = 0; i < _sentSounds.Size();)
    {
        PlaySoundInfo& info = _sentSounds[i];
        if (info.wave)
        {
            i++;
        }
        else
        {
            _sentSounds.Delete(i);
        }
    }

    int index = _sentSounds.Add();
    _sentSounds[index].creator = _player;
    _sentSounds[index].id = msg.soundId;
    _sentSounds[index].wave = wave;
}

void NetworkClient::SoundState(IWave* wave, SoundStateType state)
{
    if (_state < NGSPlay)
    {
        return; // send event messages only when playing
    }

    int creator = -1, id = -1;
    for (int i = 0; i < _sentSounds.Size();)
    {
        PlaySoundInfo& info = _sentSounds[i];
        if (!info.wave)
        {
            _sentSounds.Delete(i);
            continue;
        }
        if (info.wave == wave)
        {
            creator = info.creator;
            id = info.id;
        }
        i++;
    }

    if (id < 0)
    {
        return;
    }

    SoundStateMessage msg;
    msg.state = state;
    msg.creator = creator;
    msg.soundId = id;
    SendMsg(&msg, NMFNone);
}

void NetworkClient::CreateAllObjects()
{
    // pass 2
    for (int i = 0; i < GWorld->NVehicles(); i++)
    {
        Vehicle* veh = GWorld->GetVehicle(i);
        if (veh)
        {
            UpdateObject(veh, NMFGuaranteed);
        }
    }
    for (int i = 0; i < GWorld->NAnimals(); i++)
    {
        Vehicle* veh = GWorld->GetAnimal(i);
        if (veh)
        {
            UpdateObject(veh, NMFGuaranteed);
        }
    }
    for (int i = 0; i < GWorld->NBuildings(); i++)
    {
        Vehicle* veh = GWorld->GetBuilding(i);
        if (veh)
        {
            UpdateObject(veh, NMFGuaranteed);
        }
    }
    for (int i = 0; i < GWorld->NCloudlets(); i++)
    {
        Vehicle* veh = GWorld->GetCloudlet(i);
        if (veh)
        {
            UpdateObject(veh, NMFGuaranteed);
        }
    }
    for (int i = 0; i < GWorld->NFastVehicles(); i++)
    {
        Vehicle* veh = GWorld->GetFastVehicle(i);
        if (veh)
        {
            UpdateObject(veh, NMFGuaranteed);
        }
    }
    for (int i = 0; i < GWorld->NOutVehicles(); i++)
    {
        Vehicle* veh = GWorld->GetOutVehicle(i);
        if (veh)
        {
            UpdateObject(veh, NMFGuaranteed);
        }
    }

    // AI structure
    AICenter* center = GWorld->GetEastCenter();
    if (center)
    {
        UpdateCenter(center);
    }
    center = GWorld->GetWestCenter();
    if (center)
    {
        UpdateCenter(center);
    }
    center = GWorld->GetGuerrilaCenter();
    if (center)
    {
        UpdateCenter(center);
    }
    center = GWorld->GetCivilianCenter();
    if (center)
    {
        UpdateCenter(center);
    }
    center = GWorld->GetLogicCenter();
    if (center)
    {
        UpdateCenter(center);
    }

    ChangeGameState gs(NGSBriefing);
    SendMsg(&gs, NMFGuaranteed);
}

void NetworkClient::DestroyAllObjects()
{
    _localObjects.Clear();
    _remoteObjects.Clear();
    if (DiagLevel >= 1)
    {
        DiagLogF("Client: all objects destroyed");
    }

    // avoid id duplicity at next missions
}

void NetworkClient::ClientReady(NetworkGameState state)
{
    ChangeGameState gs(state);
    SendMsg(&gs, NMFGuaranteed);
}

bool NetworkClient::CreateVehicle(Vehicle* veh, VehicleListType list, RString name, int idVeh)
{
    if (!veh)
    {
        return false;
    }
    if (_state < NGSLoadIsland)
    {
        return false;
    }

    NetworkId id = CreateLocalObject(veh);

    // send message to center
    NetworkMessageType msgType = veh->GetNMType(NMCCreate);
    NetworkMessageFormatBase* format = GetFormat(/*LOCAL_PLAYER, */ msgType);

    // create message
    Ref<NetworkMessage> msg = new NetworkMessage();
    msg->time = Glob.time;
    NetworkMessageContext ctx(msg, format, this, TO_SERVER, MSG_SEND);
    ctx.SetClass(NMCCreate);

    // difference from CreateObject - vehicle doesnot known its list, name, id:
    NET_ERROR(dynamic_cast<const IndicesCreateVehicle*>(ctx.GetIndices()))
    const IndicesCreateVehicle* indices = static_cast<const IndicesCreateVehicle*>(ctx.GetIndices());

    if (ctx.IdxTransfer(indices->list, (int&)list) != TMOK)
    {
        return false;
    }
    if (ctx.IdxTransfer(indices->name, name) != TMOK)
    {
        return false;
    }
    if (ctx.IdxTransfer(indices->idVehicle, idVeh) != TMOK)
    {
        return false;
    }

    if (veh->TransferMsg(ctx) != TMOK)
    {
        return false;
    }

    // send message
    DWORD dwMsgId = NetworkComponent::SendMsg(TO_SERVER, msg, msgType, NMFGuaranteed);
    return dwMsgId != 0xFFFFFFFF;
}

bool NetworkClient::CreateCommand(AISubgroup* subgrp, int index, Command* cmd)
{
    if (_state < NGSLoadIsland)
    {
        return false;
    }

    NetworkId id = CreateLocalObject(cmd);

    // send message to center
    NetworkMessageType msgType = cmd->GetNMType(NMCCreate);
    NetworkMessageFormatBase* format = GetFormat(/*LOCAL_PLAYER, */ msgType);

    // create message
    Ref<NetworkMessage> msg = new NetworkMessage();
    msg->time = Glob.time;
    NetworkMessageContext ctx(msg, format, this, TO_SERVER, MSG_SEND);
    ctx.SetClass(NMCCreate);

    // difference from CreateObject - command doesnot known its subgrp, index
    NET_ERROR(dynamic_cast<const IndicesCreateCommand*>(ctx.GetIndices()))
    const IndicesCreateCommand* indices = static_cast<const IndicesCreateCommand*>(ctx.GetIndices());

    if (ctx.IdxTransferRef(indices->subgroup, subgrp) != TMOK)
    {
        return false;
    }
    if (ctx.IdxTransfer(indices->index, index) != TMOK)
    {
        return false;
    }
    if (cmd->TransferMsg(ctx) != TMOK)
    {
        return false;
    }

    // send message
    DWORD dwMsgId = NetworkComponent::SendMsg(TO_SERVER, msg, msgType, NMFGuaranteed);
    return dwMsgId != 0xFFFFFFFF;
}

void NetworkClient::DeleteCommand(AISubgroup* subgrp, int index, Command* cmd)
{
    const NetworkId& id = cmd->GetNetworkId();
    if (id.IsNull())
    {
        return;
    }

    DeleteCommandMessage msg(subgrp, index, id);
    SendMsg(&msg, NMFGuaranteed);

#if CHECK_MSG
    CheckLocalObjects();
#endif

    for (int i = 0; i < _localObjects.Size(); i++)
    {
        NetworkLocalObjectInfo& info = _localObjects[i];
        if (info.id == id)
        {
            _localObjects.Delete(i);
            if (DiagLevel >= 1)
            {
                DiagLogF("Client: command deleted %d:%d", id.creator, id.id);
            }
            return;
        }
    }
    LOG_DEBUG(Network, "Warning: Client: Command info {}:{} not found.", id.creator, id.id);

#if CHECK_MSG
    CheckLocalObjects();
#endif
}

bool NetworkClient::CreateCenter(AICenter* center)
{
    NET_ERROR(center);
    if (!CreateObject(center))
    {
        return false;
    }

    for (int g = 0; g < center->NGroups(); g++)
    {
        AIGroup* grp = center->GetGroup(g);
        if (!grp)
        {
            continue;
        }
        if (!CreateObject(grp))
        {
            return false;
        }
        for (int s = 0; s < grp->NSubgroups(); s++)
        {
            AISubgroup* subgrp = grp->GetSubgroup(s);
            if (!subgrp)
            {
                continue;
            }
            if (!CreateObject(subgrp))
            {
                return false;
            }
            for (int u = 0; u < subgrp->NUnits(); u++)
            {
                AIUnit* unit = subgrp->GetUnit(u);
                if (!unit)
                {
                    continue;
                }
                if (!CreateObject(unit))
                {
                    return false;
                }
            }
        }
    }
    return true;
}

bool NetworkClient::UpdateCenter(AICenter* center)
{
    // order - units, subgroups, groups, center
    // because of ownership changes
    NET_ERROR(center);
    for (int g = 0; g < center->NGroups(); g++)
    {
        AIGroup* grp = center->GetGroup(g);
        if (!grp)
        {
            continue;
        }
        for (int s = 0; s < grp->NSubgroups(); s++)
        {
            AISubgroup* subgrp = grp->GetSubgroup(s);
            if (!subgrp)
            {
                continue;
            }
            for (int u = 0; u < subgrp->NUnits(); u++)
            {
                AIUnit* unit = subgrp->GetUnit(u);
                if (!unit)
                {
                    continue;
                }
                UpdateObject(unit, NMFGuaranteed);
            }
            UpdateObject(subgrp, NMFGuaranteed);
        }
        UpdateObject(grp, NMFGuaranteed);
    }
    UpdateObject(center, NMFGuaranteed);

    for (int g = 0; g < center->NGroups(); g++)
    {
        AIGroup* grp = center->GetGroup(g);
        if (!grp)
        {
            continue;
        }
        for (int s = 0; s < grp->NSubgroups(); s++)
        {
            AISubgroup* subgrp = grp->GetSubgroup(s);
            if (!subgrp)
            {
                continue;
            }
            for (int u = 0; u < subgrp->NUnits(); u++)
            {
                AIUnit* unit = subgrp->GetUnit(u);
                if (!unit)
                {
                    continue;
                }

                int player = unit->GetPerson()->GetRemotePlayer();
                if (player != 1)
                {
                    SelectPlayer(player, unit->GetPerson());
                }
            }
        }
    }

    return true;
}

// Network Command Types
enum NetworkCommandType
{
    CMDNone = -1,
    CMDLogin,
    CMDLogout,
    CMDKick,
    CMDRestart,
    CMDMission,
    CMDMissions,
    CMDShutdown,
    CMDReassign,
    CMDMonitor,
    CMDUserlist,
    CMDVote,
    CMDAdmin,
    CMDInit,
    CMDDebug,
    CMDBan,
    CMDUnban,
    CMDLock,
    CMDUnlock
};

// Maps text form of Network Command to Network Command Type
struct NetworkCommand
{
    // text form
    const char* command;
    // Network Command Type
    NetworkCommandType type;
};

// Maps text form of Network Commands to Network Command Types

static NetworkCommand NetworkCommands[] = {
    {"login", CMDLogin},     {"logout", CMDLogout},     {"kick", CMDKick},         {"restart", CMDRestart},
    {"mission", CMDMission}, {"missions", CMDMissions}, {"shutdown", CMDShutdown}, {"reassign", CMDReassign},
    {"monitor", CMDMonitor}, {"userlist", CMDUserlist}, {"vote", CMDVote},         {"admin", CMDAdmin},
    {"init", CMDInit},       {"debug", CMDDebug},       {"ban", CMDBan},           {"unban", CMDUnban},
    {"lock", CMDLock},       {"unlock", CMDUnlock}};

// Find Network Command Type for text form of Network Command
static NetworkCommandType FindCommandType(const char* beg, int len)
{
    for (int i = 0; i < sizeof(NetworkCommands) / sizeof(NetworkCommand); i++)
    {
        NetworkCommand& item = NetworkCommands[i];
        if (strlen(item.command) == len && strnicmp(beg, item.command, len) == 0)
        {
            return item.type;
        }
    }
    return CMDNone;
}

// Find DirectPlay ID of player for text form of player
static int FindPlayerId(const char* name, AutoArray<PlayerIdentity>& identities)
{
    for (int i = 0; i < identities.Size(); i++)
    {
        if (stricmp(name, identities[i].name) == 0)
        {
            return identities[i].dpnid;
        }
    }
    int playerid = atoi(name);
    for (int i = 0; i < identities.Size(); i++)
    {
        if (identities[i].playerid == playerid)
        {
            return identities[i].dpnid;
        }
    }
    return AI_PLAYER; // not found
}
RString GetIdentityText(const PlayerIdentity& identity)
{
    RString spec = Format("%d ms", identity._avgPing);
    if (identity._rights & PRServer)
    {
        spec = LocalizeString(IDS_MP_SERVER);
    }
    else if (identity._rights & (PRAdmin | PRVotedAdmin))
    {
        if (identity._rights & PRVotedAdmin)
        {
            spec = spec + RString(", ") + LocalizeString(IDS_MP_MASTER);
        }
        else
        {
            spec = spec + RString(", *") + LocalizeString(IDS_MP_MASTER) + RString("*");
        }
    }
    return Format("%s (%s)", (const char*)identity.name, (const char*)spec);
}

void NetworkClient::SendKick(int player)
{
    NetworkCommandMessage msg;
    msg.type = NCMTKick;
    msg.content.Write(&player, sizeof(int));
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::SendLockSession(bool lock)
{
    NetworkCommandMessage msg;
    msg.type = NCMTLockSession;
    msg.content.Write(&lock, sizeof(bool));
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::SendBan(int player)
{
    NetworkCommandMessage msg;
    msg.type = NCMTBan;
    msg.content.Write(&player, sizeof(int));
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::SendUnban(const char* idOrIp)
{
    NetworkCommandMessage msg;
    msg.type = NCMTUnban;
    msg.content.WriteString(idOrIp);
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::Disconnect(RString message)
{
    LOG_DEBUG(Network, "[disconnect] NetworkClient::Disconnect: msg='{}' serverState={} state={}", (const char*)message,
              (int)_serverState, (int)_state);
    RemoveUserMessages();

    _state = NGSNone;
    _serverState = NGSNone;

    // Leaving the server: difficulty reverts to the local profile.
    USER_CONFIG.ClearServerDifficulty();

    const PlayerIdentity* id = FindIdentity(_player);
    if (id)
    {
        RString senderName = id->name;
        GChatList.Add(CCGlobal, senderName, message, false, true);
        RefArray<NetworkObject> dummy;
        GNetworkManager.Chat(CCGlobal, senderName, dummy, message);
    }
}

bool NetworkClient::ProcessCommand(RString command)
{
    const char* beg = command;
    if (*beg != '#')
    {
        return false;
    }
    beg++;

    const char* end = strchr(beg, ' ');
    int len = end ? end - beg : strlen(beg);
    NetworkCommandType type = FindCommandType(beg, len);
    beg += len;
    while (*beg == ' ')
    {
        beg++;
    }

    switch (type)
    {
        case CMDNone:
        case CMDAdmin:
            break;
        case CMDLogin:
        {
            NetworkCommandMessage msg;
            msg.type = NCMTLogin;
            msg.content.WriteString(beg);
            SendMsg(&msg, NMFGuaranteed);
        }
        break;
        case CMDLogout:
            if (_gameMaster)
            {
                NetworkCommandMessage msg;
                msg.type = NCMTLogout;
                SendMsg(&msg, NMFGuaranteed);
            }
            break;
        case CMDKick:
            if (_gameMaster || GetNetworkManager().IsServer())
            {
                int id = FindPlayerId(beg, _identities);
                if (id != AI_PLAYER)
                {
                    SendKick(id);
                }
            }
            break;
        case CMDBan:
            if (_gameMaster || GetNetworkManager().IsServer())
            {
                int id = FindPlayerId(beg, _identities);
                if (id != AI_PLAYER)
                {
                    SendBan(id);
                }
            }
            break;
        case CMDUnban:
            if ((_gameMaster || GetNetworkManager().IsServer()) && *beg)
            {
                SendUnban(beg);
            }
            break;
        case CMDLock:
            if (_gameMaster || GetNetworkManager().IsServer())
            {
                SendLockSession(true);
            }
            break;
        case CMDUnlock:
            if (_gameMaster || GetNetworkManager().IsServer())
            {
                SendLockSession(false);
            }
            break;
        case CMDRestart:
            if (_gameMaster)
            {
                NetworkCommandMessage msg;
                msg.type = NCMTRestart;
                SendMsg(&msg, NMFGuaranteed);
            }
            break;
        case CMDMission:
            if (_gameMaster)
            {
                NetworkCommandMessage msg;
                msg.type = NCMTMission;
                msg.content.WriteString(beg);
                bool cadetMode = false;
                msg.content.Write(&cadetMode, sizeof(bool));
                SendMsg(&msg, NMFGuaranteed);
            }
            break;
        case CMDMissions:
            if (_gameMaster)
            {
                NetworkCommandMessage msg;
                msg.type = NCMTMissions;
                SendMsg(&msg, NMFGuaranteed);
            }
            break;
        case CMDShutdown:
            if (_gameMaster)
            {
                NetworkCommandMessage msg;
                msg.type = NCMTShutdown;
                SendMsg(&msg, NMFGuaranteed);
            }
            break;
        case CMDReassign:
            if (_gameMaster)
            {
                NetworkCommandMessage msg;
                msg.type = NCMTReassign;
                SendMsg(&msg, NMFGuaranteed);
            }
            break;
        case CMDMonitor:
            if (_gameMaster)
            {
                NetworkCommandMessage msg;
                msg.type = NCMTMonitorAsk;
                float value = 10.0f;
                if (*beg)
                {
                    value = atof(beg);
                }
                msg.content.Write(&value, sizeof(value));
                SendMsg(&msg, NMFGuaranteed);
            }
            break;
        case CMDUserlist:
            for (int i = 0; i < _identities.Size(); i++)
            {
                const PlayerIdentity& identity = _identities[i];
                char buffer[256];
                snprintf(buffer, sizeof(buffer), "%d: %s (id = %s)", identity.playerid,
                         (const char*)GetIdentityText(identity), (const char*)identity.id);
                GChatList.Add(CCGlobal, nullptr, buffer, false, true);
            }
            break;
        case CMDInit:
            if (_gameMaster)
            {
                NetworkCommandMessage msg;
                msg.type = NCMTInit;
                SendMsg(&msg, NMFGuaranteed);
            }
            break;
        case CMDDebug:
            if (_gameMaster || GetNetworkManager().IsServer())
            {
                NetworkCommandMessage msg;
                msg.type = NCMTDebugAsk;
                msg.content.WriteString(beg);
                SendMsg(&msg, NMFGuaranteed);
            }
            break;
        case CMDVote:
        {
            end = strchr(beg, ' ');
            len = end ? end - beg : strlen(beg);
            type = FindCommandType(beg, len);
            beg += len;
            while (*beg == ' ')
            {
                beg++;
            }

            switch (type)
            {
                case CMDKick:
                {
                    int id = FindPlayerId(beg, _identities);
                    if (id != AI_PLAYER)
                    {
                        NetworkCommandMessage msg;
                        msg.type = NCMTVote;
                        int subtype = NCMTKick;
                        msg.content.Write(&subtype, sizeof(int));
                        msg.content.Write(&id, sizeof(int));
                        SendMsg(&msg, NMFGuaranteed);
                    }
                }
                break;
                case CMDRestart:
                {
                    NetworkCommandMessage msg;
                    msg.type = NCMTVote;
                    int subtype = NCMTRestart;
                    msg.content.Write(&subtype, sizeof(int));
                    SendMsg(&msg, NMFGuaranteed);
                }
                break;
                case CMDReassign:
                {
                    NetworkCommandMessage msg;
                    msg.type = NCMTVote;
                    int subtype = NCMTReassign;
                    msg.content.Write(&subtype, sizeof(int));
                    SendMsg(&msg, NMFGuaranteed);
                }
                break;
                case CMDMission:
                {
                    NetworkCommandMessage msg;
                    msg.type = NCMTVote;
                    int subtype = NCMTMission;
                    msg.content.Write(&subtype, sizeof(int));
                    RString name = beg;
                    name.Lower();
                    msg.content.WriteString(name);
                    bool cadetMode = false;
                    msg.content.Write(&cadetMode, sizeof(bool));
                    SendMsg(&msg, NMFGuaranteed);
                }
                break;
                case CMDMissions:
                {
                    NetworkCommandMessage msg;
                    msg.type = NCMTVote;
                    int subtype = NCMTMissions;
                    msg.content.Write(&subtype, sizeof(int));
                    SendMsg(&msg, NMFGuaranteed);
                }
                break;
                case CMDAdmin:
                {
                    int id = FindPlayerId(beg, _identities);
                    if (id != AI_PLAYER)
                    {
                        NetworkCommandMessage msg;
                        msg.type = NCMTVote;
                        int subtype = NCMTAdmin;
                        msg.content.Write(&subtype, sizeof(int));
                        msg.content.Write(&id, sizeof(int));
                        SendMsg(&msg, NMFGuaranteed);
                    }
                }
                break;
            }
        }
        break;
    }
    return true;
}

void NetworkClient::SelectMission(RString mission, bool cadetMode)
{
    if (_gameMaster)
    {
        NetworkCommandMessage msg;
        msg.type = NCMTMission;
        msg.content.WriteString(mission);
        msg.content.Write(&cadetMode, sizeof(bool));
        SendMsg(&msg, NMFGuaranteed);
    }
    _selectMission = false;
    _voteMission = false;
}

void NetworkClient::VoteMission(RString mission, bool cadetMode)
{
    NetworkCommandMessage msg;
    msg.type = NCMTVote;
    int subtype = NCMTMission;
    msg.content.Write(&subtype, sizeof(int));
    msg.content.WriteString(mission);
    msg.content.Write(&cadetMode, sizeof(bool));
    SendMsg(&msg, NMFGuaranteed);

    _voteMission = false;
}

DWORD NetworkClient::SendMsg(NetworkSimpleObject* object, NetMsgFlags dwFlags)
{
    return NetworkComponent::SendMsg(TO_SERVER, object, dwFlags);
}

void NetworkClient::EnqueueMsg(int to, NetworkMessage* msg, NetworkMessageType type)
{
    int index = _messageQueue.Add();
    _messageQueue[index].type = type;
    _messageQueue[index].msg = msg;
}

void NetworkClient::EnqueueMsgNonGuaranteed(int to, NetworkMessage* msg, NetworkMessageType type)
{
    int index = _messageQueueNonGuaranteed.Add();
    _messageQueueNonGuaranteed[index].type = type;
    _messageQueueNonGuaranteed[index].msg = msg;
}

NetworkMessageFormatBase* NetworkClient::GetFormat(/*int client, */ int type)
{
    // A wire type is untrusted; a negative type (NCTSmallUnsigned decodes a >INT_MAX
    // varint into a negative int) must not index GMsgFormats[] below 0.
    if (type < 0)
    {
        return nullptr;
    }
    if (type < NMTFirstVariant)
    {
        // static format
        return GMsgFormats[type];
    }
    else
    {
        // dynamic format
        int index = type - NMTFirstVariant;
        if (index >= _formats.Size())
        {
            return nullptr;
        }
        return &_formats[index];
    }
}

void NetworkClient::GetPlayers(AutoArray<NetPlayerInfo, MemAllocSA>& players)
{
    players.Resize(0);
    for (int i = 0; i < _identities.Size(); i++)
    {
        int index = players.Add();
        players[index].dpid = _identities[i].dpnid;
        players[index].name = _identities[i].GetName();
    }
}

void NetworkClient::AssignPlayer(int role, int player)
{
    if (role < 0 || role >= _playerRoles.Size())
    {
        return;
    }

    if (_playerRoles[role].player == player)
    {
        return;
    }

    PlayerRole info = Poseidon::BuildNetworkPlayerRoleAssignmentRequest(_playerRoles[role], player);

    // create message
    NetworkMessageType type = info.GetNMType(NMCCreate);
    NetworkMessageFormatBase* format = GetFormat(type);
    Ref<NetworkMessage> msg = new NetworkMessage();
    msg->time = Glob.time;
    NetworkMessageContext ctx(msg, format, this, TO_SERVER, MSG_SEND);

    NET_ERROR(dynamic_cast<const IndicesPlayerRole*>(ctx.GetIndices()))
    const IndicesPlayerRole* indices = static_cast<const IndicesPlayerRole*>(ctx.GetIndices());

    TMError err;
    err = ctx.IdxTransfer(indices->index, role);
    if (err != TMOK)
    {
        return;
    }
    err = info.TransferMsg(ctx);
    if (err != TMOK)
    {
        return;
    }

    // send message
    NetworkComponent::SendMsg(TO_SERVER, msg, type, NMFGuaranteed);
}

void NetworkClient::AskForDammage(Object* who, EntityAI* owner, Vector3Par modelPos, float val, float valRange,
                                  RString ammo)
{
    if (_state < NGSPlay)
    {
        return; // send event messages only when playing
    }

    AskForDammageMessage msg;
    msg.who = who;
    msg.owner = owner;
    msg.modelPos = modelPos;
    msg.val = val;
    msg.valRange = valRange;
    msg.ammo = ammo;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskForSetDammage(Object* who, float dammage)
{
    if (_state < NGSPlay)
    {
        return; // send event messages only when playing
    }

    AskForSetDammageMessage msg;
    msg.who = who;
    msg.dammage = dammage;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskForGetIn(Person* soldier, Transport* vehicle, GetInPosition position)
{
    if (_state < NGSPlay)
    {
        return; // send event messages only when playing
    }
    /*
    RptF
    (
      "Asking for get in: %s (%s, id %d:%d) into %s (%s, id %d:%d)",
      (const char *)soldier->GetDebugName(), soldier->IsLocal() ? "LOCAL" : "REMOTE",
      soldier->GetNetworkId().creator, soldier->GetNetworkId().id,
      (const char *)vehicle->GetDebugName(), vehicle->IsLocal() ? "LOCAL" : "REMOTE",
      vehicle->GetNetworkId().creator, vehicle->GetNetworkId().id
    );
    */
    AskForGetInMessage msg;
    msg.soldier = soldier;
    msg.vehicle = vehicle;
    msg.position = position;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskForGetOut(Person* soldier, Transport* vehicle, bool parachute)
{
    if (_state < NGSPlay)
    {
        return; // send event messages only when playing
    }
    /*
    RptF
    (
      "Asking for get out: %s (%s, id %d:%d) from %s (%s, id %d:%d)",
      (const char *)soldier->GetDebugName(), soldier->IsLocal() ? "LOCAL" : "REMOTE",
      soldier->GetNetworkId().creator, soldier->GetNetworkId().id,
      (const char *)vehicle->GetDebugName(), vehicle->IsLocal() ? "LOCAL" : "REMOTE",
      vehicle->GetNetworkId().creator, vehicle->GetNetworkId().id
    );
    */

    AskForGetOutMessage msg;
    msg.soldier = soldier;
    msg.vehicle = vehicle;
    msg.parachute = parachute;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskForChangePosition(Person* soldier, Transport* vehicle, UIActionType type)
{
    if (_state < NGSPlay)
    {
        return; // send event messages only when playing
    }

    AskForChangePositionMessage msg;
    msg.soldier = soldier;
    msg.vehicle = vehicle;
    msg.type = type;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskForAimWeapon(EntityAI* vehicle, int weapon, Vector3Par dir)
{
    if (_state < NGSPlay)
    {
        return; // send event messages only when playing
    }

    AskForAimWeaponMessage msg;
    msg.vehicle = vehicle;
    msg.weapon = weapon;
    msg.dir = dir;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskForAimObserver(EntityAI* vehicle, Vector3Par dir)
{
    if (_state < NGSPlay)
    {
        return; // send event messages only when playing
    }

    AskForAimObserverMessage msg;
    msg.vehicle = vehicle;
    msg.dir = dir;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskForSelectWeapon(EntityAI* vehicle, int weapon)
{
    if (_state < NGSPlay)
    {
        return; // send event messages only when playing
    }

    AskForSelectWeaponMessage msg;
    msg.vehicle = vehicle;
    msg.weapon = weapon;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskForAmmo(EntityAI* vehicle, int weapon, int burst)
{
    if (_state < NGSPlay)
    {
        return; // send event messages only when playing
    }

    AskForAmmoMessage msg;
    msg.vehicle = vehicle;
    msg.weapon = weapon;
    msg.burst = burst;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskForAddImpulse(Vehicle* vehicle, Vector3Par force, Vector3Par torque)
{
    if (_state < NGSPlay)
    {
        return; // send event messages only when playing
    }

    AskForAddImpulseMessage msg;
    msg.vehicle = vehicle;
    msg.force = force;
    msg.torque = torque;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskForMove(Object* vehicle, Vector3Par pos)
{
    if (_state < NGSPlay)
    {
        return; // send event messages only when playing
    }

    AskForMoveVectorMessage msg;
    msg.vehicle = vehicle;
    msg.pos = pos;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskForMove(Object* vehicle, Matrix4Par trans)
{
    if (_state < NGSPlay)
    {
        return; // send event messages only when playing
    }

    AskForMoveMatrixMessage msg;
    msg.vehicle = vehicle;
    msg.pos = trans.Position();
    msg.orient = trans.Orientation();
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskForJoin(AIGroup* join, AIGroup* group)
{
    if (_state < NGSPlay)
    {
        return; // send event messages only when playing
    }

    AskForJoinGroupMessage msg;
    msg.join = join;
    msg.group = group;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskForJoin(AIGroup* join, OLinkArray<AIUnit>& units)
{
    if (_state < NGSPlay)
    {
        return; // send event messages only when playing
    }

    AskForJoinUnitsMessage msg;
    msg.join = join;
    msg.units = units;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskForHideBody(Person* vehicle)
{
    if (_state < NGSPlay)
    {
        return; // send event messages only when playing
    }

    AskForHideBodyMessage msg;
    msg.vehicle = vehicle;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::ExplosionDammageEffects(EntityAI* owner, Shot* shot, Object* directHit, Vector3Par pos,
                                            Vector3Par dir, const AmmoType* type, bool enemyDammage)
{
    if (_state < NGSPlay)
    {
        return; // send event messages only when playing
    }

    ExplosionDammageEffectsMessage msg;
    msg.owner = owner;
    msg.shot = shot;
    msg.directHit = directHit;
    msg.pos = pos;
    msg.dir = dir;
    msg.type = type->GetName();
    msg.enemyDammage = enemyDammage;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::FireWeapon(EntityAI* vehicle, int weapon, const Magazine* magazine, EntityAI* target)
{
    if (_state < NGSPlay)
    {
        return; // send event messages only when playing
    }

    if (!magazine)
    {
        return;
    }

    FireWeaponMessage msg;
    msg.vehicle = vehicle;
    msg.target = target;
    msg.weapon = weapon;
    msg.magazineCreator = magazine->_creator;
    msg.magazineId = magazine->_id;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::UpdateWeapons(EntityAI* vehicle)
{
    UpdateWeaponsMessage msg;
    msg.vehicle = vehicle;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AddWeaponCargo(VehicleSupply* vehicle, RString weapon)
{
    AddWeaponCargoMessage msg;
    msg.vehicle = vehicle;
    msg.weapon = weapon;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::RemoveWeaponCargo(VehicleSupply* vehicle, RString weapon)
{
    RemoveWeaponCargoMessage msg;
    msg.vehicle = vehicle;
    msg.weapon = weapon;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AddMagazineCargo(VehicleSupply* vehicle, const Magazine* magazine)
{
    AddMagazineCargoMessage msg;
    msg.vehicle = vehicle;
    msg.magazine = const_cast<Magazine*>(magazine);
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::RemoveMagazineCargo(VehicleSupply* vehicle, int creator, int id)
{
    RemoveMagazineCargoMessage msg;
    msg.vehicle = vehicle;
    msg.creator = creator;
    msg.id = id;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::VehicleInit(VehicleInitCmd& init)
{
    SendMsg(&init, NMFGuaranteed);
}

void NetworkClient::OnVehicleDestroyed(EntityAI* killed, EntityAI* killer)
{
    if (_state < NGSPlay)
    {
        return; // send event messages only when playing
    }

    VehicleDestroyedMessage msg;
    msg.killed = killed;
    msg.killer = killer;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::OnVehicleDamaged(EntityAI* damaged, EntityAI* killer, float damage, RString ammo)
{
    if (_state < NGSPlay)
    {
        return;
    }

    VehicleDamagedMessage msg;
    msg._damaged = damaged;
    msg._killer = killer;
    msg._damage = damage;
    msg._ammo = ammo;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::OnIncomingMissile(EntityAI* target, RString ammo, EntityAI* owner)
{
    if (_state < NGSPlay)
    {
        return;
    }

    IncomingMissileMessage msg;
    msg._target = target;
    msg._ammo = ammo;
    msg._owner = owner;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::MarkerCreate(int channel, AIUnit* sender, RefArray<NetworkObject>& units, ArcadeMarkerInfo& info)
{
    if (_state < NGSLoadIsland)
    {
        return;
    }

    MarkerCreateMessage msg;
    msg.marker = info;
    msg.channel = channel;
    msg.sender = sender;
    msg.units = units;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::MarkerDelete(RString name)
{
    if (_state < NGSLoadIsland)
    {
        return;
    }

    MarkerDeleteMessage msg;
    msg.name = name;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::SetFlagOwner(Person* owner, EntityAI* carrier)
{
    SetFlagOwnerMessage msg;
    msg.owner = owner;
    msg.carrier = carrier;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::SetFlagCarrier(Person* owner, EntityAI* carrier)
{
    SetFlagCarrierMessage msg;
    msg.owner = owner;
    msg.carrier = carrier;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::PublicVariable(RString name)
{
    GameState* gstate = GWorld->GetGameState();
    GameValuePar var = gstate->VarGet(name);
    if (var.GetNil())
    {
        return;
    }

    QOStream out;
    if (!Poseidon::SerializeScriptValue(out, var))
    {
        Fail("publicVariable: unsupported type");
        return;
    }

    PublicVariableMessage msg;
    msg._name = name;
    int size = out.pcount();
    msg._value.Resize(size);
    memcpy(msg._value.Data(), out.str(), size);
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::GroupSynchronization(AIGroup* grp, int synchronization, bool active)
{
    GroupSynchronizationMessage msg;
    msg._group = grp;
    msg._synchronization = synchronization;
    msg._active = active;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::DetectorActivation(Detector* det, bool active)
{
    DetectorActivationMessage msg;
    msg._detector = det;
    msg._active = active;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskForCreateUnit(AIGroup* group, RString type, Vector3Par position, RString init, float skill,
                                     Rank rank)
{
    AskForCreateUnitMessage msg;
    msg._group = group;
    msg._type = type;
    msg._position = position;
    msg._init = init;
    msg._skill = skill;
    msg._rank = rank;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskForDeleteVehicle(Entity* veh)
{
    AskForDeleteVehicleMessage msg;
    msg._vehicle = veh;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskForReceiveUnitAnswer(AIUnit* from, AISubgroup* to, int answer)
{
    AskForReceiveUnitAnswerMessage msg;
    msg._from = from;
    msg._to = to;
    msg._answer = answer;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskForGroupRespawn(Person* person, EntityAI* killer)
{
    AskForGroupRespawnMessage msg;
    msg._person = person;
    msg._killer = killer;
    msg._group = person->Brain()->GetGroup();
    msg._from = GetPlayer();
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskForActivateMine(Mine* mine, bool activate)
{
    AskForActivateMineMessage msg;
    msg._mine = mine;
    msg._activate = activate;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskForInflameFire(Fireplace* fireplace, bool fire)
{
    AskForInflameFireMessage msg;
    msg._fireplace = fireplace;
    msg._fire = fire;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::AskForAnimationPhase(Entity* vehicle, RString animation, float phase)
{
    AskForAnimationPhaseMessage msg;
    msg._vehicle = vehicle;
    msg._animation = animation;
    msg._phase = phase;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::CopyUnitInfo(Person* from, Person* to)
{
    CopyUnitInfoMessage msg;
    msg._from = from;
    msg._to = to;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::Chat(int channel, RString text)
{
    ChatMessage msg(channel, GetLocalPlayerName(), text);
    SendMsg(&msg, NMFGuaranteed | GetChatPriority());
}

void NetworkClient::Chat(int channel, AIUnit* sender, RefArray<NetworkObject>& units, RString text)
{
    ChatMessage msg(channel, sender, units, GetLocalPlayerName(), text);
    SendMsg(&msg, NMFGuaranteed | GetChatPriority());
}

void NetworkClient::Chat(int channel, RString sender, RefArray<NetworkObject>& units, RString text)
{
    ChatMessage msg(channel, nullptr, units, sender, text);
    SendMsg(&msg, NMFGuaranteed | GetChatPriority());
}

void NetworkClient::RadioChat(int channel, AIUnit* sender, RefArray<NetworkObject>& units, RString text,
                              RadioSentence& sentence)
{
    RadioChatMessage msg;
    msg.channel = channel;
    msg.sender = sender;
    msg.units = units;
    msg.text = text;
    msg.sentence = sentence;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::RadioChatWave(int channel, RefArray<NetworkObject>& units, RString wave, AIUnit* sender,
                                  RString senderName)
{
    RadioChatWaveMessage msg;
    msg.channel = channel;
    msg.units = units;
    msg.wave = wave;
    msg.sender = sender;
    msg.senderName = senderName;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::SetVoiceChannel(int channel)
{
    _voiceChannel = channel;
    SetVoiceChannelMessage msg(channel);
    SendMsg(&msg, NMFGuaranteed);
    LOG_DEBUG(Network, "VoN# cli voicechan-msg ch={} units=0 transport={}", channel, _client != nullptr);
    if (_client)
    {
        _client->SetVoiceChannel(channel);
        _client->SetVoiceTransmit(channel != CCNone);
    }
}

void NetworkClient::SetVoiceChannel(int channel, RefArray<NetworkObject>& units)
{
    _voiceChannel = channel;
    SetVoiceChannelMessage msg(channel, units);
    SendMsg(&msg, NMFGuaranteed);
    LOG_DEBUG(Network, "VoN# cli voicechan-msg ch={} units={} transport={}", channel, units.Size(), _client != nullptr);
    if (_client)
    {
        _client->SetVoiceChannel(channel);
        _client->SetVoiceTransmit(channel != CCNone);
    }
}

int NetworkClient::SendVoiceTestTone(int frames, int amplitude)
{
    _voiceChannel = CCDirect;
    SetVoiceChannelMessage msg(CCDirect);
    SendMsg(&msg, NMFGuaranteed);
    if (_client)
        _client->SetVoiceChannel(CCDirect);
    return _client ? _client->SendVoiceTestTone(frames, amplitude) : 0;
}

void NetworkClient::GetVoiceSpeakers(AutoArray<NetVoiceSpeakerInfo, Poseidon::Foundation::MemAllocSA>& speakers) const
{
    if (_client)
        _client->GetVoiceSpeakers(speakers);
}

int NetworkClient::GetVoiceTransmitHealth() const
{
    return _client ? _client->GetVoiceTransmitHealth() : 0;
}

// Files can be transferred only to a player's temporary folder on the server -
// dest path format must be "tmp\\players\\" + playername + "\\relativepath".
// Transferred file size must not exceed MaxCustomFileSize.
// All of this is enforced server side and clients violating it are kicked,
// so fake clients cannot write anywhere on the server disc.

void NetworkClient::TransferFileToServer(RString dest, RString source)
{
    const std::vector<char> data = Poseidon::ReadFileUtf8(source);
    if (data.empty() && Poseidon::FileExistsUtf8(source))
    {
        LOG_WARN(Network, "[TransferFileToServer] refusing empty file '{}'", (const char*)source);
        return;
    }
    if (data.empty())
    {
        LOG_WARN(Network, "[TransferFileToServer] failed to read '{}'", (const char*)source);
        return;
    }

    const DWORD start = GlobalTickCount();
    const int segments = Poseidon::SendNetworkFileTransferSegments<TransferFileToServerMessage>(
        dest, data.data(), static_cast<int>(data.size()),
        [this](TransferFileToServerMessage& msg) { SendMsg(&msg, NMFGuaranteed | NMFHighPriority); });
    LOG_INFO(Network, "[NMTTransferFileToServer] upload queued src='{}' dst='{}' bytes={} segments={} enqueueMs={}",
             (const char*)source, (const char*)dest, data.size(), segments, GlobalTickCount() - start);
}

void NetworkClient::GetTransferStats(int& curBytes, int& totBytes)
{
    curBytes = 0;
    totBytes = 0;
    for (int i = 0; i < _files.Size(); i++)
    {
        curBytes = _files[i].received;
        totBytes = _files[i].fileData.Size();
    }
    if (totBytes <= 0 && _state == NGSTransferMission && !_missionFileValid)
    {
        totBytes = Poseidon::NetworkMissionHeaderTransferSize(_missionHeader.fileSizeL, _missionHeader.fileSizeH);
        if (totBytes > 0 && !_missionTransferHeaderStatsLogged)
        {
            LOG_DEBUG(Network, "[NMTTransferMission] transfer UI waiting for first segment file='{}' total={}",
                      (const char*)_missionHeader.fileName, totBytes);
            _missionTransferHeaderStatsLogged = true;
        }
    }
}

NetworkLocalObjectInfo* NetworkClient::GetLocalObjectInfo(NetworkId& id)
{
    for (int i = 0; i < _localObjects.Size(); i++)
    {
        NetworkLocalObjectInfo& info = _localObjects[i];
        if (info.id == id)
        {
            return &info;
        }
    }
    return nullptr;
}

NetworkRemoteObjectInfo* NetworkClient::GetRemoteObjectInfo(NetworkId& id)
{
    for (int i = 0; i < _remoteObjects.Size(); i++)
    {
        NetworkRemoteObjectInfo& info = _remoteObjects[i];
        if (info.id == id)
        {
            return &info;
        }
    }
    return nullptr;
}

NetworkObject* NetworkClient::GetObject(NetworkId& id)
{
    if (id.creator == 0)
    {
        return nullptr;
    }
    else if (id.creator == 1)
    {
        return GLandscape->FindObject(id.id);
    }
    else
    {
        NetworkRemoteObjectInfo* infoR = GetRemoteObjectInfo(id);
        if (infoR)
        {
            return infoR->object;
        }
        NetworkLocalObjectInfo* infoL = GetLocalObjectInfo(id);
        if (infoL)
        {
            return infoL->object;
        }
        return nullptr;
    }
}

void NetworkClient::DeleteObject(NetworkId& id)
{
    if (id.IsNull())
    {
        return;
    }

    DeleteObjectMessage msg;
    msg.object = id;
    SendMsg(&msg, NMFGuaranteed);

#if CHECK_MSG
    CheckLocalObjects();
#endif

    for (int i = 0; i < _localObjects.Size(); i++)
    {
        NetworkLocalObjectInfo& info = _localObjects[i];
        if (info.id == id)
        {
            _localObjects.Delete(i);
            if (DiagLevel >= 1)
            {
                DiagLogF("Client: local object destroyed (DeleteObject function) %d:%d", id.creator, id.id);
            }
            return;
        }
    }
    LOG_DEBUG(Network, "Warning: Client: Object info {}:{} not found.", id.creator, id.id);

#if CHECK_MSG
    CheckLocalObjects();
#endif
}

void NetworkClient::ShowTarget(Person* vehicle, TargetType* target)
{
    if (_state < NGSPlay)
    {
        return; // send event messages only when playing
    }

    ShowTargetMessage msg;
    msg.vehicle = vehicle;
    msg.target = target;
    SendMsg(&msg, NMFGuaranteed);
}

void NetworkClient::ShowGroupDir(Person* vehicle, Vector3Par dir)
{
    if (_state < NGSPlay)
    {
        return; // send event messages only when playing
    }

    ShowGroupDirMessage msg;
    msg.vehicle = vehicle;
    msg.dir = dir;
    SendMsg(&msg, NMFGuaranteed);
}

// Subsidiary structure for sorting messages by error
struct UpdateLocalObjectInfo
{
    // Local object info
    NetworkLocalObjectInfo* oInfo;
    // Message class type
    NetworkMessageClass cls;
    // Error (difference)
    float error;
    // Critical update
    bool criticalUpdate;
};

// Compare local object infos by error
int CmpLocalObjects(const UpdateLocalObjectInfo* info1, const UpdateLocalObjectInfo* info2)
{
    float diff = info2->error - info1->error;
    return sign(diff);
}

// Format value of error for output
const char* FormatVal(float val, char* buffer)
{
    if (val == FLT_MAX)
    {
        snprintf(buffer, sizeof(buffer), "%s", (const char*)"MAX");
    }
    else
    {
        snprintf(buffer, sizeof(buffer), "%.0f", val);
    }
    return buffer;
}

void NetworkClient::EstimateBandwidth(int& nMsgMax, int& nBytesMax)
{
    if (IsBotClient())
    {
        // unlimited bandwidth
        nMsgMax = INT_MAX;
        nBytesMax = INT_MAX;
    }
    else
    {
        int nMsg = 0;
        int nBytes = 0;
        /*
          #if _ENABLE_CHEATS
            if (outputDiags == 1 && nMsg > 0) snprintf(output, sizeof(output), "Waiting (%d, %d); ", nBytes, nMsg);
          #endif
        */
        nMsgMax = MaxMsgSend;
        nBytesMax = 1024;

        int latencyMS, throughputBPS;
        if (_client->GetConnectionInfo(latencyMS, throughputBPS))
        {
            // nBytesMax += 1.25 * info.dwThroughputBPS;
            nBytesMax += throughputBPS;
            nBytesMax += throughputBPS >> 2; // info.dwThroughputBPS / 4
                                             /*
                                               #if _ENABLE_CHEATS
                                                   if (outputDiags == 1) sprintf(output + strlen(output), "%d ms, %d bps; ", latencyMS, throughputBPS*8);
                                               #endif
                                             */
            if (_connectionQuality < CQPoor)
            {
                if (latencyMS >= 500)
                {
                    _connectionQuality = CQPoor;
                }
            }
        }
        int nPlayers = _identities.Size() - 1;
        if (nPlayers > 0)
        {
            saturate(nBytesMax, MinBandwidth / 8, MaxBandwidth / 8);
        }

        nBytesMax = toInt(0.001 * GEngine->GetAvgFrameDuration() * nBytesMax);

        nBytesMax -= nBytes;
        nMsgMax -= nMsg;

#if _ENABLE_CHEATS
        if (outputLogs)
            LOG_DEBUG(Network, "Client: Limit ({}, {})", nBytesMax, nMsgMax);
        /*
            if (outputDiags == 1) sprintf(output + strlen(output), "Limit (%d, %d); ", nBytesMax, nMsgMax);
        */
#endif
    }
}

void NetworkClient::CreateObjectsList(AutoArray<UpdateLocalObjectInfo, MemAllocSA>& objects)
{
    float maxError = 0, sumError = 0;
    for (int i = 0; i < _localObjects.Size(); i++)
    {
        NetworkLocalObjectInfo& oInfo = _localObjects[i];

        for (int j = NMCUpdateFirst; j < NMCUpdateN; j++)
        {
            NetworkUpdateInfo& info = oInfo.updates[j];

            float error;
            if (info.lastCreatedMsg)
            {
                // message on the way
                continue;
            }
            else
            {
                NetworkMessageType type = oInfo.object->GetNMType((NetworkMessageClass)j);
                if (type == NMTNone)
                {
                    continue;
                }

                if (!info.lastSentMsg)
                {
                    error = FLT_MAX;
                }
                else
                {
                    NetworkMessageFormatBase* format = GetFormat(type);
                    if (!format)
                    {
                        RptF("Client: Bad message %d(%s) format", (int)type,
                             (const char*)NetworkMessageTypeNames[type]);
                        continue;
                    }
                    NetworkMessageContext ctx(info.lastSentMsg, format, this, TO_SERVER,
                                              MSG_RECEIVE // avoid PrepareMessage
                    );
                    ctx.SetClass((NetworkMessageClass)j);
                    error = oInfo.object->CalculateError(ctx);
#if LOG_CLIENT_ERRORS
                    if (error >= LogClientErrorLimit)
                    {
                        DiagLogF("Object %s %d:%d", NetworkMessageTypeNames[type], oInfo.id.creator, oInfo.id.id);
                        DiagLogF("  - error %.3f", error);
                        float dummy = oInfo.object->CalculateError(ctx);
                        NET_ERROR(dummy == error);
                    }
#endif
                    if (!_finite(error))
                    {
                        RptF("Client: Infinite error detected on %s", (const char*)oInfo.object->GetDebugName());
                        // recalculate: debuggin opportunity
                        oInfo.object->CalculateError(ctx);
                    }
                }
                // note: following condition seems to be identical to error>MinErrorToSend
                // but it is different when error is NaN
                if (!(error <= MinErrorToSend))
                {
                    int index = objects.Add();
                    objects[index].oInfo = &oInfo;
                    objects[index].cls = (NetworkMessageClass)j;
                    objects[index].error = error;
                    objects[index].criticalUpdate = false;
                    if (!_finite(error))
                    {
                        RptF("Client: Infinite error catched on %s", (const char*)oInfo.object->GetDebugName());
                    }
                }
                saturateMax(maxError, error);
                sumError += error;
            }
        }
    }
    /*
    #if _ENABLE_CHEATS
      if (outputDiags == 1 && sumError > 0)
      {
        char buf1[32], buf2[32];
        sprintf
        (
          output + strlen(output),
          "Err %s / %s",
          FormatVal(maxError, buf1),
          FormatVal(sumError, buf2)
        );
        GlobalShowMessage(1000, output);
      }
    #endif
    */

    // sort local objects by errors
    QSort(objects.Data(), objects.Size(), CmpLocalObjects);
}

void NetworkClient::SendMessages()
{
    // calculate limits
    int nMsg = 0, nBytes = 0;
    int nMsgMax, nBytesMax;

    EstimateBandwidth(nMsgMax, nBytesMax);

    // send enqueued guaranteed messages

    const int nMsgMaxGuaranteed = nMsgMax;
    const int nBytesMaxGuaranteed = nBytesMax;

    int maxSize = MaxSizeGuaranteed;
    while (_messageQueue.Size() > 0)
    {
        if (nMsg >= nMsgMaxGuaranteed || nBytes >= nBytesMaxGuaranteed)
        {
            /*
            #if _ENABLE_CHEATS
                  if (outputDiags == 1)
                  {
                    strcat(output, "Limit reached");
                    GlobalShowMessage(1000, output);
                  }
            #endif
            */
            break;
        }

        NetworkMessage* msg = _messageQueue[0].msg;
        int size = msg->size;
        if (size >= maxSize)
        {
            DWORD dwMsgId = SendMsgRemote(TO_SERVER, msg, _messageQueue[0].type, NMFGuaranteed | NMFStatsAlreadyDone);
            (void)dwMsgId; // Return value used for debugging
            _messageQueue.Delete(0);
            nMsg++;
            nBytes += size;
        }
        else
        {
            int totalSize = 0;
            int i;
            for (i = 0; i < _messageQueue.Size(); i++)
            {
                NetworkMessage* msg = _messageQueue[i].msg;
                int size = msg->size;
                if (totalSize + size > maxSize)
                {
                    break;
                }
                totalSize += size;
            }
            DWORD dwMsgId = SendMsgQueue(TO_SERVER, _messageQueue, 0, i, NMFGuaranteed);
            (void)dwMsgId; // Return value used for debugging
            _messageQueue.Delete(0, i);
            nMsg++;
            nBytes += totalSize;
        }
    }

    AUTO_STATIC_ARRAY(UpdateLocalObjectInfo, objects, 256);

#define ENABLE_CRITICAL_UPDATES 1

#if !ENABLE_CRITICAL_UPDATES
    if (nMsg >= nMsgMax || nBytes >= nBytesMax)
    {
#if _ENABLE_CHEATS
        if (outputLogs)
            LOG_DEBUG(Network, "  Client: limit reached (guaranteed)");
#endif
        goto DoNotSendUpdates;
    }
#endif

    if (_state >= NGSPlay)
    {
#if CHECK_MSG
        CheckLocalObjects();
#endif

        // delete destroyed objects - keep order
        for (int i = 0; i < _localObjects.Size();)
        {
            NetworkLocalObjectInfo& info = _localObjects[i];
            if (!info.object)
            {
                DeleteObjectMessage msg;
                msg.object = info.id;
                SendMsg(&msg, NMFGuaranteed);
                if (DiagLevel >= 1)
                {
                    DiagLogF("Client: local object destroyed (object not found) %d:%d", info.id.creator, info.id.id);
                }
                _localObjects.Delete(i);
                continue;
            }
            i++;
        }

#if CHECK_MSG
        CheckLocalObjects();
#endif

        // update errors of local objects
        CreateObjectsList(objects);

        // create and send critical updates
#if ENABLE_CRITICAL_UPDATES
        for (int i = 0; i < objects.Size(); i++)
        {
            NetworkLocalObjectInfo& info = *objects[i].oInfo;
            NetworkMessageClass cls = objects[i].cls;

            float error = objects[i].error;

            // any message about dammage that transmit change
            // that could mean dead should be considered high priority
            if (cls != NMCUpdateDammage || error < ERR_COEF_STRUCTURE)
            {
                continue;
            }

            objects[i].criticalUpdate = true;
            int bytes = UpdateObject(info.object, cls, NMFGuaranteed | NMFHighPriority);

#if _ENABLE_CHEATS
            if (outputLogs)
            {
                LOG_DEBUG(Network, "  Client: Object {}:{} updated - size {} bytes, critical", info.id.creator,
                          info.id.id, bytes);
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
                LOG_DEBUG(Network, "  Client: limit reached (guaranteed)");
#endif
            goto DoNotSendUpdates;
        }

#endif
    }

DoNotSendUpdates:
    // send nonguaranteed enqueued messages
    maxSize = MaxSizeNonguaranteed;
    int next = 0; // index of next object to update
    bool empty = false;
    while (true)
    {
        // enforce send last message
        if (!empty || _messageQueueNonGuaranteed.Size() <= 0)
        {
            if (nMsg >= nMsgMax || nBytes >= nBytesMax)
            {
                break;
            }
        }

        if (_messageQueueNonGuaranteed.Size() <= 0)
        {
            empty = true;
            // starving, try to create further message
            if (!PrepareNextUpdate(objects, next))
            {
                break;
            }
            // local communication - message was already sent
            if (_parent->IsServer())
            {
                NET_ERROR(_messageQueueNonGuaranteed.Size() == 0);
                nMsg++;
                continue;
            }

            NET_ERROR(_messageQueueNonGuaranteed.Size() > 0);
        }

        NetworkMessageQueueItem& item = _messageQueueNonGuaranteed[0];
        int size = item.msg->size;
        // enforce send last message
        if (size >= maxSize || nMsg >= nMsgMax || nBytes >= nBytesMax)
        {
            DWORD dwMsgId = SendMsgRemote(TO_SERVER, item.msg, item.type, NMFStatsAlreadyDone);
#if LOG_SEND_PROCESS
            LOG_DEBUG(Network, "Client: Message {:x} sent", dwMsgId);
#endif
            if (item.msg->objectUpdateInfo)
            {
#if LOG_SEND_PROCESS
                LOG_DEBUG(Network, "  - update info {:x} updated", (uintptr_t)item.msg->objectUpdateInfo);
#endif
                if (item.msg->objectUpdateInfo->lastCreatedMsg != item.msg)
                {
                    RptF("Last created message is bad:");
                    RptF("  sending message: %x (type %s), id = %x", item.msg.GetRef(),
                         NetworkMessageTypeNames[item.type], dwMsgId);
                    RptF("	attached object info: %x", item.msg->objectUpdateInfo);
                    RptF("	last created message: %x, id = %x", item.msg->objectUpdateInfo->lastCreatedMsg.GetRef(),
                         item.msg->objectUpdateInfo->lastCreatedMsgId);
                    RptF("	last sent message: %x", item.msg->objectUpdateInfo->lastSentMsg.GetRef());

                    item.msg->objectUpdateInfo->lastCreatedMsg = item.msg;
                }
                item.msg->objectUpdateInfo->lastCreatedMsgId = dwMsgId;
                if (dwMsgId == 0xffffffff)
                {
                    item.msg->objectUpdateInfo->lastCreatedMsg = nullptr;
                    item.msg->objectUpdateInfo->lastCreatedMsgTime = 0;
                }
            }
            _messageQueueNonGuaranteed.Delete(0);
            nMsg++;
            nBytes += size;
        }
        else
        {
            int totalSize = 0;
            int i;
            for (i = 0; true; i++)
            {
                if (i >= _messageQueueNonGuaranteed.Size())
                {
                    empty = true;
                    // starving, try to create further message
                    if (!PrepareNextUpdate(objects, next))
                    {
                        break;
                    }
                    NET_ERROR(!_parent->IsServer());
                    NET_ERROR(i < _messageQueueNonGuaranteed.Size());
                }

                NetworkMessageQueueItem& item = _messageQueueNonGuaranteed[i];
                int size = item.msg->size;
                if (totalSize + size > maxSize)
                {
                    break;
                }
                totalSize += size;
            }
            DWORD dwMsgId = SendMsgQueue(TO_SERVER, _messageQueueNonGuaranteed, 0, i, NMFNone);
#if LOG_SEND_PROCESS
            LOG_DEBUG(Network, "Client: Message {:x} sent", dwMsgId);
#endif
            for (int j = 0; j < i; j++)
            {
                NetworkMessageQueueItem& item = _messageQueueNonGuaranteed[j];
                if (item.msg->objectUpdateInfo)
                {
#if LOG_SEND_PROCESS
                    LOG_DEBUG(Network, "  - update info {:x} updated", (uintptr_t)item.msg->objectUpdateInfo);
#endif
                    if (item.msg->objectUpdateInfo->lastCreatedMsg != item.msg)
                    {
                        RptF("Last created message is bad:");
                        RptF("  sending message: %x (type %s), id = %x", item.msg.GetRef(),
                             NetworkMessageTypeNames[item.type], dwMsgId);
                        RptF("	attached object info: %x", item.msg->objectUpdateInfo);
                        RptF("	last created message: %x, id = %x", item.msg->objectUpdateInfo->lastCreatedMsg.GetRef(),
                             item.msg->objectUpdateInfo->lastCreatedMsgId);
                        RptF("	last sent message: %x", item.msg->objectUpdateInfo->lastSentMsg.GetRef());

                        item.msg->objectUpdateInfo->lastCreatedMsg = item.msg;
                    }
                    item.msg->objectUpdateInfo->lastCreatedMsgId = dwMsgId;
                    if (dwMsgId == 0xffffffff)
                    {
                        item.msg->objectUpdateInfo->lastCreatedMsg = nullptr;
                        item.msg->objectUpdateInfo->lastCreatedMsgTime = 0;
                    }
                }
            }
            _messageQueueNonGuaranteed.Delete(0, i);
            nMsg++;
            nBytes += totalSize;
        }
    }
    NET_ERROR(!empty || _messageQueueNonGuaranteed.Size() == 0);

    //  int sumError = 0; // messages not sent total error
    if (next < objects.Size())
    {
#if _ENABLE_CHEATS
        if (outputLogs)
            LOG_DEBUG(Network, "  Client: limit reached");
#endif
        /*
            for (int i=next; i<objects.Size(); i++)
            {
              if (objects[i].error < FLT_MAX)
              {
                sumError += objects[i].error;
              }
            }
        */
    }

#if CHECK_MSG
    CheckLocalObjects();
#endif

#if _ENABLE_CHEATS
    // remove statistics info
    static AutoArray<int> texts;
    for (int i = 0; i < texts.Size(); i++)
    {
        if (texts[i] >= 0)
            GEngine->RemoveText(texts[i]);
    }
    texts.Resize(0);

    // create statistics
    if (outputDiags == 1)
    {
        int count = _messageQueue.Size(), size = 0;
        for (int i = 0; i < count; i++)
            size += _messageQueue[i].msg->size;

        RString output = Format("Server: HLWait%3d(%5dB) ", count, size) + _client->GetStatistics();

        texts.Add(GEngine->ShowTextF(1000, 10, 15, output));
    }
#endif
}

bool NetworkClient::PrepareNextUpdate(AutoArray<UpdateLocalObjectInfo, MemAllocSA>& objects, int& next)
{
    while (next < objects.Size())
    {
        UpdateLocalObjectInfo& info = objects[next++];
        if (info.criticalUpdate)
        {
            continue;
        }

        NetworkLocalObjectInfo& oInfo = *info.oInfo;
        NetworkMessageClass cls = info.cls;
        int bytes = UpdateObject(oInfo.object, cls, NMFNone);
        if (bytes < 0)
        {
            continue;
        }

#if _ENABLE_CHEATS
        if (outputLogs)
            LOG_DEBUG(Network, "  Client: Object updated - size {} bytes", bytes);
#endif
        return true;
    }
    return false;
}

void NetworkClient::Respawn(Person* soldier, Vector3Par pos)
{
    if (_state < NGSPlay)
    {
        return; // send event messages only when playing
    }

    // add to respawn queue
    AIUnit* unit = soldier->Brain();
    NET_ERROR(unit);
    AIGroup* grp = unit->GetGroup();
    NET_ERROR(grp);
    if (!grp)
        Fail("Respawning unit with no group");

    int index = _respawnQueue.Add();
    RespawnQueueItem& item = _respawnQueue[index];
    item.person = soldier;
    item.position = pos;
    item.time = Glob.time + GetRespawnDelay();
    item.player = soldier == GWorld->GetRealPlayer();
    /*
      item.type = soldier->GetType();
      item.side = soldier->Vehicle::GetTargetSide();
      item.varname = soldier->GetVarName();
      item.info = soldier->GetInfo();
      item.player = soldier == GWorld->GetRealPlayer() ? _player : 0;
      item.group = grp;
      item.position = pos;
      item.time = Glob.time + GetRespawnDelay();
      item.id = -1;
      for (int i=0; i<vehiclesMap.Size(); i++)
        if (vehiclesMap[i] == soldier)
        {
          item.id = i;
          break;
        }
    */
}

int NetworkClient::NLocalObjectsNULL() const
{
    int n = 0;
    for (int i = 0; i < _localObjects.Size(); i++)
    {
        const NetworkLocalObjectInfo& info = _localObjects[i];
        if (!info.object)
        {
            n++;
        }
    }
    return n;
}

float NetworkClient::GetLastMsgAgeReliable()
{
    if (_client)
    {
        return _client->GetLastMsgAgeReliable();
    }
    return 0;
}
