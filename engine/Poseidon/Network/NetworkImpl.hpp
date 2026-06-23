#pragma once

#include <Poseidon/Network/NetworkImplComponent.hpp>
#include <Poseidon/Network/NetworkImplClient.hpp>
#include <Poseidon/Network/NetworkImplServer.hpp>

// Network Manager

// Implementation of basic network functions
class NetworkManager : public INetworkManager
{
    friend class NetworkComponent;

  protected:
    SRef<NetTranspSessionEnum> _sessionEnum;
    // time of last call of EnumHosts
    Poseidon::Foundation::UITime _lastEnumHosts;
    // IP address of computer (empty for local network) where search for sessions
    RString _ip;
    // port where search for sessions
    int _port;
    // additional remote hosts
    AutoArray<RemoteHostAddress> _hosts;

    // network server component
    SRef<NetworkServer> _server;
    // network client component
    SRef<NetworkClient> _client;

  public:
    NetworkManager();
    ~NetworkManager();

    bool IsServer() const override { return _server != nullptr; }
    // Return if client is created
    bool IsClient() const { return _client != nullptr; }
    bool HasAdminLoggedIn() const override;
    // Return if local player joined in progress (JIP)
    bool IsJIP() const override;
    // Save world state to file
    bool SaveWorldState(const char* filename) override;
    // Load world state from file
    bool LoadWorldState(const char* filename) override;

    // Access to server component
    NetworkServer* GetServer() { return _server; }
    // Access to client component
    NetworkClient* GetClient() { return _client; }

    bool Init(RString ip, int port, bool startEnum) override;
    void Done() override;

    void GetSessions(AutoArray<SessionInfo>& sessions) override;
    RString IPToGUID(RString ip, int port) override;
    bool CreateSession(int port, RString password) override;
    ConnectResult JoinSession(RString guid, RString password) override;
    bool WaitForSession() override;
    void GetPlayers(AutoArray<NetPlayerInfo, Poseidon::Foundation::MemAllocSA>& players) override;
    void CreateMission(RString mission, RString world) override;
    void InitMission(bool cadetMode) override;
    void Close() override;
    unsigned CleanUpMemory() override;

    void KickOff(int dpnid, KickOffReason reason) override;
    void Ban(int dpnid) override;
    void LockSession(bool lock = true) override;

    void OnSimulate() override;

    NetworkGameState GetGameState() const override;
    NetworkGameState GetServerState() const override;
    NetworkGameState GetClientServerState() const;
    bool WasServerPlaying() const override;
    void SetGameState(NetworkGameState state) override;
    bool IsGameMaster() const override;
    bool IsAdmin() const override;
    ConnectionQuality GetConnectionQuality() const override;
    void GetParams(float& param1, float& param2) const override;
    void SetParams(float param1, float param2) override;
    const MissionHeader* GetMissionHeader() const override;
    int GetPlayer() const override;
    int NPlayerRoles() const override;
    const PlayerRole* GetPlayerRole(int role) const override;
    const PlayerRole* GetMyPlayerRole() const override;
    const PlayerIdentity* FindIdentity(int dpnid) const override;
    const AutoArray<PlayerIdentity>* GetIdentities() const override;
    void AssignPlayer(int role, int player) override;
    void UnassignPlayer(int role) override;
    void SelectPlayer(int player, Person* person, bool respawn = false) override;
    void PlaySound(RString name, Vector3Par position, Vector3Par speed, float volume, float freq, IWave* wave) override;
    void SoundState(IWave* wave, SoundStateType state) override;
    NetworkGameState GetPlayerState(int dpid) override;
    RString GetPlayerName(int dpid) override;
    Vector3 GetCameraPosition(int dpid) override;
    NetworkObject* GetObject(NetworkId& id) override;
    bool CreateVehicle(Vehicle* veh, VehicleListType type, RString name, int idVeh) override;
    bool CreateCenter(AICenter* center) override; // whole AI structure
    bool CreateObject(NetworkObject* object) override;
    void CreateAllObjects() override;
    void DeleteObject(NetworkId& id) override;
    void DestroyAllObjects() override;
    bool CreateCommand(AISubgroup* subgrp, int index, Command* cmd) override;
    void DeleteCommand(AISubgroup* subgrp, int index, Command* cmd) override;
    void ClientReady(NetworkGameState state) override;
    bool PublicExec(RString command) override;
    bool RemoteExec(RString name, const AutoArray<char>& params, int target, const AutoArray<char>& targetSpec,
                    bool jip, RString jipKey, bool callMode) override;
    bool RemoteExecRemove(RString jipKey) override;
    void AskForDammage(Object* who, EntityAI* owner, Vector3Par modelPos, float val, float valRange,
                       RString ammo) override;
    void AskForSetDammage(Object* who, float dammage) override;
    void AskForGetIn(Person* soldier, Transport* vehicle, GetInPosition position) override;
    void AskForGetOut(Person* soldier, Transport* vehicle, bool parachute) override;
    void AskForChangePosition(Person* soldier, Transport* vehicle, UIActionType type) override;
    void AskForAimWeapon(EntityAI* vehicle, int weapon, Vector3Par dir) override;
    void AskForAimObserver(EntityAI* vehicle, Vector3Par dir) override;
    void AskForSelectWeapon(EntityAI* vehicle, int weapon) override;
    void AskForAmmo(EntityAI* vehicle, int weapon, int burst) override;
    void AskForAddImpulse(Vehicle* vehicle, Vector3Par force, Vector3Par torque) override;
    void AskForMove(Object* vehicle, Vector3Par pos) override;
    void AskForMove(Object* vehicle, Matrix4Par trans) override;
    void AskForJoin(AIGroup* join, AIGroup* group) override;
    void AskForJoin(AIGroup* join, OLinkArray<AIUnit>& units) override;
    void AskForHideBody(Person* vehicle) override;
    void ExplosionDammageEffects(EntityAI* owner, Shot* shot, Object* directHit, Vector3Par pos, Vector3Par dir,
                                 const AmmoType* type, bool enemyDammage) override;
    void FireWeapon(EntityAI* vehicle, int weapon, const Magazine* magazine, EntityAI* target) override;
    void UpdateWeapons(EntityAI* vehicle) override;
    void AddWeaponCargo(VehicleSupply* vehicle, RString weapon) override;
    void RemoveWeaponCargo(VehicleSupply* vehicle, RString weapon) override;
    void AddMagazineCargo(VehicleSupply* vehicle, const Magazine* magazine) override;
    void RemoveMagazineCargo(VehicleSupply* vehicle, int creator, int id) override;
    void VehicleInit(VehicleInitCmd& init) override;
    void OnVehicleDestroyed(EntityAI* killed, EntityAI* killer) override;
    void OnVehicleDamaged(EntityAI* damaged, EntityAI* killer, float damage, RString ammo) override;
    void OnIncomingMissile(EntityAI* target, RString ammo, EntityAI* owner) override;
    void MarkerCreate(int channel, AIUnit* sender, RefArray<NetworkObject>& units, ArcadeMarkerInfo& info) override;
    void MarkerDelete(RString name) override;
    void SetFlagOwner(Person* owner, EntityAI* carrier) override;
    void SetFlagCarrier(Person* owner, EntityAI* carrier) override;
    void SendRadioMessage(NetworkSimpleObject* msg) override;

    void PublicVariable(RString name) override;
    void Chat(int channel, RString text) override;
    void Chat(int channel, AIUnit* sender, RefArray<NetworkObject>& units, RString text) override;
    void Chat(int channel, RString sender, RefArray<NetworkObject>& units, RString text) override;
    void RadioChat(int channel, AIUnit* sender, RefArray<NetworkObject>& units, RString text,
                   RadioSentence& sentence) override;
    void RadioChatWave(int channel, RefArray<NetworkObject>& units, RString wave, AIUnit* sender,
                       RString senderName) override;
    void SetVoiceChannel(int channel) override;
    void SetVoiceChannel(int channel, RefArray<NetworkObject>& units) override;

    void TransferFile(RString dest, RString source) override;
    void SendMissionFile() override;
    void GetTransferStats(int& curBytes, int& totBytes) override;

    void ShowTarget(Person* vehicle, TargetType* target) override;
    void ShowGroupDir(Person* vehicle, Vector3Par dir) override;

    void GroupSynchronization(AIGroup* grp, int synchronization, bool active) override;
    void DetectorActivation(Detector* det, bool active) override;

    void AskForCreateUnit(AIGroup* group, RString type, Vector3Par position, RString init, float skill,
                          Rank rank) override;
    void AskForDeleteVehicle(Entity* veh) override;
    void AskForReceiveUnitAnswer(AIUnit* from, AISubgroup* to, int answer) override;
    void AskForGroupRespawn(Person* person, EntityAI* killer) override;
    void AskForActivateMine(Mine* mine, bool activate) override;
    void AskForInflameFire(Fireplace* fireplace, bool fire) override;
    void AskForAnimationPhase(Entity* vehicle, RString animation, float phase) override;
    void CopyUnitInfo(Person* from, Person* to) override;

    RespawnMode GetRespawnMode() const override;
    float GetRespawnDelay() const override;
    void Respawn(Person* soldier, Vector3Par pos) override;

    bool ProcessCommand(RString command) override;

    void SendKick(int player) override;
    void SendLockSession(bool lock = true) override;

    bool CanSelectMission() const override;
    bool CanVoteMission() const override;
    const AutoArray<RString>& GetServerMissions() const override;
    void SelectMission(RString mission, bool cadetMode) override;
    void VoteMission(RString mission, bool cadetMode) override;

    void UpdateObject(NetworkObject* object) override;

    // Cancel _dp->EnumHost operation
    void StopEnumHosts();
    // Start _dp->EnumHost operation
    bool StartEnumHosts();

    // Return original name of local player (without decoration used when several players vith the same name are
    // connected)
    RString GetLocalPlayerName() const;

    Poseidon::Foundation::Time GetEstimatedEndTime() const override;
    void SetEstimatedEndTime(Poseidon::Foundation::Time time) override;

    void DisposeBody(Person* body) override;
    bool IsControlsPaused() override;
    float GetLastMsgAgeReliable() override;
};

extern NetworkManager GNetworkManager;
extern NetworkMessageFormat* GMsgFormats[NMTN];

// Machine-readable per-connection snapshot used by the headless harness
// to inspect server-side network state (latency / throughput / jip).
struct NetworkConnectionSnapshot
{
    int dpid;
    RString name;
    NetworkGameState state;
    bool jip;
    bool botClient;
    bool hasConnectionInfo;
    int latencyMs;
    int throughputBps;
    int avgPingMs;
    int avgBandwidthBps;
};

extern RString ServerTmpDir;
RString GetServerTmpDir();

// total number of diagnostic types
const int nOutputDiags = 4;
extern int outputDiags;
extern bool outputLogs;

// transfer parameters
// When update error is bellow this threshold, update message is not sent
extern float MinErrorToSend;
// Maximal number of messages sent in single simulation step
extern int MaxMsgSend;
// Maximal number of messages sent in single simulation step on dedicated server
extern int DSMaxMsgSend;
// Maximal size of agregated guaranteed message
extern int MaxSizeGuaranteed;
// Maximal size of agregated guaranteed message
extern int MaxSizeNonguaranteed;
// Limit (minimum) for bandwidth estimation
extern int MinBandwidth;
// Limit (minimum) for bandwidth estimation on dedicated server
extern int DSMinBandwidth;
// Limit (maximum) for bandwidth estimation
extern int MaxBandwidth;
// How many guarenteed message over current bandwith estimation
// should be passed to low-level network layer if necessary
// Increasing this value may make mision download faster
extern float ThrottleGuaranteed;

// == default MinErrorToSend / 2, after 2 s update is forced
#define ERR_COEF_TIME_POSITION 0.005
// == default MinErrorToSend / 5, after 5 s update is forced
#define ERR_COEF_TIME_GENERIC 0.002

#define MSGID_REPLACE 0

void DiagLogF(const char* format, ...);
void WriteDiagOutput(bool server);
int GetDiagLevel(NetworkMessageType type, bool remote);

#ifdef _WIN32
HRESULT WINAPI ReceiveMessage(void* context, DWORD type, void* message);
#endif
void InitMsgFormats();
unsigned int IntegrityCheckAnswer(IntegrityQuestionType type, const IntegrityQuestion& q, bool server = false);
const char* FormatVal(float val, char* buffer);

RString CreateMPMissionBank(RString filename, RString island);
void RemoveBank(const char* prefix);

int GetNetworkPort();

namespace Poseidon
{
void DeleteDirectoryStructure(const char* name, bool deleteDir);
}
namespace Poseidon
{
void CreatePath(RString path);
}
using ::Poseidon::CreatePath;
namespace Poseidon
{
bool ParseMission(bool multiplayer);
}

// Release COM object
#define SAFE_RELEASE(p)     \
    {                       \
        if (p)              \
        {                   \
            (p)->Release(); \
            (p) = 0;        \
        }                   \
    }

// Multiplayer version info

#include <Poseidon/Foundation/Platform/VersionNo.h>

// Actual multiplayer version
#define MP_VERSION_ACTUAL APP_VERSION_NUM
// The earliest supported multiplayer version
#define MP_VERSION_REQUIRED MP_VERSION_ACTUAL

// Integrity question message
// This message is sent by server to clients to check consistency of their data (to avoid cheaters).
#define INTEGRITY_QUESTION_MSG(XX) \
	XX(int, id, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Unique id of question"), IdxTransfer) \
	XX(int, type, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Type of question"), IdxTransfer) \
	XX(RString, name, NDTString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Question name"), IdxTransfer) \
	XX(int, offset, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Region in question"), IdxTransfer) \
	XX(int, size, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Region in question"), IdxTransfer)

DECLARE_NET_MESSAGE(IntegrityQuestion, INTEGRITY_QUESTION_MSG)

// Answer to integrity question message
#define INTEGRITY_ANSWER_MSG(XX) \
	XX(int, id, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Unique id of question"), IdxTransfer) \
	XX(int, type, NDTInteger, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Type of question"), IdxTransfer) \
	XX(int, answer, NDTInteger, NCTNone, DEFVALUE(int, 0), DOC_MSG("Answer value (CRC of selected data)"), IdxTransfer)

DECLARE_NET_MESSAGE(IntegrityAnswer, INTEGRITY_ANSWER_MSG)

// Message used for distribution of players' states to clients
#define PLAYER_STATE_MSG(XX) \
	XX(int, player, NDTInteger, NCTNone, DEFVALUE(int, AI_PLAYER), DOC_MSG("Client (player) ID"), IdxTransfer) \
	XX(int, state, NDTInteger, NCTNone, DEFVALUE(int, NGSNone), DOC_MSG("New state of player"), IdxTransfer)

DECLARE_NET_MESSAGE(PlayerState, PLAYER_STATE_MSG)

#define ATTACH_PERSON_MSG(XX) \
	XX(OLink<Person>, person, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Person to attach"), IdxTransferRef) \
	XX(OLink<AIUnit>, unit, NDTRef, NCTNone, DEFVALUENULL, DOC_MSG("Unit to attach"), IdxTransferRef)

DECLARE_NET_MESSAGE(AttachPerson, ATTACH_PERSON_MSG)

// Network indices live in networkIndices.hpp
#include <Poseidon/Network/NetworkIndices.hpp>

inline bool NetworkClient::IsBotClient() const
{
    return _parent->IsServer();
}
