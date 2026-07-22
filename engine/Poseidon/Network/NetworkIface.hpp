#pragma once

#include <Poseidon/Network/NetworkObject.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Network/NetTransport.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Foundation/Time/Time.hpp>
#include <Poseidon/AI/VehicleAI.hpp>

namespace Poseidon { class Command; }

#define NO_PLAYER								0
#define AI_PLAYER								1

#define STATIC_OBJECT						1

#if _ENABLE_CHEATS
#define DOCUMENT_MSG_FORMATS		0
#else
#define DOCUMENT_MSG_FORMATS		0
#endif

#if DOCUMENT_MSG_FORMATS
#define DOC_MSG(text) text
#else
#define DOC_MSG(text) ""
#endif

DECL_ENUM(NetworkMessageErrorType)

struct MissionHeader;
struct PlayerRole;
struct PlayerIdentity;

namespace Poseidon { DECL_ENUM(VehicleListType) } // enum is defined as Poseidon::VehicleListType (World.hpp)
namespace Poseidon { class VehicleSupply; }
namespace Poseidon { class Transport; }
namespace Poseidon { class Detector; }
using Poseidon::Detector;
namespace Poseidon { class EntityAI; }
typedef EntityAI TargetType;
namespace Poseidon { class Mine; }
namespace Poseidon { class Fireplace; }

namespace Poseidon { class RadioSentence; }
using Poseidon::RadioSentence;

namespace Poseidon { struct VehicleInitCmd; }

#define CHECK_CAST(dst, src, dsttype) \
	dsttype *dst = static_cast<dsttype *>(src); \
	NET_ERROR(dynamic_cast<dsttype *>(src) != nullptr);

#define CHECK_ASSIGN(dst, src, dsttype) \
	dsttype &dst = static_cast<dsttype &>(src);

// Interface for network manager object

// Basic info about multiplayer player
struct NetPlayerInfo
{
	// Unique DirectPlay ID of player
	int dpid;
	// name of player
	RString name;
};

// States of multiplayer game
enum NetworkGameState
{
	// no game or exiting game
	NGSNone,
	// game is creating (message formats are not registered yet)
	NGSCreating,
	// game is created, messages can be sent
	NGSCreate,
	// player is logged into game (his identity is created)
	NGSLogin,
	// server is editting mission
	NGSEdit,
	// player voted mission
	NGSMissionVoted,
	// players are assigned to sides
	NGSPrepareSide,
	// players are assigned to roles
	NGSPrepareRole,
	// player is already assigned
	NGSPrepareOK,
	// mission debriefing
	NGSDebriefing,
	// mission debriefing is already read
	NGSDebriefingOK,
	// transfering mission file
	NGSTransferMission,
	// loading island
	NGSLoadIsland,
	// briefing screen
	NGSBriefing,
	// game is already running
	NGSPlay,
};

// Positions in vehicle to get in
enum GetInPosition
{
	// anywhere
	GIPAny,
	// to commander position
	GIPCommander,
	// to driver / pilot position
	GIPDriver,
	// to gunner position
	GIPGunner,
	// back
	GIPCargo
};

// Respawn mode
enum RespawnMode
{
	RespawnNone,
	RespawnSeaGull,
	RespawnAtPlace,
	RespawnInBase,
	RespawnToGroup, // respawn to group member, leader to leader, other to other
	RespawnToFriendly, // respawn to any playable friendly unit
};

// State of sound
enum SoundStateType
{
	SSRestart,
	SSStop,
	SSRepeat,
	SSLastLoop,
};

// Quality of connection
enum ConnectionQuality
{
	CQGood,
	CQPoor,
	CQBad
};

namespace Poseidon { class Magazine; }
using Poseidon::Command;
using Poseidon::VehicleListType;
using Poseidon::VehicleSupply;
using Poseidon::Mine;
using Poseidon::Fireplace;
using Poseidon::VehicleInitCmd;
using Poseidon::Magazine;
using Poseidon::Rank;

//enum UIActionType;

enum KickOffReason
{
	KORKick,
	KORBan,
	KORFade,
	KORAddon,
	KOROther
};

// Interface to basic network functions
class INetworkManager
{
public:
	// Initialize multiplayer, listen on given ip address and port
	virtual bool Init(RString ip, int port, bool startEnum = true)= 0;
	// Finish multiplayer
	virtual void Done() = 0;
//	virtual void GetSessions(AutoArray<SessionInfo, MemAllocSA> &sessions) = 0;
	// Retrieve list of sessions
	virtual void GetSessions(AutoArray<SessionInfo> &sessions) = 0;
	// Probe known remote hosts and refresh session metadata such as ping.
	virtual bool ProbeRemoteHosts(AutoArray<RemoteHostAddress> &hosts, int port) = 0;
	// Transfer IP address and port into DirectPlay URL address
	virtual RString IPToGUID(RString ip, int port) = 0;
	// Create multiplayer game (server + client)
	virtual bool CreateSession(int port, RString password) = 0;
	// Join to multiplayer game (client only)
	virtual ConnectResult JoinSession(RString guid, RString password) = 0;
	// Wait until session is created on given ip and port
	virtual bool WaitForSession() = 0;
	// Retrieve list of players
	virtual void GetPlayers(AutoArray<NetPlayerInfo, Poseidon::Foundation::MemAllocSA> &players) = 0;
	// Creates pbo file for selected mission (on server)
	virtual void CreateMission(RString mission, RString world) = 0;
	// Initialize mission
	// create and send mission header structure (including difficulty settings)
	// create and send slots for sides and roles
	virtual void InitMission(bool cadetMode) = 0;
	// Close multiplayer session, listen to sessions again
	virtual void Close() = 0;
	// Network simulation - called onced during every frame
	virtual void OnSimulate() = 0;
	// Return if server is created
	virtual bool IsServer() const = 0;
	// Return if local player joined in progress (JIP)
	virtual bool IsJIP() const { return false; }
	// Save world state to file
	virtual bool SaveWorldState(const char* filename) { return false; }
	// Load world state from file
	virtual bool LoadWorldState(const char* filename) { return false; }
	// Kick off given player from game
	virtual void KickOff(int dpnid, KickOffReason reason) = 0;
	// Ban given player (kick off + add to dynamic ban list)
	virtual void Ban(int dpnid) = 0;
	// disable/enable connecting of further clients
	virtual void LockSession(bool lock = true)= 0;

	// perform regular memory clean-up
	virtual unsigned CleanUpMemory() {return 0;}

	// Return current state of game
	virtual NetworkGameState GetGameState() const = 0;
	// Return current state on server
	virtual NetworkGameState GetServerState() const = 0;
	// Return true if server state ever reached NGSPlay during this session
	virtual bool WasServerPlaying() const { return false; }
	// Set current state of game (on server + send to clients)
	virtual void SetGameState(NetworkGameState state) = 0;
	// Check if client is logged into dedicated server as game master
	virtual bool IsGameMaster() const = 0;
	// Return if gamemaster is only voted admin (cannot do shutdown etc.)
	virtual bool IsAdmin() const = 0;
	// True if an admin/game master is currently logged in: on a server, anyone; on a
	// client, the local player. Lets a test observe an admin grant from either side.
	virtual bool HasAdminLoggedIn() const { return false; }
	// Return connection quality for client
	virtual ConnectionQuality GetConnectionQuality() const = 0;
	// Retrieves parameters for current mission
	virtual void GetParams(float &param1, float &param2) const = 0;
	// Set parameters for current mission
	virtual void SetParams(float param1, float param2) = 0;
	// Return mission header
	virtual const MissionHeader *GetMissionHeader() const = 0;
	// Return id of player on client
	virtual int GetPlayer() const = 0;
	// Return number of role slots for current mission
	virtual int NPlayerRoles() const = 0;
	// Return given role slot
	virtual const PlayerRole *GetPlayerRole(int role) const = 0;
	// Return role slot for local client
	virtual const PlayerRole *GetMyPlayerRole() const = 0;
	// Search for identity with given player id
	virtual const PlayerIdentity *FindIdentity(int dpnid) const = 0;
	// Return array of identities
	virtual const AutoArray<PlayerIdentity> *GetIdentities() const = 0;

	// Assign player to role slot
	virtual void AssignPlayer(int role, int player) = 0;
	// Unassign role slot
	virtual void UnassignPlayer(int role) = 0;
	// Select person as player
	virtual void SelectPlayer(int player, Person *person, bool respawn = false)= 0;
	// Play sound on all clients (except itself)
	virtual void PlaySound
	(
		RString name, Vector3Par position, Vector3Par speed,
		float volume, float freq, IWave *wave
	) = 0;
	// Change state of played sound on all clients (except itself)
	virtual void SoundState(IWave *wave, SoundStateType state) = 0;
	// Return game state for given player
	virtual NetworkGameState GetPlayerState(int dpid) = 0;
	// Return name for given player
	virtual RString GetPlayerName(int dpid) = 0;
	// Return remote VoN speakers currently tracked by the local client.
	virtual void GetVoiceSpeakers(AutoArray<NetVoiceSpeakerInfo, Poseidon::Foundation::MemAllocSA> &speakers) = 0;
	// Local transmit pipeline health, Poseidon::VoNTransmitHealth as int
	// (0 = Off / voice not initialized).
	virtual int GetVoiceTransmitHealth() = 0;
	// Return camera position for given player
	virtual Vector3 GetCameraPosition(int dpid) = 0;
	// Return object by id
	virtual NetworkObject *GetObject(NetworkId& id) = 0;

	// Register vehicle on client and send message to server (and other clients)
	virtual bool CreateVehicle
	(
		Vehicle *veh, VehicleListType type, RString name, int idVeh
	) = 0;
	// Register whole AI structure for given AICenter (center, groups, subgroups, units)
	virtual bool CreateCenter(AICenter *center) = 0;	// whole AI structure
	// Register generic network object and send message to server (and other clients)
	virtual bool CreateObject(NetworkObject *object) = 0;
	// Send first update of all created objects over network (as guaranteed message)
	virtual void CreateAllObjects() = 0;
	// Unregister given network object, send message to others
	virtual void DeleteObject(NetworkId &id) = 0;
	// Unregister all objects, both local and remote
	virtual void DestroyAllObjects() = 0;
	// Register command as network object, send message to others
	virtual bool CreateCommand(AISubgroup *subgrp, int index, Command *cmd) = 0;
	// Unregister command, send message to others
	virtual void DeleteCommand(AISubgroup *subgrp, int index, Command *cmd) = 0;
	// Send message to server that player changed his state
	virtual void ClientReady(NetworkGameState state) = 0;
	// Request code execution
	virtual bool PublicExec(RString command) = 0;
	// Targeted named remote execution
	virtual bool RemoteExec(RString name, const AutoArray<char>& params, int target,
	                        const AutoArray<char>& targetSpec, bool jip, RString jipKey, bool callMode)
	{
		return false;
	}
	virtual bool RemoteExecRemove(RString jipKey) { return false; }
	// Ask object owner for damage of object
	virtual void AskForDammage
	(
		Object *who, EntityAI *owner,
		Vector3Par modelPos, float val, float valRange, RString ammo
	) = 0;
	// Ask object owner for set of total damage of object
	virtual void AskForSetDammage
	(
		Object *who, float dammage
	) = 0;
	// Ask vehicle owner for get in person
	virtual void AskForGetIn
	(
		Person *soldier, Transport *vehicle,
		GetInPosition position
	) = 0;
	// Ask vehicle owner for get out person
	virtual void AskForGetOut
	(
		Person *soldier, Transport *vehicle, bool parachute
	) = 0;
	// Ask vehicle owner for change person position
	virtual void AskForChangePosition
	(
		Person *soldier, Transport *vehicle, UIActionType type
	) = 0;
	// Ask vehicle owner for aim weapon
	virtual void AskForAimWeapon
	(
		EntityAI *vehicle, int weapon, Vector3Par dir
	) = 0;
	// Ask vehicle owner for aim observer turret
	virtual void AskForAimObserver
	(
		EntityAI *vehicle, Vector3Par dir
	) = 0;
	// Ask vehicle owner for select weapon
	virtual void AskForSelectWeapon
	(
		EntityAI *vehicle, int weapon
	) = 0;
	// Ask vehicle owner for change ammo state
	virtual void AskForAmmo
	(
		EntityAI *vehicle, int weapon, int burst
	) = 0;
	// Ask vehicle owner for add impulse
	virtual void AskForAddImpulse
	(
		Vehicle *vehicle, Vector3Par force, Vector3Par torque
	) = 0;
	// Ask object owner for move object
	virtual void AskForMove
	(
		Object *vehicle, Vector3Par pos
	) = 0;
	// Ask object owner for move object
	virtual void AskForMove
	(
		Object *vehicle, Matrix4Par trans
	) = 0;
	// Ask group owner for join groups
	virtual void AskForJoin
	(
		AIGroup *join, AIGroup *group
	) = 0;
	// Ask group owner for join groups
	virtual void AskForJoin
	(
		AIGroup *join, OLinkArray<AIUnit> &units
	) = 0;
	// Ask person owner for hide body
	virtual void AskForHideBody(Person *vehicle) = 0;
	// Transfer explosion effects (explosion, smoke, etc.) to other clients
	virtual void ExplosionDammageEffects
	(
		EntityAI *owner, Shot *shot,
		Object *directHit, Vector3Par pos, Vector3Par dir, const AmmoType *type,
		bool enemyDammage
	) = 0;
	// Transfer fire effects (sound, fire, smoke, recoil effect, etc.) to other clients
	virtual void FireWeapon
	(
		EntityAI *vehicle, int weapon, const Magazine *magazine, EntityAI *target
	) = 0;
	// Send update of weapons to vehicle owner
	virtual void UpdateWeapons(EntityAI *vehicle) = 0;
	// Ask vehicle to add weapon into cargo
	virtual void AddWeaponCargo(VehicleSupply *vehicle, RString weapon) = 0;
	// Ask vehicle to remove weapon from cargo
	virtual void RemoveWeaponCargo(VehicleSupply *vehicle, RString weapon) = 0;
	// Ask vehicle to add magazine into cargo
	virtual void AddMagazineCargo(VehicleSupply *vehicle, const Magazine *magazine) = 0;
	// Ask vehicle to remove magazine from cargo
	virtual void RemoveMagazineCargo(VehicleSupply *vehicle, int creator, int id) = 0;
	// Transfer init expression to other clients and execute it
	virtual void VehicleInit(VehicleInitCmd &init) = 0;
	// Transfer message who is responsible to destroy vehicle to other clients
	virtual void OnVehicleDestroyed(EntityAI *killed, EntityAI *killer) = 0;
	// Transfer message about damage of vehicle to other clients
	virtual void OnVehicleDamaged(EntityAI *damaged, EntityAI *killer, float damage, RString ammo) = 0;
	// Transfer message about fired missile to other clients
	virtual void OnIncomingMissile(EntityAI *target, RString ammo, EntityAI *owner) = 0;
	// Transfer info about (user made) marker creation to other clients
	virtual void MarkerCreate(int channel, AIUnit *sender, RefArray<NetworkObject> &units, ArcadeMarkerInfo &info) = 0;
	// Transfer info about (user made) marker was deleted to other clients
	virtual void MarkerDelete(RString name) = 0;
	// Ask flag (carrier) owner for assign new owner
	virtual void SetFlagOwner
	(
		Person *owner, EntityAI *carrier
	) = 0;
	// Ask client owns flag owner for change of flag ownership
	virtual void SetFlagCarrier
	(
		Person *owner, EntityAI *carrier
	) = 0;
	// Send radio message to other clients
	// Sending a whole radio message is implemented only for vehicle messages.
	virtual void SendRadioMessage(NetworkSimpleObject *msg) = 0;
	// Public variable to other clients
	virtual void PublicVariable(RString name) = 0;
	// Send chat message
	virtual void Chat(int channel, RString text) = 0;
	// Send chat message
	virtual void Chat(int channel, AIUnit *sender, RefArray<NetworkObject> &units, RString text) = 0;
	// Send chat message
	virtual void Chat(int channel, RString sender, RefArray<NetworkObject> &units, RString text) = 0;
	// Send radio message as chat (text and sentence)
	virtual void RadioChat(int channel, AIUnit *sender, RefArray<NetworkObject> &units, RString text, RadioSentence &sentence) = 0;
	// Send text radio message as chat (sound and title)
	virtual void RadioChatWave(int channel, RefArray<NetworkObject> &units, RString wave, AIUnit *sender, RString senderName) = 0;
	// Set channel for Voice Over Net
	virtual void SetVoiceChannel(int channel) = 0;
	// Set channel for Voice Over Net
	virtual void SetVoiceChannel(int channel, RefArray<NetworkObject> &units) = 0;
	// Transfer file to other clients
	virtual void TransferFile(RString dest, RString source) = 0;
	// Server send mission pbo file to clients
	virtual void SendMissionFile() = 0;
	// Retrieve state of file transfer
	virtual void GetTransferStats(int &curBytes, int &totBytes) = 0;
	// Returns respawn mode
	virtual RespawnMode GetRespawnMode() const = 0;
	// Returns respawn delay
	virtual float GetRespawnDelay() const = 0;
	// Add person to respawn queue (respawn after delay)
	virtual void Respawn(Person *soldier, Vector3Par pos) = 0;
	// Process chat command for remote control of dedicated server
	virtual bool ProcessCommand(RString command) = 0;
	// Ask server to kick off player
	virtual void SendKick(int player) = 0;
	// Ask server to disable/enable connection of further clients
	virtual void SendLockSession(bool lock = true)= 0;

	// Checks if gamemaster is to select mission on dedicated server
	virtual bool CanSelectMission() const = 0;
	// Checks if client is to vote mission on dedicated server
	virtual bool CanVoteMission() const = 0;
	// Returns array of missions available on dedicated server
	virtual const AutoArray<RString> &GetServerMissions() const = 0;
	// Select mission on dedicated server
	virtual void SelectMission(RString mission, bool cadetMode) = 0;
	// Vote mission on dedicated server
	virtual void VoteMission(RString mission, bool cadetMode) = 0;
	// Force update of network object
	virtual void UpdateObject(NetworkObject *object) = 0;
	// Ask player to show target
	virtual void ShowTarget(Person *vehicle, TargetType *target) = 0;
	// Ask player to show group direction
	virtual void ShowGroupDir(Person *vehicle, Vector3Par dir) = 0;
	// Transfer activation of group synchronization
	virtual void GroupSynchronization(AIGroup *grp, int synchronization, bool active) = 0;
	// Transfer activation of detector (through radio)
	virtual void DetectorActivation(Detector *det, bool active) = 0;
	// Ask group owner to create new unit
	virtual void AskForCreateUnit(AIGroup *group, RString type, Vector3Par position, RString init, float skill, Rank rank) = 0;
	// Ask vehicle owner to destroy vehicle
	virtual void AskForDeleteVehicle(Entity *veh) = 0;
	// Ask subgroup to receive answer from unit
	virtual void AskForReceiveUnitAnswer
	(
		AIUnit *from, AISubgroup *to, int answer
	) = 0;
	// Ask group owner to respawn player
	virtual void AskForGroupRespawn(Person *person, EntityAI *killer) = 0;
	// Ask mine owner to activate it
	virtual void AskForActivateMine(Mine *mine, bool activate) = 0;
	// Ask fireplace to inflame / put down
	virtual void AskForInflameFire(Fireplace *fireplace, bool fire) = 0;
	// Ask vehicle for user defined animation
	virtual void AskForAnimationPhase(Entity *vehicle, RString animation, float phase) = 0;
	// Copy unit info from one person to other
	virtual void CopyUnitInfo(Person *from, Person *to) = 0;
	// Return estimated end of mission time
	virtual Poseidon::Foundation::Time GetEstimatedEndTime() const = 0;
	// Set estimated end of mission time
	virtual void SetEstimatedEndTime(Poseidon::Foundation::Time time) = 0;

	// Body can be hidden (for better performance)
	virtual void DisposeBody(Person *body) = 0;

	// Game is paused due to disconnection state of game
	virtual bool IsControlsPaused() = 0;

	// Last received message's age in seconds (used to eliminate "disconnect cheat")
	virtual float GetLastMsgAgeReliable() = 0;
};

// Return networn manager class
INetworkManager &GetNetworkManager();

// Interface to basic network component (client or server)
class INetworkComponent
{
public:
	virtual ~INetworkComponent() {}
	// Return message format of given message type
	virtual NetworkMessageFormatBase *GetFormat(/*DWORD client, */int type) = 0;
	// Return network object with given id
	virtual NetworkObject *GetObject(NetworkId &id) = 0;
};
