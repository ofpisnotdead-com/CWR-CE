#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/Config.hpp>
#include <Poseidon/Core/Config/UserConfig.hpp>
#include <Poseidon/Core/ModSystem.hpp>
#include <Poseidon/Network/NetworkImpl.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/UI/Locale/StringtableExt.hpp>
#include <Poseidon/UI/Locale/Languages.hpp>

#include <Poseidon/AI/ArcadeTemplate.hpp>
#include <Poseidon/AI/AI.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/Game/Chat.hpp>
#include <Poseidon/Network/XML/Xml.hpp>
#include <Poseidon/Foundation/Strings/StrFormat.hpp>

#include <Poseidon/Foundation/Platform/GamePaths.hpp>
#include <Poseidon/Foundation/Algorithms/Qsort.hpp>

#include <Poseidon/World/Entities/Vehicles/AllAIVehicles.hpp>
#include <Poseidon/World/Entities/Vehicles/SeaGull.hpp>
#include <Poseidon/World/Detection/Detector.hpp>
#include <Poseidon/World/Entities/Weapons/Shots.hpp>

#include <Poseidon/Dev/Debug/DebugTrap.hpp>

#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/IO/PackFiles.hpp>
#include <Poseidon/IO/FileServer.hpp>
#include <Poseidon/IO/Filesystem/FileOps.hpp>

#include <Random/randomGen.hpp>
#include <Poseidon/Game/Commands/GameStateExt.hpp>

#include <Poseidon/Core/Progress.hpp>

#include <Poseidon/Game/UiActions.hpp>

#include <Poseidon/Foundation/Algorithms/Crc.hpp>
#include <float.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3D.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathOpt.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/Foundation/Types/Memtype.h>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/platform.hpp>

#ifdef _WIN32
#include <io.h>
#endif

#include <Poseidon/World/Scene/Camera/Camera.hpp>

#include <Poseidon/Foundation/Strings/Mbcs.hpp>

using Poseidon::Foundation::Time;

// Use private memory heap for system messages
#define USE_PRIVATE_HEAP 1

using namespace Poseidon;
namespace Poseidon::Foundation
{
template class Ref<NetworkObject>;
} // namespace Poseidon::Foundation

#if _ENABLE_CHEATS

// current diagnostic type
// - 0: no diagnostics
// - 1: basic statistics
// - 2: outgoing messages
// - 3: incomming messages
int outputDiags = 0;
// enable general diagnostic logs
bool outputLogs = false;
#endif

int MaxMsgSend = 26;
int DSMaxMsgSend = 128;
int MaxSizeGuaranteed = 512;
int MaxSizeNonguaranteed = 256;
int MinBandwidth = 28800;
int DSMinBandwidth = 8 * 1024 * 1024;
int MaxBandwidth = INT_MAX;
float MinErrorToSend = 0.01f;

float ThrottleGuaranteed = 1.0f;

// Check consistency of local objects
#define CHECK_MSG 0

// Temporary directory for Network server (temporary directory must be different for different dedicated servers)
RString ServerTmpDir;
// Return temporary directory for Network server
RString GetServerTmpDir()
{
    return ServerTmpDir;
}

unsigned int CalculateConfigCRC(const char* path);
#include <Poseidon/Network/IntegrityCheck.hpp>

#define CLIENT_CRC_KNOWN 0
#if CLIENT_CRC_KNOWN
static const ExeCRCBlock ToCheck[] = {
    {0x0042a000, 0x0400, 0xf39fef09}, // ID changer
    {0x00427c00, 0x0400, 0x4aca1424},
    {0x00428000, 0x0400, 0xed59f759},
    {0x00428400, 0x0400, 0x3fe4da96},
    {0x00435800, 0x0400, 0x212315ef}, // ammo cheat
    {0, 0, 0},
};
// check known valid client crc
static int GetReferenceCRC(int offset, int size)
{
    for (const ExeCRCBlock* crc = ToCheck; crc->size > 0; crc++)
    {
        if (crc->offset == offset && crc->size == size)
        {
            return crc->crc;
        }
    }
    return 0;
}
const ExeCRCBlock* GetCodeCheck()
{
    return ToCheck;
}

#else
static const ExeCRCBlock ToCheck[] = {{0, 0, 0}};
const ExeCRCBlock* GetCodeCheck()
{
    return ToCheck;
}
// check known valid client crc
static int GetReferenceCRC(int offset, int size)
{
    return CalculateExeCRC(offset, size);
}
#endif

// Calculate answer to any integrity question
// server: check the stored CRC instead of the real exe.
unsigned int IntegrityCheckAnswer(IntegrityQuestionType type, const IntegrityQuestion& q, bool server)
{
    switch (type)
    {
        case IQTConfig:
            return CalculateConfigCRC(q.name);
        case IQTExe:
            if (server)
            {
                return GetReferenceCRC(q.offset, q.size);
            }
            return CalculateExeCRC(q.offset, q.size);
        case IQTData:
            return CalculateDataCRC(q.name, q.offset, q.size);
    }
    return 0;
}

// General diagnostics level
// - level == 0 - nothing
// - level == 1 - errors
// - level == 2 - basic statictics
// - level == 3 - extended statistics
// - level == 4 - warnings
const int DiagLevel = 0;

// Return diagnostics level for given message type
int GetDiagLevel(NetworkMessageType type, bool remote)
{
    // level == 0 - nothing
    // level == 1 - errors
    // level == 2 - message headers
    // level == 3 - message bodies
    // level == 4 - all
    switch (type)
    {
        default:
        case NMTUpdateSeagull:
            return 0;
            // copy here to log remote messages (transferred via DPlay)
            //	case NMTGameState:
            //		return remote ? 3 : 0;
    }
}

// Names of message types (used for diagnostics output)
#define NMT_DEFINE_ENUM_NAMES(macro, class, name, description, group) #name,

const char* NetworkMessageTypeNames[NMTN] = {NETWORK_MESSAGE_TYPES(NMT_DEFINE_ENUM_NAMES)};

// Diagnostics logs in single simulation step
AutoArray<RString> DiagOutput;

// Write diagnostics logs to output
void WriteDiagOutput(bool server)
{
    if (DiagOutput.Size() == 0)
    {
        return;
    }

    if (server)
    {
        LOG_DEBUG(Network, "Server simulation, time = {:.3f}, {} rows", Glob.time.toFloat(), DiagOutput.Size());
    }
    else
    {
        LOG_DEBUG(Network, "Client simulation, time = {:.3f}, {} rows", Glob.time.toFloat(), DiagOutput.Size());
    }
    for (int i = 0; i < DiagOutput.Size(); i++)
    {
        LOG_DEBUG(Network, "{}", (const char*)DiagOutput[i]);
    }
    LOG_DEBUG(Network, "");
    DiagOutput.Clear();
}

// Write single line of text into file
static void WriteToFile(HANDLE file, RString string)
{
    DWORD written;
    WriteFile(file, string.Data(), string.GetLength(), &written, nullptr);
    WriteFile(file, "\r\n", 2, &written, nullptr);
}

// Write diagnostics logs into file
void PrintNetworkInfo(HANDLE file)
{
    if (DiagOutput.Size() == 0)
    {
        return;
    }

    WriteToFile(file, "Pending network output:");
    for (int i = 0; i < DiagOutput.Size(); i++)
    {
        WriteToFile(file, DiagOutput[i]);
    }
}

// Write message into diagnostics log
void DiagLogF(const char* format, ...)
{
    char buf[512];

    va_list arglist;
    va_start(arglist, format);
    vsprintf(buf, format, arglist);
    va_end(arglist);

    DiagOutput.Add(buf);
}

// Destroy file bank with given prefix
void RemoveBank(const char* prefix)
{
    int prefixLen = strlen(prefix);
    for (int i = 0; i < GFileBanks.Size();)
    {
        QFBank& bank = GFileBanks[i];
        if (strnicmp(bank.GetPrefix(), prefix, prefixLen) == 0)
        {
            GFileServer->FlushBank(&bank);
            GFileBanks.Delete(i);
        }
        else
        {
            i++;
        }
    }
}

extern bool EnumModDirectories(ModDirectoryCallback callback, void* context);

struct FindMissionPboContext
{
    const char* relPath;
    RString result;
};

static bool FindMissionPboCallback(RStringB dir, void* ctx)
{
    auto* c = static_cast<FindMissionPboContext*>(ctx);
    if (dir.GetLength() == 0)
        return false;
    char path[1024];
    // Try language-specific PBO first (e.g., mission.cz.pbo for Czech)
    const char* langSuffix = GetLanguagePboSuffix(GLanguage);
    if (langSuffix)
    {
        snprintf(path, sizeof(path), "%s\\%s.%s.pbo", (const char*)dir, c->relPath, langSuffix);
        if (FilePathExists(path))
        {
            snprintf(path, sizeof(path), "%s\\%s.%s", (const char*)dir, c->relPath, langSuffix);
            c->result = path;
            return true;
        }
    }
    snprintf(path, sizeof(path), "%s\\%s.pbo", (const char*)dir, c->relPath);
    if (FilePathExists(path))
    {
        snprintf(path, sizeof(path), "%s\\%s", (const char*)dir, c->relPath);
        c->result = path;
        return true;
    }
    return false;
}

// Create file bank for multiplayer mission
RString CreateMPMissionBank(RString filename, RString island)
{
    // remove bank
    const std::string prefix = GameDirs::MPCurrentPrefix();
    RemoveBank(prefix.c_str());

    // prefer mod directory PBO over base game
    FindMissionPboContext ctx;
    ctx.relPath = filename;
    RString openPath = filename;
    if (EnumModDirectories(FindMissionPboCallback, &ctx))
    {
        openPath = ctx.result;
    }

    // create bank
    int index = GFileBanks.Add();
    QFBank& bank = GFileBanks[index];
    bank.open(openPath);
    RString str = RString(prefix.c_str()) + island + RString("/");
    str.Lower();
    bank.SetPrefix(str);
    return str;
}

NetworkMessageType NetworkSimpleObject::GetNMType(NetworkMessageClass cls) const
{
    return NMTNone;
}

IndicesNetworkObject::IndicesNetworkObject()
{
    objectCreator = -1;
    objectId = -1;
    objectPosition = -1;
    guaranteed = -1;
}

void IndicesNetworkObject::Scan(NetworkMessageFormatBase* format){SCAN(objectCreator) SCAN(objectId)
                                                                      SCAN(objectPosition) SCAN(guaranteed)}

// Create network message indices for NetworkObject class
NetworkMessageIndices* GetIndicesNetworkObject()
{
    return new IndicesNetworkObject();
}

NetworkObject::NetworkObject()
{
    _lastUpdateTime = TIME_MIN;
    _maxPredictionTime = TIME_MIN;
}

float NetworkObject::GetMaxPredictionTime(NetworkMessageContext& ctx) const
{
    // default: if we do not receive any update for 10 seconds
    // assume object in not doing anything (otherwise we would get an update)
    return 10;
}

bool NetworkObject::CheckPredictionFrozen() const
{
    if (!IsLocal())
    {
        return Glob.time > _maxPredictionTime;
    }
    return GetNetworkManager().IsControlsPaused();
}

NetworkMessageFormat& NetworkObject::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("objectCreator", NDTInteger, NCTNone, DEFVALUE(int, 0),
               DOC_MSG("ID of client, which created this object"));
    format.Add("objectId", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Unique (for client) ID of object"));
    format.Add("objectPosition", NDTVector, NCTNone, DEFVALUE(Vector3, VZero), DOC_MSG("Current position of object"));
    format.Add("guaranteed", NDTBool, NCTNone, DEFVALUE(bool, false),
               DOC_MSG("Message is guaranteed (must be delivered)"));
    return format;
}

TMError NetworkObject::TransferMsg(NetworkMessageContext& ctx)
{
    if (ctx.IsSending())
    {
        // TESTING - Replace by PoseidonAssert
        NET_ERROR(dynamic_cast<const IndicesNetworkObject*>(ctx.GetIndices()))
        const IndicesNetworkObject* indices = static_cast<const IndicesNetworkObject*>(ctx.GetIndices());

        NetworkId id = GetNetworkId();
        TMCHECK(ctx.IdxTransfer(indices->objectCreator, id.creator))
        TMCHECK(ctx.IdxTransfer(indices->objectId, id.id))
        Vector3 position = GetCurrentPosition();
        TMCHECK(ctx.IdxTransfer(indices->objectPosition, position))
    }
    else
    {
        if (ctx.GetClass() == NMCUpdatePosition)
        {
            _lastUpdateTime = Glob.time;
            _maxPredictionTime = _lastUpdateTime + GetMaxPredictionTime(ctx);
        }
    }
    return TMOK;
}

float NetworkObject::CalculateError(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCUpdatePosition:
            return ERR_COEF_TIME_POSITION * (Glob.time - ctx.GetMsgTime());
        default:
            return ERR_COEF_TIME_GENERIC * (Glob.time - ctx.GetMsgTime());
    }
}

float NetworkObject::CalculateErrorCoef(Vector3Par position, Vector3Par cameraPosition)
{
    // undefined camera position
    if (cameraPosition[0] == -FLT_MAX)
    {
        return 1.0;
    }

    // coef = limit / distance (maximum is 1)
    const float limit = 20.0f;
    float dist2 = position.Distance2(cameraPosition);
    if (dist2 <= Square(limit))
    {
        return 1.0;
    }
    else
    {
        return Square(limit) / dist2;
    }
}

NetworkMessageType ClientInfoObject::GetNMType(NetworkMessageClass cls) const
{
    switch (cls)
    {
        case NMCUpdateGeneric:
            return NMTUpdateClientInfo;
        default:
            return NMTNone;
    }
}

// network message indices for ClientInfoObject class
class IndicesUpdateClientInfo : public IndicesNetworkObject
{
    typedef IndicesNetworkObject base;

  public:
    // index of field in message format
    int cameraPosition;

    IndicesUpdateClientInfo();
    NetworkMessageIndices* Clone() const override { return new IndicesUpdateClientInfo; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesUpdateClientInfo::IndicesUpdateClientInfo()
{
    cameraPosition = -1;
}

void IndicesUpdateClientInfo::Scan(NetworkMessageFormatBase* format)
{
    base::Scan(format);

    SCAN(cameraPosition)
}

// Create network message indices for ClientInfoObject class
NetworkMessageIndices* GetIndicesUpdateClientInfo()
{
    return new IndicesUpdateClientInfo();
}

NetworkMessageFormat& ClientInfoObject::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    switch (cls)
    {
        case NMCUpdateGeneric:
            NetworkObject::CreateFormat(cls, format);
            format.Add("cameraPosition", NDTVector, NCTNone, DEFVALUE(Vector3, InvalidCamPos),
                       DOC_MSG("Position of camera on client"));
            break;
        default:
            NetworkObject::CreateFormat(cls, format);
            break;
    }
    return format;
}

TMError ClientInfoObject::TransferMsg(NetworkMessageContext& ctx)
{
    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
            TMCHECK(NetworkObject::TransferMsg(ctx))
            {
                NET_ERROR(dynamic_cast<const IndicesUpdateClientInfo*>(ctx.GetIndices()))
                const IndicesUpdateClientInfo* indices = static_cast<const IndicesUpdateClientInfo*>(ctx.GetIndices());

                if (ctx.IsSending())
                {
                    const Camera* camera = GScene->GetCamera();
                    if (camera)
                    {
                        _cameraPosition = camera->Position();
                    }
                    else
                    {
                        _cameraPosition = InvalidCamPos;
                    }
                }
                ITRANSF(cameraPosition)
            }
            break;
        default:
            TMCHECK(NetworkObject::TransferMsg(ctx))
            break;
    }
    return TMOK;
}

float ClientInfoObject::CalculateError(NetworkMessageContext& ctx)
{
    float error = NetworkObject::CalculateError(ctx);

    switch (ctx.GetClass())
    {
        case NMCUpdateGeneric:
        {
            NET_ERROR(dynamic_cast<const IndicesUpdateClientInfo*>(ctx.GetIndices()))
            const IndicesUpdateClientInfo* indices = static_cast<const IndicesUpdateClientInfo*>(ctx.GetIndices());

            Vector3 pos;
            if (ctx.IdxTransfer(indices->cameraPosition, pos) == TMOK)
            {
                const Camera* camera = GScene->GetCamera();
                if (camera)
                {
                    error += 1.0 * pos.Distance2(camera->Position());
                }
            }
        }
        default:
            break;
    }
    return error;
}

RString ClientInfoObject::GetDebugName() const
{
    return RString("ClientInfoObject");
}

// static (nonregistered) messages

// network message indices for PlayerMessage class
class IndicesPlayer : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int player;
    int name;
    int server;

    IndicesPlayer();
    NetworkMessageIndices* Clone() const override { return new IndicesPlayer; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesPlayer::IndicesPlayer()
{
    player = -1;
    name = -1;
    server = -1;
}

void IndicesPlayer::Scan(NetworkMessageFormatBase* format){SCAN(player) SCAN(name) SCAN(server)}

// Create network message indices for PlayerMessage class
NetworkMessageIndices* GetIndicesPlayer()
{
    return new IndicesPlayer();
}

NetworkMessageFormat& PlayerMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("player", NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("Unique ID of client (player)"));
    format.Add("name", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Player's name"));
    format.Add("server", NDTBool, NCTNone, DEFVALUE(bool, false),
               DOC_MSG("Bot client (running in the same process as server)"));
    return format;
}

TMError PlayerMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesPlayer*>(ctx.GetIndices()))
    const IndicesPlayer* indices = static_cast<const IndicesPlayer*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransfer(indices->player, player))
    TMCHECK(ctx.IdxTransfer(indices->name, name))
    TMCHECK(ctx.IdxTransfer(indices->server, server))
    return TMOK;
}

// network message indices for NetworkMessageQueue class
class IndicesMessages : public NetworkMessageIndices
{
  public:
    IndicesMessages();
    NetworkMessageIndices* Clone() const override { return new IndicesMessages; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesMessages::IndicesMessages() = default;

void IndicesMessages::Scan(NetworkMessageFormatBase* format) {}

// Create network message indices for NetworkMessageQueue class
NetworkMessageIndices* GetIndicesMessages()
{
    return new IndicesMessages();
}

NetworkMessageFormat& NetworkMessageQueue::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    return format;
}

TMError NetworkMessageQueue::TransferMsg(NetworkMessageContext& ctx)
{
    return TMOK;
}

// variant (registered) messages

// network message indices for ChangeGameState class
class IndicesGameState : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int gameState;

    IndicesGameState();
    NetworkMessageIndices* Clone() const override { return new IndicesGameState; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesGameState::IndicesGameState()
{
    gameState = -1;
}

void IndicesGameState::Scan(NetworkMessageFormatBase* format){SCAN(gameState)}

// Create network message indices for ChangeGameState class
NetworkMessageIndices* GetIndicesGameState()
{
    return new IndicesGameState();
}

NetworkMessageFormat& ChangeGameState::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("gameState", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Current multiplayer game state"));
    return format;
}

TMError ChangeGameState::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesGameState*>(ctx.GetIndices()))
    const IndicesGameState* indices = static_cast<const IndicesGameState*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransfer(indices->gameState, (int&)gameState))
    return TMOK;
}

// network message indices for LogoutMessage class
class IndicesLogout : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int dpnid;

    IndicesLogout();
    NetworkMessageIndices* Clone() const override { return new IndicesLogout; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesLogout::IndicesLogout()
{
    dpnid = -1;
}

void IndicesLogout::Scan(NetworkMessageFormatBase* format){SCAN(dpnid)}

// Create network message indices for LogoutMessage class
NetworkMessageIndices* GetIndicesLogout()
{
    return new IndicesLogout();
}

NetworkMessageFormat& LogoutMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("dpnid", NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("ID of client (player)"));
    return format;
}

TMError LogoutMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesLogout*>(ctx.GetIndices()))
    const IndicesLogout* indices = static_cast<const IndicesLogout*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransfer(indices->dpnid, (int&)dpnid))
    return TMOK;
}

// network message indices for MissionHeader class
class IndicesMissionHeader : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int island;
    int name;
    int description;
    int fileName;
    int fileDir;
    int fileSizeL;
    int fileSizeH;
    int fileCRC;
    int respawn;
    int respawnDelay;
    int cadetMode;
    int disabledAI;
    int aiKills;
    int updateOnly;
    int joinInProgress;
    int difficulty[DTN];
    int addOns;
    int estimatedEndTime;

    int titleParam1;
    int valuesParam1;
    int textsParam1;
    int defValueParam1;
    int titleParam2;
    int valuesParam2;
    int textsParam2;
    int defValueParam2;

    IndicesMissionHeader();
    NetworkMessageIndices* Clone() const override { return new IndicesMissionHeader; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesMissionHeader::IndicesMissionHeader()
{
    island = -1;
    name = -1;
    description = -1;
    fileName = -1;
    fileDir = -1;
    fileSizeL = -1;
    fileSizeH = -1;
    fileCRC = -1;
    respawn = -1;
    respawnDelay = -1;
    cadetMode = -1;
    disabledAI = -1;
    aiKills = -1;
    updateOnly = -1;
    joinInProgress = -1;
    for (int i = 0; i < DTN; i++)
    {
        difficulty[i] = -1;
    }
    addOns = -1;
    estimatedEndTime = -1;

    titleParam1 = -1;
    valuesParam1 = -1;
    textsParam1 = -1;
    defValueParam1 = -1;
    titleParam2 = -1;
    valuesParam2 = -1;
    textsParam2 = -1;
    defValueParam2 = -1;
}

void IndicesMissionHeader::Scan(NetworkMessageFormatBase* format)
{
    SCAN(island)
    SCAN(name)
    SCAN(description)
    SCAN(fileName)
    SCAN(fileDir)
    SCAN(fileSizeL)
    SCAN(fileSizeH)
    SCAN(fileCRC)
    SCAN(respawn)
    SCAN(respawnDelay)
    SCAN(cadetMode)
    SCAN(disabledAI)
    SCAN(aiKills)
    SCAN(updateOnly)
    SCAN(joinInProgress)
    for (int i = 0; i < DTN; i++)
    {
        RString name = RString("diff") + RString(Config::diffDesc[i].name);
        difficulty[i] = format->FindIndex(name);
    }
    SCAN(addOns)

    SCAN(estimatedEndTime)

    SCAN(titleParam1)
    SCAN(valuesParam1)
    SCAN(textsParam1)
    SCAN(defValueParam1)
    SCAN(titleParam2)
    SCAN(valuesParam2)
    SCAN(textsParam2)
    SCAN(defValueParam2)
}

// Create network message indices for MissionHeader class
NetworkMessageIndices* GetIndicesMissionHeader()
{
    return new IndicesMissionHeader();
}

MissionHeader::MissionHeader()
{
    fileSizeL = 0;
    fileSizeH = 0;
    fileCRC = 0;
    cadetMode = false;
    disabledAI = false;
    aiKills = false;
    updateOnly = false;
    joinInProgress = false;
    respawn = RespawnNone;
    respawnDelay = 0.0f;
    start = 0;
    defValueParam1 = 0;
    defValueParam2 = 0;

    difficulty.Resize(DTN);
    difficulty.Compact();

    estimatedEndTime = TIME_MIN;
}

NetworkMessageFormat& MissionHeader::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("island", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Island (map), where mission is placed"));
    format.Add("name", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Name of mission"));
    format.Add("description", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Description of mission"));

    format.Add("fileName", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Name of mission file"));
    format.Add("fileDir", NDTString, NCTNone, DEFVALUE(RString, ""),
               DOC_MSG("Directory, where mission file is placed"));
    format.Add("fileSizeL", NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("Size of mission file (low DWORD)"));
    format.Add("fileSizeH", NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("Size of mission file (high DWORD)"));
    format.Add("fileCRC", NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("CRC of mission file"));

    format.Add("respawn", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, RespawnSeaGull), DOC_MSG("Respawn type"));
    format.Add("respawnDelay", NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Respawn delay (in seconds)"));

    format.Add("cadetMode", NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Cadet / Veteran mode"));
    format.Add("disabledAI", NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("AI is disabled"));
    format.Add("aiKills", NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Write AI kills into statistics"));

    format.Add("updateOnly", NDTBool, NCTNone, DEFVALUE(bool, false),
               DOC_MSG("This message is only update of (recently sent) mission info"));

    format.Add("joinInProgress", NDTBool, NCTNone, DEFVALUE(bool, false),
               DOC_MSG("Allow players to join after mission has started"));

    for (int i = 0; i < DTN; i++)
    {
        RString name = RString("diff") + RString(Config::diffDesc[i].name);
        format.Add(name, NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Difficulty settings"));
    }

    format.Add("addOns", NDTStringArray, NCTNone, DEFVALUESTRINGARRAY, DOC_MSG("List of used addons"));

    format.Add("estimatedEndTime", NDTTime, NCTNone, DEFVALUE(Time, TIME_MIN),
               DOC_MSG("Time of estimated end of mission"));

    format.Add("titleParam1", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Mission parameter - title"));
    format.Add("valuesParam1", NDTFloatArray, NCTNone, DEFVALUEFLOATARRAY,
               DOC_MSG("Mission parameter - list of values"));
    format.Add("textsParam1", NDTStringArray, NCTNone, DEFVALUESTRINGARRAY,
               DOC_MSG("Mission parameter - list of value names"));
    format.Add("defValueParam1", NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Mission parameter - default value"));
    format.Add("titleParam2", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Mission parameter - title"));
    format.Add("valuesParam2", NDTFloatArray, NCTNone, DEFVALUEFLOATARRAY,
               DOC_MSG("Mission parameter - list of values"));
    format.Add("textsParam2", NDTStringArray, NCTNone, DEFVALUESTRINGARRAY,
               DOC_MSG("Mission parameter - list of value names"));
    format.Add("defValueParam2", NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Mission parameter - default value"));

    return format;
}

TMError MissionHeader::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesMissionHeader*>(ctx.GetIndices()))
    const IndicesMissionHeader* indices = static_cast<const IndicesMissionHeader*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransfer(indices->island, island))
    TMCHECK(ctx.IdxTransfer(indices->name, name))
    TMCHECK(ctx.IdxTransfer(indices->description, description))

    TMCHECK(ctx.IdxTransfer(indices->fileName, fileName))
    TMCHECK(ctx.IdxTransfer(indices->fileDir, fileDir))
    TMCHECK(ctx.IdxTransfer(indices->fileSizeL, fileSizeL))
    TMCHECK(ctx.IdxTransfer(indices->fileSizeH, fileSizeH))
    TMCHECK(ctx.IdxTransfer(indices->fileCRC, fileCRC))

    TMCHECK(ctx.IdxTransfer(indices->respawn, (int&)respawn))
    TMCHECK(ctx.IdxTransfer(indices->respawnDelay, respawnDelay))

    TMCHECK(ctx.IdxTransfer(indices->cadetMode, cadetMode))
    TMCHECK(ctx.IdxTransfer(indices->disabledAI, disabledAI))
    TMCHECK(ctx.IdxTransfer(indices->aiKills, aiKills))
    TMCHECK(ctx.IdxTransfer(indices->updateOnly, updateOnly))
    TMCHECK(ctx.IdxTransfer(indices->joinInProgress, joinInProgress))
    for (int i = 0; i < DTN; i++)
    {
        TMCHECK(ctx.IdxTransfer(indices->difficulty[i], difficulty[i]))
    }

    TMCHECK(ctx.IdxTransfer(indices->addOns, addOns))

    TMCHECK(ctx.IdxTransfer(indices->estimatedEndTime, estimatedEndTime))

    TMCHECK(ctx.IdxTransfer(indices->titleParam1, titleParam1))
    TMCHECK(ctx.IdxTransfer(indices->valuesParam1, valuesParam1))
    TMCHECK(ctx.IdxTransfer(indices->textsParam1, textsParam1))
    TMCHECK(ctx.IdxTransfer(indices->defValueParam1, defValueParam1))
    TMCHECK(ctx.IdxTransfer(indices->titleParam2, titleParam2))
    TMCHECK(ctx.IdxTransfer(indices->valuesParam2, valuesParam2))
    TMCHECK(ctx.IdxTransfer(indices->textsParam2, textsParam2))
    TMCHECK(ctx.IdxTransfer(indices->defValueParam2, defValueParam2))

    return TMOK;
}

IndicesPlayerRole::IndicesPlayerRole()
{
    index = -1;
    side = -1;
    group = -1;
    unit = -1;
    vehicle = -1;
    position = -1;
    leader = -1;
    roleLocked = -1;
    player = -1;
}

void IndicesPlayerRole::Scan(NetworkMessageFormatBase* format){SCAN(index) SCAN(side) SCAN(group) SCAN(unit)
                                                                   SCAN(vehicle) SCAN(position) SCAN(leader)
                                                                       SCAN(roleLocked) SCAN(player)}

// Create network message indices for PlayerRole class
NetworkMessageIndices* GetIndicesPlayerRole()
{
    return new IndicesPlayerRole();
}
NetworkMessageIndices* GetIndicesPlayerSide()
{
    return new IndicesPlayerRole();
}

NetworkMessageFormat& PlayerRole::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("index", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Index of role"));
    format.Add("side", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Side of role"));
    format.Add("group", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Group ID of role"));
    format.Add("unit", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Unit ID of role"));
    format.Add("vehicle", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Vehicle used by this role"));
    format.Add("position", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0),
               DOC_MSG("Position in vehicle (driver, commander, ...)"));
    format.Add("leader", NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Is this unit group leader"));
    format.Add("roleLocked", NDTBool, NCTNone, DEFVALUE(bool, false),
               DOC_MSG("Role is locked (only admin can assign this role)"));
    format.Add("player", NDTInteger, NCTNone, DEFVALUE(int, AI_PLAYER), DOC_MSG("Currently attached player"));
    return format;
}

TMError PlayerRole::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesPlayerRole*>(ctx.GetIndices()))
    const IndicesPlayerRole* indices = static_cast<const IndicesPlayerRole*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransfer(indices->side, (int&)side))
    TMCHECK(ctx.IdxTransfer(indices->group, group))
    TMCHECK(ctx.IdxTransfer(indices->unit, unit))
    TMCHECK(ctx.IdxTransfer(indices->vehicle, vehicle))
    TMCHECK(ctx.IdxTransfer(indices->position, (int&)position))
    TMCHECK(ctx.IdxTransfer(indices->leader, leader))
    TMCHECK(ctx.IdxTransfer(indices->roleLocked, roleLocked))
    TMCHECK(ctx.IdxTransfer(indices->player, player))
    return TMOK;
}

DEFINE_NET_MESSAGE(PublicVariable, PUB_VAR_MSG)

DEFINE_NET_MESSAGE(GroupSynchronization, GROUP_SYNC_MSG)

DEFINE_NET_MESSAGE(DetectorActivation, DET_ACT_MSG)

DEFINE_NET_MESSAGE(AskForCreateUnit, CREATE_UNIT_MSG)

DEFINE_NET_MESSAGE(AskForDeleteVehicle, DELETE_VEHICLE_MSG)

DEFINE_NET_MESSAGE(AskForReceiveUnitAnswer, UNIT_ANSWER_MSG)

DEFINE_NET_MESSAGE(AskForGroupRespawn, GROUP_RESPAWN_MSG)

DEFINE_NET_MESSAGE(CopyUnitInfo, COPY_UNIT_INFO_MSG)

DEFINE_NET_MESSAGE(GroupRespawnDone, GROUP_RESPAWN_DONE_MSG)

DEFINE_NET_MESSAGE(MissionParams, MISSION_PARAMS_MSG)

DEFINE_NET_MESSAGE(AskForActivateMine, ACTIVATE_MINE_MSG)

DEFINE_NET_MESSAGE(VehicleDamaged, VEHICLE_DAMAGED_MSG)

DEFINE_NET_MESSAGE(AskForInflameFire, INFLAME_FIRE_MSG)

DEFINE_NET_MESSAGE(AskForAnimationPhase, ANIMATION_PHASE_MSG)

DEFINE_NET_MESSAGE(IncomingMissile, INCOMING_MISSILE_MSG)

DEFINE_NET_MESSAGE(PublicExec, PUBLICEXEC_MSG)

DEFINE_NET_MESSAGE(RemoteExec, REMOTEEXEC_MSG)

IndicesChat::IndicesChat()
{
    channel = -1;
    sender = -1;
    units = -1;
    name = -1;
    text = -1;
}

void IndicesChat::Scan(NetworkMessageFormatBase* format){SCAN(channel) SCAN(sender) SCAN(units) SCAN(name) SCAN(text)}

// Create network message indices for ChatMessage class
NetworkMessageIndices* GetIndicesChat()
{
    return new IndicesChat();
}

NetworkMessageFormat& ChatMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("channel", NDTInteger, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Radio channel"));
    format.Add("sender", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Sender unit"));
    format.Add("units", NDTRefArray, NCTNone, DEFVALUEREFARRAY, DOC_MSG("List of receiving units"));
    format.Add("name", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Sender name"));
    format.Add("text", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Message content"));
    return format;
}

TMError ChatMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesChat*>(ctx.GetIndices()))
    const IndicesChat* indices = static_cast<const IndicesChat*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransfer(indices->channel, channel))
    TMCHECK(ctx.IdxTransferRef(indices->sender, sender))
    TMCHECK(ctx.IdxTransferRefs(indices->units, units))
    TMCHECK(ctx.IdxTransfer(indices->name, name))
    TMCHECK(ctx.IdxTransfer(indices->text, text))
    return TMOK;
}

IndicesRadioChat::IndicesRadioChat()
{
    channel = -1;
    sender = -1;
    units = -1;
    text = -1;
    sentence = -1;
}

void IndicesRadioChat::Scan(NetworkMessageFormatBase* format){SCAN(channel) SCAN(sender) SCAN(units) SCAN(text)
                                                                  SCAN(sentence)}

// Create network message indices for RadioChatMessage class
NetworkMessageIndices* GetIndicesRadioChat()
{
    return new IndicesRadioChat();
}

NetworkMessageFormat& RadioChatMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("channel", NDTInteger, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Radio channel"));
    format.Add("sender", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Sender unit"));
    format.Add("units", NDTRefArray, NCTNone, DEFVALUEREFARRAY, DOC_MSG("List of receiving units"));
    format.Add("text", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Content of message (text)"));
    format.Add("sentence", NDTSentence, NCTNone, DEFVALUE(RadioSentence, RadioSentence()),
               DOC_MSG("Content of message (list of words to say)"));
    return format;
}

TMError RadioChatMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesRadioChat*>(ctx.GetIndices()))
    const IndicesRadioChat* indices = static_cast<const IndicesRadioChat*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransfer(indices->channel, channel))
    TMCHECK(ctx.IdxTransferRef(indices->sender, sender))
    TMCHECK(ctx.IdxTransferRefs(indices->units, units))
    TMCHECK(ctx.IdxTransfer(indices->text, text))
    TMCHECK(ctx.IdxTransfer(indices->sentence, sentence))
    return TMOK;
}

IndicesRadioChatWave::IndicesRadioChatWave()
{
    channel = -1;
    units = -1;
    wave = -1;
    sender = -1;
    senderName = -1;
}

void IndicesRadioChatWave::Scan(NetworkMessageFormatBase* format){SCAN(channel) SCAN(units) SCAN(wave) SCAN(sender)
                                                                      SCAN(senderName)}

// Create network message indices for RadioChatWaveMessage class
NetworkMessageIndices* GetIndicesRadioChatWave()
{
    return new IndicesRadioChatWave();
}

NetworkMessageFormat& RadioChatWaveMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("channel", NDTInteger, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Radio channel"));
    format.Add("units", NDTRefArray, NCTNone, DEFVALUEREFARRAY, DOC_MSG("List of receiving units"));
    format.Add("wave", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Sound identifier"));
    format.Add("sender", NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Sender unit"));
    format.Add("senderName", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Sender name"));
    return format;
}

TMError RadioChatWaveMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesRadioChatWave*>(ctx.GetIndices()))
    const IndicesRadioChatWave* indices = static_cast<const IndicesRadioChatWave*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransfer(indices->channel, channel))
    TMCHECK(ctx.IdxTransferRefs(indices->units, units))
    TMCHECK(ctx.IdxTransfer(indices->wave, wave))
    TMCHECK(ctx.IdxTransferRef(indices->sender, sender))
    TMCHECK(ctx.IdxTransfer(indices->senderName, senderName))
    return TMOK;
}

IndicesSetVoiceChannel::IndicesSetVoiceChannel()
{
    channel = -1;
    units = -1;
}

void IndicesSetVoiceChannel::Scan(NetworkMessageFormatBase* format){SCAN(channel) SCAN(units)}

// Create network message indices for SetVoiceChannelMessage class
NetworkMessageIndices* GetIndicesSetVoiceChannel()
{
    return new IndicesSetVoiceChannel();
}

NetworkMessageFormat& SetVoiceChannelMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("channel", NDTInteger, NCTSmallSigned, DEFVALUE(int, 0), DOC_MSG("Radio channel"));
    format.Add("units", NDTRefArray, NCTNone, DEFVALUEREFARRAY, DOC_MSG("List of receiving units"));
    return format;
}

TMError SetVoiceChannelMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesSetVoiceChannel*>(ctx.GetIndices()))
    const IndicesSetVoiceChannel* indices = static_cast<const IndicesSetVoiceChannel*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransfer(indices->channel, channel))
    TMCHECK(ctx.IdxTransferRefs(indices->units, units))
    return TMOK;
}

// network message indices for SetSpeakerMessage class
class IndicesSetSpeaker : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int player;
    int on;
    int creator;
    int id;

    IndicesSetSpeaker();
    NetworkMessageIndices* Clone() const override { return new IndicesSetSpeaker; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesSetSpeaker::IndicesSetSpeaker()
{
    player = -1;
    on = -1;
    creator = -1;
    id = -1;
}

void IndicesSetSpeaker::Scan(NetworkMessageFormatBase* format){SCAN(player) SCAN(on) SCAN(creator) SCAN(id)}

// Create network message indices for SetSpeakerMessage class
NetworkMessageIndices* GetIndicesSetSpeaker()
{
    return new IndicesSetSpeaker();
}

NetworkMessageFormat& SetSpeakerMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("player", NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("Client (player) ID of speaking player"));
    format.Add("on", NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Turn on / off direct speaking"));
    format.Add("creator", NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("ID of speaking unit"));
    format.Add("id", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("ID of speaking unit"));
    return format;
}

TMError SetSpeakerMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesSetSpeaker*>(ctx.GetIndices()))
    const IndicesSetSpeaker* indices = static_cast<const IndicesSetSpeaker*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransfer(indices->player, player))
    TMCHECK(ctx.IdxTransfer(indices->on, on))
    TMCHECK(ctx.IdxTransfer(indices->creator, object.creator))
    TMCHECK(ctx.IdxTransfer(indices->id, object.id))
    return TMOK;
}

// network message indices for SelectPlayerMessage class
class IndicesSelectPlayer : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int player;
    int creator;
    int id;
    int position;
    int respawn;

    IndicesSelectPlayer();
    NetworkMessageIndices* Clone() const override { return new IndicesSelectPlayer; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesSelectPlayer::IndicesSelectPlayer()
{
    player = -1;
    creator = -1;
    id = -1;
    position = -1;
    respawn = -1;
}

void IndicesSelectPlayer::Scan(NetworkMessageFormatBase* format){SCAN(player) SCAN(creator) SCAN(id) SCAN(position)
                                                                     SCAN(respawn)}

// Create network message indices for SelectPlayerMessage class
NetworkMessageIndices* GetIndicesSelectPlayer()
{
    return new IndicesSelectPlayer();
}

NetworkMessageFormat& SelectPlayerMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("player", NDTInteger, NCTNone, DEFVALUE(int, AI_PLAYER), DOC_MSG("Client (player) ID of player"));
    format.Add("creator", NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("ID of player's unit"));
    format.Add("id", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("ID of player's unit"));
    format.Add("position", NDTVector, NCTNone, DEFVALUE(Vector3, Vector3(-FLT_MAX, -FLT_MAX, -FLT_MAX)),
               DOC_MSG("Player's unit position"));
    format.Add("respawn", NDTBool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Selection of player's unit after respawn"));
    return format;
}

TMError SelectPlayerMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesSelectPlayer*>(ctx.GetIndices()))
    const IndicesSelectPlayer* indices = static_cast<const IndicesSelectPlayer*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransfer(indices->player, player))
    TMCHECK(ctx.IdxTransfer(indices->creator, person.creator))
    TMCHECK(ctx.IdxTransfer(indices->id, person.id))
    TMCHECK(ctx.IdxTransfer(indices->position, position))
    TMCHECK(ctx.IdxTransfer(indices->respawn, respawn))
    return TMOK;
}

// network message indices for ChangeOwnerMessage class
class IndicesChangeOwner : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int creator;
    int id;
    int owner;

    IndicesChangeOwner();
    NetworkMessageIndices* Clone() const override { return new IndicesChangeOwner; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesChangeOwner::IndicesChangeOwner()
{
    creator = -1;
    id = -1;
    owner = -1;
}

void IndicesChangeOwner::Scan(NetworkMessageFormatBase* format){SCAN(creator) SCAN(id) SCAN(owner)}

// Create network message indices for ChangeOwnerMessage class
NetworkMessageIndices* GetIndicesChangeOwner()
{
    return new IndicesChangeOwner();
}

NetworkMessageFormat& ChangeOwnerMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("creator", NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("ID of object, which owner is changing"));
    format.Add("id", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("ID of object, whic player is changing"));
    format.Add("owner", NDTInteger, NCTNone, DEFVALUE(int, AI_PLAYER), DOC_MSG("Client ID of new owner"));
    return format;
}

TMError ChangeOwnerMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesChangeOwner*>(ctx.GetIndices()))
    const IndicesChangeOwner* indices = static_cast<const IndicesChangeOwner*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransfer(indices->creator, object.creator))
    TMCHECK(ctx.IdxTransfer(indices->id, object.id))
    TMCHECK(ctx.IdxTransfer(indices->owner, owner))
    return TMOK;
}

// network message indices for PlaySoundMessage class
class IndicesPlaySound : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int name;
    int position;
    int speed;
    int volume;
    int frequency;
    int creator;
    int soundId;

    IndicesPlaySound();
    NetworkMessageIndices* Clone() const override { return new IndicesPlaySound; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesPlaySound::IndicesPlaySound()
{
    name = -1;
    position = -1;
    speed = -1;
    volume = -1;
    frequency = -1;
    creator = -1;
    soundId = -1;
}

void IndicesPlaySound::Scan(NetworkMessageFormatBase* format){SCAN(name) SCAN(position) SCAN(speed) SCAN(volume)
                                                                  SCAN(frequency) SCAN(creator) SCAN(soundId)}

// Create network message indices for PlaySoundMessage class
NetworkMessageIndices* GetIndicesPlaySound()
{
    return new IndicesPlaySound();
}

NetworkMessageFormat& PlaySoundMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    Vector3 temp = VZero;
    format.Add("name", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Sound identifier"));
    format.Add("position", NDTVector, NCTNone, DEFVALUE(Vector3, temp), DOC_MSG("Sound source position"));
    format.Add("speed", NDTVector, NCTNone, DEFVALUE(Vector3, temp), DOC_MSG("Sound source speed"));
    format.Add("volume", NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Sound volume"));
    format.Add("frequency", NDTFloat, NCTNone, DEFVALUE(float, 0), DOC_MSG("Sound pitch"));
    format.Add("creator", NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("Unique network ID of sound"));
    format.Add("soundId", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Unique network ID of sound"));
    return format;
}

TMError PlaySoundMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesPlaySound*>(ctx.GetIndices()))
    const IndicesPlaySound* indices = static_cast<const IndicesPlaySound*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransfer(indices->name, name))
    TMCHECK(ctx.IdxTransfer(indices->position, position))
    TMCHECK(ctx.IdxTransfer(indices->speed, speed))
    TMCHECK(ctx.IdxTransfer(indices->volume, volume))
    TMCHECK(ctx.IdxTransfer(indices->frequency, freq))
    TMCHECK(ctx.IdxTransfer(indices->creator, creator))
    TMCHECK(ctx.IdxTransfer(indices->soundId, soundId))
    return TMOK;
}

// network message indices for SoundStateMessage class
class IndicesSoundState : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int state;
    int creator;
    int soundId;

    IndicesSoundState();
    NetworkMessageIndices* Clone() const override { return new IndicesSoundState; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesSoundState::IndicesSoundState()
{
    state = -1;
    creator = -1;
    soundId = -1;
}

void IndicesSoundState::Scan(NetworkMessageFormatBase* format){SCAN(state) SCAN(creator) SCAN(soundId)}

// Create network message indices for SoundStateMessage class
NetworkMessageIndices* GetIndicesSoundState()
{
    return new IndicesSoundState();
}

NetworkMessageFormat& SoundStateMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("state", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("New state of sound"));
    format.Add("creator", NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("Network ID of sound"));
    format.Add("soundId", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Network ID of sound"));
    return format;
}

TMError SoundStateMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesSoundState*>(ctx.GetIndices()))
    const IndicesSoundState* indices = static_cast<const IndicesSoundState*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransfer(indices->state, (int&)state))
    TMCHECK(ctx.IdxTransfer(indices->creator, creator))
    TMCHECK(ctx.IdxTransfer(indices->soundId, soundId))
    return TMOK;
}

// network message indices for TransferFileMessage, TransferMissionFileMessage and TransferFileToServerMessage classes
class IndicesTransferFile : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int path;
    int data;
    int totSize;
    int offset;
    int totSegments;
    int curSegment;

    IndicesTransferFile();
    NetworkMessageIndices* Clone() const override { return new IndicesTransferFile; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesTransferFile::IndicesTransferFile()
{
    path = -1;
    data = -1;
    totSize = -1;
    offset = -1;
    totSegments = -1;
    curSegment = -1;
}

void IndicesTransferFile::Scan(NetworkMessageFormatBase* format){SCAN(path) SCAN(data) SCAN(totSize) SCAN(offset)
                                                                     SCAN(totSegments) SCAN(curSegment)}

// Create network message indices for TransferFileMessage class
NetworkMessageIndices* GetIndicesTransferFile()
{
    return new IndicesTransferFile();
}
// Create network message indices for TransferMissionFileMessage class
NetworkMessageIndices* GetIndicesTransferMissionFile()
{
    return new IndicesTransferFile();
}
// Create network message indices for TransferFileToServerMessage class
NetworkMessageIndices* GetIndicesTransferFileToServer()
{
    return new IndicesTransferFile();
}

NetworkMessageFormat& TransferFileMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("path", NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Path of transferred file"));
    format.Add("data", NDTRawData, NCTNone, DEFVALUERAWDATA, DOC_MSG("Content of file (single segment)"));
    format.Add("totSize", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Total size of file"));
    format.Add("offset", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Offset of this segment"));
    format.Add("totSegments", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 1), DOC_MSG("Total number of segments"));
    format.Add("curSegment", NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Index of this segment"));
    return format;
}

TMError TransferFileMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesTransferFile*>(ctx.GetIndices()))
    const IndicesTransferFile* indices = static_cast<const IndicesTransferFile*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransfer(indices->path, path))
    TMCHECK(ctx.IdxTransfer(indices->data, data))
    TMCHECK(ctx.IdxTransfer(indices->totSize, totSize))
    TMCHECK(ctx.IdxTransfer(indices->offset, offset))
    TMCHECK(ctx.IdxTransfer(indices->totSegments, totSegments))
    TMCHECK(ctx.IdxTransfer(indices->curSegment, curSegment))
    return TMOK;
}

// network message indices for AskMissionFileMessage class
class IndicesAskMissionFile : public NetworkMessageIndices
{
  public:
    // index of field in message format
    int valid;

    IndicesAskMissionFile();
    NetworkMessageIndices* Clone() const override { return new IndicesAskMissionFile; }
    void Scan(NetworkMessageFormatBase* format) override;
};

IndicesAskMissionFile::IndicesAskMissionFile()
{
    valid = -1;
}

void IndicesAskMissionFile::Scan(NetworkMessageFormatBase* format){SCAN(valid)}

// Create network message indices for AskMissionFileMessage class
NetworkMessageIndices* GetIndicesAskMissionFile()
{
    return new IndicesAskMissionFile();
}

NetworkMessageFormat& AskMissionFileMessage::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat& format)
{
    format.Add("valid", NDTBool, NCTNone, DEFVALUE(bool, false),
               DOC_MSG("Mission file is valid (present on client computer)"));
    return format;
}

TMError AskMissionFileMessage::TransferMsg(NetworkMessageContext& ctx)
{
    NET_ERROR(dynamic_cast<const IndicesAskMissionFile*>(ctx.GetIndices()))
    const IndicesAskMissionFile* indices = static_cast<const IndicesAskMissionFile*>(ctx.GetIndices());

    TMCHECK(ctx.IdxTransfer(indices->valid, valid))
    return TMOK;
}
