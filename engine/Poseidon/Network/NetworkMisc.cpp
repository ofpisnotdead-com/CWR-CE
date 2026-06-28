#include <Poseidon/Foundation/Platform/AppConfig.hpp>

using namespace Poseidon;
#include <algorithm>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/Config.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Core/ModSystem.hpp>
#include <Poseidon/Core/Version.hpp>
#include <Poseidon/Network/NetworkImpl.hpp>
#include <Poseidon/Network/NetworkConfig.hpp>
#include <Poseidon/Network/NetworkCustomAssets.hpp>
#include <Poseidon/Network/NetworkFileTransfer.hpp>
#include <Poseidon/Network/NetworkServerAuth.hpp>
#include <Poseidon/Network/WireBounds.hpp>
#include <Poseidon/Core/Global.hpp>
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
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <system_error>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/GlobalAlive.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Memory/MemAlloc.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Threads/MultiSync.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/Memtype.h>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

#ifdef _WIN32
#include <io.h>
#endif
#include <SDL3/SDL.h>
#include <filesystem>

#include <Poseidon/World/Scene/Camera/Camera.hpp>

#include <Poseidon/Foundation/Strings/Mbcs.hpp>

using namespace Poseidon::Dev;
using Poseidon::Foundation::MemAllocSA;
using Poseidon::Foundation::Time;

// Squad checking

// XML parser for squad xml document
class SquadParser : public SAXParser
{
  protected:
    // identity of checked player
    PlayerIdentity* _identity;
    // squad of checked player
    SquadIdentity* _squad;

    // currently read element
    RString _element;
    // inside <squad> ... </squad>
    bool _ctxSquad;
    // inside <member> ... </member>
    bool _ctxMember;
    // found <member> record for checked player
    bool _found;
    // player nick match with nick given in <member> record
    bool _validated;

  public:
    SquadParser(PlayerIdentity* identity, SquadIdentity* squad)
    {
        _identity = identity;
        _squad = squad;
        _ctxSquad = false;
        _ctxMember = false;
        _found = false;
        _validated = false;
    }
    // Return if player record was found and validated
    bool Found() const { return _found && !_ctxMember && _validated; }

    void OnStartElement(RString name, XMLAttributes& attributes) override;
    void OnEndElement(RString name) override;
    void OnCharacters(RString chars) override;
};

void SquadParser::OnStartElement(RString name, XMLAttributes& attributes)
{
    if (strcmp(name, "squad") == 0)
    {
        NET_ERROR(!_ctxSquad);
        NET_ERROR(!_ctxMember);
        _ctxSquad = true;
        const XMLAttribute* attr = attributes.Find("nick");
        if (attr)
        {
            _squad->nick = attr->value;
        }
    }
    else if (strcmp(name, "member") == 0)
    {
        NET_ERROR(_ctxSquad);
        NET_ERROR(!_ctxMember);
        _ctxMember = true;
        const XMLAttribute* attr = attributes.Find("id");
        if (attr && attr->value == _identity->id)
        {
            _found = true;
            const XMLAttribute* attr = attributes.Find("nick");
            // verify nick matches - if not, refuse validation
            _validated = attr && strcmpi(_identity->name, attr->value) == 0;
        }
    }
    _element = name;
}

void SquadParser::OnEndElement(RString name)
{
    if (strcmp(name, "squad") == 0)
    {
        NET_ERROR(_ctxSquad);
        NET_ERROR(!_ctxMember);
        _ctxSquad = false;
    }
    else if (strcmp(name, "member") == 0)
    {
        NET_ERROR(_ctxMember);
        _ctxMember = false;
        if (_found)
        {
            Abort();
        }
    }
    _element = "";
}

void SquadParser::OnCharacters(RString chars)
{
    if (!_ctxSquad)
    {
        return;
    }
    if (_ctxMember)
    {
        if (!_found)
        {
            return;
        }
        if (strcmp(_element, "name") == 0)
        {
            _identity->fullname = chars;
        }
        else if (strcmp(_element, "email") == 0)
        {
            _identity->email = chars;
        }
        else if (strcmp(_element, "icq") == 0)
        {
            _identity->icq = chars;
        }
        else if (strcmp(_element, "remark") == 0)
        {
            _identity->remark = chars;
        }
    }
    else
    {
        if (strcmp(_element, "name") == 0)
        {
            _squad->name = chars;
        }
        else if (strcmp(_element, "email") == 0)
        {
            _squad->email = chars;
        }
        else if (strcmp(_element, "web") == 0)
        {
            _squad->web = chars;
        }
        else if (strcmp(_element, "picture") == 0)
        {
            _squad->picture = chars;
        }
        else if (strcmp(_element, "title") == 0)
        {
            _squad->title = chars;
        }
    }
}

CheckSquadObject::CheckSquadObject(PlayerIdentity& identity, Ref<SquadIdentity> squad, bool newSquad, RString proxy)
    : _stateDone(true)
{
    _identity = identity;
    _squad = squad;
    _newSquad = newSquad;
    _proxy = proxy;

    StartDownloadingXMLSource();
}

CheckSquadObject::~CheckSquadObject()
{
    // we cannot destruct background object while thread is working
    _stateDone.Wait();
}

// Download to memory
static DWORD WINAPI DownloadToMemThread(void* context)
{
    DownloadToMemContext* c = (DownloadToMemContext*)context;
    c->result = DownloadFile(c->url, *c->size, c->proxy);
    c->event->Set();
    return 0;
}

// Download to file
static DWORD WINAPI DownloadToFileThread(void* context)
{
    DownloadToFileContext* c = (DownloadToFileContext*)context;
    DownloadFile(c->url, c->file, c->proxy);
    c->event->Set();
    return 0;
}

// Use independent thread for file download
#define MT_DOWNLOAD 1

#if MT_DOWNLOAD

#ifdef _WIN32
// Download to file
static void DownloadToFileOverlapped(DownloadToFileContext* context)
{
    DWORD threadId;
    HANDLE handle = CreateThread(nullptr, 64 * 1024, DownloadToFileThread, context, 0, &threadId);
    if (!handle)
    {
        // create thread failed - fallback to direct processing
        DownloadToFileThread(context);
    }
    else
    {
        // handle no longer required
        CloseHandle(handle);
    }
}

// Download to memory
static void DownloadToMemOverlapped(DownloadToMemContext* context)
{
    DWORD threadId;
    HANDLE handle = CreateThread(nullptr, 64 * 1024, DownloadToMemThread, context, 0, &threadId);
    if (!handle)
    {
        // create thread failed - fallback to direct processing
        DownloadToMemThread(context);
    }
    else
    {
        // handle no longer required
        CloseHandle(handle);
    }
}
#else
#include <thread>

// Download to file
static void DownloadToFileOverlapped(DownloadToFileContext* context)
{
    std::thread t(DownloadToFileThread, context);
    t.detach();
}

// Download to memory
static void DownloadToMemOverlapped(DownloadToMemContext* context)
{
    std::thread t(DownloadToMemThread, context);
    t.detach();
}
#endif

#else // !MT_DOWNLOAD

// Download to file
static void DownloadToFileOverlapped(DownloadToFileContext* context)
{
    DownloadToFileThread(context);
}

// Download to memory
static void DownloadToMemOverlapped(DownloadToMemContext* context)
{
    DownloadToMemThread(context);
}

#endif // MT_DOWNLOAD

void CheckSquadObject::StartDownloadingXMLSource()
{
    _state = DownloadingSquad;
    _stateDone.Reset();

    _squadContext.url = _identity.squadId;
    _squadContext.size = &_squadXMLSize;
    _squadContext.event = &_stateDone;
    _squadContext.proxy = _proxy.GetLength() > 0 ? (const char*)_proxy : nullptr;
    _squadContext.result.Free();
    DownloadToMemOverlapped(&_squadContext);
}

void CheckSquadObject::EndDownloadingXMLSource()
{
    _squadXMLData = _squadContext.result;
}

void CheckSquadObject::StartDownloadingLogo()
{
    // process XML results
    ProcessXML();

    if (_logoUrl.GetLength() > 0 && _logoFile.GetLength() > 0)
    {
        _state = DownloadingLogo;
        _stateDone.Reset();
        // start downloading logo - if necessary
        // note: _logoUrl is RString, but is passed as const char *
        // which is MT safe
        _logoContext.file = _logoFile;
        _logoContext.url = _logoUrl;
        _logoContext.event = &_stateDone;
        _logoContext.proxy = _proxy.GetLength() > 0 ? (const char*)_proxy : nullptr;
        DownloadToFileOverlapped(&_logoContext);
        // download result ignored?
    }
    else
    {
        _state = AllDone;
        // _stateDone is still Set
    }
}

bool CheckSquadObject::IsDone()
{
    if (!_stateDone.TryWait())
    {
        return false;
    }
    switch (_state)
    {
        case DownloadingSquad:
            EndDownloadingXMLSource();
            StartDownloadingLogo();
            return false;
        case DownloadingLogo:
            _state = AllDone;
            return true;
        case AllDone:
            return true;
    }
    return false;
}

void CheckSquadObject::ProcessXML()
{
    _logoUrl = RString();
    _logoFile = RString();
    if (DoProcessXML())
    {
        _identity.squad = _squad;
    }
    else
    {
        _squad = nullptr;
        _identity.squadId = "";
    }
}

bool CheckSquadObject::DoProcessXML()
{
    NET_ERROR(_identity.squadId.GetLength() > 0);

    if (!_squadXMLData || _squadXMLSize <= 0)
    {
        return false;
    }

    SquadParser parser(&_identity, _squad);
    QIStream in(_squadXMLData, _squadXMLSize);

    if (!parser.Parse(in))
    {
        return false;
    }
    _squadXMLData.Free();

    if (!parser.Found())
    {
        return false;
    }

    if (_newSquad && _squad->picture.GetLength() > 0)
    {
        // download picture
        RString src = GetServerTmpDir() + RString("/squads/") + _squad->nick + RString("/") + _squad->picture;
        CreatePath(src);
        char url[256];
        snprintf(url, sizeof(url), "%s", (const char*)_squad->id);
        char* ptr = strrchr(url, '/');
        // FIX allow also \ in url
        if (!ptr)
        {
            ptr = strrchr(url, '\\');
        }
        if (ptr)
        {
            ptr++;
            *ptr = 0;
        }
        else
        {
            ptr = url;
        }
        strcpy(ptr, _squad->picture);

        _logoUrl = url;
        _logoFile = src;
    }
    else
    {
        _logoUrl = RString();
        _logoFile = RString();
    }

    return true;
}

// Voting system

bool Vote::HasValue(const char* val, int valueSize) const
{
    if (valueSize != value.Size())
    {
        return false;
    }
    return memcmp(val, value.Data(), valueSize) == 0;
}

int Voting::Add(int player, char* value, int valueSize)
{
    int index = -1;
    for (int i = 0; i < Size(); i++)
    {
        if (Get(i).player == player)
        {
            index = i;
            break;
        }
    }
    if (index < 0)
    {
        index = base::Add();
        Set(index).player = player;
    }
    if (value)
    {
        Set(index).value.Realloc(valueSize);
        Set(index).value.Resize(valueSize);
        memcpy(Set(index).value.Data(), value, valueSize);
    }
    else
    {
        Set(index).value.Realloc(0);
        Set(index).value.Resize(0);
    }
    return index;
}

bool Voting::Check(const AutoArray<PlayerIdentity>& identities) const
{
    int sum = 0;
    int n = identities.Size();

    for (int j = 0; j < Size(); j++)
    {
        int player = Get(j).player;
        for (int i = 0; i < n; i++)
        {
            if (identities[i].dpnid == player)
            {
                sum++;
                break;
            }
        }
    }
    if (Poseidon::VoteThresholdMet(sum, n, _threshold))
    {
        return true;
    }
    if (!_selection)
    {
        return false;
    }

    // let
    // rest = number of players that did not vote yet
    // first = number of players that voted for the most popular choice
    // second = number of players that voted for the second most popular choice
    // voting is done when
    // first>=second+rest

    int first = 0, second = 0;
    int rest = identities.Size() - sum;
    GetValue(&first, &second);
    LOG_DEBUG(Network, "First {}, second {}, rest {}", first, second, rest);
    return Poseidon::VoteSelectionComplete(first, second, rest);
}

const AutoArray<char>* Voting::GetValue(int* n, int* n2) const
{
    if (Size() == 0)
    {
        return nullptr;
    }

    AUTO_STATIC_ARRAY(const Vote*, values, 32);
    AUTO_STATIC_ARRAY(int, votes, 32);
    for (int i = 0; i < Size(); i++)
    {
        int found = -1;
        const char* value = Get(i).value.Data();
        int valueSize = Get(i).value.Size();
        for (int j = 0; j < values.Size(); j++)
        {
            if (values[j]->HasValue(value, valueSize))
            {
                found = j;
                break;
            }
        }
        if (found >= 0)
        {
            votes[found]++;
        }
        else
        {
            values.Add(&Get(i));
            votes.Add(1);
        }
    }
    const Vote* best = nullptr;
    int maxVotes = 0;
    for (int i = 0; i < votes.Size(); i++)
    {
        if (votes[i] > maxVotes)
        {
            maxVotes = votes[i];
            best = values[i];
        }
    }
    if (n)
    {
        *n = maxVotes;
    }
    if (n2)
    {
        int max2Votes = 0;
        for (int i = 0; i < votes.Size(); i++)
        {
            if (votes[i] > max2Votes && values[i] != best)
            {
                max2Votes = votes[i];
            }
        }
        *n2 = max2Votes;
    }
    if (best)
    {
        return &best->value;
    }
    return nullptr;
}

bool Voting::HasID(char* id, int idSize) const
{
    if (idSize != _id.Size())
    {
        return false;
    }
    return memcmp(id, _id.Data(), idSize) == 0;
}

void Votings::Add(NetworkServer* server, char* id, int idSize, float threshold, int player, char* value, int valueSize,
                  bool selection)
{
    int index = -1;
    for (int i = 0; i < Size(); i++)
    {
        if (Get(i).HasID(id, idSize))
        {
            index = i;
            break;
        }
    }

    if (index < 0)
    {
        index = base::Add();
        Set(index)._threshold = threshold;
        Set(index)._selection = selection;
        Set(index)._id.Realloc(idSize);
        Set(index)._id.Resize(idSize);
        memcpy(Set(index)._id.Data(), id, idSize);
    }

    Voting& voting = Set(index);
    voting.Add(player, value, valueSize);
    if (voting.Check(*server->GetIdentities()))
    {
        // FIX: voting can change in ApplyVoting
        Voting copy = voting;
        Delete(index);
        server->ApplyVoting(copy.GetID(), copy.GetValue());
    }
}

void Votings::Check(NetworkServer* server)
{
    for (int i = 0; i < Size();)
    {
        Voting& voting = Set(i);
        if (voting.Check(*server->GetIdentities()))
        {
            server->ApplyVoting(voting.GetID(), voting.GetValue());
            Delete(i);
        }
        else
        {
            i++;
        }
    }
}

// Network Components

NetworkComponent::NetworkComponent(NetworkManager* parent)
{
    _parent = parent;
    _state = NGSNone;
    _nextId = 0;
}

NetworkComponent::~NetworkComponent() {}

#if 0
// Process out of memory error on local heap
void NetworkMemoryError( MemHeapLocked *heap, int size )
{
#ifdef _WIN32
	ErrorMessage
	(
		"Network: Out of reserved memory (%d KB).\n"
		"Code change required (current limit %d KB).\n"
		"Total free %d KB\n"
		"Free blocks %d, Max free size %d KB",
		size/1024,
		heap->Size()/1024,
		heap->TotalFreeLeft()/1024,
		heap->CountFreeLeft(),
		heap->MaxFreeLeft()/1024
	);
#else
	ErrorMessage
	(
		"Network: Out of reserved memory (%d KB).\n",
		size/1024
	);
#endif
}
#endif

// high level transfer functions

DWORD NetworkComponent::SendMsg(int to, NetworkSimpleObject* object, NetMsgFlags dwFlags)
{
    if (!object)
    {
        return 0xFFFFFFFF;
    }

    // prepare format
    NetworkMessageType type = object->GetNMType();
    NetworkMessageFormatBase* format = GetFormat(/*to, */ type);
    if (!format)
    {
        if (DiagLevel >= 1)
        {
            DiagLogF("Warning: Format not found");
        }
        return 0xFFFFFFFF;
    }

    // create message
    Ref<NetworkMessage> msg = new NetworkMessage();
    msg->time = Glob.time;
    NetworkMessageContext ctx(msg, format, this, to, MSG_SEND);
    TMError err = object->TransferMsg(ctx);
    if (err != TMOK)
    {
        return 0xFFFFFFFF;
    }

    // send message
    return SendMsg(to, msg, type, dwFlags);
}

void NetworkComponent::DecodeMessage(int from, NetworkMessageRaw& src)
{
    StatRawMsgReceived(from, src.GetSize());

    OnMonitorIn(src.GetSize());

    // message header
    NetworkMessageType type;
    if (!src.Get((int&)type, NCTSmallUnsigned))
        return;
    Time time;
    if (!src.Get(time, NCTNone))
        return;

    if (type == NMTMessages)
    {
        int n;
        if (!src.GetCount(n, NCTSmallUnsigned))
            return;
        for (int i = 0; i < n; i++)
        {
            NetworkMessageType type;
#if _ENABLE_CHEATS
            int oldPos = src.GetPos();
#endif
            src.Get((int&)type, NCTSmallUnsigned);
            NetworkMessageFormatBase* format = GetFormat(type);
            if (!format)
            {
                if (DiagLevel >= 1)
                {
                    DiagLogF("Warning: Format not found");
                }
                return;
            }
            Ref<NetworkMessage> msg = new NetworkMessage();
            msg->time = time;
            // Message body
            DecodeMsg(msg, src, format);
            OnMessage(from, msg, type);
#if _ENABLE_CHEATS
            StatMsgReceived(type, src.GetPos() - oldPos);
#endif
        }
    }
    else
    {
        NetworkMessageFormatBase* format = GetFormat(type);
        if (!format)
        {
            if (DiagLevel >= 1)
            {
                DiagLogF("Warning: Format not found");
            }
            return;
        }
        Ref<NetworkMessage> msg = new NetworkMessage();
        msg->time = time;
        // Message body
        DecodeMsg(msg, src, format);
        OnMessage(from, msg, type);
#if _ENABLE_CHEATS
        StatMsgReceived(type, src.GetSize());
#endif
    }
}

void NetworkComponent::ReceiveLocalMessages()
{
    for (int i = 0; i < _receivedLocalMessages.Size(); i++)
    {
        NetworkLocalMessageInfo& info = _receivedLocalMessages[i];
        OnMessage(info.from, info.msg, info.type);
    }
    _receivedLocalMessages.Resize(0);
}

const PlayerRole* NetworkComponent::FindPlayerRole(int player) const
{
    for (int i = 0; i < _playerRoles.Size(); i++)
    {
        const PlayerRole& role = _playerRoles[i];
        if (role.player == player)
        {
            return &role;
        }
    }
    return nullptr;
}

const PlayerIdentity* NetworkComponent::FindIdentity(int dpnid) const
{
    for (int i = 0; i < _identities.Size(); i++)
    {
        const PlayerIdentity& identity = _identities[i];
        if (identity.dpnid == dpnid)
        {
            return &identity;
        }
    }
    return nullptr;
}

PlayerIdentity* NetworkComponent::FindIdentity(int dpnid)
{
    for (int i = 0; i < _identities.Size(); i++)
    {
        PlayerIdentity& identity = _identities[i];
        if (identity.dpnid == dpnid)
        {
            return &identity;
        }
    }
    return nullptr;
}

int NetworkComponent::ReceiveFileSegment(TransferFileMessage& msg)
{
    // The declared geometry is wire-controlled. Reject it before any allocation or
    // write so a malformed header can neither drive a giant Resize nor index/copy
    // outside the buffers.
    static const int kMaxTransferBytes = 256 * 1024 * 1024;
    static const int kMaxTransferSegments = 1024 * 1024;
    if (!WireBounds::SegmentInBounds(msg.totSize, msg.totSegments, msg.curSegment, msg.offset, msg.data.Size()) ||
        !WireBounds::ValidLength(msg.totSize, kMaxTransferBytes) ||
        !WireBounds::ValidCount(msg.totSegments, kMaxTransferSegments))
    {
        return -1;
    }

    // find file in list
    int index = -1;
    for (int i = 0; i < _files.Size(); i++)
    {
        if (_files[i].fileName == msg.path)
        {
            index = i;
            break;
        }
    }
    if (index < 0)
    {
        // new file
        index = _files.Add();
        ReceivingFile& file = _files[index];
        file.fileName = msg.path;
        file.fileSegments.Resize(msg.totSegments);
        for (int i = 0; i < file.fileSegments.Size(); i++)
        {
            file.fileSegments[i] = false;
        }
        file.fileData.Resize(msg.totSize);
        file.received = 0;
    }
    ReceivingFile& file = _files[index];

    // A resumed transfer must match the geometry the buffers were allocated for;
    // a later packet declaring a larger file must not write past them.
    if (msg.curSegment >= file.fileSegments.Size() ||
        !WireBounds::RangeInBounds(msg.offset, msg.data.Size(), file.fileData.Size()))
    {
        return -1;
    }

    int remainingSegments = 0;
    for (int i = 0; i < file.fileSegments.Size(); i++)
    {
        if (!file.fileSegments[i])
        {
            ++remainingSegments;
        }
    }

    const bool complete = Poseidon::ApplyReceivedNetworkFileTransferSegment(
        file.fileSegments[msg.curSegment], msg.data.Size(), file.received, remainingSegments);
    memcpy(file.fileData.Data() + msg.offset, msg.data.Data(), msg.data.Size());
    if (!complete)
    {
        return 0;
    }
    //  whole file received
    CreatePath(file.fileName);
    QOFStream f;
    f.open(file.fileName);
    f.write(file.fileData.Data(), file.fileData.Size());
    f.close();
    // delete buffer
    _files.Delete(index);
    if (f.fail())
        LOG_WARN(Network, "[ReceiveFileSegment] FAILED to write '{}' ({} bytes)", (const char*)msg.path, msg.totSize);
    return f.fail() ? -1 : +1;
}

void NetworkComponent::TransferFile(int to, RString dest, RString source)
{
    QIFStreamB f;
    f.AutoOpen(source);

    Poseidon::SendNetworkFileTransferSegments<TransferFileMessage>(
        dest, f.GetBuffer()->GetData(), f.GetBuffer()->GetSize(),
        [this, to](TransferFileMessage& msg) { SendMsg(to, &msg, NMFGuaranteed); });
}

void NetworkComponent::TransferFace(int to, int player)
{
    // user only for transfer from server to clients
    NetworkClient* client = _parent->GetClient();
    bool notBotClient = !client || client->GetPlayer() != to;

    RString srcDir = Poseidon::BuildNetworkServerPlayerUploadDir(GetServerTmpDir(), player);
    RString dstDir = Poseidon::BuildNetworkPlayerAssetTmpDir(player);
    if (srcDir.GetLength() == 0 || dstDir.GetLength() == 0)
    {
        return;
    }

    RString src = Poseidon::BuildNetworkServerPlayerAssetUploadPath(GetServerTmpDir(), player, RString("face.paa"));
    RString dst = Poseidon::BuildNetworkPlayerAssetTmpPath(player, RString("face.paa"));
    if (QIFStream::FileExists(src))
    {
        if (notBotClient)
        {
            TransferFile(to, dst, src);
        }
        else
        {
            CreatePath(dstDir);
            ::CopyFile(src, dst, FALSE);
        }
    }
    else
    {
        src = Poseidon::BuildNetworkServerPlayerAssetUploadPath(GetServerTmpDir(), player, RString("face.jpg"));
        dst = Poseidon::BuildNetworkPlayerAssetTmpPath(player, RString("face.jpg"));
        if (QIFStream::FileExists(src))
        {
            if (notBotClient)
            {
                TransferFile(to, dst, src);
            }
            else
            {
                CreatePath(dstDir);
            }
            ::CopyFile(src, dst, FALSE);
        }
    }
}

void NetworkComponent::TransferCustomRadio(int to, RString player)
{
    // user only for transfer from server to clients
    NetworkClient* client = _parent->GetClient();
    bool notBotClient = !client || client->GetPlayer() != to;

    RString srcDir = GetServerTmpDir() + RString("/players/") + player + RString("/sound/");
    RString dstDir = RString("tmp/players/") + player + RString("/sound/");

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(std::string(srcDir), ec))
    {
        if (!entry.is_regular_file())
            continue;
        RString filename = entry.path().filename().string().c_str();
        RString src = srcDir + filename;
        RString dst = dstDir + filename;
        if (notBotClient)
        {
            TransferFile(to, dst, src);
        }
        else
        {
            CreatePath(dstDir);
            ::CopyFile(src, dst, FALSE);
        }
    }
}

// Network Manager

NetworkManager::NetworkManager()
{
    _sessionEnum = nullptr;
}

NetworkManager::~NetworkManager()
{
    Done();
}

void NetworkManager::StopEnumHosts()
{
    if (_sessionEnum)
    {
        _sessionEnum->StopEnumHosts();
    }
}

bool NetworkManager::StartEnumHosts()
{
    if (_sessionEnum)
    {
        _lastEnumHosts = Glob.uiTime;
        AutoArray<RemoteHostAddress>* hosts = nullptr;
        if (_ip.GetLength() == 0)
        {
            hosts = &_hosts;
        }
        return _sessionEnum->StartEnumHosts(_ip, _port, hosts);
    }
    return true;
}

bool NetworkManager::Init(RString ip, int port, bool startEnum)
{
    InitMsgFormats();

    _ip = ip;
    _port = port;

    // load some settings from Flashpoint.cfg
    ParamFile cfg;
    cfg.Parse(FlashpointCfg);

    _sessionEnum = CreateNetSessionEnum(cfg);

    NET_ERROR(_sessionEnum);
    if (!_sessionEnum->Init(MAGIC_APP))
    {
        _sessionEnum = nullptr;
        return false;
    }

    // read external hosts file
    ParamFile file;
    file.ParsePlainText("hosts.txt");
    int n = file.GetEntryCount();
    _hosts.Realloc(n);
    _hosts.Resize(n);
    for (int i = 0; i < n; i++)
    {
        const ParamEntry& entry = file.GetEntry(i);
        RemoteHostAddress& address = _hosts[i];

        address.name = entry.GetName();
        RString value = entry;

        const char* ptr = strrchr(value, ':');
        if (ptr)
        {
            address.ip = value.Substring(0, ptr - value);
            address.port = atoi(ptr + 1);
        }
        else
        {
            address.ip = value;
            address.port = DefaultNetworkPort;
        }
    }

    if (startEnum)
    {
        return StartEnumHosts();
    }
    return true;
}

void NetworkManager::Done()
{
    _client = nullptr;
    _server = nullptr;
    _sessionEnum = nullptr;
}

// void NetworkManager::GetSessions(AutoArray<SessionInfo, MemAllocSA> &sessions)
void NetworkManager::GetSessions(AutoArray<SessionInfo>& sessions)
{
    if (_sessionEnum)
    {
        _sessionEnum->GetSessions(sessions);
    }

    for (int i = 0; i < sessions.Size(); i++)
    {
        SessionInfo& info = sessions[i];

        info.badActualVersion = info.actualVersion < MP_VERSION_REQUIRED;
        info.badRequiredVersion = MP_VERSION_ACTUAL < info.requiredVersion;

        info.badMod = info.equalModRequired && stricmp(info.mod, ModSystem::GetModNames()) != 0;
        info.badTag = stricmp(info.versionTag, GetVersionTag()) != 0;
    }
}

void CheckMPVersion(SessionInfo& info)
{
    info.badActualVersion = info.actualVersion < MP_VERSION_REQUIRED;
    info.badRequiredVersion = MP_VERSION_ACTUAL < info.requiredVersion;
}

RString NetworkManager::IPToGUID(RString ip, int port)
{
    if (_sessionEnum)
    {
        return _sessionEnum->IPToGUID(ip, port);
    }
    return nullptr;
}

bool NetworkManager::CreateSession(int port, RString password)
{
    StopEnumHosts();

    // create server
    GDebugger.PauseCheckingAlive();
    ProgressStart(LocalizeString(IDS_CREATE_SERVER));
    _server = new NetworkServer(this, port, password);
    if (!_server->IsValid())
    {
        _server = nullptr;
        StartEnumHosts();
        ProgressFinish();
        GDebugger.ResumeCheckingAlive();
        return false;
    }
    ProgressFinish();
    GDebugger.ResumeCheckingAlive();

    // retrieve local address
    char address[512];
    address[0] = 0;
    if (!_server->GetURL(address, sizeof(address)))
    {
        _server = nullptr;
        StartEnumHosts();
        ProgressFinish();
        GDebugger.ResumeCheckingAlive();
        return false;
    }
    char botAddress[512];
    const char* portSuffix = strrchr(address, ':');
    if (portSuffix)
    {
        snprintf(botAddress, sizeof(botAddress), "127.0.0.1%s", portSuffix);
    }
    else
    {
        snprintf(botAddress, sizeof(botAddress), "127.0.0.1:%d", port);
    }

    // create bot client (internal client for object ownership, respawns, etc.)
    GDebugger.PauseCheckingAlive();
    ProgressStart(LocalizeString(IDS_CREATE_CLIENT));
    _client = new NetworkClient(this, botAddress, password, true);
    if (!_client->IsValid())
    {
        _server = nullptr;
        _client = nullptr;
        StartEnumHosts();
        ProgressFinish();
        GDebugger.ResumeCheckingAlive();
        return false;
    }
    ProgressFinish();
    GDebugger.ResumeCheckingAlive();

    return true;
}

bool NetworkManager::WaitForSession()
{
    ProgressStart(LocalizeString(IDS_DISP_CLIENT_TEXT));

    StartEnumHosts();
    DWORD lastEnumTime = ::GlobalTickCount();
    while (true)
    {
        {
            SDL_PumpEvents();
            const bool* keystate = SDL_GetKeyboardState(nullptr);
            if (keystate[SDL_SCANCODE_ESCAPE])
            {
                ProgressFinish();
                return false;
            }
        }

        if (_sessionEnum->NSessions() > 0)
        {
            break;
        }

        Sleep(100);
        I_AM_ALIVE();
        if (::GlobalTickCount() - lastEnumTime > 2500)
        {
            StartEnumHosts();
            lastEnumTime = ::GlobalTickCount();
        }
    }
    StopEnumHosts();
    ProgressFinish();
    return true;
}

ConnectResult NetworkManager::JoinSession(RString guid, RString password)
{
    StopEnumHosts();

    // create client
    GDebugger.PauseCheckingAlive();
    ProgressStart(LocalizeString(IDS_CREATE_CLIENT));
    _client = new NetworkClient(this, guid, password, false);
    if (!_client->IsValid())
    {
        ConnectResult result = _client->GetConnectResult();
        _client = nullptr;
        ProgressFinish();
        GDebugger.ResumeCheckingAlive();
        StartEnumHosts();
        return result;
    }
    ProgressFinish();
    GDebugger.ResumeCheckingAlive();

    return CROK;
}

void NetworkManager::Close()
{
    GDebugger.PauseCheckingAlive();

    // remove server and client
    _client = nullptr;
    _server = nullptr;

    StartEnumHosts();

    GDebugger.ResumeCheckingAlive();
}

void NetworkManager::GetPlayers(AutoArray<NetPlayerInfo, MemAllocSA>& players)
{
    if (_client)
    {
        _client->GetPlayers(players);
    }
    else
    {
        players.Resize(0);
    }
}

unsigned NetworkManager::CleanUpMemory()
{
    unsigned ret = 0;
    if (_server)
    {
        ret += _server->CleanUpMemory();
    }
    if (_client)
    {
        ret += _client->CleanUpMemory();
    }
    return ret;
}

void NetworkManager::CreateMission(RString mission, RString world)
{
    if (_server)
    {
        _server->CreateMission(mission, world);
    }
}

void NetworkManager::KickOff(int dpnid, KickOffReason reason)
{
    if (_server)
    {
        _server->KickOff(dpnid, reason);
    }
}

void NetworkManager::Ban(int dpnid)
{
    if (_server)
    {
        _server->Ban(dpnid);
    }
}

void NetworkManager::LockSession(bool lock)
{
    if (_server)
    {
        _server->LockSession(lock);
    }
}

void NetworkManager::InitMission(bool cadetMode)
{
    if (_server)
    {
        _server->InitMission(cadetMode);
    }
}

void NetworkManager::OnSimulate()
{
    if (_sessionEnum && _sessionEnum->RunningEnumHosts())
    {
        // refresh
        float age = Glob.uiTime - _lastEnumHosts;
        if (age > 3) // Sockets refresh timeout
        {
            StartEnumHosts();
        }
    }

    if (_client)
    {
        _client->OnSimulate();
    }
    if (_server)
    {
        _server->OnSimulate();
    }
}

NetworkGameState NetworkManager::GetGameState() const
{
    if (_client)
    {
        return _client->GetGameState();
    }
    else if (_server && AppConfig::Instance().IsSimulateMode())
    {
        return _server->GetGameState();
    }
    else
    {
        return NGSNone;
    }
}

NetworkGameState NetworkManager::GetServerState() const
{
    if (_client)
    {
        return _client->GetServerState();
    }
    else if (_server && AppConfig::Instance().IsSimulateMode())
    {
        return _server->GetGameState();
    }
    else
    {
        LOG_DEBUG(Network, "[GetServerState] _client is null, returning NGSNone");
        return NGSNone;
    }
}

NetworkGameState NetworkManager::GetClientServerState() const
{
    if (_client)
    {
        return _client->GetServerState();
    }
    return NGSNone;
}

bool NetworkManager::WasServerPlaying() const
{
    if (_client)
        return _client->WasPlaying();
    return false;
}

bool NetworkManager::IsJIP() const
{
    if (_client)
        return _client->IsJIP();
    return false;
}

bool NetworkManager::SaveWorldState(const char* filename)
{
    if (_server)
        return _server->SaveWorldState(filename);
    return false;
}

bool NetworkManager::LoadWorldState(const char* filename)
{
    if (_server)
        return _server->LoadWorldState(filename);
    return false;
}

void NetworkManager::SetGameState(NetworkGameState state)
{
    if (_server)
    {
        _server->SetGameState(state);
    }
}

bool NetworkManager::IsGameMaster() const
{
    if (_client)
    {
        return _client->IsGameMaster();
    }
    else
    {
        return false;
    }
}

bool NetworkManager::IsAdmin() const
{
    if (_client)
    {
        return _client->IsAdmin();
    }
    else
    {
        return false;
    }
}

bool NetworkManager::HasAdminLoggedIn() const
{
    if (_server)
    {
        return _server->AdminLoginGranted();
    }
    if (_client)
    {
        return _client->IsGameMaster();
    }
    return false;
}

ConnectionQuality NetworkManager::GetConnectionQuality() const
{
    if (_client)
    {
        return _client->GetConnectionQuality();
    }
    else
    {
        return CQGood;
    }
}

void NetworkManager::GetParams(float& param1, float& param2) const
{
    if (_client)
    {
        _client->GetParams(param1, param2);
    }
}

void NetworkManager::SetParams(float param1, float param2)
{
    if (_client)
    {
        _client->SetParams(param1, param2);
    }
}

const MissionHeader* NetworkManager::GetMissionHeader() const
{
    if (_server)
    {
        return _server->GetMissionHeader();
    }
    else if (_client)
    {
        return _client->GetMissionHeader();
    }
    else
    {
        return nullptr;
    }
}

int NetworkManager::GetPlayer() const
{
    if (_client)
    {
        return _client->GetPlayer();
    }
    else
    {
        return 0;
    }
}

int NetworkManager::NPlayerRoles() const
{
    if (_client)
    {
        return _client->NPlayerRoles();
    }
    else
    {
        return 0;
    }
}

const PlayerRole* NetworkManager::GetPlayerRole(int role) const
{
    if (_client)
    {
        return _client->GetPlayerRole(role);
    }
    else
    {
        return nullptr;
    }
}

const PlayerRole* NetworkManager::GetMyPlayerRole() const
{
    if (_client)
    {
        return _client->GetMyPlayerRole();
    }
    else
    {
        return nullptr;
    }
}

const PlayerIdentity* NetworkManager::FindIdentity(int dpnid) const
{
    if (_client)
    {
        return _client->FindIdentity(dpnid);
    }
    else
    {
        return nullptr;
    }
}

const AutoArray<PlayerIdentity>* NetworkManager::GetIdentities() const
{
    if (_client)
    {
        return _client->GetIdentities();
    }
    else
    {
        return nullptr;
    }
}

void NetworkManager::GetTransferStats(int& curBytes, int& totBytes)
{
    if (_client)
    {
        _client->GetTransferStats(curBytes, totBytes);
    }
}

void NetworkManager::ClientReady(NetworkGameState state)
{
    if (_client)
    {
        _client->ClientReady(state);
    }
}

void NetworkManager::AssignPlayer(int role, int player)
{
    if (_client)
    {
        _client->AssignPlayer(role, player);
    }
}

void NetworkManager::UnassignPlayer(int role)
{
    if (_client)
    {
        _client->AssignPlayer(role, AI_PLAYER);
    }
}

void NetworkManager::SelectPlayer(int player, Person* person, bool respawn)
{
    if (_client)
    {
        _client->SelectPlayer(player, person, respawn);
    }
}

void NetworkManager::PlaySound(RString name, Vector3Par position, Vector3Par speed, float volume, float freq,
                               IWave* wave)
{
    if (_client)
    {
        _client->PlaySound(name, position, speed, volume, freq, wave);
    }
}

void NetworkManager::SoundState(IWave* wave, SoundStateType state)
{
    if (_client)
    {
        _client->SoundState(wave, state);
    }
}

NetworkGameState NetworkManager::GetPlayerState(int dpid)
{
    if (_server)
    {
        return _server->GetPlayerState(dpid);
    }
    else
    {
        return NGSNone;
    }
}

RString NetworkManager::GetPlayerName(int dpid)
{
    if (_server)
    {
        return _server->GetPlayerName(dpid);
    }
    else
    {
        return "Unknown player";
    }
}

Vector3 NetworkManager::GetCameraPosition(int dpid)
{
    if (_server)
    {
        return _server->GetCameraPosition(dpid);
    }
    else
    {
        return VZero;
    }
}

NetworkObject* NetworkManager::GetObject(NetworkId& id)
{
    if (_client)
    {
        return _client->GetObject(id);
    }
    return nullptr;
}

void NetworkManager::CreateAllObjects()
{
    if (_client)
    {
        _client->CreateAllObjects();
    }
}

void NetworkManager::DestroyAllObjects()
{
    if (_server)
    {
        _server->DestroyAllObjects();
    }
    if (_client)
    {
        _client->DestroyAllObjects();
    }
}

void NetworkManager::DeleteObject(NetworkId& id)
{
    if (_client)
    {
        _client->DeleteObject(id);
    }
}

bool NetworkManager::CreateVehicle(Vehicle* veh, VehicleListType type, RString name, int idVeh)
{
    if (_client)
    {
        return _client->CreateVehicle(veh, type, name, idVeh);
    }
    return false;
}

bool NetworkManager::CreateCenter(AICenter* center)
{
    if (_client)
    {
        return _client->CreateCenter(center);
    }
    return false;
}

bool NetworkManager::CreateObject(NetworkObject* object)
{
    if (_client)
    {
        return _client->CreateObject(object);
    }
    return false;
}

bool NetworkManager::PublicExec(RString command)
{
    if (_client)
    {
        return _client->PublicExec(command);
    }
    return false;
}

bool NetworkManager::RemoteExec(RString name, const AutoArray<char>& params, int target,
                                const AutoArray<char>& targetSpec, bool jip, RString jipKey, bool callMode)
{
    if (_client)
    {
        return _client->RemoteExec(name, params, target, targetSpec, jip, jipKey, callMode);
    }
    return false;
}

bool NetworkManager::RemoteExecRemove(RString jipKey)
{
    if (_client)
    {
        return _client->RemoteExecRemove(jipKey);
    }
    return false;
}

bool NetworkManager::CreateCommand(AISubgroup* subgrp, int index, Command* cmd)
{
    if (_client)
    {
        return _client->CreateCommand(subgrp, index, cmd);
    }
    return false;
}

void NetworkManager::DeleteCommand(AISubgroup* subgrp, int index, Command* cmd)
{
    if (_client)
    {
        _client->DeleteCommand(subgrp, index, cmd);
    }
}

void NetworkManager::AskForDammage(Object* who, EntityAI* owner, Vector3Par modelPos, float val, float valRange,
                                   RString ammo)
{
    if (_client)
    {
        _client->AskForDammage(who, owner, modelPos, val, valRange, ammo);
    }
}

void NetworkManager::AskForSetDammage(Object* who, float dammage)
{
    if (_client)
    {
        _client->AskForSetDammage(who, dammage);
    }
}

void NetworkManager::AskForAddImpulse(Vehicle* vehicle, Vector3Par force, Vector3Par torque)
{
    if (_client)
    {
        _client->AskForAddImpulse(vehicle, force, torque);
    }
}

void NetworkManager::AskForMove(Object* vehicle, Vector3Par pos)
{
    if (_client)
    {
        _client->AskForMove(vehicle, pos);
    }
}

void NetworkManager::AskForMove(Object* vehicle, Matrix4Par trans)
{
    if (_client)
    {
        _client->AskForMove(vehicle, trans);
    }
}

void NetworkManager::AskForJoin(AIGroup* join, AIGroup* group)
{
    if (_client)
    {
        _client->AskForJoin(join, group);
    }
}

void NetworkManager::AskForJoin(AIGroup* join, OLinkArray<AIUnit>& units)
{
    if (_client)
    {
        _client->AskForJoin(join, units);
    }
}

void NetworkManager::ExplosionDammageEffects(EntityAI* owner, Shot* shot, Object* directHit, Vector3Par pos,
                                             Vector3Par dir, const AmmoType* type, bool enemyDammage)
{
    if (_client)
    {
        _client->ExplosionDammageEffects(owner, shot, directHit, pos, dir, type, enemyDammage);
    }
}

void NetworkManager::AskForGetIn(Person* soldier, Transport* vehicle, GetInPosition position)
{
    if (_client)
    {
        _client->AskForGetIn(soldier, vehicle, position);
    }
}

void NetworkManager::AskForGetOut(Person* soldier, Transport* vehicle, bool parachute)
{
    if (_client)
    {
        _client->AskForGetOut(soldier, vehicle, parachute);
    }
}

void NetworkManager::AskForChangePosition(Person* soldier, Transport* vehicle, UIActionType type)
{
    if (_client)
    {
        _client->AskForChangePosition(soldier, vehicle, type);
    }
}

void NetworkManager::AskForAimWeapon(EntityAI* vehicle, int weapon, Vector3Par dir)
{
    if (_client)
    {
        _client->AskForAimWeapon(vehicle, weapon, dir);
    }
}

void NetworkManager::AskForAimObserver(EntityAI* vehicle, Vector3Par dir)
{
    if (_client)
    {
        _client->AskForAimObserver(vehicle, dir);
    }
}

void NetworkManager::AskForSelectWeapon(EntityAI* vehicle, int weapon)
{
    if (_client)
    {
        _client->AskForSelectWeapon(vehicle, weapon);
    }
}

void NetworkManager::AskForAmmo(EntityAI* vehicle, int weapon, int burst)
{
    if (_client)
    {
        _client->AskForAmmo(vehicle, weapon, burst);
    }
}

void NetworkManager::AskForHideBody(Person* vehicle)
{
    if (_client)
    {
        _client->AskForHideBody(vehicle);
    }
}

void NetworkManager::FireWeapon(EntityAI* vehicle, int weapon, const Magazine* magazine, EntityAI* target)
{
    if (_client)
    {
        _client->FireWeapon(vehicle, weapon, magazine, target);
    }
}

void NetworkManager::UpdateWeapons(EntityAI* vehicle)
{
    if (_client)
    {
        _client->UpdateWeapons(vehicle);
    }
}

void NetworkManager::AddWeaponCargo(VehicleSupply* vehicle, RString weapon)
{
    if (_client)
    {
        _client->AddWeaponCargo(vehicle, weapon);
    }
}

void NetworkManager::RemoveWeaponCargo(VehicleSupply* vehicle, RString weapon)
{
    if (_client)
    {
        _client->RemoveWeaponCargo(vehicle, weapon);
    }
}

void NetworkManager::AddMagazineCargo(VehicleSupply* vehicle, const Magazine* magazine)
{
    if (_client)
    {
        _client->AddMagazineCargo(vehicle, magazine);
    }
}

void NetworkManager::RemoveMagazineCargo(VehicleSupply* vehicle, int creator, int id)
{
    if (_client)
    {
        _client->RemoveMagazineCargo(vehicle, creator, id);
    }
}

void NetworkManager::UpdateObject(NetworkObject* object)
{
    if (_client)
    {
        _client->UpdateObject(object, NMFGuaranteed);
    }
}

void NetworkManager::VehicleInit(VehicleInitCmd& init)
{
    if (_client)
    {
        _client->VehicleInit(init);
    }
}

void NetworkManager::OnVehicleDestroyed(EntityAI* killed, EntityAI* killer)
{
    if (_client)
    {
        _client->OnVehicleDestroyed(killed, killer);
    }
}

void NetworkManager::OnVehicleDamaged(EntityAI* damaged, EntityAI* killer, float damage, RString ammo)
{
    if (_client)
    {
        _client->OnVehicleDamaged(damaged, killer, damage, ammo);
    }
}

void NetworkManager::OnIncomingMissile(EntityAI* target, RString ammo, EntityAI* owner)
{
    if (_client)
    {
        _client->OnIncomingMissile(target, ammo, owner);
    }
}

void NetworkManager::MarkerCreate(int channel, AIUnit* sender, RefArray<NetworkObject>& units, ArcadeMarkerInfo& info)
{
    if (_client)
    {
        _client->MarkerCreate(channel, sender, units, info);
    }
}

void NetworkManager::MarkerDelete(RString name)
{
    if (_client)
    {
        _client->MarkerDelete(name);
    }
}

void NetworkManager::SetFlagOwner(Person* owner, EntityAI* carrier)
{
    if (_client)
    {
        _client->SetFlagOwner(owner, carrier);
    }
}

void NetworkManager::SetFlagCarrier(Person* owner, EntityAI* carrier)
{
    if (_client)
    {
        _client->SetFlagCarrier(owner, carrier);
    }
}

void NetworkManager::SendRadioMessage(NetworkSimpleObject* msg)
{
    if (_client)
    {
        _client->SendMsg(msg, NMFGuaranteed);
    }
}

bool NetworkManager::ProcessCommand(RString command)
{
    if (_client)
    {
        return _client->ProcessCommand(command);
    }
    return false;
}

void NetworkManager::SendKick(int player)
{
    if (_client)
    {
        _client->SendKick(player);
    }
}

void NetworkManager::SendLockSession(bool lock)
{
    if (_client)
    {
        _client->SendLockSession(lock);
    }
}

bool NetworkManager::CanSelectMission() const
{
    if (_client)
    {
        return _client->CanSelectMission();
    }
    return false;
}

bool NetworkManager::CanVoteMission() const
{
    if (_client)
    {
        return _client->CanVoteMission();
    }
    return false;
}

const AutoArray<RString>& NetworkManager::GetServerMissions() const
{
    NET_ERROR(_client);
    return _client->GetServerMissions();
}

void NetworkManager::SelectMission(RString mission, bool cadetMode)
{
    if (_client)
    {
        _client->SelectMission(mission, cadetMode);
    }
}

void NetworkManager::VoteMission(RString mission, bool cadetMode)
{
    if (_client)
    {
        _client->VoteMission(mission, cadetMode);
    }
}

void NetworkManager::PublicVariable(RString name)
{
    if (_client)
    {
        _client->PublicVariable(name);
    }
}

void NetworkManager::Chat(int channel, RString text)
{
    if (_client)
    {
        _client->Chat(channel, text);
    }
}

void NetworkManager::Chat(int channel, AIUnit* sender, RefArray<NetworkObject>& units, RString text)
{
    if (_client)
    {
        _client->Chat(channel, sender, units, text);
    }
}

void NetworkManager::Chat(int channel, RString sender, RefArray<NetworkObject>& units, RString text)
{
    if (_client)
    {
        _client->Chat(channel, sender, units, text);
    }
}

void NetworkManager::RadioChat(int channel, AIUnit* sender, RefArray<NetworkObject>& units, RString text,
                               RadioSentence& sentence)
{
    if (_client)
    {
        _client->RadioChat(channel, sender, units, text, sentence);
    }
}

void NetworkManager::RadioChatWave(int channel, RefArray<NetworkObject>& units, RString wave, AIUnit* sender,
                                   RString senderName)
{
    if (_client)
    {
        _client->RadioChatWave(channel, units, wave, sender, senderName);
    }
}

void NetworkManager::SetVoiceChannel(int channel)
{
    if (_client)
    {
        _client->SetVoiceChannel(channel);
    }
}

void NetworkManager::SetVoiceChannel(int channel, RefArray<NetworkObject>& units)
{
    if (_client)
    {
        _client->SetVoiceChannel(channel, units);
    }
}

void NetworkManager::TransferFile(RString dest, RString source)
{
    if (_client)
    {
        _client->TransferFile(TO_SERVER, dest, source);
    }
}

void NetworkManager::SendMissionFile()
{
    if (_server)
    {
        _server->SendMissionFile();
    }
}

RespawnMode NetworkManager::GetRespawnMode() const
{
    if (_client)
    {
        return _client->GetRespawnMode();
    }
    return RespawnNone;
}

float NetworkManager::GetRespawnDelay() const
{
    if (_client)
    {
        return _client->GetRespawnDelay();
    }
    return 0;
}

void NetworkManager::Respawn(Person* soldier, Vector3Par pos)
{
    if (_client)
    {
        _client->Respawn(soldier, pos);
    }
}

RString NetworkManager::GetLocalPlayerName() const
{
    if (_client)
    {
        return _client->GetLocalPlayerName();
    }
    return Glob.header.playerName;
}

void NetworkManager::ShowTarget(Person* vehicle, TargetType* target)
{
    if (_client)
    {
        _client->ShowTarget(vehicle, target);
    }
}

void NetworkManager::ShowGroupDir(Person* vehicle, Vector3Par dir)
{
    if (_client)
    {
        _client->ShowGroupDir(vehicle, dir);
    }
}

void NetworkManager::GroupSynchronization(AIGroup* grp, int synchronization, bool active)
{
    if (_client)
    {
        _client->GroupSynchronization(grp, synchronization, active);
    }
}

void NetworkManager::DetectorActivation(Detector* det, bool active)
{
    if (_client)
    {
        _client->DetectorActivation(det, active);
    }
}

void NetworkManager::AskForCreateUnit(AIGroup* group, RString type, Vector3Par position, RString init, float skill,
                                      Rank rank)
{
    if (_client)
    {
        _client->AskForCreateUnit(group, type, position, init, skill, rank);
    }
}

void NetworkManager::AskForDeleteVehicle(Entity* veh)
{
    if (_client)
    {
        _client->AskForDeleteVehicle(veh);
    }
}

void NetworkManager::AskForReceiveUnitAnswer(AIUnit* from, AISubgroup* to, int answer)
{
    if (_client)
    {
        _client->AskForReceiveUnitAnswer(from, to, answer);
    }
}

void NetworkManager::AskForGroupRespawn(Person* person, EntityAI* killer)
{
    if (_client)
    {
        _client->AskForGroupRespawn(person, killer);
    }
}

void NetworkManager::AskForActivateMine(Mine* mine, bool activate)
{
    if (_client)
    {
        _client->AskForActivateMine(mine, activate);
    }
}

void NetworkManager::AskForInflameFire(Fireplace* fireplace, bool fire)
{
    if (_client)
    {
        _client->AskForInflameFire(fireplace, fire);
    }
}

void NetworkManager::AskForAnimationPhase(Entity* vehicle, RString animation, float phase)
{
    if (_client)
    {
        _client->AskForAnimationPhase(vehicle, animation, phase);
    }
}

void NetworkManager::CopyUnitInfo(Person* from, Person* to)
{
    if (_client)
    {
        _client->CopyUnitInfo(from, to);
    }
}

Time NetworkManager::GetEstimatedEndTime() const
{
    if (_client)
    {
        return _client->GetEstimatedEndTime();
    }
    else
    {
        return TIME_MIN;
    }
}

void NetworkManager::SetEstimatedEndTime(Time time)
{
    if (_server)
    {
        _server->SetEstimatedEndTime(time);
    }
}

void NetworkManager::DisposeBody(Person* body)
{
    if (_client)
    {
        _client->DisposeBody(body);
    }
}

#if _ENABLE_CHEATS
extern bool forceControlsPaused;
#endif

bool NetworkManager::IsControlsPaused()
{
#if _ENABLE_CHEATS
    if (forceControlsPaused)
        return true;
#endif
    if (_client)
    {
        return _client->IsControlsPaused();
    }
    return false;
}

float NetworkManager::GetLastMsgAgeReliable()
{
    if (_client)
    {
        return _client->GetLastMsgAgeReliable();
    }
    return 0;
}

// Global instance of Network Manager class
NetworkManager GNetworkManager;

INetworkManager& GetNetworkManager()
{
    return GNetworkManager;
}

RString NetObjToNetId(NetworkObject* obj)
{
    if (GWorld->GetMode() == GModeNetware)
    {
        NetworkId id = obj->GetNetworkId();
        return NetworkIdToNetId(id);
    }
    else
    {
        return RString();
    }
}

NetworkObject* NetIdToNetObj(const char* netId)
{
    const char* idStr = strchr(netId, ':');
    if (idStr && GWorld->GetMode() == GModeNetware)
    {
        int creator = atoi(netId);
        int objId = atoi(idStr + 1);
        NetworkId id(creator, objId);

        return GetNetworkManager().GetObject(id);
    }
    return nullptr;
}

RString GetLocalPlayerName()
{
    return GNetworkManager.GetLocalPlayerName();
}

#if _ENABLE_CHEATS
void NetworkComponent::StatMsgSent(int msg, int size)
{
    for (int i = 0; i < _statistics.Size(); i++)
    {
        if (_statistics[i].message == msg)
        {
            _statistics[i].msgSent++;
            _statistics[i].sizeSent += size;
            return;
        }
    }
    int index = _statistics.Add();
    _statistics[index].message = msg;
    _statistics[index].msgSent++;
    _statistics[index].sizeSent += size;
}

void NetworkComponent::StatMsgReceived(int msg, int size)
{
    for (int i = 0; i < _statistics.Size(); i++)
    {
        if (_statistics[i].message == msg)
        {
            _statistics[i].msgReceived++;
            _statistics[i].sizeReceived += size;
            return;
        }
    }
    int index = _statistics.Add();
    _statistics[index].message = msg;
    _statistics[index].msgReceived++;
    _statistics[index].sizeReceived += size;
}

// Round screen position to pixel
#define CX(x) (toInt((x) * w) + 0.5)
// Round screen position to pixel
#define CY(y) (toInt((y) * h) + 0.5)

// Compare statistics items for sorted display
int CmpSentMessages(const NetworkStatisticsItem* info1, const NetworkStatisticsItem* info2)
{
    float diff = info2->msgSent - info1->msgSent;
    if (diff != 0)
        return sign(diff);
    diff = info2->sizeSent - info1->sizeSent;
    if (diff != 0)
        return sign(diff);
    return info2->message - info1->message;
}

// Compare statistics items for sorted display
int CmpReceivedMessages(const NetworkStatisticsItem* info1, const NetworkStatisticsItem* info2)
{
    float diff = info2->msgReceived - info1->msgReceived;
    if (diff != 0)
        return sign(diff);
    diff = info2->sizeReceived - info1->sizeReceived;
    if (diff != 0)
        return sign(diff);
    return info2->message - info1->message;
}

// Draw network statistics by types of messages
void DrawNetworkStatistics()
{
    NetworkComponent* component = GNetworkManager.GetServer();
    if (!component)
        component = GNetworkManager.GetClient();
    if (!component)
        return;
    AutoArray<NetworkStatisticsItem>& stat = component->GetStatistics();

    RString title;
    switch (outputDiags)
    {
        case 2:
            QSort(stat.Data(), stat.Size(), CmpSentMessages);
            title = "Outgoing messages";
            break;
        case 3:
            QSort(stat.Data(), stat.Size(), CmpReceivedMessages);
            title = "Incomming messages";
            break;
        default:
            return;
    }

    PackedColor color(Color(1, 1, 1, 1));
    static Ref<Font> font;
    static float size;
    if (!font)
    {
        font = GEngine->LoadFont(GetFontID("tahomaB48"));
        size = 0.024;
    }
    float fontHeight = size;

    const float xb = 0.05;
    const float xe = 0.95;
    const float yb = 0.05;
    const float ye = 0.95;
    const float rowHeight = 0.03;
    const int nRows = toInt((ye - yb) / rowHeight) - 1;
    const int columns = 4;
    const float colWidths[columns] = {
        0.4, 0.16, 0.16, 0.16
        // 0.3
        // 0.2,
        // 0.3
    };

    const float w = GEngine->Width2D();
    const float h = GEngine->Height2D();

    const float cxb = CX(xb);
    const float cxe = CX(xe);
    const float cyb = CY(yb);
    const float cye = CY(ye);

    // background
    PackedColor bgColor(Color(0, 0, 0, 0.5));
    MipInfo mip = GEngine->TextBank()->UseMipmap(nullptr, 0, 0);
    GEngine->Draw2D(mip, bgColor, Rect2DPixel(cxb, cyb, cxe - cxb, cye - cyb));

    // lines
    float bottom = yb;
    float cbottom = CY(bottom);
    GEngine->DrawLine(Line2DPixel(cxb, cbottom, cxe, cbottom), color, color);
    for (int i = 0; i < nRows + 1; i++)
    {
        bottom += rowHeight;
        cbottom = CY(bottom);
        GEngine->DrawLine(Line2DPixel(cxb, cbottom, cxe, cbottom), color, color);
    }
    float left = xb;
    float cleft = cxb;
    GEngine->DrawLine(Line2DPixel(cleft, cyb, cleft, cbottom), color, color);
    for (int i = 0; i < columns; i++)
    {
        left += colWidths[i];
        cleft = CX(left);
        GEngine->DrawLine(Line2DPixel(cleft, cyb, cleft, cbottom), color, color);
    }

    // titles
    float top = yb + 0.5 * (rowHeight - fontHeight);
    left = xb;
    RString str = title;
    float x = left + 0.5 * (colWidths[0] - GEngine->GetTextWidth(size, font, str));
    GEngine->DrawText(Point2DFloat(x, top), size, font, color, str);

    left += colWidths[0];
    str = "# of messages";
    x = left + 0.5 * (colWidths[1] - GEngine->GetTextWidth(size, font, str));
    GEngine->DrawText(Point2DFloat(x, top), size, font, color, str);

    left += colWidths[1];
    str = "Tot. B";
    x = left + 0.5 * (colWidths[2] - GEngine->GetTextWidth(size, font, str));
    GEngine->DrawText(Point2DFloat(x, top), size, font, color, str);

    left += colWidths[2];
    str = "Avg. B";
    x = left + 0.5 * (colWidths[3] - GEngine->GetTextWidth(size, font, str));
    GEngine->DrawText(Point2DFloat(x, top), size, font, color, str);

    // values
    for (int i = 0; i < nRows && i < stat.Size(); i++)
    {
        top += rowHeight;

        left = xb;
        str = NetworkMessageTypeNames[stat[i].message];
        x = left + 0.5 * (colWidths[0] - GEngine->GetTextWidth(size, font, str));
        GEngine->DrawText(Point2DFloat(x, top), size, font, color, str);
        left += colWidths[0];

        int val1 = outputDiags == 2 ? stat[i].msgSent : stat[i].msgReceived;
        int val2 = outputDiags == 2 ? stat[i].sizeSent : stat[i].sizeReceived;

        if (val1 > 0)
        {
            x = left + 0.5 * (colWidths[1] - GEngine->GetTextWidthF(size, font, "%d", val1));
            GEngine->DrawTextF(Point2DFloat(x, top), size, font, color, "%d", val1);
        }
        left += colWidths[1];

        if (val2 > 0)
        {
            x = left + 0.5 * (colWidths[2] - GEngine->GetTextWidthF(size, font, "%d", val2));
            GEngine->DrawTextF(Point2DFloat(x, top), size, font, color, "%d", val2);
        }
        left += colWidths[2];
        if (val2 > 0 && val1 > 0)
        {
            float avg = float(val2) / val1;
            x = left + 0.5 * (colWidths[2] - GEngine->GetTextWidthF(size, font, "%.1f", avg));
            GEngine->DrawTextF(Point2DFloat(x, top), size, font, color, "%.1f", avg);
        }
    }
}

#endif

void NetworkComponent::StatRawMsgSent(int to, int size)
{
    for (int i = 0; i < _rawStatistics.Size(); i++)
    {
        if (_rawStatistics[i].player == to)
        {
            _rawStatistics[i].msgSent++;
            _rawStatistics[i].sizeSent += size;
            return;
        }
    }
    int index = _rawStatistics.Add();
    _rawStatistics[index].player = to;
    _rawStatistics[index].msgSent++;
    _rawStatistics[index].sizeSent += size;
}

void NetworkComponent::StatRawMsgReceived(int from, int size)
{
    for (int i = 0; i < _rawStatistics.Size(); i++)
    {
        if (_rawStatistics[i].player == from)
        {
            _rawStatistics[i].msgReceived++;
            _rawStatistics[i].sizeReceived += size;
            return;
        }
    }
    int index = _rawStatistics.Add();
    _rawStatistics[index].player = from;
    _rawStatistics[index].msgReceived++;
    _rawStatistics[index].sizeReceived += size;
}
